// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicles/WiesbadenCarLightsComponent.h"
#include "Vehicles/WiesbadenEngineAudio.h"

/**
 * Lichtanlage: Schaltlogik und Blinktakt.
 *
 * Geprueft wird ausschliesslich die datenreine Logik - kein Actor, keine
 * Lichtquellen, keine Welt. Genau das ist der Grund, warum die Schaltregeln
 * als statische Funktionen ausgelagert sind: eine Blinkfrequenz laesst sich
 * nicht sinnvoll "durch Hinsehen" pruefen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarLightsTest,
	"WiesbadenReal.Vehicles.CarLights",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCarLightsTest::RunTest(const FString& Parameters)
{
	// -- 1. Blinktakt -------------------------------------------------------
	// 1,5 Hz bedeutet 0,667 s Periode: die erste Haelfte hell, die zweite dunkel.
	constexpr float Hz = 1.5f;
	const float Period = 1.0f / Hz;

	TestTrue(TEXT("Blinker startet hell"),
		UWiesbadenCarLightsComponent::ComputeBlinkOn(0.0f, Hz));
	TestTrue(TEXT("Kurz vor Halbperiode noch hell"),
		UWiesbadenCarLightsComponent::ComputeBlinkOn(Period * 0.49f, Hz));
	TestFalse(TEXT("Nach Halbperiode dunkel"),
		UWiesbadenCarLightsComponent::ComputeBlinkOn(Period * 0.51f, Hz));
	TestFalse(TEXT("Kurz vor Periodenende dunkel"),
		UWiesbadenCarLightsComponent::ComputeBlinkOn(Period * 0.99f, Hz));
	TestTrue(TEXT("Nach voller Periode wieder hell"),
		UWiesbadenCarLightsComponent::ComputeBlinkOn(Period * 1.01f, Hz));

	// Tastverhaeltnis ueber viele Perioden: muss bei 50 % liegen. Ein
	// schleichender Fehler in der Phasenrechnung faellt hier auf, auch wenn
	// die Einzelpunkte oben noch stimmen.
	int32 OnCount = 0;
	constexpr int32 Steps = 6000;
	for (int32 i = 0; i < Steps; ++i)
	{
		const float T = static_cast<float>(i) * (Period * 10.0f / static_cast<float>(Steps));
		if (UWiesbadenCarLightsComponent::ComputeBlinkOn(T, Hz))
		{
			++OnCount;
		}
	}
	const float Duty = static_cast<float>(OnCount) / static_cast<float>(Steps);
	TestTrue(FString::Printf(TEXT("Tastverhaeltnis ~50%% (ist %.3f)"), Duty),
		Duty > 0.47f && Duty < 0.53f);

	// Ungueltige Frequenz darf nicht dauerhaft leuchten lassen.
	TestFalse(TEXT("Frequenz 0 laesst den Blinker aus"),
		UWiesbadenCarLightsComponent::ComputeBlinkOn(1.0f, 0.0f));

	// -- 2. Fahrlicht-Stufen ------------------------------------------------
	using EH = EWiesbadenHeadlightMode;
	TestEqual(TEXT("Aus -> Standlicht"),
		UWiesbadenCarLightsComponent::GetNextHeadlightMode(EH::Off), EH::Parking);
	TestEqual(TEXT("Standlicht -> Abblendlicht"),
		UWiesbadenCarLightsComponent::GetNextHeadlightMode(EH::Parking), EH::LowBeam);
	TestEqual(TEXT("Abblendlicht -> Fernlicht"),
		UWiesbadenCarLightsComponent::GetNextHeadlightMode(EH::LowBeam), EH::HighBeam);
	TestEqual(TEXT("Fernlicht -> Aus (Zyklus geschlossen)"),
		UWiesbadenCarLightsComponent::GetNextHeadlightMode(EH::HighBeam), EH::Off);

	// -- 3. Blinkerlogik ----------------------------------------------------
	using EI = EWiesbadenIndicatorMode;
	TestEqual(TEXT("Aus + links = links"),
		UWiesbadenCarLightsComponent::ApplyIndicatorToggle(EI::Off, EI::Left), EI::Left);
	TestEqual(TEXT("Links erneut = aus (Hebel rastet zurueck)"),
		UWiesbadenCarLightsComponent::ApplyIndicatorToggle(EI::Left, EI::Left), EI::Off);
	TestEqual(TEXT("Links + rechts = rechts"),
		UWiesbadenCarLightsComponent::ApplyIndicatorToggle(EI::Left, EI::Right), EI::Right);

	// Warnblinkanlage darf nicht durch einen Einzelblinker verschwinden -
	// wer sie eingeschaltet hat, will sie bewusst wieder ausschalten.
	TestEqual(TEXT("Warnblinker bleibt bei Blinker links"),
		UWiesbadenCarLightsComponent::ApplyIndicatorToggle(EI::Hazard, EI::Left), EI::Hazard);
	TestEqual(TEXT("Warnblinker erneut = aus"),
		UWiesbadenCarLightsComponent::ApplyIndicatorToggle(EI::Hazard, EI::Hazard), EI::Off);

	// -- 4. Seitenzuordnung -------------------------------------------------
	TestTrue(TEXT("Links blinkt links"), UWiesbadenCarLightsComponent::IndicatorAffectsLeft(EI::Left));
	TestFalse(TEXT("Links blinkt nicht rechts"), UWiesbadenCarLightsComponent::IndicatorAffectsRight(EI::Left));
	TestTrue(TEXT("Warnblinker blinkt links"), UWiesbadenCarLightsComponent::IndicatorAffectsLeft(EI::Hazard));
	TestTrue(TEXT("Warnblinker blinkt rechts"), UWiesbadenCarLightsComponent::IndicatorAffectsRight(EI::Hazard));
	TestFalse(TEXT("Aus blinkt nirgends"), UWiesbadenCarLightsComponent::IndicatorAffectsLeft(EI::Off));

	return true;
}

/**
 * Motorklang-Synthese.
 *
 * Der Klang wird zur Laufzeit erzeugt; ohne Test faellt eine Regression erst
 * beim Hinhoeren auf - und "klingt irgendwie anders" ist kein Befund, mit dem
 * sich arbeiten laesst. Geprueft werden daher nachrechenbare Eigenschaften:
 * Zuendfrequenz, Pegelgrenzen, Stille bei abgestelltem Motor, Determinismus
 * und Phasenstetigkeit ueber Blockgrenzen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarEngineAudioTest,
	"WiesbadenReal.Vehicles.EngineAudio",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCarEngineAudioTest::RunTest(const FString& Parameters)
{
	// -- 1. Zuendfrequenz ---------------------------------------------------
	// Viertakter: je Zylinder eine Zuendung pro zwei Umdrehungen.
	// Vierzylinder bei 3000 U/min -> 3000/60 * 2 = 100 Hz.
	TestEqual(TEXT("Vierzylinder bei 3000 U/min zuendet mit 100 Hz"),
		FWiesbadenEngineAudioModel::GetFiringFrequencyHz(3000.0f, 4), 100.0f);
	TestEqual(TEXT("Vierzylinder im Leerlauf (900) zuendet mit 30 Hz"),
		FWiesbadenEngineAudioModel::GetFiringFrequencyHz(900.0f, 4), 30.0f);
	TestEqual(TEXT("Sechszylinder bei 3000 U/min zuendet mit 150 Hz"),
		FWiesbadenEngineAudioModel::GetFiringFrequencyHz(3000.0f, 6), 150.0f);
	TestEqual(TEXT("Stillstand zuendet nicht"),
		FWiesbadenEngineAudioModel::GetFiringFrequencyHz(0.0f, 4), 0.0f);

	// -- 2. Pegel -----------------------------------------------------------
	FWiesbadenEngineAudioParams Params;
	Params.EngineRpm = 3000.0f;
	Params.Throttle = 1.0f;
	Params.bEngineRunning = true;

	const float GainFull = FWiesbadenEngineAudioModel::GetOutputGain(Params);
	TestTrue(TEXT("Unter Last ist Pegel groesser 0"), GainFull > 0.0f);
	TestTrue(TEXT("Pegel bleibt im Bereich 0..1"), GainFull <= 1.0f);

	Params.Throttle = 0.0f;
	const float GainCoast = FWiesbadenEngineAudioModel::GetOutputGain(Params);
	TestTrue(TEXT("Schiebebetrieb ist leiser als Volllast"), GainCoast < GainFull);

	Params.bEngineRunning = false;
	TestEqual(TEXT("Abgestellter Motor hat Pegel 0"),
		FWiesbadenEngineAudioModel::GetOutputGain(Params), 0.0f);

	// -- 3. Samples ---------------------------------------------------------
	constexpr int32 SampleRate = 44100;
	constexpr int32 NumSamples = 1024;

	TArray<int16> BufferA;
	TArray<int16> BufferB;
	BufferA.SetNumUninitialized(NumSamples);
	BufferB.SetNumUninitialized(NumSamples);

	// Abgestellter Motor: Stille, nicht Rauschen.
	FWiesbadenEngineAudioParams Off;
	Off.bEngineRunning = false;
	FWiesbadenEngineAudioState StateOff;

	TestTrue(TEXT("Erzeugung bei abgestelltem Motor meldet Erfolg"),
		FWiesbadenEngineAudioModel::GenerateSamples(Off, SampleRate, NumSamples, StateOff, BufferA.GetData()));

	int32 NonZero = 0;
	for (int16 S : BufferA)
	{
		if (S != 0) { ++NonZero; }
	}
	TestEqual(TEXT("Abgestellter Motor erzeugt exakt Stille"), NonZero, 0);

	// Laufender Motor: Signal vorhanden und ausgesteuert.
	FWiesbadenEngineAudioParams Running;
	Running.EngineRpm = 2500.0f;
	Running.Throttle = 0.6f;
	Running.bEngineRunning = true;

	FWiesbadenEngineAudioState State1;
	FWiesbadenEngineAudioState State2;

	TestTrue(TEXT("Erzeugung im Betrieb meldet Erfolg"),
		FWiesbadenEngineAudioModel::GenerateSamples(Running, SampleRate, NumSamples, State1, BufferA.GetData()));

	int32 Peak = 0;
	int64 Energy = 0;
	for (int16 S : BufferA)
	{
		Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(S)));
		Energy += FMath::Abs(static_cast<int32>(S));
	}
	TestTrue(TEXT("Laufender Motor erzeugt hoerbares Signal"), Energy > 0);
	TestTrue(TEXT("Signal ist nicht uebersteuert"), Peak <= 32767);
	TestTrue(TEXT("Signal nutzt den Pegelbereich sinnvoll"), Peak > 1000);

	// Determinismus: gleiche Eingaben, gleicher Startzustand -> gleiche Samples.
	FWiesbadenEngineAudioModel::GenerateSamples(Running, SampleRate, NumSamples, State2, BufferB.GetData());
	TestTrue(TEXT("Synthese ist deterministisch"), BufferA == BufferB);

	// -- 4. Phasenstetigkeit ------------------------------------------------
	// Der Zustand muss ueber Blockgrenzen fortgeschrieben werden. Startete
	// jeder Block bei Phase 0, gaebe es an jeder Blockgrenze einen Sprung -
	// hoerbar als Knacken im Takt der Puffergroesse.
	FWiesbadenEngineAudioState Continuous;
	FWiesbadenEngineAudioModel::GenerateSamples(Running, SampleRate, NumSamples, Continuous, BufferA.GetData());
	const double PhaseAfterFirst = Continuous.FiringPhase;

	FWiesbadenEngineAudioModel::GenerateSamples(Running, SampleRate, NumSamples, Continuous, BufferB.GetData());

	TestTrue(TEXT("Phase wird ueber Bloecke fortgeschrieben"),
		!FMath::IsNearlyEqual(PhaseAfterFirst, Continuous.FiringPhase, 1e-9));
	TestTrue(TEXT("Phase bleibt im Bereich 0..1"),
		Continuous.FiringPhase >= 0.0 && Continuous.FiringPhase < 1.0);
	TestFalse(TEXT("Zweiter Block wiederholt den ersten nicht"), BufferA == BufferB);

	// -- 5. Fehlerfaelle ----------------------------------------------------
	FWiesbadenEngineAudioState Bad;
	TestFalse(TEXT("Nullzeiger wird abgelehnt"),
		FWiesbadenEngineAudioModel::GenerateSamples(Running, SampleRate, NumSamples, Bad, nullptr));
	TestFalse(TEXT("Abtastrate 0 wird abgelehnt"),
		FWiesbadenEngineAudioModel::GenerateSamples(Running, 0, NumSamples, Bad, BufferA.GetData()));

	// Bei ungueltiger Abtastrate muss der Puffer geleert sein - nicht mit
	// den Resten des vorherigen Aufrufs stehenbleiben.
	NonZero = 0;
	for (int16 S : BufferA)
	{
		if (S != 0) { ++NonZero; }
	}
	TestEqual(TEXT("Ungueltige Abtastrate hinterlaesst Stille"), NonZero, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarAutomaticHeadlightsTest,
	"WiesbadenReal.Vehicles.CarLights.AutomaticHeadlights",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Lichtautomatik muss in der Daemmerung schalten, nicht erst in der Nacht.
 *
 * Hintergrund: Die Tageszeit schreitet mit einer Stunde je 2,5 Realminuten
 * fort, und die Stadt hat keine Strassenbeleuchtung. Wer nach einer halben
 * Stunde Spielzeit weiterfaehrt, sitzt ohne Licht in voelliger Schwaerze - die
 * Aufnahme eines Testlaufs zeigte 66 km/h im dritten Gang bei
 * Scheinwerferanzeige AUS.
 *
 * Geprueft wird beides: dass die Schwelle in der Daemmerung greift UND dass sie
 * bei Tageslicht NICHT greift - eine Automatik, die immer einschaltet, waere
 * genauso falsch.
 */
bool FCarAutomaticHeadlightsTest::RunTest(const FString& Parameters)
{
	// Mittagssonne: kein Licht.
	TestFalse(TEXT("Hoher Sonnenstand braucht kein Licht"),
		UWiesbadenCarLightsComponent::ShouldUseHeadlights(1.0f));
	TestFalse(TEXT("Nachmittag braucht kein Licht"),
		UWiesbadenCarLightsComponent::ShouldUseHeadlights(0.5f));

	// Daemmerung: Licht MUSS an, bevor der Sonnenstand null erreicht.
	TestTrue(TEXT("Daemmerung braucht Licht"),
		UWiesbadenCarLightsComponent::ShouldUseHeadlights(0.10f));
	TestTrue(TEXT("Nacht braucht Licht"),
		UWiesbadenCarLightsComponent::ShouldUseHeadlights(0.0f));

	// Die Schwelle liegt ueber null - sonst ginge das Licht erst an, wenn es
	// bereits finster ist.
	TestTrue(TEXT("Schwelle liegt in der Daemmerung, nicht bei Dunkelheit"),
		UWiesbadenCarLightsComponent::AutoHeadlightSunThreshold > 0.05f);

	// Automatik schaltet zwischen AUS und ABBLENDLICHT.
	UWiesbadenCarLightsComponent* Lights = NewObject<UWiesbadenCarLightsComponent>();
	TestTrue(TEXT("Startzustand AUS"),
		Lights->GetHeadlightMode() == EWiesbadenHeadlightMode::Off);

	Lights->SetAutomaticHeadlights(true);
	TestTrue(TEXT("Automatik schaltet Abblendlicht ein"),
		Lights->GetHeadlightMode() == EWiesbadenHeadlightMode::LowBeam);

	Lights->SetAutomaticHeadlights(false);
	TestTrue(TEXT("Automatik schaltet bei Tag wieder aus"),
		Lights->GetHeadlightMode() == EWiesbadenHeadlightMode::Off);

	// Nach einem manuellen Eingriff schweigt die Automatik. Sonst wuerde sie
	// eine bewusste Entscheidung des Fahrers ueberschreiben.
	Lights->CycleHeadlights();
	const EWiesbadenHeadlightMode ManualMode = Lights->GetHeadlightMode();

	Lights->SetAutomaticHeadlights(true);
	TestTrue(TEXT("Nach manuellem Eingriff greift die Automatik nicht mehr ein"),
		Lights->GetHeadlightMode() == ManualMode);

	return true;
}
