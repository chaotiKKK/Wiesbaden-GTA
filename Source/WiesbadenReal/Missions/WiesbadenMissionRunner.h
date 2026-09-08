// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Missions/WiesbadenMissionTypes.h"

/** Ergebnis eines Fortschritts-Schritts. */
struct FMissionProgressResult
{
	int32 NextObjectiveIndex = 0;
	bool bAdvanced = false;
	bool bMissionCompleted = false;
	int32 GuthabenAwarded = 0;
};

/**
 * Reine Fortschritts-Logik einer laufenden Mission - kein Welt-/Tick-Zugriff,
 * damit direkt unit-testbar. Das Subsystem ruft Step() je Tick mit der
 * aktuellen Spielerposition und wendet das Ergebnis auf seinen Zustand an.
 */
class WIESBADENREAL_API FWiesbadenMissionRunner
{
public:
	/**
	 * Ein Schritt: Ist das aktuelle Ziel erfuellt, ruecke den Index vor; war es
	 * das letzte Ziel, gilt die Mission als abgeschlossen (Belohnung ausgewiesen).
	 * Ist das Ziel offen oder der Index ungueltig, passiert nichts.
	 */
	static FMissionProgressResult Step(
		const FMission& Mission,
		int32 CurrentObjectiveIndex,
		const FMissionContext& Ctx);
};
