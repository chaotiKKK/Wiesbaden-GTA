// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenWeatherFX.h"
#include "World/WiesbadenWeatherSystem.h"

namespace
{
	/** Wetter-Zustand mit einer Lage und einer Niederschlagsstaerke. */
	FWiesbadenWeatherState MakeState(ECityWeatherPreset Preset, float Rain01,
		float CloudCover01 = 0.0f, float SunElevationDeg = 45.0f,
		float AmbientLight = 1.0f, float Hours = 12.0f)
	{
		FWiesbadenWeatherState State;
		State.CurrentWeather = Preset;
		State.PreviousWeather = Preset;
		State.Blend01 = 1.0f;
		State.Intensity.Rain = Rain01;
		State.Intensity.CloudCover = CloudCover01;
		State.SunElevationDeg = SunElevationDeg;
		State.bIsNight = SunElevationDeg < 0.0f;
		State.AmbientLightMultiplier = AmbientLight;
		State.TimeOfDayHours = Hours;
		return State;
	}

	FWiesbadenWeatherOverlayParams OverlayFor(const FWiesbadenWeatherState& State)
	{
		return FWiesbadenWeatherOverlayParams::FromFXParams(
			FWiesbadenWeatherFXParams::FromWeatherState(State));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherOverlayDensityTest,
	"WiesbadenReal.Weather.OverlayDichte",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWeatherOverlayDensityTest::RunTest(const FString& Parameters)
{
	// -- 1. Klar heisst: nichts zeichnen. -----------------------------------
	//
	// Nicht nur "Staerke 0", sondern ausdruecklich unsichtbar - daran haengt
	// im Spiel das Abschalten des Volumes. Ein Vollbild-Durchgang, der nur
	// Nullen addiert, kostet trotzdem Fuellrate.
	const FWiesbadenWeatherOverlayParams Clear =
		OverlayFor(MakeState(ECityWeatherPreset::Clear, 0.0f));
	TestEqual(TEXT("Klar: kein Regen"), Clear.RainStrength, 0.0f);
	TestEqual(TEXT("Klar: kein Schnee"), Clear.SnowStrength, 0.0f);
	TestFalse(TEXT("Klar: Overlay unsichtbar"), Clear.IsVisible());

	// -- 2. Regen faellt, Schnee nicht - und umgekehrt. ---------------------
	const FWiesbadenWeatherOverlayParams Rain =
		OverlayFor(MakeState(ECityWeatherPreset::Rain, 1.0f));
	TestTrue(FString::Printf(TEXT("Regen voll (%.2f)"), Rain.RainStrength),
		FMath::IsNearlyEqual(Rain.RainStrength, 1.0f, 0.001f));
	TestEqual(TEXT("Bei Regen faellt KEIN Schnee"), Rain.SnowStrength, 0.0f);
	TestTrue(TEXT("Regen ist sichtbar"), Rain.IsVisible());

	const FWiesbadenWeatherOverlayParams Snow =
		OverlayFor(MakeState(ECityWeatherPreset::Snow, 1.0f));
	TestTrue(FString::Printf(TEXT("Schnee voll (%.2f)"), Snow.SnowStrength),
		FMath::IsNearlyEqual(Snow.SnowStrength, 1.0f, 0.001f));
	TestEqual(TEXT("Bei Schnee faellt KEIN Regen"), Snow.RainStrength, 0.0f);

	// -- 3. Der Kern: Regen und Schnee haben EIGENE Vollraten. --------------
	//
	// Die Partikelraten sind verschieden (1200 gegen 600 Partikel/s), weil
	// Flocken groesser sind und langsamer fallen. Wer beide durch dieselbe
	// Zahl teilt, bekommt bei identischem Wetter halb so dichten Schnee -
	// und merkt es nicht, weil beide Werte fuer sich plausibel aussehen.
	// Darum hier der Quervergleich bei GLEICHER Wetterintensitaet, nicht
	// zwei Einzelpruefungen nebeneinander.
	for (const float Intensity : { 0.25f, 0.5f, 0.75f })
	{
		const float R = OverlayFor(MakeState(ECityWeatherPreset::Rain, Intensity)).RainStrength;
		const float S = OverlayFor(MakeState(ECityWeatherPreset::Snow, Intensity)).SnowStrength;
		TestTrue(FString::Printf(
			TEXT("Bei Intensitaet %.2f faellt Schnee (%.3f) so dicht wie Regen (%.3f)"),
			Intensity, S, R),
			FMath::IsNearlyEqual(R, S, 0.001f));
		TestTrue(FString::Printf(TEXT("Intensitaet %.2f schlaegt durch (%.3f)"), Intensity, R),
			FMath::IsNearlyEqual(R, Intensity, 0.001f));
	}

	// -- 4. Nie ausserhalb 0..1. --------------------------------------------
	//
	// Das Material rechnet mit der Staerke als Schwelle gegen einen Hash in
	// 0..1. Ein Wert ueber 1 laesst JEDE Spalte regnen - die Scheibe wird
	// weiss. Ein Ueberlauf muss also hier haengenbleiben.
	const FWiesbadenWeatherOverlayParams Over =
		OverlayFor(MakeState(ECityWeatherPreset::Rain, 4.0f));
	TestTrue(FString::Printf(TEXT("Ueberzogene Intensitaet bleibt bei 1 (%.2f)"),
		Over.RainStrength), Over.RainStrength <= 1.0f);
	TestTrue(TEXT("Ueberzogene Intensitaet ist nicht negativ"), Over.RainStrength >= 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherOverlayLookTest,
	"WiesbadenReal.Weather.OverlayAussehen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWeatherOverlayLookTest::RunTest(const FString& Parameters)
{
	// -- 1. Wind stellt den Niederschlag schraeg. ---------------------------
	const FWiesbadenWeatherOverlayParams Still =
		OverlayFor(MakeState(ECityWeatherPreset::Rain, 1.0f, /*CloudCover=*/0.0f));
	const FWiesbadenWeatherOverlayParams Breezy =
		OverlayFor(MakeState(ECityWeatherPreset::Rain, 1.0f, /*CloudCover=*/1.0f));
	const FWiesbadenWeatherOverlayParams Storm =
		OverlayFor(MakeState(ECityWeatherPreset::Thunderstorm, 1.0f, /*CloudCover=*/1.0f));

	TestTrue(FString::Printf(TEXT("Wind schraegt an: still %.2f < windig %.2f"),
		Still.Slant, Breezy.Slant), Breezy.Slant > Still.Slant);
	TestTrue(FString::Printf(TEXT("Sturm schraeger als windig (%.2f > %.2f)"),
		Storm.Slant, Breezy.Slant), Storm.Slant > Breezy.Slant);

	// Auch der Sturm darf die Streifen nicht waagrecht legen - jenseits davon
	// laufen sie im Material aus dem Bild statt zu fallen.
	TestTrue(FString::Printf(TEXT("Schraeglage gedeckelt (%.2f)"), Storm.Slant),
		Storm.Slant <= 0.6f);
	TestTrue(TEXT("Windstille steht senkrecht oder fast"), Still.Slant < 0.2f);

	// -- 2. Die Tropfen nehmen die Lichtfarbe an. ---------------------------
	//
	// Sonst leuchtet der Regen bei Abendrot kalt-blau aus einer orangen Stadt.
	const FWiesbadenWeatherState EveningState =
		MakeState(ECityWeatherPreset::Rain, 1.0f, 0.3f, /*SunElevationDeg=*/8.0f,
			/*AmbientLight=*/0.7f, /*Hours=*/19.5f);
	const FWiesbadenWeatherFXParams EveningFX =
		FWiesbadenWeatherFXParams::FromWeatherState(EveningState);
	const FWiesbadenWeatherOverlayParams Evening =
		FWiesbadenWeatherOverlayParams::FromFXParams(EveningFX);
	TestEqual(TEXT("Tropfenfarbe ist die Lichtfarbe"), Evening.Tint, EveningFX.SunLightColor);

	// -- 3. Nachts gedaempft, aber nicht aus. -------------------------------
	//
	// Beide Richtungen pruefen: voll hell waere der Regen greller als die
	// naechtliche Stadt, ganz aus waere er unsichtbar - obwohl gerade
	// Strassenlaternen ihn zeigen.
	const FWiesbadenWeatherOverlayParams Night =
		OverlayFor(MakeState(ECityWeatherPreset::Rain, 1.0f, 0.2f,
			/*SunElevationDeg=*/-25.0f, /*AmbientLight=*/0.35f, /*Hours=*/2.0f));
	const FWiesbadenWeatherOverlayParams Noon =
		OverlayFor(MakeState(ECityWeatherPreset::Rain, 1.0f, 0.2f,
			/*SunElevationDeg=*/60.0f, /*AmbientLight=*/1.0f, /*Hours=*/12.0f));

	TestTrue(FString::Printf(TEXT("Nachts dunkler als mittags (%.2f < %.2f)"),
		Night.Brightness, Noon.Brightness), Night.Brightness < Noon.Brightness);
	TestTrue(FString::Printf(TEXT("Nachts noch sichtbar (%.2f)"), Night.Brightness),
		Night.Brightness >= 0.25f);
	TestTrue(TEXT("Mittags volle Helligkeit"),
		FMath::IsNearlyEqual(Noon.Brightness, 1.0f, 0.001f));

	// Untergrenze greift auch bei voellig schwarzem Umgebungslicht.
	const FWiesbadenWeatherOverlayParams Pitch =
		OverlayFor(MakeState(ECityWeatherPreset::Rain, 1.0f, 0.2f, -40.0f,
			/*AmbientLight=*/0.0f, /*Hours=*/3.0f));
	TestTrue(FString::Printf(TEXT("Auch ohne Umgebungslicht sichtbar (%.2f)"),
		Pitch.Brightness), Pitch.Brightness > 0.2f);

	return true;
}
