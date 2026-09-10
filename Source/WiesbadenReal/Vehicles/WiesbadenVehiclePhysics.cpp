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
	EngineRpm = Powertrain.IdleRpm;
	FuelLiters = TankCapacityLiters;
	LateralVelocityMetersPerS = 0.0f;
	YawRateRadPerS = 0.0f;
	SteerAngleNorm = 0.0f;
}

float FWiesbadenVehiclePhysics::GetTotalGearRatio() const
{
	if (Gear <= 0)
	{
		// Rueckwaertsgang: Uebersetzung aus der geteilten Spec.
		return Powertrain.ReverseGearRatio * Powertrain.FinalDriveRatio;
	}
	const int32 Count = Powertrain.ForwardGearRatios.Num();
	const int32 Index = FMath::Clamp(Gear - 1, 0, FMath::Max(0, Count - 1));
	return Powertrain.ForwardGearRatios[Index] * Powertrain.FinalDriveRatio;
}

float FWiesbadenVehiclePhysics::RpmFromSpeed(float Speed) const
{
	// v (m/s) -> Radwinkelgeschwindigkeit -> Motordrehzahl ueber die Uebersetzung.
	const float WheelOmega = Speed / FMath::Max(WheelRadiusM, 0.01f);
	return WheelOmega * GetTotalGearRatio() * (60.0f / (2.0f * PI));
}

float FWiesbadenVehiclePhysics::MotorTorqueAt(float Rpm) const
{
	// Motor auf die echte, geteilte Drehmomentkurve vereinheitlicht - kein
	// aus der Leistung abgeleitetes Ersatzmodell mehr.
	return Powertrain.TorqueNmAt(Rpm);
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
	const int32 LastGear = FMath::Max(1, Powertrain.ForwardGearRatios.Num());
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
	const float MaxTractiveForce = MuTraction * Powertrain.MassKg * GravityMetersPerS2;
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
		EngineRpm = Powertrain.IdleRpm * (1.0f + 0.3f * Throttle);
	}
	else
	{
		EngineRpm = RpmFromSpeed(SpeedMetersPerS);
	}
	EngineRpm = FMath::Clamp(EngineRpm, Powertrain.IdleRpm * 0.5f, Powertrain.MaxRpm * 1.05f);

	// Antriebskraft: vorwaerts positiv, rueckwaerts negativ.
	// Ohne Treibstoff liefert der Motor keine Kraft - der Wagen rollt nur
	// noch aus (Roll-/Luftwiderstand), Bremse und Lenkung bleiben wirksam.
	float DriveForce = 0.0f;
	if (HasFuel())
	{
		if (bReverse)
		{
			DriveForce = -GetDriveForce(Throttle);
		}
		else
		{
			DriveForce = GetDriveForce(Throttle);
		}
	}

	// Widerstaende wirken gegen die Bewegungsrichtung.
	const float Speed = SpeedMetersPerS;
	const float RollResistance = (FMath::Abs(Speed) > 0.1f)
		? RollCoeff * Powertrain.MassKg * GravityMetersPerS2 * FMath::Sign(Speed)
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
		const float RpmFraction = FMath::Clamp(EngineRpm / FMath::Max(Powertrain.MaxRpm, 1.0f), 0.0f, 1.2f);
		const float TorqueNm = EngineBrakeTorqueNm * RpmFraction;
		EngineBrakeForce = TorqueNm * FMath::Abs(GetTotalGearRatio())
			/ FMath::Max(WheelRadiusM, 0.01f) * FMath::Sign(Speed);
	}

	// Nettokraft entlang der Fahrtrichtung; die Bremse bremst auf 0 ab.
	float NetForce = DriveForce - RollResistance - AirResistance - BrakeForce - EngineBrakeForce;

	// Haften: Im Stillstand ohne Zugkraft bleibt das Fahrzeug stehen.
	if (FMath::Abs(Speed) < 0.1f && FMath::Abs(NetForce) < RollCoeff * Powertrain.MassKg * GravityMetersPerS2 * 0.5f)
	{
		NetForce = 0.0f;
		SpeedMetersPerS = 0.0f;
	}

	const float Acceleration = NetForce / FMath::Max(Powertrain.MassKg, 1.0f);
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
		EngineRpm = FMath::Clamp(RpmFromSpeed(SpeedMetersPerS), Powertrain.IdleRpm * 0.5f, Powertrain.MaxRpm * 1.05f);
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

	// -- Querdynamik: DYNAMISCHES Einspurmodell statt kinematisch -------------
	//
	// Bisher fuhr der Wagen exakt in Blickrichtung (kinematisches Bicycle-Modell,
	// yaw = v/L*tan(delta)) - kein Schlupf, kein Drift: "auf Schienen". Jetzt
	// rechnet ein dynamisches Einspurmodell die Reifen-Seitenkraefte aus den
	// Schraeglaufwinkeln, integriert Quergeschwindigkeit (Vy) UND Gierrate (r)
	// und laesst den hecklastigen Kaefer quer rutschen, unter- und (unter Last)
	// leicht uebersteuern. Ergebnis: der Wagen bewegt sich entlang (Vx, Vy),
	// die Karosserie zeigt einen Schwimmwinkel - genau das "reale" Fahrgefuehl.
	const float Vx = SpeedMetersPerS;
	if (Vx > LowSpeedBlendMetersPerS)
	{
	const float L = FMath::Max(WheelbaseM, 0.5f);
	const float aFront = L * (1.0f - FrontWeightFraction);   // CG -> Vorderachse
	const float bRear = L * FrontWeightFraction;             // CG -> Hinterachse
	const float m = FMath::Max(Powertrain.MassKg, 1.0f);
	const float Iz = FMath::Max(YawInertiaKgM2, 1.0f);

	// Kurzschluss-Lenkrate bei vollem Anschlag und niedrigem Tempo dampfen:
	// ohne diesen Schritt würde ein aufgedrücktes Lenkrad in ein bis zwei Bildern
	// 90° drehen und das Fahrzeug sofort ins Trudeln bringen. Der Kaefer lenkt
	// ohne Servounterstuetzung, also mit handlichem Aufwand - schneller als 1,2
	// rad/s fühlt sich nach nichts an, was in reellen Rädern steht.
	YawRateRadPerS = FMath::Min(YawRateRadPerS, 1.2f);
	YawRateRadPerS = FMath::Max(YawRateRadPerS, -1.2f);

		const float UsableSteerDeg = ComputeUsableSteerAngleDeg(
			MaxSteerAngleDeg, Vx, SteerFalloffSpeedMetersPerS);
		const float Delta = FMath::DegreesToRadians(SteerAngleNorm * UsableSteerDeg);

		const float Vy = LateralVelocityMetersPerS;
		const float r = YawRateRadPerS;

		// Schraeglaufwinkel vorn/hinten (klein-Winkel ueber atan2 stabil).
		const float AlphaF = FMath::Atan2(Vy + aFront * r, Vx) - Delta;
		const float AlphaR = FMath::Atan2(Vy - bRear * r, Vx);

		// Reibungskreis: die LAENGSkraft (Antrieb/Bremse) verbraucht Grip, der
		// dann quer fehlt. Der verbleibende Queranteil ist Wurzel(1 - (a_x/mu g)^2)
		// - dieselbe Kopplung wie ComputeAvailableLateralAccel. Ohne sie liesse
		// sich unter Vollbremsung genauso scharf einlenken wie ohne (Schienen).
		const float LatFraction = ComputeAvailableLateralAccel(
			MuTraction, GravityMetersPerS2, Acceleration)
			/ FMath::Max(MuTraction * GravityMetersPerS2, 0.01f);

		// Reifen-Seitenkraefte, linear, im Reibungskreis je Achse gesaettigt.
		const float FrontLoad = m * GravityMetersPerS2 * FrontWeightFraction;
		const float RearLoad = m * GravityMetersPerS2 * (1.0f - FrontWeightFraction);
		const float FyfMax = MuTraction * FrontLoad * LatFraction;
		const float FyrMax = MuTraction * RearLoad * LatFraction;
		const float Fyf = FMath::Clamp(-CorneringStiffnessFrontNPerRad * AlphaF, -FyfMax, FyfMax);
		const float Fyr = FMath::Clamp(-CorneringStiffnessRearNPerRad * AlphaR, -FyrMax, FyrMax);

		// Bewegungsgleichungen (Zentripetalterm -Vx*r).
		const float dVy = (Fyf + Fyr) / m - Vx * r;
		const float dr = (aFront * Fyf - bRear * Fyr) / Iz;

		LateralVelocityMetersPerS = Vy + dVy * DeltaSeconds;
		YawRateRadPerS = r + dr * DeltaSeconds;

		// Sicherung gegen Ausbrechen: Schwimmwinkel und Gierrate begrenzen. Die
		// stationaere Gierrate ergibt sich physikalisch aus a_lat = Vx*r <= mu*g;
		// die Klemmung deckelt nur transiente UEberschwinger auf dieses Limit.
		LateralVelocityMetersPerS =
			FMath::Clamp(LateralVelocityMetersPerS, -0.7f * Vx - 1.0f, 0.7f * Vx + 1.0f);
		const float MaxYaw = MuTraction * GravityMetersPerS2 / FMath::Max(Vx, 1.0f);
		YawRateRadPerS = FMath::Clamp(YawRateRadPerS, -MaxYaw, MaxYaw);
	}
	else
	{
		// Langsam/Stand/Rueckwaerts: kinematisch (dynamisches Modell singulaer bei
		// v->0). Querschlupf sanft abbauen, damit kein Rest-Drift haengen bleibt.
		YawRateRadPerS = ComputeYawRate(SteerAngleNorm, Acceleration);
		LateralVelocityMetersPerS =
			FMath::FInterpTo(LateralVelocityMetersPerS, 0.0f, DeltaSeconds, 5.0f);
	}

	Out.YawRateRadPerS = YawRateRadPerS;
	Out.LateralVelocityMetersPerS = LateralVelocityMetersPerS;
	Out.SlipAngleDeg = (FMath::Abs(Vx) > 0.5f)
		? FMath::RadiansToDegrees(FMath::Atan2(LateralVelocityMetersPerS, FMath::Abs(Vx)))
		: 0.0f;
	Out.SteerAngleNorm = SteerAngleNorm;
	Out.ForwardAccelerationMetersPerS2 = Acceleration;

	// Verbrauch: Arbeit aus der Antriebskraft (P = F * v) plus Grundverbrauch
	// des laufenden Motors. Energiegehalt Benzin: ~8,9 kWh/l.
	if (HasFuel())
	{
		const float WheelPowerKw = FMath::Abs(DriveForce * SpeedMetersPerS) / 1000.0f;
		const float IdleLiters = IdleConsumptionLitersPerHour * (DeltaSeconds / 3600.0f);
		const float DriveLiters = (WheelPowerKw * (DeltaSeconds / 3600.0f)) * ConsumptionLitersPerKWh;
		FuelLiters = FMath::Max(0.0f, FuelLiters - IdleLiters - DriveLiters);
	}
}
