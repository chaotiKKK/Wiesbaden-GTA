// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeoCoordinateConverter.generated.h"

/**
 * Geodaetische Koordinate auf dem WGS84-Ellipsoid.
 *
 * Longitude/Latitude in Dezimalgrad, Height als ellipsoidische Hoehe in Metern
 * (NICHT Hoehe ueber Geoid/NN - die Umrechnung erfolgt in UHeightmapImporter,
 * weil DEM-Produkte wie Copernicus DEM orthometrische Hoehen liefern).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FGeoCoordinate
{
	GENERATED_BODY()

	/** Geographische Laenge in Dezimalgrad, [-180, 180]. Oestlich positiv. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geo")
	double Longitude = 0.0;

	/** Geographische Breite in Dezimalgrad, [-90, 90]. Noerdlich positiv. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geo")
	double Latitude = 0.0;

	/** Ellipsoidische Hoehe in Metern ueber dem WGS84-Ellipsoid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geo")
	double Height = 0.0;

	FGeoCoordinate() = default;

	FGeoCoordinate(double InLongitude, double InLatitude, double InHeight = 0.0)
		: Longitude(InLongitude)
		, Latitude(InLatitude)
		, Height(InHeight)
	{
	}

	/** True, wenn Lon/Lat im gueltigen Wertebereich liegen und keine NaNs enthalten. */
	bool IsValid() const
	{
		return FMath::IsFinite(Longitude) && FMath::IsFinite(Latitude) && FMath::IsFinite(Height)
			&& Longitude >= -180.0 && Longitude <= 180.0
			&& Latitude >= -90.0 && Latitude <= 90.0;
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("(lon=%.7f, lat=%.7f, h=%.3fm)"), Longitude, Latitude, Height);
	}
};

/**
 * Achsenparallele geographische Bounding-Box (Overpass-Reihenfolge beachten:
 * Overpass erwartet south,west,north,east - diese Struktur speichert explizit
 * benannt, um Vertauschungen auszuschliessen).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FGeoBounds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geo")
	double MinLongitude = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geo")
	double MinLatitude = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geo")
	double MaxLongitude = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geo")
	double MaxLatitude = 0.0;

	FGeoBounds() = default;

	FGeoBounds(double InMinLon, double InMinLat, double InMaxLon, double InMaxLat)
		: MinLongitude(InMinLon)
		, MinLatitude(InMinLat)
		, MaxLongitude(InMaxLon)
		, MaxLatitude(InMaxLat)
	{
	}

	bool IsValid() const
	{
		return MaxLongitude > MinLongitude && MaxLatitude > MinLatitude
			&& FMath::IsFinite(MinLongitude) && FMath::IsFinite(MinLatitude)
			&& FMath::IsFinite(MaxLongitude) && FMath::IsFinite(MaxLatitude);
	}

	bool Contains(const FGeoCoordinate& Coord) const
	{
		return Coord.Longitude >= MinLongitude && Coord.Longitude <= MaxLongitude
			&& Coord.Latitude >= MinLatitude && Coord.Latitude <= MaxLatitude;
	}

	FGeoCoordinate GetCenter() const
	{
		return FGeoCoordinate(
			(MinLongitude + MaxLongitude) * 0.5,
			(MinLatitude + MaxLatitude) * 0.5,
			0.0);
	}

	/** Erweitert die Box so, dass Coord enthalten ist. Fuer inkrementellen Aufbau. */
	void Include(const FGeoCoordinate& Coord)
	{
		MinLongitude = FMath::Min(MinLongitude, Coord.Longitude);
		MinLatitude = FMath::Min(MinLatitude, Coord.Latitude);
		MaxLongitude = FMath::Max(MaxLongitude, Coord.Longitude);
		MaxLatitude = FMath::Max(MaxLatitude, Coord.Latitude);
	}

	/** Overpass-API-Formatierung: "south,west,north,east". */
	FString ToOverpassBBox() const
	{
		return FString::Printf(TEXT("%.7f,%.7f,%.7f,%.7f"),
			MinLatitude, MinLongitude, MaxLatitude, MaxLongitude);
	}
};

/**
 * Georeferenzierung: WGS84 <-> Unreal-Weltkoordinaten.
 *
 * KOORDINATENSYSTEM-KONVENTION (identisch zu Cesium for Unreal, damit die
 * Pipeline mit und ohne Cesium-Plugin dieselben Weltpositionen erzeugt):
 *
 *   Unreal +X = Ost      (East)
 *   Unreal +Y = Sued     (South, also -Nord)
 *   Unreal +Z = Oben     (Up, entlang der Ellipsoid-Normalen am Origin)
 *
 * Das ist ein linkshaendiges East-South-Up-System, tangential am
 * Georeferenz-Origin. Unreal ist linkshaendig, ENU ist rechtshaendig - die
 * Nord-Achse wird daher gespiegelt. Wer das ignoriert, erhaelt eine
 * spiegelverkehrte Stadt (Rheinstrasse verlaeuft dann falsch herum).
 *
 * MASSSTAB: 1 Unreal Unit = 1 cm, also Faktor 100 gegenueber Metern (1:1-Welt).
 *
 * GENAUIGKEIT: Die Transformation geht ueber ECEF (Earth-Centered,
 * Earth-Fixed) und ist damit exakt - keine Mercator- oder
 * Equirectangular-Approximation. Bei einer Stadtausdehnung von ~20 km betraegt
 * der Fehler einer naiven Flat-Earth-Projektion bereits > 5 m in der Hoehe;
 * das faellt an Rampen und Bruecken sofort auf. Alle Zwischenrechnungen in
 * double.
 *
 * Die Klasse ist nach Initialisierung immutable und damit thread-safe lesbar -
 * der BuildingGenerator und der RoadNetworkGenerator konvertieren parallel.
 */
UCLASS(BlueprintType)
class WIESBADENREAL_API UGeoCoordinateConverter : public UObject
{
	GENERATED_BODY()

public:
	/** WGS84 Aequatorradius (grosse Halbachse) in Metern. */
	static constexpr double WGS84_SemiMajorAxis = 6378137.0;

	/** WGS84 Polradius (kleine Halbachse) in Metern. */
	static constexpr double WGS84_SemiMinorAxis = 6356752.314245;

	/** Erste numerische Exzentrizitaet zum Quadrat: e^2 = (a^2 - b^2) / a^2. */
	static constexpr double WGS84_EccentricitySquared = 6.69437999014e-3;

	/** Zweite numerische Exzentrizitaet zum Quadrat: e'^2 = (a^2 - b^2) / b^2. */
	static constexpr double WGS84_SecondEccentricitySquared = 6.73949674228e-3;

	/** Unreal Units pro Meter. */
	static constexpr double UnitsPerMeter = 100.0;

	/**
	 * Georeferenz-Origin des Projekts: Wiesbaden, Schlossplatz / Marktkirche.
	 * Gewaehlt, weil er (a) im Stadtzentrum liegt, wodurch die
	 * Float-Praezision der Renderpipeline dort maximal ist, wo die meisten
	 * Details stehen, und (b) ein eindeutig identifizierbarer Punkt zur
	 * visuellen Verifikation gegen Luftbilder ist.
	 */
	static constexpr double WiesbadenOriginLongitude = 8.2400000;
	static constexpr double WiesbadenOriginLatitude = 50.0824000;
	static constexpr double WiesbadenOriginHeight = 117.0;

	/** Bounding-Box des Stadtgebiets Wiesbaden inkl. Vororte (Spezifikation 4.1). */
	static FGeoBounds GetWiesbadenBounds()
	{
		return FGeoBounds(8.0800, 49.9950, 8.4200, 50.1600);
	}

	/**
	 * Setzt den Georeferenz-Origin. Muss genau einmal vor der ersten
	 * Konvertierung aufgerufen werden.
	 * @return false, wenn die Koordinate ungueltig ist; der Konverter bleibt
	 *         dann uninitialisiert und alle Konvertierungen liefern Nullvektoren
	 *         (statt stillschweigend falscher Positionen).
	 */
	UFUNCTION(BlueprintCallable, Category = "GIS|Georeference")
	bool Initialize(const FGeoCoordinate& InOrigin);

	/** Initialisiert mit dem Projekt-Standard-Origin (Schlossplatz Wiesbaden). */
	UFUNCTION(BlueprintCallable, Category = "GIS|Georeference")
	bool InitializeWithWiesbadenOrigin();

	UFUNCTION(BlueprintPure, Category = "GIS|Georeference")
	bool IsInitialized() const { return bInitialized; }

	UFUNCTION(BlueprintPure, Category = "GIS|Georeference")
	FGeoCoordinate GetOrigin() const { return Origin; }

	// -- Hauptkonvertierungen ------------------------------------------------

	/**
	 * WGS84 -> Unreal-Weltposition in Unreal Units (cm).
	 * Bei nicht initialisiertem Konverter oder ungueltiger Eingabe wird
	 * FVector::ZeroVector zurueckgegeben und einmalig geloggt.
	 */
	UFUNCTION(BlueprintPure, Category = "GIS|Georeference")
	FVector GeoToUnreal(const FGeoCoordinate& Coord) const;

	/** Unreal-Weltposition (cm) -> WGS84. Exakte Inverse von GeoToUnreal. */
	UFUNCTION(BlueprintPure, Category = "GIS|Georeference")
	FGeoCoordinate UnrealToGeo(const FVector& UnrealPosition) const;

	/**
	 * Wie GeoToUnreal, ignoriert aber die Hoehe der Eingabe und liefert Z=0.
	 * Fuer OSM-Daten, die keine Hoeheninformation tragen - die Z-Komponente
	 * wird spaeter durch Terrain-Sampling gesetzt.
	 */
	UFUNCTION(BlueprintPure, Category = "GIS|Georeference")
	FVector GeoToUnrealGround(const FGeoCoordinate& Coord) const;

	// -- ECEF-Zwischenstufe (oeffentlich fuer Tests und Cesium-Interop) ------

	/** Geodaetisch -> ECEF (kartesisch, erdfest, Meter). */
	static FVector GeodeticToECEF(const FGeoCoordinate& Coord);

	/** ECEF (Meter) -> geodaetisch. Iterationsfrei nach Bowring/Ferrari. */
	static FGeoCoordinate ECEFToGeodetic(const FVector& ECEF);

	// -- Metrische Hilfsfunktionen ------------------------------------------

	/**
	 * Grosskreis-Distanz zwischen zwei Koordinaten in Metern (Haversine).
	 * Fuer Stadtdistanzen genau auf < 0.5 % - ausreichend fuer
	 * Routenlaengen-Anzeige im GPS. Fuer Geometrieerzeugung wird stattdessen
	 * die exakte ECEF-Differenz verwendet.
	 */
	UFUNCTION(BlueprintPure, Category = "GIS|Georeference")
	static double HaversineDistanceMeters(const FGeoCoordinate& A, const FGeoCoordinate& B);

	/** Exakte 3D-Distanz zweier Koordinaten in Metern ueber ECEF. */
	UFUNCTION(BlueprintPure, Category = "GIS|Georeference")
	static double ECEFDistanceMeters(const FGeoCoordinate& A, const FGeoCoordinate& B);

	/**
	 * Meter pro Grad Longitude auf der gegebenen Breite. Wird fuer die
	 * Umrechnung von DEM-Rasterweiten (in Bogensekunden) in Meter gebraucht.
	 */
	UFUNCTION(BlueprintPure, Category = "GIS|Georeference")
	static double MetersPerDegreeLongitude(double LatitudeDegrees);

	/** Meter pro Grad Latitude auf der gegebenen Breite. */
	UFUNCTION(BlueprintPure, Category = "GIS|Georeference")
	static double MetersPerDegreeLatitude(double LatitudeDegrees);

	/**
	 * Konvertiert eine geographische Bounding-Box in eine Unreal-Box (cm).
	 * Z-Bereich wird auf [MinZ, MaxZ] gesetzt, die aus dem Hoehenmodell
	 * stammen; ohne Angabe +/- 1 km um den Origin.
	 */
	UFUNCTION(BlueprintPure, Category = "GIS|Georeference")
	FBox GeoBoundsToUnrealBox(const FGeoBounds& Bounds, double MinZMeters = -1000.0, double MaxZMeters = 1000.0) const;

private:
	/** Berechnet die ECEF->ESU-Rotationsmatrix am Origin. */
	void BuildTransform();

	FGeoCoordinate Origin;

	/** ECEF-Position des Origins in Metern. */
	FVector OriginECEF = FVector::ZeroVector;

	/**
	 * Zeilen der ECEF->ESU-Rotationsmatrix. Explizit als drei Vektoren
	 * gespeichert, weil FMatrix in Unreal zeilenweise mit Translation
	 * arbeitet und die Semantik hier eindeutig bleiben soll.
	 */
	FVector EastAxis = FVector::ZeroVector;
	FVector SouthAxis = FVector::ZeroVector;
	FVector UpAxis = FVector::ZeroVector;

	bool bInitialized = false;

	/** Verhindert Log-Spam bei Aufrufen auf uninitialisiertem Konverter. */
	mutable bool bLoggedUninitializedWarning = false;
};
