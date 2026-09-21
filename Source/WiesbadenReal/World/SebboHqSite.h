// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/GeoCoordinateConverter.h"

/** Der feste Standort des SebboTower; Form und Zugang bleiben davon getrennt. */
namespace SebboHqSite
{
	// 8 m von der Wolkenbruch zurueckgenommen - GEMESSEN, nicht geschaetzt.
	//
	// Der Turm stand in der Strasse. Die Zufahrtssonde hat die Fahrbahnachse
	// von Segment 54432 in Turmkoordinaten gelegt (Saved/Diagnose/
	// zufahrtsprobe.json, "fahrbahn_lokal"): sie laeuft entlang der ganzen
	// Eingangsfront 26 bis 349 cm INNERHALB der Fassade, mit 275 cm Breite
	// reicht die Fahrbahn 497 cm unter das Gebaeude. Deshalb fuehrte die
	// Garagenschuerze ins Nichts - die Strasse lag nicht vor ihr, sondern
	// hinter ihr, unter dem Erdgeschoss. Der Wagen kam trotzdem an, er fuhr
	// auf genau diesem verdeckten Stueck Fahrbahn.
	//
	// 497 cm nehmen das Gebaeude von der Fahrbahn herunter, 300 cm mehr
	// stellen die Schuerze (Half..Half+300) genau an die Fahrbahnkante:
	//
	//     innere Fahrbahnkante   1054 cm -> 1854 cm   (lokal, vom Mittelpunkt)
	//     Fassade                1551 cm
	//     Fuss der Schuerze      1851 cm
	//
	// Verschoben wird entgegen der Garagenrichtung (lokal -X, Weltrichtung
	// 70 Grad); die Zahlen loest Tools/sebbo_standort.py aus genau diesem
	// Header wieder auf.
	inline constexpr double Latitude = 50.093882;
	inline constexpr double Longitude = 8.224528;
	inline constexpr double HeadingDegrees = 250.0;

	inline FGeoCoordinate Coordinate()
	{
		return FGeoCoordinate(Longitude, Latitude, 0.0);
	}

	inline FRotator Heading()
	{
		return FRotator(0.0, HeadingDegrees, 0.0);
	}
}
