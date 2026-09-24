// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vehicles/WiesbadenCar.h"
#include "Components/SceneComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeetleWheelVisualTest,
	"WiesbadenReal.Vehicles.Beetle.WheelVisual",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBeetleWheelVisualTest::RunTest(const FString& Parameters)
{
	// Das Spender-Rad stammt von rechts: seine Felgen-Aussenseite zeigt +Y.
	const FQuat Rechts = AWiesbadenCar::WheelVisualRotation(0.0f, 0.0f, false).Quaternion();
	const FQuat Links = AWiesbadenCar::WheelVisualRotation(0.0f, 0.0f, true).Quaternion();
	TestTrue(TEXT("Rechte Felge zeigt nach aussen"),
		Rechts.RotateVector(FVector::RightVector).Y > 0.99);
	TestTrue(TEXT("Linke Felge zeigt nach aussen"),
		Links.RotateVector(FVector::RightVector).Y < -0.99);

	// Beim Vorwaertsrollen muss ein Punkt unten am Reifen nach hinten laufen.
	const FVector RadUnten(0.0, 0.0, -1.0);
	const FVector RechtsRollt = AWiesbadenCar::WheelVisualRotation(10.0f, 0.0f, false)
		.Quaternion().RotateVector(RadUnten);
	const FVector LinksRollt = AWiesbadenCar::WheelVisualRotation(10.0f, 0.0f, true)
		.Quaternion().RotateVector(RadUnten);
	TestTrue(TEXT("Rechtes Rad rollt vorwaerts"), RechtsRollt.X < -0.1);
	TestTrue(TEXT("Linkes Rad rollt vorwaerts"), LinksRollt.X < -0.1);

	const FVector LinksGelenkt = AWiesbadenCar::WheelVisualRotation(0.0f, 15.0f, true)
		.Quaternion().RotateVector(FVector::RightVector);
	const FVector RechtsGelenkt = AWiesbadenCar::WheelVisualRotation(0.0f, 15.0f, false)
		.Quaternion().RotateVector(FVector::RightVector);
	TestTrue(TEXT("Beide Vorderradachsen lenken gleich"),
		FMath::Abs(LinksGelenkt.X + RechtsGelenkt.X) < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeetleRideHeightTest,
	"WiesbadenReal.Vehicles.Beetle.RideHeight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBeetleRideHeightTest::RunTest(const FString& Parameters)
{
	// Die Wurzel schwebt GroundClearanceCm ueber der Fahrbahn (Kollisionsbox,
	// Bordsteine). Der Reifenaufstand muss trotzdem AUF der Fahrbahn liegen:
	// Hoehe des Radmittelpunkts ueber der Wurzel - Radius == -Bodenfreiheit.
	// Vorher hingen die Raeder direkt an der Wurzel und der Kaefer stand
	// 35 cm in der Luft.
	AWiesbadenCar* Car = GetMutableDefault<AWiesbadenCar>();
	const float Radius = Car->VehiclePhysics.WheelRadiusM * 100.0f;
	auto HeightAboveRoot = [](const USceneComponent* Comp)
	{
		double Z = 0.0;
		for (const USceneComponent* C = Comp; C && C->GetAttachParent(); C = C->GetAttachParent())
		{
			Z += C->GetRelativeLocation().Z;
		}
		return Z;
	};
	for (const TCHAR* Name : { TEXT("FrontLeftWheel"), TEXT("FrontRightWheel"),
		TEXT("RearLeftWheel"), TEXT("RearRightWheel") })
	{
		const USceneComponent* Wheel = Cast<USceneComponent>(Car->GetDefaultSubobjectByName(Name));
		if (!TestNotNull(Name, Wheel)) { continue; }
		const double Contact = HeightAboveRoot(Wheel) - Radius + Car->GroundClearanceCm;
		TestTrue(FString::Printf(TEXT("%s: Reifenaufstand %.1f cm ueber der Fahrbahn"), Name, Contact),
			FMath::Abs(Contact) < 2.0);
	}
	// Die Karosserie hat ihren Ursprung am Reifenaufstand.
	const USceneComponent* Body = Cast<USceneComponent>(Car->GetDefaultSubobjectByName(TEXT("BodyMesh")));
	if (TestNotNull(TEXT("BodyMesh"), Body))
	{
		TestEqual(TEXT("Karosserie-Ursprung liegt auf der Fahrbahn"),
			HeightAboveRoot(Body) + Car->GroundClearanceCm, 0.0, 0.5);
	}
	return true;
}
