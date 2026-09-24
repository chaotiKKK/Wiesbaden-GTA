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
	Fuel.Reset();
	LateralVelocityMetersPerS = 0.0f;
	YawRateRadPerS = 0.0f;
	SteerAngleNorm = 0.0f;
	LastLongAccelMetersPerS2 = 0.0f;
	BrakeAbsPhaseRad = 0.0f;
	bDriveSlipState = false;
	bBrakeLockState = false;
	SurfaceGripScale = 1.0f;
	ShiftTimeRemaining = 0.0f;
	WheelSpinFlare = 0.0f;
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

float FWiesbadenVehiclePhysics::GetGearDirection() const
{
	// Gleiche Bedingung wie bReverse im Tick: nur der Rueckwaertsgang dreht um.
	return (Gear < 0) ? -1.0f : 1.0f;
}

float FWiesbadenVehiclePhysics::RpmFromSpeed(float Speed) const
{
	// v (m/s) -> Radwinkelgeschwindigkeit -> Motordrehzahl ueber die Uebersetzung.
	// Die Gangrichtung bildet die Fahrtrichtung auf die Motor-Drehrichtung ab:
	// rueckwaerts (v < 0, Richtung -1) dreht der Motor vorwaerts. Ohne sie wurde
	// die Drehzahl rueckwaerts negativ und landete an der Untergrenze (400 U/min
	// bei ~25 km/h) - Tacho, Motorklang, Antriebsmoment und Motorbremse liefen
	// damit rueckwaerts alle falsch.
	const float WheelOmega = Speed * GetGearDirection() / FMath::Max(WheelRadiusM, 0.01f);
	return WheelOmega * GetTotalGearRatio() * (60.0f / (2.0f * PI));
}

float FWiesbadenVehiclePhysics::AdvanceWheelSpinFlare(
	bool bWheelSpinning, float Throttle, float CurrentFlareRpm,
	float MaxFlareRpm, float RiseRatePerSec, float DecayRatePerSec, float DeltaSeconds)
{
	// Ziel: beim Durchdrehen dreht der unbelastete Motor hoch (skaliert mit dem
	// Gaspedal), sonst faellt der Flare auf 0 zurueck.
	const float Target = bWheelSpinning
		? MaxFlareRpm * FMath::Clamp(Throttle, 0.0f, 1.0f)
		: 0.0f;
	// Konstante Rate zum Ziel - hoch schnell (Ausbrechen), zurueck langsamer.
	const float Rate = (Target > CurrentFlareRpm) ? RiseRatePerSec : DecayRatePerSec;
	const float Step = FMath::Max(0.0f, Rate) * FMath::Max(0.0f, DeltaSeconds);
	if (CurrentFlareRpm < Target)
	{
		return FMath::Min(CurrentFlareRpm + Step, Target);
	}
	return FMath::Max(CurrentFlareRpm - Step, Target);
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
		// Hochschalten trennt kurz den Kraftschluss (Kupplung): fuer die
		// Schaltdauer faellt das Antriebsmoment weg (Zugkraftunterbrechung), dann
		// greift der neue Gang. Nur beim HOCHschalten - das gibt dem Antrieb sein
		// mechanisches Gefuehl, ohne Runterschalten/Leerlauf/Stand zu beruehren.
		ShiftTimeRemaining = UpshiftDurationSeconds;
	}
	else if (EngineRpm < ShiftDownRpm && Gear > 1)
	{
		Gear -= 1;
	}
}

float FWiesbadenVehiclePhysics::GetWheelForceDemand(float Throttle) const
{
	// ROHE Antriebskraft am Rad aus Motormoment * Gesamtuebersetzung / Radius,
	// mit dem Gaspedal skaliert - OHNE Traktionsgrenze. Die Begrenzung durch die
	// Reifenhaftung (und damit der Radschlupf) uebernimmt das Slip-Modell im Tick
	// ueber ComputeTransmittedLongitudinalForce - so kann die geforderte Kraft
	// die Haftgrenze der Antriebsachse ueberschreiten und das Rad durchdrehen.
	const float Torque = MotorTorqueAt(EngineRpm);
	const float WheelForce = Torque * GetTotalGearRatio() / FMath::Max(WheelRadiusM, 0.01f);
	return WheelForce * FMath::Clamp(Throttle, 0.0f, 1.0f);
}

float FWiesbadenVehiclePhysics::ComputeTransmittedLongitudinalForce(
	float DemandN, float StaticGripN, float KineticGripN, bool& bSlipping)
{
	const float StaticGrip = FMath::Max(StaticGripN, 0.0f);
	const float KineticGrip = FMath::Clamp(KineticGripN, 0.0f, StaticGrip);
	const float AbsDemand = FMath::Abs(DemandN);

	if (bSlipping)
	{
		// Bleibt rutschend, bis die Anforderung unter die Gleitreibung faellt -
		// sonst flatterte der Zustand exakt am Grenzwert hin und her (Hysterese).
		if (AbsDemand <= KineticGrip)
		{
			bSlipping = false;
			return DemandN;
		}
		return FMath::Sign(DemandN) * KineticGrip;
	}

	if (AbsDemand > StaticGrip)
	{
		// Ueber der Haftreibung -> das Rad rutscht (dreht durch bzw. blockiert),
		// der Grip faellt auf die kleinere Gleitreibung.
		bSlipping = true;
		return FMath::Sign(DemandN) * KineticGrip;
	}

	return DemandN;
}

float FWiesbadenVehiclePhysics::ComputeAbsBrakeCapN(
	float StaticGripN, float KineticGripN, float PhaseRad)
{
	const float StaticGrip = FMath::Max(StaticGripN, 0.0f);
	const float KineticGrip = FMath::Clamp(KineticGripN, 0.0f, StaticGrip);

	// Threshold-/ABS-Anmutung: die uebertragbare Bremskraft pulst zwischen Gleit-
	// und Haftreibung (das Rad wechselt zwischen blockiert und wieder greifend).
	// Nie ueber die Haftreibung ("begrenzt"), im Mittel (Static+Kinetic)/2.
	const float Pulse = 0.5f + 0.5f * FMath::Sin(PhaseRad);
	return FMath::Lerp(KineticGrip, StaticGrip, Pulse);
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

float FWiesbadenVehiclePhysics::ComputeDynamicFrontLoadFraction(
	float StaticFrontFraction, float LongitudinalAccelMetersPerS2,
	float GravityMetersPerS2, float CgHeightM, float WheelbaseM)
{
	const float G = FMath::Max(GravityMetersPerS2, 0.01f);
	const float L = FMath::Max(WheelbaseM, 0.01f);

	// Uebertragener Lastanteil = a_x * h / (g * L). a_x > 0 (beschleunigen)
	// nimmt der Vorderachse Last (nach hinten), a_x < 0 (bremsen) gibt ihr Last.
	const float Transfer = LongitudinalAccelMetersPerS2 * CgHeightM / (G * L);
	const float FrontFraction = StaticFrontFraction - Transfer;

	// Keine Achse hebt rechnerisch ganz ab - ein Rest bleibt immer belastet.
	return FMath::Clamp(FrontFraction, 0.08f, 0.92f);
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
		EffectiveMuTraction(), GravityMetersPerS2, LongitudinalAccelMetersPerS2);
	const float MaxLateralYaw = AvailableLateral / Speed;
	return FMath::Clamp(KinematicYaw, -MaxLateralYaw, MaxLateralYaw);
}

void FWiesbadenVehiclePhysics::Tick(
	const FWiesbadenVehiclePhysicsInput& Input,
	float DeltaSeconds,
	FWiesbadenVehiclePhysicsOutput& Out)
{
	DeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, 0.5f);

	// Belags-Griffigkeit dieses Ticks uebernehmen (skaliert das effektive mu in
	// allen Grip-Termen - Antrieb, Bremse, Reibungskreis, Gierlimit).
	SurfaceGripScale = FMath::Clamp(Input.SurfaceGripScale, 0.1f, 1.0f);

	// Duenner Orchestrator: erst die Laengsdynamik (liefert die Laengs-
	// beschleunigung), dann die Querdynamik, die sie fuer Reibungskreis und
	// Radlastverlagerung braucht. Jede Phase ist fuer sich verstaendlich.
	const float Acceleration = TickLongitudinal(Input, DeltaSeconds, Out);
	TickLateral(Input, DeltaSeconds, Acceleration, Out);
}

float FWiesbadenVehiclePhysics::TickLongitudinal(
	const FWiesbadenVehiclePhysicsInput& Input,
	float DeltaSeconds,
	FWiesbadenVehiclePhysicsOutput& Out)
{
	// Laufende Schaltunterbrechung altern lassen, DANN schalten (ein frisch in
	// ShiftGear gesetzter Timer laeuft so die volle Schaltdauer).
	ShiftTimeRemaining = FMath::Max(0.0f, ShiftTimeRemaining - DeltaSeconds);
	ShiftGear(Input, DeltaSeconds);
	const bool bShifting = ShiftTimeRemaining > 0.0f;

	const bool bReverse = (Gear < 0);
	const float Throttle = FMath::Clamp(Input.Throttle, 0.0f, 1.0f);
	const float Brake = FMath::Clamp(Input.Brake, 0.0f, 1.0f);
	const float WeightN = Powertrain.MassKg * GravityMetersPerS2;

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

	// Antriebskraft mit RADSCHLUPF: vorwaerts positiv, rueckwaerts negativ.
	// Ohne Treibstoff liefert der Motor keine Kraft - der Wagen rollt nur
	// noch aus (Roll-/Luftwiderstand), Bremse und Lenkung bleiben wirksam.
	//
	// Die uebertragbare Kraft ist durch die Haftreibung der ANTRIEBSachse
	// (Kaefer: hinten) begrenzt. Deren Last ist dynamisch - beim Anfahren
	// squattet das Heck und bekommt mehr Grip. Fordert der Motor mehr als die
	// Haftreibung, dreht das Rad durch und der Grip faellt auf Gleitreibung:
	// harter Vollgas-Start kostet so Vortrieb (Radspin) statt ihn zu klemmen.
	// Radlast aus dem VORTICK (a_x erst nach der Antriebskraft bekannt).
	float DriveForce = 0.0f;
	if (HasFuel() && !bShifting)
	{
		// Richtung aus DERSELBEN Quelle wie die Drehzahl-Umrechnung (Gang).
		const float DemandRaw = GetWheelForceDemand(Throttle);
		float Demand = DemandRaw * GetGearDirection();

		// Rueckwaerts-Begrenzer: die Anforderung in den letzten 20 % vor
		// ReverseMaxSpeed weich auf 0 zuruecknehmen. Die harte Tempoklemme
		// weiter unten allein liess den Motor am Limit mit voller Kraft gegen
		// die Klemme druecken - mit der korrekten Rueckwaerts-Drehzahl liegt die
		// Anforderung dort ueber der Haftgrenze, und der Wagen fuhr mit
		// Dauer-Radspin (Quietschen, Spuren, ASR, Drehzahl am Anschlag).
		if (bReverse)
		{
			const float Limit = FMath::Max(ReverseMaxSpeedMetersPerS, 0.1f);
			const float Headroom = FMath::Clamp(
				(Limit - FMath::Abs(SpeedMetersPerS)) / (0.2f * Limit), 0.0f, 1.0f);
			Demand *= Headroom;
		}

		const float RearFracDyn = 1.0f - ComputeDynamicFrontLoadFraction(
			FrontWeightFraction, LastLongAccelMetersPerS2, GravityMetersPerS2, CgHeightM, WheelbaseM);
		const float RearLoadN = WeightN * RearFracDyn;

		DriveForce = ComputeTransmittedLongitudinalForce(
			Demand, StaticGripN(RearLoadN), KineticGripN(RearLoadN), bDriveSlipState);
	}
	else
	{
		// Kein Kraftschluss (kein Sprit ODER Kupplung waehrend des Hochschaltens).
		bDriveSlipState = false;
	}
	Out.bWheelSpin = bDriveSlipState && FMath::Abs(DriveForce) > 1.0f;

	// Widerstaende wirken gegen die Bewegungsrichtung.
	const float Speed = SpeedMetersPerS;
	const float RollResistance = (FMath::Abs(Speed) > 0.1f)
		? RollCoeff * WeightN * FMath::Sign(Speed)
		: 0.0f;
	const float AirResistance = AirDensityKgM3 * 0.5f * DragCoeffAreaM2 * Speed * FMath::Abs(Speed);

	// Bremskraft mit BLOCKIER-/ABS-Anmutung. Die geforderte Bremskraft (Pedal +
	// Handbremse) ist durch den verfuegbaren LAENGS-Grip begrenzt - und der folgt
	// aus dem REIBUNGSKREIS: die aktuelle Querbeschleunigung (a_lat = Vx * Gierrate
	// aus dem Vortick) verbraucht Haftung, die dann laengs zum Bremsen fehlt.
	//
	// So EMERGIERT das Blockieren aus der Grip-Grenze, nicht aus dem Pedalwert:
	// auf der Geraden steht der volle Grip (mu*g), ein 0,7-g-Pedal blockiert dort
	// NICHT; beim Bremsen in der Kurve (oder spaeter auf griffarmem Belag ueber
	// ein kleineres mu) faellt der verfuegbare Grip unter die Anforderung und die
	// Raeder blockieren. Ueberschreitet die Anforderung die Haftgrenze, pulst die
	// uebertragene Kraft zwischen Gleit- und Haftreibung (Schwellwert/ABS) -
	// "begrenzt und gepulst". Die Seitenfuehrung bricht ueber denselben
	// Reibungskreis (Querdynamik) von selbst weg.
	const float BrakeDemandN = Brake * BrakeForceN
		+ (Input.bHandbrake ? BrakeForceN * 0.6f : 0.0f);

	// Lock-ENTSCHEIDUNG aus dem kombinierten Reibungskreis: die Querbeschleunigung
	// (a_lat = Vx * Gierrate aus dem Vortick) zehrt am verfuegbaren Laengs-Grip.
	// Auf der Geraden steht der volle Grip mu*g -> ein 0,7-g-Pedal blockiert NICHT;
	// in der Kurve (oder kuenftig auf kleinerem mu) faellt der Laengs-Grip unter
	// die Anforderung -> Blockieren. So folgt der Zustand aus der GRIP-Grenze,
	// nicht aus dem BrakeForceN-Wert.
	const float LateralAccel = FMath::Abs(SpeedMetersPerS * YawRateRadPerS);
	const float AvailLongGripN = Powertrain.MassKg *
		ComputeAvailableLateralAccel(EffectiveMuTraction(), GravityMetersPerS2, LateralAccel);
	if (bBrakeLockState)
	{
		if (BrakeDemandN <= AvailLongGripN * MuKineticFraction) { bBrakeLockState = false; }
	}
	else if (BrakeDemandN > AvailLongGripN)
	{
		bBrakeLockState = true;
	}

	// KRAFT-Cap an der VOLLEN Laengshaftung (mu*Gewicht); bei Blockieren pulst die
	// uebertragene Kraft zwischen Gleit- und Haftreibung (Schwellwert/ABS,
	// "begrenzt und gepulst"). Die Quer-Minderung uebernimmt weiterhin der
	// Reibungskreis der Querdynamik ueber die so entstehende Laengsbeschleunigung
	// - kein doppelter Abzug, und Bremsen bleibt am Kurvenlimit wirksam.
	const float BrakeGripStatic = StaticGripN(WeightN);
	const float BrakeGripKinetic = KineticGripN(WeightN);
	float BrakeCapN = BrakeGripStatic;
	if (bBrakeLockState)
	{
		BrakeAbsPhaseRad += 2.0f * PI * BrakeAbsPulseHz * DeltaSeconds;
		BrakeAbsPhaseRad = FMath::Fmod(BrakeAbsPhaseRad, 2.0f * PI);
		BrakeCapN = ComputeAbsBrakeCapN(BrakeGripStatic, BrakeGripKinetic, BrakeAbsPhaseRad);
	}
	else
	{
		BrakeAbsPhaseRad = 0.0f;
	}

	const float BrakeForce = FMath::Min(BrakeDemandN, BrakeCapN) * FMath::Sign(Speed);
	Out.bWheelLock = bBrakeLockState && FMath::Abs(Speed) > 0.1f;

	// Motorbremse im Schub: geschlossene Drosselklappe, Gang eingelegt.
	//
	// Ohne diesen Anteil rollt das Fahrzeug beim Gaswegnehmen nur gegen Roll-
	// und Luftwiderstand aus und fuehlt sich an, als waere es im Leerlauf.
	// Das Moment wirkt ueber die Gesamtuebersetzung - im kleinen Gang deutlich
	// spuerbar, im grossen kaum, genau wie beim echten Fahrzeug.
	float EngineBrakeForce = 0.0f;
	if (Throttle < 0.05f && FMath::Abs(Speed) > 0.5f && Gear != 0 && !bShifting)
	{
		const float RpmFraction = FMath::Clamp(EngineRpm / FMath::Max(Powertrain.MaxRpm, 1.0f), 0.0f, 1.2f);
		const float TorqueNm = EngineBrakeTorqueNm * RpmFraction;
		EngineBrakeForce = TorqueNm * FMath::Abs(GetTotalGearRatio())
			/ FMath::Max(WheelRadiusM, 0.01f) * FMath::Sign(Speed);
	}

	// Nettokraft entlang der Fahrtrichtung; die Bremse bremst auf 0 ab.
	float NetForce = DriveForce - RollResistance - AirResistance - BrakeForce - EngineBrakeForce;

	// Haften: Im Stillstand ohne Zugkraft bleibt das Fahrzeug stehen.
	if (FMath::Abs(Speed) < 0.1f && FMath::Abs(NetForce) < RollCoeff * WeightN * 0.5f)
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

	// Radspin-Drehzahlflare (nur Anzeige/Klang): beim Durchdrehen entkoppelt der
	// Motor von der Strasse und dreht hoch. Er wird NUR auf die AUSGABE-Drehzahl
	// gelegt - die interne EngineRpm (Schalten, Drehmoment) bleibt geschwindig-
	// keitsabgeleitet, damit der Flare den Antrieb nicht destabilisiert.
	WheelSpinFlare = AdvanceWheelSpinFlare(
		Out.bWheelSpin, Throttle, WheelSpinFlare,
		MaxWheelSpinFlareRpm, WheelSpinFlareRiseRate, WheelSpinFlareDecayRate, DeltaSeconds);

	// Laengs-Ausgaben fuellen.
	Out.ForwardSpeedMetersPerS = SpeedMetersPerS;
	Out.SpeedKmh = FMath::Abs(SpeedMetersPerS) * 3.6f;
	Out.EngineRpm = FMath::Clamp(
		EngineRpm + WheelSpinFlare,
		Powertrain.IdleRpm * 0.5f, Powertrain.MaxRpm * 1.15f);
	Out.Gear = Gear;
	Out.ForwardAccelerationMetersPerS2 = Acceleration;

	// Laengsbeschleunigung fuer den naechsten Tick merken: die Traktionsgrenze
	// der Antriebsachse braucht deren dynamische Radlast, und die folgt aus a_x.
	LastLongAccelMetersPerS2 = Acceleration;

	// Treibstoff: der Tank besitzt Fuellstand und Verbrauch; hier nur die
	// mechanische Radleistung uebergeben (P = F * v).
	const float WheelPowerKw = FMath::Abs(DriveForce * SpeedMetersPerS) / 1000.0f;
	Fuel.Consume(WheelPowerKw, DeltaSeconds);

	return Acceleration;
}

void FWiesbadenVehiclePhysics::TickLateral(
	const FWiesbadenVehiclePhysicsInput& Input,
	float DeltaSeconds,
	float LongitudinalAccelMetersPerS2,
	FWiesbadenVehiclePhysicsOutput& Out)
{
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
		YawRateRadPerS = FMath::Clamp(YawRateRadPerS, -1.2f, 1.2f);

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
			EffectiveMuTraction(), GravityMetersPerS2, LongitudinalAccelMetersPerS2)
			/ FMath::Max(EffectiveMuTraction() * GravityMetersPerS2, 0.01f);

		// Reifen-Seitenkraefte, linear, im Reibungskreis je Achse gesaettigt.
		//
		// DYNAMISCHE Achslasten: Bremsen laedt die Vorderachse (mehr Grip vorn,
		// Heck leichter -> Lastwechsel-Uebersteuern), Gas laedt die Hinterachse
		// (Traktion, stabil). Der schon berechnete Laengsbeschleunigungswert
		// treibt die Verlagerung - dieselbe Groesse, die auch die Karosserie
		// nicken laesst; so decken sich Bild und Physik.
		const float FrontFracDyn = ComputeDynamicFrontLoadFraction(
			FrontWeightFraction, LongitudinalAccelMetersPerS2, GravityMetersPerS2, CgHeightM, L);
		const float FrontLoad = m * GravityMetersPerS2 * FrontFracDyn;
		const float RearLoad = m * GravityMetersPerS2 * (1.0f - FrontFracDyn);
		const float FyfMax = StaticGripN(FrontLoad) * LatFraction;
		const float FyrMax = StaticGripN(RearLoad) * LatFraction;

		// LASTABHAENGIGE Schraeglaufsteifigkeit: ein staerker belasteter Reifen
		// baut Seitenkraft steiler auf. Bisher skalierte die dynamische Achslast
		// nur die Saettigung (FyfMax/FyrMax); die STEIGUNG (Cf/Cr) blieb fest, also
		// reagierte die Balance UNTERHALB der Grenze kaum auf die Pedale. Jetzt
		// skaliert Cf/Cr mit dem Lastverhaeltnis (dyn/statisch): Bremsen laedt vorn
		// -> mehr Front-Biss beim Einlenken; Gas laedt hinten -> stabiler. KONSERVATIV
		// geklemmt (+-MaxStiffnessLoadShift), damit die Hinterachse nicht so weich
		// wird, dass das lineare Einspurmodell instabil wird (kritische Geschwindig-
		// keit ueber Hoechsttempo). Bei a_x=0 ist das Verhaeltnis 1 -> stationaere
		// Kurve unveraendert.
		const float MinScale = 1.0f - MaxStiffnessLoadShift;
		const float MaxScale = 1.0f + MaxStiffnessLoadShift;
		const float CfScale = FMath::Clamp(FrontFracDyn / FMath::Max(FrontWeightFraction, 0.01f), MinScale, MaxScale);
		const float CrScale = FMath::Clamp((1.0f - FrontFracDyn) / FMath::Max(1.0f - FrontWeightFraction, 0.01f), MinScale, MaxScale);
		const float Cf = CorneringStiffnessFrontNPerRad * CfScale;
		const float Cr = CorneringStiffnessRearNPerRad * CrScale;
		const float Fyf = FMath::Clamp(-Cf * AlphaF, -FyfMax, FyfMax);
		const float Fyr = FMath::Clamp(-Cr * AlphaR, -FyrMax, FyrMax);

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
		const float MaxYaw = EffectiveMuTraction() * GravityMetersPerS2 / FMath::Max(Vx, 1.0f);
		YawRateRadPerS = FMath::Clamp(YawRateRadPerS, -MaxYaw, MaxYaw);

		// BLOCKIERTE Raeder gleiten und richten den Wagen zur Fahrtrichtung aus,
		// statt Gier aufzubauen - ohne Seitenfuehrung fehlt sonst jede daempfende
		// Kraft und das Heck reisst beim Kurvenbremsen weit herum (der im Audit
		// bemaengelte ~80-Grad-Ausbruch). Eine sanfte, schrittweite-stabile
		// Daempfung holt den UEBERSCHUSS zurueck, ohne das Blockieren abzuschalten:
		// der Lastwechsel bleibt spuerbar, laeuft aber nicht mehr weg. NUR bei
		// blockierten Raedern aktiv -> gerades Bremsen (Gier ~0) und normale Kurve
		// (nicht blockiert) bleiben voellig unveraendert.
		if (bBrakeLockState && LockedYawDampingRate > 0.0f)
		{
			const float Damp = FMath::Exp(-LockedYawDampingRate * DeltaSeconds);
			YawRateRadPerS *= Damp;
			LateralVelocityMetersPerS *= Damp;
		}
	}
	else
	{
		// Langsam/Stand/Rueckwaerts: kinematisch (dynamisches Modell singulaer bei
		// v->0). Querschlupf sanft abbauen, damit kein Rest-Drift haengen bleibt.
		YawRateRadPerS = ComputeYawRate(SteerAngleNorm, LongitudinalAccelMetersPerS2);
		LateralVelocityMetersPerS =
			FMath::FInterpTo(LateralVelocityMetersPerS, 0.0f, DeltaSeconds, 5.0f);
	}

	Out.YawRateRadPerS = YawRateRadPerS;
	Out.LateralVelocityMetersPerS = LateralVelocityMetersPerS;
	Out.SlipAngleDeg = (FMath::Abs(Vx) > 0.5f)
		? FMath::RadiansToDegrees(FMath::Atan2(LateralVelocityMetersPerS, FMath::Abs(Vx)))
		: 0.0f;
	Out.SteerAngleNorm = SteerAngleNorm;
}
