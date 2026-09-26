// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Geometrie des Trennens mit dem Plasmacutter, datenrein.
 *
 * Das Dead-Space-Prinzip: der Cutter trennt entlang einer GERADEN Kante. Die
 * Schnittebene steht senkrecht auf dem Blick und ist um den Winkel
 * CutPlaneAngleDeg um die Blickachse gedreht; das Mausrad dreht sie in Rasten
 * (FWiesbadenWeaponSpec::CutAngleStepDeg). Ein vorbereitetes Stueck faellt
 * ab, wenn sein Schwerpunkt auf der negativen Seite der Ebene liegt.
 *
 * BEWUSST kein Slicing beliebiger Meshes zur Laufzeit: das ist teuer und die
 * Kante sieht dabei schlecht aus. Hier wird nur entschieden, WELCHES
 * vorbereitete Stueck abfaellt - deshalb ist die Regel eine reine
 * Geometriefunktion und ohne Welt pruefbar.
 */
namespace WiesbadenCutMath
{
	/**
	 * Normale der Schnittebene: um die Blickachse (AimDirection) gedreht.
	 *
	 * Bei 0 Grad ist die Kante senkrecht (die Ebene enthaelt die Blickachse
	 * und die Hoehe), bei 90 Grad waagerecht. Ergebnis ist immer
	 * Einheitslaenge; bei degenerierter Blickrichtung waagerecht nach oben.
	 */
	WIESBADENREAL_API FVector CutPlaneNormal(const FVector& AimDirection, float AngleDeg);

	/** Vorzeichenbehafteter Abstand eines Punkts von der Ebene (cm). */
	WIESBADENREAL_API float SignedDistanceToPlane(
		const FVector& Point, const FVector& PlanePoint, const FVector& PlaneNormal);

	/**
	 * Gehoert ein Stueck zur abfallenden Seite? Der Schwerpunkt entscheidet -
	 * ein Stueck, das die Ebene nur streift, bleibt stehen.
	 */
	WIESBADENREAL_API bool ShouldDetach(
		const FVector& PieceCentroid, const FVector& PlanePoint, const FVector& PlaneNormal);

	/**
	 * Schnittebene weiterdrehen (Mausrad-Rasten): bleibt immer im Kreis
	 * 0..<360 Grad, auch bei rueckwaerts drehen und bei Spruengen um mehrere
	 * Rasten (starkes Scrollen).
	 */
	WIESBADENREAL_API float RotateCutPlane(float AngleDeg, float StepDeg);
}
