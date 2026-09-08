// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Missions/WiesbadenMissionTypes.h"
#include "Missions/WiesbadenMissionLoader.h"

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
