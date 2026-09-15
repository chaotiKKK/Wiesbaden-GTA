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

#include <limits>

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

	// Bounds MUESSEN endlich sein: NaN/uninitialisierte Bounds (der schnelle
	// Build lieferte sie bei einem Teil der Bakes) machen die Komponenten-
	// Weltbounds NaN und stuerzen den Renderer beim ersten Bild ab.
	{
		const FBoxSphereBounds B = Mesh->GetBounds();
		const bool bFinite = !B.Origin.ContainsNaN() && !B.BoxExtent.ContainsNaN()
			&& FMath::IsFinite(B.SphereRadius);
		TestTrue(TEXT("Bounds endlich (kein NaN/Inf)"), bFinite);
		TestTrue(TEXT("Bounds decken die Geometrie (Radius > 0)"), B.SphereRadius > 0.0f);
	}
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

// Eine Section mit einer NaN-Vertexposition (degenerierte Quellgeometrie - beim
// Alkis10-Bake nachgewiesen) darf NICHT in NaN-Bounds resultieren: sonst meldet
// FStaticMeshRenderData::Serialize beim Laden "found NaN in Bounds" und der
// Renderer kann kippen. Der Baker muss solche Sections wie leere fuehren und die
// uebrige (gute) Geometrie behalten.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkStaticMeshBakerNaNTest,
	"WiesbadenReal.GIS.ChunkStaticMeshBaker.DropsNaNSection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkStaticMeshBakerNaNTest::RunTest(const FString& Parameters)
{
	UProceduralMeshComponent* Pm = NewObject<UProceduralMeshComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("ProcMesh angelegt"), Pm))
	{
		return false;
	}

	const TArray<int32> Tris = { 0, 1, 2, 0, 2, 3 };
	const TArray<FVector> Normals = {
		FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector };
	const TArray<FVector2D> UVs = {
		FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
	const TArray<FColor> Colors = {
		FColor::White, FColor::White, FColor::White, FColor::White };
	const TArray<FProcMeshTangent> Tangents;

	// Section 0: sauberes Quad. Section 1: Quad mit EINEM NaN-Vertex.
	const TArray<FVector> Good = {
		FVector(0, 0, 0), FVector(100, 0, 0), FVector(100, 100, 0), FVector(0, 100, 0) };
	Pm->CreateMeshSection(0, Good, Tris, Normals, UVs, Colors, Tangents, /*bCreateCollision=*/false);

	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const TArray<FVector> Bad = {
		FVector(300, 0, 0), FVector(400, 0, 0), FVector(400, 100, 0), FVector(NaN, NaN, NaN) };
	Pm->CreateMeshSection(1, Bad, Tris, Normals, UVs, Colors, Tangents, /*bCreateCollision=*/false);

	const FString PackagePath = TEXT("/Game/Generated/Test/SM_ChunkBakerNaN");
	FString Err;
	UStaticMesh* Mesh = WiesbadenChunkStaticMeshBaker::BakeFromProcMesh(
		Pm, PackagePath, /*bCookComplexCollision=*/false, /*bEnableNanite=*/false, Err);

	if (!TestNotNull(TEXT("StaticMesh erzeugt (gute Section ueberlebt)"), Mesh))
	{
		return false;
	}

	const FBoxSphereBounds B = Mesh->GetBounds();
	const bool bFinite = !B.Origin.ContainsNaN() && !B.BoxExtent.ContainsNaN()
		&& FMath::IsFinite(B.SphereRadius);
	TestTrue(TEXT("Bounds endlich trotz NaN-Section"), bFinite);
	TestTrue(TEXT("Gute Geometrie erhalten (Radius > 0)"), B.SphereRadius > 0.0f);

	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().Delete(*FileName, /*RequireExists=*/false, /*EvenReadOnly=*/true);

	return true;
}

#endif // WITH_EDITOR
