// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Core/WiesbadenDevActions.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDevActionsTest,
	"WiesbadenReal.Dev.Actions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDevActionsTest::RunTest(const FString& Parameters)
{
	// Teleportziele: exakte bekannte Weltkoordinaten.
	TestEqual(TEXT("Platter Strasse"),
		FWiesbadenDevActions::TeleportTargetCm(EWiesbadenDevTeleport::PlatterStrasse),
		FVector(-121474.0, -119312.0, 11347.0));
	TestEqual(TEXT("Nerobergbahn"),
		FWiesbadenDevActions::TeleportTargetCm(EWiesbadenDevTeleport::Nerobergbahn),
		FVector(-104083.0, -137317.0, 8800.0));
	TestEqual(TEXT("Garten Nerotal 48"),
		FWiesbadenDevActions::TeleportTargetCm(EWiesbadenDevTeleport::GartenNerotal48),
		FVector(-71366.0, -124226.0, 8530.0));

	// Spawn = Ziel + 300 cm Fallhoehe.
	TestEqual(TEXT("Spawn liegt 300 ueber dem Ziel"),
		FWiesbadenDevActions::TeleportSpawnCm(EWiesbadenDevTeleport::Nerobergbahn).Z,
		8800.0 + 300.0);

	// Aufrichten: Pitch/Roll -> 0, Yaw bleibt, Z +150.
	const FTransform Kippt(FRotator(40.0, 90.0, 25.0), FVector(100.0, 200.0, 500.0), FVector::OneVector);
	const FTransform Auf = FWiesbadenDevActions::UprightTransform(Kippt);
	TestTrue(TEXT("Pitch 0"), FMath::IsNearlyZero(Auf.Rotator().Pitch, 0.01));
	TestTrue(TEXT("Roll 0"), FMath::IsNearlyZero(Auf.Rotator().Roll, 0.01));
	TestEqual(TEXT("Yaw bleibt"), Auf.Rotator().Yaw, 90.0);
	TestEqual(TEXT("Z +150"), Auf.GetLocation().Z, 650.0);
	TestEqual(TEXT("X/Y bleiben"), FVector2D(Auf.GetLocation()), FVector2D(100.0, 200.0));

	// ParseWorldTarget: "X,Y,Z" (cm) -> Vektor, fuer -WbTeleportTo.
	{
		FVector Out;
		TestTrue(TEXT("Gueltiges Tripel wird geparst"),
			FWiesbadenDevActions::ParseWorldTarget(TEXT("193459,-187390,12000"), Out));
		TestEqual(TEXT("Tripel korrekt"), Out, FVector(193459.0, -187390.0, 12000.0));

		FVector Sp;
		TestTrue(TEXT("Leerzeichen werden toleriert"),
			FWiesbadenDevActions::ParseWorldTarget(TEXT(" 10 , 20 , 30 "), Sp));
		TestEqual(TEXT("Getrimmtes Tripel korrekt"), Sp, FVector(10.0, 20.0, 30.0));

		FVector Ignored;
		TestFalse(TEXT("Zu wenige Felder"),
			FWiesbadenDevActions::ParseWorldTarget(TEXT("1,2"), Ignored));
		TestFalse(TEXT("Zu viele Felder"),
			FWiesbadenDevActions::ParseWorldTarget(TEXT("1,2,3,4"), Ignored));
		TestFalse(TEXT("Nicht-numerisch"),
			FWiesbadenDevActions::ParseWorldTarget(TEXT("1,zwei,3"), Ignored));
		TestFalse(TEXT("Leer"),
			FWiesbadenDevActions::ParseWorldTarget(TEXT(""), Ignored));
	}

	return true;
}
