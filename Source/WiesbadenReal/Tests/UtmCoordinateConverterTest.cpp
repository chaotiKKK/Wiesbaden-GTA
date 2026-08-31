// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/UtmCoordinateConverter.h"

/**
 * UTM-Transformation fuer amtliche deutsche Geodaten (ETRS89/UTM32).
 *
 * Ein Fehler hier ist besonders heimtueckisch: die Geometrie bleibt in sich
 * stimmig, liegt aber als Ganzes verschoben. Ein Vorzeichenfehler im Ostwert
 * spiegelt die Stadt am Mittelmeridian, ein falscher Zonen-Mittelmeridian
 * verschiebt sie um Hunderte Kilometer - beides sieht auf einer leeren Karte
 * unauffaellig aus. Geprueft wird daher gegen nachrechenbare Bezugspunkte und
 * auf Umkehrbarkeit.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUtmCoordinateConverterTest,
	"WiesbadenReal.GIS.UtmCoordinateConverter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FUtmCoordinateConverterTest::RunTest(const FString& Parameters)
{
	// -- 1. Mittelmeridian der Zonen ----------------------------------------
	TestEqual(TEXT("Zone 32 hat den Mittelmeridian 9 Grad Ost"),
		FUtmCoordinateConverter::GetZoneCentralMeridian(32), 9.0);
	TestEqual(TEXT("Zone 33 hat den Mittelmeridian 15 Grad Ost"),
		FUtmCoordinateConverter::GetZoneCentralMeridian(33), 15.0);
	TestEqual(TEXT("Zone 31 hat den Mittelmeridian 3 Grad Ost"),
		FUtmCoordinateConverter::GetZoneCentralMeridian(31), 3.0);

	TestEqual(TEXT("Wiesbaden (8.24 Ost) liegt in Zone 32"),
		FUtmCoordinateConverter::GetZoneForLongitude(8.24), 32);
	TestEqual(TEXT("Berlin (13.4 Ost) liegt in Zone 33"),
		FUtmCoordinateConverter::GetZoneForLongitude(13.4), 33);

	// -- 2. Punkt auf dem Mittelmeridian ------------------------------------
	// Auf dem Mittelmeridian ist der Ostwert per Definition exakt 500000.
	// Das ist der einzige Punkt, an dem sich die Projektion ohne Reihen-
	// entwicklung von Hand nachrechnen laesst.
	{
		const FGeoCoordinate OnMeridian =
			FUtmCoordinateConverter::UtmToGeographic(500000.0, 5540000.0, 32);

		TestTrue(TEXT("Ostwert 500000 liegt exakt auf 9 Grad Ost"),
			FMath::IsNearlyEqual(OnMeridian.Longitude, 9.0, 1e-9));
		TestTrue(TEXT("Breite liegt plausibel in Hessen"),
			OnMeridian.Latitude > 49.9 && OnMeridian.Latitude < 50.1);
	}

	// -- 3. Echter ALKIS-Punkt aus dem Wiesbadener Datensatz ----------------
	// Erster Stuetzpunkt des Gebaeudes DEHE062000016fUX.
	// Gegenprobe von Hand: 450096.9 liegt 49903 m westlich des
	// Mittelmeridians; bei 50 Grad Breite sind 1 Grad Laenge rund 71.700 m,
	// also etwa 0.696 Grad -> 9.0 - 0.696 = 8.304 Grad Ost.
	{
		const FGeoCoordinate Alkis =
			FUtmCoordinateConverter::UtmToGeographic(450096.921, 5538831.749, 32);

		TestTrue(FString::Printf(TEXT("ALKIS-Punkt liegt bei 8.3037 Ost (ist %.5f)"), Alkis.Longitude),
			FMath::IsNearlyEqual(Alkis.Longitude, 8.30368, 1e-4));
		TestTrue(FString::Printf(TEXT("ALKIS-Punkt liegt bei 49.9997 Nord (ist %.5f)"), Alkis.Latitude),
			FMath::IsNearlyEqual(Alkis.Latitude, 49.99972, 1e-4));

		// Der Punkt muss westlich des Mittelmeridians liegen - ein
		// Vorzeichenfehler im Ostwert wuerde ihn nach 9.7 Grad spiegeln.
		TestTrue(TEXT("Punkt liegt westlich des Mittelmeridians"), Alkis.Longitude < 9.0);
	}

	// -- 4. Umkehrbarkeit ---------------------------------------------------
	// Hin- und Rueckrechnung ueber das Stadtgebiet. Toleranz 1 mm - die
	// Reihenentwicklung ist innerhalb einer Zone deutlich genauer als das.
	{
		const FGeoCoordinate Samples[] = {
			FGeoCoordinate(8.2400, 50.0824, 0.0),   // Schlossplatz
			FGeoCoordinate(8.2234, 50.0933, 0.0),   // Platter Strasse 144
			FGeoCoordinate(8.3037, 49.9997, 0.0),   // Mainz-Kastel
			FGeoCoordinate(8.4200, 50.1600, 0.0),   // Nordostecke der Bbox
			FGeoCoordinate(9.0000, 50.0000, 0.0),   // auf dem Mittelmeridian
		};

		for (const FGeoCoordinate& Sample : Samples)
		{
			double Easting = 0.0;
			double Northing = 0.0;

			TestTrue(TEXT("Hinrechnung gelingt"),
				FUtmCoordinateConverter::GeographicToUtm(Sample, 32, Easting, Northing));

			// Ostwerte muessen im gueltigen UTM-Band liegen.
			TestTrue(FString::Printf(TEXT("Ostwert plausibel bei %.4f Ost (ist %.1f)"),
					Sample.Longitude, Easting),
				Easting > 100000.0 && Easting < 900000.0);

			const FGeoCoordinate Back =
				FUtmCoordinateConverter::UtmToGeographic(Easting, Northing, 32);

			// 1e-8 Grad entspricht rund 1 mm.
			TestTrue(FString::Printf(TEXT("Laenge umkehrbar (%.7f -> %.7f)"),
					Sample.Longitude, Back.Longitude),
				FMath::IsNearlyEqual(Back.Longitude, Sample.Longitude, 1e-8));
			TestTrue(FString::Printf(TEXT("Breite umkehrbar (%.7f -> %.7f)"),
					Sample.Latitude, Back.Latitude),
				FMath::IsNearlyEqual(Back.Latitude, Sample.Latitude, 1e-8));
		}
	}

	// -- 5. Zonenkennziffer im Ostwert --------------------------------------
	// Amtliche Daten liefern beide Schreibweisen. Wird die Zonenkennziffer
	// nicht abgetrennt, liegt der Punkt 32 Millionen Meter zu weit oestlich.
	{
		int32 Zone = 0;
		double Easting = 0.0;

		FUtmCoordinateConverter::SplitZonePrefixedEasting(32450096.921, Zone, Easting);
		TestEqual(TEXT("Zonenkennziffer 32 wird erkannt"), Zone, 32);
		TestTrue(TEXT("Ostwert nach Abtrennung korrekt"),
			FMath::IsNearlyEqual(Easting, 450096.921, 1e-3));

		FUtmCoordinateConverter::SplitZonePrefixedEasting(450096.921, Zone, Easting);
		TestEqual(TEXT("Ostwert ohne Kennziffer meldet Zone 0"), Zone, 0);
		TestTrue(TEXT("Ostwert bleibt unveraendert"),
			FMath::IsNearlyEqual(Easting, 450096.921, 1e-3));
	}

	// -- 6. Fehlerfaelle ----------------------------------------------------
	TestFalse(TEXT("Ungueltige Zone wird abgelehnt"),
		FUtmCoordinateConverter::UtmToGeographic(450000.0, 5540000.0, 0).IsValid());
	TestFalse(TEXT("Zone 61 wird abgelehnt"),
		FUtmCoordinateConverter::UtmToGeographic(450000.0, 5540000.0, 61).IsValid());

	double DummyE = 0.0;
	double DummyN = 0.0;
	TestFalse(TEXT("Ungueltige Koordinate wird abgelehnt"),
		FUtmCoordinateConverter::GeographicToUtm(FGeoCoordinate(400.0, 400.0, 0.0), 32, DummyE, DummyN));

	return true;
}
