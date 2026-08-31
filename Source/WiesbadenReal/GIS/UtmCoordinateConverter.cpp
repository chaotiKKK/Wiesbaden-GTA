// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/UtmCoordinateConverter.h"

#include "WiesbadenReal.h"

namespace
{
	/** Zweite numerische Exzentrizitaet zum Quadrat: e'^2 = e^2 / (1 - e^2). */
	constexpr double EccentricityPrimeSquared =
		FUtmCoordinateConverter::EccentricitySquared
		/ (1.0 - FUtmCoordinateConverter::EccentricitySquared);

	/** Nordwert-Verschiebung der Suedhalbkugel in Metern (UTM-Definition). */
	constexpr double SouthernFalseNorthing = 10000000.0;
}

FGeoCoordinate FUtmCoordinateConverter::UtmToGeographic(
	double Easting,
	double Northing,
	int32 Zone,
	bool bNorthernHemisphere)
{
	if (!FMath::IsFinite(Easting) || !FMath::IsFinite(Northing))
	{
		return FGeoCoordinate(200.0, 200.0, 0.0); // ungueltig
	}

	if (Zone < 1 || Zone > 60)
	{
		UE_LOG(LogWbGIS, Warning, TEXT("UTM: ungueltige Zone %d."), Zone);
		return FGeoCoordinate(200.0, 200.0, 0.0);
	}

	const double X = Easting - FalseEasting;
	const double Y = bNorthernHemisphere ? Northing : (Northing - SouthernFalseNorthing);

	const double A = SemiMajorAxis;
	const double ESq = EccentricitySquared;

	// Meridianbogenlaenge zurueckrechnen.
	const double M = Y / ScaleFactor;
	const double Mu = M / (A * (1.0
		- ESq / 4.0
		- 3.0 * ESq * ESq / 64.0
		- 5.0 * ESq * ESq * ESq / 256.0));

	// Hilfsgroesse e1 der Fusspunktbreiten-Reihe.
	const double SqrtOneMinusE = FMath::Sqrt(1.0 - ESq);
	const double E1 = (1.0 - SqrtOneMinusE) / (1.0 + SqrtOneMinusE);

	const double E1_2 = E1 * E1;
	const double E1_3 = E1_2 * E1;
	const double E1_4 = E1_3 * E1;

	// Fusspunktbreite (footprint latitude).
	const double Phi1 = Mu
		+ (3.0 * E1 / 2.0 - 27.0 * E1_3 / 32.0) * FMath::Sin(2.0 * Mu)
		+ (21.0 * E1_2 / 16.0 - 55.0 * E1_4 / 32.0) * FMath::Sin(4.0 * Mu)
		+ (151.0 * E1_3 / 96.0) * FMath::Sin(6.0 * Mu)
		+ (1097.0 * E1_4 / 512.0) * FMath::Sin(8.0 * Mu);

	const double SinPhi1 = FMath::Sin(Phi1);
	const double CosPhi1 = FMath::Cos(Phi1);
	const double TanPhi1 = FMath::Tan(Phi1);

	// Am Pol degeneriert die Reihe; fuer Deutschland nie erreicht, aber die
	// Funktion darf dort nicht stillschweigend Unsinn liefern.
	if (FMath::Abs(CosPhi1) < UE_DOUBLE_SMALL_NUMBER)
	{
		return FGeoCoordinate(GetZoneCentralMeridian(Zone), Phi1 >= 0.0 ? 90.0 : -90.0, 0.0);
	}

	const double C1 = EccentricityPrimeSquared * CosPhi1 * CosPhi1;
	const double T1 = TanPhi1 * TanPhi1;

	const double OneMinusESinSq = 1.0 - ESq * SinPhi1 * SinPhi1;

	// Querkruemmungshalbmesser und Meridiankruemmungshalbmesser.
	const double N1 = A / FMath::Sqrt(OneMinusESinSq);
	const double R1 = A * (1.0 - ESq) / (OneMinusESinSq * FMath::Sqrt(OneMinusESinSq));

	const double D = X / (N1 * ScaleFactor);
	const double D2 = D * D;
	const double D3 = D2 * D;
	const double D4 = D3 * D;
	const double D5 = D4 * D;
	const double D6 = D5 * D;

	const double C1_2 = C1 * C1;
	const double T1_2 = T1 * T1;

	const double LatRad = Phi1 - (N1 * TanPhi1 / R1) * (
		D2 / 2.0
		- (5.0 + 3.0 * T1 + 10.0 * C1 - 4.0 * C1_2 - 9.0 * EccentricityPrimeSquared) * D4 / 24.0
		+ (61.0 + 90.0 * T1 + 298.0 * C1 + 45.0 * T1_2
			- 252.0 * EccentricityPrimeSquared - 3.0 * C1_2) * D6 / 720.0);

	const double LonOffsetRad = (
		D
		- (1.0 + 2.0 * T1 + C1) * D3 / 6.0
		+ (5.0 - 2.0 * C1 + 28.0 * T1 - 3.0 * C1_2
			+ 8.0 * EccentricityPrimeSquared + 24.0 * T1_2) * D5 / 120.0) / CosPhi1;

	const double Longitude = GetZoneCentralMeridian(Zone) + FMath::RadiansToDegrees(LonOffsetRad);
	const double Latitude = FMath::RadiansToDegrees(LatRad);

	return FGeoCoordinate(Longitude, Latitude, 0.0);
}

bool FUtmCoordinateConverter::GeographicToUtm(
	const FGeoCoordinate& Coord,
	int32 Zone,
	double& OutEasting,
	double& OutNorthing)
{
	OutEasting = 0.0;
	OutNorthing = 0.0;

	if (!Coord.IsValid() || Zone < 1 || Zone > 60)
	{
		return false;
	}

	const double A = SemiMajorAxis;
	const double ESq = EccentricitySquared;

	const double LatRad = FMath::DegreesToRadians(Coord.Latitude);
	const double LonRad = FMath::DegreesToRadians(Coord.Longitude);
	const double Lon0Rad = FMath::DegreesToRadians(GetZoneCentralMeridian(Zone));

	const double SinLat = FMath::Sin(LatRad);
	const double CosLat = FMath::Cos(LatRad);
	const double TanLat = FMath::Tan(LatRad);

	if (FMath::Abs(CosLat) < UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const double N = A / FMath::Sqrt(1.0 - ESq * SinLat * SinLat);
	const double T = TanLat * TanLat;
	const double C = EccentricityPrimeSquared * CosLat * CosLat;
	const double Aa = CosLat * (LonRad - Lon0Rad);

	const double Aa2 = Aa * Aa;
	const double Aa3 = Aa2 * Aa;
	const double Aa4 = Aa3 * Aa;
	const double Aa5 = Aa4 * Aa;
	const double Aa6 = Aa5 * Aa;

	// Meridianbogenlaenge.
	const double M = A * (
		(1.0 - ESq / 4.0 - 3.0 * ESq * ESq / 64.0 - 5.0 * ESq * ESq * ESq / 256.0) * LatRad
		- (3.0 * ESq / 8.0 + 3.0 * ESq * ESq / 32.0 + 45.0 * ESq * ESq * ESq / 1024.0) * FMath::Sin(2.0 * LatRad)
		+ (15.0 * ESq * ESq / 256.0 + 45.0 * ESq * ESq * ESq / 1024.0) * FMath::Sin(4.0 * LatRad)
		- (35.0 * ESq * ESq * ESq / 3072.0) * FMath::Sin(6.0 * LatRad));

	OutEasting = ScaleFactor * N * (
		Aa
		+ (1.0 - T + C) * Aa3 / 6.0
		+ (5.0 - 18.0 * T + T * T + 72.0 * C - 58.0 * EccentricityPrimeSquared) * Aa5 / 120.0)
		+ FalseEasting;

	OutNorthing = ScaleFactor * (M + N * TanLat * (
		Aa2 / 2.0
		+ (5.0 - T + 9.0 * C + 4.0 * C * C) * Aa4 / 24.0
		+ (61.0 - 58.0 * T + T * T + 600.0 * C - 330.0 * EccentricityPrimeSquared) * Aa6 / 720.0));

	if (Coord.Latitude < 0.0)
	{
		OutNorthing += SouthernFalseNorthing;
	}

	return true;
}

void FUtmCoordinateConverter::SplitZonePrefixedEasting(double RawEasting, int32& OutZone, double& OutEasting)
{
	// Ein gueltiger Ostwert liegt zwischen 100.000 und 900.000. Ist der Wert
	// groesser, sind die fuehrenden Ziffern die Zonenkennziffer.
	if (RawEasting >= 1000000.0)
	{
		OutZone = static_cast<int32>(FMath::FloorToDouble(RawEasting / 1000000.0));
		OutEasting = RawEasting - static_cast<double>(OutZone) * 1000000.0;
	}
	else
	{
		OutZone = 0; // keine Zone im Wert enthalten
		OutEasting = RawEasting;
	}
}
