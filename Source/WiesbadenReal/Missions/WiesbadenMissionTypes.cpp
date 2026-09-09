// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenMissionTypes.h"

bool FMissionObjective::IsComplete(const FMissionContext& Ctx) const
{
	// Horizontal messen: Hoehe/Hang darf nicht ueber die Erfuellung entscheiden
	// (dieselbe Wahl wie bei der Gebaeude-Kollision/Bahn).
	const double Dx = Ctx.PlayerLocation.X - Location.X;
	const double Dy = Ctx.PlayerLocation.Y - Location.Y;
	const double DistSq = Dx * Dx + Dy * Dy;
	const double RadiusSq = RadiusCm * RadiusCm;

	switch (Type)
	{
	case EObjectiveType::ReachLocation:
		return DistSq <= RadiusSq; // im Radius angekommen
	case EObjectiveType::LeaveArea:
		return DistSq >= RadiusSq; // Gebiet verlassen (Fluchtpunkt)
	case EObjectiveType::Dwell:
		return Ctx.SecondsInRadius >= HoldSeconds; // lange genug gehalten
	case EObjectiveType::PickUpCargo:
		return DistSq <= RadiusSq; // an der Aufnahme angekommen -> Fracht aufnehmen
	case EObjectiveType::DropOffCargo:
		return DistSq <= RadiusSq && Ctx.bCarryingCargo; // am Ziel UND mit Fracht
	default:
		return false;
	}
}

double FMissionObjective::AdvanceDwell(double CurrentDwellSeconds,
	const FVector& PlayerLocation, double DeltaSeconds) const
{
	// Horizontal messen (wie IsComplete). Im Radius: Verweildauer aufaddieren.
	// Ausserhalb: zuruecksetzen - "halten" heisst ununterbrochen drinbleiben.
	const double Dx = PlayerLocation.X - Location.X;
	const double Dy = PlayerLocation.Y - Location.Y;
	const bool bInside = (Dx * Dx + Dy * Dy) <= (RadiusCm * RadiusCm);
	return bInside ? CurrentDwellSeconds + DeltaSeconds : 0.0;
}
