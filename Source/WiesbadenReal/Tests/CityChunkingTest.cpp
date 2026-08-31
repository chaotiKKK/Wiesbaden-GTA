// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenCityChunking.h"
#include "GIS/WiesbadenWorldBuilder.h"

/**
 * Aufteilung der Stadt-Geometrie in World-Partition-faehige Chunk-Zellen
 * (FWiesbadenCityChunking::BuildChunks):
 *  1. Sections werden anhand des Dreiecks-Schwerpunkts einer Grid-Zelle
 *     zugeordnet (CellSizeCm) - ein Actor pro Zelle streamt statt eines
 *     monolithischen Riesen-Actors.
 *  2. Index-Remap: Ein Vertex, der in mehreren Dreiecken derselben Zelle
 *     vorkommt, wird nur einmal kopiert (geteilte Kanten bleiben konsistent).
 *  3. Kanal/Surface/MaterialVariant/FacadeOverrideKey bleiben je Section
 *     erhalten (Material-Lookup im WorldBuilder funktioniert unveraendert).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityChunkingTest,
	"WiesbadenReal.Core.CityChunking",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** Quad (2 Dreiecke, geteilte Kante) mit 4 Vertices an einem Welt-Offset. */
	FRoadMeshSection MakeQuad(double OffsetX, double OffsetY)
	{
		FRoadMeshSection Section;
		Section.Channel = ERoadMeshChannel::Carriageway;
		Section.Surface = EOSMSurfaceType::Asphalt;

		Section.Vertices.Add(FVector(OffsetX + 0.0, OffsetY + 0.0, 0.0));
		Section.Vertices.Add(FVector(OffsetX + 100.0, OffsetY + 0.0, 0.0));
		Section.Vertices.Add(FVector(OffsetX + 100.0, OffsetY + 100.0, 0.0));
		Section.Vertices.Add(FVector(OffsetX + 0.0, OffsetY + 100.0, 0.0));

		Section.Triangles = { 0, 1, 2, 0, 2, 3 };
		Section.Normals.Init(FVector::UpVector, 4);
		Section.UVs.Init(FVector2D::ZeroVector, 4);
		return Section;
	}
}

bool FCityChunkingTest::RunTest(const FString& Parameters)
{
	// -- 1. Zwei getrennte Zellen --------------------------------------------
	{
		FRoadMeshData RoadMesh;
		RoadMesh.Sections.Add(MakeQuad(0.0, 0.0));            // Zelle (0,0)
		RoadMesh.Sections.Add(MakeQuad(50000.0, 0.0));        // Schwerpunkt (50050,50) -> Zelle (1,0)

		TMap<FIntPoint, FCityChunkMesh> Chunks;
		FWiesbadenCityChunking::BuildChunks(RoadMesh, FBuildingMeshData(), FRegionAssetLayout(), 50000.0 /*cm*/, Chunks);

		TestEqual(TEXT("Zwei Zellen erzeugt"), Chunks.Num(), 2);
		TestTrue(TEXT("Zelle (0,0) vorhanden"), Chunks.Contains(FIntPoint(0, 0)));
		TestTrue(TEXT("Zelle (1,0) vorhanden"), Chunks.Contains(FIntPoint(1, 0)));

		const FCityChunkMesh& A = Chunks[FIntPoint(0, 0)];
		TestEqual(TEXT("Zelle A: eine Road-Section"), A.RoadSections.Num(), 1);
		TestEqual(TEXT("Zelle A: 4 Vertices (Index-Remap, geteilte Kante)"),
			A.RoadSections[0].Vertices.Num(), 4);
		TestEqual(TEXT("Zelle A: 2 Dreiecke"), A.RoadSections[0].Triangles.Num(), 6);
		TestEqual(TEXT("Zelle A: Kanal erhalten"), A.RoadSections[0].Channel, ERoadMeshChannel::Carriageway);
		TestEqual(TEXT("Zelle A: Surface erhalten"), A.RoadSections[0].Surface, EOSMSurfaceType::Asphalt);
	}

	// -- 2. Zellgrenze: Schwerpunkt exakt auf Vielfachem -> naechste Zelle ---
	{
		FRoadMeshData RoadMesh;
		// Ein Dreieck mit X-Schwerpunkt exakt 50000 (Mittelwert der drei X):
		// (49950+50000+50050)/3 = 50000 -> 50000/50000 = 1.0 -> Floor = Zelle
		// (1,0), nicht (0,0).
		FRoadMeshSection Section;
		Section.Channel = ERoadMeshChannel::Carriageway;
		Section.Surface = EOSMSurfaceType::Asphalt;
		Section.Vertices = {
			FVector(49950.0, 0.0, 0.0),
			FVector(50000.0, 100.0, 0.0),
			FVector(50050.0, 0.0, 0.0)
		};
		Section.Triangles = { 0, 1, 2 };
		Section.Normals.Init(FVector::UpVector, 3);
		Section.UVs.Init(FVector2D::ZeroVector, 3);
		RoadMesh.Sections.Add(Section);

		TMap<FIntPoint, FCityChunkMesh> Chunks;
		FWiesbadenCityChunking::BuildChunks(RoadMesh, FBuildingMeshData(), FRegionAssetLayout(), 50000.0, Chunks);

		TestEqual(TEXT("Grenzfall: genau 1 Zelle"), Chunks.Num(), 1);
		TestTrue(TEXT("Grenzfall: Zelle (1,0) (Floor-Division)"),
			Chunks.Contains(FIntPoint(1, 0)));
	}

	// -- 3. Gebaeude-Section: MaterialVariant + FacadeOverrideKey bleiben -----
	{
		FRoadMeshData RoadMesh;
		FBuildingMeshData BuildingMesh;

		FBuildingMeshSection Wall;
		Wall.Channel = EBuildingMeshChannel::Wall;
		Wall.MaterialVariant = 3; // Glas
		Wall.FacadeOverrideKey = TEXT("Mainzer Strasse 129");
		Wall.Vertices = { FVector(0.0, 0.0, 0.0), FVector(50.0, 0.0, 0.0),
			FVector(50.0, 0.0, 300.0), FVector(0.0, 0.0, 300.0) };
		Wall.Triangles = { 0, 1, 2, 0, 2, 3 };
		Wall.Normals.Init(FVector::ForwardVector, 4);
		Wall.UVs.Init(FVector2D::ZeroVector, 4);
		BuildingMesh.Sections.Add(Wall);

		TMap<FIntPoint, FCityChunkMesh> Chunks;
		FWiesbadenCityChunking::BuildChunks(RoadMesh, BuildingMesh, FRegionAssetLayout(), 50000.0, Chunks);

		TestEqual(TEXT("Gebaeude: 1 Zelle"), Chunks.Num(), 1);
		const FCityChunkMesh& C = Chunks[FIntPoint(0, 0)];
		TestEqual(TEXT("Gebaeude: 1 Building-Section"), C.BuildingSections.Num(), 1);
		TestEqual(TEXT("Gebaeude: MaterialVariant erhalten"),
			C.BuildingSections[0].MaterialVariant, 3);
		TestEqual(TEXT("Gebaeude: FacadeOverrideKey erhalten"),
			C.BuildingSections[0].FacadeOverrideKey, FString(TEXT("Mainzer Strasse 129")));
	}

	// -- 4. Leere Sections + ungueltige Zellgroesse ---------------------------
	{
		FRoadMeshData RoadMesh;
		RoadMesh.Sections.Add(FRoadMeshSection()); // leer -> uebersprungen

		TMap<FIntPoint, FCityChunkMesh> Chunks;
		FWiesbadenCityChunking::BuildChunks(RoadMesh, FBuildingMeshData(), FRegionAssetLayout(), 50000.0, Chunks);
		TestEqual(TEXT("Leere Section uebersprungen"), Chunks.Num(), 0);

		FWiesbadenCityChunking::BuildChunks(RoadMesh, FBuildingMeshData(), FRegionAssetLayout(), 0.0, Chunks);
		TestEqual(TEXT("Zellgroesse 0 -> keine Ausgabe"), Chunks.Num(), 0);
	}

	// -- 5. WorldBuilder-Defaults fuer das Chunking ---------------------------
	{
		const AWiesbadenWorldBuilder* WB = GetDefault<AWiesbadenWorldBuilder>();
		TestNotNull(TEXT("WorldBuilder-CDO vorhanden"), WB);
		TestTrue(TEXT("Chunk-Erzeugung default aktiv (Streaming-Ziel)"), WB->bGenerateCityChunks);
		TestTrue(TEXT("Chunk-Groesse > 0"), WB->CityChunkSizeMeters > 0.0);
	}

	return true;
}
