// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenTireEffectsComponent.h"
#include "Vehicles/WiesbadenVehiclePhysics.h"

namespace
{
	constexpr float VehicleDt = 0.01f;

	/** Simuliert das Fahrzeug mit konstanten Eingaben fuer Seconds Sekunden. */
	void Simulate(FWiesbadenVehiclePhysics& Vehicle, const FWiesbadenVehiclePhysicsInput& In, float Seconds)
	{
		FWiesbadenVehiclePhysicsOutput Out;
		for (float T = 0.0f; T < Seconds; T += VehicleDt)
		{
			Vehicle.Tick(In, VehicleDt, Out);
		}
	}

	/** Simuliert und liefert den Zustand am Ende. */
	void SimulateTo(FWiesbadenVehiclePhysics& Vehicle, const FWiesbadenVehiclePhysicsInput& In,
		float Seconds, FWiesbadenVehiclePhysicsOutput& Out)
	{
		for (float T = 0.0f; T < Seconds; T += VehicleDt)
		{
			Vehicle.Tick(In, VehicleDt, Out);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehiclePhysicsStandstillTest,
	"WiesbadenReal.Vehicles.Physics.Standstill",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehiclePhysicsStandstillTest::RunTest(const FString& Parameters)
{
	FWiesbadenVehiclePhysics Vehicle;
	Vehicle.Reset();

	FWiesbadenVehiclePhysicsInput In;
	FWiesbadenVehiclePhysicsOutput Out;
	Vehicle.Tick(In, VehicleDt, Out);

	TestEqual(TEXT("Ohne Gas steht das Fahrzeug"), Out.ForwardSpeedMetersPerS, 0.0f);
	TestEqual(TEXT("Gang 1 im Stand"), Out.Gear, 1);
	TestEqual(TEXT("Keine Gierrate im Stand"), Out.YawRateRadPerS, 0.0f);
	TestTrue(TEXT("Drehzahl am Leerlauf"), Out.EngineRpm >= Vehicle.Powertrain.IdleRpm * 0.9f);

	// Selbst mit voller Lenkung dreht sich ein stehendes Fahrzeug nicht.
	In.Steering = 1.0f;
	Vehicle.Tick(In, VehicleDt, Out);
	TestEqual(TEXT("Lenken im Stand erzeugt keine Gierrate"), Out.YawRateRadPerS, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehiclePhysicsAccelerationTest,
	"WiesbadenReal.Vehicles.Physics.Acceleration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehiclePhysicsAccelerationTest::RunTest(const FString& Parameters)
{
	FWiesbadenVehiclePhysics Vehicle;
	Vehicle.Reset();

	FWiesbadenVehiclePhysicsInput In;
	In.Throttle = 1.0f;
	FWiesbadenVehiclePhysicsOutput Out;

	// Vollgas aus dem Stand; Zeiten bis 60 mph und 100 km/h messen.
	float TimeTo60Mph = -1.0f;
	float TimeTo100 = -1.0f;
	for (float T = 0.0f; T < 60.0f; T += VehicleDt)
	{
		Vehicle.Tick(In, VehicleDt, Out);
		if (TimeTo60Mph < 0.0f && Out.SpeedKmh >= 96.56f)
		{
			TimeTo60Mph = T;
		}
		if (TimeTo100 < 0.0f && Out.SpeedKmh >= 100.0f)
		{
			TimeTo100 = T;
		}
	}

	// Vorlage: Road & Track 9/1973 (VW Sports Bug) 0-60 mph in 18,2 s. Seit der
	// Massekorrektur (970 kg mit Fahrer, 29.09.2026) trifft das Modell das mit
	// Schaltpausen, Schlupf und Triebstrangverlusten (Fahrmessung im Spiel:
	// 18,2-18,3 s; mit 820 kg waren es 15,4 s). +-10 % Rahmen.
	AddInfo(FString::Printf(TEXT("0-60 mph %.1f s, 0-100 km/h %.1f s"), TimeTo60Mph, TimeTo100));
	TestTrue(TEXT("0-100 km/h erreicht"), TimeTo100 > 0.0f);
	TestTrue(FString::Printf(TEXT("0-60 mph wie Road & Track 9/1973, 18,2 s +-10 %% (%.1f s)"), TimeTo60Mph),
		TimeTo60Mph > 16.4f && TimeTo60Mph < 20.0f);
	TestTrue(TEXT("Hoechstgeschwindigkeit wie Kaefer 1302 (125..140 km/h)"),
		Out.SpeedKmh > 125.0f && Out.SpeedKmh < 140.0f);
	TestTrue(TEXT("Automatik schaltet in den hoechsten Gang"), Out.Gear >= 4);

	// Konvergenz: am Limit aendert sich die Geschwindigkeit kaum.
	Vehicle.Tick(In, VehicleDt, Out);
	const float SpeedAfter = Out.ForwardSpeedMetersPerS;
	Vehicle.Tick(In, VehicleDt, Out);
	TestTrue(TEXT("Geschwindigkeit ist stabil am Limit"),
		FMath::Abs(Out.ForwardSpeedMetersPerS - SpeedAfter) < 0.2f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehiclePhysicsBrakingTest,
	"WiesbadenReal.Vehicles.Physics.Braking",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehiclePhysicsBrakingTest::RunTest(const FString& Parameters)
{
	FWiesbadenVehiclePhysics Vehicle;
	Vehicle.Reset();

	// Erst auf ~100 km/h beschleunigen, dann voll bremsen.
	FWiesbadenVehiclePhysicsInput In;
	In.Throttle = 1.0f;
	Simulate(Vehicle, In, 12.0f);
	TestTrue(TEXT("Vor dem Bremsen bewegt sich das Fahrzeug"),
		Vehicle.SpeedMetersPerS > 15.0f);

	FWiesbadenVehiclePhysicsOutput Out;
	In.Throttle = 0.0f;
	In.Brake = 1.0f;
	SimulateTo(Vehicle, In, 12.0f, Out);

	TestTrue(TEXT("Bremse bringt das Fahrzeug zum Stehen"), Out.SpeedKmh < 1.0f);
	TestTrue(TEXT("Keine Rueckwaertsbewegung durch die Bremse"),
		Out.ForwardSpeedMetersPerS >= 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehiclePhysicsSteeringTest,
	"WiesbadenReal.Vehicles.Physics.Steering",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehiclePhysicsSteeringTest::RunTest(const FString& Parameters)
{
	FWiesbadenVehiclePhysics Vehicle;
	Vehicle.Reset();

	FWiesbadenVehiclePhysicsInput In;
	In.Throttle = 1.0f;
	Simulate(Vehicle, In, 3.0f);

	// Der Lenkeinschlag baut sich mit begrenzter Geschwindigkeit auf. Hier stand
	// vorher ein einzelner 10-ms-Tick mit der Erwartung voller Gierrate - das
	// hat genau die Traegheitsfreiheit festgehalten, die abgestellt werden
	// sollte: Tastendruck gleich Volleinschlag im selben Bild.
	FWiesbadenVehiclePhysicsOutput Out;
	In.Steering = 1.0f;
	Simulate(Vehicle, In, 0.5f);
	Vehicle.Tick(In, VehicleDt, Out);
	TestTrue(TEXT("Lenken rechts erzeugt positive Gierrate"), Out.YawRateRadPerS > 0.05f);

	In.Steering = -1.0f;
	Simulate(Vehicle, In, 0.5f);
	Vehicle.Tick(In, VehicleDt, Out);
	TestTrue(TEXT("Lenken links erzeugt negative Gierrate"), Out.YawRateRadPerS < -0.05f);

	// Bei hoher Geschwindigkeit bleibt die Gierrate begrenzt (Untersteuern).
	// Das Budget ist das volle mu*g nur bei Laengsbeschleunigung null; hier
	// wird es als Obergrenze verwendet, die in jedem Fall gilt.
	const float LateralLimit = Vehicle.MuTraction * Vehicle.GravityMetersPerS2 / Vehicle.SpeedMetersPerS;
	TestTrue(TEXT("Gierrate respektiert das Seitenkraftlimit"),
		FMath::Abs(Out.YawRateRadPerS) <= LateralLimit * 1.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehiclePhysicsReverseTest,
	"WiesbadenReal.Vehicles.Physics.Reverse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehiclePhysicsReverseTest::RunTest(const FString& Parameters)
{
	FWiesbadenVehiclePhysics Vehicle;
	Vehicle.Reset();

	FWiesbadenVehiclePhysicsInput In;
	In.bReverseRequested = true;
	In.Throttle = 1.0f;

	FWiesbadenVehiclePhysicsOutput Out;
	SimulateTo(Vehicle, In, 8.0f, Out);

	TestTrue(TEXT("Rueckwaertsgang: negative Geschwindigkeit"),
		Out.ForwardSpeedMetersPerS < -1.0f);
	TestTrue(TEXT("Rueckwaertsgeschwindigkeit begrenzt"),
		Out.ForwardSpeedMetersPerS >= -Vehicle.ReverseMaxSpeedMetersPerS * 1.01f);
	TestEqual(TEXT("Gang -1 meldet Rueckwaerts"), Out.Gear, -1);

	// Am Rueckwaerts-Limit mit Vollgas: der Begrenzer nimmt die Anforderung
	// zurueck, der Wagen haelt sein Tempo OHNE Dauer-Radspin, und die Drehzahl
	// folgt den Raedern (~3500 U/min bei 29 km/h) statt am Anschlag zu kleben.
	TestFalse(TEXT("Am Rueckwaerts-Limit kein Dauer-Radspin"), Out.bWheelSpin);
	TestTrue(FString::Printf(TEXT("Drehzahl am Rueckwaerts-Limit folgt den Raedern (%.0f U/min)"), Out.EngineRpm),
		Out.EngineRpm > 3000.0f && Out.EngineRpm < 4000.0f);

	// Gas weg: der Radspin-Flare klingt ab, die Drehzahl bleibt aber an die
	// rollenden Raeder gekoppelt (bei ~25 km/h rund 3000 U/min, nicht 400).
	In.Throttle = 0.0f;
	SimulateTo(Vehicle, In, 1.0f, Out);
	TestTrue(TEXT("Rueckwaerts rollt der Wagen im Schub weiter"),
		Out.ForwardSpeedMetersPerS < -5.0f);
	TestTrue(FString::Printf(TEXT("Motor dreht auch rueckwaerts mit den Raedern (%.0f U/min)"), Out.EngineRpm),
		Out.EngineRpm > 2500.0f && Out.EngineRpm < 3600.0f);
	In.Brake = 1.0f;
	SimulateTo(Vehicle, In, 4.0f, Out);
	TestEqual(TEXT("Rueckwaerts bremsen endet im Stand, nicht in Vorwaertsfahrt"),
		Out.ForwardSpeedMetersPerS, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehiclePhysicsTractionTest,
	"WiesbadenReal.Vehicles.Physics.TractionLimit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehiclePhysicsTractionTest::RunTest(const FString& Parameters)
{
	FWiesbadenVehiclePhysics Vehicle;
	Vehicle.Reset();

	FWiesbadenVehiclePhysicsInput In;
	In.Throttle = 1.0f;

	FWiesbadenVehiclePhysicsOutput Out;
	const float MaxAccel = Vehicle.MuTraction * Vehicle.GravityMetersPerS2;
	for (int32 Step = 0; Step < 3000; ++Step)
	{
		Vehicle.Tick(In, VehicleDt, Out);
		// Die Beschleunigung darf das Traktionslimit nie ueberschreiten
		// (Luft-/Rollwiderstand druecken sie nur nach unten).
		if (!TestTrue(TEXT("Beschleunigung unter Traktionslimit"),
			Out.ForwardAccelerationMetersPerS2 <= MaxAccel * 1.01f))
		{
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleSteerRateTest,
	"WiesbadenReal.Vehicles.Physics.SteerRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Der Lenkeinschlag folgt der Eingabe mit begrenzter Geschwindigkeit.
 *
 * Die Simulation gab den Lenkbefehl bisher unveraendert an das Giermodell
 * weiter - ein Tastendruck bedeutete Volleinschlag im selben Bild. Kein
 * Fahrzeug auf Raedern verhaelt sich so, und es war der groesste Anteil an
 * dem Eindruck, die Fahrphysik sei unrealistisch.
 */
bool FVehicleSteerRateTest::RunTest(const FString& Parameters)
{
	// Einlenken: 2,5 je Sekunde heisst 0,25 in 0,1 s.
	const float AfterOneStep = FWiesbadenVehiclePhysics::AdvanceSteerAngle(0.0f, 1.0f, 2.5f, 4.0f, 0.1f);
	TestTrue(FString::Printf(TEXT("Ein Schritt von 0,1 s ergibt 0,25 (%.3f)"), AfterOneStep),
		FMath::IsNearlyEqual(AfterOneStep, 0.25f, 0.001f));

	// Von Anschlag zu Anschlag braucht es mehrere Schritte, nicht einen.
	float Angle = 0.0f;
	int32 Steps = 0;
	while (Angle < 0.999f && Steps < 1000)
	{
		Angle = FWiesbadenVehiclePhysics::AdvanceSteerAngle(Angle, 1.0f, 2.5f, 4.0f, 0.1f);
		++Steps;
	}
	TestEqual(TEXT("Voller Einschlag nach 4 Schritten (0,4 s)"), Steps, 4);

	// Zurueckstellen laeuft schneller als Einlenken.
	const float Returning = FWiesbadenVehiclePhysics::AdvanceSteerAngle(1.0f, 0.0f, 2.5f, 4.0f, 0.1f);
	TestTrue(FString::Printf(TEXT("Ruecklauf 0,4 je 0,1 s (%.3f)"), Returning),
		FMath::IsNearlyEqual(Returning, 0.6f, 0.001f));

	// Kein Ueberschwingen ueber den Zielwert hinaus.
	const float Overshoot = FWiesbadenVehiclePhysics::AdvanceSteerAngle(0.0f, 0.05f, 2.5f, 4.0f, 1.0f);
	TestTrue(FString::Printf(TEXT("Kein Ueberschwingen (%.3f)"), Overshoot),
		FMath::IsNearlyEqual(Overshoot, 0.05f, 0.001f));

	// Im Fahrzeug: eine Sechzigstelsekunde Lenkbefehl erzeugt fast keine Gierrate.
	FWiesbadenVehiclePhysics Vehicle;
	Vehicle.Reset();
	FWiesbadenVehiclePhysicsInput In;
	In.Throttle = 1.0f;
	Simulate(Vehicle, In, 3.0f);

	FWiesbadenVehiclePhysicsOutput Out;
	In.Steering = 1.0f;
	Vehicle.Tick(In, 1.0f / 60.0f, Out);
	TestTrue(FString::Printf(TEXT("Ein Bild Lenkbefehl: Einschlag klein (%.3f)"), Out.SteerAngleNorm),
		Out.SteerAngleNorm < 0.05f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleFrictionCircleTest,
	"WiesbadenReal.Vehicles.Physics.FrictionCircle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Laengs- und Querkraft teilen sich ein Reibungsbudget.
 *
 * Ohne diese Kopplung liess sich unter voller Bremsung genauso scharf
 * einlenken wie ohne - 0,7 g bremsen plus 0,75 g Querkraft waeren 1,03 g,
 * mehr als der Reifen uebertragen kann.
 */
bool FVehicleFrictionCircleTest::RunTest(const FString& Parameters)
{
	constexpr float Mu = 0.75f;
	constexpr float G = 9.81f;
	const float Budget = Mu * G;

	// Ohne Laengskraft steht das volle Budget zur Verfuegung.
	const float Free = FWiesbadenVehiclePhysics::ComputeAvailableLateralAccel(Mu, G, 0.0f);
	TestTrue(FString::Printf(TEXT("Ohne Bremsen volles Budget (%.2f von %.2f)"), Free, Budget),
		FMath::IsNearlyEqual(Free, Budget, 0.01f));

	// Bei voller Ausnutzung laengs bleibt nichts uebrig.
	const float Saturated = FWiesbadenVehiclePhysics::ComputeAvailableLateralAccel(Mu, G, Budget);
	TestTrue(FString::Printf(TEXT("Volle Bremsung laesst keine Querkraft (%.2f)"), Saturated),
		Saturated < 0.01f);

	// Halbes Budget laengs -> Wurzel(1 - 0.25) = 0,866 quer.
	const float Half = FWiesbadenVehiclePhysics::ComputeAvailableLateralAccel(Mu, G, Budget * 0.5f);
	TestTrue(FString::Printf(TEXT("Halbe Bremsung laesst 86,6 %% quer (%.2f von %.2f)"), Half, Budget),
		FMath::IsNearlyEqual(Half, Budget * 0.8660f, 0.02f));

	// Ueber das Budget hinaus wird nicht negativ.
	const float Beyond = FWiesbadenVehiclePhysics::ComputeAvailableLateralAccel(Mu, G, Budget * 3.0f);
	TestTrue(TEXT("Ueberlast ergibt keine negative Querkraft"), Beyond >= 0.0f);

	// Im Fahrzeug: unter Vollbremsung faellt die Gierrate gegenueber dem
	// Rollen mit gleichem Lenkeinschlag und gleicher Geschwindigkeit.
	FWiesbadenVehiclePhysics Rolling;
	Rolling.Reset();
	FWiesbadenVehiclePhysicsInput In;
	In.Throttle = 1.0f;
	Simulate(Rolling, In, 8.0f);

	FWiesbadenVehiclePhysics Braking = Rolling;   // gleicher Zustand, gleiche Geschwindigkeit

	FWiesbadenVehiclePhysicsInput Steer;
	Steer.Steering = 1.0f;
	Simulate(Rolling, Steer, 0.6f);
	Simulate(Braking, Steer, 0.6f);

	FWiesbadenVehiclePhysicsOutput RollOut;
	FWiesbadenVehiclePhysicsOutput BrakeOut;
	Rolling.Tick(Steer, VehicleDt, RollOut);

	FWiesbadenVehiclePhysicsInput SteerAndBrake = Steer;
	SteerAndBrake.Brake = 1.0f;
	Braking.Tick(SteerAndBrake, VehicleDt, BrakeOut);

	TestTrue(
		FString::Printf(TEXT("Unter Vollbremsung weniger Gierrate (%.3f statt %.3f rad/s)"),
			BrakeOut.YawRateRadPerS, RollOut.YawRateRadPerS),
		BrakeOut.YawRateRadPerS < RollOut.YawRateRadPerS);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleLoadTransferTest,
	"WiesbadenReal.Vehicles.Physics.LoadTransfer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Laengs-Radlastverlagerung: die Achslasten reagieren auf Gas und Bremse.
 *
 * Bremsen kippt Last nach vorn (Anteil steigt), Beschleunigen nach hinten
 * (Anteil faellt). Ohne diesen Hebel blieb die Kurvenbalance statisch - der
 * groesste fehlende Beitrag zum glaubwuerdigen Fahrgefuehl. Die uebertragene
 * Last ist a_x * h / (g * L).
 */
bool FVehicleLoadTransferTest::RunTest(const FString& Parameters)
{
	constexpr float Static = 0.42f;   // Kaefer hecklastig
	constexpr float G = 9.81f;
	constexpr float H = 0.45f;
	constexpr float L = 2.7f;

	auto Front = [&](float Ax)
	{
		return FWiesbadenVehiclePhysics::ComputeDynamicFrontLoadFraction(Static, Ax, G, H, L);
	};

	// Ohne Laengsbeschleunigung bleibt es beim statischen Anteil.
	TestTrue(FString::Printf(TEXT("Neutral = statisch (%.3f)"), Front(0.0f)),
		FMath::IsNearlyEqual(Front(0.0f), Static, 0.001f));

	// Bremsen (a_x < 0) laedt die Vorderachse.
	const float Braking = Front(-6.0f);
	TestTrue(FString::Printf(TEXT("Bremsen laedt vorn (%.3f > %.3f)"), Braking, Static),
		Braking > Static + 0.02f);

	// Beschleunigen (a_x > 0) entlastet die Vorderachse.
	const float Accel = Front(4.0f);
	TestTrue(FString::Printf(TEXT("Gas entlastet vorn (%.3f < %.3f)"), Accel, Static),
		Accel < Static - 0.02f);

	// Betrag stimmt mit a_x * h / (g * L) ueberein.
	const float Expected = Static - (-6.0f) * H / (G * L);
	TestTrue(FString::Printf(TEXT("Betrag der Verlagerung (%.3f ~ %.3f)"), Braking, Expected),
		FMath::IsNearlyEqual(Braking, Expected, 0.005f));

	// Extremwerte werden geklemmt - keine Achse hebt rechnerisch ganz ab.
	TestTrue(TEXT("Vollbremsung klemmt bei 0,92"),
		FMath::IsNearlyEqual(Front(-50.0f), 0.92f, 0.001f));
	TestTrue(TEXT("Vollgas klemmt bei 0,08"),
		FMath::IsNearlyEqual(Front(50.0f), 0.08f, 0.001f));

	// Im Fahrzeug: Bremsen in eine Kurve bleibt beherrschbar (kein Ausbrechen
	// durch die entlastete Hinterachse - der Schwimmwinkel bleibt endlich und
	// begrenzt).
	FWiesbadenVehiclePhysics Vehicle;
	Vehicle.Reset();
	FWiesbadenVehiclePhysicsInput In;
	In.Throttle = 1.0f;
	Simulate(Vehicle, In, 6.0f);

	FWiesbadenVehiclePhysicsInput TrailBrake;
	TrailBrake.Steering = 1.0f;
	TrailBrake.Brake = 0.6f;
	FWiesbadenVehiclePhysicsOutput Out;
	SimulateTo(Vehicle, TrailBrake, 1.5f, Out);

	TestTrue(FString::Printf(TEXT("Trail-Braking bleibt endlich (%.2f Grad)"), Out.SlipAngleDeg),
		FMath::IsFinite(Out.SlipAngleDeg) && FMath::Abs(Out.SlipAngleDeg) < 45.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleLongitudinalSlipTest,
	"WiesbadenReal.Vehicles.Physics.LongitudinalSlip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Laengsschlupf / Traktionsverlust: durchdrehende Antriebsraeder beim harten
 * Anfahren und blockierende Raeder mit gepulster Bremskraft beim starken
 * Bremsen. Beides ueber Haft-/Gleitreibung mit Hysterese - die letzte grosse
 * Luecke zwischen "berechenbar" und "glaubwuerdig".
 */
bool FVehicleLongitudinalSlipTest::RunTest(const FString& Parameters)
{
	// -- Reine Kennlinie: Haft-/Gleitreibung mit Hysterese -------------------
	{
		const float StaticGrip = 3000.0f;
		const float KineticGrip = 2000.0f;

		// Haftend, Anforderung unter der Haftgrenze -> voll uebertragen.
		bool bSlip = false;
		float F = FWiesbadenVehiclePhysics::ComputeTransmittedLongitudinalForce(2500.0f, StaticGrip, KineticGrip, bSlip);
		TestTrue(TEXT("Unter Haftgrenze: volle Kraft, kein Schlupf"),
			!bSlip && FMath::IsNearlyEqual(F, 2500.0f, 0.1f));

		// Ueber der Haftgrenze -> Rutschen beginnt, Grip faellt auf Gleitreibung.
		F = FWiesbadenVehiclePhysics::ComputeTransmittedLongitudinalForce(3500.0f, StaticGrip, KineticGrip, bSlip);
		TestTrue(FString::Printf(TEXT("Ueber Haftgrenze: Schlupf, Gleitreibung (%.0f)"), F),
			bSlip && FMath::IsNearlyEqual(F, 2000.0f, 0.1f));

		// Rutschend, Anforderung zwischen Gleit- und Haftgrenze -> bleibt rutschend.
		F = FWiesbadenVehiclePhysics::ComputeTransmittedLongitudinalForce(2500.0f, StaticGrip, KineticGrip, bSlip);
		TestTrue(FString::Printf(TEXT("Rutschend bleibt rutschend, Gleitreibung (%.0f)"), F),
			bSlip && FMath::IsNearlyEqual(F, 2000.0f, 0.1f));

		// Rutschend, Anforderung unter Gleitgrenze -> greift wieder (Hysterese).
		F = FWiesbadenVehiclePhysics::ComputeTransmittedLongitudinalForce(1500.0f, StaticGrip, KineticGrip, bSlip);
		TestTrue(FString::Printf(TEXT("Unter Gleitgrenze: greift wieder (%.0f)"), F),
			!bSlip && FMath::IsNearlyEqual(F, 1500.0f, 0.1f));

		// Vorzeichen bleibt erhalten (Rueckwaerts/Bremsen).
		bSlip = false;
		F = FWiesbadenVehiclePhysics::ComputeTransmittedLongitudinalForce(-3500.0f, StaticGrip, KineticGrip, bSlip);
		TestTrue(FString::Printf(TEXT("Negatives Vorzeichen bleibt (%.0f)"), F),
			bSlip && FMath::IsNearlyEqual(F, -2000.0f, 0.1f));
	}

	// -- Reine Kennlinie: gepulste Bremskraft, nie ueber der Haftgrenze ------
	{
		const float StaticGrip = 6000.0f;
		const float KineticGrip = 4000.0f;

		const float High = FWiesbadenVehiclePhysics::ComputeAbsBrakeCapN(StaticGrip, KineticGrip, HALF_PI);
		const float Low = FWiesbadenVehiclePhysics::ComputeAbsBrakeCapN(StaticGrip, KineticGrip, -HALF_PI);
		TestTrue(FString::Printf(TEXT("Puls-Hoch = Haftreibung (%.0f)"), High),
			FMath::IsNearlyEqual(High, StaticGrip, 1.0f));
		TestTrue(FString::Printf(TEXT("Puls-Tief = Gleitreibung (%.0f)"), Low),
			FMath::IsNearlyEqual(Low, KineticGrip, 1.0f));

		// UEber eine volle Periode: nie ueber Haftreibung, Mittel ~ (S+K)/2.
		float Sum = 0.0f; float MaxCap = 0.0f; const int32 N = 360;
		for (int32 i = 0; i < N; ++i)
		{
			const float Cap = FWiesbadenVehiclePhysics::ComputeAbsBrakeCapN(
				StaticGrip, KineticGrip, (2.0f * PI * i) / N);
			Sum += Cap;
			MaxCap = FMath::Max(MaxCap, Cap);
		}
		TestTrue(FString::Printf(TEXT("Bremskraft nie ueber Haftreibung (max %.0f)"), MaxCap),
			MaxCap <= StaticGrip + 1.0f);
		TestTrue(FString::Printf(TEXT("Mittel ~ (Haft+Gleit)/2 (%.0f)"), Sum / N),
			FMath::IsNearlyEqual(Sum / N, 5000.0f, 50.0f));
	}

	// -- Im Fahrzeug: TROCKEN dreht der kalibrierte Kaefer nicht durch --------
	// (R&T 9/1973: 0-30 mph in 5,2 s ohne Traktionsprobleme; 50 PS, 57 % hinten.)
	{
		FWiesbadenVehiclePhysics Vehicle;
		Vehicle.Reset();
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		FWiesbadenVehiclePhysicsOutput Out;
		bool bSpun = false;
		for (int32 Step = 0; Step < 300; ++Step) { Vehicle.Tick(In, VehicleDt, Out); bSpun = bSpun || Out.bWheelSpin; }
		TestFalse(TEXT("Trocken: Vollgas-Start ohne Radspin"), bSpun);
	}

	// -- Im Fahrzeug: NASS (voller Regen) -> Radspin, verschwindet bei Fahrt ---
	{
		FWiesbadenVehiclePhysics Vehicle;
		Vehicle.Reset();
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		In.SurfaceGripScale = 0.65f;
		FWiesbadenVehiclePhysicsOutput Out;

		bool bSpunAtLaunch = false;
		for (int32 Step = 0; Step < 120; ++Step)   // erste ~1,2 s
		{
			Vehicle.Tick(In, VehicleDt, Out);
			bSpunAtLaunch = bSpunAtLaunch || Out.bWheelSpin;
		}
		TestTrue(TEXT("Nass: Vollgas-Start dreht die Antriebsraeder durch"), bSpunAtLaunch);

		// Auf Tempo - bei Fahrt reicht das Moment nicht mehr fuer Radspin.
		Simulate(Vehicle, In, 10.0f);
		Vehicle.Tick(In, VehicleDt, Out);
		TestTrue(TEXT("Bei Fahrt kein Radspin mehr"), !Out.bWheelSpin);
	}

	// -- Blockieren folgt aus der GRIP-Grenze, nicht aus dem Pedalwert -------
	// Die Bremsanforderung wird gegen den reibungskreis-reduzierten Laengs-Grip
	// geprueft. Auf der Geraden (keine Querbeschleunigung) steht der volle Grip
	// mu*g, der ueber der Vollbrems-Anforderung liegt -> kein Block. In der Kurve
	// verbraucht die Querbeschleunigung Grip, bis er unter die Anforderung faellt
	// -> Block. Beides unabhaengig vom konkreten BrakeForceN-Wert.
	{
		FWiesbadenVehiclePhysics V;   // Kaefer-Standardwerte
		const float G = V.GravityMetersPerS2;
		const float FullBrakeDemandN = V.BrakeForceN;

		// Normalbremse ist der KALIBRIERTE Wert (R&T 9/1973, 29.09.2026) - eine
		// Aenderung braucht eine neue Messung als Begruendung.
		TestTrue(FString::Printf(TEXT("BrakeForceN auf kalibriertem Wert (%.0f N)"), V.BrakeForceN),
			FMath::IsNearlyEqual(V.BrakeForceN, 8300.0f, 0.5f));

		// Geradeaus: voller Grip mu*g -> Grip-Kraft ueber der Anforderung -> kein Block.
		const float GripStraightN = V.Powertrain.MassKg *
			FWiesbadenVehiclePhysics::ComputeAvailableLateralAccel(V.MuTraction, G, 0.0f);
		TestTrue(FString::Printf(TEXT("Geradeaus: Grip %.0f N > Vollbrems-Anforderung %.0f N (kein Block)"),
			GripStraightN, FullBrakeDemandN), GripStraightN > FullBrakeDemandN);

		// Zuegige Kurve: Querbeschleunigung zehrt am Grip -> Laengs-Grip < Anforderung.
		const float ALat = 0.6f * V.MuTraction * G;
		const float GripCornerN = V.Powertrain.MassKg *
			FWiesbadenVehiclePhysics::ComputeAvailableLateralAccel(V.MuTraction, G, ALat);
		TestTrue(FString::Printf(TEXT("In der Kurve: Grip %.0f N < Anforderung %.0f N (Block emergiert)"),
			GripCornerN, FullBrakeDemandN), GripCornerN < FullBrakeDemandN);
	}

	// -- Im Fahrzeug: Geradeaus-Vollbremsung blockiert NICHT, stoppt normal ---
	// (Blockiermodell, also ohne ABS - mit ABS meldet bAbsActive die Regelung.)
	{
		FWiesbadenVehiclePhysics Vehicle;
		Vehicle.Reset();
		Vehicle.bAbsEnabled = false;
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		Simulate(Vehicle, In, 10.0f);          // geradeaus auf Tempo (kein Lenken)

		In.Throttle = 0.0f;
		In.Brake = 1.0f;
		FWiesbadenVehiclePhysicsOutput Out;
		bool bAnyLock = false;
		for (int32 Step = 0; Step < 150; ++Step)
		{
			Vehicle.Tick(In, VehicleDt, Out);
			bAnyLock = bAnyLock || Out.bWheelLock;
		}
		TestFalse(TEXT("Geradeaus-Vollbremsung blockiert NICHT (0,7 g unter Grip)"), bAnyLock);
		SimulateTo(Vehicle, In, 8.0f, Out);
		TestTrue(FString::Printf(TEXT("Geradeaus-Bremsung stoppt (%.2f km/h)"), Out.SpeedKmh), Out.SpeedKmh < 1.0f);
		TestTrue(TEXT("Keine Rueckwaertsbewegung durch die Bremse"), Out.ForwardSpeedMetersPerS >= 0.0f);
	}

	// -- Im Fahrzeug: Bremsen in der Kurve -> Blockieren aus der Grip-Grenze --
	// Das Blockiermodell gibt es nur OHNE ABS (mit ABS: Test Physics.Abs).
	{
		FWiesbadenVehiclePhysics Vehicle;
		Vehicle.Reset();
		Vehicle.bAbsEnabled = false;
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		Simulate(Vehicle, In, 8.0f);           // Tempo aufbauen
		In.Steering = 1.0f;
		Simulate(Vehicle, In, 1.0f);           // in die Kurve (Querbeschleunigung)

		In.Throttle = 0.0f;
		In.Brake = 1.0f;                        // hart bremsen, weiter eingelenkt
		FWiesbadenVehiclePhysicsOutput Out;
		bool bLocked = false;
		float MinDecel = 0.0f; float MaxDecel = 0.0f; bool bHaveDecel = false;
		for (int32 Step = 0; Step < 100; ++Step)
		{
			Vehicle.Tick(In, VehicleDt, Out);
			if (Out.bWheelLock)
			{
				bLocked = true;
				const float Decel = Out.ForwardAccelerationMetersPerS2;   // negativ
				if (!bHaveDecel) { MinDecel = MaxDecel = Decel; bHaveDecel = true; }
				MinDecel = FMath::Min(MinDecel, Decel);
				MaxDecel = FMath::Max(MaxDecel, Decel);
			}
		}
		TestTrue(TEXT("Bremsen in der Kurve blockiert (grip-abgeleitet)"), bLocked);
		// Gepulst: die Verzoegerung schwankt spuerbar (Gleit<->Haft), nicht konstant.
		TestTrue(FString::Printf(TEXT("Bremsschlupf pulst (Spanne %.2f m/s^2)"), MaxDecel - MinDecel),
			(MaxDecel - MinDecel) > 0.3f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleSurfaceGripTest,
	"WiesbadenReal.Vehicles.Physics.SurfaceGrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Untergrund-abhaengiger Reibbeiwert: ein griffarmer Belag (SurfaceGripScale < 1)
 * skaliert das effektive mu und wirkt ueber DIESELBE Kopplung wie die
 * Reifenhaftung - Traktion, Anfahr-Radspin und grip-abgeleitetes Brems-
 * blockieren setzen frueher/staerker ein.
 */
bool FVehicleSurfaceGripTest::RunTest(const FString& Parameters)
{
	// -- Anfahren: griffarm dreht mehr durch -> weniger Vortrieb --------------
	{
		FWiesbadenVehiclePhysics Dry;   Dry.Reset();
		FWiesbadenVehiclePhysics Slick; Slick.Reset();

		FWiesbadenVehiclePhysicsInput InDry;   InDry.Throttle = 1.0f;
		FWiesbadenVehiclePhysicsInput InSlick = InDry; InSlick.SurfaceGripScale = 0.4f;

		FWiesbadenVehiclePhysicsOutput OutDry, OutSlick;
		SimulateTo(Dry, InDry, 2.0f, OutDry);
		SimulateTo(Slick, InSlick, 2.0f, OutSlick);

		TestTrue(FString::Printf(TEXT("Griffarm: weniger Vortrieb beim Anfahren (%.0f < %.0f km/h)"),
			OutSlick.SpeedKmh, OutDry.SpeedKmh), OutSlick.SpeedKmh < OutDry.SpeedKmh - 2.0f);
		// ... und der Radspin haelt auf griffarmem Belag laenger an.
		TestTrue(TEXT("Griffarm: Antriebsraeder drehen noch durch"), OutSlick.bWheelSpin);
	}

	// -- Geradeaus-Vollbremsung: trocken haelt, griffarm blockiert -----------
	// (Blockiermodell, also ohne ABS - mit ABS meldet bAbsActive die Regelung.)
	{
		FWiesbadenVehiclePhysics Vehicle; Vehicle.Reset();
		Vehicle.bAbsEnabled = false;
		FWiesbadenVehiclePhysicsInput Acc; Acc.Throttle = 1.0f;
		Simulate(Vehicle, Acc, 8.0f);              // geradeaus auf Tempo

		FWiesbadenVehiclePhysics Slick = Vehicle;  // gleicher Zustand/Tempo

		FWiesbadenVehiclePhysicsInput BrakeDry;  BrakeDry.Brake = 1.0f;  // trocken (1.0)
		FWiesbadenVehiclePhysicsInput BrakeSlick = BrakeDry; BrakeSlick.SurfaceGripScale = 0.5f;

		FWiesbadenVehiclePhysicsOutput Out;
		bool bDryLock = false, bSlickLock = false;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Vehicle.Tick(BrakeDry, VehicleDt, Out);   bDryLock = bDryLock || Out.bWheelLock;
			Slick.Tick(BrakeSlick, VehicleDt, Out);   bSlickLock = bSlickLock || Out.bWheelLock;
		}
		TestFalse(TEXT("Trocken: Geradeaus-Vollbremsung blockiert NICHT"), bDryLock);
		TestTrue(TEXT("Griffarm: Geradeaus-Vollbremsung blockiert (kuerzerer Grip)"), bSlickLock);
	}

	// -- Kennlinie: griffarm hat weniger verfuegbaren Laengs-Grip ------------
	{
		constexpr float G = 9.81f;
		const float DryGrip = FWiesbadenVehiclePhysics::ComputeAvailableLateralAccel(0.75f, G, 0.0f);
		const float SlickGrip = FWiesbadenVehiclePhysics::ComputeAvailableLateralAccel(0.75f * 0.5f, G, 0.0f);
		TestTrue(FString::Printf(TEXT("Griffarm: kleineres Grip-Budget (%.2f < %.2f)"), SlickGrip, DryGrip),
			SlickGrip < DryGrip - 0.5f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleEngineBrakeTest,
	"WiesbadenReal.Vehicles.Physics.EngineBrake",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Gas weg bremst spuerbar - der Motor haengt am Antriebsstrang.
 *
 * Ohne diesen Anteil rollte das Fahrzeug nur gegen Roll- und Luftwiderstand
 * aus und fuehlte sich an, als waere der Gang herausgenommen.
 */
bool FVehicleEngineBrakeTest::RunTest(const FString& Parameters)
{
	FWiesbadenVehiclePhysics WithBrake;
	WithBrake.Reset();

	FWiesbadenVehiclePhysicsInput Full;
	Full.Throttle = 1.0f;
	Simulate(WithBrake, Full, 12.0f);

	FWiesbadenVehiclePhysics WithoutBrake = WithBrake;
	WithoutBrake.EngineBrakeTorqueNm = 0.0f;

	const float StartSpeed = WithBrake.SpeedMetersPerS;
	TestTrue(FString::Printf(TEXT("Ausgangsgeschwindigkeit brauchbar (%.1f m/s)"), StartSpeed),
		StartSpeed > 10.0f);

	// Beide rollen 3 s ohne Gas und ohne Bremse aus.
	const FWiesbadenVehiclePhysicsInput Coast;
	Simulate(WithBrake, Coast, 3.0f);
	Simulate(WithoutBrake, Coast, 3.0f);

	const float LossWith = StartSpeed - WithBrake.SpeedMetersPerS;
	const float LossWithout = StartSpeed - WithoutBrake.SpeedMetersPerS;

	// Schwelle +0.6 statt +1.0: die vereinheitlichte Drehmomentkurve + der
	// angepasste Luftwiderstand verschieben den Betriebspunkt (Spitze ~137 km/h),
	// die Motorbremse setzt am Schub-Beginn bei etwas niedrigerer Drehzahl an.
	// Der QUALITATIVE Nachweis (Gas weg bremst spuerbar staerker) bleibt.
	TestTrue(
		FString::Printf(TEXT("Motorbremse verzoegert staerker (%.2f statt %.2f m/s in 3 s)"),
			LossWith, LossWithout),
		LossWith > LossWithout + 0.6f);

	// Aber nicht so stark, dass es wie eine Bremsung wirkt: unter 3 m/s^2.
	TestTrue(
		FString::Printf(TEXT("Motorbremse bleibt unter 3 m/s^2 (%.2f)"), LossWith / 3.0f),
		LossWith / 3.0f < 3.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleSteeringFalloffTest,
	"WiesbadenReal.Vehicles.Physics.SteeringFalloff",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Der nutzbare Lenkeinschlag muss mit der Geschwindigkeit abnehmen.
 *
 * Zuvor kommandierte ein voll durchgedruecktes A oder D auch bei Tempo 100 den
 * vollen Anschlag von 35 Grad. Die Seitenkraftgrenze fing das ab, aber erst als
 * harte Klemmung - das Fahrzeug schlug in die Begrenzung, statt weich zu
 * reagieren.
 *
 * Geprueft wird beides: dass beim Rangieren der volle Einschlag bleibt UND dass
 * er bei Tempo deutlich faellt.
 */
bool FVehicleSteeringFalloffTest::RunTest(const FString& Parameters)
{
	const FWiesbadenVehiclePhysics Defaults;
	const float MaxDeg = Defaults.MaxSteerAngleDeg;
	const float Falloff = Defaults.SteerFalloffSpeedMetersPerS;

	// Stillstand: voller Anschlag zum Rangieren.
	const float AtRest = FWiesbadenVehiclePhysics::ComputeUsableSteerAngleDeg(MaxDeg, 0.0f, Falloff);
	TestTrue(
		FString::Printf(TEXT("Im Stand voller Einschlag (%.1f von %.1f Grad)"), AtRest, MaxDeg),
		FMath::IsNearlyEqual(AtRest, MaxDeg, 0.01f));

	// Bei der Kennlinien-Geschwindigkeit die Haelfte.
	const float AtFalloff = FWiesbadenVehiclePhysics::ComputeUsableSteerAngleDeg(MaxDeg, Falloff, Falloff);
	TestTrue(
		FString::Printf(TEXT("Bei %.0f m/s die Haelfte (%.1f Grad)"), Falloff, AtFalloff),
		FMath::IsNearlyEqual(AtFalloff, MaxDeg * 0.5f, 0.01f));

	// Bei Landstrassentempo deutlich weniger.
	const float AtHighway = FWiesbadenVehiclePhysics::ComputeUsableSteerAngleDeg(MaxDeg, 33.0f, Falloff);
	TestTrue(
		FString::Printf(TEXT("Bei 120 km/h stark reduziert (%.1f Grad)"), AtHighway),
		AtHighway < MaxDeg * 0.35f && AtHighway > 0.0f);

	// Monoton fallend - kein Sprung, keine Umkehr.
	float Previous = AtRest;
	for (float Speed = 1.0f; Speed <= 40.0f; Speed += 1.0f)
	{
		const float Current = FWiesbadenVehiclePhysics::ComputeUsableSteerAngleDeg(MaxDeg, Speed, Falloff);
		if (!TestTrue(
			FString::Printf(TEXT("Kennlinie faellt monoton (bei %.0f m/s)"), Speed),
			Current <= Previous))
		{
			return false;
		}
		Previous = Current;
	}

	// Bremsverzoegerung im physikalisch Moeglichen: Reifen uebertragen auf
	// trockenem Asphalt hoechstens rund 1 g.
	const float Decel = Defaults.BrakeForceN / FMath::Max(Defaults.Powertrain.MassKg, 1.0f);
	TestTrue(
		FString::Printf(TEXT("Bremsverzoegerung unter 1 g (%.1f m/s^2)"), Decel),
		Decel < 9.81f);
	TestTrue(
		FString::Printf(TEXT("Bremsverzoegerung nicht laecherlich klein (%.1f m/s^2)"), Decel),
		Decel > 4.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleFallStepTest,
	"WiesbadenReal.Vehicles.Physics.FallOverVoid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehicleFallStepTest::RunTest(const FString& Parameters)
{
	// Ohne Boden (ungeladene Zelle) beschleunigt der Wagen nach unten.
	const float V1 = AWiesbadenCar::AdvanceFallSpeedCmS(0.0f, 981.0f, 1.0f);
	TestTrue(TEXT("Nach 1 s faellt er mit ~981 cm/s"),
		FMath::IsNearlyEqual(V1, 981.0f, 1.0f));

	const float V2 = AWiesbadenCar::AdvanceFallSpeedCmS(V1, 981.0f, 1.0f);
	TestTrue(TEXT("Fallgeschwindigkeit waechst monoton"), V2 > V1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleSlipTest,
	"WiesbadenReal.Vehicles.Physics.Slip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * "Nicht auf Schienen": bei zuegiger Kurvenfahrt baut der Wagen einen
 * Karosserie-Schwimmwinkel auf - er bewegt sich NICHT mehr exakt in
 * Blickrichtung, sondern rutscht quer (dynamisches Einspurmodell). Genau das
 * war die urspruengliche Bitte; das kinematische Modell konnte es prinzipiell
 * nicht. Der Schlupf muss spuerbar, aber begrenzt sein (kein Ausbrechen).
 */
bool FVehicleSlipTest::RunTest(const FString& Parameters)
{
	FWiesbadenVehiclePhysics Vehicle;
	Vehicle.Reset();

	// Auf Tempo bringen, dann zuegig einlenken.
	FWiesbadenVehiclePhysicsInput In;
	In.Throttle = 1.0f;
	Simulate(Vehicle, In, 6.0f);

	FWiesbadenVehiclePhysicsOutput Out;
	In.Steering = 1.0f;
	SimulateTo(Vehicle, In, 1.5f, Out);

	// Der Wagen rutscht quer: Schwimmwinkel ungleich null.
	TestTrue(FString::Printf(TEXT("Kurvenfahrt erzeugt Schwimmwinkel (%.2f Grad, nicht auf Schienen)"),
		Out.SlipAngleDeg),
		FMath::Abs(Out.SlipAngleDeg) > 0.5f);

	// ... aber der Wagen bricht nicht aus (Schlupf bleibt beherrschbar).
	TestTrue(FString::Printf(TEXT("Schwimmwinkel bleibt beherrschbar (%.2f Grad)"), Out.SlipAngleDeg),
		FMath::Abs(Out.SlipAngleDeg) < 30.0f);

	// Die Quergeschwindigkeit ist real, nicht null (auf Schienen waere sie null).
	TestTrue(FString::Printf(TEXT("Quergeschwindigkeit vorhanden (%.2f m/s)"), Out.LateralVelocityMetersPerS),
		FMath::Abs(Out.LateralVelocityMetersPerS) > 0.1f);

	// Geradeaus (nach Zuruecklenken) baut sich der Schlupf wieder ab.
	In.Steering = 0.0f;
	SimulateTo(Vehicle, In, 3.0f, Out);
	TestTrue(FString::Printf(TEXT("Geradeaus laeuft der Schlupf aus (%.2f Grad)"), Out.SlipAngleDeg),
		FMath::Abs(Out.SlipAngleDeg) < 2.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleBodyTiltTest,
	"WiesbadenReal.Vehicles.Physics.BodyTilt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehicleBodyTiltTest::RunTest(const FString& Parameters)
{
	// Parameter wie am Fahrzeug voreingestellt.
	const float PitchPer = 0.35f, RollPer = 0.55f, MaxP = 3.5f, MaxR = 5.0f, Resp = 8.0f;

	auto Settle = [&](float LongA, float LatA, float& P, float& R)
	{
		P = 0.0f; R = 0.0f;
		for (int32 i = 0; i < 200; ++i)
		{
			AWiesbadenCar::ComputeBodyTilt(LongA, LatA, PitchPer, RollPer, MaxP, MaxR, Resp, 0.02f, P, R);
		}
	};

	// -- Bremsen: Nase taucht (negatives Nicken) --
	{
		float P, R; Settle(-6.0f, 0.0f, P, R);
		TestTrue(FString::Printf(TEXT("Bremsen: Nase taucht (%.2f < 0)"), P), P < -0.5f);
		TestTrue(TEXT("Bremsen: kein Wanken"), FMath::IsNearlyZero(R, 0.01f));
	}

	// -- Beschleunigen: Nase hebt sich (positives Nicken) --
	{
		float P, R; Settle(3.0f, 0.0f, P, R);
		TestTrue(FString::Printf(TEXT("Beschleunigen: Nase hebt sich (%.2f > 0)"), P), P > 0.5f);
	}

	// -- Kurve: Wanken, Richtung folgt der Querbeschleunigung --
	{
		float PR, RR, PL, RL;
		Settle(0.0f, 5.0f, PR, RR);
		Settle(0.0f, -5.0f, PL, RL);
		TestTrue(FString::Printf(TEXT("Kurve erzeugt Wanken (%.2f)"), RR), FMath::Abs(RR) > 0.5f);
		TestTrue(TEXT("Wanken kehrt mit der Querbeschleunigung die Richtung"),
			FMath::Sign(RR) != FMath::Sign(RL));
	}

	// -- Anschlag haelt auch bei extremer Beschleunigung --
	{
		float P, R; Settle(-50.0f, 50.0f, P, R);
		TestTrue(FString::Printf(TEXT("Nicken am Anschlag (%.2f ~ -%.1f)"), P, MaxP),
			FMath::IsNearlyEqual(P, -MaxP, 0.05f));
		TestTrue(FString::Printf(TEXT("Wanken am Anschlag (%.2f ~ %.1f)"), R, MaxR),
			FMath::IsNearlyEqual(R, MaxR, 0.05f));
	}

	// -- Glaettung: kein Sprung im ersten Bild --
	{
		float P = 0.0f, R = 0.0f;
		AWiesbadenCar::ComputeBodyTilt(-6.0f, 0.0f, PitchPer, RollPer, MaxP, MaxR, Resp, 0.016f, P, R);
		const float Target = -6.0f * PitchPer;   // -2,1
		TestTrue(FString::Printf(TEXT("Erstes Bild nur ein Bruchteil des Ziels (%.3f vs %.2f)"), P, Target),
			FMath::Abs(P) < FMath::Abs(Target) * 0.5f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleBodySpringTest,
	"WiesbadenReal.Vehicles.Physics.BodySpring",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Gefederte Karosserie des Spielerwagens: Feder-Masse statt Glaettung. Gemessen am
 * 29.09.2026 mit der Glaettung: 0,00 Grad Nachschwingen nach dem Stopp, 0 cm
 * Karosseriehub an 13 Kanten - die Karosserie wirkte festgeklebt.
 */
bool FVehicleBodySpringTest::RunTest(const FString& Parameters)
{
	const AWiesbadenCar* Car = GetDefault<AWiesbadenCar>();
	const float Hz = Car->BodySpringHz;
	const float Zeta = Car->BodyDampingRatio;
	constexpr float Dt = 1.0f / 60.0f;

	// -- Sprung auf ein Ziel: sichtbar ueber das Ziel hinaus, dann schnell ruhig --
	{
		float X = 0.0f, V = 0.0f, Max = 0.0f, Rest = 0.0f;
		for (int32 i = 0; i < 180; ++i)   // 3 s
		{
			AWiesbadenCar::AdvanceBodySpring(-2.0f, 0.0f, Hz, Zeta, 0.0f, Dt, X, V);
			Max = FMath::Max(Max, -X);
			if (i * Dt > 1.5f) { Rest = FMath::Max(Rest, FMath::Abs(X + 2.0f)); }
		}
		const float Ueber = 100.0f * (Max - 2.0f) / 2.0f;
		TestTrue(FString::Printf(TEXT("Sichtbares Ueberschwingen (%.0f %%)"), Ueber), Ueber > 10.0f && Ueber < 40.0f);
		TestTrue(FString::Printf(TEXT("Nach 1,5 s ruhig (Rest %.3f Grad)"), Rest), Rest < 0.1f);
	}

	// -- Bremse los nach dem Stopp: die Nase kommt ueber die Ruhelage zurueck --
	{
		float X = -2.7f, V = 0.0f, Gegen = 0.0f;
		for (int32 i = 0; i < 120; ++i)
		{
			AWiesbadenCar::AdvanceBodySpring(0.0f, 0.0f, Hz, Zeta, 0.0f, Dt, X, V);
			Gegen = FMath::Max(Gegen, X);   // Nicken nach OBEN = Nachschwingen
		}
		TestTrue(FString::Printf(TEXT("Nachschwingen nach dem Stopp (%.2f Grad)"), Gegen), Gegen > 0.3f);
		TestTrue(FString::Printf(TEXT("... und wieder in Ruhe (%.3f)"), X), FMath::Abs(X) < 0.05f);
	}

	// -- Kante: kurzer Stoss der Wurzel nach oben -> der Aufbau bleibt zurueck,
	//    federt nach und kehrt in die Ruhelage zurueck; der Anschlag haelt. ------
	{
		float X = 0.0f, V = 0.0f, Tief = 0.0f, Hoch = 0.0f;
		for (int32 i = 0; i < 120; ++i)
		{
			const float Stoss = (i < 3) ? -2000.0f : 0.0f;   // -(Wurzel nach oben)
			AWiesbadenCar::AdvanceBodySpring(0.0f, Stoss, Hz, Zeta, Car->BodyHeaveMaxCm, Dt, X, V);
			Tief = FMath::Min(Tief, X);
			Hoch = FMath::Max(Hoch, X);
		}
		TestTrue(FString::Printf(TEXT("Kante federt ein (%.1f cm)"), Tief), Tief < -2.0f);
		TestTrue(FString::Printf(TEXT("Anschlag haelt (%.1f cm)"), Tief), Tief >= -Car->BodyHeaveMaxCm - 0.01f);
		TestTrue(FString::Printf(TEXT("Federt zurueck ueber die Ruhelage (%.1f cm)"), Hoch), Hoch > 0.3f);
		TestTrue(FString::Printf(TEXT("Nach 2 s wieder in Ruhe (%.2f cm)"), X), FMath::Abs(X) < 0.1f);
	}

	// -- Bildratenfest: 30 und 144 Bilder/s zeigen dieselbe Bewegung ---------
	{
		auto Nach = [&](float Schritt)
		{
			float X = 0.0f, V = 0.0f;
			for (float T = 0.0f; T < 0.4f - 1e-4f; T += Schritt)
			{
				AWiesbadenCar::AdvanceBodySpring(-2.0f, 0.0f, Hz, Zeta, 0.0f, Schritt, X, V);
			}
			return X;
		};
		const float A = Nach(1.0f / 30.0f);
		const float B = Nach(1.0f / 144.0f);
		TestTrue(FString::Printf(TEXT("Bildratenfest (%.3f ~ %.3f)"), A, B), FMath::IsNearlyEqual(A, B, 0.1f));
	}

	// -- Daempfung wirkt: aperiodisch gedaempft schwingt nichts nach ----------
	{
		float X = -2.0f, V = 0.0f, Gegen = 0.0f;
		for (int32 i = 0; i < 120; ++i)
		{
			AWiesbadenCar::AdvanceBodySpring(0.0f, 0.0f, Hz, 1.0f, 0.0f, Dt, X, V);
			Gegen = FMath::Max(Gegen, X);
		}
		TestTrue(FString::Printf(TEXT("Daempfungsgrad 1: kein Nachschwingen (%.3f)"), Gegen), Gegen < 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleTireEffectsTest,
	"WiesbadenReal.Vehicles.TireEffects",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Reifen-Quietsch-Intensitaet aus dem Schlupf-Zustand (datenrein). Macht die
 * bisher nur getesteten Flags produktiv: Radspin quietscht auch langsam,
 * Blockieren/Drift erst mit Fahrt, ruhige Fahrt bleibt still.
 */
bool FVehicleTireEffectsTest::RunTest(const FString& Parameters)
{
	auto I = [](bool Spin, bool Lock, float SlipDeg, float Kmh)
	{
		return UWiesbadenTireEffectsComponent::ComputeSquealIntensity(Spin, Lock, SlipDeg, Kmh);
	};

	// Ruhige Fahrt: kein Quietschen.
	TestTrue(TEXT("Kein Schlupf -> still"), FMath::IsNearlyZero(I(false, false, 0.0f, 50.0f)));

	// Radspin quietscht auch bei geringem Tempo (durchdrehendes Rad).
	TestTrue(TEXT("Radspin quietscht auch langsam"), I(true, false, 0.0f, 5.0f) > 0.5f);
	// ... sogar im Stand (Burnout).
	TestTrue(TEXT("Radspin quietscht im Stand"), I(true, false, 0.0f, 0.0f) > 0.5f);

	// Blockieren quietscht mit Fahrt, aber NICHT im Stand (stehendes Rad rutscht nicht).
	TestTrue(TEXT("Blockieren + Fahrt quietscht"), I(false, true, 0.0f, 50.0f) > 0.5f);
	TestTrue(TEXT("Blockieren im Stand still"), FMath::IsNearlyZero(I(false, true, 0.0f, 0.0f)));

	// Drift: grosser Schwimmwinkel quietscht, kleiner nicht.
	TestTrue(TEXT("Grosser Schwimmwinkel quietscht"), I(false, false, 20.0f, 50.0f) > 0.3f);
	TestTrue(TEXT("Kleiner Schwimmwinkel still"), FMath::IsNearlyZero(I(false, false, 3.0f, 50.0f)));

	// Wertebereich bleibt 0..1.
	TestTrue(TEXT("Intensitaet in [0,1]"),
		I(true, true, 45.0f, 120.0f) <= 1.0f && I(true, true, 45.0f, 120.0f) >= 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleLockedYawDampingTest,
	"WiesbadenReal.Vehicles.Physics.LockedYawDamping",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Kurvenbremsung mit blockierten Raedern reisst den Wagen nicht mehr weit herum.
 * Die Gier-Daempfung wirkt NUR bei blockierten Raedern - sie zaehmt den
 * ueberschiessenden Dreh, ohne das Blockieren abzuschalten; gerades Bremsen und
 * normale Kurvenfahrt bleiben unveraendert. Blockieren gibt es nur OHNE ABS -
 * alle Szenarien hier schalten es darum ab.
 */
bool FVehicleLockedYawDampingTest::RunTest(const FString& Parameters)
{
	// -- Kurvenbremsung: aufsummierte Kursaenderung waehrend der Bremsung ------
	auto RunCornerBrake = [](float DampRate, float& OutHeadingDeg, bool& OutLocked)
	{
		FWiesbadenVehiclePhysics V;
		V.Reset();
		V.bAbsEnabled = false;
		V.LockedYawDampingRate = DampRate;

		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		Simulate(V, In, 8.0f);        // Tempo aufbauen
		In.Steering = 1.0f;
		Simulate(V, In, 1.0f);        // in die Kurve (Gier aufbauen)

		In.Throttle = 0.0f;
		In.Brake = 1.0f;              // hart bremsen, weiter eingelenkt
		FWiesbadenVehiclePhysicsOutput Out;
		float Heading = 0.0f;
		bool bLocked = false;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			V.Tick(In, VehicleDt, Out);
			Heading += Out.YawRateRadPerS * VehicleDt;
			bLocked = bLocked || Out.bWheelLock;
		}
		OutHeadingDeg = FMath::RadiansToDegrees(Heading);
		OutLocked = bLocked;
	};

	float HDamped = 0.0f, HUndamped = 0.0f;
	bool LDamped = false, LUndamped = false;
	RunCornerBrake(3.0f, HDamped, LDamped);   // Standard-Daempfung
	RunCornerBrake(0.0f, HUndamped, LUndamped); // wie vorher (aus)

	// Das Blockieren bleibt in BEIDEN Faellen erhalten (nicht abgeschaltet).
	TestTrue(TEXT("Blockieren bleibt erhalten (gedaempft)"), LDamped);
	TestTrue(TEXT("Blockieren bleibt erhalten (ungedaempft)"), LUndamped);

	// Ohne Daempfung bricht der Wagen deutlich aus (Bezugsgroesse).
	TestTrue(FString::Printf(TEXT("Ungedaempfter Ausbruch ist gross (%.0f Grad)"), HUndamped),
		FMath::Abs(HUndamped) > 10.0f);

	// Mit Daempfung ist der Dreh spuerbar begrenzt ...
	TestTrue(FString::Printf(TEXT("Ausbruch gedaempft (%.0f statt %.0f Grad)"), HDamped, HUndamped),
		FMath::Abs(HDamped) < FMath::Abs(HUndamped) * 0.8f);
	// ... aber der Lastwechsel ist nicht plattgemacht (etwas Dreh bleibt).
	TestTrue(FString::Printf(TEXT("Rest-Lastwechsel bleibt (%.1f Grad)"), HDamped),
		FMath::Abs(HDamped) > 2.0f);

	// -- Gerades Vollbremsen: unveraendert -----------------------------------
	auto RunStraightBrake = [](float DampRate, float& OutSpeed)
	{
		FWiesbadenVehiclePhysics V;
		V.Reset();
		V.bAbsEnabled = false;
		V.LockedYawDampingRate = DampRate;
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		Simulate(V, In, 8.0f);
		In.Throttle = 0.0f;
		In.Brake = 1.0f;
		FWiesbadenVehiclePhysicsOutput Out;
		SimulateTo(V, In, 3.0f, Out);
		OutSpeed = Out.SpeedKmh;
	};
	float S1 = 0.0f, S0 = 0.0f;
	RunStraightBrake(3.0f, S1);
	RunStraightBrake(0.0f, S0);
	TestTrue(TEXT("Gerades Bremsen unveraendert"), FMath::IsNearlyEqual(S1, S0, 0.01f));

	// -- Normale Kurve (Gas, keine Bremse): unveraendert ---------------------
	auto RunCorner = [](float DampRate, float& OutYaw)
	{
		FWiesbadenVehiclePhysics V;
		V.Reset();
		V.bAbsEnabled = false;
		V.LockedYawDampingRate = DampRate;
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		Simulate(V, In, 6.0f);
		In.Steering = 1.0f;
		FWiesbadenVehiclePhysicsOutput Out;
		SimulateTo(V, In, 1.5f, Out);
		OutYaw = Out.YawRateRadPerS;
	};
	float Y1 = 0.0f, Y0 = 0.0f;
	RunCorner(3.0f, Y1);
	RunCorner(0.0f, Y0);
	TestTrue(TEXT("Normale Kurve unveraendert (nicht blockiert)"), FMath::IsNearlyEqual(Y1, Y0, 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleAbsTest,
	"WiesbadenReal.Vehicles.Physics.Abs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * ABS: eine Vollbremsung in der Kurve laesst die Raeder nicht gleiten - der Wagen
 * bleibt lenkbar und dreht weiter in die Kurve, statt blockiert geradeaus zu
 * rutschen. Gemessen im Spiel (29.09.2026): ohne ABS blieb eine Vollbremsung aus
 * der Kurve bis zum Stillstand blockiert. Geradeaus aendert das ABS nichts (dort
 * reicht der Grip fuer das Pedal), und die Handbremse blockiert weiterhin.
 */
bool FVehicleAbsTest::RunTest(const FString& Parameters)
{
	struct FErgebnis
	{
		float KursGrad = 0.0f; bool bGeglitten = false; bool bLeuchte = false; bool bSpuren = false;
		bool bVorneGleitet = false; bool bHintenGleitet = false; float KmhNach = 0.0f;
	};
	auto Kurvenbremsung = [](bool bAbs, bool bHandbremse)
	{
		FWiesbadenVehiclePhysics V;
		V.Reset();
		V.bAbsEnabled = bAbs;
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		Simulate(V, In, 8.0f);        // Tempo aufbauen
		In.Steering = 1.0f;
		Simulate(V, In, 1.0f);        // in die Kurve (Querbeschleunigung)

		In.Throttle = 0.0f;
		In.Brake = 1.0f;              // voll bremsen, Lenkung bleibt
		In.bHandbrake = bHandbremse;
		FErgebnis E;
		FWiesbadenVehiclePhysicsOutput Out;
		for (int32 Step = 0; Step < 90; ++Step)
		{
			V.Tick(In, VehicleDt, Out);
			E.KursGrad += FMath::RadiansToDegrees(Out.YawRateRadPerS * VehicleDt);
			E.bVorneGleitet = E.bVorneGleitet || V.bFrontAxleSliding;
			E.bHintenGleitet = E.bHintenGleitet || V.bRearAxleSliding;
			E.bGeglitten = E.bVorneGleitet || E.bHintenGleitet;
			E.bLeuchte = E.bLeuchte || Out.bAbsActive;
			E.bSpuren = E.bSpuren || Out.bWheelLock;
		}
		E.KmhNach = Out.SpeedKmh;
		return E;
	};

	const FErgebnis Abs = Kurvenbremsung(true, false);
	const FErgebnis Ohne = Kurvenbremsung(false, false);

	TestFalse(TEXT("Mit ABS gleiten die Raeder nie"), Abs.bGeglitten);
	TestTrue(TEXT("Ohne ABS gleiten sie (Bezug)"), Ohne.bGeglitten);
	TestTrue(TEXT("Die Leuchte meldet die ABS-Regelung"), Abs.bLeuchte);
	// Das ABS haelt die Raeder rollend - keine Bremsspuren, kein Blockier-
	// Quietschen (Review PR #26: vorher zog jeder Halt Spuren unter allen vier).
	TestFalse(TEXT("ABS-Regelung ist kein Blockieren (keine Spuren)"), Abs.bSpuren);
	TestTrue(TEXT("Ohne ABS blockieren sie (Bezug)"), Ohne.bSpuren);
	TestTrue(FString::Printf(TEXT("Mit ABS lenkbar: Kurs %.1f statt %.1f Grad"), Abs.KursGrad, Ohne.KursGrad),
		Abs.KursGrad > Ohne.KursGrad * 1.3f);
	TestTrue(FString::Printf(TEXT("Kein Dreher beim Bremsen (%.1f Grad)"), Abs.KursGrad),
		FMath::Abs(Abs.KursGrad) < 90.0f);
	// Der Preis der Stabilitaet: die Hinterachse bremst mit der Verteilung nur so
	// stark, dass sie Seitenfuehrung behaelt - etwas weniger Verzoegerung als vier
	// gleitende Raeder, aber kaum (in 0,9 s hoechstens 4 km/h; seit der Grip-
	// Kalibrierung 29.09.2026 nutzt das Blockiermodell das staerkere Pedal voll,
	// das ABS haelt die Hinterachse fuer die Stabilitaet zurueck: 3,3 km/h).
	TestTrue(FString::Printf(TEXT("Bremst fast so stark (%.1f gegen %.1f km/h)"), Abs.KmhNach, Ohne.KmhNach),
		Abs.KmhNach <= Ohne.KmhNach + 4.0f);

	// Geradeaus: das ABS regelt vorn an der Achsgrenze (Pedal * Verteilung liegt
	// darueber), der Wagen verzoegert trotzdem kraeftig und bleibt gerade.
	{
		FWiesbadenVehiclePhysics V;
		V.Reset();
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		Simulate(V, In, 10.0f);
		const float V0 = V.SpeedMetersPerS;
		In.Throttle = 0.0f;
		In.Brake = 1.0f;
		FWiesbadenVehiclePhysicsOutput Out;
		SimulateTo(V, In, 1.0f, Out);
		const float VerzoegerungG = (V0 - Out.ForwardSpeedMetersPerS) / 1.0f / 9.81f;
		TestTrue(FString::Printf(TEXT("Geradeaus kraeftig (%.2f g)"), VerzoegerungG), VerzoegerungG > 0.6f);
		TestTrue(TEXT("Geradeaus ohne Gier"), FMath::Abs(Out.YawRateRadPerS) < 0.001f);
		TestFalse(TEXT("Geradeaus gleitet nichts"), V.bBrakeLockState);
	}

	// Die Handbremse wirkt am ABS vorbei nur HINTEN: das Heck gleitet, die
	// Vorderachse bleibt geregelt und lenkt - der Wagen dreht ein
	// (Handbremswende). Review PR #26: vorher verloren beide Achsen die
	// Seitenfuehrung, der Wagen rutschte gedaempft geradeaus.
	{
		const FErgebnis Hb = Kurvenbremsung(true, true);
		TestTrue(TEXT("Handbremse blockiert das Heck auch mit ABS"), Hb.bHintenGleitet && Hb.bSpuren);
		TestFalse(TEXT("... die Vorderachse gleitet nicht"), Hb.bVorneGleitet);
		TestTrue(FString::Printf(TEXT("... und dreht staerker ein als ohne Handbremse (%.1f gegen %.1f Grad)"),
			Hb.KursGrad, Abs.KursGrad), Hb.KursGrad > Abs.KursGrad);
	}

	// Aus schneller Kurve GERADE bremsen (Lenkung zurueck auf 0): der Wagen faengt
	// sich. Im Spiel gemessen (29.09.2026): solange der Reibungskreis den Luft-
	// widerstand mitzaehlte, lag die Verzoegerung bei 120 km/h ueber mu*g, die
	// Seitenfuehrung fiel auf null, und der Wagen drehte bis 43 Grad Schwimmwinkel weg.
	{
		FWiesbadenVehiclePhysics V;
		V.Reset();
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		Simulate(V, In, 16.0f);       // ~110 km/h
		In.Steering = -0.6f;
		Simulate(V, In, 1.5f);        // schnelle Linkskurve am Limit
		In.Throttle = 0.0f;
		In.Steering = 0.0f;
		In.Brake = 1.0f;
		FWiesbadenVehiclePhysicsOutput Out;
		float MaxSchwimm = 0.0f;
		for (int32 Step = 0; Step < 300; ++Step)
		{
			V.Tick(In, VehicleDt, Out);
			MaxSchwimm = FMath::Max(MaxSchwimm, FMath::Abs(Out.SlipAngleDeg));
		}
		// Etwas Lastwechsel beim Heckmotor-Kaefer ist echt - aber kein Dreher, und
		// nach drei Sekunden steht er wieder gerade.
		TestTrue(FString::Printf(TEXT("Gerade Bremsung nach der Kurve: kein Dreher (Schwimmwinkel max %.1f Grad)"), MaxSchwimm),
			MaxSchwimm < 20.0f);
		TestTrue(FString::Printf(TEXT("... und faengt sich (Schwimmwinkel am Ende %.1f Grad)"), Out.SlipAngleDeg),
			FMath::Abs(Out.SlipAngleDeg) < 3.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleShiftingTest,
	"WiesbadenReal.Vehicles.Physics.Shifting",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Hochschalten mit Zugkraftunterbrechung: fuer die Schaltdauer faellt das
 * Antriebsmoment weg (die Beschleunigung sackt kurz ab) und erholt sich danach.
 * Ohne Schaltvorgang bleibt alles unveraendert; die Schaltverluste kosten etwas
 * 0-100-Zeit, die Endgeschwindigkeit bleibt gleich.
 */
bool FVehicleShiftingTest::RunTest(const FString& Parameters)
{
	// -- Delle beim Hochschalten -------------------------------------------
	{
		FWiesbadenVehiclePhysics V;
		V.Reset();   // Standard: UpshiftDurationSeconds = 0.35 s
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		FWiesbadenVehiclePhysicsOutput Out;

		int32 PrevGear = V.Gear;
		float AccelBefore = 0.0f;
		bool bFound = false;
		for (int32 Step = 0; Step < 600 && !bFound; ++Step)
		{
			const float LastAccel = AccelBefore;
			V.Tick(In, VehicleDt, Out);
			if (Out.Gear > PrevGear)
			{
				// Hochschalten passierte in diesem Tick.
				TestTrue(TEXT("Schaltunterbrechung wird gesetzt"), V.ShiftTimeRemaining > 0.0f);

				float MinDuring = TNumericLimits<float>::Max();
				int32 Guard = 0;
				while (V.ShiftTimeRemaining > 0.0f && Guard++ < 200)
				{
					V.Tick(In, VehicleDt, Out);
					MinDuring = FMath::Min(MinDuring, Out.ForwardAccelerationMetersPerS2);
				}
				// In der Schaltpause faellt die Beschleunigung deutlich unter den
				// Wert davor (kein Antrieb -> nur Widerstaende).
				TestTrue(FString::Printf(TEXT("Zugkraft-Delle in der Schaltpause (%.2f < %.2f m/s^2)"),
					MinDuring, LastAccel), MinDuring < LastAccel - 1.0f);

				// Nach der Schaltung greift der neue Gang wieder.
				for (int32 k = 0; k < 15; ++k) { V.Tick(In, VehicleDt, Out); }
				TestTrue(FString::Printf(TEXT("Zugkraft erholt sich (%.2f m/s^2)"),
					Out.ForwardAccelerationMetersPerS2), Out.ForwardAccelerationMetersPerS2 > 0.5f);
				bFound = true;
			}
			else
			{
				PrevGear = Out.Gear;
				AccelBefore = Out.ForwardAccelerationMetersPerS2;
			}
		}
		TestTrue(TEXT("Ein Hochschalten trat auf"), bFound);
	}

	// -- A/B: Schaltverluste kosten Zeit, Endgeschwindigkeit bleibt gleich --
	auto RunTo = [](float UpshiftDur, float& OutTimeTo100, float& OutTopKmh)
	{
		FWiesbadenVehiclePhysics V;
		V.Reset();
		V.UpshiftDurationSeconds = UpshiftDur;
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		FWiesbadenVehiclePhysicsOutput Out;
		OutTimeTo100 = -1.0f;
		for (float T = 0.0f; T < 60.0f; T += VehicleDt)
		{
			V.Tick(In, VehicleDt, Out);
			if (OutTimeTo100 < 0.0f && Out.SpeedKmh >= 100.0f) { OutTimeTo100 = T; }
		}
		OutTopKmh = Out.SpeedKmh;
	};

	float T100Shift = 0.0f, TopShift = 0.0f, T100Instant = 0.0f, TopInstant = 0.0f;
	RunTo(0.35f, T100Shift, TopShift);
	RunTo(0.0f, T100Instant, TopInstant);

	TestTrue(TEXT("Beide erreichen 100 km/h"), T100Shift > 0.0f && T100Instant > 0.0f);
	TestTrue(FString::Printf(TEXT("Schalten kostet 0-100-Zeit (%.1f > %.1f s)"), T100Shift, T100Instant),
		T100Shift > T100Instant + 0.3f);
	TestTrue(FString::Printf(TEXT("Endgeschwindigkeit unveraendert (%.0f ~ %.0f km/h)"), TopShift, TopInstant),
		FMath::IsNearlyEqual(TopShift, TopInstant, 1.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleSurfaceGripFromWorldTest,
	"WiesbadenReal.Vehicles.SurfaceGripFromWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Ableitung Belag/Wetter -> SurfaceGripScale: trocken voller Grip, bei Regen
 * spuerbar weniger, monoton fallend, nach unten begrenzt. So erlebt der Spieler
 * den Grip-Effekt aus dem echten Wetter (ohne Dev-Flag).
 */
bool FVehicleSurfaceGripFromWorldTest::RunTest(const FString& Parameters)
{
	const float Dry = AWiesbadenCar::ComputeSurfaceGripScale(0.0f);
	const float Half = AWiesbadenCar::ComputeSurfaceGripScale(0.5f);
	const float Wet = AWiesbadenCar::ComputeSurfaceGripScale(1.0f);

	TestTrue(FString::Printf(TEXT("Trocken = voller Grip (%.2f)"), Dry), FMath::IsNearlyEqual(Dry, 1.0f, 0.001f));
	TestTrue(FString::Printf(TEXT("Regen senkt den Grip spuerbar (%.2f < 1)"), Wet), Wet < 0.85f);
	TestTrue(TEXT("Grip faellt monoton mit der Naesse"), Half < Dry && Wet < Half);
	TestTrue(FString::Printf(TEXT("Grip bleibt fahrbar begrenzt (%.2f >= 0.1)"), Wet), Wet >= 0.1f);

	// Eingaben werden geklemmt (robust gegen ueberzogene Intensitaeten).
	TestTrue(TEXT("Ueberregen bleibt <= 1 und >= 0.1"),
		AWiesbadenCar::ComputeSurfaceGripScale(5.0f) >= 0.1f
		&& AWiesbadenCar::ComputeSurfaceGripScale(-1.0f) <= 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleLoadStiffnessTest,
	"WiesbadenReal.Vehicles.Physics.LoadStiffness",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Lastabhaengige Schraeglaufsteifigkeit: Lastwechsel verschiebt die Kurvenbalance
 * schon im linearen Bereich pedalabhaengig (Bremsen -> Front-Biss, Gas -> Heck
 * laedt). A/B ueber MaxStiffnessLoadShift; stationaere Kurve und Geradeaus
 * bleiben unveraendert, und das Modell bleibt stabil (kein Aufschwingen).
 */
bool FVehicleLoadStiffnessTest::RunTest(const FString& Parameters)
{
	// Manoever: Tempo aufbauen, moderat einlenken, dann Pedal - Gierrate messen.
	auto Run = [](float Shift, float AccelSeconds, float Throttle, float Brake,
		int32 Steps, float& OutYaw, float& OutMaxAbs, bool& OutFinite)
	{
		FWiesbadenVehiclePhysics V;
		V.Reset();
		V.MaxStiffnessLoadShift = Shift;
		FWiesbadenVehiclePhysicsInput In;
		In.Throttle = 1.0f;
		Simulate(V, In, AccelSeconds);
		// Sanft einlenken: die Gierrate bleibt UNTER dem Seitenkraftlimit
		// (MaxYaw), sonst maskiert die Klemmung die Steifigkeits-Wirkung.
		// 0,25 lag nach 6 s Vollgas (~70 km/h) schon AM Limit: die Bremsprobe war
		// nur gruen, weil die Raeder dort blockierten und die Gierdaempfung die
		// Rate unter die Klemme zog. Mit ABS blockiert nichts mehr - 0,12 misst
		// jetzt wirklich den linearen Bereich (29.09.2026).
		In.Steering = 0.12f;
		Simulate(V, In, 0.5f);
		In.Throttle = Throttle;
		In.Brake = Brake;
		FWiesbadenVehiclePhysicsOutput Out;
		float MaxAbs = 0.0f;
		for (int32 i = 0; i < Steps; ++i)
		{
			V.Tick(In, VehicleDt, Out);
			MaxAbs = FMath::Max(MaxAbs, FMath::Abs(Out.YawRateRadPerS));
		}
		OutYaw = Out.YawRateRadPerS;
		OutMaxAbs = MaxAbs;
		OutFinite = FMath::IsFinite(Out.YawRateRadPerS);
	};

	float Y = 0.0f, Mx = 0.0f; bool Fin = false;
	float Y0 = 0.0f;

	// -- Bremsen in der Kurve: mit vs. ohne Lastkopplung messbar verschieden --
	Run(0.2f, 6.0f, 0.0f, 0.3f, 40, Y, Mx, Fin);
	Run(0.0f, 6.0f, 0.0f, 0.3f, 40, Y0, Mx, Fin);
	const float BrakeDiff = Y - Y0;
	TestTrue(FString::Printf(TEXT("Bremsen verschiebt die Balance (%.4f)"), BrakeDiff),
		FMath::Abs(BrakeDiff) > 0.002f);

	// -- Gas in der Kurve: verschiebt in die GEGENrichtung (Heck laedt) -------
	float Yt = 0.0f, Yt0 = 0.0f;
	Run(0.2f, 6.0f, 1.0f, 0.0f, 40, Yt, Mx, Fin);
	Run(0.0f, 6.0f, 1.0f, 0.0f, 40, Yt0, Mx, Fin);
	const float ThrDiff = Yt - Yt0;
	TestTrue(FString::Printf(TEXT("Gas verschiebt die Balance (%.4f)"), ThrDiff),
		FMath::Abs(ThrDiff) > 0.002f);
	TestTrue(FString::Printf(TEXT("Pedalabhaengig: Bremsen vs Gas entgegengesetzt (%.4f / %.4f)"),
		BrakeDiff, ThrDiff), BrakeDiff * ThrDiff < 0.0f);

	// -- Stationaere Kurve (a_x~0 bei Hoechsttempo): UNVERAENDERT ------------
	float Ys = 0.0f, Ys0 = 0.0f;
	Run(0.2f, 14.0f, 1.0f, 0.0f, 100, Ys, Mx, Fin);
	Run(0.0f, 14.0f, 1.0f, 0.0f, 100, Ys0, Mx, Fin);
	TestTrue(FString::Printf(TEXT("Stationaere Kurve unveraendert (%.4f ~ %.4f)"), Ys, Ys0),
		FMath::IsNearlyEqual(Ys, Ys0, 0.01f));

	// -- Geradeaus: unveraendert (keine Seitenkraft, egal welche Steifigkeit) -
	{
		FWiesbadenVehiclePhysics A; A.Reset(); A.MaxStiffnessLoadShift = 0.2f;
		FWiesbadenVehiclePhysics B; B.Reset(); B.MaxStiffnessLoadShift = 0.0f;
		FWiesbadenVehiclePhysicsInput In; In.Throttle = 1.0f;
		FWiesbadenVehiclePhysicsOutput OA, OB;
		SimulateTo(A, In, 8.0f, OA);
		SimulateTo(B, In, 8.0f, OB);
		TestTrue(TEXT("Geradeaus unveraendert (Tempo)"), FMath::IsNearlyEqual(OA.SpeedKmh, OB.SpeedKmh, 0.01f));
	}

	// -- STABILITAET: hartes Bremsen in der Kurve schwingt nicht auf ---------
	Run(0.2f, 8.0f, 0.0f, 1.0f, 150, Y, Mx, Fin);
	TestTrue(TEXT("Gierrate bleibt endlich"), Fin);
	TestTrue(FString::Printf(TEXT("Kein Aufschwingen (max |Gier| %.2f rad/s)"), Mx), Mx < 3.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleWheelSpinFlareTest,
	"WiesbadenReal.Vehicles.Physics.WheelSpinFlare",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Drehzahlflare bei Radspin: beim Durchdrehen entkoppeln die Antriebsraeder von
 * der Strasse, der unbelastete Motor dreht hoch - die ANGEZEIGTE/gehoerte
 * Drehzahl flart ueber die geschwindigkeitsabgeleitete Basis, waehrend der
 * Antrieb (Schalten/Drehmoment/Tempo) unberuehrt bleibt.
 */
bool FVehicleWheelSpinFlareTest::RunTest(const FString& Parameters)
{
	using Phys = FWiesbadenVehiclePhysics;

	// -- Datenreine Kennlinie ------------------------------------------------
	{
		constexpr float Max = 2500.0f, Rise = 9000.0f, Decay = 5000.0f, Dt = 0.01f;

		// Spin: der Flare steigt (ein Schritt = Rise*Dt).
		const float Up1 = Phys::AdvanceWheelSpinFlare(true, 1.0f, 0.0f, Max, Rise, Decay, Dt);
		TestTrue(TEXT("Spin -> Flare steigt"), Up1 > 0.0f);
		TestTrue(TEXT("Anstieg = Rate*Dt"), FMath::IsNearlyEqual(Up1, Rise * Dt, 1e-2f));

		// Er klemmt am Ziel (Max*Throttle), nicht darueber.
		float F = 0.0f;
		for (int32 i = 0; i < 200; ++i) { F = Phys::AdvanceWheelSpinFlare(true, 1.0f, F, Max, Rise, Decay, Dt); }
		TestTrue(TEXT("Flare klemmt am Ziel"), FMath::IsNearlyEqual(F, Max, 1.0f));

		// Ohne Spin faellt er auf 0 und klemmt dort (nicht negativ).
		float D = Max;
		for (int32 i = 0; i < 400; ++i) { D = Phys::AdvanceWheelSpinFlare(false, 0.0f, D, Max, Rise, Decay, Dt); }
		TestTrue(TEXT("kein Spin -> Flare faellt auf 0"), D == 0.0f);

		// Gaspedal skaliert das Ziel (halbes Gas -> halber Flare).
		float H = 0.0f;
		for (int32 i = 0; i < 200; ++i) { H = Phys::AdvanceWheelSpinFlare(true, 0.5f, H, Max, Rise, Decay, Dt); }
		TestTrue(TEXT("halbes Gas -> halbes Ziel"), FMath::IsNearlyEqual(H, Max * 0.5f, 1.0f));

		// Ausbrechen schneller als Beruhigen (Rise > Decay).
		const float StepUp = Phys::AdvanceWheelSpinFlare(true, 1.0f, 1000.0f, Max, Rise, Decay, Dt) - 1000.0f;
		const float StepDn = 1000.0f - Phys::AdvanceWheelSpinFlare(false, 0.0f, 1000.0f, Max, Rise, Decay, Dt);
		TestTrue(TEXT("Ausbrechen schneller als Beruhigen"), StepUp > StepDn);
	}

	// -- Im Fahrzeug: Vollgas-Start flart die AUSGABE ueber die Basis --------
	// (auf nasser Strasse - trocken dreht der kalibrierte Kaefer nicht durch)
	{
		FWiesbadenVehiclePhysics Vehicle; Vehicle.Reset();
		FWiesbadenVehiclePhysicsInput In; In.Throttle = 1.0f; In.SurfaceGripScale = 0.65f;
		FWiesbadenVehiclePhysicsOutput Out;

		bool bFlared = false;
		for (int32 Step = 0; Step < 60; ++Step)   // ~0,6 s Anfahren
		{
			Vehicle.Tick(In, VehicleDt, Out);
			if (Out.bWheelSpin)
			{
				// Ausgabe-Drehzahl liegt beim Spin ueber der internen Basis-Drehzahl.
				bFlared = bFlared || (Out.EngineRpm > Vehicle.EngineRpm + 50.0f);
			}
		}
		TestTrue(TEXT("Radspin flart die Ausgabe-Drehzahl ueber die Basis"), bFlared);
		TestTrue(TEXT("Flare-Zustand aktiv"), Vehicle.WheelSpinFlare > 0.0f);
	}

	// -- Marschfahrt (kein Spin): Ausgabe == Basis, kein Flare ---------------
	{
		FWiesbadenVehiclePhysics Vehicle; Vehicle.Reset();
		FWiesbadenVehiclePhysicsInput In; In.Throttle = 1.0f;
		Simulate(Vehicle, In, 12.0f);   // auf Tempo, kein Radspin mehr
		FWiesbadenVehiclePhysicsOutput Out;
		Vehicle.Tick(In, VehicleDt, Out);
		TestFalse(TEXT("Bei Fahrt kein Radspin"), Out.bWheelSpin);
		TestTrue(TEXT("Flare abgeklungen"), Vehicle.WheelSpinFlare < 1.0f);
		TestTrue(TEXT("Ausgabe-Drehzahl = Basis (kein Flare)"),
			FMath::IsNearlyEqual(Out.EngineRpm, Vehicle.EngineRpm, 1.0f));
	}

	// -- Der Flare veraendert die FAHRT NICHT (reine Anzeige) ----------------
	// Zwei identische Laeufe, einer mit Flare (Standard), einer mit MaxFlare = 0.
	// Tempo UND Gang nach 12 s Vollgas muessen exakt gleich sein.
	{
		FWiesbadenVehiclePhysics A; A.Reset();
		FWiesbadenVehiclePhysics B; B.Reset(); B.MaxWheelSpinFlareRpm = 0.0f;
		FWiesbadenVehiclePhysicsInput In; In.Throttle = 1.0f;
		FWiesbadenVehiclePhysicsOutput OutA, OutB;
		SimulateTo(A, In, 12.0f, OutA);
		SimulateTo(B, In, 12.0f, OutB);
		TestTrue(TEXT("Flare aendert Tempo nicht"),
			FMath::IsNearlyEqual(OutA.SpeedKmh, OutB.SpeedKmh, 0.01f));
		TestEqual(TEXT("Flare aendert Gang nicht"), OutA.Gear, OutB.Gear);
	}

	return true;
}
