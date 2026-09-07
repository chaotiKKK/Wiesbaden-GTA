// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenStreamingSource.h"

// Geschwindigkeits-Vorausladung: die reine Kurve (m) aus Tempo x Sekunden,
// gedeckelt. Kein Weltzugriff -> headless testbar.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreamingLookAheadTest,
	"WiesbadenReal.World.StreamingSource.LookAheadMeters",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FStreamingLookAheadTest::RunTest(const FString& Parameters)
{
	// Stand: keine Vorausladung (Radius bleibt beim Boden-Wert, FPS hoch).
	TestEqual(TEXT("Tempo 0 -> 0 m"),
		AWiesbadenStreamingSource::ComputeLookAheadMeters(0.0f, 6.0f, 1500.0f), 0.0f);

	// Abgeschaltet (0 s) -> 0, egal wie schnell.
	TestEqual(TEXT("0 s -> 0 m"),
		AWiesbadenStreamingSource::ComputeLookAheadMeters(69.4f, 0.0f, 1500.0f), 0.0f);

	// 250 km/h = 69.4 m/s, 6 s -> ~416 m (unter der Deckelung).
	TestTrue(TEXT("69.4 m/s x 6 s ~ 416 m"),
		FMath::IsNearlyEqual(
			AWiesbadenStreamingSource::ComputeLookAheadMeters(69.4f, 6.0f, 1500.0f),
			416.4f, 0.5f));

	// Deckelung greift bei Extremtempo.
	TestEqual(TEXT("Deckelung bei MaxMeters"),
		AWiesbadenStreamingSource::ComputeLookAheadMeters(69.4f, 6.0f, 300.0f), 300.0f);

	// Negatives Tempo (unsinnig) -> 0, kein Rueckwaerts-Versatz.
	TestEqual(TEXT("negatives Tempo -> 0 m"),
		AWiesbadenStreamingSource::ComputeLookAheadMeters(-10.0f, 6.0f, 1500.0f), 0.0f);

	return true;
}

// Bonus: der hoehenadaptive Boden/Luft-Radius (bisher ohne Testabdeckung).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreamingAdaptiveRadiusTest,
	"WiesbadenReal.World.StreamingSource.AdaptiveRadius",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FStreamingAdaptiveRadiusTest::RunTest(const FString& Parameters)
{
	const float Ground = 900.0f;
	const float Air = 6000.0f;
	const float Start = 60.0f;
	const float Full = 350.0f;

	// Unter dem Start-Band: voller Boden-Radius (eng, schnell).
	TestEqual(TEXT("Boden -> GroundRadius"),
		AWiesbadenStreamingSource::ComputeAdaptiveRadiusMeters(0.0f, Ground, Air, Start, Full), Ground);

	// Ueber dem Full-Band: voller Luft-Radius (weite Sicht).
	TestEqual(TEXT("Hoch -> AirRadius"),
		AWiesbadenStreamingSource::ComputeAdaptiveRadiusMeters(1000.0f, Ground, Air, Start, Full), Air);

	// Dazwischen: streng monoton zwischen Boden und Luft.
	const float Mid = AWiesbadenStreamingSource::ComputeAdaptiveRadiusMeters(
		205.0f, Ground, Air, Start, Full);
	TestTrue(TEXT("Mitte zwischen Boden und Luft"), Mid > Ground && Mid < Air);

	// Degeneriertes Band (Full <= Start): harte Stufe statt Division durch <= 0.
	TestEqual(TEXT("degeneriert unter Full -> Ground"),
		AWiesbadenStreamingSource::ComputeAdaptiveRadiusMeters(10.0f, Ground, Air, 300.0f, 300.0f), Ground);
	TestEqual(TEXT("degeneriert ueber Full -> Air"),
		AWiesbadenStreamingSource::ComputeAdaptiveRadiusMeters(500.0f, Ground, Air, 300.0f, 300.0f), Air);

	return true;
}
