// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "NPC/WiesbadenPursuer.h"

// Reaktive Verfolger-Logik: entdeckt den Spieler im Detect-Radius, faehrt planar
// auf ihn zu (gedeckelt, ohne Ueberschwingen), haelt die Spur bis zum Lose-Radius
// (Hysterese), meldet Caught im Fang-Radius. Werte hand gerechnet.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPursuerStepTest,
	"WiesbadenReal.NPC.PursuerStep",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPursuerStepTest::RunTest(const FString& Parameters)
{
	const FWiesbadenPursuerParams P; // Detect 5000, Lose 9000, Catch 400, MaxSpeed 1400

	auto St = [](double X, double Y, EWiesbadenPursuerMode M) -> FWiesbadenPursuerState
	{
		FWiesbadenPursuerState S; S.Position = FVector(X, Y, 0.0); S.Mode = M; return S;
	};
	auto Pl = [](double X, double Y, double Z) { return FVector(X, Y, Z); };

	// (1) Entdeckung: Idle, Spieler im Detect-Radius (3000 < 5000) -> Chasing und
	//     faehrt MaxSpeed*dt = 1400 cm auf ihn zu: (0,0) -> (1400,0).
	{
		const FWiesbadenPursuerState R =
			FWiesbadenPursuer::Step(St(0, 0, EWiesbadenPursuerMode::Idle), Pl(3000, 0, 0), P, 1.0);
		TestTrue(TEXT("entdeckt -> Chasing"), R.Mode == EWiesbadenPursuerMode::Chasing);
		TestTrue(TEXT("zieht 1400 cm naeher"), R.Position.Equals(FVector(1400, 0, 0), 1.0));
	}

	// (2) Keine Entdeckung: Idle, Spieler fern (6000 > 5000, aber < Lose) -> bleibt
	//     Idle und bewegt sich nicht.
	{
		const FWiesbadenPursuerState R =
			FWiesbadenPursuer::Step(St(0, 0, EWiesbadenPursuerMode::Idle), Pl(6000, 0, 0), P, 1.0);
		TestTrue(TEXT("fern & idle -> Idle"), R.Mode == EWiesbadenPursuerMode::Idle);
		TestTrue(TEXT("idle steht"), R.Position.Equals(FVector::ZeroVector, 0.01));
	}

	// (3) Hysterese: Chasing, Spieler im Band (6000, Detect < 6000 < Lose) -> bleibt
	//     Chasing und verfolgt weiter.
	{
		const FWiesbadenPursuerState R =
			FWiesbadenPursuer::Step(St(0, 0, EWiesbadenPursuerMode::Chasing), Pl(6000, 0, 0), P, 1.0);
		TestTrue(TEXT("Band -> bleibt Chasing"), R.Mode == EWiesbadenPursuerMode::Chasing);
		TestTrue(TEXT("verfolgt weiter (1400)"), R.Position.Equals(FVector(1400, 0, 0), 1.0));
	}

	// (4) Spur verloren: Chasing, Spieler jenseits Lose (10000 > 9000) -> Idle, steht.
	{
		const FWiesbadenPursuerState R =
			FWiesbadenPursuer::Step(St(0, 0, EWiesbadenPursuerMode::Chasing), Pl(10000, 0, 0), P, 1.0);
		TestTrue(TEXT("jenseits Lose -> Idle"), R.Mode == EWiesbadenPursuerMode::Idle);
		TestTrue(TEXT("idle steht"), R.Position.Equals(FVector::ZeroVector, 0.01));
	}

	// (5) Kein Ueberschwingen: Chasing, Spieler nah (500 cm, > Catch) -> faehrt genau
	//     hin (min(dist, MaxSpeed*dt)), nicht drueber.
	{
		const FWiesbadenPursuerState R =
			FWiesbadenPursuer::Step(St(0, 0, EWiesbadenPursuerMode::Chasing), Pl(500, 0, 0), P, 1.0);
		TestTrue(TEXT("kein Ueberschwingen -> genau am Spieler"), R.Position.Equals(FVector(500, 0, 0), 1.0));
	}

	// (6) Eingeholt: Spieler im Fang-Radius (300 < 400) -> Caught.
	{
		const FWiesbadenPursuerState R =
			FWiesbadenPursuer::Step(St(0, 0, EWiesbadenPursuerMode::Chasing), Pl(300, 0, 0), P, 1.0);
		TestTrue(TEXT("im Fang-Radius -> Caught"), R.Mode == EWiesbadenPursuerMode::Caught);
	}

	// (7) Planar: Spieler XY nah (3000) aber Z sehr weit -> Hoehe ignoriert -> entdeckt.
	{
		const FWiesbadenPursuerState R =
			FWiesbadenPursuer::Step(St(0, 0, EWiesbadenPursuerMode::Idle), Pl(3000, 0, 999999), P, 1.0);
		TestTrue(TEXT("Hoehe ignoriert -> Chasing"), R.Mode == EWiesbadenPursuerMode::Chasing);
	}

	return true;
}
