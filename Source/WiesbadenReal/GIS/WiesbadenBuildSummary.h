// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// WICHTIG (Projekt-Konvention, UE 5.8): die .generated.h wird VOR den
// USTRUCT-Definitionen eingebunden (wie in WiesbadenTrafficLights.h), damit
// CURRENT_FILE_ID beim Expandieren von GENERATED_BODY() bereits gesetzt ist.
#include "WiesbadenBuildSummary.generated.h"

struct FRoadNetwork;
struct FGeneratedBuilding;
struct FRoadFurnitureLayout;
struct FWiesbadenCityData;

/**
 * Zusammenfassung eines Stadt-Builds (Editor-BuildCity oder Laufzeit-Build im
 * GameInstance). Wird am Ende des Laufs als Block geloggt UND als eine
 * CSV-Zeile in Saved/BuildHistory/CityBuilds.csv geschrieben, damit
 * Build-Zeiten ueber mehrere Laeufe vergleichbar sind.
 *
 * Datenrein: keine UObject-/Editor-Abhaengigkeiten (headless testbar).
 */
struct WIESBADENREAL_API FWiesbadenBuildSummary
{
	/** Quelle des Laufs: "Editor" (WorldBuilder) oder "Runtime" (GameInstance). */
	FString Source;

	/** Zeitpunkt des Laufendes (lokal, als Text). */
	FString Timestamp;

	double DurationSeconds = 0.0;

	int32 RoadSegments = 0;
	int32 Intersections = 0;
	int32 Buildings = 0;
	int32 Signs = 0;

	/** Seitenlaenge des Terrain-Rasters (0 = kein Terrain). */
	int32 TerrainGrid = 0;

	/** "Landscape" / "Vorschau" / leer (kein Terrain). */
	FString TerrainMode;

	/**
	 * Terrain-Qualitaetswarnung aus der Pipeline (CheckTerrainQuality): true,
	 * wenn das Tile deutlich groesser als die OSM-Ausdehnung war (Crop fehlt?)
	 * bzw. die Hoehenspanne unplausibel gross/klein war. Als eigene CSV-Spalten,
	 * damit Crop-/Hoehenprobleme ueber mehrere Builds vergleichbar sind.
	 */
	bool bTerrainTileTooLarge = false;

	bool bTerrainHeightRangeSuspicious = false;

	int32 RegionsWater = 0;
	int32 RegionsGreen = 0;
	int32 RegionsResidential = 0;
	int32 RegionsCommercial = 0;
	int32 RegionsIndustrial = 0;

	int32 RegionAssetsTotal = 0;
	int32 RegionAssetsTrees = 0;
	int32 RegionAssetsWaterfront = 0;
	int32 RegionAssetsIndustrial = 0;

	/** Anzahl der erzeugten World-Partition-Chunk-Actors (Editor, 0 sonst). */
	int32 Chunks = 0;

	/** Kantenlaenge einer Chunk-Zelle in Metern (Editor, 0 sonst). */
	double ChunkSizeMeters = 0.0;

	/** Map-Pfad bei Auto-Save (Editor) - leer sonst. */
	FString MapPath;

	/** "ok" bzw. "fehlgeschlagen: <grund>". */
	FString Result;
};

/** CSV-Kopfzeile (Spalten entsprechen FWiesbadenBuildSummary). */
WIESBADENREAL_API FString BuildSummaryCsvHeader();

/**
 * Formatiert einen Datensatz als CSV-Zeile (datenrein, testbar). Kommas,
 * Anfuehrungszeichen und Zeilenumbrueche werden RFC-4180-gequotet.
 */
WIESBADENREAL_API FString FormatBuildSummaryCsvRow(const FWiesbadenBuildSummary& Summary);

/** Standardpfad der CSV: Projekt-Saved/BuildHistory/CityBuilds.csv. */
WIESBADENREAL_API FString GetBuildSummaryCsvPath();

/**
 * Kompakter Einzeiler eines Builds (Details-Panel + GetLastBuildSummary),
 * identisch zwischen Editor- und Laufzeit-Pfad:
 * "Letzter Build: 83.4 s, ok (2026-08-16 15:45:00)".
 */
WIESBADENREAL_API FString FormatBuildSummaryLine(double DurationSeconds, const FString& Result, const FString& Timestamp);

/**
 * Haengt eine Zeile an die CSV an (erzeugt Datei + Kopfzeile beim ersten
 * Lauf). Thread-sicher (kritischer Abschnitt - Editor und Laufzeit-Worker
 * koennen sich ueberlappen). @return false + OutError bei Fehler.
 */
WIESBADENREAL_API bool AppendBuildSummaryToCsv(const FWiesbadenBuildSummary& Summary, FString& OutError);

/**
 * Setzt GameDefaultMap/EditorStartupMap im Text einer DefaultEngine.ini
 * (datenrein, testbar): ergaenzt die Section
 * [/Script/EngineSettings.GameMapsSettings] bei Bedarf und ersetzt vorhandene
 * Werte, ohne andere Eintraege anzutasten. Idempotent (zweiter Lauf aendert
 * nichts). Grund: GConfig->SetString+Flush(GEngineIni) kann in UE 5.8 still
 * nichts schreiben (Flush skippt, wenn FindBranch den Branch unter dem vollen
 * Pfad nicht findet, und meldet trotzdem Erfolg) - SaveCityAsMap schreibt
 * deshalb die Datei direkt ueber FFileHelper mit diesem Helfer und
 * verifiziert danach gegen die Platte.
 *
 * @param IniText Aktueller Text der DefaultEngine.ini (beliebig, auch leer).
 * @param MapRef  Map-Referenz, z. B. "/Game/Maps/WiesbadenCity.WiesbadenCity".
 * @return Neuer Ini-Text mit gesetzten Keys (Section ergaenzt wenn noetig).
 */
WIESBADENREAL_API FString ApplyDefaultMapToIniText(const FString& IniText, const FString& MapRef);

/**
 * Baut die Summary aus einem FWiesbadenCityData - gemeinsame Quelle fuer
 * Editor (WorldBuilder) und Runtime (GameInstance), ersetzt das fruehere
 * bUseMovedResults-Flag und die inline-Summary im GameInstance.
 *
 * Regionen/RegionAssets/TerrainGrid werden NIE gemovt und kommen immer aus
 * dem CityData. Die Ergebnis-Member RoadNetwork/Buildings/FurnitureLayout
 * sind im Editor-Erfolgspfad per MoveTemp aus dem CityData in die
 * Actor-Member gewandert (dort leer) - in dem Fall die gemovten Member als
 * Overrides uebergeben, sonst nullptr (= aus dem CityData lesen).
 *
 * @param TerrainMode Anzeige des Terrain-Kanals ("Landscape"/"Vorschau"),
 *        Editor-Erfolgspfad; sonst leer.
 */
WIESBADENREAL_API FWiesbadenBuildSummary BuildSummaryFromCityData(
	const FWiesbadenCityData& Data,
	const FString& Source,
	const FString& Result,
	double DurationSeconds,
	const FString& Timestamp,
	const FRoadNetwork* MovedRoadNetwork = nullptr,
	const TArray<FGeneratedBuilding>* MovedBuildings = nullptr,
	const FRoadFurnitureLayout* MovedFurniture = nullptr,
	const FString& TerrainMode = TEXT(""));

/**
 * Gemeinsame Information ueber den letzten Stadt-Build (Zeitpunkt, Dauer,
 * Ergebnis) - EINE USTRUCT fuer alle Halter: GameInstance (CityData),
 * CitySubsystem, GameMode und WorldBuilder (Details-Panel). Die frueheren
 * Einzel-Felder/-Getter bleiben als Komfort-Wrapper erhalten und lesen aus
 * dieser Struktur.
 *
 * Datenrein: keine UObject-/Editor-Abhaengigkeiten (headless testbar).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FLastBuildInfo
{
	GENERATED_BODY()

	/** Zeitpunkt des Build-Laufs (lokal); leer, wenn keiner gelaufen. */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	FString Timestamp;

	/** Dauer des Build-Laufs in Sekunden (0 = keiner gelaufen). */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	double DurationSeconds = 0.0;

	/** Ergebnis ("ok" / "abgebrochen: ..." / "fehlgeschlagen: ..."). */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	FString Result;

	/** True, wenn noch kein Build gelaufen ist (Timestamp leer). */
	bool IsEmpty() const { return Timestamp.IsEmpty(); }

	/**
	 * Kompakter Einzeiler ("Letzter Build: 83.4 s, ok (2026-08-16 15:45:00)"),
	 * identisch zwischen Editor und Runtime (FormatBuildSummaryLine). Leer,
	 * wenn kein Build gelaufen ist. Kein UFUNCTION (UHT verbietet UFUNCTION in
	 * Struct-Scope) - Blueprints erreichen den Einzeiler ueber die
	 * GetLastBuildSummary()-Wrapper der Getter-Klassen.
	 */
	FString GetSummary() const;

	/** Baut die Info aus einer FWiesbadenBuildSummary (Zeitpunkt/Dauer/Ergebnis). */
	static FLastBuildInfo FromBuildSummary(const FWiesbadenBuildSummary& Summary);
};
