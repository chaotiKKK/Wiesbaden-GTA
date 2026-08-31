// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/GeoCoordinateConverter.h"

#include "WiesbadenReal.h"

bool UGeoCoordinateConverter::Initialize(const FGeoCoordinate& InOrigin)
{
	if (!InOrigin.IsValid())
	{
		UE_LOG(LogWbGIS, Error,
			TEXT("GeoCoordinateConverter::Initialize: ungueltiger Origin %s - Konverter bleibt uninitialisiert."),
			*InOrigin.ToString());
		bInitialized = false;
		return false;
	}

	// Ein Origin an den Polen wuerde die Ost-Achse degenerieren lassen. Fuer
	// Wiesbaden irrelevant, aber die Klasse ist wiederverwendbar.
	if (FMath::Abs(InOrigin.Latitude) > 89.9)
	{
		UE_LOG(LogWbGIS, Error,
			TEXT("GeoCoordinateConverter::Initialize: Origin zu nah am Pol (lat=%.6f) - ")
			TEXT("das lokale Tangentialsystem ist dort nicht eindeutig."),
			InOrigin.Latitude);
		bInitialized = false;
		return false;
	}

	Origin = InOrigin;
	BuildTransform();
	bInitialized = true;
	bLoggedUninitializedWarning = false;

	UE_LOG(LogWbGIS, Log,
		TEXT("Georeferenz initialisiert. Origin %s -> ECEF (%.3f, %.3f, %.3f) m. Massstab 1 uu = 1 cm."),
		*Origin.ToString(), OriginECEF.X, OriginECEF.Y, OriginECEF.Z);

	return true;
}

bool UGeoCoordinateConverter::InitializeWithWiesbadenOrigin()
{
	return Initialize(FGeoCoordinate(
		WiesbadenOriginLongitude,
		WiesbadenOriginLatitude,
		WiesbadenOriginHeight));
}

void UGeoCoordinateConverter::BuildTransform()
{
	OriginECEF = GeodeticToECEF(Origin);

	const double LonRad = FMath::DegreesToRadians(Origin.Longitude);
	const double LatRad = FMath::DegreesToRadians(Origin.Latitude);

	const double SinLon = FMath::Sin(LonRad);
	const double CosLon = FMath::Cos(LonRad);
	const double SinLat = FMath::Sin(LatRad);
	const double CosLat = FMath::Cos(LatRad);

	// Standard-ENU-Basisvektoren, ausgedrueckt im ECEF-Rahmen.
	EastAxis = FVector(-SinLon, CosLon, 0.0);
	const FVector NorthAxis(-SinLat * CosLon, -SinLat * SinLon, CosLat);
	UpAxis = FVector(CosLat * CosLon, CosLat * SinLon, SinLat);

	// Unreal ist linkshaendig: +Y zeigt nach Sueden, nicht nach Norden.
	SouthAxis = -NorthAxis;
}

FVector UGeoCoordinateConverter::GeodeticToECEF(const FGeoCoordinate& Coord)
{
	if (!Coord.IsValid())
	{
		return FVector::ZeroVector;
	}

	const double LonRad = FMath::DegreesToRadians(Coord.Longitude);
	const double LatRad = FMath::DegreesToRadians(Coord.Latitude);

	const double SinLat = FMath::Sin(LatRad);
	const double CosLat = FMath::Cos(LatRad);

	// Kruemmungsradius im Ersten Vertikal (prime vertical radius).
	const double N = WGS84_SemiMajorAxis / FMath::Sqrt(1.0 - WGS84_EccentricitySquared * SinLat * SinLat);

	const double NPlusH = N + Coord.Height;

	return FVector(
		NPlusH * CosLat * FMath::Cos(LonRad),
		NPlusH * CosLat * FMath::Sin(LonRad),
		(N * (1.0 - WGS84_EccentricitySquared) + Coord.Height) * SinLat);
}

FGeoCoordinate UGeoCoordinateConverter::ECEFToGeodetic(const FVector& ECEF)
{
	// Geschlossene Loesung nach Ferrari/Bowring - keine Iteration, Genauigkeit
	// im Sub-Millimeterbereich fuer Hoehen von -10 km bis +100 km.
	const double X = ECEF.X;
	const double Y = ECEF.Y;
	const double Z = ECEF.Z;

	const double A = WGS84_SemiMajorAxis;
	const double B = WGS84_SemiMinorAxis;
	const double ESq = WGS84_EccentricitySquared;
	const double EPrimeSq = WGS84_SecondEccentricitySquared;

	const double P = FMath::Sqrt(X * X + Y * Y);

	// Degenerierter Fall: Punkt auf der Rotationsachse (Pol). Dort ist die
	// Longitude nicht definiert; wir setzen sie auf 0.
	if (P < UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		const double PolarHeight = FMath::Abs(Z) - B;
		return FGeoCoordinate(0.0, Z >= 0.0 ? 90.0 : -90.0, PolarHeight);
	}

	const double Theta = FMath::Atan2(Z * A, P * B);
	const double SinTheta = FMath::Sin(Theta);
	const double CosTheta = FMath::Cos(Theta);

	const double LatRad = FMath::Atan2(
		Z + B * EPrimeSq * SinTheta * SinTheta * SinTheta,
		P - A * ESq * CosTheta * CosTheta * CosTheta);

	const double LonRad = FMath::Atan2(Y, X);

	const double SinLat = FMath::Sin(LatRad);
	const double N = A / FMath::Sqrt(1.0 - ESq * SinLat * SinLat);

	// Bei hohen Breiten wird die P/cos(lat)-Formel numerisch instabil; dann
	// ueber Z/sin(lat) rechnen.
	const double CosLat = FMath::Cos(LatRad);
	double Height;
	if (FMath::Abs(CosLat) > 0.1)
	{
		Height = P / CosLat - N;
	}
	else
	{
		Height = Z / SinLat - N * (1.0 - ESq);
	}

	return FGeoCoordinate(
		FMath::RadiansToDegrees(LonRad),
		FMath::RadiansToDegrees(LatRad),
		Height);
}

FVector UGeoCoordinateConverter::GeoToUnreal(const FGeoCoordinate& Coord) const
{
	if (!bInitialized)
	{
		if (!bLoggedUninitializedWarning)
		{
			UE_LOG(LogWbGIS, Error,
				TEXT("GeoToUnreal auf uninitialisiertem Konverter aufgerufen. ")
				TEXT("Alle Ergebnisse sind Nullvektoren. Initialize() zuerst aufrufen."));
			bLoggedUninitializedWarning = true;
		}
		return FVector::ZeroVector;
	}

	if (!Coord.IsValid())
	{
		UE_LOG(LogWbGIS, Warning, TEXT("GeoToUnreal: ungueltige Koordinate %s uebersprungen."), *Coord.ToString());
		return FVector::ZeroVector;
	}

	// Differenzvektor im ECEF-Rahmen, dann Projektion auf die lokalen Achsen.
	const FVector Delta = GeodeticToECEF(Coord) - OriginECEF;

	return FVector(
		FVector::DotProduct(Delta, EastAxis) * UnitsPerMeter,
		FVector::DotProduct(Delta, SouthAxis) * UnitsPerMeter,
		FVector::DotProduct(Delta, UpAxis) * UnitsPerMeter);
}

FVector UGeoCoordinateConverter::GeoToUnrealGround(const FGeoCoordinate& Coord) const
{
	FVector Result = GeoToUnreal(FGeoCoordinate(Coord.Longitude, Coord.Latitude, Origin.Height));
	Result.Z = 0.0;
	return Result;
}

FGeoCoordinate UGeoCoordinateConverter::UnrealToGeo(const FVector& UnrealPosition) const
{
	if (!bInitialized)
	{
		if (!bLoggedUninitializedWarning)
		{
			UE_LOG(LogWbGIS, Error, TEXT("UnrealToGeo auf uninitialisiertem Konverter aufgerufen."));
			bLoggedUninitializedWarning = true;
		}
		return FGeoCoordinate();
	}

	if (UnrealPosition.ContainsNaN())
	{
		UE_LOG(LogWbGIS, Warning, TEXT("UnrealToGeo: Position enthaelt NaN."));
		return FGeoCoordinate();
	}

	const double EastMeters = UnrealPosition.X / UnitsPerMeter;
	const double SouthMeters = UnrealPosition.Y / UnitsPerMeter;
	const double UpMeters = UnrealPosition.Z / UnitsPerMeter;

	// Ruecktransformation: die Achsen sind orthonormal, daher ist die
	// Transponierte gleich der Inversen.
	const FVector ECEF = OriginECEF
		+ EastAxis * EastMeters
		+ SouthAxis * SouthMeters
		+ UpAxis * UpMeters;

	return ECEFToGeodetic(ECEF);
}

double UGeoCoordinateConverter::HaversineDistanceMeters(const FGeoCoordinate& A, const FGeoCoordinate& B)
{
	if (!A.IsValid() || !B.IsValid())
	{
		return 0.0;
	}

	// Mittlerer Erdradius (IUGG), passend fuer Haversine.
	constexpr double MeanEarthRadius = 6371008.8;

	const double Lat1 = FMath::DegreesToRadians(A.Latitude);
	const double Lat2 = FMath::DegreesToRadians(B.Latitude);
	const double DeltaLat = Lat2 - Lat1;
	const double DeltaLon = FMath::DegreesToRadians(B.Longitude - A.Longitude);

	const double SinHalfLat = FMath::Sin(DeltaLat * 0.5);
	const double SinHalfLon = FMath::Sin(DeltaLon * 0.5);

	const double H = SinHalfLat * SinHalfLat
		+ FMath::Cos(Lat1) * FMath::Cos(Lat2) * SinHalfLon * SinHalfLon;

	return 2.0 * MeanEarthRadius * FMath::Asin(FMath::Sqrt(FMath::Min(1.0, H)));
}

double UGeoCoordinateConverter::ECEFDistanceMeters(const FGeoCoordinate& A, const FGeoCoordinate& B)
{
	if (!A.IsValid() || !B.IsValid())
	{
		return 0.0;
	}
	return FVector::Dist(GeodeticToECEF(A), GeodeticToECEF(B));
}

double UGeoCoordinateConverter::MetersPerDegreeLongitude(double LatitudeDegrees)
{
	const double LatRad = FMath::DegreesToRadians(LatitudeDegrees);
	const double SinLat = FMath::Sin(LatRad);
	const double N = WGS84_SemiMajorAxis / FMath::Sqrt(1.0 - WGS84_EccentricitySquared * SinLat * SinLat);
	return (UE_DOUBLE_PI / 180.0) * N * FMath::Cos(LatRad);
}

double UGeoCoordinateConverter::MetersPerDegreeLatitude(double LatitudeDegrees)
{
	const double LatRad = FMath::DegreesToRadians(LatitudeDegrees);
	const double SinLat = FMath::Sin(LatRad);
	const double Denominator = 1.0 - WGS84_EccentricitySquared * SinLat * SinLat;

	// Meridiankruemmungsradius M = a(1-e^2) / (1 - e^2 sin^2 lat)^(3/2)
	const double M = WGS84_SemiMajorAxis * (1.0 - WGS84_EccentricitySquared)
		/ (Denominator * FMath::Sqrt(Denominator));

	return (UE_DOUBLE_PI / 180.0) * M;
}

FBox UGeoCoordinateConverter::GeoBoundsToUnrealBox(const FGeoBounds& Bounds, double MinZMeters, double MaxZMeters) const
{
	if (!bInitialized || !Bounds.IsValid())
	{
		return FBox(ForceInit);
	}

	// Alle vier Ecken transformieren: die Kanten der Geo-Box sind in
	// Unreal-Koordinaten nicht exakt achsenparallel (Konvergenz der
	// Meridiane), daher wird die umschliessende Box gebildet.
	const FGeoCoordinate Corners[4] = {
		FGeoCoordinate(Bounds.MinLongitude, Bounds.MinLatitude, Origin.Height),
		FGeoCoordinate(Bounds.MaxLongitude, Bounds.MinLatitude, Origin.Height),
		FGeoCoordinate(Bounds.MinLongitude, Bounds.MaxLatitude, Origin.Height),
		FGeoCoordinate(Bounds.MaxLongitude, Bounds.MaxLatitude, Origin.Height),
	};

	FBox Result(ForceInit);
	for (const FGeoCoordinate& Corner : Corners)
	{
		const FVector P = GeoToUnreal(Corner);
		Result += FVector(P.X, P.Y, MinZMeters * UnitsPerMeter);
		Result += FVector(P.X, P.Y, MaxZMeters * UnitsPerMeter);
	}

	return Result;
}
