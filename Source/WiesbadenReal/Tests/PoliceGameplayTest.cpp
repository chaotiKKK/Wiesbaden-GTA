// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "NPC/WiesbadenPoliceSubsystem.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoliceGameplayTest,
	"WiesbadenReal.Polizei.PursuitControls",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FPoliceGameplayTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenPolice;
	TestEqual(TEXT("Keine Streifen ohne Fahndung"), PatrolCount(0), 0);
	TestEqual(TEXT("Eine Streife bei Stufe 1"), PatrolCount(1), 1);
	TestEqual(TEXT("Maximal sechs Streifen"), PatrolCount(99), 6);
	TestEqual(TEXT("Negative Stufe sicher"), PatrolCount(-1), 0);
	const auto Forward = DriveControl({}, FVector::ForwardVector, FVector(1000, 0, 0), 0, 50);
	TestTrue(TEXT("Geradeaus anfahren"), Forward.Throttle > 0 && Forward.Steering == 0);
	TestTrue(TEXT("Rechts +Y positiv"), DriveControl({}, FVector::ForwardVector, FVector(1000, 1000, 0), 0, 50).Steering > 0);
	TestTrue(TEXT("Links -Y negativ"), DriveControl({}, FVector::ForwardVector, FVector(1000, -1000, 0), 0, 50).Steering < 0);
	const auto Fast = DriveControl({}, FVector::ForwardVector, FVector(1000, 0, 0), 80, 50);
	TestTrue(TEXT("Vor Limit bremsen statt weiter Gas"), Fast.Brake > 0 && Fast.Throttle == 0);
	const auto Close = DriveControl({}, FVector::ForwardVector, FVector(100, 0, 0), 10, 50);
	TestTrue(TEXT("Am Ziel stehen bleiben"), Close.Brake == 1 && Close.Throttle == 0);
	return true;
}
