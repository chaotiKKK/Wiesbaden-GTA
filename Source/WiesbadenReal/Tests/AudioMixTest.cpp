// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Audio/WiesbadenAudioMix.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenAudioMixTest,
	"WiesbadenReal.Audio.Mix",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWiesbadenAudioMixTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenAudioMix;

	// dB <-> linear.
	TestTrue(TEXT("0 dB -> 1,0"), FMath::IsNearlyEqual(DbToLinear(0.0f), 1.0f, 0.001f));
	TestTrue(TEXT("-6 dB -> ~0,501"), FMath::IsNearlyEqual(DbToLinear(-6.0f), 0.501f, 0.01f));
	TestTrue(TEXT("-20 dB -> 0,1"), FMath::IsNearlyEqual(DbToLinear(-20.0f), 0.1f, 0.001f));
	TestTrue(TEXT("linear 1,0 -> 0 dB"), FMath::IsNearlyEqual(LinearToDb(1.0f), 0.0f, 0.01f));
	TestTrue(TEXT("linear 0,5 -> ~-6,02 dB"), FMath::IsNearlyEqual(LinearToDb(0.5f), -6.02f, 0.05f));
	TestTrue(TEXT("Round-Trip -15 dB"), FMath::IsNearlyEqual(LinearToDb(DbToLinear(-15.0f)), -15.0f, 0.01f));

	// Fader-Kennlinie (0..1 -> dB, linear in dB von -60 bis 0).
	TestTrue(TEXT("Regler 1 -> 0 dB"), FMath::IsNearlyEqual(SliderToDb(1.0f), 0.0f, 0.001f));
	TestTrue(TEXT("Regler 0 -> -60 dB (MinDb)"), FMath::IsNearlyEqual(SliderToDb(0.0f), -60.0f, 0.001f));
	TestTrue(TEXT("Regler 0,5 -> -30 dB"), FMath::IsNearlyEqual(SliderToDb(0.5f), -30.0f, 0.001f));

	// Regler -> linearer Faktor.
	TestTrue(TEXT("Regler 1 -> Faktor 1,0"), FMath::IsNearlyEqual(SliderToLinear(1.0f), 1.0f, 0.001f));
	TestTrue(TEXT("Regler 0 -> Faktor 0 (Stille)"), SliderToLinear(0.0f) == 0.0f);

	// Perzeptiv, NICHT linear-als-dB und NICHT roh-linear: 0,5 ist deutlich leiser
	// als der halbe Faktor (Fallstrick "Regler == dB" bzw. "Regler == Amplitude").
	const float Half = SliderToLinear(0.5f);
	TestTrue(TEXT("Regler 0,5 perzeptiv leise (< 0,1)"), Half > 0.0f && Half < 0.1f);

	// Monoton steigend.
	bool bMonotonic = true;
	float Prev = -1.0f;
	for (int32 i = 0; i <= 10; ++i)
	{
		const float V = SliderToLinear(i / 10.0f);
		bMonotonic = bMonotonic && (V >= Prev);
		Prev = V;
	}
	TestTrue(TEXT("Faktor steigt monoton mit dem Regler"), bMonotonic);

	// Clamping ausserhalb 0..1.
	TestTrue(TEXT("Regler >1 geklemmt auf 0 dB"), FMath::IsNearlyEqual(SliderToDb(1.5f), 0.0f, 0.001f));
	TestTrue(TEXT("Regler <0 -> Stille"), SliderToLinear(-0.3f) == 0.0f);

	return true;
}
