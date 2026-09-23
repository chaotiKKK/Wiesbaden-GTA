// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGeneratedBuilding;

/**
 * Haelt Abstellplaetze von Gebaeuden frei.
 *
 * WARUM ES DAS BRAUCHT - gemessen, nicht vermutet:
 *
 * Der alte Hubschrauber wurde bisher nur gegen die FAHRBAHN geprueft
 * (FWiesbadenRoadClearance). Gebaeude kamen in der Pruefung nicht vor. Das ist
 * nicht bloss eine Luecke, es kehrt die Sache um: wer von der Strasse
 * wegrueckt, rueckt ins Blockinnere - und dort stehen die Haeuser. Gemessen
 * am Stand vor dieser Datei: der zweite Hubschrauber landete auf Platz 13 von 36
 * geprueften Stellen (die ersten zwoelf lagen auf der Fahrbahn) und stand
 * damit mitten in "Platter Strasse 150", einem Bau von 80 x 26 m - 9,4 m
 * innerhalb der Grundrissmitte, also nicht am Rand gestreift, sondern drin.
 *
 * WARUM NICHT EINFACH DIE KOLLISION FRAGEN:
 *
 * Aus demselben Grund, aus dem die Fahrbahn aus dem Netz kommt: die Plaetze
 * werden im ersten Bild vergeben, da hat World Partition noch keine einzige
 * Stadtkachel hereingestreamt. Ein Lot traefe nur die Landschaft und meldete
 * ueberall "frei". Die Gebaeudeliste liegt dagegen serialisiert am
 * WorldBuilder und ist sofort da.
 *
 * WARUM DER GEDREHTE GRUNDRISS UND NICHT `Bounds`:
 *
 * `FGeneratedBuilding::Bounds` ist achsparallel und deckt bei gedrehten
 * Gebaeuden im Mittel das 2,1-fache des Grundrisses ab. Mit ihr gaelte
 * reichlich Freiflaeche als bebaut - am Ka-52-Standort etwa meldeten die
 * Bounds zwei Treffer, der echte Grundriss keinen einzigen. Ein Platz, der
 * gut ist, darf nicht verworfen werden.
 */
struct WIESBADENREAL_API FWiesbadenBuildingClearance
{
public:
	/**
	 * Baut den Index aus den Gebaeuden im Umkreis von Center.
	 *
	 * Nur im Umkreis: die gebackene Karte traegt 104.458 Gebaeude, und fuer
	 * die Frage "ist dieser eine Platz frei?" braucht es die anderen 104.400
	 * nicht. Der Radius muss alle geprueften Stellen samt Grundriss
	 * einschliessen, sonst gilt dort faelschlich alles als frei.
	 *
	 * @param ExtraMarginCm Zuschlag rings um jeden Grundriss - Vordaecher,
	 *                      Treppen und Rampen stehen nicht in der Liste.
	 */
	void BuildAround(const TArray<FGeneratedBuilding>& Buildings,
		const FVector2D& Center, double AreaRadiusCm, double ExtraMarginCm = 0.0);

	/**
	 * True, wenn ein Kreis um Point einen Gebaeudegrundriss beruehrt.
	 *
	 * Der Kreis statt eines Punktes, weil ein Hubschrauber 14 m lang ist: die
	 * Mitte kann neben dem Haus liegen, waehrend das Heck in der Wand steckt.
	 */
	bool IsBlocked(const FVector2D& Point, double RadiusCm) const;

	/** Anzahl eingetragener Grundrisse (Diagnose). */
	int32 GetFootprintCount() const { return Footprints.Num(); }

	bool IsEmpty() const { return Footprints.Num() == 0; }

	/**
	 * Abstand eines Punktes zu einem gedrehten Rechteck (datenrein, testbar).
	 *
	 * 0, wenn der Punkt drin liegt. Waagerecht gemessen: ein Haus den Hang
	 * hinauf steht dem Hubschrauber genauso im Weg wie eines auf gleicher
	 * Hoehe.
	 *
	 * @param ExtentCm Halbe Kantenlaengen (nicht die vollen).
	 * @param YawDeg   Drehung des Rechtecks in Grad.
	 */
	static double DistanceToRotatedBox2D(
		const FVector2D& Point, const FVector2D& CenterCm,
		const FVector2D& ExtentCm, double YawDeg);

private:
	/** Ein Gebaeudegrundriss: gedrehtes Rechteck. */
	struct FFootprint
	{
		FVector2D CenterCm = FVector2D::ZeroVector;
		FVector2D ExtentCm = FVector2D::ZeroVector;
		double YawDeg = 0.0;
	};

	/** Kantenlaenge einer Gitterzelle in cm (50 m) - wie bei der Fahrbahn. */
	static constexpr double CellSizeCm = 5000.0;

	static FIntPoint CellOf(const FVector2D& Point);

	TArray<FFootprint> Footprints;

	/** Zelle -> Indizes in Footprints. */
	TMap<FIntPoint, TArray<int32>> Cells;
};
