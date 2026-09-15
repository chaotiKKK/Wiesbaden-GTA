// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenChunkStaticMeshBaker.h"

#include "WiesbadenReal.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

#if WITH_EDITOR
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

namespace WiesbadenChunkStaticMeshBaker
{

UStaticMesh* BakeFromProcMesh(UProceduralMeshComponent* Source, const FString& PackagePath,
	bool bCookComplexCollision, bool bEnableNanite, FString& OutError)
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
	// Slot-Namen der PolygonGroups: BuildFromMeshDescriptions ordnet die Render-
	// Sections den Material-Slots UEBER DIESE NAMEN zu. Ohne sie fielen alle
	// Sections auf einen Slot -> die per-Section-Materialzuweisung waere kaputt.
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();

	// Bounds aus den ECHTEN Vertexpositionen mitfuehren: BuildFromMeshDescriptions
	// mit bFastBuild=true schreibt die Mesh-Bounds nicht zuverlaessig (rund die
	// Haelfte der Gebaeude-Bakes kam mit NaN/uninitialisierten ExtendedBounds heraus
	// -> UStaticMeshComponent::CalcBounds liefert NaN-Weltbounds -> Renderer-Crash
	// beim ersten Bild, RendererScene.cpp ContainsNaN). Aus den Positionen berechnet
	// sind die Bounds deterministisch gueltig, unabhaengig vom schnellen Build-Pfad.
	FBox GeoBounds(ForceInit);

	int32 ValidSections = 0;
	int32 NaNSectionsDropped = 0;
	for (int32 S = 0; S < NumSections; ++S)
	{
		const FName SlotName(*FString::Printf(TEXT("Slot%d"), S));
		const FProcMeshSection* Sec = Source->GetProcMeshSection(S);

		// Degenerierte Quellgeometrie mit NaN-Vertexpositionen abfangen: eine
		// einzige NaN-Position schlaegt sowohl in die Mesh-Vertices als auch in
		// GeoBounds durch -> die "sichere" Bounds-Ueberschreibung unten wird selbst
		// NaN -> FStaticMeshRenderData::Serialize meldet beim Laden "found NaN in
		// Bounds" und der Renderer kann kippen. Solche Sections sind ohnehin
		// unsichtbar/kaputt; sie wie eine leere Section fuehren (die gute Geometrie
		// der uebrigen Sections bleibt erhalten).
		bool bSectionHasNaN = false;
		if (Sec)
		{
			for (const FProcMeshVertex& V : Sec->ProcVertexBuffer)
			{
				if (V.Position.ContainsNaN())
				{
					bSectionHasNaN = true;
					break;
				}
			}
		}

		if (!Sec || Sec->ProcIndexBuffer.Num() < 3 || Sec->ProcVertexBuffer.Num() == 0
			|| bSectionHasNaN)
		{
			// Leere/degenerierte Section trotzdem als benannten Slot fuehren, damit
			// die Slot-Indizes zur per-Section-Materialzuweisung passen.
			if (bSectionHasNaN)
			{
				++NaNSectionsDropped;
			}
			SlotNames[MeshDesc.CreatePolygonGroup()] = SlotName;
			continue;
		}

		const FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
		SlotNames[PolyGroup] = SlotName;

		// Vertices dieser Section anlegen; lokale -> globale ID-Abbildung.
		TArray<FVertexID> VertIds;
		VertIds.Reserve(Sec->ProcVertexBuffer.Num());
		for (const FProcMeshVertex& V : Sec->ProcVertexBuffer)
		{
			const FVertexID Vid = MeshDesc.CreateVertex();
			VtxPos[Vid] = FVector3f(V.Position);
			GeoBounds += V.Position;
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

	// Nanite VOR dem Build setzen: die Nanite-Ressourcen (samt Fallback-Mesh)
	// werden in BuildFromMeshDescriptions/PostEditChange miterzeugt und ins Asset
	// serialisiert. Die Stadt besteht aus Zehntausenden statischen Fahrbahn- und
	// Gebaeude-Dreiecken - genau der Fall, fuer den Nanite die Draw-Calls
	// zusammenfasst und das Dreiecks-LOD GPU-seitig aufloest. Nur fuer sichtbare
	// Render-Meshes; das unsichtbare Kollisions-Mesh rendert nie (bEnableNanite=false).
	Mesh->NaniteSettings.bEnabled = bEnableNanite;

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

	// Bounds ZULETZT erzwingen (nach PostEditChange, vor dem Speichern): der
	// schnelle Build liess sie bei einem Teil der Bakes NaN/uninitialisiert.
	// RenderData-Basisbounds hart aus den echten Positionen setzen, dann die
	// davon abgeleiteten ExtendedBounds neu berechnen (die
	// UStaticMeshComponent::CalcBounds -> GetBounds() liest). So kann keine
	// NaN-Weltbound mehr entstehen, die den Renderer beim ersten Bild kippt.
	if (GeoBounds.IsValid)
	{
		const FBoxSphereBounds SafeBounds(GeoBounds);
		if (FStaticMeshRenderData* RD = Mesh->GetRenderData())
		{
			RD->Bounds = SafeBounds;
			Mesh->CalculateExtendedBounds();
		}
		else
		{
			Mesh->SetExtendedBounds(SafeBounds);
		}
	}

	// --- 5) Paket auf die Platte schreiben ---------------------------------
	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());
	// Bestehende Datei ZUERST loeschen, sonst schreibt SavePackage die neue
	// Geometrie NICHT ueber ein vorhandenes .uasset (der Bake baute das Mesh im
	// Speicher, die Platte blieb aber stale - genau daran scheiterte ein Re-Bake
	// stillschweigend, sodass Code-Aenderungen scheinbar wirkungslos blieben und
	// eine alte Bake-Generation als "Korruption" erschien). Ohne Datei schreibt
	// SavePackage sauber neu - per erzwungenem Loeschen + Re-Bake verifiziert.
	if (IFileManager::Get().FileExists(*FileName))
	{
		IFileManager::Get().Delete(*FileName, /*RequireExists=*/false, /*EvenReadOnly=*/true);
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Pkg, Mesh, *FileName, SaveArgs))
	{
		// NICHT den Speicher-Mesh zurueckgeben: sonst haengt der Chunk an einem
		// nie gespeicherten Asset, das Protokoll bleibt still und die Platte
		// stale. Nullptr macht den Fehler in BakeToStaticMeshes sichtbar.
		OutError = FString::Printf(TEXT("SavePackage fehlgeschlagen: %s"), *FileName);
		return nullptr;
	}

	UE_LOG(LogWbCore, Log,
		TEXT("ChunkStaticMeshBaker: %s gebacken (%d Sections, %d NaN-Sections verworfen, ")
		TEXT("Kollision %s, Nanite %s) -> %s"),
		*AssetName, NumSections, NaNSectionsDropped,
		bCookComplexCollision ? TEXT("gekocht") : TEXT("keine"),
		bEnableNanite ? TEXT("an") : TEXT("aus"), *FileName);
	return Mesh;
#endif // WITH_EDITOR
}

} // namespace WiesbadenChunkStaticMeshBaker
