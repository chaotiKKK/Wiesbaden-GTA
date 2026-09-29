// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenBusDrive.h"

FWiesbadenVehiclePhysics WiesbadenBusDrive::MakeBusPhysics()
{
	FWiesbadenVehiclePhysics Physics;
	// Generischer Niederflur-Linienbus. Die Formeln und Zustandsuebergaenge sind
	// exakt die des Spielerwagens; Masse, Motor, Radstand und Reifen sind busgross.
	Physics.Powertrain.MassKg = 12000.0f;
	Physics.Powertrain.MaxTorqueNm = 950.0f;
	Physics.Powertrain.IdleRpm = 600.0f;
	Physics.Powertrain.MaxRpm = 2500.0f;
	Physics.Powertrain.TorqueCurveNormalized = {
		FVector2D(600.0, 0.65), FVector2D(1000.0, 0.90),
		FVector2D(1500.0, 1.00), FVector2D(2000.0, 0.88),
		FVector2D(2500.0, 0.60) };
	Physics.Powertrain.ForwardGearRatios = {3.2f, 2.1f, 1.35f, 0.90f};
	Physics.Powertrain.FinalDriveRatio = 5.0f;
	Physics.ShiftUpRpm = 2200.0f;
	Physics.ShiftDownRpm = 1050.0f;
	Physics.UpshiftDurationSeconds = 0.45f;
	Physics.WheelRadiusM = 0.342f;   // gemessener Reifen (Tools/bake_eswebus_wheels.py)
	Physics.WheelbaseM = 5.25f;
	Physics.BrakeForceN = 52000.0f;
	Physics.EngineBrakeTorqueNm = 95.0f;
	Physics.DragCoeffAreaM2 = 5.5f;
	Physics.RollCoeff = 0.014f;
	Physics.MaxSteerAngleDeg = 38.0f;
	Physics.MuTraction = 0.85f;
	Physics.LateralGripFactor = 1.0f;     // Kaefer-Kalibrierung gilt hier nicht
	Physics.DrivetrainEfficiency = 1.0f;
	Physics.bAbsEnabled = false;          // wie vor dem Spieler-ABS: Bremsplanung unveraendert
	Physics.CorneringStiffnessFrontNPerRad = 160000.0f;
	Physics.CorneringStiffnessRearNPerRad = 190000.0f;
	Physics.YawInertiaKgM2 = 70000.0f;
	Physics.FrontWeightFraction = 0.48f;
	Physics.CgHeightM = 0.85f;
	Physics.Reset();
	return Physics;
}

WiesbadenBusDrive::FStep WiesbadenBusDrive::Advance(FState& State,
	const WiesbadenBusLine::FBusState& Timetable,
	const WiesbadenBusLine::FBusRoute& Route,
	float CruiseKmh, float DeltaSeconds, int64 VehicleId, float SteeringNorm)
{
	FStep Step;
	Step.Position = Timetable;
	if (DeltaSeconds <= 0.0f || Route.TotalLengthCm <= 0.0)
	{
		return Step;
	}
	if (!State.bInitialized || State.VehicleId != VehicleId || State.bForward != Timetable.bForward)
	{
		State.Physics = MakeBusPhysics();
		State.ArcCm = Timetable.ArcLengthCm;
		State.LastTimetableArcCm = Timetable.ArcLengthCm;
		State.VehicleId = VehicleId;
		State.bForward = Timetable.bForward;
		State.bInitialized = true;
	}

	// Auf dem eigenen Rueckweg faehrt der Bus SEINE Linie vorwaerts, mit seinen
	// Halten; ihr Ende (Einstiegshaltestelle) ist ebenfalls ein Halt zum Anbremsen.
	const bool bReturn = Timetable.bReturnPath && Route.HasReturnLeg();
	const TArray<double>& StopList = bReturn ? Route.ReturnStopArcCm : Route.StopArcCm;
	const double LegLengthCm = bReturn ? Route.ReturnLengthCm : Route.TotalLengthCm;
	const double Direction = (bReturn || Timetable.bForward) ? 1.0 : -1.0;
	const double GapCm = (Timetable.ArcLengthCm - State.ArcCm) * Direction;
	const bool bScheduleMoving = !Timetable.bDwelling
		&& FMath::Abs(Timetable.ArcLengthCm - State.LastTimetableArcCm) > 0.1;
	State.LastTimetableArcCm = Timetable.ArcLengthCm;

	// Vorausliegende Halte verlangen Bremsweg. Bei Rot oder Verweilen gibt der
	// Fahrplan keine Strecke frei, daher ist das direkte Zeit-Ziel die Grenze.
	double DistanceToStopCm = TNumericLimits<double>::Max();
	for (const double StopArc : StopList)
	{
		const double Ahead = (StopArc - State.ArcCm) * Direction;
		if (Ahead > 1.0 && Ahead < DistanceToStopCm)
		{
			DistanceToStopCm = Ahead;
		}
	}
	if (bReturn)
	{
		const double ToEnd = LegLengthCm - State.ArcCm;
		if (ToEnd > 1.0 && ToEnd < DistanceToStopCm)
		{
			DistanceToStopCm = ToEnd;
		}
	}
	const double LookAheadCm = bScheduleMoving ? 1600.0 : 0.0;
	const double ScheduleRoomCm = FMath::Max(0.0, GapCm + LookAheadCm);
	const double BrakingRoomCm = Timetable.bDwelling
		? FMath::Max(0.0, GapCm)
		: FMath::Min(ScheduleRoomCm, DistanceToStopCm);
	const float MaxByDistanceMs = FMath::Sqrt(2.0f * 3.0f
		* static_cast<float>(FMath::Max(0.0, BrakingRoomCm - 50.0) / 100.0));
	const float CruiseMs = FMath::Max(1.0f, CruiseKmh) / 3.6f;
	const float CatchUpMs = FMath::Clamp(static_cast<float>(GapCm / 800.0), 0.0f, 2.5f);
	const float TargetMs = FMath::Min(CruiseMs + CatchUpMs, MaxByDistanceMs);
	const float ErrorMs = TargetMs - State.Physics.SpeedMetersPerS;
	FWiesbadenVehiclePhysicsInput Input;
	Input.Throttle = ErrorMs > 0.1f ? FMath::Clamp(0.25f + ErrorMs * 0.48f, 0.0f, 1.0f) : 0.0f;
	Input.Brake = ErrorMs < -0.15f ? FMath::Clamp(-ErrorMs * 0.34f, 0.0f, 1.0f) : 0.0f;
	Input.Steering = FMath::Clamp(SteeringNorm, -1.0f, 1.0f);
	State.Physics.Tick(Input, DeltaSeconds, Step.Physics);

	const double PreviousArc = State.ArcCm;
	State.ArcCm += Direction * FMath::Max(0.0f, Step.Physics.ForwardSpeedMetersPerS)
		* 100.0 * DeltaSeconds;
	// Eine schon aktive Rotphase oder Verweilphase darf der physikalische Bus
	// nicht ueberfahren. Bei der Fahrt bleibt die Fahrplan-Reserve zum Aufholen.
	const double MaxGrantedArc = Timetable.ArcLengthCm + Direction * LookAheadCm;
	if ((State.ArcCm - MaxGrantedArc) * Direction > 0.0)
	{
		State.ArcCm = MaxGrantedArc;
	}
	State.ArcCm = FMath::Clamp(State.ArcCm, 0.0, LegLengthCm);
	Step.TravelledCm = FMath::Abs(State.ArcCm - PreviousArc);
	State.WheelDegrees = FMath::Fmod(State.WheelDegrees
		+ static_cast<float>(Step.TravelledCm / (2.0 * PI * 34.2) * 360.0), 360.0f);
	Step.WheelDegrees = State.WheelDegrees;
	Step.Position.ArcLengthCm = State.ArcCm;
	Step.Position.bDwelling = Timetable.bDwelling
		&& FMath::Abs(State.ArcCm - Timetable.ArcLengthCm) < 50.0
		&& Step.Physics.ForwardSpeedMetersPerS < 0.6f;
	return Step;
}

FRotator WiesbadenBusDrive::WheelVisualRotation(float ForwardRollDegrees,
	bool bRightSide, float SteeringDegrees)
{
	return FRotator(0.0f, SteeringDegrees + (bRightSide ? 180.0f : 0.0f),
		bRightSide ? -ForwardRollDegrees : ForwardRollDegrees);
}
