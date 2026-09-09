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

	// Erfuellung hat Vorrang vor der Frist: wer im letzten Moment liefert, gewinnt.
	if (Mission.Objectives[CurrentObjectiveIndex].IsComplete(Ctx))
	{
		Result.bAdvanced = true;
		Result.NextObjectiveIndex = CurrentObjectiveIndex + 1;
		if (Result.NextObjectiveIndex >= Mission.Objectives.Num())
		{
			Result.bMissionCompleted = true;
			Result.GuthabenAwarded = Mission.Reward.Guthaben;
		}
		return Result;
	}

	// Ziel noch offen: ist das Zeitlimit ueberschritten, gilt die Mission als
	// GESCHEITERT - keine Belohnung. Ohne Frist (HasDeadline() == false) nie.
	if (Mission.HasDeadline() && Ctx.ElapsedSeconds > Mission.DeadlineSeconds)
	{
		Result.bAdvanced = true;      // Zustandswechsel -> das Subsystem reagiert
		Result.bMissionFailed = true; // GuthabenAwarded bleibt 0
		return Result;
	}

	return Result; // Ziel offen, Frist noch nicht ueberschritten
}
