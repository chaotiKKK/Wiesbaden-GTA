// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenFrameProfiler.h"

// Der gefensterte Bildzeit-Profiler, ohne Welt/Engine geprueft.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbFrameProfilerTest,
	"WiesbadenReal.World.FrameProfiler",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbFrameProfilerTest::RunTest(const FString& Parameters)
{
	// 30 ruhige Bilder (20 ms) + ein Ausreisser (120 ms).
	FWbFrameProfiler P;
	for (int32 I = 0; I < 30; ++I) { P.SampleFrame(20.0); }
	P.SampleFrame(120.0);

	const FWbFrameReport R = P.Report();
	TestEqual(TEXT("31 Bilder"), R.FrameCount, 31);
	TestTrue(TEXT("Schlechtestes 120 ms"), FMath::IsNearlyEqual(R.WorstMs, 120.0, 0.01));
	TestEqual(TEXT("Ein Ausreisser (> 2x Mittel, ab 11 Bildern)"), R.SpikeCount, 1);
	TestEqual(TEXT("Ein Aussetzer (> 50 ms)"), R.HitchCount, 1);
	TestTrue(TEXT("Mittel ~23.2 ms"), FMath::IsNearlyEqual(R.MeanMs, 720.0 / 31.0, 0.05));
	TestTrue(TEXT("~43 Bilder/s"), R.Fps > 40.0 && R.Fps < 46.0);

	// Erstes Riesen-Ladebild (>= 500 ms) wird nicht mitgezaehlt.
	FWbFrameProfiler Load;
	Load.SampleFrame(900.0);
	TestEqual(TEXT("Ladebild uebersprungen: 0 Bilder"), Load.Report().FrameCount, 0);

	// Strang-/Subsystem-Mittel.
	FWbFrameProfiler St;
	St.SampleFrame(20.0); St.SampleFrame(20.0);
	FWbStrandTimes S; S.LightMs = 3.0; S.GameThreadMs = 10.0;
	St.AddStrands(S); St.AddStrands(S);
	St.AddSubsystemTime(4.0); St.AddSubsystemTime(4.0);
	const FWbFrameReport RS = St.Report();
	TestTrue(TEXT("Mittel Ampeln 3.0"), FMath::IsNearlyEqual(RS.MeanLightMs, 3.0, 0.01));
	TestTrue(TEXT("Mittel Spiel 10.0"), FMath::IsNearlyEqual(RS.MeanGameThreadMs, 10.0, 0.01));
	TestTrue(TEXT("Mittel Subsystem 4.0"), FMath::IsNearlyEqual(RS.MeanSubsystemMs, 4.0, 0.01));

	// BeginWindow setzt zurueck; Report bleibt null-sicher.
	St.BeginWindow();
	const FWbFrameReport RZ = St.Report();
	TestEqual(TEXT("Nach BeginWindow: 0 Bilder"), RZ.FrameCount, 0);
	TestTrue(TEXT("Nach BeginWindow: Mittel 0 (kein DivByZero)"), RZ.MeanMs == 0.0);

	// AdvanceWindow: 4 s Vorlauf, dann 15 s Fenster.
	FWbFrameProfiler Q;
	TestFalse(TEXT("Vor Vorlauf: nicht messend"), Q.IsMeasuring());
	TestFalse(TEXT("2 s: keine Meldung"), Q.AdvanceWindow(2.0f));
	TestFalse(TEXT("noch nicht messend"), Q.IsMeasuring());
	TestFalse(TEXT("5 s gesamt: Start, keine Meldung"), Q.AdvanceWindow(3.0f));
	TestTrue(TEXT("jetzt messend"), Q.IsMeasuring());
	Q.SampleFrame(20.0);
	TestFalse(TEXT("10 s Fenster: noch keine Meldung"), Q.AdvanceWindow(10.0f));
	TestTrue(TEXT("16 s Fenster: Meldung faellig"), Q.AdvanceWindow(6.0f));

	return true;
}
