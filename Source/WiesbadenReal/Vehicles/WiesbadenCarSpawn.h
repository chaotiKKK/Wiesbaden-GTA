// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/RoadNetworkTypes.h"

/**
 * Startpunkt des Spielerfahrzeugs.
 *
 * Der gewuenschte Ort ist eine Adresse, das Fahrzeug muss aber auf der
 * Fahrbahn stehen - nicht im Gebaeude. Die Adresse liefert daher nur den
 * Zielpunkt; die tatsaechliche Position wird auf die naechstgelegene
 * befahrbare Spur gesetzt und in deren Fahrtrichtung ausgerichtet.
 *
 * Datenrein und ohne Welt pruefbar: die Spursuche ist reine Geometrie auf dem
 * bereits erzeugten Strassennetz.
 */
struct WIESBADENREAL_API FWiesbadenCarSpawn
{
	/**
	 * Platter Strasse 144, 65193 Wiesbaden.
	 *
	 * Ermittelt aus dem Schwerpunkt des Gebaeudeumrisses in den OSM-Daten
	 * (Way 182281265, building=apartments, addr:housenumber=144). Bewusst als
	 * Konstante hinterlegt statt zur Laufzeit ueber die Adresse gesucht: die
	 * Adresssuche ueber 119.000 Gebaeude waere fuer einen festen Startpunkt
	 * unverhaeltnismaessig, und die Koordinate aendert sich nicht.
	 */
	static constexpr double PlatterStrasse144Latitude = 50.0932604;
	static constexpr double PlatterStrasse144Longitude = 8.2234186;

	/** Hoehe ueber der Fahrbahn beim Einsetzen, in cm. */
	static constexpr double SpawnHeightOffsetCm = 20.0;

	/**
	 * Sucht den naechstgelegenen Punkt auf einer befahrbaren Fahrspur.
	 *
	 * @param Network       Erzeugtes Strassennetz.
	 * @param TargetWorld   Zielpunkt (Weltkoordinaten, cm) - typischerweise
	 *                      der Gebaeudeschwerpunkt der Zieladresse.
	 * @param MaxRadiusCm   Suchradius. Findet sich in diesem Umkreis keine
	 *                      Spur, schlaegt die Suche fehl, statt das Fahrzeug
	 *                      irgendwohin zu setzen.
	 * @param OutLocation   Position auf der Spurmitte, um SpawnHeightOffsetCm
	 *                      angehoben.
	 * @param OutRotation   Ausrichtung in Fahrtrichtung der Spur.
	 * @param OutLaneId     Gefundene Spur, INDEX_NONE bei Misserfolg.
	 * @return false, wenn keine befahrbare Spur im Radius liegt.
	 */
	static bool FindNearestDrivableLanePoint(
		const FRoadNetwork& Network,
		const FVector& TargetWorld,
		double MaxRadiusCm,
		FVector& OutLocation,
		FRotator& OutRotation,
		int32& OutLaneId);

	/**
	 * Naechster Punkt auf einem Streckenzug samt Richtung dort.
	 *
	 * Der Abstand wird HORIZONTAL gemessen (nur X/Y), der zurueckgegebene
	 * Punkt behaelt jedoch die Hoehe des Streckenzugs. Der Zielpunkt stammt
	 * aus einer Adresse und hat keine brauchbare Hoehe, waehrend die Spuren
	 * auf Terrainhoehe liegen - am Hang sind das in Wiesbaden ueber 100 m
	 * Unterschied, die eine 3D-Messung vollstaendig dominieren wuerden.
	 *
	 * @return Quadrierter horizontaler Abstand.
	 */
	static double DistanceToPolylineSquared(
		const TArray<FVector>& Polyline,
		const FVector& Point,
		FVector& OutClosest,
		FVector& OutDirection);
};
