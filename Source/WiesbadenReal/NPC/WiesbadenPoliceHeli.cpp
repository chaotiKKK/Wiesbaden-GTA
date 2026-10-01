// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "NPC/WiesbadenPoliceHeli.h"

FWiesbadenPoliceHeliState FWiesbadenPoliceHeli::Step(const FWiesbadenPoliceHeliState& Current,
	const FVector& PlayerPos, const FWiesbadenPoliceHeliParams& Params, double DeltaSeconds)
{
	FWiesbadenPoliceHeliState Next = Current;

	// Seitenabstand: Richtung vom Spieler zur aktuellen Heli-XY. Direkt
	// darueber (Delta ~ 0) waehlt die Modell eine Vorgabe-Richtung (+X), sonst
	// wuerde der Seitenabstand zufaellig um den Spieler kreisen.
	FVector2D Abstand = FVector2D(Current.Position.X - PlayerPos.X, Current.Position.Y - PlayerPos.Y);
	if (Abstand.SizeSquared() < FMath::Square(100.0))
	{
		Abstand = FVector2D(Params.FollowRadiusCm, 0.0);
	}
	else
	{
		Abstand = Abstand.GetSafeNormal() * Params.FollowRadiusCm;
	}

	const FVector Ziel(
		PlayerPos.X + Abstand.X,
		PlayerPos.Y + Abstand.Y,
		PlayerPos.Z + Params.HoverHeightCm);

	// Gedampft auf das Ziel zu, gedeckelt auf MaxSpeed*dt (kein Ueberschwingen).
	const FVector Delta = Ziel - Current.Position;
	const double Distanz = Delta.Size();
	const double Schritt = FMath::Min(Distanz, Params.MaxSpeedCmPerSec * DeltaSeconds);
	if (Distanz > UE_KINDA_SMALL_NUMBER && Schritt > 0.0)
	{
		Next.Position = Current.Position + Delta / Distanz * Schritt;
	}

	// Sichtkontakt mit Hysterese zwischen Spot- und Lose-Radius.
	const double Planar = FVector2D::Distance(
		FVector2D(Next.Position.X, Next.Position.Y), FVector2D(PlayerPos.X, PlayerPos.Y));
	if (Planar < Params.SpotRadiusCm)
	{
		Next.bSpotted = true;
	}
	else if (Planar > Params.LoseRadiusCm)
	{
		Next.bSpotted = false;
	}

	return Next;
}
