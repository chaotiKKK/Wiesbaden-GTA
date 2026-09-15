// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Ziele der Dev-Teleports. Index == -ExecCmds-Argument (WbTeleport <n>).
enum class EWiesbadenDevTeleport : uint8
{
	PlatterStrasse   = 0,
	Nerobergbahn     = 1,
	GartenNerotal48  = 2,
};

// Datenreine Kernlogik der Dev-Aktionen: keine Welt, kein Pawn - damit unter
// Automation testbar. HUD und Konsole rufen dieselben Funktionen (DRY).
struct WIESBADENREAL_API FWiesbadenDevActions
{
	// Zielpunkt in cm (bekannte Weltkoordinaten), ohne Fallhoehe.
	static FVector TeleportTargetCm(EWiesbadenDevTeleport Target);

	// Zielpunkt plus 300 cm Fallhoehe (ein Punkt IM Boden liesse den Wagen
	// steckenbleiben).
	static FVector TeleportSpawnCm(EWiesbadenDevTeleport Target);

	// Aufrichten: Nick/Roll auf 0, Yaw + Ort behalten, 150 cm anheben.
	static FTransform UprightTransform(const FTransform& Current);

	// Parst "X,Y,Z" (Weltkoordinaten in cm) in einen Vektor. False bei falscher
	// Feldzahl oder nicht-endlichen Werten. Fuer das Dev-Flag -WbTeleportTo=<X,Y,Z>,
	// das Pawn + Streaming-Quelle an eine beliebige Koordinate setzt (gezielte
	// Aufnahmen ferner Bauwerke wie Tunnel/Bruecken).
	static bool ParseWorldTarget(const FString& Spec, FVector& OutCm);
};
