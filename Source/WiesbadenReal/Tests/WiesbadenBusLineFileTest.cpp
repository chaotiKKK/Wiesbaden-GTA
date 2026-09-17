// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/GeoCoordinateConverter.h"
#include "World/WiesbadenBusLineFile.h"

/**
 * Der gemeinsame Leser der Liniendatei: parst die echten Dateien unter
 * Data/Raw/Bus und legt sie in Weltkoordinaten. Geprueft wird, dass Bus-Actor
 * und Haltestellenmonitor wirklich DASSELBE Objekt bekommen (nicht zwei gleiche
 * Kopien), dass die Route in sich stimmig ist und dass fehlende Dateien nicht
 * abstuerzen.
 *
 * Bewusst ohne festgeschriebene Zahlen aus der Datei (Haltezahl, Laenge): die
 * Linie waechst mit jeder OSM-Aenderung. Geprueft werden die Eigenschaften, die
 * gelten muessen, egal wie lang die Linie gerade ist - plus die Laenge gegen den
 * eigenen Pfad nachgerechnet.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenBusLineFileTest,
	"WiesbadenReal.Traffic.BusLineFile",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWiesbadenBusLineFileTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenBusLineFile;

	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	if (!TestNotNull(TEXT("Konverter erzeugt"), Converter)) { return false; }
	if (!TestTrue(TEXT("Wiesbaden-Origin initialisiert"), Converter->InitializeWithWiesbadenOrigin()))
	{
		return false;
	}

	ClearCache();
	const TSharedRef<const FLineRoute> Line6 = ReadLine(TEXT("line6.json"), *Converter);
	if (!TestTrue(TEXT("line6.json gelesen"), Line6->File.bLoaded)) { return false; }

	TestEqual(TEXT("Liniennummer aus der Datei"), Line6->File.Ref, FString(TEXT("6")));
	TestTrue(TEXT("Pfad vorhanden"), Line6->File.GeoPath.Num() > 100);
	TestTrue(TEXT("Halte vorhanden"), Line6->File.GeoStops.Num() >= 30);
	TestEqual(TEXT("ein Name je Halte"),
		Line6->File.StopNames.Num(), Line6->File.GeoStops.Num());
	// `monitor_stops` steht als "*" in der Datei (= alle Halte bekommen eine
	// Saeule). Gelesen muessen dort die HALTENAMEN stehen, sonst findet der Monitor
	// keine einzige Saeule und stellt stillschweigend nichts auf.
	TestEqual(TEXT("Monitorhalte sind die Haltenamen (Platzhalter '*' aufgeloest)"),
		Line6->File.MonitorStops.Num(), Line6->File.StopNames.Num());
	TestTrue(TEXT("kein Platzhalter mehr in der Liste"),
		Line6->File.MonitorStops.IndexOfByKey(FString(TEXT("*"))) == INDEX_NONE);
	TestTrue(TEXT("Wendezeit 10 min aus der Datei"),
		FMath::IsNearlyEqual(Line6->File.TerminusDwellSeconds, 600.0, 0.001));
	TestTrue(TEXT("Zieltext aus der Datei"), !Line6->File.Destination.IsEmpty());
	// Der andere Endpunkt: die Saeule auf der Gegenseite zeigt die Gegenrichtung
	// und braucht deren Zieltext. Fehlt er, stuende auf beiden Tafeln dasselbe Ziel.
	TestTrue(TEXT("Zieltext der Gegenrichtung aus der Datei"), !Line6->File.Origin.IsEmpty());
	TestTrue(TEXT("die beiden Endpunkte sind verschieden"), Line6->File.Origin != Line6->File.Destination);
	TestTrue(TEXT("Zielschilder aus der Datei"),
		!Line6->File.Blinds.Forward.IsEmpty() && !Line6->File.Blinds.Backward.IsEmpty()
		&& !Line6->File.Blinds.Line.IsEmpty());

	// Route: Bogenlaengen streng monoton, Gesamtlaenge = letzter Wert, Halte
	// sortiert und auf der Linie. Ein Halte-Bogen ausserhalb [0, Laenge] hiesse,
	// dass Halte und Fahrlinie zwei verschiedene Geometrien sind.
	TestEqual(TEXT("ein Pfadpunkt je Bogenlaenge"), Line6->ArcCm.Num(), Line6->WorldPath.Num());
	TestTrue(TEXT("Bogenlaenge beginnt bei 0"), FMath::IsNearlyEqual(Line6->ArcCm[0], 0.0, 0.01));
	bool bMonotone = true;
	for (int32 i = 1; i < Line6->ArcCm.Num(); ++i)
	{
		if (Line6->ArcCm[i] <= Line6->ArcCm[i - 1]) { bMonotone = false; }
	}
	TestTrue(TEXT("Bogenlaengen streng monoton steigend"), bMonotone);
	TestTrue(TEXT("Gesamtlaenge = letzter Bogenwert"),
		FMath::IsNearlyEqual(Line6->Route.TotalLengthCm, Line6->ArcCm.Last(), 0.01));

	const double Total = Line6->Route.TotalLengthCm;
	bool bStopsOk = Line6->Route.StopArcCm.Num() >= 2;
	for (int32 i = 0; i < Line6->Route.StopArcCm.Num(); ++i)
	{
		const double S = Line6->Route.StopArcCm[i];
		if (S < 0.0 || S > Total) { bStopsOk = false; }
		if (i > 0 && S < Line6->Route.StopArcCm[i - 1]) { bStopsOk = false; }
	}
	TestTrue(TEXT("Halte-Bogenlaengen aufsteigend und auf der Linie"), bStopsOk);

	// Laenge gegen den eigenen Pfad nachgerechnet (Grosskreis): die Projektion ist
	// eine lokale Tangentialebene, also muss sie die Pfadlaenge innerhalb eines
	// Prozents treffen. Faengt Achsen-/Skalierungsfehler in der Projektion.
	double GeoLengthCm = 0.0;
	for (int32 i = 1; i < Line6->File.GeoPath.Num(); ++i)
	{
		const FVector2D A = Line6->File.GeoPath[i - 1];
		const FVector2D B = Line6->File.GeoPath[i];
		const double LatRad = FMath::DegreesToRadians((A.X + B.X) * 0.5);
		const double DX = FMath::DegreesToRadians(B.Y - A.Y) * 6371000.0 * FMath::Cos(LatRad);
		const double DY = FMath::DegreesToRadians(B.X - A.X) * 6371000.0;
		GeoLengthCm += FMath::Sqrt(DX * DX + DY * DY) * 100.0;
	}
	TestTrue(TEXT("Projizierte Laenge entspricht der Pfadlaenge (< 1 %)"),
		FMath::Abs(Total - GeoLengthCm) < GeoLengthCm * 0.01);

	// Achslage: der Nordpunkt liegt noerdlich (kleineres Y) und oestlich (groesseres
	// X) des Suedpunkts. Vertauschte Achsen waeren sonst erst im Spiel zu sehen.
	const FVector First = Line6->WorldPath[0];
	const FVector Last = Line6->WorldPath.Last();
	TestTrue(TEXT("Achsen: Suedpunkt hat groesseres Y (Sueden = +Y)"), Last.Y > First.Y);
	TestTrue(TEXT("Achsen: Suedpunkt hat kleineres X (Osten = +X)"), Last.X < First.X);

	// Der Kern der Sache: der zweite Aufruf liefert DASSELBE Objekt, nicht eine
	// zweite Kopie. Genau so bekommen Bus-Actor und Haltestellenmonitor einer
	// Linie dieselbe geparste Route.
	const TSharedRef<const FLineRoute> Again = ReadLine(TEXT("line6.json"), *Converter);
	TestTrue(TEXT("zweiter Aufruf: dasselbe Objekt"),
		&Line6.Get() == &Again.Get());
	TestTrue(TEXT("dieselben Pfadpunkte (kein zweiter Parser)"),
		Line6->WorldPath.GetData() == Again->WorldPath.GetData());

	// Linie 3 ebenso (die zweite Linie muss aus derselben Quelle kommen).
	const TSharedRef<const FLineRoute> Line3 = ReadLine(TEXT("line3.json"), *Converter);
	TestTrue(TEXT("line3.json gelesen"), Line3->File.bLoaded);
	TestEqual(TEXT("Liniennummer Linie 3"), Line3->File.Ref, FString(TEXT("3")));
	TestTrue(TEXT("Linie 3: Halte vorhanden"), Line3->File.GeoStops.Num() >= 15);
	TestTrue(TEXT("Linie 3: Route gebaut"),
		Line3->WorldPath.Num() > 100 && Line3->Route.StopArcCm.Num() >= 2);
	TestTrue(TEXT("Linie 3: anderes Objekt als Linie 6"), &Line3.Get() != &Line6.Get());

	// Fahrplan: lesbar, sortiert, innerhalb des Tages.
	const TSharedRef<const WiesbadenBusLine::FBusSchedule> Plan =
		ReadSchedule(TEXT("line6_schedule.json"));
	TestTrue(TEXT("Fahrplan gelesen"),
		Plan->DepartureSeconds.Num() > 0 && Plan->DaySeconds > 0.0);
	bool bSorted = true;
	for (int32 i = 1; i < Plan->DepartureSeconds.Num(); ++i)
	{
		if (Plan->DepartureSeconds[i] < Plan->DepartureSeconds[i - 1]) { bSorted = false; }
	}
	TestTrue(TEXT("Abfahrten aufsteigend"), bSorted);
	TestTrue(TEXT("Abfahrten innerhalb des Tages"),
		Plan->DepartureSeconds.Last() < Plan->DaySeconds);

	// Fehlende Dateien: kein Absturz, sondern ein leerer Datensatz.
	const TSharedRef<const FLineRoute> Missing = ReadLine(TEXT("line_gibtsnicht.json"), *Converter);
	TestFalse(TEXT("fehlende Liniendatei: bLoaded false"), Missing->File.bLoaded);
	TestEqual(TEXT("fehlende Liniendatei: keine Route"), Missing->WorldPath.Num(), 0);
	const TSharedRef<const WiesbadenBusLine::FBusSchedule> NoPlan = ReadSchedule(FString());
	TestEqual(TEXT("ohne Fahrplandatei: leerer Fahrplan"), NoPlan->DepartureSeconds.Num(), 0);
	TestTrue(TEXT("ohne Fahrplandatei: Tageslaenge gesetzt"), NoPlan->DaySeconds > 0.0);

	// Nur der Cache, nicht die Daten: ClearCache darf die gehaltenen Zeiger nicht
	// ungueltig machen (sie zaehlen selbst).
	ClearCache();
	TestTrue(TEXT("Daten ueberleben den Cache-Reset"), Line6->WorldPath.Num() > 100);
	return true;
}
