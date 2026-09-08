// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Missions/WiesbadenMissionTypes.h"

/** Ergebnis der Auftragsvergabe: der naechste Auftrag (falls vorhanden). */
struct FMissionDispatchResult
{
	bool bHasMission = false;
	bool bProcedural = false; // true, wenn nach Erschoepfung des Pools erzeugt
	FMission Mission;
};

/**
 * Auftragsvergabe (nachladend): waehlt anhand der Zahl bereits abgeschlossener
 * Auftraege den naechsten. Erst die handgeschriebenen Missionen aus dem Pool in
 * Reihenfolge, danach ENDLOS prozedural erzeugte Kurierjobs aus den im Pool
 * bekannten Orten. Rein datenbasiert (kein Welt-/Zufalls-/Zeitzugriff), damit
 * deterministisch und direkt unit-testbar - Muster wie FWiesbadenMissionRunner.
 */
class WIESBADENREAL_API FWiesbadenMissionDispatcher
{
public:
	/**
	 * @param Pool           Handgeschriebene Missions-Vorlagen (aus JSON).
	 * @param CompletedCount Wie viele Auftraege bereits abgeschlossen sind
	 *                       (0 = noch keiner) - zugleich der Vergabe-Cursor.
	 */
	static FMissionDispatchResult NextMission(
		const TArray<FMission>& Pool, int32 CompletedCount);
};
