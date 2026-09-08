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
};
