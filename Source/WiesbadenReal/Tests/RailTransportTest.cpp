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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRailTransportStationGroundingTest,
	"WiesbadenReal.World.RailTransport.StationGrounding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FRailTransportStationGroundingTest::RunTest(const FString& Parameters)
{
	// Nerobergbahn-nah: 438 m Lauf, ~26 % Durchschnittssteigung, klar unter der
	// Maximalsteigung von 30 %. Frueher hob der Aufrufer das obere Ende auf
	// Talhoehe + Laenge * MaxSteigung an - hier 130 m + 131 m = 261 m gegen die
	// echten 245 m -, und die Bergstation hing 16 m in der Luft.
	const double Clearance = 35.0;
	const double MaxGrade = 0.30;
	const double TerrainBottom = 13000.0;   // 130 m
	const double TerrainTop = 24500.0;      // 245 m

	double StartZ = 0.0;
	double EndZ = 0.0;
	WiesbadenRailTransport::StationRailEndpoints(TerrainBottom, TerrainTop, Clearance, StartZ, EndZ);

	// Die Enden ruhen auf dem Gelaende - kein Anheben des Bergendes.
	TestEqual(TEXT("Talstation auf Gelaende"), StartZ, TerrainBottom + Clearance);
	TestEqual(TEXT("Bergstation auf Gelaende"), EndZ, TerrainTop + Clearance);

	const TArray<double> Distances = { 0.0, 15000.0, 30000.0, 43800.0 };
	const TArray<double> Terrain = { 13000.0, 17000.0, 21000.0, 24500.0 };
	TArray<FWiesbadenRailProfilePoint> Profile;
	TestTrue(TEXT("Profil wird erzeugt"), WiesbadenRailTransport::BuildConstrainedGradeProfile(
		Distances, Terrain, StartZ, EndZ, Clearance, MaxGrade, Profile));

	// Das obere Ende steht auf dem Terrain, nicht darueber in der Luft.
	TestTrue(TEXT("Bergstation liegt am Boden"),
		FMath::Abs(Profile.Last().RailZCm - (Terrain.Last() + Clearance)) < 1.0);
	TestTrue(TEXT("Talstation liegt am Boden"),
		FMath::Abs(Profile[0].RailZCm - (Terrain[0] + Clearance)) < 1.0);

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
