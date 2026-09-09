// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenMissionDeadline.h"

double FWiesbadenMissionDeadline::ComputeSeconds(
	const FVector& Start,
	const TArray<FMissionObjective>& Objectives,
	const FMissionDeadlineParams& Params)
{
	// Planare Routenlaenge: Start -> Ziel0 -> ... -> ZielN. Hoehe (Z) bleibt aussen
	// vor, damit Haenge/Etagen die Frist nicht verzerren (wie ReachLocation misst).
	double RouteCm = 0.0;
	FVector Prev = Start;
	for (const FMissionObjective& O : Objectives)
	{
		const double Dx = O.Location.X - Prev.X;
		const double Dy = O.Location.Y - Prev.Y;
		RouteCm += FMath::Sqrt(Dx * Dx + Dy * Dy);
		Prev = O.Location;
	}

	const double Pace = FMath::Max(1.0, Params.PaceCmPerSecond); // Division absichern
	const double Window = RouteCm / Pace + Params.BufferSeconds;
	return FMath::Max(Window, Params.MinSeconds);
}
