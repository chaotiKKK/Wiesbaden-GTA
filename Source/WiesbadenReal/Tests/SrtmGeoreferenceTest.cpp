// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/HeightmapImporter.h"

/**
 * Georeferenzierung von SRTM-Kacheln (.hgt) aus dem Dateinamen.
 *
 * WARUM DIESER TEST EXISTIERT: Hier lag ein Fehler, der monatelang unbemerkt
 * blieb, weil er nichts kaputt machte - er verschob nur die Welt.
 * ImportSrtmHgt las "N50E008" als NORDWEST-Ecke und rechnete MinLatitude =
 * 50 - 1 = 49. Tatsaechlich bezeichnet der Name die SUEDWEST-Ecke; die Kachel
 * deckt 50..51 Grad Nord ab.
 *
 * Folge: das Hoehenmodell lag 1 Grad (~111 km) zu weit suedlich. Wiesbaden
 * stand auf dem Relief einer anderen Landschaft. Nichts stuerzte ab, nichts
 * sah offensichtlich falsch aus - es war einfach nicht Wiesbaden. Genau solche
 * Fehler finden Absturz- und Plausibilitaetspruefungen nicht; nur ein Test auf
 * die konkreten Zahlen findet sie.
 *
 * Die Laenge war immer korrekt als Suedwest-Ecke behandelt. Der Test prueft
 * daher beide Achsen getrennt, damit die Asymmetrie nicht zurueckkehrt.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSrtmGeoreferenceTest,
	"WiesbadenReal.GIS.HeightmapImporter.SrtmGeoreference",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSrtmGeoreferenceTest::RunTest(const FString& Parameters)
{
	double Lon = 0.0;
	double Lat = 0.0;

	// -- 1. Die Kachel des Projekts ----------------------------------------
	// N50E008 ist die Kachel, auf der Wiesbaden (50.08 N, 8.24 O) liegt.
	TestTrue(TEXT("N50E008 wird erkannt"),
		UHeightmapImporter::ParseSrtmTileOrigin(TEXT("N50E008.hgt"), Lon, Lat));
	TestEqual(TEXT("N50E008: suedliche Kante liegt bei 50 Grad, nicht 49"), Lat, 50.0);
	TestEqual(TEXT("N50E008: westliche Kante liegt bei 8 Grad"), Lon, 8.0);

	// Der eigentliche Fachtest: Wiesbaden muss INNERHALB der Kachel liegen.
	// Mit dem alten Offset (49..50) lag die Stadt oberhalb des Rasters und
	// wurde auf den Rand geklemmt - daher das fehlende Rheintal.
	constexpr double WiesbadenLat = 50.0824;
	constexpr double WiesbadenLon = 8.2400;
	TestTrue(TEXT("Wiesbaden liegt in der Breitenspanne der Kachel"),
		WiesbadenLat >= Lat && WiesbadenLat <= Lat + 1.0);
	TestTrue(TEXT("Wiesbaden liegt in der Laengenspanne der Kachel"),
		WiesbadenLon >= Lon && WiesbadenLon <= Lon + 1.0);

	// -- 2. Vollstaendiger Pfad statt blossem Namen -------------------------
	TestTrue(TEXT("Vollstaendiger Pfad wird akzeptiert"),
		UHeightmapImporter::ParseSrtmTileOrigin(
			TEXT("C:/Daten/DEM/N50E008.hgt"), Lon, Lat));
	TestEqual(TEXT("Pfad: Breite unveraendert"), Lat, 50.0);
	TestEqual(TEXT("Pfad: Laenge unveraendert"), Lon, 8.0);

	// Kleinschreibung ist zulaessig (manche Anbieter liefern so aus).
	TestTrue(TEXT("Kleinschreibung wird erkannt"),
		UHeightmapImporter::ParseSrtmTileOrigin(TEXT("n50e008.hgt"), Lon, Lat));
	TestEqual(TEXT("Kleinschreibung: Breite"), Lat, 50.0);

	// -- 3. Nachbarkachel ---------------------------------------------------
	// N49E008 deckt 49..50 ab und stoesst genau an N50E008 an. Ohne die
	// Korrektur haetten beide Kacheln dieselbe Breite belegt.
	TestTrue(TEXT("N49E008 wird erkannt"),
		UHeightmapImporter::ParseSrtmTileOrigin(TEXT("N49E008.hgt"), Lon, Lat));
	TestEqual(TEXT("N49E008: suedliche Kante bei 49 Grad"), Lat, 49.0);

	// -- 4. Alle vier Quadranten -------------------------------------------
	// Suedliche Breiten und westliche Laengen sind negativ; die genannte Zahl
	// ist der Betrag der SUEDWEST-Ecke. "S01W001" deckt -1..0 in beiden Achsen.
	TestTrue(TEXT("S01W001 wird erkannt"),
		UHeightmapImporter::ParseSrtmTileOrigin(TEXT("S01W001.hgt"), Lon, Lat));
	TestEqual(TEXT("S01W001: Breite -1"), Lat, -1.0);
	TestEqual(TEXT("S01W001: Laenge -1"), Lon, -1.0);

	TestTrue(TEXT("N00E000 wird erkannt"),
		UHeightmapImporter::ParseSrtmTileOrigin(TEXT("N00E000.hgt"), Lon, Lat));
	TestEqual(TEXT("N00E000: Breite 0"), Lat, 0.0);
	TestEqual(TEXT("N00E000: Laenge 0"), Lon, 0.0);

	// Dreistellige Laenge jenseits von 100 Grad.
	TestTrue(TEXT("S34W071 wird erkannt (Anden)"),
		UHeightmapImporter::ParseSrtmTileOrigin(TEXT("S34W071.hgt"), Lon, Lat));
	TestEqual(TEXT("S34W071: Breite -34"), Lat, -34.0);
	TestEqual(TEXT("S34W071: Laenge -71"), Lon, -71.0);

	TestTrue(TEXT("N27E086 wird erkannt (Himalaya)"),
		UHeightmapImporter::ParseSrtmTileOrigin(TEXT("N27E086.hgt"), Lon, Lat));
	TestEqual(TEXT("N27E086: Breite 27"), Lat, 27.0);
	TestEqual(TEXT("N27E086: Laenge 86"), Lon, 86.0);

	// -- 5. Ungueltige Namen -----------------------------------------------
	// Muessen scheitern statt still eine falsche Georeferenz zu liefern - eine
	// stille Null waere wieder ein Fehler, der wie Gelaende aussieht.
	TestFalse(TEXT("Name ohne Schema wird abgelehnt"),
		UHeightmapImporter::ParseSrtmTileOrigin(TEXT("heightmap.hgt"), Lon, Lat));
	TestFalse(TEXT("Zu kurzer Name wird abgelehnt"),
		UHeightmapImporter::ParseSrtmTileOrigin(TEXT("N50E.hgt"), Lon, Lat));
	TestFalse(TEXT("Leerer Name wird abgelehnt"),
		UHeightmapImporter::ParseSrtmTileOrigin(FString(), Lon, Lat));
	TestFalse(TEXT("Unplausible Breite wird abgelehnt"),
		UHeightmapImporter::ParseSrtmTileOrigin(TEXT("N99E008.hgt"), Lon, Lat));

	return true;
}
