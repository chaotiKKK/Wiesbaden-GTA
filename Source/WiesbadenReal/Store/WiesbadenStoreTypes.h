// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

// Datenreine Store-Typen (Teilprojekt 2 Stueck 3 - Ausgabe-Senke). Keine
// Engine-Abhaengigkeit ueber CoreMinimal hinaus, damit die Kauf-Logik ohne
// Welt/Save unit-testbar ist.

/** Eine kaufbare Freischaltung (Ausgabe-Senke fuers Guthaben). */
struct FStoreItem
{
	FName Id;
	FString Title;
	FString Beschreibung;
	int32 Kosten = 0;
};

/** Ausgang eines Kaufversuchs. */
enum class EPurchaseOutcome : uint8
{
	Success,           // gekauft: Guthaben abgebucht, Freischaltung erteilt
	AlreadyOwned,      // schon im Besitz - kein zweiter Kauf
	NotEnoughGuthaben, // Guthaben reicht nicht
	UnknownItem,       // Id nicht im Katalog
};

/** Ergebnis der reinen Kauf-Bewertung. */
struct FPurchaseEvaluation
{
	EPurchaseOutcome Outcome = EPurchaseOutcome::UnknownItem;
	int32 NewGuthaben = 0; // Kontostand nach dem Kauf (bei Success), sonst der alte
};
