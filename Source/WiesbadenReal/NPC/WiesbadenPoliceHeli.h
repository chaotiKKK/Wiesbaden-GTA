// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Zustand des Heli-Verfolgers (Position + Sichtkontakt). Reiner Wert. */
struct FWiesbadenPoliceHeliState
{
	FVector Position = FVector::ZeroVector;

	/** Spieler aus der Luft im Blick (Hysterese zwischen Spot- und Lose-Radius). */
	bool bSpotted = false;
};

/** Parameter des Heli-Verfolgers (cm). */
struct FWiesbadenPoliceHeliParams
{
	double HoverHeightCm = 12000.0;   // 120 m ueber dem Spieler
	double FollowRadiusCm = 6000.0;   // 60 m Seitenabstand (nicht direkt darueber)
	double MaxSpeedCmPerSec = 2500.0; // ~90 km/h
	double SpotRadiusCm = 25000.0;    // 250 m: Sichtkontakt aufnehmen
	double LoseRadiusCm = 32000.0;    // 320 m: Spur verlieren (Hysterese > Spot)
};

/**
 * Reiner Luft-Verfolger - kein Welt-/Tick-Zugriff, direkt unit-testbar
 * (Muster FWiesbadenPursuer). Der Actor (AWiesbadenPoliceHelicopter) ruft
 * Step() je Tick und uebernimmt Position/Sichtkontakt; die ECHTE Strahlen-
 * Sichtpruefung (WiesbadenPolice::CanSee) schraenkt bSpotted dort weiter ein,
 * damit Gebaeude die Luftverfolgung nicht sinnlos machen.
 *
 * Schweben statt Verfolgen am Boden: Ziel ist der Spieler in Hoehe
 * HoverHeightCm mit einem Seitenabstand FollowRadiusCm - ein Heli direkt
 * ueber dem Spieler verdeckt nur die eigene Sichtachse.
 */
class WIESBADENREAL_API FWiesbadenPoliceHeli
{
public:
	/**
	 * Ein Verfolgungs-Schritt: bewegt den Heli gedampft (gedeckelt auf
	 * MaxSpeed*dt) auf die Schwebeposition ueber dem Spieler, haelt den
	 * Seitenabstand und fuehrt den Sichtkontakt mit Hysterese.
	 */
	static FWiesbadenPoliceHeliState Step(const FWiesbadenPoliceHeliState& Current,
		const FVector& PlayerPos, const FWiesbadenPoliceHeliParams& Params,
		double DeltaSeconds);
};
