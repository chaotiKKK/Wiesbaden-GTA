// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Missions/WiesbadenMissionTypes.h"

/**
 * Parameter des fairen Zeitfensters. Bewusst grosszuegig, damit jede befristete
 * Mission SCHAFFBAR bleibt: langsames Reisetempo + fester Puffer + Untergrenze.
 */
struct FMissionDeadlineParams
{
	/** Faires Reisetempo in cm/s (~25 km/h effektiv - City mit Ampeln/Kurven). */
	double PaceCmPerSecond = 700.0;

	/** Fester Zuschlag in s: Anfahrt-Reaktion, Parken, Verkehr. */
	double BufferSeconds = 30.0;

	/** Untergrenze in s, damit sehr kurze Routen nicht unfair knapp werden. */
	double MinSeconds = 45.0;
};

/**
 * Datenreine Berechnung eines fairen Missions-Zeitlimits aus der Route. Kein
 * Welt-/Tick-Zugriff (nur FVector/TArray) -> direkt unit-testbar. Das Subsystem
 * ruft ComputeSeconds beim Missionsstart, wenn deadline_seconds < 0 ("auto")
 * gesetzt ist, und ersetzt den Platzhalter durch das berechnete Fenster.
 */
class WIESBADENREAL_API FWiesbadenMissionDeadline
{
public:
	/**
	 * Faires Zeitfenster (Sekunden) fuer die Route Start -> Ziel0 -> ... -> ZielN.
	 * Distanz PLANAR (2D, Hoehe ignoriert) / Tempo + Puffer, nach unten auf
	 * MinSeconds begrenzt. Ohne Ziele -> MinSeconds.
	 */
	static double ComputeSeconds(
		const FVector& Start,
		const TArray<FMissionObjective>& Objectives,
		const FMissionDeadlineParams& Params);
};
