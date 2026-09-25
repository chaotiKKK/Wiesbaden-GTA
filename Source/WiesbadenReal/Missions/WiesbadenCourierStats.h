// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WiesbadenCourierStats.generated.h"

/**
 * Kurier-Bilanz fuer Dennos Lieferungen - im Spielstand gespeichert (ab v3)
 * und beim Annehmen eines Auftrags angezeigt. Ein aelterer Spielstand laedt
 * mit leerer Bilanz.
 */
USTRUCT()
struct WIESBADENREAL_API FWbCourierStats
{
	GENERATED_BODY()

	/** Abgegebene Lieferungen (immer innerhalb der Frist - sonst verfaellt der Auftrag). */
	UPROPERTY()
	int32 Delivered = 0;

	/** Lieferungen, deren Frist ablief. */
	UPROPERTY()
	int32 Missed = 0;

	/** Davon flott: mit mindestens der halben Frist uebrig (hoechste Trinkgeldstufe). */
	UPROPERTY()
	int32 Fast = 0;

	UPROPERTY()
	int32 TipTotal = 0;

	/** Hoechstes einzelnes Trinkgeld (EUR). */
	UPROPERTY()
	int32 TipRecord = 0;
};

/** Datenreine Fortschreibung und Anzeige der Bilanz (Test). */
namespace WiesbadenCourierStats
{
	/** Abgabe verbuchen. true = das Trinkgeld ist ein neuer Rekord. */
	bool RecordDelivery(FWbCourierStats& Stats, int32 Tip, bool bFast);

	/** Abgelaufene Frist verbuchen. */
	void RecordMissed(FWbCourierStats& Stats);

	/** Anteil puenktlicher Lieferungen in ganzen Prozent; -1 ohne Lieferung. */
	int32 PunctualityPercent(const FWbCourierStats& Stats);

	/** Eine Zeile fuer den Annahme-Hinweis. */
	FString Describe(const FWbCourierStats& Stats);
}
