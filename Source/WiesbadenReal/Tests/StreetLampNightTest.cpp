// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "World/RoadFurnitureSpawnerComponent.h"
#include "Vehicles/WiesbadenCarLightsComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreetLampNightFactorTest,
	"WiesbadenReal.World.StreetLamps.NightFactor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FStreetLampNightFactorTest::RunTest(const FString& Parameters)
{
	auto Night = [](float Sun) { return URoadFurnitureSpawnerComponent::ComputeStreetLampNightFactor(Sun); };

	TestEqual(TEXT("Mittagssonne: Laternen aus"), Night(0.8f), 0.0f);
	TestEqual(TEXT("Sonne unter dem Horizont: Laternen voll an"), Night(-0.3f), 1.0f);
	// Stadt- und Fahrzeuglicht gehen zusammen an: an der Schwelle der
	// Scheinwerfer-Automatik beginnt das Einblenden.
	TestEqual(TEXT("An der Scheinwerfer-Schwelle noch aus"),
		Night(UWiesbadenCarLightsComponent::AutoHeadlightSunThreshold), 0.0f);
	TestTrue(TEXT("Knapp darunter schon eingeblendet"),
		Night(UWiesbadenCarLightsComponent::AutoHeadlightSunThreshold - 0.05f) > 0.0f);
	// Stetig und monoton - kein Flackern in der Daemmerung.
	float Previous = Night(0.5f);
	for (float Sun = 0.5f; Sun >= -0.2f; Sun -= 0.01f)
	{
		const float Now = Night(Sun);
		TestTrue(FString::Printf(TEXT("Monoton bei Sonne %.2f"), Sun), Now >= Previous - 1e-5f);
		TestTrue(FString::Printf(TEXT("Kein Sprung bei Sonne %.2f"), Sun), Now - Previous < 0.12f);
		Previous = Now;
	}
	return true;
}
