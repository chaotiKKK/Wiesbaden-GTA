// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "NPC/WiesbadenPursuer.h"

FWiesbadenPursuerState FWiesbadenPursuer::Step(
	const FWiesbadenPursuerState& Current, const FVector& PlayerPos,
	const FWiesbadenPursuerParams& Params, double DeltaSeconds)
{
	// Planare Distanz zum Spieler (Hoehe ignoriert, wie ueberall in der Missions-Naht).
	const double Dx = PlayerPos.X - Current.Position.X;
	const double Dy = PlayerPos.Y - Current.Position.Y;
	const double Dist = FMath::Sqrt(Dx * Dx + Dy * Dy);

	FWiesbadenPursuerState Next = Current;

	// Modus aus der Distanz + Hysterese: im Band zwischen Detect und Lose bleibt der
	// aktuelle Modus erhalten (einmal dran, verfolgt bis Lose; einmal weg, wartet bis Detect).
	if (Dist <= Params.CatchRadiusCm)
	{
		Next.Mode = EWiesbadenPursuerMode::Caught;
	}
	else if (Dist <= Params.DetectRadiusCm)
	{
		Next.Mode = EWiesbadenPursuerMode::Chasing;
	}
	else if (Dist >= Params.LoseRadiusCm)
	{
		Next.Mode = EWiesbadenPursuerMode::Idle;
	}
	else
	{
		Next.Mode = (Current.Mode == EWiesbadenPursuerMode::Idle)
			? EWiesbadenPursuerMode::Idle
			: EWiesbadenPursuerMode::Chasing;
	}

	// Bewegung nur beim Verfolgen: planar auf den Spieler zu, gedeckelt auf
	// MaxSpeed*dt und ohne Ueberschwingen (min mit der Restdistanz).
	if (Next.Mode == EWiesbadenPursuerMode::Chasing && Dist > KINDA_SMALL_NUMBER)
	{
		const double StepDist = FMath::Min(Dist, Params.MaxSpeedCmPerSec * DeltaSeconds);
		Next.Position.X = Current.Position.X + Dx / Dist * StepDist;
		Next.Position.Y = Current.Position.Y + Dy / Dist * StepDist;
	}

	return Next;
}
