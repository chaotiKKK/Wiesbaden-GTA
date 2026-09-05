// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

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

	// Vollgas aus dem Stand; Zeit bis 100 km/h messen.
	float TimeTo100 = -1.0f;
	for (float T = 0.0f; T < 60.0f; T += VehicleDt)
	{
		Vehicle.Tick(In, VehicleDt, Out);
		if (TimeTo100 < 0.0f && Out.SpeedKmh >= 100.0f)
		{
			TimeTo100 = T;
		}
	}

	// Das kinematische Modell ist BEWUSST idealisiert (kein Schlupf, keine
	// Schaltzeit, keine Antriebsstrangverluste) - "berechenbar und stabil". Mit
	// der jetzt geteilten, echten Drehmomentkurve (102 Nm @ 2600) faehrt es 0-100
	// in gut 13 s. Die realen ~23 s eines 44-PS-Kaefers entstehen erst in der
	// verlustmodellierenden Chaos-Physik (AWiesbadenChaosCar::TickSelfTest) - das
	// gehoert dorthin, nicht in dieses reine Modell. Hier wird deshalb nur ein
	// plausibler Rahmen geprueft, nicht die exakte Werksangabe.
	TestTrue(TEXT("0-100 km/h erreicht"), TimeTo100 > 0.0f);
	TestTrue(TEXT("0-100 zuegig, aber nicht sportwagenhaft (8..20 s)"),
		TimeTo100 > 8.0f && TimeTo100 < 20.0f);
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
