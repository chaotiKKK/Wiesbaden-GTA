// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR

#include "GIS/WiesbadenChunkStaticMeshBaker.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

// Baut ein triviales ProcMesh (ein Quad = zwei Dreiecke) und laesst den Baker
// daraus ein StaticMesh mit gekochter Complex-as-Simple-Kollision erzeugen.
// Prueft: Asset entsteht, Render-Daten vorhanden, BodySetup auf Trimesh gesetzt.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkStaticMeshBakerTest,
	"WiesbadenReal.GIS.ChunkStaticMeshBaker.BakeProducesCollisionMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkStaticMeshBakerTest::RunTest(const FString& Parameters)
{
	UProceduralMeshComponent* Pm = NewObject<UProceduralMeshComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("ProcMesh angelegt"), Pm))
	{
		return false;
	}

	// Ein Quad in der XY-Ebene.
	const TArray<FVector> Verts = {
		FVector(0, 0, 0), FVector(100, 0, 0), FVector(100, 100, 0), FVector(0, 100, 0) };
	const TArray<int32> Tris = { 0, 1, 2, 0, 2, 3 };
	const TArray<FVector> Normals = {
		FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector };
	const TArray<FVector2D> UVs = {
		FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
	const TArray<FColor> Colors = {
		FColor::White, FColor::White, FColor::White, FColor::White };
	const TArray<FProcMeshTangent> Tangents;
	Pm->CreateMeshSection(0, Verts, Tris, Normals, UVs, Colors, Tangents, /*bCreateCollision=*/true);

	const FString PackagePath = TEXT("/Game/Generated/Test/SM_ChunkBakerTest");
	FString Err;
	UStaticMesh* Mesh = WiesbadenChunkStaticMeshBaker::BakeFromProcMesh(
		Pm, PackagePath, /*bCookComplexCollision=*/true, Err);

	TestTrue(FString::Printf(TEXT("Kein Fehler ('%s')"), *Err), Err.IsEmpty());
	if (!TestNotNull(TEXT("StaticMesh erzeugt"), Mesh))
	{
		return false;
	}

	// Render-Daten serialisiert (der teure Laufzeit-Proxy-Aufbau entfaellt damit).
	TestTrue(TEXT("RenderData mit LOD vorhanden"),
		Mesh->GetRenderData() != nullptr && Mesh->GetRenderData()->LODResources.Num() > 0);
	TestTrue(TEXT("Material-Slot vorhanden"), Mesh->GetStaticMaterials().Num() >= 1);

	// Kollision vorgekocht als Complex-as-Simple-Trimesh.
	UBodySetup* BS = Mesh->GetBodySetup();
	if (TestNotNull(TEXT("BodySetup vorhanden"), BS))
	{
		TestTrue(TEXT("CollisionTraceFlag = UseComplexAsSimple"),
			BS->CollisionTraceFlag == CTF_UseComplexAsSimple);
	}

	// Aufraeumen: das Test-Asset von der Platte entfernen.
	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().Delete(*FileName, /*RequireExists=*/false, /*EvenReadOnly=*/true);

	return true;
}

#endif // WITH_EDITOR
