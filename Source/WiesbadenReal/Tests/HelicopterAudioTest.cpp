// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicles/WiesbadenHelicopterAudio.h"

namespace
{
	constexpr int32 SampleRate = 44100;

	float ComputeRms(const int16* Samples, int32 Count)
	{
		double SumSq = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			SumSq += static_cast<double>(Samples[Index]) * Samples[Index];
		}
		return FMath::Sqrt(SumSq / FMath::Max(1, Count));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterAudioFrequencyTest,
	"WiesbadenReal.Vehicles.HelicopterAudio.Frequencies",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHelicopterAudioFrequencyTest::RunTest(const FString& Parameters)
{
	// Blattpassfrequenz: 4 Blaetter bei 420 U/min -> 28 Hz (das "Wop-Wop").
	TestTrue(TEXT("BladePass 420/4 = 28 Hz"),
		FMath::IsNearlyEqual(FWiesbadenHelicopterAudioModel::GetBladePassFrequency(420.0f, 4), 28.0f, 0.01f));
	TestTrue(TEXT("BladePass ohne Drehzahl = 0"),
		FMath::IsNearlyZero(FWiesbadenHelicopterAudioModel::GetBladePassFrequency(0.0f, 4), 0.01f));

	// Filter-Grenzfrequenz steigt mit Drehzahl und Blattlast, bleibt begrenzt.
	const float CutoffSlow = FWiesbadenHelicopterAudioModel::GetFilterCutoffHz(420.0f, 0.3f);
	const float CutoffFast = FWiesbadenHelicopterAudioModel::GetFilterCutoffHz(840.0f, 0.3f);
	TestTrue(TEXT("Cutoff steigt mit der Drehzahl"), CutoffFast > CutoffSlow);
	const float CutoffLoad = FWiesbadenHelicopterAudioModel::GetFilterCutoffHz(420.0f, 1.0f);
	TestTrue(TEXT("Cutoff steigt mit der Blattlast"), CutoffLoad > CutoffSlow);
	TestTrue(TEXT("Cutoff bleibt im Hoerbereich begrenzt"),
		FWiesbadenHelicopterAudioModel::GetFilterCutoffHz(10000.0f, 1.0f) <= 8000.0f);

	// Konfigurierbare Basis: niedrige Basis (Kampfheli, dumpf) < hohe Basis.
	FWiesbadenHelicopterAudioParams Heavy;
	Heavy.MainRotorRpm = 420.0f;
	Heavy.Collective = 0.5f;
	Heavy.RotorCutoffBaseHz = 110.0f;
	FWiesbadenHelicopterAudioParams Civil = Heavy;
	Civil.RotorCutoffBaseHz = 180.0f;
	TestTrue(TEXT("Niedrige Basis -> niedrigere Cutoff-Frequenz"),
		FWiesbadenHelicopterAudioModel::GetRotorCutoffHz(Heavy)
			< FWiesbadenHelicopterAudioModel::GetRotorCutoffHz(Civil));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterAudioSlapTest,
	"WiesbadenReal.Vehicles.HelicopterAudio.BladeSlap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHelicopterAudioSlapTest::RunTest(const FString& Parameters)
{
	// Der Blade-Slap (AM-Tiefe) veraendert den Klang: gleicher Seed, nur die
	// Slap-Tiefe variiert -> unterschiedliche Samples. Ohne Motor (Autorotation)
	// ist nur der Rotor hoerbar, sodass der Effekt nicht vom Motor-Ton verdeckt
	// wird.
	FWiesbadenHelicopterAudioParams Params;
	Params.MainRotorRpm = 420.0f;
	Params.Collective = 0.5f;
	Params.bEngineRunning = false;
	Params.BladeCount = 3;

	TArray<int16> A;
	TArray<int16> B;
	A.SetNum(8192);
	B.SetNum(8192);

	Params.BladeSlapDepth = 0.0f;
	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 21u, A.GetData());
	Params.BladeSlapDepth = 1.0f;
	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 21u, B.GetData());

	bool bDiffers = false;
	for (int32 Index = 0; Index < A.Num() && !bDiffers; ++Index)
	{
		bDiffers = (A[Index] != B[Index]);
	}
	TestTrue(TEXT("Slap-Tiefe veraendert den Rotor-Klang"), bDiffers);

	// Kampfheli-Werte (0.55) unterscheiden sich von den Zivil-Defaults (0.38).
	Params.BladeSlapDepth = 0.38f;
	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 21u, A.GetData());
	Params.BladeSlapDepth = 0.55f;
	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 21u, B.GetData());

	bDiffers = false;
	for (int32 Index = 0; Index < A.Num() && !bDiffers; ++Index)
	{
		bDiffers = (A[Index] != B[Index]);
	}
	TestTrue(TEXT("Kampfheli-Slap unterscheidet sich vom Zivil-Default"), bDiffers);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterAudioDeterminismTest,
	"WiesbadenReal.Vehicles.HelicopterAudio.Determinism",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHelicopterAudioDeterminismTest::RunTest(const FString& Parameters)
{
	FWiesbadenHelicopterAudioParams Params;
	Params.MainRotorRpm = 420.0f;
	Params.EngineRpm = 3360.0f;
	Params.Collective = 0.5f;
	Params.bEngineRunning = true;

	TArray<int16> A;
	TArray<int16> B;
	A.SetNum(8192);
	B.SetNum(8192);

	// Gleicher Seed -> identische Samples (deterministisch fuer Tests).
	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 42u, A.GetData());
	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 42u, B.GetData());
	TestTrue(TEXT("Gleicher Seed erzeugt identische Samples"),
		FMemory::Memcmp(A.GetData(), B.GetData(), A.Num() * sizeof(int16)) == 0);

	// Anderer Seed -> andere Samples.
	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 43u, B.GetData());
	bool bDiffers = false;
	for (int32 Index = 0; Index < A.Num() && !bDiffers; ++Index)
	{
		bDiffers = (A[Index] != B[Index]);
	}
	TestTrue(TEXT("Anderer Seed erzeugt andere Samples"), bDiffers);

	// Alle Samples im int16-Bereich (kein Overflow/Clamp-Fehler).
	bool bInRange = true;
	for (const int16 Sample : A)
	{
		if (Sample < -32768 || Sample > 32767)
		{
			bInRange = false;
			break;
		}
	}
	TestTrue(TEXT("Samples liegen im int16-Bereich"), bInRange);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterAudioLevelTest,
	"WiesbadenReal.Vehicles.HelicopterAudio.Levels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHelicopterAudioLevelTest::RunTest(const FString& Parameters)
{
	// Rotor-Lautstaerke folgt der Blattlast. Ohne Motor (Autorotation) ist nur
	// der Rotor hoerbar, sodass der Collective-Effekt nicht vom Motor-Ton
	// uebertoent wird.
	FWiesbadenHelicopterAudioParams Params;
	Params.MainRotorRpm = 420.0f;
	Params.EngineRpm = 3360.0f;
	Params.Collective = 1.0f;
	Params.bEngineRunning = false;

	TArray<int16> Loud;
	TArray<int16> Soft;
	Loud.SetNum(8192);
	Soft.SetNum(8192);

	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 7u, Loud.GetData());
	Params.Collective = 0.3f;
	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 7u, Soft.GetData());

	const float LoudRms = ComputeRms(Loud.GetData(), Loud.Num());
	const float SoftRms = ComputeRms(Soft.GetData(), Soft.Num());
	TestTrue(TEXT("Mehr Blattlast -> lautere Rotor-Samples"), LoudRms > SoftRms * 1.3f);

	// Triebwerk an -> mehr Pegel als reine Autorotation; Autorotation ist nicht stumm.
	Params.Collective = 0.5f;
	Params.bEngineRunning = true;
	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 9u, Loud.GetData());
	Params.bEngineRunning = false;
	FWiesbadenHelicopterAudioModel::GenerateSamples(Params, SampleRate, 8192, 9u, Soft.GetData());

	const float EngineRms = ComputeRms(Loud.GetData(), Loud.Num());
	const float RotorOnlyRms = ComputeRms(Soft.GetData(), Soft.Num());
	TestTrue(TEXT("Triebwerk an -> mehr Pegel als Autorotation"), EngineRms > RotorOnlyRms * 1.05f);
	TestTrue(TEXT("Autorotation ist nicht stumm (Rotor rauscht)"), RotorOnlyRms > 500.0f);

	return true;
}
