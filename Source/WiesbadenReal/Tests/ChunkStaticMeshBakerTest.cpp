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

	// Zwei getrennte Quads = zwei Sections. So laesst sich pruefen, dass jede
	// Section ihren EIGENEN Material-Slot behaelt (Slot-Namen-Zuordnung).
	auto AddQuad = [&](int32 Section, double OffsetX)
	{
		const TArray<FVector> Verts = {
			FVector(OffsetX, 0, 0), FVector(OffsetX + 100, 0, 0),
			FVector(OffsetX + 100, 100, 0), FVector(OffsetX, 100, 0) };
		const TArray<int32> Tris = { 0, 1, 2, 0, 2, 3 };
		const TArray<FVector> Normals = {
			FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector };
		const TArray<FVector2D> UVs = {
			FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
		const TArray<FColor> Colors = {
			FColor::White, FColor::White, FColor::White, FColor::White };
		const TArray<FProcMeshTangent> Tangents;
		Pm->CreateMeshSection(Section, Verts, Tris, Normals, UVs, Colors, Tangents, /*bCreateCollision=*/true);
	};
	AddQuad(0, 0.0);
	AddQuad(1, 300.0);

	const FString PackagePath = TEXT("/Game/Generated/Test/SM_ChunkBakerTest");
	FString Err;
	UStaticMesh* Mesh = WiesbadenChunkStaticMeshBaker::BakeFromProcMesh(
		Pm, PackagePath, /*bCookComplexCollision=*/true, /*bEnableNanite=*/false, Err);

	TestTrue(FString::Printf(TEXT("Kein Fehler ('%s')"), *Err), Err.IsEmpty());
	if (!TestNotNull(TEXT("StaticMesh erzeugt"), Mesh))
	{
		return false;
	}

	// Render-Daten serialisiert (der teure Laufzeit-Proxy-Aufbau entfaellt damit).
	TestTrue(TEXT("RenderData mit LOD vorhanden"),
		Mesh->GetRenderData() != nullptr && Mesh->GetRenderData()->LODResources.Num() > 0);
	// Jede Section behaelt ihren eigenen Material-Slot (Slot-Namen-Zuordnung):
	// zwei Eingabe-Sections -> zwei Material-Slots -> zwei Render-Sections.
	TestEqual(TEXT("Zwei Material-Slots (je Section einer)"), Mesh->GetStaticMaterials().Num(), 2);
	if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
	{
		TestEqual(TEXT("Zwei Render-Sections"),
			Mesh->GetRenderData()->LODResources[0].Sections.Num(), 2);
	}

	// Kollision vorgekocht als Complex-as-Simple-Trimesh.
	UBodySetup* BS = Mesh->GetBodySetup();
	if (TestNotNull(TEXT("BodySetup vorhanden"), BS))
	{
		TestTrue(TEXT("CollisionTraceFlag = UseComplexAsSimple"),
			BS->CollisionTraceFlag == CTF_UseComplexAsSimple);
	}

	// Nanite-Pfad: derselbe Bake mit bEnableNanite=true muss Nanite auf dem Asset
	// setzen und weiter gueltige Render-Daten liefern (Fahrbahn/Gebaeude sind das
	// klassische Nanite-Ziel: viele statische Dreiecke, Draw-Call-Zusammenfassung).
	const FString NanitePath = TEXT("/Game/Generated/Test/SM_ChunkBakerTestNanite");
	FString NaniteErr;
	UStaticMesh* NaniteMesh = WiesbadenChunkStaticMeshBaker::BakeFromProcMesh(
		Pm, NanitePath, /*bCookComplexCollision=*/false, /*bEnableNanite=*/true, NaniteErr);
	TestTrue(FString::Printf(TEXT("Nanite-Bake ohne Fehler ('%s')"), *NaniteErr), NaniteErr.IsEmpty());
	if (TestNotNull(TEXT("Nanite-StaticMesh erzeugt"), NaniteMesh))
	{
		TestTrue(TEXT("NaniteSettings.bEnabled gesetzt"), NaniteMesh->NaniteSettings.bEnabled);
		TestTrue(TEXT("Nanite-Mesh hat Render-Daten"),
			NaniteMesh->GetRenderData() != nullptr && NaniteMesh->GetRenderData()->LODResources.Num() > 0);
	}

	// Aufraeumen: die Test-Assets von der Platte entfernen.
	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().Delete(*FileName, /*RequireExists=*/false, /*EvenReadOnly=*/true);
	const FString NaniteFileName = FPackageName::LongPackageNameToFilename(
		NanitePath, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().Delete(*NaniteFileName, /*RequireExists=*/false, /*EvenReadOnly=*/true);

	return true;
}

#endif // WITH_EDITOR
