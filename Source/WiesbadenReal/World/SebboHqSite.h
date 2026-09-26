// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/GeoCoordinateConverter.h"

/** Der feste Standort des SebboTower; Form und Zugang bleiben davon getrennt. */
namespace SebboHqSite
{
	// Die Front zeigt zur Platter Strasse. Gegenueber dem alten, zur
	// Wolkenbruch gewandten Standort liegt der Mittelpunkt 28 m westlich und
	// 20 m suedlich. Die Vermessung der OSM-Geometrie haelt den 34-m-Sockel
	// knapp noerdlich des oeffentlichen Fuss-/Radwegs 35825899; beide
	// Eingangsanker bleiben auf der Platter-Seite dieses Wegs.
	// Die Koordinate wird von Tools/sebbo_standort.py aus diesem Header gelesen.
	inline constexpr double Latitude = 50.093702144;
	inline constexpr double Longitude = 8.224136765;
	inline constexpr double HeadingDegrees = 120.0;
	inline constexpr TCHAR AccessRoadName[] = TEXT("Platter Stra\u00dfe");

	inline FGeoCoordinate Coordinate()
	{
		return FGeoCoordinate(Longitude, Latitude, 0.0);
	}

	inline FRotator Heading()
	{
		return FRotator(0.0, HeadingDegrees, 0.0);
	}
}
