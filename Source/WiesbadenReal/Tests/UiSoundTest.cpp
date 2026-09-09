// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "UI/WiesbadenUiSound.h"

namespace
{
	constexpr int32 UiSampleRate = 44100;

	float MaxAbs(const int16* Samples, int32 Count)
	{
		int32 Peak = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Peak = FMath::Max(Peak, FMath::Abs<int32>(Samples[Index]));
		}
		return static_cast<float>(Peak);
	}
}

// UI-Signaltoene: reines PCM-Modell, ohne Welt/Audiogeraet geprueft.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiSoundTest,
	"WiesbadenReal.UI.UiSound",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FUiSoundTest::RunTest(const FString& Parameters)
{
	const int32 N = UiSampleRate / 4; // 0,25 s
	TArray<int16> Chime; Chime.SetNumUninitialized(N);
	TArray<int16> Buzz;  Buzz.SetNumUninitialized(N);

	// -- Erzeugung gelingt bei gueltigen Argumenten --
	TestTrue(TEXT("Chime: erzeugt"),
		FWiesbadenUiSoundModel::GenerateSamples(EWiesbadenUiSound::KaufChime, UiSampleRate, N, 1u, Chime.GetData()));
	TestTrue(TEXT("Buzz: erzeugt"),
		FWiesbadenUiSoundModel::GenerateSamples(EWiesbadenUiSound::AblehnungBuzz, UiSampleRate, N, 1u, Buzz.GetData()));

	// -- Nicht stumm: ein hoerbarer Ton hat deutlichen Ausschlag --
	TestTrue(TEXT("Chime: nicht stumm"), MaxAbs(Chime.GetData(), N) > 2000.0f);
	TestTrue(TEXT("Buzz: nicht stumm"), MaxAbs(Buzz.GetData(), N) > 2000.0f);

	// -- Huellkurve klingt aus: das letzte 1 % liegt weit unter dem Gesamt-Peak
	//    (kein Klick am Ende). --
	{
		const int32 Tail = FMath::Max(1, N / 100);
		const float Overall = MaxAbs(Chime.GetData(), N);
		const float End = MaxAbs(Chime.GetData() + (N - Tail), Tail);
		TestTrue(TEXT("Chime: Ende klingt auf ~0 aus"), End < 0.15f * Overall);
	}

	// -- Deterministisch: gleicher Seed -> identische Samples --
	{
		TArray<int16> Chime2; Chime2.SetNumUninitialized(N);
		FWiesbadenUiSoundModel::GenerateSamples(EWiesbadenUiSound::KaufChime, UiSampleRate, N, 1u, Chime2.GetData());
		TestEqual(TEXT("Chime: deterministisch je Seed"),
			FMemory::Memcmp(Chime.GetData(), Chime2.GetData(), N * sizeof(int16)), 0);
	}

	// -- Die zwei Toene sind unterscheidbar --
	TestTrue(TEXT("Chime != Buzz"),
		FMemory::Memcmp(Chime.GetData(), Buzz.GetData(), N * sizeof(int16)) != 0);

	// -- Ungueltige Argumente -> false, kein Schreiben --
	int16 Dummy = 0;
	TestFalse(TEXT("null-Puffer -> false"),
		FWiesbadenUiSoundModel::GenerateSamples(EWiesbadenUiSound::KaufChime, UiSampleRate, N, 1u, nullptr));
	TestFalse(TEXT("NumSamples 0 -> false"),
		FWiesbadenUiSoundModel::GenerateSamples(EWiesbadenUiSound::KaufChime, UiSampleRate, 0, 1u, &Dummy));
	TestFalse(TEXT("SampleRate 0 -> false"),
		FWiesbadenUiSoundModel::GenerateSamples(EWiesbadenUiSound::KaufChime, 0, N, 1u, &Dummy));

	return true;
}
