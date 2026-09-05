// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "UI/WiesbadenVehicleHUD.h"
#include "GIS/BuildingGenerator.h"
#include "Vehicles/WiesbadenCarLightsComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleHUDTest,
	"WiesbadenReal.Vehicles.HUD",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Rechenteile des Fahrzeug-HUDs, ohne Welt und ohne Canvas.
 *
 * Bewusst datenrein gehalten: ein Tacho, der bei Leerlauf schon ausschlaegt
 * oder ueber der Skalenendgeschwindigkeit weiterdreht, faellt im laufenden
 * Spiel kaum auf - in Zahlen dagegen sofort.
 */
bool FVehicleHUDTest::RunTest(const FString& Parameters)
{
	// -- Drehzahlband --------------------------------------------------------
	{
		constexpr float Idle = 850.0f;
		constexpr float Max = 6500.0f;

		// Im Leerlauf muss der Balken LEER sein. Bezugspunkt ist die
		// Leerlaufdrehzahl, nicht null - sonst stuende der Motor im Stand
		// bereits bei rund einem Achtel.
		TestTrue(TEXT("Leerlauf -> 0"),
			FMath::IsNearlyEqual(AWiesbadenVehicleHUD::ComputeRpmFill(Idle, Idle, Max), 0.0f, 1e-4f));

		TestTrue(TEXT("Hoechstdrehzahl -> 1"),
			FMath::IsNearlyEqual(AWiesbadenVehicleHUD::ComputeRpmFill(Max, Idle, Max), 1.0f, 1e-4f));

		// Mitte der Spanne.
		const float Middle = AWiesbadenVehicleHUD::ComputeRpmFill((Idle + Max) * 0.5f, Idle, Max);
		TestTrue(TEXT("Mitte -> 0.5"), FMath::IsNearlyEqual(Middle, 0.5f, 1e-3f));

		// Unterhalb des Leerlaufs und oberhalb der Grenze wird geklemmt.
		TestTrue(TEXT("Unter Leerlauf geklemmt"),
			AWiesbadenVehicleHUD::ComputeRpmFill(0.0f, Idle, Max) == 0.0f);
		TestTrue(TEXT("Ueber Maximum geklemmt"),
			AWiesbadenVehicleHUD::ComputeRpmFill(99999.0f, Idle, Max) == 1.0f);

		// Entartete Spanne darf nicht durch Null teilen.
		TestTrue(TEXT("Spanne 0 -> 0"),
			AWiesbadenVehicleHUD::ComputeRpmFill(1000.0f, 1000.0f, 1000.0f) == 0.0f);
	}

	// -- Tachozeiger ---------------------------------------------------------
	{
		constexpr float MaxKmh = 140.0f;
		constexpr float Sweep = 240.0f;

		TestTrue(TEXT("Stillstand -> 0 Grad"),
			FMath::IsNearlyEqual(AWiesbadenVehicleHUD::ComputeNeedleAngleDegrees(0.0f, MaxKmh, Sweep), 0.0f, 1e-4f));

		TestTrue(TEXT("Skalenende -> voller Ausschlag"),
			FMath::IsNearlyEqual(AWiesbadenVehicleHUD::ComputeNeedleAngleDegrees(MaxKmh, MaxKmh, Sweep), Sweep, 1e-3f));

		TestTrue(TEXT("Halbe Geschwindigkeit -> halber Ausschlag"),
			FMath::IsNearlyEqual(
				AWiesbadenVehicleHUD::ComputeNeedleAngleDegrees(MaxKmh * 0.5f, MaxKmh, Sweep),
				Sweep * 0.5f, 1e-3f));

		// Ueber der Skala bleibt der Zeiger am Anschlag statt weiterzudrehen.
		TestTrue(TEXT("Ueber Skalenende bleibt am Anschlag"),
			FMath::IsNearlyEqual(
				AWiesbadenVehicleHUD::ComputeNeedleAngleDegrees(400.0f, MaxKmh, Sweep), Sweep, 1e-3f));

		TestTrue(TEXT("Skalenende 0 -> 0 Grad"),
			AWiesbadenVehicleHUD::ComputeNeedleAngleDegrees(50.0f, 0.0f, Sweep) == 0.0f);
	}

	// -- Gang und Lichtstufe -------------------------------------------------
	{
		TestEqual(TEXT("Rueckwaerts"), AWiesbadenVehicleHUD::FormatGear(-1), FString(TEXT("R")));
		TestEqual(TEXT("Leerlauf"), AWiesbadenVehicleHUD::FormatGear(0), FString(TEXT("N")));
		TestEqual(TEXT("Dritter Gang"), AWiesbadenVehicleHUD::FormatGear(3), FString(TEXT("3")));

		TestEqual(TEXT("Licht aus"),
			AWiesbadenVehicleHUD::FormatHeadlightMode(static_cast<uint8>(EWiesbadenHeadlightMode::Off)),
			FString(TEXT("AUS")));
		TestEqual(TEXT("Abblendlicht"),
			AWiesbadenVehicleHUD::FormatHeadlightMode(static_cast<uint8>(EWiesbadenHeadlightMode::LowBeam)),
			FString(TEXT("ABBLEND")));
		TestEqual(TEXT("Fernlicht"),
			AWiesbadenVehicleHUD::FormatHeadlightMode(static_cast<uint8>(EWiesbadenHeadlightMode::HighBeam)),
			FString(TEXT("FERN")));
	}

	// -- Steuerkurs (Helikopter-Cockpit) -------------------------------------
	{
		TestEqual(TEXT("Nord"), AWiesbadenVehicleHUD::FormatHeading(0.0f), FString(TEXT("N 000")));
		TestEqual(TEXT("Ost"), AWiesbadenVehicleHUD::FormatHeading(90.0f), FString(TEXT("O 090")));
		TestEqual(TEXT("Sued"), AWiesbadenVehicleHUD::FormatHeading(180.0f), FString(TEXT("S 180")));
		TestEqual(TEXT("West"), AWiesbadenVehicleHUD::FormatHeading(270.0f), FString(TEXT("W 270")));
		TestEqual(TEXT("Nordost"), AWiesbadenVehicleHUD::FormatHeading(45.0f), FString(TEXT("NO 045")));
		// Ueberlauf: 360 -> 0 = Nord, negativ wird normalisiert.
		TestEqual(TEXT("360 = Nord"), AWiesbadenVehicleHUD::FormatHeading(360.0f), FString(TEXT("N 000")));
		TestEqual(TEXT("-90 = West"), AWiesbadenVehicleHUD::FormatHeading(-90.0f), FString(TEXT("W 270")));
		// Sektor rundet: 22 Grad zaehlt noch als Nord, 23 als Nordost.
		TestEqual(TEXT("22 Grad -> N"), AWiesbadenVehicleHUD::FormatHeading(22.0f), FString(TEXT("N 022")));
		TestEqual(TEXT("23 Grad -> NO"), AWiesbadenVehicleHUD::FormatHeading(23.0f), FString(TEXT("NO 023")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleHUDControlLegendTest,
	"WiesbadenReal.Vehicles.HUD.ControlLegend",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Tastenlegende muss die tragenden Belegungen nennen.
 *
 * Die Steuerung wird gepollt statt ueber Input-Assets gebunden - es gibt also
 * keine Datei, aus der ein Spieler die Belegung ablesen koennte, und im Spiel
 * stand sie bis jetzt nirgends. Die Legende ist damit die EINZIGE Quelle. Wird
 * eine Taste im Code geaendert und hier vergessen, zeigt das HUD eine falsche
 * Auskunft an - schlimmer als gar keine.
 *
 * Geprueft werden deshalb nicht Formulierungen, sondern die Tasten selbst.
 */
bool FVehicleHUDControlLegendTest::RunTest(const FString& Parameters)
{
	TArray<FString> VehicleLines;
	AWiesbadenVehicleHUD::GetControlLegendLines(/*bInVehicle=*/true, VehicleLines);

	TestTrue(TEXT("Fahrzeug-Legende ist nicht leer"), VehicleLines.Num() > 0);

	const FString VehicleText = FString::Join(VehicleLines, TEXT("\n"));

	// AWiesbadenCar::ReadInput und ApplyVehiclePhysics.
	TestTrue(TEXT("Gas genannt"), VehicleText.Contains(TEXT("Gas")));
	TestTrue(TEXT("Bremse genannt"), VehicleText.Contains(TEXT("Bremse")));
	TestTrue(TEXT("Rueckwaerts im Stand genannt"), VehicleText.Contains(TEXT("rueckwaerts")));
	TestTrue(TEXT("Handbremse genannt"), VehicleText.Contains(TEXT("Leertaste")));
	TestTrue(TEXT("Lenken genannt"), VehicleText.Contains(TEXT("Lenken")));
	TestTrue(TEXT("Blinker genannt"), VehicleText.Contains(TEXT("Blinker")));
	TestTrue(TEXT("Licht genannt"), VehicleText.Contains(TEXT("Licht")));

	// AWiesbadenGameMode::Tick - Ein- und Aussteigen liegt auf F.
	TestTrue(TEXT("Aussteigen genannt"), VehicleText.Contains(TEXT("Aussteigen")));

	TArray<FString> FootLines;
	AWiesbadenVehicleHUD::GetControlLegendLines(/*bInVehicle=*/false, FootLines);

	TestTrue(TEXT("Fuss-Legende ist nicht leer"), FootLines.Num() > 0);

	const FString FootText = FString::Join(FootLines, TEXT("\n"));

	// AWiesbadenFootPawn::ReadInput.
	TestTrue(TEXT("Gehen genannt"), FootText.Contains(TEXT("Gehen")));
	TestTrue(TEXT("Rennen genannt"), FootText.Contains(TEXT("Rennen")));
	TestTrue(TEXT("Umsehen genannt"), FootText.Contains(TEXT("Umsehen")));
	// Die Waffe zu Fuss IST die Kettensaege - Sebbo hat beide Haende daran.
	// Stuende hier weiter "Schiessen", verspraeche die Legende eine Pistole,
	// die es nicht mehr gibt.
	TestTrue(TEXT("Kettensaege genannt"), FootText.Contains(TEXT("Kettensaege")));
	TestTrue(TEXT("Springen genannt"), FootText.Contains(TEXT("Springen")));
	TestTrue(TEXT("Einsteigen genannt"), FootText.Contains(TEXT("Einsteigen")));
	TestTrue(TEXT("Nerobergbahn genannt"), FootText.Contains(TEXT("Nerobergbahn")));

	// Die beiden Legenden duerfen sich nicht gleichen - sonst zeigt eine von
	// beiden die falsche Belegung an.
	TestTrue(TEXT("Fahrzeug- und Fuss-Legende unterscheiden sich"), VehicleText != FootText);

	// -- Handlungshinweis zu Fuss --------------------------------------------
	//
	// Zu Fuss war der Schirm bis auf die Legende LEER: keine Karte, kein
	// Strassenname, kein Hinweis, dass an der Talstation E mitfahren laesst.
	// Der Hinweis entscheidet nach Entfernung; die Auswahl ist hier geprueft,
	// weil sie im Spiel nur an genau der richtigen Stelle sichtbar wird.
	{
		using FHud = AWiesbadenVehicleHUD;
		constexpr double CarReach = 600.0;
		constexpr double RailReach = 1200.0;

		TestTrue(TEXT("Nichts in der Naehe -> kein Hinweis"),
			FHud::BuildFootPrompt(5000.0, 9000.0, CarReach, RailReach).IsEmpty());

		TestTrue(TEXT("Wagen in Reichweite -> Einsteigen"),
			FHud::BuildFootPrompt(300.0, 9000.0, CarReach, RailReach).Contains(TEXT("Einsteigen")));

		TestTrue(TEXT("Bahn in Reichweite -> Mitfahren"),
			FHud::BuildFootPrompt(5000.0, 800.0, CarReach, RailReach).Contains(TEXT("Nerobergbahn")));

		// Beides erreichbar: das Naehere gewinnt. Ohne diese Regel blinkten an
		// der Talstation neben dem geparkten Wagen zwei Hinweise uebereinander.
		TestTrue(TEXT("Beides: Wagen naeher -> Einsteigen"),
			FHud::BuildFootPrompt(200.0, 900.0, CarReach, RailReach).Contains(TEXT("Einsteigen")));
		TestTrue(TEXT("Beides: Bahn naeher -> Mitfahren"),
			FHud::BuildFootPrompt(550.0, 300.0, CarReach, RailReach).Contains(TEXT("Nerobergbahn")));

		// Negativ heisst "gibt es in dieser Welt nicht" und darf nicht als
		// Entfernung 0 durchgehen - sonst stuende der Hinweis dauerhaft da.
		TestTrue(TEXT("Kein Fahrzeug vorhanden -> kein Hinweis"),
			FHud::BuildFootPrompt(-1.0, -1.0, CarReach, RailReach).IsEmpty());
	}

	return true;
}

// Weltkarte: die datenreine Fit-Projektion (ganzes Netz -> Bildschirm, Norden
// oben). Ohne Canvas/Welt pruefbar - das Zeichnen selbst ist der triviale Teil.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapProjectionTest,
	"WiesbadenReal.World.WorldMapProjection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWorldMapProjectionTest::RunTest(const FString& Parameters)
{
	// Netz-Grenzen 0..1000 (Ost) x 0..500 (Nord) cm; Bild 800x600, Mitte (400,300).
	const FVector2D WMin(0.0, 0.0);
	const FVector2D WMax(1000.0, 500.0);
	const FWorldMapProjection Proj = FWiesbadenMinimap::MakeWorldMapProjection(
		WMin, WMax, FVector2D(400.0, 300.0), FVector2D(800.0, 600.0), /*MarginFrac=*/1.0f);

	// Gleichmaessiger Massstab, begrenzt durch die WEITE Achse (Ost): 800/1000 = 0.8.
	TestTrue(TEXT("Massstab an der engeren Passung (0.8 px/cm)"),
		FMath::IsNearlyEqual(Proj.ScalePxPerCm, 0.8f, 0.001f));

	// Weltmitte -> Bildschirmmitte.
	const FVector2D C = Proj.Project(FVector(500.0, 250.0, 0.0));
	TestTrue(TEXT("Weltmitte -> Bildschirmmitte"), C.Equals(FVector2D(400.0, 300.0), 0.01));

	// Norden oben: weiter noerdlich (groesseres Welt-Y) -> HOEHER (kleineres Bild-Y).
	const FVector2D North = Proj.Project(FVector(500.0, 500.0, 0.0));
	TestTrue(TEXT("Norden ist oben"), North.Y < C.Y);

	// Osten rechts: groesseres Welt-X -> groesseres Bild-X; Rand bei +400 px (250cm*0.8... 500cm*0.8=400).
	const FVector2D East = Proj.Project(FVector(1000.0, 250.0, 0.0));
	TestTrue(TEXT("Osten ist rechts"), East.X > C.X);
	TestTrue(TEXT("Ostrand bei 800 px (500 cm * 0.8)"),
		FMath::IsNearlyEqual(East.X, 800.0, 0.01));

	// Huellbox aus einem Mini-Netz.
	FRoadNetwork Net;
	FRoadSegment Seg;
	Seg.Centerline = { FVector(10.0, 20.0, 0.0), FVector(110.0, 220.0, 0.0) };
	Net.Segments.Add(Seg);
	FVector2D BMin, BMax;
	TestTrue(TEXT("Huellbox gefunden"),
		FWiesbadenMinimap::ComputeNetworkBoundsXY(Net, BMin, BMax));
	TestTrue(TEXT("Huellbox Min"), BMin.Equals(FVector2D(10.0, 20.0), 0.01));
	TestTrue(TEXT("Huellbox Max"), BMax.Equals(FVector2D(110.0, 220.0), 0.01));

	// Leeres Netz -> keine Huellbox.
	FRoadNetwork Empty;
	FVector2D E1, E2;
	TestFalse(TEXT("Leeres Netz -> keine Huellbox"),
		FWiesbadenMinimap::ComputeNetworkBoundsXY(Empty, E1, E2));

	return true;
}

// Regression: eine lange Strasse mit DICHT liegenden Punkten muss bei
// Stadt-Zoom Linien ergeben, nicht null. Der erste Wurf cullte je Teilsegment
// und lieferte aus 125.000 Segmenten 0 Linien - die Karte war leer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapDecimationTest,
	"WiesbadenReal.World.WorldMapDecimation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWorldMapDecimationTest::RunTest(const FString& Parameters)
{
	// Diagonale ~1-km-Strasse mit einem Punkt je ~104 cm = 1000 dicht liegende
	// Punkte (wie im echten Netz nach dem Resampling). Diagonal, damit die
	// Huellbox in BEIDEN Achsen Ausdehnung hat (eine flache Strasse haette
	// Nullhoehe und die Projektion waere ungueltig - kein Netz-Fall).
	FRoadNetwork Net;
	FRoadSegment Seg;
	Seg.HighwayType = EOSMHighwayType::Residential;
	for (int32 I = 0; I <= 1000; ++I)
	{
		Seg.Centerline.Add(FVector(I * 100.0, I * 30.0, 0.0));
	}
	Net.Segments.Add(Seg);

	FVector2D WMin, WMax;
	FWiesbadenMinimap::ComputeNetworkBoundsXY(Net, WMin, WMax);
	// Massstab wie bei der Ganzstadt-Ansicht: 1 km auf ~400 px.
	const FWorldMapProjection Proj = FWiesbadenMinimap::MakeWorldMapProjection(
		WMin, WMax, FVector2D(200.0, 300.0), FVector2D(800.0, 600.0), 1.0f);

	TArray<FMinimapLine> Lines;
	FWiesbadenMinimap::BuildWorldMapLines(Net, Proj, /*MaxLines=*/16000, /*MinSegmentPx=*/2.0f, Lines);

	// Kern der Regression: NICHT null trotz dicht liegender Teilsegmente.
	TestTrue(TEXT("Dichte Punkte ergeben Linien (nicht null)"), Lines.Num() > 0);
	// Und nicht je Teilsegment eine Linie (Dezimierung greift): deutlich unter
	// den 1000 Eingabe-Teilsegmenten.
	TestTrue(TEXT("Dezimiert (deutlich unter 1000 Teilsegmenten)"), Lines.Num() < 600);

	return true;
}


// Weltkarte: die datenreine Projektion der gedrehten Gebaeude-Grundriss-Boxen
// in Bildschirm-Vierecke (bebautes-Gebiet-Schattierung).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapBuildingsTest,
	"WiesbadenReal.World.WorldMapBuildings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWorldMapBuildingsTest::RunTest(const FString& Parameters)
{
	// Projektion: 1 px/cm, Weltmitte (0,0) auf Bildschirm (400,300).
	FWorldMapProjection Proj;
	Proj.WorldMin = FVector2D(-1000.0, -1000.0);
	Proj.WorldMax = FVector2D(1000.0, 1000.0);
	Proj.ScreenCentre = FVector2D(400.0, 300.0);
	Proj.ScalePxPerCm = 1.0f;

	// Ein Gebaeude bei (0,0), Halbmasse (100,50) cm, Yaw 0.
	FGeneratedBuilding B;
	B.FootprintCenterCm = FVector2D(0.0, 0.0);
	B.FootprintExtentCm = FVector2D(100.0, 50.0);
	B.FootprintYawDegrees = 0.0f;
	B.FootprintAreaSqm = 2.0;
	TArray<FGeneratedBuilding> Buildings; Buildings.Add(B);

	TArray<FWorldMapQuad> Quads;
	FWiesbadenMinimap::BuildWorldMapBuildings(Buildings, Proj, /*MaxQuads=*/100, /*MinAreaPx=*/0.0f, Quads);
	if (TestEqual(TEXT("Ein Viereck"), Quads.Num(), 1))
	{
		// Yaw 0: Box-X=+X(Ost)->Bildschirm +X, Box-Y=+Y(Nord)->Bildschirm -Y (Norden oben).
		TestTrue(TEXT("Ecke (+ex,+ey) rechts-oben (500,250)"), Quads[0].A.Equals(FVector2D(500.0, 250.0), 0.01));
		TestTrue(TEXT("Ecke (-ex,-ey) links-unten (300,350)"), Quads[0].C.Equals(FVector2D(300.0, 350.0), 0.01));
	}

	// Entartetes/winziges Gebaeude (Halbmasse ~0) -> kein Viereck.
	FGeneratedBuilding Tiny;
	Tiny.FootprintExtentCm = FVector2D(0.5, 0.5);
	Tiny.FootprintAreaSqm = 0.1;
	TArray<FGeneratedBuilding> TinyList; TinyList.Add(Tiny);
	TArray<FWorldMapQuad> TinyQuads;
	FWiesbadenMinimap::BuildWorldMapBuildings(TinyList, Proj, /*MaxQuads=*/100, /*MinAreaPx=*/0.0f, TinyQuads);
	TestEqual(TEXT("Winziges Gebaeude uebersprungen"), TinyQuads.Num(), 0);

	return true;
}
