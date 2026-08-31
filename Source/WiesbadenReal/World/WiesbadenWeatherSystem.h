// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/CityPrompt.h"

#include "WiesbadenWeatherSystem.generated.h"

/** Intensitaeten einer Wetterlage (fuer Rendering/HUD). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenWeatherIntensity
{
	GENERATED_BODY()

	/** Niederschlagsintensitaet (0..1). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Rain = 0.0f;

	/** Nebeldichte (0..1). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Fog = 0.0f;

	/** Bewoelkungsgrad (0..1). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float CloudCover = 0.0f;
};

/** Sichtbarer Wetter-/Tageszeit-Zustand (Ausgabe eines Ticks, fuer BP/HUD/Rendering). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenWeatherState
{
	GENERATED_BODY()

	/** Aktive Wetterlage (nach dem letzten Wechsel). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	ECityWeatherPreset CurrentWeather = ECityWeatherPreset::Clear;

	/** Vorherige Wetterlage (fuer den sanften Uebergang). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	ECityWeatherPreset PreviousWeather = ECityWeatherPreset::Clear;

	/** Uebergangsfaktor 0..1 (0 = gerade gewechselt, 1 = voll im neuen Wetter). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Blend01 = 1.0f;

	/** Tageszeit in Stunden (0..24). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float TimeOfDayHours = 9.0f;

	/** Sonnenstand: -1 (tiefste Nacht) .. +1 (Zenit); 0 = Horizont. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float SunElevationFactor = 0.0f;

	/** True, wenn die Sonne unter dem Horizont steht. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	bool bIsNight = false;

	/** Gemischte Intensitaeten (alt und neu ueber Blend01). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FWiesbadenWeatherIntensity Intensity;

	/** Umgebungslicht 0.35 (Nacht) .. 1.0 (Mittag). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float AmbientLightMultiplier = 1.0f;
};

/** Parameter der Wetter-Zustandsmaschine. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenWeatherSettings
{
	GENERATED_BODY()

	/** Uebergangsdauer zwischen zwei Wetterlagen (Sekunden Realzeit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.1"))
	float TransitionSeconds = 8.0f;

	/** Ingame-Stunden pro Real-Sekunde (SPEC 11: 1 Real-Stunde = 24 Ingame-Stunden). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.0"))
	//
	// Ein voller Tag dauerte hier 60 Minuten - nach einer halben Stunde Spielen
	// war Nacht, und weil die Stadt nachts keine eigene Lichtquelle hatte, war
	// sie ab da unbenutzbar. Drei Stunden je Tag lassen rund anderthalb Stunden
	// Tageslicht am Stueck und machen den Wechsel trotzdem erlebbar.
	float HoursPerRealSecond = 24.0f / 10800.0f;

	/** Start-Tageszeit (Stunden 0..24). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float StartTimeOfDayHours = 9.0f;
};

/**
 * Datenreine Wetter-Zustandsmaschine (kein Welt-/Actor-Zugriff, testbar).
 *
 * - Wetterlagen kommen als ECityWeatherPreset (z. B. aus dem City-Prompt); ein
 *   Wechsel blendet sanft ueber TransitionSeconds (Intensitaeten mischen sich
 *   zwischen alt und neu).
 * - Die Tageszeit schreitet mit HoursPerRealSecond fort; Sonnenstand und
 *   Umgebungslicht werden daraus abgeleitet.
 * - Ausgabe ist ein FWiesbadenWeatherState (Wetterlage, Blend, Zeit, Sonne,
 *   Intensitaeten, Licht) fuer Rendering/HUD.
 *
 * Die Zustandsmaschine ist deterministisch - dadurch in Automation- und
 * node-Tests pruefbar. Rendering (Sky, Licht, Partikel) bleibt dem Aufrufer.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenWeatherSystem
{
	GENERATED_BODY()

	/** Parameter der Zustandsmaschine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FWiesbadenWeatherSettings Settings;

	/** Ziel-Wetterlage (aus City-Prompt oder Blueprint). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	ECityWeatherPreset TargetWeather = ECityWeatherPreset::Clear;

	/** Aktuelle Ausgabe des letzten Ticks (fuer GetState/Blueprint). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FWiesbadenWeatherState LastState;

	/**
	 * Setzt die Ziel-Wetterlage. Gleiche Lage wird ignoriert (kein Blend-Reset);
	 * ein Wechsel startet einen sanften Uebergang (Blend01 -> 0, dann -> 1).
	 */
	void SetTargetWeather(ECityWeatherPreset NewWeather);

	/** Aktuelle Ziel-Wetterlage. */
	ECityWeatherPreset GetTargetWeather() const { return TargetWeather; }

	/** Setzt die Tageszeit direkt (Stunden 0..24, Wrap). */
	void SetTimeOfDay(float Hours);

	/** Treibt Wetter-Uebergang und Tageszeit einen Schritt weiter. */
	void Tick(float DeltaSeconds);

	/** Letzte Tick-Ausgabe (Wetterlage, Sonne, Intensitaeten, Licht). */
	const FWiesbadenWeatherState& GetState() const { return LastState; }

	/** Sonnenstand-Faktor (-1..+1) zur Tageszeit; 0 = Horizont, +1 = Zenit. */
	static float ComputeSunElevationFactor(float TimeOfDayHours);

	/** Intensitaeten einer Wetterlage (fuer die Uebergangs-Mischung). */
	static FWiesbadenWeatherIntensity GetIntensityFor(ECityWeatherPreset Weather);

private:
	float Blend01 = 1.0f;
};
