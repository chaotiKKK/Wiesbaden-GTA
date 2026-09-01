// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicles/WiesbadenChaosCar.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleRestTest,
	"WiesbadenReal.Vehicles.RestsOnWheels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehicleRestTest::RunTest(const FString& Parameters)
{
	constexpr double Margin = 2.0;

	// Der aktuelle Fehler: Chassis-Mesh reicht bis Z=0, Radaufstandspunkt liegt
	// bei rund 34 cm (Reifenradius). Chassis-Unterkante sitzt also 34 cm tiefer
	// als der Reifen -> haengt auf dem Bauch, NICHT auf den Raedern.
	TestFalse(TEXT("Chassis unter den Raedern haengt auf dem Bauch"),
		AWiesbadenChaosCar::RestsOnWheels(/*ChassisBottom=*/0.0, /*WheelContact=*/34.3, Margin));

	// Nach dem Fix: Chassis-Kollision endet auf Radhoehe (oder darueber) ->
	// steht auf den Raedern.
	TestTrue(TEXT("Chassis auf Radhoehe steht auf den Raedern"),
		AWiesbadenChaosCar::RestsOnWheels(/*ChassisBottom=*/34.3, /*WheelContact=*/34.3, Margin));
	TestTrue(TEXT("Chassis ueber den Raedern steht auf den Raedern"),
		AWiesbadenChaosCar::RestsOnWheels(/*ChassisBottom=*/40.0, /*WheelContact=*/34.3, Margin));

	// Toleranz: ein paar Millimeter unter Radhoehe zaehlt noch als "auf den
	// Raedern" (Feder-/Messrauschen), deutlich darunter nicht mehr.
	TestTrue(TEXT("Knapp unter Radhoehe noch innerhalb Toleranz"),
		AWiesbadenChaosCar::RestsOnWheels(/*ChassisBottom=*/33.0, /*WheelContact=*/34.3, Margin));
	TestFalse(TEXT("Klar unter Radhoehe ausserhalb Toleranz"),
		AWiesbadenChaosCar::RestsOnWheels(/*ChassisBottom=*/30.0, /*WheelContact=*/34.3, Margin));
	return true;
}
