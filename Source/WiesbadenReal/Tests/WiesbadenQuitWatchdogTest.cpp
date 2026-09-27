// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Core/WiesbadenQuitWatchdog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenQuitWatchdogTest,
	"WiesbadenReal.Core.QuitWatchdog",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWiesbadenQuitWatchdogTest::RunTest(const FString& Parameters)
{
	using EEntscheidung = FWiesbadenQuitWatchdog::EEntscheidung;

	const double Warn = 45.0;
	const double Stall = 420.0;
	const double Exit = 120.0;

	// Karten-Ladephase: kein Tick seit Laufbeginn - nie eingreifen, sonst
	// stirbt jeder Lauf an der Streaming-Ladezeit.
	TestTrue(TEXT("Vor dem ersten Tick wird nie eingegriffen"),
		FWiesbadenQuitWatchdog::Evaluate(500.0, 0.0, 0.0, Warn, Stall, Exit) == EEntscheidung::Keine);

	// Gesunder Lauf: Tick gerade eben.
	TestTrue(TEXT("Frischer Tick ist unauffaellig"),
		FWiesbadenQuitWatchdog::Evaluate(500.0, 499.5, 0.0, Warn, Stall, Exit) == EEntscheidung::Keine);

	// GPU-Stall: erst warnen (Probe3 erholte sich nach 347 s - die Warnung
	// erklaert die Pause im Log, bevor die Frist reisst)...
	TestTrue(TEXT("Tick-Pause ab Warnfrist gibt eine Warnung"),
		FWiesbadenQuitWatchdog::Evaluate(545.0, 500.0, 0.0, Warn, Stall, Exit) == EEntscheidung::WarnungStall);

	// ...und hart beenden, wenn keine Erholung kommt.
	TestTrue(TEXT("Tick-Pause ab Stallfrist erzwingt das Ende"),
		FWiesbadenQuitWatchdog::Evaluate(920.0, 500.0, 0.0, Warn, Stall, Exit) == EEntscheidung::KillStall);

	// Nach einer Exit-Anfrage faellt der Tick aus (Welt abgebaut) - das darf
	// KEIN Stall-Fehlalarm sein, es zaehlt nur noch die Exit-Frist.
	TestTrue(TEXT("Tick-Stopp im Shutdown ist kein Stall"),
		FWiesbadenQuitWatchdog::Evaluate(920.0, 500.0, 850.0, Warn, Stall, Exit) == EEntscheidung::Keine);

	TestTrue(TEXT("Exit ohne Ende innerhalb der Frist erzwingt das Ende"),
		FWiesbadenQuitWatchdog::Evaluate(971.0, 500.0, 850.0, Warn, Stall, Exit) == EEntscheidung::KillExit);

	// Rangfolge im Grenzfall: beendet sich der Exit NIE, zaehlt die Exit-Frist
	// auch dann, wenn die Stall-Frist laenger waere.
	TestTrue(TEXT("Exit-Frist hat Vorrang vor der Stall-Frist"),
		FWiesbadenQuitWatchdog::Evaluate(100000.0, 500.0, 850.0, Warn, Stall, Exit) == EEntscheidung::KillExit);

	return true;
}
