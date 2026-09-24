// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "World/WiesbadenBusDrive.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenBusDriveTest,
	"WiesbadenReal.Vehicles.Bus.PhysicalDrive",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWiesbadenBusDriveTest::RunTest(const FString& Parameters)
{
	WiesbadenBusLine::FBusRoute Route;
	Route.StopArcCm = {0.0, 10000.0, 20000.0};
	Route.TotalLengthCm = 20000.0;
	WiesbadenBusDrive::FState Drive;
	WiesbadenBusLine::FBusState Timetable;
	constexpr float Dt = 0.05f;
	constexpr double CruiseCmS = 32.0 / 3.6 * 100.0;
	WiesbadenBusDrive::FStep Step;

	for (int32 Tick = 0; Tick < 225; ++Tick)
	{
		Timetable.ArcLengthCm = FMath::Min(10000.0, (Tick + 1) * Dt * CruiseCmS);
		Step = WiesbadenBusDrive::Advance(Drive, Timetable, Route, 32.0f, Dt, 601);
		if (Tick == 40)
		{
			TestTrue(TEXT("Bus beschleunigt mit gemeinsamem Fahrzeugmodell"),
				Step.Physics.ForwardSpeedMetersPerS > 1.0f);
			TestTrue(TEXT("Busposition integriert Fahrzeuggeschwindigkeit"),
				Step.Position.ArcLengthCm > 100.0);
		}
	}
	AddInfo(FString::Printf(TEXT("Anfahrt: Bogen %.0f cm, Tempo %.2f m/s, Gang %d, Drehzahl %.0f"),
		Step.Position.ArcLengthCm, Step.Physics.ForwardSpeedMetersPerS,
		Step.Physics.Gear, Step.Physics.EngineRpm));
	TestTrue(TEXT("Bus bremst innerhalb von 15 m vor der Halte"),
		Step.Position.ArcLengthCm > 8500.0 && Step.Position.ArcLengthCm < 10000.0);
	Timetable.ArcLengthCm = 10000.0;
	Timetable.bDwelling = true;
	for (int32 Tick = 0; Tick < 160; ++Tick)
	{
		Step = WiesbadenBusDrive::Advance(Drive, Timetable, Route, 32.0f, Dt, 601);
	}
	AddInfo(FString::Printf(TEXT("Nach Halt: Bogen %.0f cm, Tempo %.2f m/s"),
		Step.Position.ArcLengthCm, Step.Physics.ForwardSpeedMetersPerS));
	TestTrue(TEXT("Bus steht an der Halte"),
		FMath::Abs(Step.Position.ArcLengthCm - 10000.0) < 100.0);
	TestTrue(TEXT("Motorbremse und Bremse bringen ihn zum Stehen"),
		Step.Physics.ForwardSpeedMetersPerS < 0.6f);

	const FQuat Left = WiesbadenBusDrive::WheelVisualRotation(10.0f, false).Quaternion();
	const FQuat Right = WiesbadenBusDrive::WheelVisualRotation(10.0f, true).Quaternion();
	AddInfo(FString::Printf(TEXT("Radkontakt Y links %.3f rechts %.3f"),
		Left.RotateVector(FVector(0, 0, -1)).Y,
		Right.RotateVector(FVector(0, 0, -1)).Y));
	TestTrue(TEXT("Linke Radaussenseite zeigt nach links"),
		Left.RotateVector(FVector(-1, 0, 0)).X < -0.95);
	TestTrue(TEXT("Rechte Radaussenseite zeigt nach rechts"),
		Right.RotateVector(FVector(-1, 0, 0)).X > 0.95);
	TestTrue(TEXT("Beide Reifen rollen in Fahrtrichtung"),
		Left.RotateVector(FVector(0, 0, -1)).Y < -0.1 &&
		Right.RotateVector(FVector(0, 0, -1)).Y < -0.1);
	return true;
}
