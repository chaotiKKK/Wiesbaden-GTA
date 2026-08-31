// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenVehiclePhysics.h"

#include "WiesbadenReal.h"

namespace
{
	constexpr float AirDensityKgM3 = 1.2f;
}

void FWiesbadenVehiclePhysics::Reset()
{
	SpeedMetersPerS = 0.0f;
	Gear = 1;
	EngineRpm = EngineIdleRpm;
}

float FWiesbadenVehiclePhysics::GetTotalGearRatio() const
{
	if (Gear <= 0)
	{
		// Rueckwaertsgang: feste, niedrige Uebersetzung.
		return 3.9f * FinalDriveRatio;
	}
	const int32 Index = FMath::Clamp(Gear - 1, 0, FMath::Max(0, GearRatios.Num() - 1));
	return GearRatios[Index] * FinalDriveRatio;
}

float FWiesbadenVehiclePhysics::RpmFromSpeed(float Speed) const
{
	// v (m/s) -> Radwinkelgeschwindigkeit -> Motordrehzahl ueber die Uebersetzung.
	const float WheelOmega = Speed / FMath::Max(WheelRadiusM, 0.01f);
	return WheelOmega * GetTotalGearRatio() * (60.0f / (2.0f * PI));
}

float FWiesbadenVehiclePhysics::MotorTorqueAt(float Rpm) const
{
	// Drehmoment aus der Leistung am Leistungsgipfel: T_max = P / omega.
	const float OmegaAtPeak = EngineMaxPowerRpm * (2.0f * PI / 60.0f);
	const float MaxTorque = (EngineMaxPowerKw * 1000.0f) / FMath::Max(OmegaAtPeak, 0.1f);

	// Normierte Kurve: 0.55 am Leerlauf, 1.0 am Leistungsgipfel, 0.55 an der
	// Drehzahlgrenze - dazwischen linear (ausreichend fuer ein Spielmodell).
	float Factor = 1.0f;
	if (Rpm <= EngineIdleRpm)
	{
		Factor = 0.55f;
	}
	else if (Rpm < EngineMaxPowerRpm)
	{
		Factor = FMath::Lerp(0.55f, 1.0f,
			(Rpm - EngineIdleRpm) / FMath::Max(EngineMaxPowerRpm - EngineIdleRpm, 1.0f));
	}
	else if (Rpm > EngineMaxPowerRpm)
	{
		Factor = FMath::Lerp(1.0f, 0.55f,
			(Rpm - EngineMaxPowerRpm) / FMath::Max(EngineMaxRpm - EngineMaxPowerRpm, 1.0f));
	}

	return MaxTorque * Factor;
}

void FWiesbadenVehiclePhysics::ShiftGear(const FWiesbadenVehiclePhysicsInput& Input, float DeltaSeconds)
{
	// Rueckwaerts: nur aus dem (fast) Stillstand, umgekehrt zurueck in Gang 1.
	if (Input.bReverseRequested)
	{
		if (FMath::Abs(SpeedMetersPerS) < 0.5f)
		{
			Gear = -1;
		}
		return;
	}
	if (Gear < 0)
	{
		if (FMath::Abs(SpeedMetersPerS) < 0.5f)
		{
			Gear = 1;
		}
		return;
	}

	// Automatik: Hoch-/Runterschalten ueber Drehzahlschwellen.
	const int32 LastGear = FMath::Max(1, GearRatios.Num());
	if (EngineRpm > ShiftUpRpm && Gear < LastGear)
	{
		Gear += 1;
	}
	else if (EngineRpm < ShiftDownRpm && Gear > 1)
	{
		Gear -= 1;
	}
}

float FWiesbadenVehiclePhysics::GetDriveForce(float Throttle) const
{
	const float Torque = MotorTorqueAt(EngineRpm);
	const float WheelForce = Torque * GetTotalGearRatio() / FMath::Max(WheelRadiusM, 0.01f);

	// Traktionslimit: Die Reifen koennen nicht mehr Kraft uebertragen als
	// mu * Gewicht - jenseits davon drehen die Raeder durch (vereinfacht:
	// Kraft wird begrenzt statt Schlupf zu modellieren).
	const float MaxTractiveForce = MuTraction * MassKg * GravityMetersPerS2;
	return FMath::Clamp(WheelForce, -MaxTractiveForce, MaxTractiveForce) * FMath::Clamp(Throttle, 0.0f, 1.0f);
}

float FWiesbadenVehiclePhysics::ComputeUsableSteerAngleDeg(
	float MaxSteerAngleDeg, float SpeedMetersPerS, float FalloffSpeedMetersPerS)
{
	const float Falloff = FMath::Max(FalloffSpeedMetersPerS, 1.0f);
	const float Speed = FMath::Max(FMath::Abs(SpeedMetersPerS), 0.0f);

	// Einfache Hyperbel: bei Stillstand voller Anschlag, bei Falloff die
	// Haelfte, danach weiter abnehmend. Kein Sprung, keine harte Grenze.
	return MaxSteerAngleDeg / (1.0f + Speed / Falloff);
}

float FWiesbadenVehiclePhysics::AdvanceSteerAngle(
	float CurrentNorm, float TargetNorm, float Rate, float ReturnRate, float DeltaSeconds)
{
	TargetNorm = FMath::Clamp(TargetNorm, -1.0f, 1.0f);

	// Zurueckstellen heisst: Bewegung in Richtung Geradeausstellung. Das ist
	// der Fall, wenn der Zielbetrag kleiner ist oder das Vorzeichen wechselt.
	//
	// Die Pruefung muss ausdruecklich ausschliessen, dass die Lenkung gerade
	// steht: FMath::Sign(0) ist 0 und weicht damit von JEDEM Ziel ab. Ohne
	// diese Bedingung galt das Einlenken aus der Geradeausstellung als
	// Rueckstellung und lief mit der schnelleren Rate - also genau der
	// Sprunghaftigkeit, die hier abgestellt werden soll.
	const bool bReturning = CurrentNorm != 0.0f
		&& (FMath::Abs(TargetNorm) < FMath::Abs(CurrentNorm) || TargetNorm * CurrentNorm < 0.0f);

	const float Step = FMath::Max(bReturning ? ReturnRate : Rate, 0.01f) * FMath::Max(DeltaSeconds, 0.0f);
	return FMath::Clamp(FMath::FInterpConstantTo(CurrentNorm, TargetNorm, 1.0f, Step), -1.0f, 1.0f);
}

float FWiesbadenVehiclePhysics::ComputeAvailableLateralAccel(
	float MuTraction, float GravityMetersPerS2, float LongitudinalAccelMetersPerS2)
{
	const float Budget = FMath::Max(MuTraction * GravityMetersPerS2, 0.01f);
	const float UsedFraction = FMath::Clamp(FMath::Abs(LongitudinalAccelMetersPerS2) / Budget, 0.0f, 1.0f);

	// Reibungskreis: laengs und quer teilen sich ein Kraftbudget.
	return Budget * FMath::Sqrt(FMath::Max(0.0f, 1.0f - UsedFraction * UsedFraction));
}

float FWiesbadenVehiclePhysics::ComputeYawRate(
	float SteeringInput, float LongitudinalAccelMetersPerS2) const
{
	const float Speed = FMath::Abs(SpeedMetersPerS);
	if (Speed < 0.5f)
	{
		// Im (fast) Stillstand lenken die Raeder, aber das Fahrzeug dreht nicht.
		return 0.0f;
	}

	// Lenkeinschlag nimmt mit der Geschwindigkeit ab.
	const float UsableSteerDeg = ComputeUsableSteerAngleDeg(
		MaxSteerAngleDeg, Speed, SteerFalloffSpeedMetersPerS);

	const float SteeringRad = FMath::DegreesToRadians(
		FMath::Clamp(SteeringInput, -1.0f, 1.0f) * UsableSteerDeg);
	const float KinematicYaw = (SpeedMetersPerS / FMath::Max(WheelbaseM, 0.01f)) * FMath::Tan(SteeringRad);

	// Seitenkraftlimit aus dem Reibungskreis: Wer bremst oder beschleunigt,
	// hat weniger Querkraft uebrig. Bei hoher Geschwindigkeit untersteuert das
	// Fahrzeug zusaetzlich, weil die zulaessige Gierrate mit 1/v faellt.
	const float AvailableLateral = ComputeAvailableLateralAccel(
		MuTraction, GravityMetersPerS2, LongitudinalAccelMetersPerS2);
	const float MaxLateralYaw = AvailableLateral / Speed;
	return FMath::Clamp(KinematicYaw, -MaxLateralYaw, MaxLateralYaw);
}

void FWiesbadenVehiclePhysics::Tick(
	const FWiesbadenVehiclePhysicsInput& Input,
	float DeltaSeconds,
	FWiesbadenVehiclePhysicsOutput& Out)
{
	DeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, 0.5f);

	ShiftGear(Input, DeltaSeconds);

	const bool bReverse = (Gear < 0);
	const float Throttle = FMath::Clamp(Input.Throttle, 0.0f, 1.0f);
	const float Brake = FMath::Clamp(Input.Brake, 0.0f, 1.0f);

	// Drehzahl: aus der Geschwindigkeit; im Stand haelt der Leerlauf die
	// Drehzahl, Gas im Stand hebt sie leicht an.
	if (FMath::Abs(SpeedMetersPerS) < 0.05f)
	{
		EngineRpm = EngineIdleRpm * (1.0f + 0.3f * Throttle);
	}
	else
	{
		EngineRpm = RpmFromSpeed(SpeedMetersPerS);
	}
	EngineRpm = FMath::Clamp(EngineRpm, EngineIdleRpm * 0.5f, EngineMaxRpm * 1.05f);

	// Antriebskraft: vorwaerts positiv, rueckwaerts negativ.
	float DriveForce = 0.0f;
	if (bReverse)
	{
		DriveForce = -GetDriveForce(Throttle);
	}
	else
	{
		DriveForce = GetDriveForce(Throttle);
	}

	// Widerstaende wirken gegen die Bewegungsrichtung.
	const float Speed = SpeedMetersPerS;
	const float RollResistance = (FMath::Abs(Speed) > 0.1f)
		? RollCoeff * MassKg * GravityMetersPerS2 * FMath::Sign(Speed)
		: 0.0f;
	const float AirResistance = AirDensityKgM3 * 0.5f * DragCoeffAreaM2 * Speed * FMath::Abs(Speed);
	const float BrakeForce = (Brake * BrakeForceN + (Input.bHandbrake ? BrakeForceN * 0.6f : 0.0f))
		* FMath::Sign(Speed);

	// Motorbremse im Schub: geschlossene Drosselklappe, Gang eingelegt.
	//
	// Ohne diesen Anteil rollt das Fahrzeug beim Gaswegnehmen nur gegen Roll-
	// und Luftwiderstand aus und fuehlt sich an, als waere es im Leerlauf.
	// Das Moment wirkt ueber die Gesamtuebersetzung - im kleinen Gang deutlich
	// spuerbar, im grossen kaum, genau wie beim echten Fahrzeug.
	float EngineBrakeForce = 0.0f;
	if (Throttle < 0.05f && FMath::Abs(Speed) > 0.5f && Gear != 0)
	{
		const float RpmFraction = FMath::Clamp(EngineRpm / FMath::Max(EngineMaxRpm, 1.0f), 0.0f, 1.2f);
		const float TorqueNm = EngineBrakeTorqueNm * RpmFraction;
		EngineBrakeForce = TorqueNm * FMath::Abs(GetTotalGearRatio())
			/ FMath::Max(WheelRadiusM, 0.01f) * FMath::Sign(Speed);
	}

	// Nettokraft entlang der Fahrtrichtung; die Bremse bremst auf 0 ab.
	float NetForce = DriveForce - RollResistance - AirResistance - BrakeForce - EngineBrakeForce;

	// Haften: Im Stillstand ohne Zugkraft bleibt das Fahrzeug stehen.
	if (FMath::Abs(Speed) < 0.1f && FMath::Abs(NetForce) < RollCoeff * MassKg * GravityMetersPerS2 * 0.5f)
	{
		NetForce = 0.0f;
		SpeedMetersPerS = 0.0f;
	}

	const float Acceleration = NetForce / FMath::Max(MassKg, 1.0f);
	SpeedMetersPerS += Acceleration * DeltaSeconds;

	// Vorzeichenwechsel ohne Antriebskraft (Bremsen/Rollen bis zum Stillstand):
	// stehen bleiben statt rueckwaerts kriechen. Mit Antrieb (z. B. Gas im
	// Rueckwaertsgang) darf der Wechsel stattfinden.
	if (FMath::Sign(SpeedMetersPerS) != FMath::Sign(Speed)
		&& FMath::Abs(DriveForce) < 1.0f)
	{
		SpeedMetersPerS = 0.0f;
	}

	// Rueckwaertsgeschwindigkeit begrenzen.
	if (bReverse)
	{
		SpeedMetersPerS = FMath::Max(SpeedMetersPerS, -ReverseMaxSpeedMetersPerS);
	}

	// Drehzahl nach der Geschwindigkeitsaenderung aktualisieren (nur in Fahrt).
	if (FMath::Abs(SpeedMetersPerS) > 0.05f)
	{
		EngineRpm = FMath::Clamp(RpmFromSpeed(SpeedMetersPerS), EngineIdleRpm * 0.5f, EngineMaxRpm * 1.05f);
	}

	// Ausgabe fuellen.
	Out.ForwardSpeedMetersPerS = SpeedMetersPerS;
	Out.SpeedKmh = FMath::Abs(SpeedMetersPerS) * 3.6f;
	Out.EngineRpm = EngineRpm;
	Out.Gear = Gear;
	// Lenkeinschlag mit begrenzter Geschwindigkeit nachfuehren, dann erst die
	// Gierrate daraus bilden. Die rohe Eingabe darf nie direkt ins Giermodell -
	// sonst dreht das Fahrzeug in einem Bild auf Volleinschlag ein.
	SteerAngleNorm = AdvanceSteerAngle(
		SteerAngleNorm, Input.Steering, SteerRatePerSecond, SteerReturnRatePerSecond, DeltaSeconds);

	Out.YawRateRadPerS = ComputeYawRate(SteerAngleNorm, Acceleration);
	Out.SteerAngleNorm = SteerAngleNorm;
	Out.ForwardAccelerationMetersPerS2 = Acceleration;
}
