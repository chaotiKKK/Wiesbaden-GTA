// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "NPC/WiesbadenWanted.h"

int32 FWiesbadenWanted::LevelOf(const FWiesbadenWantedParams& Params, double Points)
{
	int32 Level = 0;
	for (int32 Step = 0; Step < 6; ++Step)
	{
		if (Points >= Params.LevelThresholds[Step])
		{
			Level = Step + 1;
		}
	}
	return Level;
}

FWiesbadenWantedState FWiesbadenWanted::AddEvent(const FWiesbadenWantedState& Current,
	const FWiesbadenWantedParams& Params, EWiesbadenCrimeEvent Event)
{
	double Points = 0.0;
	switch (Event)
	{
	case EWiesbadenCrimeEvent::ShotFired:         Points = Params.ShotFiredPoints; break;
	case EWiesbadenCrimeEvent::PedestrianDowned:  Points = Params.PedestrianDownedPoints; break;
	case EWiesbadenCrimeEvent::VehicleDestroyed:  Points = Params.VehicleDestroyedPoints; break;
	case EWiesbadenCrimeEvent::OfficerHit:        Points = Params.OfficerHitPoints; break;
	default:                                      Points = 0.0; break;
	}

	FWiesbadenWantedState Next = Current;
	// Deterministisch, kein Ueberlauf: Konto deckelt am Maximum.
	Next.Points = FMath::Min(Current.Points + Points, Params.MaxPoints);
	Next.Level = LevelOf(Params, Next.Points);
	Next.SecondsSinceEvent = 0.0;
	return Next;
}

FWiesbadenWantedState FWiesbadenWanted::Step(const FWiesbadenWantedState& Current,
	const FWiesbadenWantedParams& Params, double DeltaSeconds)
{
	FWiesbadenWantedState Next = Current;
	Next.SecondsSinceEvent = Current.SecondsSinceEvent
		+ FMath::Max(DeltaSeconds, 0.0);

	// Abbau erst nach der Grace: Wer fertig ist, hat kurz noch das Konto am
	// Hals; ohne Grace wuerde der Abbau zwischen zwei Schuessen desselben
	// Feuerstosses schon angreifen. Danach pro Tick das volle Delta -
	// deterministisch und ohne Rueckblick auf die bisherige Laufzeit.
	if (Next.SecondsSinceEvent > Params.DecayGraceSeconds && Next.Points > 0.0)
	{
		Next.Points = FMath::Max(0.0,
			Next.Points - Params.DecayPointsPerSecond * FMath::Max(DeltaSeconds, 0.0));
		Next.Level = LevelOf(Params, Next.Points);
	}
	return Next;
}
