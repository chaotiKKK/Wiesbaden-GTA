// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Missions/WiesbadenMissionTypes.h"
#include "Missions/WiesbadenMissionLoader.h"
#include "Missions/WiesbadenMissionRunner.h"
#include "Missions/WiesbadenMissionDispatcher.h"
#include "Missions/WiesbadenMissionDeadline.h"

// Ziel-Erfuellung ReachLocation: horizontale (2D) Distanz <= Radius. Hoehe wird
// bewusst ignoriert (Hang/Bahn-Umgebung), daher der Hoehen-Testfall.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionObjectiveReachLocationTest,
	"WiesbadenReal.Missions.ObjectiveReachLocation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMissionObjectiveReachLocationTest::RunTest(const FString& Parameters)
{
	FMissionObjective Obj;
	Obj.Type = EObjectiveType::ReachLocation;
	Obj.Location = FVector(1000.0, 2000.0, 500.0);
	Obj.RadiusCm = 800.0;

	auto Ctx = [](double X, double Y, double Z) -> FMissionContext
	{
		FMissionContext C;
		C.PlayerLocation = FVector(X, Y, Z);
		return C;
	};

	// Genau am Ziel -> erfuellt.
	TestTrue(TEXT("am Ziel"), Obj.IsComplete(Ctx(1000.0, 2000.0, 500.0)));
	// 700 cm daneben (in X) < 800 -> erfuellt.
	TestTrue(TEXT("knapp innerhalb"), Obj.IsComplete(Ctx(1700.0, 2000.0, 500.0)));
	// 900 cm daneben (in X) > 800 -> NICHT erfuellt.
	TestFalse(TEXT("knapp ausserhalb"), Obj.IsComplete(Ctx(1900.0, 2000.0, 500.0)));
	// XY am Ziel, aber 8500 cm hoeher -> horizontal erfuellt (Hoehe ignoriert).
	TestTrue(TEXT("Hoehe ignoriert"), Obj.IsComplete(Ctx(1000.0, 2000.0, 9000.0)));
	// Diagonale: (480,640) -> 800 cm exakt am Rand -> erfuellt (<=).
	TestTrue(TEXT("diagonal am Rand"), Obj.IsComplete(Ctx(1480.0, 2640.0, 500.0)));

	return true;
}

// Ziel-Erfuellung LeaveArea (Fluchtpunkt): horizontale (2D) Distanz >= Radius -
// die Umkehrung von ReachLocation. Man ist fertig, sobald man das Gebiet um den
// Ort verlassen hat.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionObjectiveLeaveAreaTest,
	"WiesbadenReal.Missions.ObjectiveLeaveArea",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMissionObjectiveLeaveAreaTest::RunTest(const FString& Parameters)
{
	FMissionObjective Obj;
	Obj.Type = EObjectiveType::LeaveArea;
	Obj.Location = FVector(1000.0, 2000.0, 500.0);
	Obj.RadiusCm = 800.0;

	auto Ctx = [](double X, double Y, double Z) -> FMissionContext
	{
		FMissionContext C;
		C.PlayerLocation = FVector(X, Y, Z);
		return C;
	};

	// Genau am Ort -> noch IM Gebiet -> NICHT erfuellt.
	TestFalse(TEXT("am Ort: noch drin"), Obj.IsComplete(Ctx(1000.0, 2000.0, 500.0)));
	// 700 cm entfernt (< 800) -> noch drin -> NICHT erfuellt.
	TestFalse(TEXT("knapp drin"), Obj.IsComplete(Ctx(1700.0, 2000.0, 500.0)));
	// 900 cm entfernt (> 800) -> Gebiet verlassen -> erfuellt.
	TestTrue(TEXT("raus"), Obj.IsComplete(Ctx(1900.0, 2000.0, 500.0)));
	// Diagonale exakt am Rand (480,640 -> 800 cm) -> erfuellt (>=).
	TestTrue(TEXT("am Rand raus"), Obj.IsComplete(Ctx(1480.0, 2640.0, 500.0)));
	// XY am Ort, nur 8500 cm hoeher -> horizontal drin (Hoehe ignoriert) -> NICHT erfuellt.
	TestFalse(TEXT("Hoehe ignoriert: drin"), Obj.IsComplete(Ctx(1000.0, 2000.0, 9000.0)));
	// Weit weg -> erfuellt.
	TestTrue(TEXT("weit weg"), Obj.IsComplete(Ctx(6000.0, 2000.0, 500.0)));

	return true;
}

// Verweil-Ziel (Dwell), Naht 1 - Verweildauer-Fortschreibung: im Radius wird
// aufaddiert, ausserhalb auf 0 zurueckgesetzt (kontinuierliches Halten).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionObjectiveDwellTest,
	"WiesbadenReal.Missions.ObjectiveDwell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMissionObjectiveDwellTest::RunTest(const FString& Parameters)
{
	FMissionObjective Obj;
	Obj.Type = EObjectiveType::Dwell;
	Obj.Location = FVector(1000.0, 2000.0, 500.0);
	Obj.RadiusCm = 800.0;
	Obj.HoldSeconds = 3.0;

	auto Loc = [](double X, double Y, double Z) { return FVector(X, Y, Z); };

	// Im Radius -> aufaddieren.
	TestTrue(TEXT("am Ort: +delta (2.0->2.5)"),
		FMath::IsNearlyEqual(Obj.AdvanceDwell(2.0, Loc(1000.0, 2000.0, 0.0), 0.5), 2.5, 0.0001));
	TestTrue(TEXT("knapp drin: +delta (1.0->1.5)"),
		FMath::IsNearlyEqual(Obj.AdvanceDwell(1.0, Loc(1700.0, 2000.0, 0.0), 0.5), 1.5, 0.0001));
	// XY am Ort, nur hoeher -> horizontal drin (Hoehe ignoriert).
	TestTrue(TEXT("Hoehe ignoriert: +delta (1.0->1.25)"),
		FMath::IsNearlyEqual(Obj.AdvanceDwell(1.0, Loc(1000.0, 2000.0, 9000.0), 0.25), 1.25, 0.0001));
	// Ausserhalb (900 > 800) -> Reset auf 0, egal wie viel vorher.
	TestTrue(TEXT("draussen: Reset 0"),
		FMath::IsNearlyEqual(Obj.AdvanceDwell(2.9, Loc(1900.0, 2000.0, 0.0), 0.5), 0.0, 0.0001));

	// Naht 2 - IsComplete: erfuellt, sobald die Verweildauer die Schwelle erreicht.
	auto Ctx = [](double SecondsInRadius) -> FMissionContext
	{
		FMissionContext C; C.SecondsInRadius = SecondsInRadius; return C;
	};
	TestFalse(TEXT("2.9 s < 3 -> offen"), Obj.IsComplete(Ctx(2.9)));
	TestTrue(TEXT("3.0 s >= 3 -> erfuellt"), Obj.IsComplete(Ctx(3.0)));
	TestTrue(TEXT("3.5 s -> erfuellt"), Obj.IsComplete(Ctx(3.5)));
	TestFalse(TEXT("0 s -> offen"), Obj.IsComplete(Ctx(0.0)));

	return true;
}

// Loader: gueltiges JSON -> korrekte Missionen; kaputt -> leer + Fehler;
// unbekannter Ziel-Typ -> uebersprungen + Fehler geloggt.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionLoaderTest,
	"WiesbadenReal.Missions.Loader",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMissionLoaderTest::RunTest(const FString& Parameters)
{
	// -- Gueltig --
	const FString Good = TEXT(
		"{ \"missions\": [ {"
		"  \"id\": \"m1\", \"title\": \"Testfahrt\","
		"  \"reward\": { \"guthaben\": 250 },"
		"  \"deadline_seconds\": 90,"
		"  \"objectives\": ["
		"    { \"type\": \"reach_location\", \"label\": \"A\", \"x\": 100, \"y\": 200, \"radius_cm\": 800 },"
		"    { \"type\": \"reach_location\", \"label\": \"B\", \"x\": 300, \"y\": 400, \"radius_cm\": 500 } ] } ] }");
	FMissionLoadResult R = FWiesbadenMissionLoader::ParseMissions(Good);
	TestTrue(TEXT("gueltig: geparst"), R.bParsed);
	TestEqual(TEXT("gueltig: 1 Mission"), R.Missions.Num(), 1);
	if (R.Missions.Num() == 1)
	{
		const FMission& M = R.Missions[0];
		TestEqual(TEXT("Titel"), M.Title, FString(TEXT("Testfahrt")));
		TestEqual(TEXT("Guthaben"), M.Reward.Guthaben, 250);
		TestTrue(TEXT("Zeitlimit geparst"), FMath::IsNearlyEqual(M.DeadlineSeconds, 90.0));
		TestEqual(TEXT("2 Ziele"), M.Objectives.Num(), 2);
		if (M.Objectives.Num() == 2)
		{
			TestEqual(TEXT("Ziel0 Label"), M.Objectives[0].Label, FString(TEXT("A")));
			TestTrue(TEXT("Ziel0 Ort"), M.Objectives[0].Location.Equals(FVector(100.0, 200.0, 0.0), 0.01));
			TestTrue(TEXT("Ziel0 Radius"), FMath::IsNearlyEqual(M.Objectives[0].RadiusCm, 800.0));
			TestTrue(TEXT("Ziel0 Typ"), M.Objectives[0].Type == EObjectiveType::ReachLocation);
		}
	}

	// -- Kaputt --
	FMissionLoadResult Bad = FWiesbadenMissionLoader::ParseMissions(TEXT("{ das ist kein json"));
	TestFalse(TEXT("kaputt: nicht geparst"), Bad.bParsed);
	TestEqual(TEXT("kaputt: 0 Missionen"), Bad.Missions.Num(), 0);
	TestTrue(TEXT("kaputt: Fehler gemeldet"), Bad.Errors.Num() > 0);

	// -- Unbekannter Ziel-Typ -> uebersprungen, Mission bleibt --
	const FString Unknown = TEXT(
		"{ \"missions\": [ { \"id\": \"m2\", \"title\": \"U\","
		"  \"objectives\": [ { \"type\": \"blah\", \"label\": \"X\", \"x\": 0, \"y\": 0, \"radius_cm\": 100 } ] } ] }");
	FMissionLoadResult UR = FWiesbadenMissionLoader::ParseMissions(Unknown);
	TestTrue(TEXT("unknown: geparst"), UR.bParsed);
	TestEqual(TEXT("unknown: 1 Mission"), UR.Missions.Num(), 1);
	if (UR.Missions.Num() == 1)
	{
		TestEqual(TEXT("unknown: 0 Ziele (uebersprungen)"), UR.Missions[0].Objectives.Num(), 0);
		TestTrue(TEXT("unknown: kein Feld -> unbefristet (0)"),
			FMath::IsNearlyEqual(UR.Missions[0].DeadlineSeconds, 0.0));
	}
	TestTrue(TEXT("unknown: Fehler geloggt"), UR.Errors.Num() > 0);

	// -- LeaveArea-Zieltyp wird als eigener Typ geparst --
	const FString Leave = TEXT(
		"{ \"missions\": [ { \"id\": \"m3\", \"title\": \"Flucht\","
		"  \"objectives\": [ { \"type\": \"leave_area\", \"label\": \"Weg hier\", \"x\": 10, \"y\": 20, \"radius_cm\": 15000 } ] } ] }");
	FMissionLoadResult LR = FWiesbadenMissionLoader::ParseMissions(Leave);
	TestTrue(TEXT("leave: geparst"), LR.bParsed);
	TestEqual(TEXT("leave: 1 Mission"), LR.Missions.Num(), 1);
	if (LR.Missions.Num() == 1)
	{
		TestEqual(TEXT("leave: 1 Ziel (leave_area erkannt)"), LR.Missions[0].Objectives.Num(), 1);
		if (LR.Missions[0].Objectives.Num() == 1)
		{
			TestTrue(TEXT("leave: Typ LeaveArea"),
				LR.Missions[0].Objectives[0].Type == EObjectiveType::LeaveArea);
		}
	}

	// -- Dwell-Zieltyp mit Haltezeit (hold_seconds) wird geparst --
	const FString DwellJson = TEXT(
		"{ \"missions\": [ { \"id\": \"m4\", \"title\": \"Halten\","
		"  \"objectives\": [ { \"type\": \"dwell\", \"label\": \"Position halten\", \"x\": 5, \"y\": 6, \"radius_cm\": 1200, \"hold_seconds\": 8 } ] } ] }");
	FMissionLoadResult DR = FWiesbadenMissionLoader::ParseMissions(DwellJson);
	TestTrue(TEXT("dwell: geparst"), DR.bParsed);
	TestEqual(TEXT("dwell: 1 Mission"), DR.Missions.Num(), 1);
	if (DR.Missions.Num() == 1)
	{
		TestEqual(TEXT("dwell: 1 Ziel (dwell erkannt)"), DR.Missions[0].Objectives.Num(), 1);
		if (DR.Missions[0].Objectives.Num() == 1)
		{
			const FMissionObjective& O = DR.Missions[0].Objectives[0];
			TestTrue(TEXT("dwell: Typ Dwell"), O.Type == EObjectiveType::Dwell);
			TestTrue(TEXT("dwell: Haltezeit 8 s"), FMath::IsNearlyEqual(O.HoldSeconds, 8.0));
		}
	}

	return true;
}

// Fortschritt: Ziel-Kette durchlaufen; letztes Ziel -> Mission fertig + Belohnung.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionRunnerTest,
	"WiesbadenReal.Missions.Runner",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMissionRunnerTest::RunTest(const FString& Parameters)
{
	FMission M;
	M.Reward.Guthaben = 250;
	FMissionObjective O0; O0.Location = FVector(0.0, 0.0, 0.0);       O0.RadiusCm = 800.0; M.Objectives.Add(O0);
	FMissionObjective O1; O1.Location = FVector(10000.0, 0.0, 0.0);   O1.RadiusCm = 800.0; M.Objectives.Add(O1);

	auto Ctx = [](double X, double Y) -> FMissionContext
	{
		FMissionContext C; C.PlayerLocation = FVector(X, Y, 0.0); return C;
	};

	// Fern von Ziel 0 -> kein Fortschritt.
	FMissionProgressResult R = FWiesbadenMissionRunner::Step(M, 0, Ctx(5000.0, 0.0));
	TestFalse(TEXT("fern: kein Fortschritt"), R.bAdvanced);
	TestEqual(TEXT("fern: Index bleibt 0"), R.NextObjectiveIndex, 0);
	TestFalse(TEXT("fern: nicht fertig"), R.bMissionCompleted);

	// An Ziel 0 -> Index 1, noch nicht fertig.
	R = FWiesbadenMissionRunner::Step(M, 0, Ctx(0.0, 0.0));
	TestTrue(TEXT("Ziel0: vorgerueckt"), R.bAdvanced);
	TestEqual(TEXT("Ziel0: Index 1"), R.NextObjectiveIndex, 1);
	TestFalse(TEXT("Ziel0: nicht fertig"), R.bMissionCompleted);
	TestEqual(TEXT("Ziel0: keine Belohnung"), R.GuthabenAwarded, 0);

	// An Ziel 1 (letztes) -> fertig + Belohnung.
	R = FWiesbadenMissionRunner::Step(M, 1, Ctx(10000.0, 0.0));
	TestTrue(TEXT("Ziel1: vorgerueckt"), R.bAdvanced);
	TestTrue(TEXT("Ziel1: Mission fertig"), R.bMissionCompleted);
	TestEqual(TEXT("Ziel1: Guthaben 250"), R.GuthabenAwarded, 250);

	// Ungueltiger Index -> nichts.
	R = FWiesbadenMissionRunner::Step(M, 5, Ctx(0.0, 0.0));
	TestFalse(TEXT("ungueltig: kein Fortschritt"), R.bAdvanced);
	TestFalse(TEXT("ungueltig: nicht fertig"), R.bMissionCompleted);

	return true;
}

// Kleiner Kurierauftrag als Testvorlage: zwei Ziele (Abholung -> Lieferung).
static FMission MakeCourierMission(const TCHAR* Id, const FVector& A, const FVector& B, int32 Reward)
{
	FMission M;
	M.Id = FName(Id);
	M.Title = Id;
	M.Reward.Guthaben = Reward;
	FMissionObjective O0; O0.Location = A; O0.RadiusCm = 800.0; O0.Label = TEXT("Abholung"); M.Objectives.Add(O0);
	FMissionObjective O1; O1.Location = B; O1.RadiusCm = 800.0; O1.Label = TEXT("Lieferung"); M.Objectives.Add(O1);
	return M;
}

// Auftragsvergabe: erst handgeschriebene Missionen in Reihenfolge, danach
// endlos prozedurale Kurierjobs (eindeutige Id, gueltige Route, deterministisch).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionDispatcherTest,
	"WiesbadenReal.Missions.Dispatcher",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMissionDispatcherTest::RunTest(const FString& Parameters)
{
	// -- Leerer Pool -> keine Vergabe --
	const TArray<FMission> Empty;
	TestFalse(TEXT("leer: kein Auftrag"),
		FWiesbadenMissionDispatcher::NextMission(Empty, 0).bHasMission);

	TArray<FMission> Pool;
	Pool.Add(MakeCourierMission(TEXT("m0"), FVector(0.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0), 250));
	Pool.Add(MakeCourierMission(TEXT("m1"), FVector(2000.0, 0.0, 0.0), FVector(3000.0, 0.0, 0.0), 300));

	// -- Phase 1: handgeschriebene Auftraege in Reihenfolge --
	FMissionDispatchResult R0 = FWiesbadenMissionDispatcher::NextMission(Pool, 0);
	TestTrue(TEXT("0: Auftrag"), R0.bHasMission);
	TestFalse(TEXT("0: nicht prozedural"), R0.bProcedural);
	TestEqual(TEXT("0: m0"), R0.Mission.Id.ToString(), FString(TEXT("m0")));

	FMissionDispatchResult R1 = FWiesbadenMissionDispatcher::NextMission(Pool, 1);
	TestFalse(TEXT("1: nicht prozedural"), R1.bProcedural);
	TestEqual(TEXT("1: m1"), R1.Mission.Id.ToString(), FString(TEXT("m1")));

	// -- Phase 2: prozedural, endlos, eindeutige Ids, echte Fahrt --
	FMissionDispatchResult P0 = FWiesbadenMissionDispatcher::NextMission(Pool, 2);
	TestTrue(TEXT("2: Auftrag"), P0.bHasMission);
	TestTrue(TEXT("2: prozedural"), P0.bProcedural);
	TestEqual(TEXT("2: 2 Ziele"), P0.Mission.Objectives.Num(), 2);
	TestTrue(TEXT("2: Belohnung > 0"), P0.Mission.Reward.Guthaben > 0);
	// Prozedurale Jobs sind auto-befristet (deadline_seconds < 0): das Subsystem
	// berechnet daraus beim Start eine faire, distanzabhaengige Frist.
	TestTrue(TEXT("2: auto-befristet (< 0)"), P0.Mission.DeadlineSeconds < 0.0);
	if (P0.Mission.Objectives.Num() == 2)
	{
		TestFalse(TEXT("2: Abholung != Lieferung"),
			P0.Mission.Objectives[0].Location.Equals(P0.Mission.Objectives[1].Location, 0.01));
	}

	FMissionDispatchResult P1 = FWiesbadenMissionDispatcher::NextMission(Pool, 3);
	TestTrue(TEXT("3: prozedural"), P1.bProcedural);
	TestTrue(TEXT("3: andere Id als 2"), P1.Mission.Id != P0.Mission.Id);

	// -- Determinismus: gleiche Eingabe -> gleiche Ausgabe --
	FMissionDispatchResult P0b = FWiesbadenMissionDispatcher::NextMission(Pool, 2);
	TestEqual(TEXT("det: gleiche Id"), P0b.Mission.Id.ToString(), P0.Mission.Id.ToString());
	if (P0.Mission.Objectives.Num() == 2 && P0b.Mission.Objectives.Num() == 2)
	{
		TestTrue(TEXT("det: gleiche Abholung"),
			P0b.Mission.Objectives[0].Location.Equals(P0.Mission.Objectives[0].Location, 0.01));
	}

	// -- Abwechslung: jeder 3. prozedurale Job (Seq%3==2) ist eine Fluchtfahrt:
	//    Abholung -> Gebiet verlassen (LeaveArea) -> Lieferung. --
	FMissionDispatchResult G = FWiesbadenMissionDispatcher::NextMission(Pool, Pool.Num() + 2); // Seq=2
	TestTrue(TEXT("getaway: prozedural"), G.bProcedural);
	TestEqual(TEXT("getaway: 3 Ziele"), G.Mission.Objectives.Num(), 3);
	if (G.Mission.Objectives.Num() == 3)
	{
		TestTrue(TEXT("getaway: Ziel0 Abholung (ReachLocation)"),
			G.Mission.Objectives[0].Type == EObjectiveType::ReachLocation);
		TestTrue(TEXT("getaway: Ziel1 Flucht (LeaveArea)"),
			G.Mission.Objectives[1].Type == EObjectiveType::LeaveArea);
		TestTrue(TEXT("getaway: Ziel2 Lieferung (ReachLocation)"),
			G.Mission.Objectives[2].Type == EObjectiveType::ReachLocation);
	}
	// Ein Nicht-Fluchtjob (Seq=0) bleibt die schlichte 2-Ziel-Fahrt.
	FMissionDispatchResult NG = FWiesbadenMissionDispatcher::NextMission(Pool, Pool.Num());
	TestEqual(TEXT("normal: 2 Ziele"), NG.Mission.Objectives.Num(), 2);
	TestTrue(TEXT("getaway: bessere Praemie als normal"),
		G.Mission.Reward.Guthaben > NG.Mission.Reward.Guthaben);

	return true;
}

// Dubletten-Punkte: enthaelt der Pool denselben Ort mehrfach (in echt z. B. die
// geteilte Platter-Lieferung == Nerotal-Abholung), darf KEIN prozeduraler Job
// Abholung==Lieferung (0-Distanz) erzeugen. Sonst waere die Fahrt sofort erfuellt.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionDispatcherNoZeroLegTest,
	"WiesbadenReal.Missions.DispatcherNoZeroLeg",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMissionDispatcherNoZeroLegTest::RunTest(const FString& Parameters)
{
	// m0 besucht (1000,0); m1 holt am SELBEN Ort ab -> der Punkt (1000,0) steht
	// doppelt in der Routing-Liste (Indizes 0 und 1).
	FMission M0;
	M0.Id = FName(TEXT("m0"));
	FMissionObjective O0; O0.Type = EObjectiveType::ReachLocation;
	O0.Location = FVector(1000.0, 0.0, 0.0); O0.RadiusCm = 800.0;
	M0.Objectives.Add(O0);

	TArray<FMission> Pool;
	Pool.Add(M0);
	Pool.Add(MakeCourierMission(TEXT("m1"), FVector(1000.0, 0.0, 0.0), FVector(5000.0, 0.0, 0.0), 250));

	// Viele prozedurale Jobs durchgehen; keiner darf ein 0-Distanz-Bein haben.
	for (int32 C = Pool.Num(); C < Pool.Num() + 12; ++C)
	{
		const FMissionDispatchResult R = FWiesbadenMissionDispatcher::NextMission(Pool, C);
		if (!R.bProcedural || R.Mission.Objectives.Num() < 2)
		{
			continue;
		}
		const FVector Pickup = R.Mission.Objectives[0].Location;
		const FVector Delivery = R.Mission.Objectives.Last().Location;
		const double DistSq =
			FMath::Square(Pickup.X - Delivery.X) + FMath::Square(Pickup.Y - Delivery.Y);
		TestTrue(FString::Printf(TEXT("Job %d: Abholung != Lieferung (Distanz > 0)"), C),
			DistSq > 1.0);
	}

	return true;
}

// Zeitlimit-Ziel: Mission scheitert (ohne Praemie), wenn die Frist verstreicht,
// bevor alle Ziele erfuellt sind; rechtzeitige Erfuellung hat Vorrang.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionDeadlineTest,
	"WiesbadenReal.Missions.Deadline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMissionDeadlineTest::RunTest(const FString& Parameters)
{
	// Ein Ziel bei (0,0,0), Radius 800, Belohnung 250, Zeitlimit 10 s.
	FMission M;
	M.Reward.Guthaben = 250;
	M.DeadlineSeconds = 10.0;
	FMissionObjective O; O.Location = FVector(0.0, 0.0, 0.0); O.RadiusCm = 800.0; M.Objectives.Add(O);

	auto Ctx = [](double X, double Y, double Elapsed) -> FMissionContext
	{
		FMissionContext C; C.PlayerLocation = FVector(X, Y, 0.0); C.ElapsedSeconds = Elapsed; return C;
	};

	// Vor der Frist, Ziel fern -> offen (kein Fehlschlag, kein Fortschritt).
	FMissionProgressResult R = FWiesbadenMissionRunner::Step(M, 0, Ctx(9000.0, 0.0, 5.0));
	TestFalse(TEXT("vor Frist: kein Fehlschlag"), R.bMissionFailed);
	TestFalse(TEXT("vor Frist: nicht vorgerueckt"), R.bAdvanced);

	// Nach der Frist, Ziel fern -> GESCHEITERT, keine Praemie.
	R = FWiesbadenMissionRunner::Step(M, 0, Ctx(9000.0, 0.0, 11.0));
	TestTrue(TEXT("nach Frist: gescheitert"), R.bMissionFailed);
	TestFalse(TEXT("nach Frist: nicht erfuellt"), R.bMissionCompleted);
	TestEqual(TEXT("nach Frist: keine Praemie"), R.GuthabenAwarded, 0);

	// Rechtzeitig am Ziel -> erfuellt + Praemie, kein Fehlschlag.
	R = FWiesbadenMissionRunner::Step(M, 0, Ctx(0.0, 0.0, 9.0));
	TestTrue(TEXT("rechtzeitig: erfuellt"), R.bMissionCompleted);
	TestFalse(TEXT("rechtzeitig: nicht gescheitert"), R.bMissionFailed);
	TestEqual(TEXT("rechtzeitig: Praemie 250"), R.GuthabenAwarded, 250);

	// Erfuellung schlaegt Frist: am Ziel, aber ueber der Frist -> Erfolg.
	R = FWiesbadenMissionRunner::Step(M, 0, Ctx(0.0, 0.0, 15.0));
	TestTrue(TEXT("Erfuellung schlaegt Frist"), R.bMissionCompleted);
	TestFalse(TEXT("Erfuellung schlaegt Frist: nicht gescheitert"), R.bMissionFailed);

	// Ohne Zeitlimit (0) scheitert nie, egal wie viel Zeit vergeht.
	FMission NoLimit = M; NoLimit.DeadlineSeconds = 0.0;
	R = FWiesbadenMissionRunner::Step(NoLimit, 0, Ctx(9000.0, 0.0, 99999.0));
	TestFalse(TEXT("ohne Limit: scheitert nie"), R.bMissionFailed);

	return true;
}

// Distanzabhaengiges Zeitfenster: faire Frist aus der Routenlaenge. Erwartungs-
// werte sind HAND gerechnet (Tempo 700 cm/s, Puffer 30 s, Untergrenze 45 s),
// nicht aus der Formel abgeleitet - so kann der Test der Implementierung wirklich
// widersprechen.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionDeadlineWindowTest,
	"WiesbadenReal.Missions.DeadlineWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMissionDeadlineWindowTest::RunTest(const FString& Parameters)
{
	auto Obj = [](double X, double Y, double Z) -> FMissionObjective
	{
		FMissionObjective O;
		O.Location = FVector(X, Y, Z);
		return O;
	};
	const FMissionDeadlineParams P; // Standard: 700 cm/s, 30 s, min 45 s

	// -- Gerade Strecke: 70000 cm / 700 + 30 = 100 + 30 = 130 s --
	{
		TArray<FMissionObjective> Objs = { Obj(70000.0, 0.0, 0.0) };
		const double W = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Objs, P);
		TestTrue(TEXT("70000 cm -> 130 s"), FMath::IsNearlyEqual(W, 130.0, 0.01));
	}

	// -- Sehr kurz: 1000 cm -> 31.43 s < 45 -> auf Untergrenze 45 s --
	{
		TArray<FMissionObjective> Objs = { Obj(1000.0, 0.0, 0.0) };
		const double W = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Objs, P);
		TestTrue(TEXT("kurze Route -> Untergrenze 45 s"), FMath::IsNearlyEqual(W, 45.0, 0.01));
	}

	// -- Mehrere Beine summieren sich: 30000 + 40000 = 70000 -> 130 s --
	{
		TArray<FMissionObjective> Objs = { Obj(30000.0, 0.0, 0.0), Obj(30000.0, 40000.0, 0.0) };
		const double W = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Objs, P);
		TestTrue(TEXT("Beine summiert -> 130 s"), FMath::IsNearlyEqual(W, 130.0, 0.01));
	}

	// -- PLANAR: grosser Hoehenunterschied aendert das Fenster nicht (immer 130) --
	{
		TArray<FMissionObjective> Objs = { Obj(70000.0, 0.0, 500000.0) };
		const double W = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Objs, P);
		TestTrue(TEXT("Hoehe ignoriert -> 130 s"), FMath::IsNearlyEqual(W, 130.0, 0.01));
	}

	// -- Ohne Ziele: Untergrenze --
	{
		TArray<FMissionObjective> Objs;
		const double W = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Objs, P);
		TestTrue(TEXT("keine Ziele -> Untergrenze"), FMath::IsNearlyEqual(W, 45.0, 0.01));
	}

	// -- Deterministisch + monoton: laengere Route -> strikt groesseres Fenster --
	{
		TArray<FMissionObjective> Kurz = { Obj(70000.0, 0.0, 0.0) };
		TArray<FMissionObjective> Lang = { Obj(700000.0, 0.0, 0.0) };
		const double A1 = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Kurz, P);
		const double A2 = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Kurz, P);
		const double B  = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Lang, P);
		TestTrue(TEXT("deterministisch"), FMath::IsNearlyEqual(A1, A2, 0.0001));
		TestTrue(TEXT("laenger -> mehr Zeit"), B > A1 + 1.0);
	}

	// -- Parameter wirken: Tempo 1000, kein Puffer, keine Untergrenze --
	// 100000 cm / 1000 + 0 = 100 s (kein Floor).
	{
		FMissionDeadlineParams Q; Q.PaceCmPerSecond = 1000.0; Q.BufferSeconds = 0.0; Q.MinSeconds = 0.0;
		TArray<FMissionObjective> Objs = { Obj(100000.0, 0.0, 0.0) };
		const double W = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Objs, Q);
		TestTrue(TEXT("Parameter wirken -> 100 s"), FMath::IsNearlyEqual(W, 100.0, 0.01));
	}

	return true;
}

// Distanz-Frist mit LeaveArea-Bein (Fluchtfahrt): der erzwungene Umweg aus dem
// Gebiet muss als Fahrtstrecke zaehlen, sonst ist die Frist zu knapp. Erwartungs-
// werte hand gerechnet (Tempo 700 cm/s, Puffer 30 s), nicht aus der Formel.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionDeadlineGetawayTest,
	"WiesbadenReal.Missions.DeadlineGetaway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMissionDeadlineGetawayTest::RunTest(const FString& Parameters)
{
	auto Reach = [](double X, double Y) -> FMissionObjective
	{
		FMissionObjective O; O.Type = EObjectiveType::ReachLocation; O.Location = FVector(X, Y, 0.0); return O;
	};
	auto Leave = [](double X, double Y, double R) -> FMissionObjective
	{
		FMissionObjective O; O.Type = EObjectiveType::LeaveArea; O.Location = FVector(X, Y, 0.0); O.RadiusCm = R; return O;
	};
	const FMissionDeadlineParams P; // 700 cm/s, 30 s, Untergrenze 45 s

	// (1) Lieferung INNERHALB des Fluchtradius -> der Umweg wird budgetiert.
	//   Start(0,0) -> Abholung(0,0) -> Flucht(Zentrum 0,0, R=10000) -> Lieferung(4000,0)
	//   Minimal: 0 + [raus Richtung Lieferung bis Radius: 10000] + [(10000,0)->(4000,0): 6000] = 16000
	//   Fenster = 16000/700 + 30 = 52.857 s. (Ohne Fix nur 4000 cm -> Untergrenze 45 s.)
	{
		TArray<FMissionObjective> Flucht = { Reach(0.0, 0.0), Leave(0.0, 0.0, 10000.0), Reach(4000.0, 0.0) };
		const double W = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Flucht, P);
		TestTrue(TEXT("Flucht nah -> 52.857 s (Umweg gezaehlt)"), FMath::IsNearlyEqual(W, 52.857, 0.02));
	}

	// (2) Ferne Lieferung -> der Ausstieg passiert unterwegs "gratis": gleiches
	//   Fenster wie ein reiner Kurier ueber dieselbe Strecke (Regressions-Waechter).
	//   Beide: 100000 cm -> 100000/700 + 30 = 172.857 s.
	{
		TArray<FMissionObjective> Flucht = { Reach(0.0, 0.0), Leave(0.0, 0.0, 10000.0), Reach(100000.0, 0.0) };
		TArray<FMissionObjective> Kurier = { Reach(0.0, 0.0), Reach(100000.0, 0.0) };
		const double Wf = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Flucht, P);
		const double Wk = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Kurier, P);
		TestTrue(TEXT("Flucht fern -> 172.857 s"), FMath::IsNearlyEqual(Wf, 172.857, 0.02));
		TestTrue(TEXT("Flucht fern == Kurier (kein Aufschlag)"), FMath::IsNearlyEqual(Wf, Wk, 0.01));
	}

	// (3) Regressions-Waechter: reine ReachLocation-Route bleibt unveraendert.
	//   Start(0,0) -> (70000,0): 70000/700 + 30 = 130 s.
	{
		TArray<FMissionObjective> Kurier = { Reach(70000.0, 0.0) };
		const double W = FWiesbadenMissionDeadline::ComputeSeconds(FVector::ZeroVector, Kurier, P);
		TestTrue(TEXT("reine Reach-Route unveraendert -> 130 s"), FMath::IsNearlyEqual(W, 130.0, 0.01));
	}

	return true;
}
