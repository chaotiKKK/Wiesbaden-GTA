// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Missions/WiesbadenMissionTypes.h"

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
