// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenCityChunking.h"

namespace
{
	/** Fuegt einen Quell-Vertex (Index) in die Zell-Section ein und remappt den Index. */
	template <typename TSection>
	int32 RemapLocalVertex(TSection& Out, const TSection& Src, int32 SrcIndex, TMap<int32, int32>& Remap)
	{
		if (const int32* Existing = Remap.Find(SrcIndex))
		{
			return *Existing;
		}

		const int32 NewIndex = Out.Vertices.Num();
		Out.Vertices.Add(Src.Vertices[SrcIndex]);

		if (Src.Normals.Num() == Src.Vertices.Num()) { Out.Normals.Add(Src.Normals[SrcIndex]); }
		if (Src.UVs.Num() == Src.Vertices.Num()) { Out.UVs.Add(Src.UVs[SrcIndex]); }
		if (Src.VertexColors.Num() == Src.Vertices.Num()) { Out.VertexColors.Add(Src.VertexColors[SrcIndex]); }
		if (Src.Tangents.Num() == Src.Vertices.Num()) { Out.Tangents.Add(Src.Tangents[SrcIndex]); }

		Remap.Add(SrcIndex, NewIndex);
		return NewIndex;
	}

	/**
	 * Teilt eine Quell-Section nach dem Dreieck-Schwerpunkt auf ein Grid auf.
	 * Je Zelle entsteht eine neue Section (Metadaten via CopyMeta uebernommen),
	 * deren Vertices ueber einen Index-Remap konsolidiert werden - geteilte
	 * Kanten innerhalb einer Zelle bleiben verbunden.
	 */
	template <typename TSection>
	TMap<FIntPoint, TSection> SplitSection(
		const TSection& Section,
		double CellSizeCm,
		TFunctionRef<void(const TSection& Src, TSection& Dst)> CopyMeta)
	{
		TMap<FIntPoint, TSection> Result;
		TMap<FIntPoint, TMap<int32, int32>> Remaps;

		const int32 NumTriangles = Section.Triangles.Num() / 3;
		for (int32 Tri = 0; Tri < NumTriangles; ++Tri)
		{
			const int32 A = Section.Triangles[Tri * 3 + 0];
			const int32 B = Section.Triangles[Tri * 3 + 1];
			const int32 C = Section.Triangles[Tri * 3 + 2];
			if (!Section.Vertices.IsValidIndex(A) || !Section.Vertices.IsValidIndex(B) ||
				!Section.Vertices.IsValidIndex(C))
			{
				continue;
			}

			// Der Schwerpunkt entscheidet die Zelle (Floor-Division; ein Dreieck
			// gehoert genau einer Zelle, auch wenn es ueber eine Grenze ragt).
			const FVector Centroid = (Section.Vertices[A] + Section.Vertices[B] + Section.Vertices[C]) / 3.0;
			const FIntPoint Cell(
				FMath::FloorToInt(Centroid.X / CellSizeCm),
				FMath::FloorToInt(Centroid.Y / CellSizeCm));

			TSection& Out = Result.FindOrAdd(Cell);
			if (Out.Vertices.Num() == 0)
			{
				CopyMeta(Section, Out);
			}

			TMap<int32, int32>& Remap = Remaps.FindOrAdd(Cell);
			const int32 NA = RemapLocalVertex(Out, Section, A, Remap);
			const int32 NB = RemapLocalVertex(Out, Section, B, Remap);
			const int32 NC = RemapLocalVertex(Out, Section, C, Remap);

			Out.Triangles.Add(NA);
			Out.Triangles.Add(NB);
			Out.Triangles.Add(NC);
		}

		return Result;
	}
}

void FWiesbadenCityChunking::BuildChunks(
	const FRoadMeshData& RoadMesh,
	const FBuildingMeshData& BuildingMesh,
	const FRegionAssetLayout& RegionAssets,
	double CellSizeCm,
	TMap<FIntPoint, FCityChunkMesh>& OutChunks)
{
	OutChunks.Reset();

	if (CellSizeCm <= 0.0)
	{
		return;
	}

	// Strassen: Metadaten = Kanal + Oberflaechenart (Material-Lookup im
	// WorldBuilder haengt an genau diesen Feldern).
	for (const FRoadMeshSection& Section : RoadMesh.Sections)
	{
		if (Section.IsEmpty())
		{
			continue;
		}

		const TMap<FIntPoint, FRoadMeshSection> CellSections = SplitSection<FRoadMeshSection>(Section, CellSizeCm,
			[](const FRoadMeshSection& Src, FRoadMeshSection& Dst)
			{
				Dst.Channel = Src.Channel;
				Dst.Surface = Src.Surface;
			});

		for (const auto& [Cell, CellSection] : CellSections)
		{
			if (!CellSection.IsEmpty())
			{
				OutChunks.FindOrAdd(Cell).RoadSections.Add(CellSection);
			}
		}
	}

	// Gebaeude: Metadaten = Kanal + MaterialVariant + FacadeOverrideKey
	// (inkl. Adress-/Prompt-Overrides - die Keys gehen durch das Chunking).
	for (const FBuildingMeshSection& Section : BuildingMesh.Sections)
	{
		if (Section.IsEmpty())
		{
			continue;
		}

		const TMap<FIntPoint, FBuildingMeshSection> CellSections = SplitSection<FBuildingMeshSection>(Section, CellSizeCm,
			[](const FBuildingMeshSection& Src, FBuildingMeshSection& Dst)
			{
				Dst.Channel = Src.Channel;
				Dst.MaterialVariant = Src.MaterialVariant;
				Dst.FacadeOverrideKey = Src.FacadeOverrideKey;
			});

		for (const auto& [Cell, CellSection] : CellSections)
		{
			if (!CellSection.IsEmpty())
			{
				OutChunks.FindOrAdd(Cell).BuildingSections.Add(CellSection);
			}
		}
	}

	// Regionsobjekte: nach ihrem Standort in dieselben Zellen einsortieren.
	//
	// Keine Aufteilung wie bei den Meshes noetig - ein Baum ist ein Punkt und
	// gehoert ganz in eine Zelle. Die Zellformel ist bewusst dieselbe wie oben
	// fuer die Dreieck-Schwerpunkte; zwei verschiedene Rundungen wuerden
	// Objekte an den Zellgrenzen in Nachbarzellen legen und beim Streamen
	// sichtbar aufpoppen lassen, waehrend die Strasse darunter schon da ist.
	for (const FPlacedRegionAsset& Asset : RegionAssets.Assets)
	{
		const FIntPoint Cell(
			FMath::FloorToInt(Asset.Location.X / CellSizeCm),
			FMath::FloorToInt(Asset.Location.Y / CellSizeCm));
		FPlacedRegionAsset& Placed = OutChunks.FindOrAdd(Cell).RegionAssets.Add_GetRef(Asset);

		// Regionsname raus. Er hat die Platzierung bestimmt und wird danach
		// nirgends mehr gelesen - beim Aufbau der Instanzen zaehlen nur
		// Kategorie, Ort, Drehung und Streuung. Ueber 1,53 Millionen Objekte
		// sind das rund 75 MB Zeichenketten, die sonst in jeder Zell-Datei
		// mitgespeichert und bei jedem Laden mitgelesen wuerden.
		Placed.RegionName.Empty();
	}
}
