// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/SebboHqElevator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqElevatorTravelTest,
	"WiesbadenReal.World.SebboHq.ElevatorTravel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqElevatorTravelTest::RunTest(const FString& Parameters)
{
	FSebboHqElevator Lift;
	TestTrue(TEXT("Aufzug wartet offen im Erdgeschoss"), Lift.IsStoppedAt(0));
	TestFalse(TEXT("Etage ausserhalb des Turms wird abgewiesen"), Lift.RequestFloor(15));
	TestEqual(TEXT("Ungueltige Wahl veraendert das Ziel nicht"), Lift.TargetFloor, 0);

	TestTrue(TEXT("Dachnahes Geschoss ist waehlbar"), Lift.RequestFloor(14));
	Lift.Tick(0.5);
	TestEqual(TEXT("Kabine faehrt nicht mit offener Tuer"), Lift.PositionCm, 0.0, 0.01);
	TestTrue(TEXT("Tuer beginnt zu schliessen"), Lift.DoorOpenFraction < 1.0);

	bool bMovedWithOpenDoor = false;
	for (int32 Step = 0; Step < 1000 && !Lift.IsStoppedAt(14); ++Step)
	{
		Lift.Tick(0.05);
		bMovedWithOpenDoor |= Lift.Phase == EHqElevatorPhase::Moving
			&& Lift.DoorOpenFraction > KINDA_SMALL_NUMBER;
	}
	TestFalse(TEXT("Tuer bleibt waehrend der Fahrt verriegelt"), bMovedWithOpenDoor);
	TestTrue(TEXT("Kabine erreicht Geschoss 14"), Lift.IsStoppedAt(14));
	TestEqual(TEXT("Kabine steht auf Geschosshoehe"), Lift.PositionCm, 5600.0, 0.01);
	for (int32 Step = 0; Step < 40; ++Step)
	{
		Lift.Tick(0.05);
	}
	TestEqual(TEXT("Tueren oeffnen oben vollstaendig"), Lift.DoorOpenFraction, 1.0, 0.01);

	TestTrue(TEXT("Rueckfahrt ins Erdgeschoss"), Lift.RequestFloor(0));
	for (int32 Step = 0; Step < 1000 && !Lift.IsStoppedAt(0); ++Step)
	{
		Lift.Tick(0.05);
	}
	TestTrue(TEXT("Kabine kommt wieder unten an"), Lift.IsStoppedAt(0));
	TestEqual(TEXT("Keine Drift ueber die Rueckfahrt"), Lift.PositionCm, 0.0, 0.01);
	return true;
}
