// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

// Datenreine Missions-Typen (Teilprojekt 1 - Missions-Rueckgrat). Keine
// Engine-Abhaengigkeit ueber CoreMinimal hinaus, damit die Ziel-Logik ohne
// Welt/Tick unit-testbar ist. Spec:
// docs/superpowers/specs/2026-09-08-missions-framework-design.md

/** Ziel-Typen. ReachLocation (ankommen), LeaveArea (Gebiet verlassen, Flucht),
 *  Dwell (X Sekunden im Radius halten), PickUpCargo/DropOffCargo (Fracht mit
 *  mitgefuehrtem Zustand aufnehmen/abgeben); spaeter Eliminate/Survive. */
enum class EObjectiveType : uint8
{
	ReachLocation,
	LeaveArea,
	Dwell,
	PickUpCargo,
	DropOffCargo,
};

/**
 * Laufzeit-Kontext fuer die Ziel-Erfuellung. Waechst spaeter um Kills/Zeit/
 * Inventar - ReachLocation braucht davon nur die Spielerposition.
 */
struct FMissionContext
{
	FVector PlayerLocation = FVector::ZeroVector;

	/** Vergangene Zeit seit Missionsstart in Sekunden - fuer Zeitlimit-Ziele. */
	double ElapsedSeconds = 0.0;

	/** Bisher ununterbrochen im Radius des aktuellen Ziels verbrachte Zeit
	 *  (Sekunden) - fuer Verweil-Ziele (Dwell). Vom Subsystem fortgeschrieben. */
	double SecondsInRadius = 0.0;

	/** Traegt der Spieler gerade die Missions-Fracht? - fuer DropOffCargo. Wird vom
	 *  Subsystem bei der Aufnahme (PickUpCargo) gesetzt und bei der Abgabe geloescht. */
	bool bCarryingCargo = false;
};

/**
 * Ein einzelnes Missions-Ziel. IsComplete ist rein datenbasiert (kein Welt-
 * zugriff) und damit direkt testbar.
 */
struct FMissionObjective
{
	EObjectiveType Type = EObjectiveType::ReachLocation;
	FVector Location = FVector::ZeroVector;
	double RadiusCm = 0.0;
	FString Label;

	/** Nur fuer Dwell: geforderte Verweildauer im Radius (Sekunden). */
	double HoldSeconds = 0.0;

	/**
	 * True, wenn das Ziel im gegebenen Kontext erfuellt ist. ReachLocation misst
	 * HORIZONTAL (2D-Distanz), damit Hang/Hoehe nicht stoeren; Dwell prueft die
	 * vom Subsystem gefuehrte Verweildauer (Ctx.SecondsInRadius).
	 */
	bool IsComplete(const FMissionContext& Ctx) const;

	/**
	 * Fortschreibung der Verweildauer fuer Dwell-Ziele: im Radius aufaddieren,
	 * ausserhalb auf 0 zuruecksetzen (kontinuierliches Halten). Rein/testbar; das
	 * Subsystem ruft dies je Tick und legt das Ergebnis in Ctx.SecondsInRadius.
	 */
	double AdvanceDwell(double CurrentDwellSeconds, const FVector& PlayerLocation,
		double DeltaSeconds) const;
};

/** Belohnung bei Missionsabschluss (v1 nur Guthaben). */
struct FMissionReward
{
	int32 Guthaben = 0;
};

/** Eine Mission: geordnete Ziel-Kette + Belohnung. */
struct FMission
{
	FName Id;
	FString Title;
	TArray<FMissionObjective> Objectives;
	FMissionReward Reward;

	/**
	 * Zeitlimit in Sekunden ab Missionsstart, in DREI Modi:
	 *   > 0  festes Limit,
	 *   0    unbefristet (kein Limit),
	 *   < 0  "auto" (AutoDeadline) - das Subsystem berechnet beim Start eine faire,
	 *        distanzabhaengige Frist und ersetzt diesen Platzhalter.
	 * Ueberschreiten vor Erfuellung -> GESCHEITERT (keine Belohnung), siehe
	 * FWiesbadenMissionRunner. Die Modi liest man ueber IsAutoDeadline()/HasDeadline(),
	 * nicht ueber rohe Vorzeichen-Vergleiche.
	 */
	double DeadlineSeconds = 0.0;

	/** Platzhalter fuer "Frist automatisch aus der Route berechnen" (Modus < 0). */
	static constexpr double AutoDeadline = -1.0;

	/** Soll die Frist beim Start distanzabhaengig berechnet werden? (Modus < 0) */
	bool IsAutoDeadline() const { return DeadlineSeconds < 0.0; }

	/** Gilt ein konkretes, positives Zeitlimit? (Modus > 0; 0/auto zaehlen nicht) */
	bool HasDeadline() const { return DeadlineSeconds > 0.0; }
};
