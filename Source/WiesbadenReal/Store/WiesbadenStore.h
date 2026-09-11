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

	/**
	 * Vertragsstrafe bei Missions-Fehlschlag: 20% der entgangenen Praemie
	 * (abgerundet), damit Zeitdruck etwas kostet. Rein rechnend; die "nie ins
	 * Minus"-Klemmung macht FWiesbadenGameState::ApplyDelta beim Abbuchen.
	 */
	static int32 ComputeFailurePenalty(int32 Reward);

	/** Katalog-Id der Kurierlizenz - die Freischaltung mit Praemien-Effekt. */
	static FName KurierlizenzId();

	/** Katalog-Id des Helikopter-Hangars - Freischaltung fuer den Heli-Einstieg. */
	static FName HelikopterHangarId();

	/** Katalog-Id des Premium-Stadtplans - schaltet die Karten-Beschriftung frei. */
	static FName PremiumStadtplanId();

	/**
	 * DEBUG-Schalter WbDev_AllowHelicopterWithoutHangar als Abfrage.
	 *
	 * Der Schalter ist eine Compile-Zeit-Wahl und liegt bewusst nur hier -
	 * sonst tragen GameMode und UI dieselbe `defined(...)`-Verzweigung doppelt
	 * (und genau dort stand sie frueher faelschlich in einer Laufzeit-`if`).
	 */
	static bool IsHangarGateForcedOpen();

	/** Soll der Kauf-Hinweis erscheinen, obwohl das Tor zu ist? (kein DEBUG-Tor) */
	static bool ShouldShowHangarPurchaseHint();

	/**
	 * Darf der Spieler den Helikopter betreten?
	 * Standard: nur mit gekauftem Hangar (Rein/testbar).
	 * DEBUG: bei definiertem WbDev_AllowHelicopterWithoutHangar immer true.
	 */
	static bool MayEnterHelicopter(bool bHasHangarUnlock);
};
