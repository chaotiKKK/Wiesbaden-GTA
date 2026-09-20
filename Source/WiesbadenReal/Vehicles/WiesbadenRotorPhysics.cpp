// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenRotorPhysics.h"

void FWiesbadenRotorPhysics::Reset()
{
	MainRotorRpm = 0.0f;
	TailRotorRpm = 0.0f;
	EngineRpm = 0.0f;
}

void FWiesbadenRotorPhysics::Tick(const FWiesbadenRotorPhysicsInput& In, float DeltaTime,
	const FVector& LocalLinearVelocityCmPerS, const FVector& LocalAngularVelocityRadPerS,
	FWiesbadenRotorPhysicsOutput& Out)
{
	DeltaTime = FMath::Max(DeltaTime, 0.0001f);

	const float Collective = FMath::Clamp(In.Collective, 0.0f, 1.0f);
	const float CycPitch = FMath::Clamp(In.CyclicPitch, -1.0f, 1.0f);
	const float CycRoll = FMath::Clamp(In.CyclicRoll, -1.0f, 1.0f);
	const float Pedal = FMath::Clamp(In.YawPedal, -1.0f, 1.0f);

	// -- Hauptrotor-Drehzahl (rad/s) --------------------------------------
	const float OmegaMain = FMath::Max(MainRotorRpm, 0.0f) * (2.0f * PI / 60.0f);
	const float OmegaTail = OmegaMain * TailRotorGearRatio;
	TailRotorRpm = MainRotorRpm * TailRotorGearRatio;

	// -- 1) Governor (Drehzahlregelung, leistungsbegrenzt) ----------------
	const float TargetOmega = TargetMainRotorRpm * (2.0f * PI / 60.0f);
	float EngineTorque = 0.0f;
	if (In.bEngineRunning)
	{
		// Proportional-Regler auf die Soll-Drehzahl ...
		const float GovernorTorque = GovernorGain * (TargetOmega - OmegaMain);
		// ... begrenzt durch die Wellenleistung (P = T * omega).
		const float PowerLimitTorque = (OmegaMain > 1.0f) ? (EnginePowerWatts / OmegaMain) : EnginePowerWatts;
		EngineTorque = FMath::Clamp(GovernorTorque, 0.0f, PowerLimitTorque);
	}

	// -- 2) Profilwiderstand des Hauptrotors (steigt mit Pitch und omega^2) --
	const float RotorDragTorque = MainRotorDragFactor * OmegaMain * OmegaMain * (1.0f + Collective);

	// -- 3) Autorotation: aufsteigender Luftstrom im Sinkflug treibt den Rotor --
	// Sinkgeschwindigkeit durch die Rotorscheibe (lokal -Z), in m/s.
	const float DescentSpeed = FMath::Max(0.0f, -LocalLinearVelocityCmPerS.Z / 100.0f);
	const float AutoTorque = AutorotationGain * DescentSpeed * (1.0f - Collective);

	// Drehzahl-Dynamik: (Motor + Autorotation - Widerstand) / Traegheit.
	const float NetTorque = EngineTorque + AutoTorque - RotorDragTorque;
	const float NewOmegaMain = FMath::Max(0.0f, OmegaMain + (NetTorque / FMath::Max(MainRotorInertia, 0.01f)) * DeltaTime);
	MainRotorRpm = NewOmegaMain * (60.0f / (2.0f * PI));

	// -- 4) Auftrieb (Lift) ------------------------------------------------
	// Blattanstellwinkel aus Collective (rad).
	// Kollektiv-Kennlinie mit dem SCHWEBEPUNKT in der Mitte.
	//
	// Hier stand eine gerade Interpolation von Min nach Max. Bei
	// Mittelstellung (Collective 0,5 - das ist die Ruhelage OHNE Eingabe) ergab
	// das 8 Grad, waehrend zum Schweben rund 3,2 Grad genuegen. Der Rotor
	// erzeugte damit das Zweieinhalbfache des Gewichts, und der Helikopter
	// stieg ohne Zutun davon.
	//
	// Ein realer Kollektivhebel wird getrimmt: Mittelstellung haelt die Hoehe,
	// darueber steigt, darunter sinkt es. Genau das bildet die geknickte
	// Kennlinie ab.
	const float HoverPitchDeg = ComputeHoverPitchDeg();
	const float PitchDeg = Collective >= 0.5f
		? FMath::Lerp(HoverPitchDeg, MaxCollectivePitchDeg, (Collective - 0.5f) * 2.0f)
		: FMath::Lerp(MinCollectivePitchDeg, HoverPitchDeg, Collective * 2.0f);
	const float PitchRad = FMath::DegreesToRadians(PitchDeg);

	// 4a) Blattspitzenverlust (Retreating Blade Stall): Ab ~75 % der
	// Hoechstgeschwindigkeit bricht der Auftrieb der ruecklaufenden Blaetter
	// ein und sinkt bis vmax auf ~45 %. Wirkt auf den gesamten Auftriebsvektor
	// (vertikal UND horizontal), damit die Maschine am Limit einknickt statt
	// seitlich wegzulaufen.
	const float ForwardSpeedMetersPerS = LocalLinearVelocityCmPerS.X / 100.0f;
	float LiftScale = 1.0f;
	const float StallStart = FMath::Max(MaxForwardSpeedMetersPerS, 1.0f) * RetreatingBladeStallStartFrac;
	if (ForwardSpeedMetersPerS > StallStart)
	{
		const float StallFrac = FMath::Clamp(
			(ForwardSpeedMetersPerS - StallStart)
				/ FMath::Max(MaxForwardSpeedMetersPerS - StallStart, 1.0f),
			0.0f, 1.0f);
		LiftScale = FMath::Lerp(1.0f, 0.45f, StallFrac);
	}

	// 4b) Koaxialer Doppelrotor: zwei gegenlaeufige Rotoren verdoppeln den
	// Auftrieb je Flaeche (LiftFactor ist je Rotor definiert).
	const float CoaxialLiftMultiplier = bCoaxialRotors ? 2.0f : 1.0f;
	const float LiftForce = MainRotorLiftFactor * OmegaMain * OmegaMain * PitchRad
		* CoaxialLiftMultiplier * LiftScale; // N

	// Gewichtskraft fuer die Normalisierung der Momentenautoritaet.
	const float WeightForce = MassKg * GravityMetersPerS2;
	const float LiftNormalized = LiftForce / FMath::Max(WeightForce, 1.0f);

	// -- 5) Zyklik: Neigung der Rotorscheibe -------------------------------
	const float MaxTiltRad = FMath::DegreesToRadians(CyclicMaxTiltDeg);
	const float TiltPitch = CycPitch * MaxTiltRad; // + = Nase runter
	const float TiltRoll = CycRoll * MaxTiltRad;   // + = rechts

	// Horizontaler Kraftanteil aus dem geneigten Auftriebsvektor.
	const float SinPitch = FMath::Sin(TiltPitch);
	const float SinRoll = FMath::Sin(TiltRoll);

	FVector Force = FVector::ZeroVector; // lokal: +X vor, +Y rechts, +Z oben
	Force.X = LiftForce * SinPitch;
	Force.Y = LiftForce * SinRoll;
	Force.Z = LiftForce * FMath::Cos(TiltPitch) * FMath::Cos(TiltRoll);

	FVector Torque = FVector::ZeroVector; // Achsen: X = Roll, Y = Pitch, Z = Yaw

	// Zyklik-Momente (quasi-statisch, skaliert mit der Rotorbelastung).
	Torque.X = CycRoll * CyclicRollMomentAuthority * LiftNormalized;   // Roll
	Torque.Y = -CycPitch * CyclicPitchMomentAuthority * LiftNormalized; // Pitch (Nase runter = -)

	// -- 6) Yaw: Koaxial-Rotoren oder Heckrotor + Reaktionsmoment ----------
	if (bCoaxialRotors)
	{
		// Gegenlaeufiges Rotorpaar hebt das Reaktionsmoment gegenseitig auf;
		// es gibt keinen Heckrotor. Das Pedal erzeugt ueber differentielle
		// Blattverstellung direkt ein Yaw-Moment (skaliert mit der Last).
		Force.Y = 0.0f;
		Torque.Z = Pedal * CoaxialYawAuthority * LiftNormalized;
	}
	else
	{
		// Reaktionsmoment: Der Koerper dreht entgegen der Rotordrehung, Mass ist
		// der aerodynamische Widerstand des Rotors.
		const float MainReactionTorque = -RotorDragTorque * MainRotorTorqueReactionScale;

		// Heckrotor-Schub (lateral, +Y = rechts). Neutral kompensiert er das
		// Reaktionsmoment, das Pedal erzeugt die Yaw-Kontrolle.
		const float TailLateralForce =
			-(MainRotorTorqueReactionScale * RotorDragTorque) / FMath::Max(TailBoomLengthM, 0.1f)
			- TailRotorThrustCoefficient * OmegaTail * OmegaTail * Pedal;

		Force.Y += TailLateralForce;
		// Moment des Heckrotor-Schubs um den Schwerpunkt (Heck bei -X).
		Torque.Z = MainReactionTorque - FMath::Max(TailBoomLengthM, 0.1f) * TailLateralForce;
	}

	// -- 7) Aerodynamische Winkeldaempfung (stabilisiert alle Achsen) ------
	Torque -= RotorAngularDamping * LocalAngularVelocityRadPerS;

	// Triebwerksdrehzahl fuer Audio/HUD: laeuft nur bei laufendem Triebwerk.
	EngineRpm = In.bEngineRunning
		? FMath::Max(MainRotorRpm, 0.0f) * EngineToMainRotorRatio
		: 0.0f;

	Out.Force = Force;
	Out.Torque = Torque;
}

float FWiesbadenRotorPhysics::ComputeHoverPitchDeg() const
{
	// Auftrieb = Faktor * omega^2 * Anstellwinkel(rad) * Koaxialfaktor.
	// Gesucht ist der Winkel, bei dem das genau die Gewichtskraft ergibt.
	//
	// Massgeblich ist die AKTUELLE Drehzahl, nicht die Solldrehzahl: Unter Last
	// pendelt sich der Rotor darunter ein (gemessen 375 statt 420 U/min), und
	// mit der Solldrehzahl gerechnet faellt der Schwebepunkt zu niedrig aus -
	// der Helikopter saenke dann bei neutralem Hebel.
	const float EffectiveRpm = MainRotorRpm > TargetMainRotorRpm * 0.2f
		? MainRotorRpm
		: TargetMainRotorRpm;
	const float Omega = EffectiveRpm * 2.0f * PI / 60.0f;
	const float CoaxialMultiplier = bCoaxialRotors ? 2.0f : 1.0f;
	const float Denominator = MainRotorLiftFactor * Omega * Omega * CoaxialMultiplier;

	if (Denominator <= KINDA_SMALL_NUMBER)
	{
		return MinCollectivePitchDeg;
	}

	const float NeededRad = (MassKg * GravityMetersPerS2) / Denominator;
	return FMath::Clamp(
		FMath::RadiansToDegrees(NeededRad),
		MinCollectivePitchDeg,
		MaxCollectivePitchDeg);
}
