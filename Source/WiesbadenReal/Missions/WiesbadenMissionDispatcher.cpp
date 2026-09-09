// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenMissionDispatcher.h"

namespace
{
	// Alle ReachLocation-Zielorte des Pools als Routing-Punkte fuer die
	// prozeduralen Auftraege einsammeln (in Pool-Reihenfolge).
	void GatherPoints(const TArray<FMission>& Pool, TArray<FVector>& OutPoints)
	{
		for (const FMission& M : Pool)
		{
			for (const FMissionObjective& O : M.Objectives)
			{
				if (O.Type == EObjectiveType::ReachLocation)
				{
					OutPoints.Add(O.Location);
				}
			}
		}
	}

	FMissionObjective MakeReach(const FVector& Loc, const TCHAR* Label)
	{
		FMissionObjective O;
		O.Type = EObjectiveType::ReachLocation;
		O.Location = Loc;
		O.RadiusCm = 800.0;
		O.Label = Label;
		return O;
	}
}

FMissionDispatchResult FWiesbadenMissionDispatcher::NextMission(
	const TArray<FMission>& Pool, int32 CompletedCount)
{
	FMissionDispatchResult Result;
	if (Pool.Num() == 0 || CompletedCount < 0)
	{
		return Result; // ohne Vorlagen / bei Unsinn keine Vergabe
	}

	// Phase 1: handgeschriebene Auftraege in Reihenfolge.
	if (CompletedCount < Pool.Num())
	{
		Result.bHasMission = true;
		Result.Mission = Pool[CompletedCount];
		return Result;
	}

	// Phase 2: endlos prozedurale Kurierjobs aus den bekannten Orten. Deterministisch
	// aus dem Cursor abgeleitet - kein Zufall -, damit derselbe Cursor stets denselben
	// Auftrag ergibt (unit-testbar, und ein Neuladen liefert nichts Ueberraschendes).
	TArray<FVector> Points;
	GatherPoints(Pool, Points);
	if (Points.Num() == 0)
	{
		return Result; // keine Orte zum Routen
	}

	const int32 Seq = CompletedCount - Pool.Num(); // 0, 1, 2, ...
	const int32 PickupIdx = (Seq * 2) % Points.Num();
	const FVector Pickup = Points[PickupIdx];

	FVector Delivery;
	if (Points.Num() >= 2)
	{
		int32 DeliveryIdx = (Seq * 2 + 1) % Points.Num();
		if (DeliveryIdx == PickupIdx)
		{
			DeliveryIdx = (DeliveryIdx + 1) % Points.Num();
		}
		Delivery = Points[DeliveryIdx];
	}
	else
	{
		// Nur ein bekannter Ort: Ziel 800 m oestlich synthetisieren, damit der
		// Auftrag eine echte Fahrt bleibt und nicht sofort als erfuellt gilt.
		Delivery = Pickup + FVector(80000.0, 0.0, 0.0);
	}

	FMission M;
	M.Id = FName(*FString::Printf(TEXT("kurier_auto_%d"), Seq + 1));
	M.Title = FString::Printf(TEXT("Kurierfahrt Nr. %d"), CompletedCount + 1);
	M.Reward.Guthaben = 250;
	// Auto-Frist: das Subsystem rechnet beim Start eine faire, distanzabhaengige
	// Deadline aus der Route. So sind auch die endlosen prozeduralen Jobs befristet.
	M.DeadlineSeconds = FMission::AutoDeadline;
	M.Objectives.Add(MakeReach(Pickup, TEXT("Fahre zur Abholung")));
	M.Objectives.Add(MakeReach(Delivery, TEXT("Liefere die Sendung")));

	Result.bHasMission = true;
	Result.bProcedural = true;
	Result.Mission = M;
	return Result;
}
