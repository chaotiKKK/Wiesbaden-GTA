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

/**
 * Weiches Ueberblenden (FWbSebboMixer, BlendSecondsFor, GaitStartFor).
 *
 * Vorher schaltete PlayAnimation hart: Stehen -> Gehen -> Rennen -> Sprung
 * sprangen von einer Pose in die naechste.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboFigureBlendTest,
	"WiesbadenReal.Vehicles.SebboFigur.Ueberblenden",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboFigureBlendTest::RunTest(const FString& Parameters)
{
	using EM = EWbSebboMove;
	using UFig = UWiesbadenSebboFigureComponent;
	// Laengen wie die echten Clips (Walk 56, Run 30 Bilder bei 30 fps).
	const auto Length = [](EM Move)
	{
		switch (Move)
		{
		case EM::Walk: return 56.0f / 30.0f;
		case EM::Run:  return 1.0f;
		case EM::Jump: return 2.2f;
		default:       return 3.0f;
		}
	};
	const auto Sum = [](const FWbSebboMixer& M)
	{
		float S = 0.0f;
		for (const FWbSebboLayer& L : M.Layers) { S += L.Weight; }
		return S;
	};

	// 1) Stehen -> Gehen ueber 0,2 s: nach 0,1 s halb und halb, dann nur Gehen.
	{
		FWbSebboMixer M;
		M.Start(EM::Idle, true, 1.0f, 0.0f);
		M.Advance(0.5f, Length);
		M.Start(EM::Walk, true, 1.0f, 0.2f);
		M.Advance(0.1f, Length);
		TestTrue(FString::Printf(TEXT("nach 0,1 s: Gehen halb eingeblendet (%.2f)"), M.WeightOf(EM::Walk)),
			FMath::IsNearlyEqual(M.WeightOf(EM::Walk), 0.5f, 0.01f));
		TestTrue(FString::Printf(TEXT("nach 0,1 s: Stehen halb ausgeblendet (%.2f)"), M.WeightOf(EM::Idle)),
			FMath::IsNearlyEqual(M.WeightOf(EM::Idle), 0.5f, 0.01f));
		TestTrue(TEXT("Gewichte ergeben 1"), FMath::IsNearlyEqual(Sum(M), 1.0f, 1e-4f));
		M.Advance(0.1f, Length);
		TestEqual(TEXT("nach 0,2 s: nur noch Gehen"), M.Layers.Num(), 1);
		TestTrue(TEXT("Gehen voll"), FMath::IsNearlyEqual(M.WeightOf(EM::Walk), 1.0f));

		// Gegenprobe: Blendzeit 0 schaltet hart wie das alte PlayAnimation.
		M.Start(EM::Run, true, 1.0f, 0.0f);
		TestTrue(TEXT("Gegenprobe hart: Rennen sofort voll"), FMath::IsNearlyEqual(M.WeightOf(EM::Run), 1.0f));
		TestEqual(TEXT("Gegenprobe hart: Gehen sofort weg"), M.WeightOf(EM::Walk), 0.0f);
	}

	// 2) Umkehr mitten in der Blende: kein Sprung, Abspielstelle bleibt.
	{
		FWbSebboMixer M;
		M.Start(EM::Idle, true, 1.0f, 0.0f);
		M.Advance(1.2f, Length);
		M.Start(EM::Walk, true, 1.0f, 0.2f);
		M.Advance(0.05f, Length);                    // Walk 0,25 / Idle 0,75
		const float IdleTime = M.Layers[0].Time;
		M.Start(EM::Idle, true, 1.0f, 0.2f);
		M.Advance(0.0f, Length);
		TestTrue(FString::Printf(TEXT("Umkehr: Stehen macht dort weiter (%.2f statt 0)"), M.WeightOf(EM::Idle)),
			FMath::IsNearlyEqual(M.WeightOf(EM::Idle), 0.75f, 0.01f));
		TestTrue(TEXT("Umkehr: Stehen behaelt seine Abspielstelle"),
			FMath::IsNearlyEqual(M.Top()->Time, IdleTime, 1e-4f));
		M.Advance(0.05f, Length);                    // restliches Viertel der Blendzeit
		TestTrue(TEXT("Umkehr: nach dem Rest nur noch Stehen"), FMath::IsNearlyEqual(M.WeightOf(EM::Idle), 1.0f));
	}

	// 3) Schneller Wechsel ueber alle vier: hoechstens MaxLayers Spuren, Summe 1.
	{
		FWbSebboMixer M;
		M.Start(EM::Idle, true, 1.0f, 0.0f);
		const EM Folge[] = { EM::Walk, EM::Run, EM::Jump, EM::Idle, EM::Walk, EM::Turn, EM::Run };
		for (const EM Move : Folge)
		{
			M.Start(Move, Move != EM::Jump, 1.0f, 0.3f);
			M.Advance(0.05f, Length);
			TestTrue(TEXT("hoechstens MaxLayers Spuren"), M.Layers.Num() <= FWbSebboMixer::MaxLayers);
			TestTrue(FString::Printf(TEXT("Summe 1 nach %s (%.4f)"), *UFig::MoveName(Move), Sum(M)),
				FMath::IsNearlyEqual(Sum(M), 1.0f, 1e-3f));
		}
	}

	// 4) Schleifen laufen um, Einmalbewegungen bleiben im letzten Bild.
	{
		FWbSebboMixer M;
		M.Start(EM::Run, true, 1.0f, 0.0f);
		M.Advance(1.25f, Length);
		TestTrue(TEXT("Rennen laeuft um (1,25 s -> 0,25 s)"), FMath::IsNearlyEqual(M.Top()->Time, 0.25f, 1e-3f));
		M.Start(EM::Jump, false, 1.0f, 0.1f);
		M.Advance(3.0f, Length);
		TestTrue(TEXT("Sprung steht im letzten Bild"), FMath::IsNearlyEqual(M.Top()->Time, 2.2f, 1e-3f));
	}

	// 5) Blendzeiten.
	TestEqual(TEXT("erster Clip: hart"), UFig::BlendSecondsFor(EM::Count, EM::Idle), 0.0f);
	TestEqual(TEXT("gleiche Bewegung: keine Blende"), UFig::BlendSecondsFor(EM::Walk, EM::Walk), 0.0f);
	TestEqual(TEXT("Stehen -> Gehen 0,2 s"), UFig::BlendSecondsFor(EM::Idle, EM::Walk), 0.2f);
	TestEqual(TEXT("Gehen -> Rennen 0,25 s"), UFig::BlendSecondsFor(EM::Walk, EM::Run), 0.25f);
	TestEqual(TEXT("Absprung 0,1 s"), UFig::BlendSecondsFor(EM::Run, EM::Jump), 0.1f);
	TestEqual(TEXT("Landung 0,15 s"), UFig::BlendSecondsFor(EM::Jump, EM::Idle), 0.15f);

	// 6) Schrittphase: linker Fuss vorn in Walk (0,384) -> linker Fuss vorn in Run (0,167).
	TestTrue(TEXT("Gehen links vorn -> Rennen links vorn"),
		FMath::IsNearlyEqual(UFig::GaitStartFor(EM::Walk, 0.384f, EM::Run), 0.167f, 1e-3f));
	TestTrue(TEXT("zweiter Zyklus von Walk -> zweiter Zyklus von Run (0,667)"),
		FMath::IsNearlyEqual(UFig::GaitStartFor(EM::Walk, 0.884f, EM::Run), 0.667f, 1e-3f));
	TestTrue(TEXT("Viertelschritt bleibt Viertelschritt (0,292)"),
		FMath::IsNearlyEqual(UFig::GaitStartFor(EM::Walk, 0.509f, EM::Run), 0.292f, 1e-3f));
	const float Hin = UFig::GaitStartFor(EM::Walk, 0.7f, EM::Run);
	const float Zurueck = UFig::GaitStartFor(EM::Run, Hin, EM::Walk);
	TestTrue(FString::Printf(TEXT("hin und zurueck: dieselbe Stelle (%.3f)"), Zurueck),
		FMath::IsNearlyEqual(Zurueck, 0.7f, 1e-3f));
	TestTrue(TEXT("Gehen -> Duckgehen: dieselbe Stelle"),
		FMath::IsNearlyEqual(UFig::GaitStartFor(EM::Walk, 0.3f, EM::CrouchWalk), 0.3f, 1e-3f));
	TestEqual(TEXT("Stehen ist kein Gangzyklus"), UFig::GaitStartFor(EM::Idle, 0.3f, EM::Walk), -1.0f);
	return true;
}
