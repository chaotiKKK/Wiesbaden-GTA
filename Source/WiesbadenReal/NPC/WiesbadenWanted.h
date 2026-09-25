// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Verbrechens-Ereignis, das die Polizei wahrnehmen kann.
 *
 * Die Welt-Anbindung entscheidet, OB ein Beamter das Ereignis ueberhaupt
 * sieht (Sichtweite/Entfernung) - diese Ebene rechnet nur die Folgen.
 */
enum class EWiesbadenCrimeEvent : uint8
{
	ShotFired,          // Schuss (in Sicht-/Hoerweite eines Beamten)
	PedestrianDowned,   // Passant getroffen (Zu-Boden-Zustand des Shooters)
	VehicleDestroyed,   // Fahrzeug zerstoert
	OfficerHit,         // Beamter getroffen
};

/** Zustand des Fahndungskontos (reiner Wert, datenrein testbar). */
struct FWiesbadenWantedState
{
	/** Punktekonto; die Stufe folgt daraus, sie wird mitgefuehrt. */
	double Points = 0.0;

	/** Stufe 0..6 (SPEC §8: Polizei -> SEK -> Heli -> BFE+ -> GSG9/Erbenheim). */
	int32 Level = 0;

	/** Sekunden seit dem letzten Ereignis (Grace vor dem Abbau). */
	double SecondsSinceEvent = 0.0;
};

/** Gewichte, Schwellen und Abbau des Kontos. */
struct FWiesbadenWantedParams
{
	// -- Punkte je Ereignis ------------------------------------------------
	double ShotFiredPoints = 25.0;
	double PedestrianDownedPoints = 45.0;
	double VehicleDestroyedPoints = 60.0;
	double OfficerHitPoints = 90.0;

	// -- Schwellen je Stufe 1..6 -------------------------------------------
	// Stufe 6 ist bewusst schwer: sie bleibt den grossen Einsaetzen (SEK,
	// Heli, Erbenheim) vorbehalten und ist in dieser Runde noch nicht belegt.
	double LevelThresholds[6] = { 20.0, 60.0, 120.0, 220.0, 340.0, 500.0 };

	/** Erst so viele Sekunden nach dem letzten Ereignis sinkt das Konto. */
	double DecayGraceSeconds = 30.0;

	/** Abbautempo nach der Grace (Punkte je Sekunde). */
	double DecayPointsPerSecond = 1.0;

	/** Hartes Konto-Maximum. */
	double MaxPoints = 600.0;
};

/**
 * Fahndungslevel, datenrein (Muster FWiesbadenPursuer: keine Welt-Zugriffe,
 * Step/AddEvent liefert den Folgezustand; der Aufrufer uebernimmt ihn).
 *
 * Deterministisch: keine Zufaelligkeit - dieselben Ereignisse fuehren immer
 * zum selben Konto. Stufe 6 benoetigt 500 Punkte und ist damit praktisch
 * nur durch anhaltende Straftaten erreichbar.
 */
class WIESBADENREAL_API FWiesbadenWanted
{
public:
	/** Stufe zu Punktestand (statisch, fuer Tests und Anzeige). */
	static int32 LevelOf(const FWiesbadenWantedParams& Params, double Points);

	/** Ereignis verbuchen; liefert den neuen Zustand (Grace startet neu). */
	static FWiesbadenWantedState AddEvent(const FWiesbadenWantedState& Current,
		const FWiesbadenWantedParams& Params, EWiesbadenCrimeEvent Event);

	/** Tick: Grace abwarten, dann Abbau; Stufe in jedem Fall nachziehen. */
	static FWiesbadenWantedState Step(const FWiesbadenWantedState& Current,
		const FWiesbadenWantedParams& Params, double DeltaSeconds);
};
