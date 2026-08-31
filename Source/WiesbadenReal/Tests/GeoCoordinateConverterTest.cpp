// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/GeoCoordinateConverter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoCoordinateConverterRoundTripTest,
	"WiesbadenReal.GIS.GeoCoordinateConverter.RoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGeoCoordinateConverterRoundTripTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	if (!TestNotNull(TEXT("Konverter erzeugt"), Converter))
	{
		return false;
	}

	if (!TestTrue(TEXT("Wiesbaden-Origin initialisiert"), Converter->InitializeWithWiesbadenOrigin()))
	{
		return false;
	}
	TestTrue(TEXT("IsInitialized"), Converter->IsInitialized());

	// Der Origin muss exakt auf den Nullvektor abbilden (Delta = 0 im ECEF-Rahmen).
	const FGeoCoordinate Origin(
		UGeoCoordinateConverter::WiesbadenOriginLongitude,
		UGeoCoordinateConverter::WiesbadenOriginLatitude,
		UGeoCoordinateConverter::WiesbadenOriginHeight);

	const FVector UnrealOrigin = Converter->GeoToUnreal(Origin);
	TestTrue(TEXT("Origin -> Nullvektor"), UnrealOrigin.Equals(FVector::ZeroVector, 0.5));

	// Round-Trip fuer mehrere Punkte im Stadtgebiet (Lon/Lat/Hoehe muessen zurueckkehren).
	const FGeoCoordinate Samples[] = {
		FGeoCoordinate(8.2400, 50.0824, 0.0),
		FGeoCoordinate(8.1000, 50.0100, 120.0),
		FGeoCoordinate(8.4000, 50.1500, 200.0),
		FGeoCoordinate(8.2400, 50.0824, 117.0),
	};

	for (const FGeoCoordinate& Coord : Samples)
	{
		const FVector World = Converter->GeoToUnreal(Coord);
		const FGeoCoordinate Back = Converter->UnrealToGeo(World);

		TestTrue(FString::Printf(TEXT("Lon-Roundtrip %s"), *Coord.ToString()),
			FMath::IsNearlyEqual(Back.Longitude, Coord.Longitude, 1e-6));
		TestTrue(FString::Printf(TEXT("Lat-Roundtrip %s"), *Coord.ToString()),
			FMath::IsNearlyEqual(Back.Latitude, Coord.Latitude, 1e-6));
		TestTrue(FString::Printf(TEXT("Hoehen-Roundtrip %s"), *Coord.ToString()),
			FMath::IsNearlyEqual(Back.Height, Coord.Height, 1e-3));
	}

	// Richtungssinn der East-South-Up-Konvention pruefen - ein Vorzeichenfehler
	// hier wuerde die ganze Stadt spiegelverkehrt erzeugen.
	// Toleranz 100 cm: Ein Punkt auf gleicher Breite liegt bei einer
	// Tangentialebenen-Projektion nicht exakt auf der Ost-Achse (Kreisbogen-
	// Kruemmung, ~5 cm bei 0.01 Laengengrad); ein Achsen-/Vorzeichenfehler
	// erzeugt dagegen Abweichungen im Kilometerbereich.
	const FVector EastOfOrigin = Converter->GeoToUnreal(FGeoCoordinate(8.2500, 50.0824, 0.0));
	TestTrue(TEXT("Oestlich -> +X"), EastOfOrigin.X > 0.0);
	TestTrue(TEXT("Oestlich -> Y ~ 0"), FMath::IsNearlyEqual(EastOfOrigin.Y, 0.0, 100.0));

	const FVector NorthOfOrigin = Converter->GeoToUnreal(FGeoCoordinate(8.2400, 50.0900, 0.0));
	TestTrue(TEXT("Noerdlich -> -Y"), NorthOfOrigin.Y < 0.0);
	TestTrue(TEXT("Noerdlich -> X ~ 0"), FMath::IsNearlyEqual(NorthOfOrigin.X, 0.0, 100.0));

	// UnrealToGeo ist die exakte Inverse - erneuter Round-Trip eines Weltpunkts.
	const FVector ArbitraryWorld(123456.0, -234567.0, 4567.0);
	const FGeoCoordinate BackToGeo = Converter->UnrealToGeo(ArbitraryWorld);
	const FVector BackToWorld = Converter->GeoToUnreal(BackToGeo);
	TestTrue(TEXT("Welt-Roundtrip X"), FMath::IsNearlyEqual(BackToWorld.X, ArbitraryWorld.X, 1e-3));
	TestTrue(TEXT("Welt-Roundtrip Y"), FMath::IsNearlyEqual(BackToWorld.Y, ArbitraryWorld.Y, 1e-3));
	TestTrue(TEXT("Welt-Roundtrip Z"), FMath::IsNearlyEqual(BackToWorld.Z, ArbitraryWorld.Z, 1e-3));

	// Bounding-Box -> Unreal-Box ist gueltig und umschliesst die Ecken.
	const FBox Box = Converter->GeoBoundsToUnrealBox(
		UGeoCoordinateConverter::GetWiesbadenBounds(), -1000.0, 1000.0);
	TestTrue(TEXT("GeoBounds -> gueltige Box"), Box.IsValid > 0);
	TestTrue(TEXT("GeoBounds -> Box > 0 ausgedehnt"), Box.GetSize().GetMax() > 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoCoordinateConverterMetricTest,
	"WiesbadenReal.GIS.GeoCoordinateConverter.Metric",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGeoCoordinateConverterMetricTest::RunTest(const FString& Parameters)
{
	// 1 Breitengrad ~= 111.2 km (auf 50 Grad etwas mehr wegen Ellipsoid).
	const double MetersPerDegLat = UGeoCoordinateConverter::MetersPerDegreeLatitude(50.0);
	TestTrue(TEXT("MetersPerDegreeLatitude(50) ~ 111 km"),
		MetersPerDegLat > 110000.0 && MetersPerDegLat < 112000.0);

	// 1 Laengengrad auf 50 Grad ~= 71.5 km (cos(50) ~ 0.643).
	const double MetersPerDegLon = UGeoCoordinateConverter::MetersPerDegreeLongitude(50.0);
	TestTrue(TEXT("MetersPerDegreeLongitude(50) ~ 71 km"),
		MetersPerDegLon > 70000.0 && MetersPerDegLon < 73000.0);

	// Haversine: 0.01 Grad Nord = ~1111 m.
	const FGeoCoordinate A(8.24, 50.0824, 0.0);
	const FGeoCoordinate B(8.24, 50.0924, 0.0);
	const double Haversine = UGeoCoordinateConverter::HaversineDistanceMeters(A, B);
	TestTrue(TEXT("Haversine 0.01 Grad ~ 1111 m"), Haversine > 1060.0 && Haversine < 1160.0);

	// ECEF- und Haversine-Distanz duerfen nur minimal abweichen (Stadtdistanzen).
	const double ECEF = UGeoCoordinateConverter::ECEFDistanceMeters(A, B);
	TestTrue(TEXT("ECEF ~ Haversine bei kurzer Distanz"),
		FMath::IsNearlyEqual(ECEF, Haversine, 1.0));

	// Geodetic <-> ECEF Round-Trip ist exakt.
	const FGeoCoordinate Coord(8.24, 50.0824, 150.0);
	const FGeoCoordinate Back = UGeoCoordinateConverter::ECEFToGeodetic(
		UGeoCoordinateConverter::GeodeticToECEF(Coord));
	TestTrue(TEXT("ECEF-Roundtrip Lon"), FMath::IsNearlyEqual(Back.Longitude, Coord.Longitude, 1e-9));
	TestTrue(TEXT("ECEF-Roundtrip Lat"), FMath::IsNearlyEqual(Back.Latitude, Coord.Latitude, 1e-9));
	TestTrue(TEXT("ECEF-Roundtrip Hoehe"), FMath::IsNearlyEqual(Back.Height, Coord.Height, 1e-6));

	return true;
}
