// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "UI/WiesbadenWorldMapView.h"

#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "GIS/BuildingGenerator.h"

namespace
{
	// Farben wie in der HUD-Weltkarte.
	const FLinearColor WmBackground(0.04f, 0.05f, 0.07f, 1.0f);
	const FLinearColor WmBuilding(0.13f, 0.14f, 0.16f, 1.0f);
	const FLinearColor WmMinorRoad(0.62f, 0.64f, 0.68f, 0.95f);
	const FLinearColor WmMajorRoad(0.95f, 0.82f, 0.35f, 1.0f);

	/** Ein FCanvasUVTri mit einheitlicher Farbe (weisses Default-Texture). */
	FCanvasUVTri MakeTri(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
	{
		FCanvasUVTri Tri;
		Tri.V0_Pos = A; Tri.V1_Pos = B; Tri.V2_Pos = C;
		Tri.V0_UV = Tri.V1_UV = Tri.V2_UV = FVector2D::ZeroVector;
		Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = Color;
		return Tri;
	}
}

bool UWiesbadenWorldMapView::NeedsRerender(
	bool bHasTarget,
	const FRoadNetwork* InCachedNetwork, const FVector2D& InCachedSize,
	const FRoadNetwork* Network, const FVector2D& Size)
{
	return !bHasTarget
		|| InCachedNetwork != Network
		|| !InCachedSize.Equals(Size, 1.0f);
}

UTextureRenderTarget2D* UWiesbadenWorldMapView::EnsureRendered(
	UWorld* World, const FRoadNetwork& Network,
	const TArray<FGeneratedBuilding>* Buildings, const FVector2D& ScreenSize,
	float ZoomFactor, const FVector2D& DesiredCentreWorld, bool bCentreValid)
{
	if (!World)
	{
		return RenderTarget;
	}

	const int32 W = FMath::Max(FMath::RoundToInt(ScreenSize.X), 16);
	const int32 H = FMath::Max(FMath::RoundToInt(ScreenSize.Y), 16);

	// Ziel (neu) anlegen, wenn keins da ist oder die Groesse nicht mehr passt.
	if (!RenderTarget || RenderTarget->SizeX != W || RenderTarget->SizeY != H)
	{
		RenderTarget = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
			World, UCanvasRenderTarget2D::StaticClass(), W, H);
		CachedNetwork = nullptr;   // Neu-Render erzwingen
	}

	// Voll-Einpassung nur bei Netz-/Groessenwechsel neu berechnen (die Netzgrenzen
	// aus allen Mittellinienpunkten sind der teure Teil).
	const bool bBaseStale = NeedsRerender(RenderTarget != nullptr, CachedNetwork, CachedSize, &Network, ScreenSize);
	if (bBaseStale)
	{
		FVector2D WMin, WMax;
		if (!FWiesbadenMinimap::ComputeNetworkBoundsXY(Network, WMin, WMax))
		{
			return RenderTarget;
		}
		const FVector2D SC(ScreenSize.X * 0.5, ScreenSize.Y * 0.5);
		BaseFit = FWiesbadenMinimap::MakeWorldMapProjection(WMin, WMax, SC, ScreenSize, 0.88f);
	}
	if (!BaseFit.IsValid())
	{
		return RenderTarget;
	}

	// Gezoomte/verschobene Sicht bauen (Zentrum wird auf die Netzgrenzen geklemmt).
	// Ohne gueltiges Zentrum (erstes Bild nach dem Oeffnen) auf die Netzmitte.
	const float Zoom = FMath::Clamp(ZoomFactor, FWiesbadenMinimap::WorldMapMinZoom, FWiesbadenMinimap::WorldMapMaxZoom);
	const FVector2D Desired = bCentreValid ? DesiredCentreWorld : BaseFit.ViewCentreWorld;
	Projection = FWiesbadenMinimap::MakeZoomedProjection(BaseFit, Zoom, Desired);

	// Neu rendern bei Netz-/Groessenwechsel ODER Sichtaenderung (Zoom/Zentrum).
	const bool bViewChanged = !FMath::IsNearlyEqual(Zoom, CachedZoom, 0.001f)
		|| !Projection.ViewCentreWorld.Equals(CachedCentre, 1.0);   // 1-cm-Schwelle
	if (!bBaseStale && !bViewChanged)
	{
		return RenderTarget;   // Sicht steht - fertiges Texture weiterverwenden
	}

	// --- Geometrie (datenrein, aus FWiesbadenMinimap) mit der gezoomten Projektion ---
	TArray<FMinimapLine> Lines;
	FWiesbadenMinimap::BuildWorldMapLines(Network, Projection, /*MaxLines=*/16000, /*MinSegmentPx=*/2.0f, Lines);
	TArray<FWorldMapQuad> Quads;
	if (Buildings)
	{
		FWiesbadenMinimap::BuildWorldMapBuildings(*Buildings, Projection, /*MaxQuads=*/12000, /*MinAreaPx=*/0.4f, Quads);
	}

	// --- EINMAL ins RenderTarget zeichnen ---
	UKismetRenderingLibrary::ClearRenderTarget2D(World, RenderTarget, WmBackground);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, RenderTarget, Canvas, CanvasSize, Ctx);
	if (Canvas)
	{
		// Gebaeude als gefuellte Flaechen UNTER den Strassen (ein Batch-Aufruf).
		if (Quads.Num() > 0)
		{
			TArray<FCanvasUVTri> Tris;
			Tris.Reserve(Quads.Num() * 2);
			for (const FWorldMapQuad& Q : Quads)
			{
				Tris.Add(MakeTri(Q.A, Q.B, Q.C, WmBuilding));
				Tris.Add(MakeTri(Q.A, Q.C, Q.D, WmBuilding));
			}
			Canvas->K2_DrawTriangle(nullptr, Tris);
		}

		// Strassen.
		for (const FMinimapLine& Line : Lines)
		{
			Canvas->K2_DrawLine(Line.Start, Line.End, Line.Thickness,
				Line.bMajor ? WmMajorRoad : WmMinorRoad);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Ctx);

	CachedNetwork = &Network;
	CachedSize = ScreenSize;
	CachedZoom = Zoom;
	CachedCentre = Projection.ViewCentreWorld;
	return RenderTarget;
}
