// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WiesbadenPlacementAudit.generated.h"

struct FRoadNetwork;
struct FRoadFurnitureLayout;
struct FRegionAssetLayout;
struct FGeneratedBuilding;
struct FOSMDataSet;
class UGeoCoordinateConverter;

/**
 * Eine Verstoss-Stelle: die Belegkoordinate fuer das Kontrollbild.
 *
 * Die Koordinaten eines Audit-Laufs SIND die Fotostellen des Vorher/Nachher-
 * Belegs: der Vorher-Lauf schreibt sie, der Nachher-Lauf dieselben Punkte
 * noch einmal - ohne diese Liste wuerden die beiden Bilder an zufaelligen
 * Stellen entstehen und nichts beweisen.
 */
USTRUCT()
struct WIESBADENREAL_API FPlacementViolation
{
	GENERATED_BODY()

	/** Regel-Schluessel, z. B. "R1-SchildFahrbahn". */
	UPROPERTY()
	FString Regel;

	/** Was genau verletzt ist (Zeichen, Objektart, Quelle). */
	UPROPERTY()
	FString Art;

	/** Position in Weltkoordinaten (cm). */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** OSM-Knoten-/Weg-Id der Quelle (0 = keine). */
	UPROPERTY()
	int64 QuellId = 0;
};

/** Ein Zaeler je Regel: Basis, Verstoesse und die Beispiele bis zum Limit. */
USTRUCT()
struct WIESBADENREAL_API FPlacementRuleCount
{
	GENERATED_BODY()

	/** Stabiler Schluessel fuer JSON und Tests, z. B. "R3-Dopplung". */
	UPROPERTY()
	FString Schluessel;

	/** Menschlich lesbare Bedeutung der Zahl. */
	UPROPERTY()
	FString Beschriftung;

	/** Wie viele Objekte ueberhaupt geprueft wurden (Basis der Zahl). */
	UPROPERTY()
	int32 Geprueft = 0;

	/** Wie viele davon gegen die Regel verstossen. */
	UPROPERTY()
	int32 Verstoesse = 0;

	/** Belegstellen bis MaxBeispiele. */
	UPROPERTY()
	TArray<FPlacementViolation> Beispiele;
};

/**
 * Ergebnis des Platzierungs-Audits ueber die ganze Stadt.
 *
 * Der Bericht zaehlt, wie oft die SECHS Regeln des Entwurfs
 * "2026-09-26-platzierungsregeln-design.md" am IST-Stand verletzt werden -
 * ohne am Verhalten der Platzierung etwas zu aendern. Er ist die
 * "Vorher"-Haelfte der Vorher-Nachher-Behauptung: nach der Umsetzung muss
 * dieselbe Pruefung jede Zahl auf 0 setzen.
 */
USTRUCT()
struct WIESBADENREAL_API FPlacementAuditReport
{
	GENERATED_BODY()

	UPROPERTY()
	bool bSuccess = false;

	UPROPERTY()
	FString ErrorMessage;

	// -- Bestand: die Basis, auf der die Prozentwerte ruhen ---------------

	UPROPERTY()
	int32 SignCount = 0;

	UPROPERTY()
	int32 DelineatorCount = 0;

	UPROPERTY()
	int32 StreetLampCount = 0;

	UPROPERTY()
	int32 FurnitureCount = 0;

	UPROPERTY()
	int32 RegionAssetCount = 0;

	UPROPERTY()
	int32 TreeCount = 0;

	UPROPERTY()
	int32 BuildingCount = 0;

	/** Segmente der aktiven Bahnwege im OSM-Datensatz (Basis R6-Bahn). */
	UPROPERTY()
	int32 RailSegmentCount = 0;

	/**
	 * False, wenn der Bahnkorridor nicht geprueft werden konnte (kein
	 * Geo-Bezug oder keine Bahnwege im Datensatz) - dann ist R6-Bahn
	 * ausdruecklich NICHT gemessen, nicht "ohne Fehler".
	 */
	UPROPERTY()
	bool bRailCheckActive = false;

	// -- Zaeler je Regel (Reihenfolge = Reihenfolge der sechs Regeln) ------

	UPROPERTY()
	TArray<FPlacementRuleCount> Regeln;

	UPROPERTY()
	double DurationSeconds = 0.0;

	/** Einen Zaeler nach Schluessel holen (nullptr, wenn die Regel fehlt). */
	const FPlacementRuleCount* Finde(const FString& Schluessel) const;

	/** Menschliche Zusammenfassung fuer das Log. */
	FString ToString() const;
};

/**
 * Prueft die fertige Platzierung von Ausstattung und Bewuchs gegen die sechs
 * Regeln des Platzierungs-Entwurfs - DATENREIN, zaehlend, ohne Verhaltens-
 * aenderung. Aus dem Audit werden die Vorher-Zahlen und die Belegkoordinaten
 * der Kontrollbilder; dieselbe Funktion liefert nach der Umsetzung die
 * Nachher-Zahlen (0 Verstoesse).
 *
 * Warum eigene Datei und kein Zaeler in den Platzierern: ein Zaeler, der
 * mitten in der Platzierung sitzt, aendert mit jedem Umbau mit und kann
 * danach nicht mehr den Zustand VOR dem Umbau messen. Der Audit liest nur
 * fertige Layouts - er ist damit unabhaengig davon, wie sie entstanden sind.
 */
struct WIESBADENREAL_API FWiesbadenPlacementAudit
{
	/** Wie viele Beispiele je Regel im Bericht landen. */
	static constexpr int32 MaxBeispieleJeRegel = 20;

	/**
	 * @param Network      Fertiges Strassennetz (Fahrbahn, Kreuzungen).
	 * @param Furniture    Schilder, Leitpfosten, Laternen, Markierungen, Moebel.
	 * @param RegionAssets Baeume, Ufer- und Industrie-Objekte.
	 * @param Buildings    Gebaeude-Grundrisse (gedrehte Boxen).
	 * @param OSMData      Geparster Datensatz: kartierte Knotenpositionen und
	 *                    die Bahnwege fuer den Korridor.
	 * @param Converter    Geo-Bezug des Baus - dieselbe Initialisierung wie
	 *                    die Pipeline, sonst liegt der Bahnkorridor woanders
	 *                    als die Bahn.
	 */
	static FPlacementAuditReport Run(
		const FRoadNetwork& Network,
		const FRoadFurnitureLayout& Furniture,
		const FRegionAssetLayout& RegionAssets,
		const TArray<FGeneratedBuilding>& Buildings,
		const FOSMDataSet& OSMData,
		const UGeoCoordinateConverter& Converter);

	/** Schreibt den Bericht als JSON (Regel -> Zahl + Beispiele). */
	static bool WriteJson(const FPlacementAuditReport& Report, const FString& Path);
};
