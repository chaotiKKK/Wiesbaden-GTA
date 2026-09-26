// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicles/WiesbadenSebboFigureComponent.h"

/**
 * Clip-Wahl der Spielerfigur (UWiesbadenSebboFigureComponent::ChooseMove).
 *
 * Die neue Sebbo-Figur bringt zehn Bewegungen mit. Frueher kannte die Figur
 * nur Idle und Walk - und lief nur mit der Kettensaege: mit jeder anderen
 * Waffe stand sie beim Gehen still im Idle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboFigureMoveTest,
	"WiesbadenReal.Vehicles.SebboFigur.Bewegungswahl",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboFigureMoveTest::RunTest(const FString& Parameters)
{
	using EM = EWbSebboMove;
	constexpr float RunFrom = 3.0f, Swagger = 3.67f, Call = 17.29f;
	const auto Pick = [&](FWbSebboMoveState S) { return static_cast<int32>(UWiesbadenSebboFigureComponent::ChooseMove(S, RunFrom, Swagger, Call)); };
	const auto N = [](EM M) { return static_cast<int32>(M); };

	FWbSebboMoveState S;
	TestEqual(TEXT("Stand: Idle"), Pick(S), N(EM::Idle));

	S.SpeedMps = 1.67f;   // Gehen 6 km/h
	TestEqual(TEXT("6 km/h: Walk"), Pick(S), N(EM::Walk));
	S.SpeedMps = 4.44f;   // Sprint 16 km/h
	TestEqual(TEXT("16 km/h: Run"), Pick(S), N(EM::Run));

	S.bAirborne = true;
	TestEqual(TEXT("in der Luft: Jump (auch im Sprint)"), Pick(S), N(EM::Jump));
	S.bRiding = true;
	TestEqual(TEXT("Mitfahrt: Surf (vor allem anderen)"), Pick(S), N(EM::Surf));

	// Drehen im Stand mit Hysterese: an ab 60, aus unter 25 Grad/s.
	FWbSebboMoveState T;
	T.YawRateDegS = 40.0f;
	TestEqual(TEXT("40 Grad/s aus dem Stand: noch Idle"), Pick(T), N(EM::Idle));
	T.YawRateDegS = -90.0f;
	TestEqual(TEXT("-90 Grad/s: Turn (beide Richtungen)"), Pick(T), N(EM::Turn));
	T.Previous = EM::Turn;
	T.YawRateDegS = 40.0f;
	TestEqual(TEXT("40 Grad/s beim Drehen: bleibt Turn"), Pick(T), N(EM::Turn));
	T.YawRateDegS = 10.0f;
	TestEqual(TEXT("10 Grad/s: zurueck zu Idle"), Pick(T), N(EM::Idle));
	T.SpeedMps = 1.67f;
	T.YawRateDegS = 200.0f;
	TestEqual(TEXT("Gehen mit Drehung: Walk, nicht Turn"), Pick(T), N(EM::Walk));

	// Takt im Stand: Idle, Swagger ab 10 s, Idle, Telefonat ab 30 s, von vorn.
	FWbSebboMoveState I;
	const TPair<float, EM> Takt[] = {
		{ 9.9f, EM::Idle }, { 10.0f, EM::Swagger }, { 13.6f, EM::Swagger }, { 13.7f, EM::Idle },
		{ 29.9f, EM::Idle }, { 30.0f, EM::Call }, { 47.2f, EM::Call },
		{ 47.4f, EM::Idle }, { 47.3f + 10.0f, EM::Swagger } };
	for (const TPair<float, EM>& P : Takt)
	{
		I.StillSeconds = P.Key;
		TestEqual(FString::Printf(TEXT("Stand nach %.1f s"), P.Key), Pick(I), N(P.Value));
	}

	// Fehlt das Telefonat (Laenge 0), bleibt es beim Idle - kein Stillstand im Takt.
	I.StillSeconds = 35.0f;
	TestEqual(TEXT("ohne Telefonat: Idle"), static_cast<int32>(UWiesbadenSebboFigureComponent::ChooseMove(I, RunFrom, Swagger, 0.0f)), N(EM::Idle));

	// Ducken: nach Mitfahrt und Luft, vor allem anderen - geduckt wird weder
	// gerannt noch gedreht noch stolziert.
	FWbSebboMoveState D;
	D.bCrouching = true;
	TestEqual(TEXT("geduckt im Stand: CrouchIdle"), Pick(D), N(EM::CrouchIdle));
	D.SpeedMps = 0.97f;   // 3,5 km/h
	TestEqual(TEXT("geduckt 3,5 km/h: CrouchWalk"), Pick(D), N(EM::CrouchWalk));
	D.SpeedMps = 4.44f;
	TestEqual(TEXT("geduckt mit Sprinttempo: CrouchWalk, nicht Run"), Pick(D), N(EM::CrouchWalk));
	D.SpeedMps = 0.0f;
	D.YawRateDegS = 200.0f;
	TestEqual(TEXT("geduckt drehen: CrouchIdle, nicht Turn"), Pick(D), N(EM::CrouchIdle));
	D.YawRateDegS = 0.0f;
	D.StillSeconds = 12.0f;
	TestEqual(TEXT("geduckt 12 s still: CrouchIdle, nicht Swagger"), Pick(D), N(EM::CrouchIdle));
	D.bAirborne = true;
	TestEqual(TEXT("geduckt in der Luft (Kante): Jump"), Pick(D), N(EM::Jump));

	// Jede Bewegung hat einen Asset-Namen (A_Sebbo_<Name>), keiner doppelt.
	TSet<FString> Namen;
	for (int32 M = 0; M < static_cast<int32>(EM::Count); ++M)
	{
		Namen.Add(UWiesbadenSebboFigureComponent::MoveName(static_cast<EM>(M)));
	}
	TestEqual(TEXT("zwoelf verschiedene Namen"), Namen.Num(), 12);
	TestFalse(TEXT("kein leerer Name"), Namen.Contains(FString()));
	return true;
}
