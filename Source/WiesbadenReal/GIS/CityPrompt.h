// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CityPrompt.generated.h"

/** Wetterlage, aus dem City-Prompt abgeleitet (speist spaeter das Wetter-System). */
UENUM(BlueprintType)
enum class ECityWeatherPreset : uint8
{
	Clear,
	Cloudy,
	Rain,
	Thunderstorm,
	Fog,
	Snow
};

/**
 * Strukturierte Ausgabe des City-Prompt-Parsers.
 *
 * Ein Text wie "dichte Gruenderzeit-Innenstadt mit Marktkirche, wolkig" wird
 * in einen Satz Pipeline-Parameter uebersetzt (WorldClaw-artig: Prompt ->
 * Spezifikation -> Welt), die die bestehende GIS-Pipeline steuern koennen.
 * Rein regelbasiert und deterministisch - dadurch ohne LLM/Cloud und in
 * Automation-Tests pruefbar.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FCityPromptSpec
{
	GENERATED_BODY()

	/** Originaltext (normalisiert). */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	FString RawPrompt;

	/** Kurzbeschreibung, z. B. "Gruenderzeit-Innenstadt, wolkig" (fuer HUD/Status). */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	FString DisplayName;

	/** Bebauungsdichte 0..1 (0 = locker/Vorort, 1 = dicht/Innenstadt). */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	float BuildingDensity = 0.5f;

	/** Fassadenstil-Gewichte je Materialvariante (0-5: Putz/Backstein/Sandstein/Glas/Beton/Fachwerk). */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	TMap<int32, float> FacadeVariantWeights;

	/** Im Prompt erkannte Landmarken (kanonische Namen, z. B. "Marktkirche"). */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	TArray<FString> DetectedLandmarks;

	/** Abgeleitete Wetterlage. */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	ECityWeatherPreset Weather = ECityWeatherPreset::Clear;

	/** Start-Tageszeit in Stunden (0..24); -1 = nicht gesetzt (System-Default). */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	float TimeOfDayHours = -1.0f;

	/** Verkehrsdichte 0..1 (0 = leer, 1 = Stau); speist spaeter die Verkehrs-KI. */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	float TrafficDensity = 0.5f;

	/** Skalierung der Gebaeudehoehen (0.6 = niedrig, 1.0 = Standard, 2.0 = Hochhaus). */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	float BuildingHeightScale = 1.0f;

	/**
	 * Schwellen der Terrain-Qualitaetskontrolle (CheckTerrainQuality), per
	 * Prompt einstellbar (z. B. "tile faktor 3.5", "hoehenspanne 5..2000").
	 * Defaults = CheckTerrainQuality-Defaults; ohne Erwaehnung im Prompt
	 * bleibt das Verhalten unveraendert.
	 */
	/** Tile darf hoechstens das MaxTileToOsmRatio-fache der OSM-Ausdehnung sein. */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	float TerrainMaxTileToOsmRatio = 2.0f;

	/** Untere Grenze der plausiblen Hoehenspanne in Metern. */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	float TerrainMinHeightSpanMeters = 1.0f;

	/** Obere Grenze der plausiblen Hoehenspanne in Metern. */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	float TerrainMaxHeightSpanMeters = 3000.0f;

	/** Anzahl erkannt-verarbeiteter Schluesselwoerter (Diagnose). */
	UPROPERTY(BlueprintReadOnly, Category = "CityPrompt")
	int32 MatchedKeywordCount = 0;
};

/** Regelbasierter Parser fuer Stadtbeschreibungen (rein, deterministisch). */
namespace CityPromptParser
{
	/** Parst einen Prompt in eine strukturierte Spezifikation. Leerer Text -> Defaults. */
	FCityPromptSpec Parse(const FString& Text);

	/**
	 * Kanonische Stil-Namen der Materialvarianten 0-5 (Gruenderzeit/Backstein/
	 * Sandstein/Moderne/Industrie/Fachwerk). Zentral, damit Parser und
	 * BuildingGenerator dieselben PromptStyle:-Schluessel erzeugen.
	 */
	TArray<FString> GetFacadeStyleNames();
}
