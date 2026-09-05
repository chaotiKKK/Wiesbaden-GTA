// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenFallThroughMonitor.h"

namespace
{
	/** Attrappe: Boden ueberall, ausser in gescripteten X-Intervallen (Luecken). */
	struct FScriptedProbe : public IWbGroundProbe
	{
		TArray<TPair<double, double>> Gaps;   // [X von, X bis) ohne Boden
		double DropCm = 50.0;                  // Boden-Abstand, wo Boden liegt

		virtual bool ProbeGround(const FVector& Pos, double /*Up*/, double /*Down*/, double& OutDrop) const override
		{
			for (const TPair<double, double>& G : Gaps)
			{
				if (Pos.X >= G.Key && Pos.X < G.Value)
				{
					return false;   // Luecke: kein Boden
				}
			}
			OutDrop = DropCm;
			return true;
		}
	};
}

// Der datenreine Durchfall-Waechter, ohne Welt/Trace geprueft.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbFallThroughMonitorTest,
	"WiesbadenReal.World.FallThroughMonitor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbFallThroughMonitorTest::RunTest(const FString& Parameters)
{
	using EMode = FWbFallReport::EMode;
	using EVerdict = FWbFallReport::EVerdict;

	// -- Trace: eine 40-m-Luecke -> DURCHGEFALLEN --------------------------
	{
		FWbFallThroughMonitor M;
		M.Begin(EMode::Trace, FVector(0, 0, 0), 140.0);
		FScriptedProbe Probe;
		Probe.Gaps.Add(TPair<double, double>(100000.0, 104000.0));   // 100 m .. 104 m
		for (double X = 0.0; X <= 200000.0; X += 1000.0)
		{
			M.Observe(FVector(X, 0, 0), FVector::ZeroVector, 0.07, Probe);
		}
		const FWbFallReport R = M.Summary();
		TestEqual(TEXT("Trace-Modus"), static_cast<int32>(R.Mode), static_cast<int32>(EMode::Trace));
		TestEqual(TEXT("Verdikt DURCHGEFALLEN"), static_cast<int32>(R.Verdict), static_cast<int32>(EVerdict::FellThrough));
		TestTrue(TEXT("Laengste Luecke ~40 m"), FMath::IsNearlyEqual(R.WorstGapM, 40.0, 2.0));
		TestTrue(TEXT("Void-Ticks > 0"), R.VoidTicks > 0);
	}

	// -- Trace: durchgehend Boden -> BESTANDEN ------------------------------
	{
		FWbFallThroughMonitor P;
		P.Begin(EMode::Trace, FVector(0, 0, 0), 140.0);
		FScriptedProbe AllGround;   // keine Luecken
		for (double X = 0.0; X <= 100000.0; X += 1000.0)
		{
			P.Observe(FVector(X, 0, 0), FVector::ZeroVector, 0.07, AllGround);
		}
		const FWbFallReport R = P.Summary();
		TestEqual(TEXT("BESTANDEN"), static_cast<int32>(R.Verdict), static_cast<int32>(EVerdict::Passed));
		TestEqual(TEXT("Keine Void-Ticks"), R.VoidTicks, 0);
	}

	// -- Wagen: reale Strecke + Karosserie-Sturz -> DURCHGEFALLEN ----------
	{
		FWbFallThroughMonitor C;
		C.Begin(EMode::Car, FVector(0, 0, 10000), 140.0);
		FScriptedProbe CarProbe;
		CarProbe.Gaps.Add(TPair<double, double>(5000.0, 200000.0));   // ab 50 m kein Boden
		double Z = 10000.0;
		for (double X = 0.0; X <= 100000.0; X += 1000.0)
		{
			if (X >= 5000.0) { Z -= 10.0; }   // Karosserie sinkt 10 cm je Tick
			C.Observe(FVector(X, 0, Z), FVector(0, 0, -500), 0.07, CarProbe);
		}
		const FWbFallReport R = C.Summary();
		TestEqual(TEXT("Wagen-Modus"), static_cast<int32>(R.Mode), static_cast<int32>(EMode::Car));
		TestTrue(TEXT("Strecke > 50 m (gueltig)"), R.DistanceM > 50.0);
		TestTrue(TEXT("Karosserie-Sturz > 1 m"), R.MaxSturzM > 1.0);
		TestEqual(TEXT("Verdikt DURCHGEFALLEN"), static_cast<int32>(R.Verdict), static_cast<int32>(EVerdict::FellThrough));
	}

	// -- Wagen: kam nicht vom Fleck -> UNGUELTIG ---------------------------
	{
		FWbFallThroughMonitor S;
		S.Begin(EMode::Car, FVector(0, 0, 10000), 140.0);
		FScriptedProbe Grounded;
		for (int32 I = 0; I < 50; ++I)
		{
			S.Observe(FVector(0, 0, 10000), FVector::ZeroVector, 0.07, Grounded);
		}
		const FWbFallReport R = S.Summary();
		TestEqual(TEXT("UNGUELTIG (0 m gefahren)"), static_cast<int32>(R.Verdict), static_cast<int32>(EVerdict::Invalid));
	}

	return true;
}
