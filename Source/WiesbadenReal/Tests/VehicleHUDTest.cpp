// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "UI/WiesbadenVehicleHUD.h"
#include "GIS/BuildingGenerator.h"
#include "UI/WiesbadenWorldMapView.h"
#include "Vehicles/WiesbadenCarLightsComponent.h"
#include "Vehicles/WiesbadenCar.h"
#include "NPC/WiesbadenStoreMerchant.h"

// Der Testname darf KEIN Praefix eines anderen Testnamens sein.
//
// Der Kommandozeilen-Runner sammelt nur BLATTKNOTEN des Testbaums
// (FAutomationReport::GetEnabledTestNames: ChildReports.Num() == 0). Ein Name,
// unter dem weitere Tests haengen, wird zum Zwischenknoten und laeuft still
// nie. Genau das war hier der Fall: "WiesbadenReal.Vehicles.HUD" war
// Elternknoten von ...HUD.ControlLegend und ...HUD.MapDistance, die
// Instrumentenwerte unten sind deshalb nie geprueft worden.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleHUDTest,
	"WiesbadenReal.Vehicles.HUD.Instruments",
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
	TestTrue(TEXT("Kamera genannt"), VehicleText.Contains(TEXT("Kamera")));

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
	// Die Kurbel im Wagen (AWiesbadenNerobergbahn::ToggleWaterValve): im
	// Wasserballastbetrieb gibt es keine zweite Bedienung - steht sie nicht in
	// der Legende, findet sie niemand.
	TestTrue(TEXT("Kurbel genannt"), FootText.Contains(TEXT("Kurbel")));

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

	// -- Konkreter Haendler-Cue ----------------------------------------------
	//
	// DescribeNearestMerchantInReach ist statisch; der reine Selektionsregel-Test
	// hier macht die Cue-Konkretheit testbar, ohne eine ganze Spielwelt zu
	// simulieren. Gemessen wird AB DEM SPIELERSTANDORT (Parameter) zur Position
	// des Haendlers - genau diese Messung fehlte zuvor.
	{
		using HM = AWiesbadenStoreMerchant;
		constexpr double MerchantReach = 400.0;

		// Kein Haendler im Suchlauf: kein Cue.
		{
			FString Cue;
			TArray<AActor*> Merchants;
			TestTrue(TEXT("kein Haendler -> kein Cue"),
				HM::DescribeNearestMerchantInReach(Merchants, FVector::ZeroVector, Cue).IsEmpty());
			TestTrue(TEXT("kein Cue -> OutCue leer"), Cue.IsEmpty());
		}

		AWiesbadenStoreMerchant* Merchant = NewObject<AWiesbadenStoreMerchant>();
		if (TestNotNull(TEXT("Haendler angelegt"), Merchant))
		{
			// Ohne Wurzel liegt der Actor im Ursprung; der Abstand kommt damit
			// allein aus dem uebergebenen Spielerstandort.
			TestTrue(TEXT("Haendler ohne Wurzel liegt im Ursprung"),
				Merchant->GetActorLocation().IsZero());

			Merchant->InteractRangeCm = static_cast<float>(MerchantReach);
			Merchant->ApproachHint = TEXT("[F]  Haendler ansprechen");

			TArray<AActor*> Merchants;
			Merchants.Add(Merchant);
			FString Cue;

			// In Reichweite: der Cue ist konkret und uebernimmt den Autor-Text.
			TestTrue(TEXT("in-reach Haendler -> konkreter Cue"),
				!HM::DescribeNearestMerchantInReach(Merchants, FVector(100.0, 0.0, 0.0), Cue).IsEmpty());
			TestEqual(TEXT("Cue uebernimmt den Autor-Text"), Cue,
				FString(TEXT("[F]  Haendler ansprechen")));

			// REGRESSION: der Spieler steht weit weg (30 m - im Wiesbadener
			// Massstab nahe), der Haendler aber im Ursprung. Frueher wurde ab dem
			// Ursprung gemessen, damit galt jeder Haendler als erreichbar; hier
			// muss der Cue leer bleiben.
			Cue.Reset();
			TestTrue(TEXT("weit entfernter Spieler -> kein Cue"),
				HM::DescribeNearestMerchantInReach(Merchants, FVector(3000.0, 0.0, 0.0), Cue).IsEmpty());
			TestTrue(TEXT("weit weg -> OutCue leer"), Cue.IsEmpty());

			// Die EIGENE Reichweite des Haendlers zaehlt, nicht die des Hinweises:
			// ein Cue auf etwas, das F nicht erreicht, waere schlimmer als keiner.
			Merchant->InteractRangeCm = 50.0f;
			TestTrue(TEXT("ausserhalb der eigenen Reichweite -> kein Cue"),
				HM::DescribeNearestMerchantInReach(Merchants, FVector(100.0, 0.0, 0.0), Cue).IsEmpty());

			// Leerer Autor-Text faellt auf den kurzen Standardhinweis zurueck.
			Merchant->InteractRangeCm = static_cast<float>(MerchantReach);
			Merchant->ApproachHint.Reset();
			TestTrue(TEXT("leerer Autor-Text -> konkret bleibender Standardhinweis"),
				HM::DescribeNearestMerchantInReach(Merchants, FVector(100.0, 0.0, 0.0), Cue)
					.Contains(TEXT("Händler")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleHUDWaterLevelTest,
	"WiesbadenReal.Vehicles.HUD.FunicularWaterLevel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Wasserstandsanzeige der Mitfahrtafel.
 *
 * Der Wasserballast ist die einzige Antriebskraft des Wagens; die Kurbel ist
 * die einzige Bedienung und diese Anzeige die einzige Rueckmeldung darauf.
 * Eine Anzeige, die bei 1,4 den Wert "140 %" oder bei offenem Schieber "zu"
 * schreibt, laesst den Spieler am falschen Hebel drehen.
 */
bool FVehicleHUDWaterLevelTest::RunTest(const FString& Parameters)
{
	using FHud = AWiesbadenVehicleHUD;

	TestEqual(TEXT("voll und zu"), FHud::FormatWaterLevel(1.0f, false),
		FString(TEXT("Wasserballast 100 % - Schieber zu")));
	TestEqual(TEXT("leer und offen"), FHud::FormatWaterLevel(0.0f, true),
		FString(TEXT("Wasserballast 0 % - Schieber offen")));

	// Der Fuellstand wird auf 5 % gerundet - die Anzeige zappelt sonst mit
	// jedem Bild (Abfluss 2 %/s).
	TestTrue(TEXT("62 % wird auf 60 % gerundet"),
		FHud::FormatWaterLevel(0.62f, false).Contains(TEXT("60 %")));
	TestTrue(TEXT("68 % wird auf 70 % gerundet"),
		FHud::FormatWaterLevel(0.68f, false).Contains(TEXT("70 %")));

	// Ausserhalb 0..1 wird geklemmt: ein Rechenfehler im Ballast darf keine
	// 140-%-Anzeige erzeugen, die wie eine Absicht aussieht.
	TestTrue(TEXT("uebervoll geklemmt"),
		FHud::FormatWaterLevel(1.4f, false).Contains(TEXT("100 %")));
	TestTrue(TEXT("negativ geklemmt"),
		FHud::FormatWaterLevel(-0.3f, false).Contains(TEXT("0 %")));

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


// Weltkarte: Zoom + Pan der Projektion. Der Massstab skaliert mit dem Zoom, das
// Blickzentrum wird auf die Netzgrenzen geklemmt, und Project/Unproject sind
// zueinander invers. Ohne Canvas/Welt pruefbar.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapZoomPanTest,
	"WiesbadenReal.World.WorldMapZoomPan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWorldMapZoomPanTest::RunTest(const FString& Parameters)
{
	// Fit: Netz 0..1000 x 0..500 cm, Bild 800x600, Mitte (400,300), voller Rand.
	const FWorldMapProjection Fit = FWiesbadenMinimap::MakeWorldMapProjection(
		FVector2D(0.0, 0.0), FVector2D(1000.0, 500.0),
		FVector2D(400.0, 300.0), FVector2D(800.0, 600.0), /*MarginFrac=*/1.0f);
	TestTrue(TEXT("Fit-Zentrum ist die Netzmitte"),
		Fit.ViewCentreWorld.Equals(FVector2D(500.0, 250.0), 0.01));

	// Zoom 2 um die Netzmitte: Massstab verdoppelt (0.8 -> 1.6), Zentrum bleibt.
	const FWorldMapProjection Z2 = FWiesbadenMinimap::MakeZoomedProjection(Fit, 2.0f, FVector2D(500.0, 250.0));
	TestTrue(TEXT("Zoom 2: Massstab 1.6 px/cm"), FMath::IsNearlyEqual(Z2.ScalePxPerCm, 1.6f, 0.001f));
	TestTrue(TEXT("Zoom 2: Zentrum bleibt Netzmitte"), Z2.ViewCentreWorld.Equals(FVector2D(500.0, 250.0), 0.01));

	// Zoom 1: das Sichtfenster ist groesser als das Netz -> auf die Netzmitte
	// zurueckzentriert, auch wenn ein Eck-Zentrum gewuenscht war.
	const FWorldMapProjection Z1 = FWiesbadenMinimap::MakeZoomedProjection(Fit, 1.0f, FVector2D(900.0, 480.0));
	TestTrue(TEXT("Zoom 1: auf Netzmitte zentriert"), Z1.ViewCentreWorld.Equals(FVector2D(500.0, 250.0), 0.01));

	// Zoom 4, Eck-Zentrum: geklemmt auf [min+halbeSicht .. max-halbeSicht].
	// HalbSichtX = 400/(0.8*4)=125 -> X in [125,875]; HalbSichtY = 300/3.2=93.75 -> Y in [93.75,406.25].
	const FWorldMapProjection Z4 = FWiesbadenMinimap::MakeZoomedProjection(Fit, 4.0f, FVector2D(900.0, 400.0));
	TestTrue(TEXT("Zoom 4: X auf 875 geklemmt"), FMath::IsNearlyEqual(Z4.ViewCentreWorld.X, 875.0, 0.01));
	TestTrue(TEXT("Zoom 4: Y (400) unveraendert"), FMath::IsNearlyEqual(Z4.ViewCentreWorld.Y, 400.0, 0.01));

	// Zoom ueber das Maximum wird geklemmt (WorldMapMaxZoom).
	const FWorldMapProjection Zmax = FWiesbadenMinimap::MakeZoomedProjection(Fit, 999.0f, FVector2D(500.0, 250.0));
	TestTrue(TEXT("Zoom geklemmt auf Max"),
		FMath::IsNearlyEqual(Zmax.ScalePxPerCm, Fit.ScalePxPerCm * FWiesbadenMinimap::WorldMapMaxZoom, 0.001f));

	// Project/Unproject sind invers (bei gezoomter, verschobener Sicht).
	const FVector2D Screen(123.0, 456.0);
	const FVector2D World = Z4.Unproject(Screen);
	const FVector2D Back = Z4.Project(FVector(World.X, World.Y, 0.0));
	TestTrue(TEXT("Project(Unproject(s)) == s"), Back.Equals(Screen, 0.01));

	return true;
}

// Weltkarte (Feature 5, Kern): der SICHTBARE Welt-Ausschnitt der aktuellen
// Zoom/Pan-Sicht. Daran haengt spaeter die Live-Vektor-Zeichnung: nur Segmente
// in diesem Fenster werden je Bild gerastert, wenn die Basistextur vergroessert
// wuerde. Ohne Canvas/Welt pruefbar.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisibleWorldBoundsTest,
	"WiesbadenReal.World.VisibleWorldBounds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVisibleWorldBoundsTest::RunTest(const FString& Parameters)
{
	// Fit wie im Zoom/Pan-Test: Netz 0..1000 x 0..500 cm, Bild 800x600, Mitte
	// (400,300), voller Rand -> 0.8 px/cm.
	const FWorldMapProjection Fit = FWiesbadenMinimap::MakeWorldMapProjection(
		FVector2D(0.0, 0.0), FVector2D(1000.0, 500.0),
		FVector2D(400.0, 300.0), FVector2D(800.0, 600.0), /*MarginFrac=*/1.0f);
	const FVector2D ScreenSize(800.0, 600.0);

	// Zoom 2 um die Netzmitte: Massstab 1.6 px/cm -> sichtbares Fenster
	// 800/1.6 = 500 cm breit und 600/1.6 = 375 cm hoch um (500, 250).
	{
		const FWorldMapProjection Z2 = FWiesbadenMinimap::MakeZoomedProjection(
			Fit, 2.0f, FVector2D(500.0, 250.0));
		FVector2D Min, Max;
		FWiesbadenMinimap::ComputeVisibleWorldBounds(Z2, ScreenSize, Min, Max);

		TestTrue(TEXT("Zoom2: sichtbare Ecke unten-links (250, 62.5)"),
			Min.Equals(FVector2D(250.0, 62.5), 0.05));
		TestTrue(TEXT("Zoom2: sichtbare Ecke oben-rechts (750, 437.5)"),
			Max.Equals(FVector2D(750.0, 437.5), 0.05));
		TestTrue(TEXT("Zoom2: Fensterbreite = 800px / Massstab"),
			FMath::IsNearlyEqual(Max.X - Min.X, 500.0, 0.05));
		TestTrue(TEXT("Zoom2: Fensterhoehe = 600px / Massstab"),
			FMath::IsNearlyEqual(Max.Y - Min.Y, 375.0, 0.05));
	}

	// Zoom 4, an die Ecke gepannt/geklemmt (Zentrum X 875, Y 400, Massstab
	// 3.2): das Fenster liegt dann NICHT mehr mittig - es zeigt die Kante.
	{
		const FWorldMapProjection Z4 = FWiesbadenMinimap::MakeZoomedProjection(
			Fit, 4.0f, FVector2D(900.0, 400.0));
		FVector2D Min, Max;
		FWiesbadenMinimap::ComputeVisibleWorldBounds(Z4, ScreenSize, Min, Max);

		TestTrue(TEXT("Zoom4: sichtbare Ecke unten-links (750, 306.25)"),
			Min.Equals(FVector2D(750.0, 306.25), 0.05));
		TestTrue(TEXT("Zoom4: sichtbare Ecke oben-rechts (1000, 493.75)"),
			Max.Equals(FVector2D(1000.0, 493.75), 0.05));
	}

	// Zoom 1 (ganzes Netz): die Sicht ist breiter als das Netz -> senkrecht
	// bleibt Platz (Bild 4:3 gegen Netz 2:1), das Fenster ragt ueber die
	// Netzgrenzen hinaus.
	{
		const FWorldMapProjection Z1 = FWiesbadenMinimap::MakeZoomedProjection(
			Fit, 1.0f, FVector2D(900.0, 480.0));
		FVector2D Min, Max;
		FWiesbadenMinimap::ComputeVisibleWorldBounds(Z1, ScreenSize, Min, Max);

		// Rand exakt = nur im Idealfall; 0.8 px/cm ist als float nicht exakt, also
		// mit 1 cm Toleranz (nicht signifikant gegen die 1000-cm-Netzbreite).
		TestTrue(TEXT("Zoom1: Fenster umfasst das Netz (X ~0..1000)"),
			FMath::Abs(Min.X) < 1.0 && FMath::Abs(Max.X - 1000.0) < 1.0);
		TestTrue(TEXT("Zoom1: Hoehe 750 cm (600px / 0.8)"),
			FMath::IsNearlyEqual(Max.Y - Min.Y, 750.0, 0.05));
	}

	return true;
}

// Weltkarte (Feature 5, Kern): die UMSCHALT-SCHWELLE von der eingebackenen
// Basistextur auf LIVE gezeichnete Strassenvektoren. Sobald die Sicht dichter
// als das Magnification-Vielfache der Textur-Aufloesung ist, kann die Textur
// beim Hineinzoomen kein Detail mehr liefern - nur noch vergroesserte Pixel.
// Ohne Canvas/Welt pruefbar.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapVectorSwitchTest,
	"WiesbadenReal.World.WorldMapVectorSwitch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWorldMapVectorSwitchTest::RunTest(const FString& Parameters)
{
	// Netz 0..2000 x 0..1000 (2:1). Basis-Textur 2000x1000 -> 1.0 Texel/cm;
	// Bildschirm 800x400 -> 0.4 px/cm. Die Sicht erreicht die 1.4x-Texeldichte
	// also bei Zoom 3.5 (0.4*3.5 = 1.4 = 1.0 * WorldMapVectorSwitchMagnification).
	//
	// Gemessen im Spiel (1600x900, Texel-Mag = Zoom/2.5): ab Zoom 3 wird die
	// Textur weich, ab Zoom 3.5 deutlich (Kanten-Peak -45 %). Die Schwelle 1.4
	// schaltet dort ein - nicht erst bei Mag 2.0 (Zoom 5, Ende der Weichzone).
	const FVector2D WMin(0.0, 0.0);
	const FVector2D WMax(2000.0, 1000.0);
	const FVector2D BaseSize(2000.0, 1000.0);
	const FWorldMapProjection BaseFit = FWiesbadenMinimap::MakeWorldMapProjection(
		WMin, WMax, BaseSize * 0.5, BaseSize, /*MarginFrac=*/1.0f);
	const FVector2D ScreenSize(800.0, 400.0);
	const FWorldMapProjection ScreenFit = FWiesbadenMinimap::MakeWorldMapProjection(
		WMin, WMax, ScreenSize * 0.5, ScreenSize, /*MarginFrac=*/1.0f);
	const FVector2D NetCentre = (WMin + WMax) * 0.5;

	// Stadt-Ansicht (Zoom 1..3): Massstab 0.4..1.2 < 1.4 Texeldichte - die
	// ueberabgetastete Textur reicht, keine Vektoren.
	for (float Zoom : { 1.0f, 2.0f, 3.0f })
	{
		const FWorldMapProjection V = FWiesbadenMinimap::MakeZoomedProjection(
			ScreenFit, Zoom, NetCentre);
		TestFalse(FString::Printf(TEXT("Zoom %.0f: Textur reicht, keine Vektoren"), Zoom),
			FWiesbadenMinimap::ShouldDrawVectorStreets(V, BaseFit));
	}

	// Hineingezoomt (ab Zoom 3.5 = Massstab 1.4): jedes Texel deckt mehr als
	// 1.4 Bildschirmpixel, die Textur wird weich -> Strassen live als Vektoren.
	for (float Zoom : { 3.75f, 4.0f, 6.0f, 8.0f })
	{
		const FWorldMapProjection V = FWiesbadenMinimap::MakeZoomedProjection(
			ScreenFit, Zoom, NetCentre);
		TestTrue(FString::Printf(TEXT("Zoom %.2f: Vektoren statt vergroesserter Textur"), Zoom),
			FWiesbadenMinimap::ShouldDrawVectorStreets(V, BaseFit));
	}

	// Schwelle mit Toleranz statt exaktem 3.5-Vergleich (float): knapp darunter
	// Textur, knapp darueber Vektoren.
	{
		const FWorldMapProjection VBelow = FWiesbadenMinimap::MakeZoomedProjection(
			ScreenFit, 3.4f, NetCentre);
		TestFalse(TEXT("Zoom 3.4 (Mag ~1.36): noch Textur"),
			FWiesbadenMinimap::ShouldDrawVectorStreets(VBelow, BaseFit));
		const FWorldMapProjection VAbove = FWiesbadenMinimap::MakeZoomedProjection(
			ScreenFit, 3.6f, NetCentre);
		TestTrue(TEXT("Zoom 3.6 (Mag ~1.44): Vektoren"),
			FWiesbadenMinimap::ShouldDrawVectorStreets(VAbove, BaseFit));
	}

	// Zoom ist monotone Funktion des Massstabs: dichter als die Textur heisst
	// hineingezoomt. Bei Zoom 3 liegt die Sicht UNTER der Schwelle 1.4.
	TestTrue(TEXT("Schwelle liegt ueber Zoom 3"),
		ScreenFit.ScalePxPerCm * 3.0f < BaseFit.ScalePxPerCm * FWiesbadenMinimap::WorldMapVectorSwitchMagnification);
	TestTrue(TEXT("Zoom 6 ueberschreitet die Schwelle"),
		ScreenFit.ScalePxPerCm * 6.0f >= BaseFit.ScalePxPerCm * FWiesbadenMinimap::WorldMapVectorSwitchMagnification);

	return true;
}

// Wegpunkt auf der Minikarte: dieselbe Dreh-/Massstab-Abbildung wie die Strassen,
// Randklemmung ausserhalb der Reichweite, planare Distanz. Ohne Canvas pruefbar.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapWaypointTest,
	"WiesbadenReal.World.MinimapWaypoint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMinimapWaypointTest::RunTest(const FString& Parameters)
{
	FMinimapSettings S;
	S.RangeCm = 25000.0;          // 250 m Umkreis
	S.DiameterPx = 260.0f;        // Radius 130 px -> 0.0052 px/cm
	S.bRotateWithPlayer = true;
	const FVector2D Centre(500.0, 400.0);
	const FVector Player(0.0, 0.0, 0.0);

	// Yaw 0: Fahrzeug-Vorwaerts (+X) zeigt nach oben. Wegpunkt 50 m voraus (+X)
	// -> ueber der Mitte (kleineres Bild-Y), in Reichweite.
	{
		const FMinimapWaypoint W = FWiesbadenMinimap::ProjectWaypointToMinimap(
			Player, 0.0, FVector(5000.0, 0.0, 0.0), Centre, S);
		TestFalse(TEXT("In Reichweite -> nicht am Rand"), W.bOffMap);
		TestTrue(TEXT("Distanz 50 m"), FMath::IsNearlyEqual(W.DistanceCm, 5000.0, 1.0));
		TestTrue(TEXT("Voraus = ueber der Mitte"), W.ScreenPos.Y < Centre.Y);
		TestTrue(TEXT("Voraus: gleiche Bild-X wie Mitte"), FMath::IsNearlyEqual(W.ScreenPos.X, Centre.X, 0.5));
	}

	// Weit ausserhalb (500 m > 250 m): am Rand geklemmt, Richtung erhalten.
	{
		const FMinimapWaypoint W = FWiesbadenMinimap::ProjectWaypointToMinimap(
			Player, 0.0, FVector(50000.0, 0.0, 0.0), Centre, S);
		TestTrue(TEXT("Ausserhalb -> am Rand"), W.bOffMap);
		const float RadiusFromCentre = FVector2D::Distance(W.ScreenPos, Centre);
		TestTrue(TEXT("Marker sitzt auf dem Kartenradius (130 px)"),
			FMath::IsNearlyEqual(RadiusFromCentre, 130.0f, 0.5f));
		TestTrue(TEXT("Distanz 500 m"), FMath::IsNearlyEqual(W.DistanceCm, 50000.0, 1.0));
	}

	// Drehung: bei 90 Grad Yaw dreht sich derselbe Wegpunkt mit (nicht mehr
	// exakt ueber der Mitte).
	{
		const FMinimapWaypoint W = FWiesbadenMinimap::ProjectWaypointToMinimap(
			Player, 90.0, FVector(5000.0, 0.0, 0.0), Centre, S);
		TestFalse(TEXT("Gedreht: kein exaktes Voraus mehr"),
			FMath::IsNearlyEqual(W.ScreenPos.X, Centre.X, 0.5) && W.ScreenPos.Y < Centre.Y);
	}

	return true;
}

// UV-Fenster der Basiskarte: das Herzstueck des "einmal backen, dann per Textur-
// Transform zoomen/pannen"-Umbaus. Datenrein pruefbar (kein Canvas/RT noetig).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapUVTest,
	"WiesbadenReal.World.WorldMapUV",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWorldMapUVTest::RunTest(const FString& Parameters)
{
	// Gleiches Seitenverhaeltnis (2:1) fuer Netz, Bildschirm und Basis -> verzerrungsfrei.
	const FVector2D WMin(0.0, 0.0), WMax(2000.0, 1000.0);
	const FVector2D Screen(1600.0, 800.0);
	const FVector2D Base(4000.0, 2000.0);
	const float M = FWiesbadenMinimap::WorldMapMarginFrac;
	const FWorldMapProjection BaseFit =
		FWiesbadenMinimap::MakeWorldMapProjection(WMin, WMax, Base * 0.5, Base, M);
	const FWorldMapProjection ScreenFit =
		FWiesbadenMinimap::MakeWorldMapProjection(WMin, WMax, Screen * 0.5, Screen, M);
	const FVector2D NetCentre = (WMin + WMax) * 0.5;

	FVector2D UVMin, UVMax;

	// Zoom 1, Netzmitte: das Sichtfenster zeigt die ganze Basis -> UV ~ [0,1].
	const FWorldMapProjection V1 = FWiesbadenMinimap::MakeZoomedProjection(ScreenFit, 1.0f, NetCentre);
	FWiesbadenMinimap::ComputeWorldMapUV(BaseFit, Base, V1, Screen, UVMin, UVMax);
	TestTrue(TEXT("Zoom1 UVmin ~0"), UVMin.X < 0.02 && UVMin.Y < 0.02);
	TestTrue(TEXT("Zoom1 UVmax ~1"), UVMax.X > 0.98 && UVMax.Y > 0.98);

	// Zoom 2, Netzmitte: halb so grosses, zentriertes Fenster -> UV-Breite ~0.5, Mitte ~0.5.
	const FWorldMapProjection V2 = FWiesbadenMinimap::MakeZoomedProjection(ScreenFit, 2.0f, NetCentre);
	FWiesbadenMinimap::ComputeWorldMapUV(BaseFit, Base, V2, Screen, UVMin, UVMax);
	TestTrue(TEXT("Zoom2 UV-Breite ~0.5"), FMath::IsNearlyEqual(UVMax.X - UVMin.X, 0.5, 0.03));
	TestTrue(TEXT("Zoom2 UV-Hoehe ~0.5"), FMath::IsNearlyEqual(UVMax.Y - UVMin.Y, 0.5, 0.03));
	TestTrue(TEXT("Zoom2 zentriert X"), FMath::IsNearlyEqual((UVMin.X + UVMax.X) * 0.5, 0.5, 0.02));
	TestTrue(TEXT("Zoom2 zentriert Y"), FMath::IsNearlyEqual((UVMin.Y + UVMax.Y) * 0.5, 0.5, 0.02));

	// UVmin liegt oben-links, UVmax unten-rechts (monoton steigend).
	TestTrue(TEXT("UVmin < UVmax"), UVMin.X < UVMax.X && UVMin.Y < UVMax.Y);
	return true;
}

// Karten-Distanzformat: unter 1 km in Metern, darueber in Kilometern.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapDistanceFormatTest,
	"WiesbadenReal.Vehicles.HUD.MapDistance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMapDistanceFormatTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("340 m"), AWiesbadenVehicleHUD::FormatMapDistance(34000.0), FString(TEXT("340 m")));
	TestEqual(TEXT("999 m knapp unter 1 km"), AWiesbadenVehicleHUD::FormatMapDistance(99900.0), FString(TEXT("999 m")));
	TestEqual(TEXT("1.0 km ab 1000 m"), AWiesbadenVehicleHUD::FormatMapDistance(100000.0), FString(TEXT("1.0 km")));
	TestEqual(TEXT("2.5 km"), AWiesbadenVehicleHUD::FormatMapDistance(250000.0), FString(TEXT("2.5 km")));
	return true;
}

// Weltkarten-RenderTarget: der eine datenreine Teil der Render-Orchestrierung -
// "muss neu gerendert werden?" (Zeichnen ins RT selbst ist nicht unit-testbar).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapViewNeedsRerenderTest,
	"WiesbadenReal.World.WorldMapView.NeedsRerender",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWorldMapViewNeedsRerenderTest::RunTest(const FString& Parameters)
{
	FRoadNetwork NetA, NetB;
	const FVector2D Size(1600.0, 900.0);

	// Kein Ziel -> immer neu rendern.
	TestTrue(TEXT("Ohne Ziel: neu rendern"),
		UWiesbadenWorldMapView::NeedsRerender(false, &NetA, Size, &NetA, Size));

	// Ziel da, gleiches Netz + gleiche Groesse -> NICHT neu rendern.
	TestFalse(TEXT("Gleiche Sicht: kein Neu-Render"),
		UWiesbadenWorldMapView::NeedsRerender(true, &NetA, Size, &NetA, Size));

	// Netz gewechselt -> neu rendern.
	TestTrue(TEXT("Netzwechsel: neu rendern"),
		UWiesbadenWorldMapView::NeedsRerender(true, &NetA, Size, &NetB, Size));

	// Bildgroesse gewechselt -> neu rendern.
	TestTrue(TEXT("Groessenwechsel: neu rendern"),
		UWiesbadenWorldMapView::NeedsRerender(true, &NetA, Size, &NetA, FVector2D(1280.0, 720.0)));

	return true;
}

// Kaefer-Mesh-Auswahl: bevorzugt die radlose Karosserie + 4 Einzelraeder, weil
// das Herbie-Voll-Mesh ein Hinterrad vermissen laesst. Herbie nur als Notfall.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeetleAssemblyTest,
	"WiesbadenReal.Vehicles.BeetleAssembly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBeetleAssemblyTest::RunTest(const FString& Parameters)
{
	// Alles verfuegbar -> radlose Karosserie + 4 Einzelraeder (NICHT Herbie).
	{
		const FBeetleAssembly A = AWiesbadenCar::ChooseBeetleAssembly(true, true, true);
		TestTrue(TEXT("alles: SeparateWheelBody"), A.Body == EBeetleBodyMesh::SeparateWheelBody);
		TestTrue(TEXT("alles: 4 Einzelraeder sichtbar"), A.bSeparateWheels);
	}

	// Body+Rad ohne Herbie -> ebenso radlose Karosserie + 4 Raeder.
	{
		const FBeetleAssembly A = AWiesbadenCar::ChooseBeetleAssembly(true, true, false);
		TestTrue(TEXT("body+rad: SeparateWheelBody"), A.Body == EBeetleBodyMesh::SeparateWheelBody);
		TestTrue(TEXT("body+rad: 4 Raeder"), A.bSeparateWheels);
	}

	// Kein Rad-Mesh (aber Herbie) -> Herbie-Notfall (eigene Raeder, keine separaten).
	{
		const FBeetleAssembly A = AWiesbadenCar::ChooseBeetleAssembly(true, false, true);
		TestTrue(TEXT("kein Rad: HerbieFull"), A.Body == EBeetleBodyMesh::HerbieFull);
		TestFalse(TEXT("kein Rad: keine Einzelraeder"), A.bSeparateWheels);
	}

	// Keine Karosserie (aber Herbie) -> Herbie-Notfall.
	{
		const FBeetleAssembly A = AWiesbadenCar::ChooseBeetleAssembly(false, true, true);
		TestTrue(TEXT("kein Body: HerbieFull"), A.Body == EBeetleBodyMesh::HerbieFull);
	}

	// Nichts verfuegbar -> Ersatzquader.
	{
		const FBeetleAssembly A = AWiesbadenCar::ChooseBeetleAssembly(false, false, false);
		TestTrue(TEXT("nichts: Cube"), A.Body == EBeetleBodyMesh::Cube);
		TestFalse(TEXT("nichts: keine Einzelraeder"), A.bSeparateWheels);
	}

	return true;
}
