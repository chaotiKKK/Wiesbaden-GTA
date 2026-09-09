// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Verhaltens-Modus eines Verfolgers. */
enum class EWiesbadenPursuerMode : uint8
{
	Idle,     // Spieler nicht in Sichtweite -> steht still
	Chasing,  // Spieler entdeckt -> faehrt auf ihn zu
	Caught,   // Spieler eingeholt (im Fang-Radius)
};

/** Zustand eines Verfolgers (Position + Modus). Reiner Wert. */
struct FWiesbadenPursuerState
{
	FVector Position = FVector::ZeroVector;
	EWiesbadenPursuerMode Mode = EWiesbadenPursuerMode::Idle;
};

/** Parameter des Verfolger-Verhaltens (Radien planar, in cm). */
struct FWiesbadenPursuerParams
{
	double DetectRadiusCm = 5000.0;    // 50 m: entdeckt den Spieler -> Verfolgung
	double LoseRadiusCm = 9000.0;      // 90 m: verliert die Spur (Hysterese > Detect)
	double CatchRadiusCm = 400.0;      // 4 m: eingeholt
	double MaxSpeedCmPerSec = 1400.0;  // ~50 km/h
};

/**
 * Reine, reaktive Verfolger-Logik - kein Welt-/Tick-Zugriff, damit direkt
 * unit-testbar (Muster wie FWiesbadenMissionRunner). Der Actor ruft Step() je
 * Tick mit der aktuellen Spielerposition und uebernimmt Position/Modus.
 */
class WIESBADENREAL_API FWiesbadenPursuer
{
public:
	/**
	 * Ein Verfolgungs-Schritt: entdeckt/verliert den Spieler (Hysterese zwischen
	 * Detect- und Lose-Radius), faehrt im Chasing planar auf ihn zu (gedeckelt auf
	 * MaxSpeed*dt, ohne Ueberschwingen), meldet Caught im Fang-Radius. Hoehe (Z)
	 * bleibt unveraendert - Bodenkontakt macht der Actor.
	 */
	static FWiesbadenPursuerState Step(
		const FWiesbadenPursuerState& Current, const FVector& PlayerPos,
		const FWiesbadenPursuerParams& Params, double DeltaSeconds);
};
