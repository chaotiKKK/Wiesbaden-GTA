// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/GeoCoordinateConverter.h"

/** Der feste Standort des SebboTower; Form und Zugang bleiben davon getrennt. */
namespace SebboHqSite
{
	inline constexpr double Latitude = 50.093950;
	inline constexpr double Longitude = 8.224490;
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
