// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenWeatherSystem.h"

float FWiesbadenWeatherSystem::ComputeSunElevationFactor(float TimeOfDayHours)
{
	// Sonne geht um 6 Uhr auf (Faktor 0), steht um 12 Uhr im Zenit (+1), geht
	// um 18 Uhr unter (0) und erreicht um 0 Uhr den tiefsten Stand (-1).
	const float Hours = FMath::Fmod(TimeOfDayHours, 24.0f);
	return FMath::Sin((Hours - 6.0f) * (2.0f * PI / 24.0f));
}

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

void FWiesbadenWeatherSystem::SetTimeOfDay(float Hours)
{
	LastState.TimeOfDayHours = FMath::Fmod(Hours, 24.0f);
	if (LastState.TimeOfDayHours < 0.0f)
	{
		LastState.TimeOfDayHours += 24.0f;
	}

	// Abgeleitete Werte SOFORT nachziehen.
	//
	// Vorher setzte diese Funktion nur die Stunde; Sonnenstand und Nachtflagge
	// wurden erst im naechsten Tick nachgerechnet. Bis dahin stand ein
	// widerspruechlicher Zustand in der Struktur - eine Abfrage direkt nach dem
	// Setzen meldete "23:00 Uhr, Sonnenstand 0,72, Nacht: nein". Wer daraufhin
	// eine Entscheidung trifft (etwa die Lichtautomatik), trifft sie auf Basis
	// der alten Tageszeit.
	LastState.SunElevationFactor = ComputeSunElevationFactor(LastState.TimeOfDayHours);
	LastState.bIsNight = LastState.SunElevationFactor < 0.0f;
}

void FWiesbadenWeatherSystem::Tick(float DeltaSeconds)
{
	DeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, 10.0f);

	// Wetter-Uebergang sanft hochblenden.
	if (Blend01 < 1.0f)
	{
		Blend01 = FMath::Min(1.0f, Blend01 + DeltaSeconds / FMath::Max(Settings.TransitionSeconds, 0.1f));
	}

	// Tageszeit fortschreiten (0..24, Wrap).
	LastState.TimeOfDayHours = FMath::Fmod(
		LastState.TimeOfDayHours + DeltaSeconds * Settings.HoursPerRealSecond, 24.0f);

	// Sonnenstand + Nacht.
	LastState.SunElevationFactor = ComputeSunElevationFactor(LastState.TimeOfDayHours);
	LastState.bIsNight = LastState.SunElevationFactor < 0.0f;

	// Intensitaeten zwischen vorheriger und aktueller Lage mischen.
	const FWiesbadenWeatherIntensity Prev = GetIntensityFor(LastState.PreviousWeather);
	const FWiesbadenWeatherIntensity Curr = GetIntensityFor(LastState.CurrentWeather);
	LastState.Intensity.Rain = FMath::Lerp(Prev.Rain, Curr.Rain, Blend01);
	LastState.Intensity.Fog = FMath::Lerp(Prev.Fog, Curr.Fog, Blend01);
	LastState.Intensity.CloudCover = FMath::Lerp(Prev.CloudCover, Curr.CloudCover, Blend01);

	// Umgebungslicht: 0.35 in der Nacht, 1.0 am hellen Mittag.
	const float Daylight = FMath::Clamp(LastState.SunElevationFactor, 0.0f, 1.0f);
	LastState.AmbientLightMultiplier = 0.35f + 0.65f * Daylight;

	LastState.Blend01 = Blend01;
}
