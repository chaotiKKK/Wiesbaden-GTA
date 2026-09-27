// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Audio/WiesbadenAudioPropagation.h"
#include "Vehicles/WiesbadenEngineAudio.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbAudioDopplerTest,
	"WiesbadenReal.Audio.Propagation.Doppler",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbAudioDopplerTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenAudioPropagation;

	// Stillstand: exakt unveraendert.
	TestEqual(TEXT("Kein Doppler im Stand"), DopplerPitchFactor(0.0f), 1.0f);

	// Annaeherung hebt, Entfernung senkt die Tonhoehe.
	TestTrue(TEXT("Annaherung hebt den Ton"), DopplerPitchFactor(30.0f) > 1.05f);
	TestTrue(TEXT("Entfernung senkt den Ton"), DopplerPitchFactor(-30.0f) < 0.95f);

	// Symmetrie fuer kleine Geschwindigkeiten: f(v) * f(-v) ~ 1.
	const float Sym = DopplerPitchFactor(20.0f) * DopplerPitchFactor(-20.0f);
	TestTrue(TEXT("Doppler symmetrisch bei kleinen v"), FMath::Abs(Sym - 1.0f) < 0.01f);

	// Extreme Geschwindigkeiten bleiben im Rahmen (keine Sirene, kein Absturz).
	TestTrue(TEXT("Doppler oben begrenzt"), DopplerPitchFactor(500.0f) <= 1.4f);
	TestTrue(TEXT("Doppler unten begrenzt"), DopplerPitchFactor(-500.0f) >= 0.7f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbAudioSpaceClassTest,
	"WiesbadenReal.Audio.Propagation.SpaceClassification",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbAudioSpaceClassTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenAudioPropagation;

	TestEqual(TEXT("Freie Flaeche bleibt draussen"),
		ClassifySpace(0.0f, 0.1f, 50.0f), EWbReverbSpace::Outdoor);
	TestEqual(TEXT("Geschlossener Raum mit niedriger Decke ist Zimmer"),
		ClassifySpace(0.2f, 0.9f, 2.8f), EWbReverbSpace::Indoor);
	TestEqual(TEXT("Geschlossener Raum mit hoher Decke ist Halle"),
		ClassifySpace(0.2f, 0.9f, 6.5f), EWbReverbSpace::Hall);
	TestEqual(TEXT("Halb offen mit verdecktem Himmel ist Tunnel"),
		ClassifySpace(0.8f, 0.5f, 3.0f), EWbReverbSpace::Tunnel);
	TestEqual(TEXT("Halb offen mit freiem Himmel bleibt draussen"),
		ClassifySpace(0.0f, 0.5f, 3.0f), EWbReverbSpace::Outdoor);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbAudioReverbTuningTest,
	"WiesbadenReal.Audio.Propagation.ReverbTuning",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbAudioReverbTuningTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenAudioPropagation;

	const FWbReverbTuning Outdoor = ReverbTuningForSpace(EWbReverbSpace::Outdoor);
	const FWbReverbTuning Indoor = ReverbTuningForSpace(EWbReverbSpace::Indoor);
	const FWbReverbTuning Hall = ReverbTuningForSpace(EWbReverbSpace::Hall);
	const FWbReverbTuning Tunnel = ReverbTuningForSpace(EWbReverbSpace::Tunnel);

	TestTrue(TEXT("Outdoor ist by-passed"), Outdoor.bBypass);
	TestEqual(TEXT("Outdoor sendet keinen Nassanteil"), Outdoor.LateGain, 0.0f);
	TestFalse(TEXT("Indoor hat Hall"), Indoor.bBypass);
	TestFalse(TEXT("Halle hat Hall"), Hall.bBypass);
	TestFalse(TEXT("Tunnel hat Hall"), Tunnel.bBypass);

	TestTrue(TEXT("Zimmer-Nachhall kuerzer als Halle"), Indoor.DecayTime < Hall.DecayTime);
	TestTrue(TEXT("Tunnel diffuser als Halle? nein - Tunnel ist ROHR: dichter"),
		Tunnel.Diffusion < Hall.Diffusion);
	TestTrue(TEXT("Send: Zimmer trockener als Halle"),
		ReverbSendForSpace(EWbReverbSpace::Indoor) < ReverbSendForSpace(EWbReverbSpace::Hall));
	TestEqual(TEXT("Outdoor-Send ist null"),
		ReverbSendForSpace(EWbReverbSpace::Outdoor), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbAudioDayNightTest,
	"WiesbadenReal.Audio.Propagation.DayNight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbAudioDayNightTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenAudioPropagation;

	TestEqual(TEXT("Mittags volles Tag-Bett"), DayBedGain(13.0f), 1.0f);
	TestEqual(TEXT("Nachts kein Tag-Bett"), DayBedGain(23.0f), 0.0f);
	TestEqual(TEXT("Abenddaemmerung 20 Uhr halb"), DayBedGain(20.0f), 0.5f);
	TestEqual(TEXT("Morgendaemmerung 6 Uhr halb"), DayBedGain(6.0f), 0.5f);
	TestEqual(TEXT("21 Uhr Tag zu Ende"), DayBedGain(21.0f), 0.0f);

	for (const float Hour : { 2.0f, 13.0f, 19.5f, 23.0f })
	{
		TestTrue(TEXT("Tag + Nacht ergibt 1"),
			FMath::IsNearlyEqual(DayBedGain(Hour) + NightBedGain(Hour), 1.0f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbAudioEngineParamTest,
	"WiesbadenReal.Audio.Propagation.EngineParamPairs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbAudioEngineParamTest::RunTest(const FString& Parameters)
{
	// Die Parameter-Leitung ist die MetaSound-Schnittstelle der Fahrzeug-Layer:
	// dieselben Werte wie die prozedurale Referenzsynthese.
	FWiesbadenEngineAudioParams Params;
	Params.EngineRpm = 2700.0f;
	Params.Throttle = 0.75f;
	Params.SpeedKmh = 42.0f;
	Params.bEngineRunning = true;
	Params.bHorn = true;

	const TArray<TPair<FName, float>> Pairs =
		WiesbadenAudioPropagation::EngineParamPairs(Params);

	TestEqual(TEXT("Fuenf Parameter"), Pairs.Num(), 5);
	if (Pairs.Num() == 5)
	{
		TestEqual(TEXT("Rpm"), Pairs[0].Key, FName(TEXT("Rpm")));
		TestEqual(TEXT("Rpm-Wert"), Pairs[0].Value, 2700.0f);
		TestEqual(TEXT("Throttle"), Pairs[1].Key, FName(TEXT("Throttle")));
		TestEqual(TEXT("SpeedKmh"), Pairs[2].Key, FName(TEXT("SpeedKmh")));
		TestEqual(TEXT("EngineRunning"), Pairs[3].Key, FName(TEXT("EngineRunning")));
		TestEqual(TEXT("Running an"), Pairs[3].Value, 1.0f);
		TestEqual(TEXT("Horn"), Pairs[4].Key, FName(TEXT("Horn")));
		TestEqual(TEXT("Horn an"), Pairs[4].Value, 1.0f);
	}

	Params.bEngineRunning = false;
	Params.bHorn = false;
	const TArray<TPair<FName, float>> Off =
		WiesbadenAudioPropagation::EngineParamPairs(Params);
	TestEqual(TEXT("Running aus"), Off[3].Value, 0.0f);
	TestEqual(TEXT("Horn aus"), Off[4].Value, 0.0f);

	return true;
}
