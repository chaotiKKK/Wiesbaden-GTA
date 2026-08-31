// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FRoadNetwork;

/**
 * Haelt die Fahrbahn von Streuobjekten frei.
 *
 * Warum es das braucht:
 *
 * `UWiesbadenRegionAssetGenerator::ScatterRegion` verteilt Baeume, Buesche und
 * Felsen auf einem Raster im Regionspolygon - OHNE das Strassennetz auch nur
 * anzusehen. Wo ein Landnutzungspolygon eine Strasse ueberlappt (Parks,
 * Gruenflaechen, Waldwege), stehen die Baeume auf der Fahrbahn. Auffallen
 * konnte das nie als Fehler: die Baum-Instanzen tragen ausdruecklich KEINE
 * Kollision (RegionAssetSpawnerComponent), der Verkehr faehrt also
 * geraeuschlos hindurch.
 *
 * Ein Abstandstest gegen 125.024 Segmente je Streupunkt waere bei rund 1,8
 * Millionen Punkten nicht bezahlbar - deshalb ein Gitter, das je Zelle nur
 * die Segmente merkt, die sie beruehren.
 */
struct WIESBADENREAL_API FWiesbadenRoadClearance
{
public:
	/**
	 * Baut den Index aus dem Strassennetz.
	 *
	 * @param Network      Fertiges Strassennetz.
	 * @param ExtraMarginCm Zuschlag auf die halbe Fahrbahnbreite.
	 * @param bIncludeSidewalk Gehweg mitsperren?
	 *
	 * Der Schalter trennt zwei verschiedene Fragen:
	 *
	 *  - Baeume (true): sollen weder auf der Fahrbahn noch auf dem Gehweg
	 *    stehen, sonst waechst mitten im Buergersteig ein Wald.
	 *  - Strassenausstattung (false): Schilder, Leitpfosten und Laternen
	 *    GEHOEREN an den Fahrbahnrand und damit auf den Gehweg. Fuer sie ist
	 *    nur die Fahrbahn selbst verboten.
	 */
	void Build(const FRoadNetwork& Network, double ExtraMarginCm,
		bool bIncludeSidewalk = true);

	/** True, wenn der Punkt auf oder neben einer Fahrbahn liegt. */
	bool IsBlocked(const FVector2D& Point) const;

	/** Anzahl der eingetragenen Abschnitte (Diagnose). */
	int32 GetSpanCount() const { return Spans.Num(); }

	bool IsEmpty() const { return Spans.Num() == 0; }

	/**
	 * Abstand eines Punktes zu einer Strecke, im Quadrat (datenrein).
	 *
	 * Eigene Fassung statt FMath::PointDistToSegmentSquared, weil hier
	 * WAAGERECHT gemessen wird: ein Baum am Hang ueber einem Tunnel ist nicht
	 * auf der Fahrbahn, sein Abstand im Raum aber klein.
	 */
	static double DistanceToSegmentSquared2D(
		const FVector2D& Point, const FVector2D& Start, const FVector2D& End);

private:
	/** Ein Fahrbahnabschnitt: Strecke plus Freihalteradius. */
	struct FSpan
	{
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D End = FVector2D::ZeroVector;
		double RadiusCm = 0.0;
	};

	/** Kantenlaenge einer Gitterzelle in cm (50 m). */
	static constexpr double CellSizeCm = 5000.0;

	static FIntPoint CellOf(const FVector2D& Point);

	TArray<FSpan> Spans;

	/** Zelle -> Indizes in Spans. */
	TMap<FIntPoint, TArray<int32>> Cells;
};
