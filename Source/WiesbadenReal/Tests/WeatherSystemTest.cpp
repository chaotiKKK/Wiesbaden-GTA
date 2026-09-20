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
	// Sommertag in Wiesbaden (MESZ = UTC+2): 12:00 Ortszeit = 10:00 UTC.
	const FDateTime Utc(2026, 6, 21, 10, 0, 0);
	const FDateTime Local(2026, 6, 21, 12, 0, 0);

	// Systemuhr: die Spieluhr zeigt die Ortszeit, die Sonne steht hoch.
	FWiesbadenWeatherSystem Weather;
	Weather.SetTimeSource(EWiesbadenTimeSource::SystemClock);
	Weather.UpdateClock(Utc, Local);
	Weather.Tick(0.0f);
	TestTrue(TEXT("Systemuhr: 12 Uhr"), FMath::IsNearlyEqual(Weather.GetState().TimeOfDayHours, 12.0f, 0.01f));
	TestTrue(TEXT("Sommer-Mittag: Sonne ueber 55 Grad"), Weather.GetState().SunElevationDeg > 55.0f);
	TestTrue(TEXT("Mittag: kein Nacht-Flag"), !Weather.GetState().bIsNight);
	TestTrue(TEXT("Mittag: helles Umgebungslicht"), Weather.GetState().AmbientLightMultiplier > 0.9f);

	// Zwoelf Stunden spaeter: Mitternacht, Sonne unter dem Horizont, Licht gedimmt.
	Weather.UpdateClock(Utc + FTimespan::FromHours(12.0), Local + FTimespan::FromHours(12.0));
	Weather.Tick(0.0f);
	TestTrue(TEXT("Systemuhr: 0 Uhr"), FMath::IsNearlyEqual(Weather.GetState().TimeOfDayHours, 0.0f, 0.01f));
	TestTrue(TEXT("Mitternacht: Sonne unter dem Horizont"), Weather.GetState().SunElevationDeg < -10.0f);
	TestTrue(TEXT("Mitternacht: Nacht-Flag"), Weather.GetState().bIsNight);
	TestTrue(TEXT("Mitternacht: gedimmtes Licht"),
		FMath::IsNearlyEqual(Weather.GetState().AmbientLightMultiplier, 0.35f, 0.01f));

	// Feste Stunde (-WbTime=13): Uhr steht, Sonne wird fuer HEUTE um 13 Uhr gerechnet -
	// auch wenn es real Mitternacht ist. Ein spaeterer Tick aendert daran nichts.
	Weather.SetTimeSource(EWiesbadenTimeSource::FixedHour, 13.0f);
	Weather.UpdateClock(Utc + FTimespan::FromHours(12.0), Local + FTimespan::FromHours(12.0));
	Weather.Tick(10.0f);
	TestTrue(TEXT("feste Stunde: 13 Uhr"), FMath::IsNearlyEqual(Weather.GetState().TimeOfDayHours, 13.0f, 0.01f));
	TestTrue(TEXT("feste Stunde: Tag trotz realer Nacht"), !Weather.GetState().bIsNight && Weather.GetState().SunElevationDeg > 50.0f);
	TestTrue(TEXT("feste Stunde: Sonne im Sueden"), FMath::Abs(Weather.GetState().SunAzimuthDeg - 180.0f) < 25.0f);

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
 * Nach dem Setzen einer festen Stunde muss der Zustand nach UpdateClock in sich
 * stimmen: Stunde, Sonnenhoehe und Nachtflagge gehoeren zusammen - wer direkt
 * danach entscheidet (etwa die Lichtautomatik), darf keinen alten Stand sehen.
 */
bool FWeatherSetTimeConsistencyTest::RunTest(const FString& Parameters)
{
	const FDateTime Utc(2026, 3, 20, 12, 0, 0);
	const FDateTime Local(2026, 3, 20, 13, 0, 0);
	FWiesbadenWeatherSystem Weather;

	// Mitternacht: Sonne unter dem Horizont, Flagge passt - ohne dass ein Tick noetig waere.
	Weather.SetTimeSource(EWiesbadenTimeSource::FixedHour, 0.0f);
	Weather.UpdateClock(Utc, Local);
	const FWiesbadenWeatherState Midnight = Weather.GetState();
	TestEqual(TEXT("Stunde uebernommen"), Midnight.TimeOfDayHours, 0.0f);
	TestTrue(TEXT("Mitternacht: Sonne unter dem Horizont"), Midnight.SunElevationDeg < 0.0f);
	TestEqual(TEXT("Nachtflagge folgt dem Sonnenstand"), Midnight.bIsNight, Midnight.SunElevationDeg < 0.0f);

	// Mittag: deutlich hoeher als um Mitternacht; Faktor = Sinus der Hoehe.
	Weather.SetTimeSource(EWiesbadenTimeSource::FixedHour, 12.0f);
	Weather.UpdateClock(Utc, Local);
	const FWiesbadenWeatherState Noon = Weather.GetState();
	TestEqual(TEXT("Mittagsstunde uebernommen"), Noon.TimeOfDayHours, 12.0f);
	TestTrue(TEXT("Mittag steht hoeher als Mitternacht"), Noon.SunElevationDeg > Midnight.SunElevationDeg);
	TestTrue(TEXT("Faktor = sin(Hoehe)"),
		FMath::IsNearlyEqual(Noon.SunElevationFactor(), FMath::Sin(FMath::DegreesToRadians(Noon.SunElevationDeg)), 1e-5f));
	TestFalse(TEXT("Mittag: Tag"), Noon.bIsNight);

	// Negative und ueberlaufende Stunden werden in [0,24) gefaltet.
	Weather.SetTimeSource(EWiesbadenTimeSource::FixedHour, -1.0f);
	TestTrue(TEXT("Negative Stunde wird gefaltet"), FMath::IsNearlyEqual(Weather.Settings.FixedHours, 23.0f, 0.001f));
	Weather.SetTimeSource(EWiesbadenTimeSource::FixedHour, 30.0f);
	TestTrue(TEXT("Ueberlaufende Stunde wird gefaltet"), FMath::IsNearlyEqual(Weather.Settings.FixedHours, 6.0f, 0.001f));

	return true;
}
