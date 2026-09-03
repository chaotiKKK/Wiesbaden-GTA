// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WiesbadenHealthReport.generated.h"

/**
 * Maschinenlesbarer Zustandsbericht der Laufzeit-Selbstdiagnosen.
 *
 * Buendelt die bisher verstreuten Kennzahlen (Streaming, Verkehr, Ampeln,
 * Fussgaenger, Gebaeude-Kollision) in EINE Struktur. Die reinen Rohzahlen fuellt
 * das CitySubsystem (BuildHealthReport); die INTERPRETATION (Warnungen) und die
 * JSON-Ausgabe leben hier - entkoppelt und testbar. So kann jede Sitzung, der
 * Rauchtest oder ein externes System den Ist-Zustand parsen, statt Prosa-Logs zu
 * greppen. Wichtig (Lehre dieser Codebasis): Warnungen sind EVIDENZ-gewichtet -
 * z. B. "Ampel-Kopplung greift nicht" nur bei genug Anfahrten ohne Halten, nicht
 * schon bei geometrischer Ampel-Naehe (sonst Fehlalarm).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenHealthReport
{
	GENERATED_BODY()

	/** World Partition: alle Zellen um den Spieler geladen. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	bool bStreamingComplete = false;

	/** Ampeln im Netz (0 = keine gebacken). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 TrafficLightCount = 0;

	/** Aktuell simulierte Verkehrsfahrzeuge. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 ActiveVehicles = 0;

	/** Seit Stadt-Spawn: Anfahrten auf signalisierte Verbindungen / Halte an Rot.
	 *  Erlauben das evidenz-gewichtete Ampel-Verdikt. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 VehiclesApproachingSignal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 VehiclesHeldAtRed = 0;

	/** Simulierte bzw. tatsaechlich gezeichnete Fussgaenger. Beide getrennt, damit
	 *  "simuliert aber nicht gezeichnet" evidenzbasiert erkannt wird. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PedestriansSimulated = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PedestriansDrawn = 0;

	/** Aktive Gebaeude-Kollisionskoerper um den Spieler (0 bei fertiger Stadt =
	 *  verdaechtig). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 BuildingCollisionBodies = 0;

	/** True, wenn ueberhaupt Stadtdaten geladen sind (sonst sind 0-Werte normal). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	bool bCityLoaded = false;

	/** Evidenz-gewichtete Warnungen (leer = gesund). */
	TArray<FString> Warnings() const;

	/** Kompakte, parsebare JSON-Zeile (inkl. "warnings"-Array). */
	FString ToJson() const;
};
