// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenMissionTypes.h"

bool FMissionObjective::IsComplete(const FMissionContext& Ctx) const
{
	switch (Type)
	{
	case EObjectiveType::ReachLocation:
	{
		// Horizontal messen: Hoehe/Hang darf nicht ueber die Erfuellung
		// entscheiden (dieselbe Wahl wie bei der Gebaeude-Kollision/Bahn).
		const double Dx = Ctx.PlayerLocation.X - Location.X;
		const double Dy = Ctx.PlayerLocation.Y - Location.Y;
		return (Dx * Dx + Dy * Dy) <= (RadiusCm * RadiusCm);
	}
	default:
		return false;
	}
}
