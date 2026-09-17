// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenWeatherSystem.h"

#include "World/WiesbadenSolar.h"

FWiesbadenWeatherIntensity FWiesbadenWeatherSystem::GetIntensityFor(ECityWeatherPreset Weather)
{
	FWiesbadenWeatherIntensity Out;
	switch (Weather)
	{
	case ECityWeatherPreset::Clear:
		Out.CloudCover = 0.15f;
		break;
	case ECityWeatherPreset::Cloudy:
		Out.Fog = 0.1f;
		Out.CloudCover = 0.7f;
		break;
	case ECityWeatherPreset::Rain:
		Out.Rain = 0.7f;
		Out.Fog = 0.3f;
		Out.CloudCover = 0.9f;
		break;
	case ECityWeatherPreset::Thunderstorm:
		Out.Rain = 1.0f;
		Out.Fog = 0.3f;
		Out.CloudCover = 1.0f;
		break;
	case ECityWeatherPreset::Fog:
		Out.Fog = 0.85f;
		Out.CloudCover = 0.4f;
		break;
	case ECityWeatherPreset::Snow:
		Out.Rain = 0.3f;
		Out.Fog = 0.4f;
		Out.CloudCover = 0.85f;
		break;
	default:
		break;
	}
	return Out;
}

void FWiesbadenWeatherSystem::SetTargetWeather(ECityWeatherPreset NewWeather)
{
	if (NewWeather == TargetWeather)
	{
		return;
	}

	// Vorherige Lage als Misch-Basis merken, Blend neu starten. Der private
	// Blend UND die Ausgabe (LastState.Blend01) muessen sofort konsistent sein,
	// sonst liefert GetState() bis zum naechsten Tick den alten Wert.
	LastState.PreviousWeather = LastState.CurrentWeather;
	LastState.CurrentWeather = NewWeather;
	TargetWeather = NewWeather;
	Blend01 = 0.0f;
	LastState.Blend01 = 0.0f;
}

void FWiesbadenWeatherSystem::SetTimeSource(EWiesbadenTimeSource Source, float InFixedHours)
{
	Settings.TimeSource = Source;
	float Hours = FMath::Fmod(InFixedHours, 24.0f);
	if (Hours < 0.0f) { Hours += 24.0f; }
	Settings.FixedHours = Hours;
}

void FWiesbadenWeatherSystem::UpdateClock(const FDateTime& NowUtc, const FDateTime& NowLocal)
{
	// Stunde der Spieluhr: Systemzeit oder feste Stunde.
	const float LocalNow = WiesbadenSolar::LocalHours(NowLocal);
	const float Hours = (Settings.TimeSource == EWiesbadenTimeSource::FixedHour) ? Settings.FixedHours : LocalNow;

	// Sonne fuer das heutige Datum zu dieser Ortsstunde: die UTC-Zeit um die
	// Differenz zur jetzigen Ortsstunde verschieben (Zeitzone bleibt implizit).
	const FDateTime SunUtc = NowUtc + FTimespan::FromHours(Hours - LocalNow);
	const WiesbadenSolar::FSunPosition Sun = WiesbadenSolar::ComputeSunPosition(
		SunUtc, WiesbadenSolar::WiesbadenLatitudeDeg, WiesbadenSolar::WiesbadenLongitudeDeg);

	LastState.TimeOfDayHours = Hours;
	LastState.SunElevationDeg = static_cast<float>(Sun.ElevationDeg);
	LastState.SunAzimuthDeg = static_cast<float>(Sun.AzimuthDeg);
	LastState.bIsNight = LastState.SunElevationDeg < 0.0f;
}

void FWiesbadenWeatherSystem::Tick(float DeltaSeconds)
{
	DeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, 10.0f);

	// Wetter-Uebergang sanft hochblenden.
	if (Blend01 < 1.0f)
	{
		Blend01 = FMath::Min(1.0f, Blend01 + DeltaSeconds / FMath::Max(Settings.TransitionSeconds, 0.1f));
	}

	// Intensitaeten zwischen vorheriger und aktueller Lage mischen.
	const FWiesbadenWeatherIntensity Prev = GetIntensityFor(LastState.PreviousWeather);
	const FWiesbadenWeatherIntensity Curr = GetIntensityFor(LastState.CurrentWeather);
	LastState.Intensity.Rain = FMath::Lerp(Prev.Rain, Curr.Rain, Blend01);
	LastState.Intensity.Fog = FMath::Lerp(Prev.Fog, Curr.Fog, Blend01);
	LastState.Intensity.CloudCover = FMath::Lerp(Prev.CloudCover, Curr.CloudCover, Blend01);

	// Umgebungslicht: 0.35 in der Nacht, 1.0 am hellen Mittag.
	const float Daylight = FMath::Clamp(LastState.SunElevationFactor(), 0.0f, 1.0f);
	LastState.AmbientLightMultiplier = 0.35f + 0.65f * Daylight;

	LastState.Blend01 = Blend01;
}
