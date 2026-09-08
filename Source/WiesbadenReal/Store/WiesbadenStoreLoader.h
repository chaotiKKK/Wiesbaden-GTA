// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Store/WiesbadenStoreTypes.h"

/** Ergebnis des Katalog-Ladens: geparste Freischaltungen + gesammelte Fehler. */
struct FStoreLoadResult
{
	TArray<FStoreItem> Items;
	TArray<FString> Errors;

	/** True, wenn das JSON grundsaetzlich geparst wurde (Container gefunden).
	 *  Einzelne uebersprungene Eintraege landen in Errors, nicht hier. */
	bool bParsed = false;
};

/**
 * Laedt den Freischaltungs-Katalog aus einem JSON-String. Rein datenbasiert
 * (kein Dateizugriff), damit direkt unit-testbar - Muster wie
 * FWiesbadenMissionLoader. Eintraege ohne Id werden uebersprungen und als
 * Fehler gesammelt, nie als Absturz.
 */
class WIESBADENREAL_API FWiesbadenStoreLoader
{
public:
	static FStoreLoadResult ParseItems(const FString& Json);
};
