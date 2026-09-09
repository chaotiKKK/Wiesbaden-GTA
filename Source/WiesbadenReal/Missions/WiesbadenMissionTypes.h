// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

// Datenreine Missions-Typen (Teilprojekt 1 - Missions-Rueckgrat). Keine
// Engine-Abhaengigkeit ueber CoreMinimal hinaus, damit die Ziel-Logik ohne
// Welt/Tick unit-testbar ist. Spec:
// docs/superpowers/specs/2026-09-08-missions-framework-design.md

/** Ziel-Typen. v1 nur ReachLocation; spaeter Eliminate/Deliver/Survive. */
enum class EObjectiveType : uint8
{
	ReachLocation,
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

	/**
	 * True, wenn das Ziel im gegebenen Kontext erfuellt ist. ReachLocation
	 * misst HORIZONTAL (2D-Distanz), damit Hang/Hoehe nicht stoeren.
	 */
	bool IsComplete(const FMissionContext& Ctx) const;
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
