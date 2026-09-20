// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/CityPrompt.h"

#include "WiesbadenWeatherSystem.generated.h"

/** Zeitquelle der Spieluhr. */
UENUM(BlueprintType)
enum class EWiesbadenTimeSource : uint8
{
	SystemClock	UMETA(DisplayName = "Lokale Systemzeit"),
	FixedHour	UMETA(DisplayName = "Feste Stunde")
};

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

	/** Tageszeit in Stunden (0..24) - Systemuhr oder feste Stunde (Zeitquelle). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float TimeOfDayHours = 12.0f;

	/** Echter Sonnenstand in Grad (WiesbadenSolar): Hoehe ueber dem Horizont
	 *  (negativ = Nacht) und Azimut ab Nord ueber Ost. Einzige Darstellung der
	 *  Sonne; der Faktor unten ist daraus abgeleitet. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float SunElevationDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float SunAzimuthDeg = 180.0f;

	/** True, wenn die Sonne unter dem Horizont steht. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	bool bIsNight = false;

	/** Sonnenstand als Faktor -1..+1 (0 = Horizont, +1 = Zenit): der Sinus der
	 *  Hoehe. Fuer Schwellwerte (Lichtautomatik, Mondlicht-Floor). */
	float SunElevationFactor() const { return FMath::Sin(FMath::DegreesToRadians(SunElevationDeg)); }

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

	/** Woher die Uhr kommt: lokale Systemzeit (Spiel) oder eine feste Stunde
	 *  (-WbTime, Prompt). Beide treiben dieselbe astronomische Sonne fuer das
	 *  heutige Datum; bei fester Stunde steht nur die Uhr still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	EWiesbadenTimeSource TimeSource = EWiesbadenTimeSource::SystemClock;

	/** Feste Stunde 0..24 (nur bei TimeSource = FixedHour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float FixedHours = 12.0f;
};

/**
 * Datenreine Wetter-Zustandsmaschine (kein Welt-/Actor-Zugriff, testbar).
 *
 * - Wetterlagen kommen als ECityWeatherPreset (z. B. aus dem City-Prompt); ein
 *   Wechsel blendet sanft ueber TransitionSeconds (Intensitaeten mischen sich
 *   zwischen alt und neu).
 * - Die Uhr kommt von aussen (UpdateClock mit UTC + Ortszeit): Systemuhr oder
 *   feste Stunde; Sonnenstand (WiesbadenSolar) und Umgebungslicht folgen daraus.
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

	/** Zeitquelle waehlen; feste Stunden werden in [0,24) gefaltet. */
	void SetTimeSource(EWiesbadenTimeSource Source, float InFixedHours = 12.0f);

	/**
	 * Uhr und Sonne aus dem Jetzt (UTC + Ortszeit) ableiten - der EINZIGE Ort, der
	 * Tageszeit, Sonnenhoehe/-azimut und Nachtflagge schreibt. Bei fester Stunde
	 * wird die Sonne fuer das heutige Datum zu dieser Ortsstunde gerechnet.
	 */
	void UpdateClock(const FDateTime& NowUtc, const FDateTime& NowLocal);

	/** Treibt den Wetter-Uebergang einen Schritt weiter und mischt Intensitaet/Licht. */
	void Tick(float DeltaSeconds);

	/** Letzte Tick-Ausgabe (Wetterlage, Sonne, Intensitaeten, Licht). */
	const FWiesbadenWeatherState& GetState() const { return LastState; }

	/** Intensitaeten einer Wetterlage (fuer die Uebergangs-Mischung). */
	static FWiesbadenWeatherIntensity GetIntensityFor(ECityWeatherPreset Weather);

private:
	float Blend01 = 1.0f;
};
