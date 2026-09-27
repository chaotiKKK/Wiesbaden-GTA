// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Projektil-Flugbahn, datenrein (kein Weltzugriff, unit-testbar).
 *
 * Der Shooter-Entwurf 2026-09 verlangt ECHTE Flugzeit und Gravitation statt
 * des bisherigen Sofort-Strahls in UWiesbadenWeaponComponent::Fire. Diese
 * Ebene rechnet nur: Position nach dt, Kollisions-Sweep ueber das
 * Bewegungssegment, Aufschlagpunkt/Einschlagsgeschwindigkeit. Die Welt-
 * Anbindung (Waffen-Komponente) sammelt die Trefferkandidaten.
 */
struct FWiesbadenProjectile
{
	FVector Position = FVector::ZeroVector;   // cm, Welt
	FVector Velocity = FVector::ZeroVector;   // cm/s
	float RemainingRangeCm = 0.0f;            // bis Reichweitenende
	float MassKg = 0.0f;
	float GravityCmPerS2 = 980.665f;
	float Damage = 0.0f;
	bool bExplosive = false;
	float BlastRadiusCm = 0.0f;
	float BlastDamage = 0.0f;
	/** Voller Eigenschaden des Schuetzen bei Detonation (Transport nach Fire). */
	float SelfDamage = 0.0f;
};

/** Ergebnis eines Integrationsschritts. */
struct FWiesbadenFlightStep
{
	/** Neuer Zustand (Position/Velocity/Restreichweite). */
	FWiesbadenProjectile Projectile;

	/** Segment von alter zu neuer Position (fuer den Welt-Sweep). */
	FVector SegmentStart = FVector::ZeroVector;
	FVector SegmentEnd = FVector::ZeroVector;

	/** true = Aufschlag in diesem Schritt (Reichweite aufgebraucht). */
	bool bRangeEnd = false;
};

/**
 * Flugbahn-Mathematik (statisch, deterministisch; Muster FWiesbadenPursuer).
 */
namespace WiesbadenBallistics
{
	/**
	 * Ein Schritt: Geschwindigkeit um g*dt integrieren, Position verschieben,
	 * Restreichweite kuerzen. SegmentStart/End fuer den Sweep des Aufrufers.
	 */
	WIESBADENREAL_API FWiesbadenFlightStep Step(const FWiesbadenProjectile& Current,
		double DeltaSeconds);

	/** Ziel in Linie (planar, Radius in cm): echte Zielsuche vor dem Welt-Sweep. */
	WIESBADENREAL_API bool SegmentHitsSphere(const FVector& Start, const FVector& End,
		const FVector& Centre, double RadiusCm);

	/** Geschwindigkeit beim Aufschlag (Betrag, cm/s) - fuer Impuls und Audio. */
	WIESBADENREAL_API double ImpactSpeed(const FWiesbadenProjectile& P);

	/**
	 * Explosions-Schaden an einem Punkt: linear vom Zentrum (BlastDamage)
	 * zum Radius (0); ausserhalb 0. Datanrein fuer Tests und beide Nutzer.
	 */
	WIESBADENREAL_API float BlastDamageAt(const FVector& BlastCentre,
		float BlastRadiusCm, float BlastDamage, const FVector& TargetPoint);

	/**
	 * Erwartete Fallhoehe ueber Distanz (cm) - fuer Zielerfassung-Anzeige und
	 * Tests der Gravitation: h = g * t^2 / 2 mit t = s / v.
	 */
	WIESBADENREAL_API double DropAtDistance(double HorizontalDistanceCm,
		double VelocityCmPerS, double GravityCmPerS2);
}
