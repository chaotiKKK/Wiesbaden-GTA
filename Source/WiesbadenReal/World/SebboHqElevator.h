// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Bewegungszustand des Tower-Aufzugs, ohne Actor- oder Komponentenabhaengigkeit. */
enum class EHqElevatorPhase : uint8
{
	Open,
	Closing,
	Moving,
	Opening,
	Closed
};

struct WIESBADENREAL_API FSebboHqElevator
{
	int32 FloorCount = 15;
	double FloorHeightCm = 400.0;
	double SpeedCmPerSecond = 250.0;
	double DoorSeconds = 1.2;
	double DwellSeconds = 5.0;

	int32 CurrentFloor = 0;
	int32 TargetFloor = 0;
	double PositionCm = 0.0;
	double DoorOpenFraction = 1.0;
	EHqElevatorPhase Phase = EHqElevatorPhase::Open;

	/** Etage 0 ist das Erdgeschoss; ungueltige Etagen veraendern nichts. */
	bool RequestFloor(int32 Floor);
	void Tick(double DeltaSeconds);
	bool IsStoppedAt(int32 Floor) const;

private:
	double OpenTimeRemaining = 5.0;
};
