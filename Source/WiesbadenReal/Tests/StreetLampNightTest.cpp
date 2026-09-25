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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreetLampHeadGeometryTest,
	"WiesbadenReal.World.StreetLamps.HeadGeometry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FStreetLampHeadGeometryTest::RunTest(const FString& Parameters)
{
	// Mast, Kappe und Glas sind EIN Netz auf den Mast-Instanzen (Skalierung
	// 0,18 x 0,18 x 7 = 18 cm x 700 cm). Frueher trugen zwei eigene HISM die
	// Koepfe und rissen mit 2 x 72.433 Instanzen das Budget des Rauchtests.
	const FVector Scale(0.18, 0.18, 7.0);
	const FStreetLampGeometry G = URoadFurnitureSpawnerComponent::BuildStreetLampGeometry(Scale);
	TestTrue(TEXT("Drei Ecken je Dreieck"), G.Section.Num() > 0 && G.Positions.Num() == G.Section.Num() * 3
		&& G.Normals.Num() == G.Positions.Num() && G.UVs.Num() == G.Positions.Num());

	auto World = [&Scale](const FVector3f& P) { return FVector(P.X * Scale.X, P.Y * Scale.Y, P.Z * Scale.Z); };
	int32 Inward = 0;
	FBox Mast(ForceInit), Head(ForceInit), Glass(ForceInit), MastRaw(ForceInit);
	for (int32 T = 0; T < G.Section.Num(); ++T)
	{
		const FVector A = World(G.Positions[T * 3]);
		const FVector B = World(G.Positions[T * 3 + 1]);
		const FVector C = World(G.Positions[T * 3 + 2]);
		// Wicklung wie im ganzen Projekt: Cross(B - A, C - A) zeigt nach aussen.
		if (FVector::DotProduct(FVector::CrossProduct(B - A, C - A), FVector(G.Normals[T * 3])) <= 0.0)
		{
			++Inward;
		}
		for (int32 K = 0; K < 3; ++K)
		{
			const FVector P = World(G.Positions[T * 3 + K]);
			if (G.Section[T] == 1)
			{
				Glass += P;
			}
			else if (P.Z <= 350.0 + 0.01)
			{
				Mast += P;
				MastRaw += FVector(G.Positions[T * 3 + K]);
			}
			else
			{
				Head += P;
			}
		}
	}
	TestEqual(TEXT("Kein Dreieck zeigt nach innen (sonst unsichtbar durch Backface-Culling)"), Inward, 0);
	// Mast: genau der Engine-Zylinder - nach Skalierung 18 cm breit, 700 cm hoch.
	TestEqual(TEXT("Mast im Rohraum = Engine-Zylinder (Radius 50)"), MastRaw.Max.X, 50.0, 0.01);
	TestEqual(TEXT("Mast im Rohraum = Engine-Zylinder (Hoehe +-50)"), MastRaw.Max.Z, 50.0, 0.01);
	TestEqual(TEXT("Mast 18 cm breit"), Mast.GetSize().X, 18.0, 0.05);
	TestEqual(TEXT("Mast 700 cm hoch"), Mast.GetSize().Z, 700.0, 0.05);
	// Kopf wie bisher: Kappe 64 cm breit bis 27 cm ueber der Spitze, Glas 50 cm
	// breit von 1 bis 17 cm ueber der Spitze.
	TestEqual(TEXT("Kappe 64 cm breit"), Head.GetSize().X, 64.0, 0.05);
	TestEqual(TEXT("Kappe endet 27 cm ueber der Mastspitze"), Head.Max.Z, 350.0 + 27.0, 0.05);
	TestEqual(TEXT("Glas 50 cm breit"), Glass.GetSize().X, 50.0, 0.05);
	TestEqual(TEXT("Glas beginnt 1 cm ueber der Mastspitze"), Glass.Min.Z, 350.0 + 1.0, 0.05);
	TestEqual(TEXT("Glas endet 17 cm ueber der Mastspitze"), Glass.Max.Z, 350.0 + 17.0, 0.05);
	return true;
}
