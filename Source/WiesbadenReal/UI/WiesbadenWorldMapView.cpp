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
	const FLinearColor WmMinorRoad(0.66f, 0.68f, 0.72f, 1.0f);
	const FLinearColor WmMajorRoad(0.97f, 0.84f, 0.38f, 1.0f);

	/** Ueberabtastung der Basis-Textur (Schaerfe beim Hineinzoomen), gedeckelt. */
	constexpr float kSuperSample = 2.5f;
	constexpr int32 kMaxBaseAxis = 4096;

	/** Ein FCanvasUVTri mit einheitlicher Farbe (weisses Default-Texture). */
	FCanvasUVTri MakeTri(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
	{
		FCanvasUVTri Tri;
		Tri.V0_Pos = A; Tri.V1_Pos = B; Tri.V2_Pos = C;
		Tri.V0_UV = Tri.V1_UV = Tri.V2_UV = FVector2D::ZeroVector;
		Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = Color;
		return Tri;
	}

	/** Ein Strassensegment als gefuelltes Band (zwei Dreiecke) anhaengen. */
	void AddBand(TArray<FCanvasUVTri>& Tris, const FVector2D& A, const FVector2D& B,
		float HalfWidth, const FLinearColor& Color)
	{
		FVector2D Dir = B - A;
		const float Len = Dir.Size();
		if (Len <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		Dir /= Len;
		const FVector2D N(-Dir.Y * HalfWidth, Dir.X * HalfWidth);
		const FVector2D A0 = A + N, A1 = A - N, B0 = B + N, B1 = B - N;
		Tris.Add(MakeTri(A0, B0, B1, Color));
		Tris.Add(MakeTri(A0, B1, A1, Color));
	}

	/** Eine gefuellte Scheibe (Dreiecksfaecher) am Knoten anhaengen - dichtet die
	 *  Aussenkante an Strassenbiegungen ab, damit die Zuege nahtlos wirken. */
	void AddDisc(TArray<FCanvasUVTri>& Tris, const FVector2D& C, float Radius,
		const FLinearColor& Color, int32 Segments = 6)
	{
		if (Radius <= 0.5f)
		{
			return;
		}
		const float Step = 2.0f * PI / static_cast<float>(FMath::Max(3, Segments));
		FVector2D Prev(C.X + Radius, C.Y);
		for (int32 i = 1; i <= Segments; ++i)
		{
			const float Ang = Step * i;
			const FVector2D Cur(C.X + Radius * FMath::Cos(Ang), C.Y + Radius * FMath::Sin(Ang));
			Tris.Add(MakeTri(C, Prev, Cur, Color));
			Prev = Cur;
		}
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

UTextureRenderTarget2D* UWiesbadenWorldMapView::EnsureBaseMap(
	UWorld* World, const FRoadNetwork& Network,
	const TArray<FGeneratedBuilding>* Buildings, const FVector2D& ScreenSize)
{
	if (!World)
	{
		return RenderTarget;
	}

	// Basis-Textur: ueberabgetastet gegenueber dem Bildschirm, gleiche Seiten-
	// verhaeltnisse (dann ist das UV-Blitten verzerrungsfrei), Achsen gedeckelt.
	const int32 BaseW = FMath::Clamp(FMath::RoundToInt(ScreenSize.X * kSuperSample), 512, kMaxBaseAxis);
	const int32 BaseH = FMath::Clamp(FMath::RoundToInt(ScreenSize.Y * kSuperSample), 512, kMaxBaseAxis);

	if (!RenderTarget || RenderTarget->SizeX != BaseW || RenderTarget->SizeY != BaseH)
	{
		RenderTarget = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
			World, UCanvasRenderTarget2D::StaticClass(), BaseW, BaseH);
		CachedNetwork = nullptr;   // Neu-Back erzwingen
	}

	// Nur backen, wenn Netz oder Groesse gewechselt hat - NICHT bei Zoom/Pan.
	if (!NeedsRerender(RenderTarget != nullptr, CachedNetwork, CachedSize, &Network, ScreenSize))
	{
		return RenderTarget;
	}

	FVector2D WMin, WMax;
	if (!FWiesbadenMinimap::ComputeNetworkBoundsXY(Network, WMin, WMax))
	{
		return RenderTarget;
	}

	BaseSizeX = BaseW;
	BaseSizeY = BaseH;
	const FVector2D BaseSizePx(BaseW, BaseH);
	BaseFit = FWiesbadenMinimap::MakeWorldMapProjection(
		WMin, WMax, BaseSizePx * 0.5, BaseSizePx, FWiesbadenMinimap::WorldMapMarginFrac);
	if (!BaseFit.IsValid())
	{
		return RenderTarget;
	}

	// Geometrie (datenrein): das GANZE Netz in Basis-Pixel. Reichliches Budget,
	// weil nur EINMAL gebacken wird; feine Dezimierung (1 Basis-Pixel).
	TArray<FMinimapLine> Lines;
	FWiesbadenMinimap::BuildWorldMapLines(Network, BaseFit, /*MaxLines=*/30000, /*MinSegmentPx=*/1.0f, Lines);
	TArray<FWorldMapQuad> Quads;
	if (Buildings)
	{
		FWiesbadenMinimap::BuildWorldMapBuildings(*Buildings, BaseFit, /*MaxQuads=*/40000, /*MinAreaPx=*/0.4f, Quads);
	}

	// Strassen als gefuellte Baender: Dicke in BILDSCHIRM-Pixeln (aus BuildWorldMapLines)
	// mal Ueberabtastung, damit sie nach dem Herunterblitten (Zoom 1) traegt.
	TArray<FCanvasUVTri> MinorTris;
	TArray<FCanvasUVTri> MajorTris;
	MinorTris.Reserve(Lines.Num() * 2);
	for (const FMinimapLine& Line : Lines)
	{
		const float HalfW = FMath::Max(0.6f, Line.Thickness * kSuperSample * 0.5f);
		TArray<FCanvasUVTri>& Dst = Line.bMajor ? MajorTris : MinorTris;
		AddBand(Dst, Line.Start, Line.End, HalfW, Line.bMajor ? WmMajorRoad : WmMinorRoad);
		// Knoten-Scheiben nur fuer Hauptstrassen (das erkennbare Skelett soll
		// garantiert nahtlos sein); Nebenstrassen tragen ueber die Bandbreite.
		if (Line.bMajor)
		{
			AddDisc(MajorTris, Line.Start, HalfW, WmMajorRoad);
			AddDisc(MajorTris, Line.End, HalfW, WmMajorRoad);
		}
	}

	// --- EINMAL ins RenderTarget backen ---
	UKismetRenderingLibrary::ClearRenderTarget2D(World, RenderTarget, WmBackground);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, RenderTarget, Canvas, CanvasSize, Ctx);
	if (Canvas)
	{
		// Gebaeude als Flaechen UNTER den Strassen (ein Batch).
		if (Quads.Num() > 0)
		{
			TArray<FCanvasUVTri> BuildingTris;
			BuildingTris.Reserve(Quads.Num() * 2);
			for (const FWorldMapQuad& Q : Quads)
			{
				BuildingTris.Add(MakeTri(Q.A, Q.B, Q.C, WmBuilding));
				BuildingTris.Add(MakeTri(Q.A, Q.C, Q.D, WmBuilding));
			}
			Canvas->K2_DrawTriangle(nullptr, BuildingTris);
		}
		// Nebenstrassen, dann Hauptstrassen darueber - je EIN Batch-Aufruf.
		if (MinorTris.Num() > 0) { Canvas->K2_DrawTriangle(nullptr, MinorTris); }
		if (MajorTris.Num() > 0) { Canvas->K2_DrawTriangle(nullptr, MajorTris); }
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Ctx);

	CachedNetwork = &Network;
	CachedSize = ScreenSize;
	return RenderTarget;
}
