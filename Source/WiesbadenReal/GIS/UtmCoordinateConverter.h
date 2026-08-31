// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/GeoCoordinateConverter.h"

/**
 * UTM <-> geographische Koordinaten (inverse und direkte Transversale
 * Mercator-Projektion).
 *
 * Gebraucht fuer amtliche deutsche Geodaten: ALKIS, ATKIS und die
 * Geobasisdaten der Laender liegen in ETRS89/UTM vor, waehrend die uebrige
 * Pipeline (OSM, SRTM, Georeferenzierung) mit geographischen Koordinaten
 * arbeitet. Ohne diese Umrechnung sind amtliche Daten nicht einsetzbar.
 *
 * ELLIPSOID: ETRS89 verwendet GRS80, die Pipeline WGS84. Beide haben dieselbe
 * grosse Halbachse (6378137 m) und unterscheiden sich in der Abplattung erst
 * in der elften Nachkommastelle - der daraus folgende Lagefehler liegt weit
 * unter einem Millimeter und wird hier bewusst vernachlaessigt.
 *
 * Nicht vernachlaessigt wird dagegen die Datumsverschiebung: ETRS89 ist auf
 * der eurasischen Platte fixiert, WGS84 folgt dem globalen Mittel. Die Platte
 * driftet rund 2,5 cm pro Jahr, seit der Definition 1989 also inzwischen etwa
 * 90 cm nach Nordost. Fuer eine Spielwelt ist das ohne Bedeutung - fuer
 * Vermessungszwecke waere es das nicht, deshalb steht es hier.
 */
struct WIESBADENREAL_API FUtmCoordinateConverter
{
	/** Grosse Halbachse GRS80/WGS84 in Metern. */
	static constexpr double SemiMajorAxis = 6378137.0;

	/** Erste numerische Exzentrizitaet zum Quadrat. */
	static constexpr double EccentricitySquared = 6.69437999014e-3;

	/** Massstabsfaktor am Mittelmeridian (UTM-Definition). */
	static constexpr double ScaleFactor = 0.9996;

	/** Ostwert-Verschiebung in Metern (UTM-Definition). */
	static constexpr double FalseEasting = 500000.0;

	/** UTM-Zone fuer Hessen und damit fuer Wiesbaden. */
	static constexpr int32 GermanyZone32 = 32;

	/**
	 * Mittelmeridian einer UTM-Zone in Grad.
	 * Zone 32 -> 9 Grad Ost.
	 */
	static double GetZoneCentralMeridian(int32 Zone)
	{
		return static_cast<double>(Zone) * 6.0 - 183.0;
	}

	/**
	 * UTM -> geographisch.
	 *
	 * @param Easting   Ostwert in Metern. In ALKIS-Daten steht die Zonennummer
	 *                  NICHT im Wert (kein "32" davor) - der Ostwert liegt bei
	 *                  Wiesbaden um 450.000.
	 * @param Northing  Nordwert in Metern.
	 * @param Zone      UTM-Zone.
	 * @param bNorthernHemisphere Fuer Deutschland immer true.
	 * @return Geographische Koordinate; bei unplausiblen Eingaben eine
	 *         ungueltige Koordinate (IsValid() == false).
	 */
	static FGeoCoordinate UtmToGeographic(
		double Easting,
		double Northing,
		int32 Zone = GermanyZone32,
		bool bNorthernHemisphere = true);

	/**
	 * Geographisch -> UTM. Gegenstueck zu UtmToGeographic, vor allem zum
	 * Pruefen der Umkehrbarkeit im Test.
	 * @return false bei ungueltiger Eingabe.
	 */
	static bool GeographicToUtm(
		const FGeoCoordinate& Coord,
		int32 Zone,
		double& OutEasting,
		double& OutNorthing);

	/**
	 * Passende UTM-Zone zu einer Laenge.
	 * Zone 32 deckt 6 bis 12 Grad Ost ab und damit ganz Hessen.
	 */
	static int32 GetZoneForLongitude(double Longitude)
	{
		return FMath::Clamp(FMath::FloorToInt32((Longitude + 180.0) / 6.0) + 1, 1, 60);
	}

	/**
	 * Zerlegt einen ALKIS-Ostwert, dem die Zonennummer vorangestellt ist
	 * ("32450096.921"), in Zone und Ostwert.
	 *
	 * Amtliche Daten liefern beide Schreibweisen: mit vorangestellter Zone
	 * (Zonenkennziffer, frueher ueblich) und ohne. Ein Ostwert ist per
	 * Definition kleiner als 1.000.000; alles darueber traegt die Zone im
	 * Wert. Wird das verwechselt, landet die Geometrie tausende Kilometer
	 * daneben - und zwar ohne Fehlermeldung.
	 */
	static void SplitZonePrefixedEasting(double RawEasting, int32& OutZone, double& OutEasting);
};
