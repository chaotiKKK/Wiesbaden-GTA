// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenRailTransport.h"
#include "Misc/AutomationTest.h"
#include "GameFramework/Pawn.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRailTransportProfileTest,
	"WiesbadenReal.World.RailTransport.Profile",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FRailTransportProfileTest::RunTest(const FString& Parameters)
{
	const TArray<double> Distances = { 0.0, 100.0, 200.0, 300.0 };
	const TArray<double> Terrain = { 1000.0, 1050.0, 1020.0, 1100.0 };
	TArray<FWiesbadenRailProfilePoint> Profile;

	TestTrue(TEXT("Profil wird erzeugt"), WiesbadenRailTransport::BuildConstrainedGradeProfile(
		Distances, Terrain, 1040.0, 1140.0, 20.0, 0.5, Profile));
	TestEqual(TEXT("Profilpunkte erhalten"), Profile.Num(), Distances.Num());

	for (int32 Index = 0; Index < Profile.Num(); ++Index)
	{
		TestTrue(TEXT("Mindestabstand zum Terrain"),
			Profile[Index].RailZCm >= Profile[Index].TerrainZCm + 20.0 - KINDA_SMALL_NUMBER);
		if (Index > 0)
		{
			const double Grade = (Profile[Index].RailZCm - Profile[Index - 1].RailZCm)
				/ (Profile[Index].ArcLengthCm - Profile[Index - 1].ArcLengthCm);
			TestTrue(TEXT("Steigung bleibt begrenzt"), FMath::Abs(Grade) <= 0.5 + KINDA_SMALL_NUMBER);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRailTransportMovementTest,
	"WiesbadenReal.World.RailTransport.Movement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FRailTransportMovementTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Talstation fuer Wagen A"),
		WiesbadenRailTransport::OpposingCablePosition(0.0, 1000.0, false), 0.0);
	TestEqual(TEXT("Bergstation fuer Wagen B"),
		WiesbadenRailTransport::OpposingCablePosition(0.0, 1000.0, true), 1000.0);
	TestEqual(TEXT("Wagen B laeuft gegenlaeufig"),
		WiesbadenRailTransport::OpposingCablePosition(250.0, 1000.0, true), 750.0);
	TestEqual(TEXT("Position wird begrenzt"),
		WiesbadenRailTransport::OpposingCablePosition(1200.0, 1000.0, false), 1000.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRailTransportRideStateTest,
	"WiesbadenReal.World.RailTransport.RideState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FRailTransportRideStateTest::RunTest(const FString& Parameters)
{
	using WiesbadenRailTransport::ERideState;
	TestTrue(TEXT("Einsteigen beginnt aus OnFoot"),
		WiesbadenRailTransport::CanTransitionRideState(ERideState::OnFoot, ERideState::Boarding));
	TestTrue(TEXT("Boarding geht in Riding ueber"),
		WiesbadenRailTransport::CanTransitionRideState(ERideState::Boarding, ERideState::Riding));
	TestTrue(TEXT("Riding geht in Exiting ueber"),
		WiesbadenRailTransport::CanTransitionRideState(ERideState::Riding, ERideState::Exiting));
	TestFalse(TEXT("OnFoot springt nicht direkt in Riding"),
		WiesbadenRailTransport::CanTransitionRideState(ERideState::OnFoot, ERideState::Riding));

	WiesbadenRailTransport::FWiesbadenRideSession Session;
	TestFalse(TEXT("Boarding ohne Passagier wird abgelehnt"), Session.BeginBoarding(nullptr, 0));
	return true;
}
