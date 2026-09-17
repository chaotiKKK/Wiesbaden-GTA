// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Datenreine Sonnenstands-Rechnung fuer die 24-h-Beleuchtung.
 *
 * Aus UTC-Zeit + Breite/Laenge folgt der ECHTE Sonnenstand (Hoehe ueber dem
 * Horizont, Azimut ab Nord ueber Ost) nach der NOAA-/Meeus-Naeherung
 * (~0,1 Grad genau - fuer Licht und Schatten mehr als genug). Damit steht die
 * Sonne im Spiel dort, wo sie in Wiesbaden zur lokalen Systemzeit wirklich
 * steht: im Juni geht sie gegen 21:30 unter, im Dezember gegen 16:30.
 * Kein Welt-/Actor-Zugriff, unit-getestet (Test Weather.Solar).
 */
namespace WiesbadenSolar
{
	/** Bezugspunkt der Stadt (Ursprung des Geo-Konverters). */
	constexpr double WiesbadenLatitudeDeg = 50.0824000;
	constexpr double WiesbadenLongitudeDeg = 8.2400000;

	struct FSunPosition
	{
		double ElevationDeg = 0.0;   // ueber dem Horizont (negativ = Nacht)
		double AzimuthDeg = 0.0;     // ab Nord, im Uhrzeigersinn (90 = Ost, 180 = Sued)
	};

	/** Sonnenstand zu einer UTC-Zeit an Breite/Laenge (Grad). */
	WIESBADENREAL_API FSunPosition ComputeSunPosition(const FDateTime& Utc,
		double LatitudeDeg, double LongitudeDeg);

	/** Lokale Uhrzeit als Stunden 0..24 (z. B. 14:30 -> 14,5). */
	WIESBADENREAL_API float LocalHours(const FDateTime& Local);

	/**
	 * Rotation der DirectionalLight fuer einen Sonnenstand. Unreal-Achsen der
	 * Stadt: +X = Ost, +Y = SUED (linkshaendig), +Z = oben. Das Licht zeigt
	 * VON der Sonne weg (Richtung des Lichtstrahls).
	 */
	WIESBADENREAL_API FRotator SunLightRotation(double ElevationDeg, double AzimuthDeg);
}
