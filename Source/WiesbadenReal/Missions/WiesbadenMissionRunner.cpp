// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenMissionRunner.h"

FMissionProgressResult FWiesbadenMissionRunner::Step(
	const FMission& Mission,
	int32 CurrentObjectiveIndex,
	const FMissionContext& Ctx)
{
	FMissionProgressResult Result;
	Result.NextObjectiveIndex = CurrentObjectiveIndex;

	if (!Mission.Objectives.IsValidIndex(CurrentObjectiveIndex))
	{
		return Result; // ungueltiger Index -> nichts zu tun
	}
	if (!Mission.Objectives[CurrentObjectiveIndex].IsComplete(Ctx))
	{
		return Result; // aktuelles Ziel noch offen
	}

	// Ziel erfuellt -> ein Ziel vorruecken.
	Result.bAdvanced = true;
	Result.NextObjectiveIndex = CurrentObjectiveIndex + 1;
	if (Result.NextObjectiveIndex >= Mission.Objectives.Num())
	{
		Result.bMissionCompleted = true;
		Result.GuthabenAwarded = Mission.Reward.Guthaben;
	}
	return Result;
}
