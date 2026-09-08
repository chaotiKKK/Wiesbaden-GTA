// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Store/WiesbadenStoreTypes.h"

/**
 * Reine Kauf-/Freischaltungs-Logik (kein Welt-/Save-Zugriff) - Muster wie
 * FWiesbadenMissionRunner. Katalog-Lookup und Persistenz macht das Subsystem;
 * diese Klasse entscheidet nur, ob ein Kauf zulaessig ist, und beziffert den
 * Effekt der Freischaltungen.
 */
class WIESBADENREAL_API FWiesbadenStore
{
public:
	/** Darf gekauft werden? Rechnet nur, veraendert nichts. */
	static FPurchaseEvaluation Evaluate(const FStoreItem& Item, int32 Guthaben, bool bAlreadyOwned);

	/** Missions-Praemie mit Kurierlizenz (+50%, abgerundet); sonst unveraendert. */
	static int32 ApplyLicenseBonus(int32 BaseReward, bool bLicensed);

	/** Katalog-Id der Kurierlizenz - die Freischaltung mit Praemien-Effekt. */
	static FName KurierlizenzId();
};
