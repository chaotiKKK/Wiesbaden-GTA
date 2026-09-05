// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenChunkStaticMeshBaker.h"

#include "WiesbadenReal.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

#if WITH_EDITOR
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

namespace WiesbadenChunkStaticMeshBaker
{

UStaticMesh* BakeFromProcMesh(UProceduralMeshComponent* Source, const FString& PackagePath,
	bool bCookComplexCollision, FString& OutError)
{
#if !WITH_EDITOR
	OutError = TEXT("BakeFromProcMesh ist nur im Editor verfuegbar (Bake-Pfad).");
	return nullptr;
#else
	if (!Source)
	{
		OutError = TEXT("Kein Quell-ProcMesh.");
		return nullptr;
	}
	const int32 NumSections = Source->GetNumSections();
	if (NumSections <= 0)
	{
		OutError = TEXT("ProcMesh hat keine Sections.");
		return nullptr;
	}

	// --- 1) MeshDescription aus den ProcMesh-Sections aufbauen --------------
	// Eine Section = eine PolygonGroup = ein Material-Slot; die Reihenfolge
	// bleibt wie in ApplyChunk, damit SetMaterial(Index) weiter passt.
	FMeshDescription MeshDesc;
	FStaticMeshAttributes Attr(MeshDesc);
	Attr.Register();
	Attr.GetVertexInstanceUVs().SetNumChannels(1);

	TVertexAttributesRef<FVector3f> VtxPos = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VinNormal = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> VinUV = Attr.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> VinColor = Attr.GetVertexInstanceColors();

	int32 ValidSections = 0;
	for (int32 S = 0; S < NumSections; ++S)
	{
		const FProcMeshSection* Sec = Source->GetProcMeshSection(S);
		if (!Sec || Sec->ProcIndexBuffer.Num() < 3 || Sec->ProcVertexBuffer.Num() == 0)
		{
			// Leere Section trotzdem als Slot fuehren, damit die Indizes passen.
			MeshDesc.CreatePolygonGroup();
			continue;
		}

		const FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();

		// Vertices dieser Section anlegen; lokale -> globale ID-Abbildung.
		TArray<FVertexID> VertIds;
		VertIds.Reserve(Sec->ProcVertexBuffer.Num());
		for (const FProcMeshVertex& V : Sec->ProcVertexBuffer)
		{
			const FVertexID Vid = MeshDesc.CreateVertex();
			VtxPos[Vid] = FVector3f(V.Position);
			VertIds.Add(Vid);
		}

		// Dreiecke: je drei Indizes -> drei VertexInstances + ein Polygon.
		const TArray<uint32>& Idx = Sec->ProcIndexBuffer;
		for (int32 I = 0; I + 2 < Idx.Num(); I += 3)
		{
			const uint32 I0 = Idx[I + 0];
			const uint32 I1 = Idx[I + 1];
			const uint32 I2 = Idx[I + 2];
			if (!VertIds.IsValidIndex(I0) || !VertIds.IsValidIndex(I1) || !VertIds.IsValidIndex(I2))
			{
				continue;
			}

			FVertexInstanceID Vi[3];
			const uint32 Tri[3] = { I0, I1, I2 };
			for (int32 K = 0; K < 3; ++K)
			{
				const FProcMeshVertex& PV = Sec->ProcVertexBuffer[Tri[K]];
				const FVertexInstanceID Vin = MeshDesc.CreateVertexInstance(VertIds[Tri[K]]);
				VinNormal[Vin] = FVector3f(PV.Normal);
				VinUV.Set(Vin, 0, FVector2f(PV.UV0));
				const FLinearColor LC(PV.Color);
				VinColor[Vin] = FVector4f(LC.R, LC.G, LC.B, LC.A);
				Vi[K] = Vin;
			}
			MeshDesc.CreatePolygon(PolyGroup, { Vi[0], Vi[1], Vi[2] });
		}
		++ValidSections;
	}

	if (ValidSections == 0)
	{
		OutError = TEXT("Keine gueltige Geometrie in den Sections.");
		return nullptr;
	}

	// --- 2) Ziel-Paket + StaticMesh anlegen --------------------------------
	UPackage* Pkg = CreatePackage(*PackagePath);
	if (!Pkg)
	{
		OutError = FString::Printf(TEXT("Paket konnte nicht angelegt werden: %s"), *PackagePath);
		return nullptr;
	}
	const FString AssetName = FPackageName::GetShortName(PackagePath);
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Pkg, *AssetName, RF_Public | RF_Standalone);
	Mesh->InitResources();
	Mesh->SetLightingGuid();

	// Material-Slots je Section (Standardmaterial; die City weist ihre echten
	// Materialien ohnehin per SetMaterial/Instanz zu).
	UMaterialInterface* DefaultMat = UMaterial::GetDefaultMaterial(MD_Surface);
	for (int32 S = 0; S < NumSections; ++S)
	{
		Mesh->GetStaticMaterials().Add(FStaticMaterial(
			DefaultMat, FName(*FString::Printf(TEXT("Slot%d"), S))));
	}

	// --- 3) Bauen (Render-Daten serialisiert) ------------------------------
	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bFastBuild = true;
	BuildParams.bCommitMeshDescription = true;
	if (!Mesh->BuildFromMeshDescriptions({ &MeshDesc }, BuildParams))
	{
		OutError = TEXT("BuildFromMeshDescriptions fehlgeschlagen.");
		return nullptr;
	}

	// --- 4) Trimesh-Kollision VORKOCHEN (Complex-as-Simple) ----------------
	if (bCookComplexCollision)
	{
		Mesh->CreateBodySetup();
		if (UBodySetup* BS = Mesh->GetBodySetup())
		{
			BS->BodySetupGuid = FGuid::NewGuid();
			BS->CollisionTraceFlag = CTF_UseComplexAsSimple;
			BS->bGenerateMirroredCollision = false;
			BS->bDoubleSidedGeometry = true;
			BS->InvalidatePhysicsData();
			BS->CreatePhysicsMeshes();   // kocht die Trimesh-Daten ins Asset
		}
	}

	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Mesh);

	// --- 5) Paket auf die Platte schreiben ---------------------------------
	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Pkg, Mesh, *FileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("SavePackage fehlgeschlagen: %s"), *FileName);
		return Mesh; // Asset existiert im Speicher, nur nicht gespeichert.
	}

	UE_LOG(LogWbCore, Log,
		TEXT("ChunkStaticMeshBaker: %s gebacken (%d Sections, Kollision %s) -> %s"),
		*AssetName, NumSections, bCookComplexCollision ? TEXT("gekocht") : TEXT("keine"),
		*FileName);
	return Mesh;
#endif // WITH_EDITOR
}

} // namespace WiesbadenChunkStaticMeshBaker
