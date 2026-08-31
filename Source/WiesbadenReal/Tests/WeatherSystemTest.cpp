// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/CityPrompt.h"
#include "World/WiesbadenWeatherSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherTransitionTest,
	"WiesbadenReal.Weather.Transitions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWeatherTransitionTest::RunTest(const FString& Parameters)
{
	FWiesbadenWeatherSystem Weather;

	// Start: klares Wetter, Blend voll, kein Niederschlag.
	Weather.Tick(0.01f);
	TestEqual(TEXT("Start: klares Wetter"), Weather.GetState().CurrentWeather, ECityWeatherPreset::Clear);
	TestTrue(TEXT("Start: Blend voll"), Weather.GetState().Blend01 > 0.99f);
	TestTrue(TEXT("Start: kein Regen"), FMath::IsNearlyZero(Weather.GetState().Intensity.Rain, 0.001f));

	// Wechsel zu Regen: sofort aktive Lage, sanfter Uebergang.
	Weather.SetTargetWeather(ECityWeatherPreset::Rain);
	TestEqual(TEXT("Wechsel sofort aktiv"), Weather.GetState().CurrentWeather, ECityWeatherPreset::Rain);
	TestEqual(TEXT("Blend startet bei 0"), Weather.GetState().Blend01, 0.0f);

	Weather.Tick(Weather.Settings.TransitionSeconds * 0.5f);
	TestTrue(TEXT("Nach halber Dauer ~50% Blend"),
		FMath::IsNearlyEqual(Weather.GetState().Blend01, 0.5f, 0.05f));
	TestTrue(TEXT("Regen-Intensitaet blendet (0 -> 0.7)"),
		FMath::IsNearlyEqual(Weather.GetState().Intensity.Rain, 0.35f, 0.05f));

	Weather.Tick(Weather.Settings.TransitionSeconds * 0.5f);
	TestTrue(TEXT("Nach voller Dauer Blend 1"), Weather.GetState().Blend01 > 0.99f);
	TestTrue(TEXT("Regen voll aktiv"), FMath::IsNearlyEqual(Weather.GetState().Intensity.Rain, 0.7f, 0.01f));

	// Gleiche Lage erneut setzen: kein Blend-Reset.
	Weather.SetTargetWeather(ECityWeatherPreset::Rain);
	TestTrue(TEXT("Gleiche Lage resettet den Blend nicht"), Weather.GetState().Blend01 > 0.99f);

	// Wechsel zu Nebel: Dichte 0.85 nach Uebergang.
	Weather.SetTargetWeather(ECityWeatherPreset::Fog);
	Weather.Tick(Weather.Settings.TransitionSeconds);
	TestTrue(TEXT("Nebel voll aktiv"), FMath::IsNearlyEqual(Weather.GetState().Intensity.Fog, 0.85f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherTimeOfDayTest,
	"WiesbadenReal.Weather.TimeOfDay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWeatherTimeOfDayTest::RunTest(const FString& Parameters)
{
	FWiesbadenWeatherSystem Weather;

	// Ein voller Ingame-Tag dauert 3 Realstunden: 9 Uhr + 24h = wieder 9 Uhr.
	// In <=10s-Schritten fahren, weil Tick grosse Deltas auf 10s clamp't
	// (Schutz vor Zeitspruengen).
	//
	// Hier standen 60 Minuten je Tag. Nach einer halben Stunde Spielen war
	// Nacht - und weil die Stadt nachts keine eigene Lichtquelle hatte, ab da
	// unbenutzbar. Der Wert wird aus den Einstellungen abgeleitet und nicht
	// erneut als Zahl hingeschrieben, damit Test und Verhalten nicht
	// auseinanderlaufen koennen.
	const float SecondsPerFullDay = 24.0f / FWiesbadenWeatherSettings().HoursPerRealSecond;
	const int32 Steps = FMath::RoundToInt(SecondsPerFullDay / 10.0f);

	for (int32 i = 0; i < Steps; ++i)
	{
		Weather.Tick(10.0f);
	}
	TestTrue(
		FString::Printf(TEXT("Ein voller Tag dauert %.0f Minuten"), SecondsPerFullDay / 60.0f),
		FMath::IsNearlyEqual(Weather.GetState().TimeOfDayHours, 9.0f, 0.05f));

	// +12h -> 21 Uhr. Schrittzahl ebenfalls aus den Einstellungen ableiten.
	for (int32 i = 0; i < Steps / 2; ++i)
	{
		Weather.Tick(10.0f);
	}
	TestTrue(TEXT("+12 Ingame-Stunden -> 21 Uhr"),
		FMath::IsNearlyEqual(Weather.GetState().TimeOfDayHours, 21.0f, 0.05f));

	// Sonnenstand: Mittag Zenit, Mitternacht tiefste Nacht.
	Weather.SetTimeOfDay(12.0f);
	Weather.Tick(0.0f);
	TestTrue(TEXT("Mittag: Sonne im Zenit"), Weather.GetState().SunElevationFactor > 0.99f);
	TestTrue(TEXT("Mittag: kein Nacht-Flag"), !Weather.GetState().bIsNight);
	TestTrue(TEXT("Mittag: helles Umgebungslicht"),
		Weather.GetState().AmbientLightMultiplier > 0.99f);

	Weather.SetTimeOfDay(0.0f);
	Weather.Tick(0.0f);
	TestTrue(TEXT("Mitternacht: Sonne tief"), Weather.GetState().SunElevationFactor < -0.99f);
	TestTrue(TEXT("Mitternacht: Nacht-Flag"), Weather.GetState().bIsNight);
	TestTrue(TEXT("Mitternacht: gedimmtes Licht"),
		FMath::IsNearlyEqual(Weather.GetState().AmbientLightMultiplier, 0.35f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherCityPromptTest,
	"WiesbadenReal.Weather.CityPrompt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWeatherCityPromptTest::RunTest(const FString& Parameters)
{
	// Das Wetter-System konsumiert die ECityWeatherPreset aus dem City-Prompt:
	// "Gewitter" im Prompt -> Thunderstorm -> volle Regen-Intensitaet.
	const FCityPromptSpec Spec = CityPromptParser::Parse(
		TEXT("dichter Innenstadtkern mit Gewitter und Regen, bewoelkt"));
	TestEqual(TEXT("Prompt erkennt Gewitter"), Spec.Weather, ECityWeatherPreset::Thunderstorm);

	FWiesbadenWeatherSystem Weather;
	Weather.SetTargetWeather(Spec.Weather);
	Weather.Tick(Weather.Settings.TransitionSeconds);

	TestEqual(TEXT("Wetter-System folgt dem Prompt"),
		Weather.GetState().CurrentWeather, ECityWeatherPreset::Thunderstorm);
	TestTrue(TEXT("Gewitter: Regen-Intensitaet 1.0"),
		FMath::IsNearlyEqual(Weather.GetState().Intensity.Rain, 1.0f, 0.01f));
	TestTrue(TEXT("Gewitter: volle Bewoelkung"),
		FMath::IsNearlyEqual(Weather.GetState().Intensity.CloudCover, 1.0f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherSetTimeConsistencyTest,
	"WiesbadenReal.Weather.SetTimeConsistency",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Nach SetTimeOfDay muss der Zustand in sich stimmen.
 *
 * Die Funktion setzte frueher nur die Stunde; Sonnenstand und Nachtflagge
 * wurden erst im naechsten Tick nachgezogen. Eine Abfrage direkt danach
 * lieferte damit einen widerspruechlichen Zustand - gemessen "23:00 Uhr,
 * Sonnenstand 0,72, Nacht: nein". Wer darauf eine Entscheidung stuetzt, etwa
 * die Lichtautomatik des Fahrzeugs, entscheidet nach der ALTEN Tageszeit.
 */
bool FWeatherSetTimeConsistencyTest::RunTest(const FString& Parameters)
{
	FWiesbadenWeatherSystem Weather;

	// Mitternacht: Sonne unter dem Horizont, ohne dass ein Tick noetig waere.
	Weather.SetTimeOfDay(0.0f);
	const FWiesbadenWeatherState Midnight = Weather.GetState();

	TestEqual(TEXT("Stunde uebernommen"), Midnight.TimeOfDayHours, 0.0f);
	TestEqual(
		TEXT("Sonnenstand passt ohne Tick zur Stunde"),
		Midnight.SunElevationFactor,
		FWiesbadenWeatherSystem::ComputeSunElevationFactor(0.0f));

	// Mittag: deutlich hoeher als um Mitternacht.
	Weather.SetTimeOfDay(12.0f);
	const FWiesbadenWeatherState Noon = Weather.GetState();

	TestEqual(TEXT("Mittagsstunde uebernommen"), Noon.TimeOfDayHours, 12.0f);
	TestTrue(TEXT("Mittag steht hoeher als Mitternacht"),
		Noon.SunElevationFactor > Midnight.SunElevationFactor);

	// Nachtflagge muss zur Sonnenhoehe passen, nicht zur vorherigen Stunde.
	TestEqual(TEXT("Nachtflagge folgt dem Sonnenstand"),
		Noon.bIsNight, Noon.SunElevationFactor < 0.0f);

	// Negative und ueberlaufende Stunden werden in [0,24) gefaltet.
	Weather.SetTimeOfDay(-1.0f);
	TestTrue(TEXT("Negative Stunde wird gefaltet"),
		Weather.GetState().TimeOfDayHours >= 0.0f && Weather.GetState().TimeOfDayHours < 24.0f);

	Weather.SetTimeOfDay(30.0f);
	TestTrue(TEXT("Ueberlaufende Stunde wird gefaltet"),
		Weather.GetState().TimeOfDayHours >= 0.0f && Weather.GetState().TimeOfDayHours < 24.0f);

	return true;
}
