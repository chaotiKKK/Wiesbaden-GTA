// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Missions/WiesbadenMissionTypes.h"

/** Ergebnis des Missions-Ladens: geparste Missionen + gesammelte Fehler. */
struct FMissionLoadResult
{
	TArray<FMission> Missions;
	TArray<FString> Errors;

	/** True, wenn das JSON grundsaetzlich geparst wurde (Container gefunden).
	 *  Einzelne uebersprungene Ziele/Missionen landen in Errors, nicht hier. */
	bool bParsed = false;
};

/**
 * Laedt Missionen aus einem JSON-String. Rein datenbasiert (kein Dateizugriff),
 * damit direkt unit-testbar. Muster wie FOSMDataParser (FJsonSerializer).
 * Unbekannte Ziel-Typen werden uebersprungen und als Fehler gesammelt, nie
 * als Absturz.
 */
class WIESBADENREAL_API FWiesbadenMissionLoader
{
public:
	static FMissionLoadResult ParseMissions(const FString& Json);
};
