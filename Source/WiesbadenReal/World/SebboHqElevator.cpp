// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/SebboHqElevator.h"

bool FSebboHqElevator::RequestFloor(int32 Floor)
{
	if (Floor < 0 || Floor >= FloorCount)
	{
		return false;
	}
	TargetFloor = Floor;
	const double Destination = Floor * FloorHeightCm;
	if (FMath::IsNearlyEqual(PositionCm, Destination, 0.1))
	{
		CurrentFloor = Floor;
		Phase = EHqElevatorPhase::Opening;
		OpenTimeRemaining = DwellSeconds;
	}
	else
	{
		Phase = DoorOpenFraction > KINDA_SMALL_NUMBER
			? EHqElevatorPhase::Closing : EHqElevatorPhase::Moving;
	}
	return true;
}

void FSebboHqElevator::Tick(double DeltaSeconds)
{
	if (DeltaSeconds <= 0.0)
	{
		return;
	}
	const double DoorStep = DeltaSeconds / FMath::Max(DoorSeconds, 0.01);
	switch (Phase)
	{
	case EHqElevatorPhase::Closing:
		DoorOpenFraction = FMath::Max(0.0, DoorOpenFraction - DoorStep);
		if (DoorOpenFraction <= KINDA_SMALL_NUMBER)
		{
			DoorOpenFraction = 0.0;
			Phase = FMath::IsNearlyEqual(PositionCm, TargetFloor * FloorHeightCm, 0.1)
				? EHqElevatorPhase::Closed : EHqElevatorPhase::Moving;
		}
		break;
	case EHqElevatorPhase::Moving:
	{
		const double Destination = TargetFloor * FloorHeightCm;
		const double Delta = Destination - PositionCm;
		const double Step = FMath::Max(0.0, SpeedCmPerSecond) * DeltaSeconds;
		PositionCm += FMath::Clamp(Delta, -Step, Step);
		if (FMath::IsNearlyEqual(PositionCm, Destination, 0.1))
		{
			PositionCm = Destination;
			CurrentFloor = TargetFloor;
			Phase = EHqElevatorPhase::Opening;
		}
		break;
	}
	case EHqElevatorPhase::Opening:
		DoorOpenFraction = FMath::Min(1.0, DoorOpenFraction + DoorStep);
		if (DoorOpenFraction >= 1.0 - KINDA_SMALL_NUMBER)
		{
			DoorOpenFraction = 1.0;
			OpenTimeRemaining = DwellSeconds;
			Phase = EHqElevatorPhase::Open;
		}
		break;
	case EHqElevatorPhase::Open:
		OpenTimeRemaining -= DeltaSeconds;
		if (OpenTimeRemaining <= 0.0)
		{
			Phase = EHqElevatorPhase::Closing;
		}
		break;
	case EHqElevatorPhase::Closed:
		break;
	}
}

bool FSebboHqElevator::IsStoppedAt(int32 Floor) const
{
	return Floor >= 0 && Floor < FloorCount && CurrentFloor == Floor
		&& Phase != EHqElevatorPhase::Moving && Phase != EHqElevatorPhase::Closing
		&& FMath::IsNearlyEqual(PositionCm, Floor * FloorHeightCm, 0.1);
}
