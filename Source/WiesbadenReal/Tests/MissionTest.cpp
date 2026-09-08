// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Missions/WiesbadenMissionTypes.h"
#include "Missions/WiesbadenMissionLoader.h"
#include "Missions/WiesbadenMissionRunner.h"
#include "Missions/WiesbadenMissionDispatcher.h"

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
	}
	TestTrue(TEXT("unknown: Fehler geloggt"), UR.Errors.Num() > 0);

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

	return true;
}
