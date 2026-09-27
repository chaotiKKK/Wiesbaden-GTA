// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/OSMTypes.h"
#include "GIS/RoadFurnitureGenerator.h"
#include "GIS/WiesbadenPlacementAudit.h"
#include "GIS/WiesbadenRegionAssets.h"

/**
 * Prueft den Platzierungs-Audit selbst: er muss genau die Verstoesse zaehlen,
 * die die sechs Regeln der Spec verbieten - und bei sauberer Platzierung null
 * melden. Nur wer weiss, dass der Zaehler stimmt, darf die "Vorher"-Zahlen
 * einer Bake fuer Beweise halten.
 *
 * Namen sind bewusst eindeutig (Audit-Segment, ...): die Testdateien dieses
 * Moduls werden teils zusammengebaut, zwei gleiche Helfer im anonymen
 * Namensraum waehren dann als doppelte Definition.
 */
namespace
{
	FRoadSegment AuditSegment(int32 Id, int64 StartNode, int64 EndNode,
		const TArray<FVector>& Line, double SpeedKmh = 50.0)
	{
		FRoadSegment Segment;
		Segment.SegmentId = Id;
		Segment.StartNodeId = StartNode;
		Segment.EndNodeId = EndNode;
		Segment.Centerline = Line;
		Segment.TrimmedCenterline = Line;
		Segment.HighwayType = EOSMHighwayType::Residential;
		Segment.CarriagewayWidthCm = 650.0;
		Segment.MaxSpeedKmh = SpeedKmh;
		Segment.LengthCm = 0.0;
		for (int32 i = 1; i < Line.Num(); ++i)
		{
			Segment.LengthCm += FVector::Dist(Line[i], Line[i - 1]);
		}
		return Segment;
	}

	FSignInstance AuditSign(const FString& SignId, const FVector& Loc, double YawDeg,
		int64 SourceNode = 0, int32 SourceSegment = INDEX_NONE)
	{
		FSignInstance Sign;
		Sign.SignId = SignId;
		Sign.Sign.Id = SignId;
		Sign.Location = Loc;
		Sign.Rotation = FRotator(0.0, YawDeg, 0.0);
		Sign.SourceNodeId = SourceNode;
		Sign.SourceSegmentId = SourceSegment;
		return Sign;
	}

	FGeneratedBuilding AuditBuilding(const FVector2D& Center,
		const FVector2D& HalfExtent, float YawDeg)
	{
		FGeneratedBuilding Building;
		Building.FootprintCenterCm = Center;
		Building.FootprintExtentCm = HalfExtent;
		Building.FootprintYawDegrees = YawDeg;
		return Building;
	}

	UGeoCoordinateConverter* AuditConverter()
	{
		UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
		Converter->InitializeWithWiesbadenOrigin();
		return Converter;
	}

	const FPlacementRuleCount* Regel(const FPlacementAuditReport& Report, const TCHAR* Schluessel)
	{
		return Report.Finde(Schluessel);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementAuditFahrbahnTest,
	"WiesbadenReal.GIS.PlacementAudit.Fahrbahn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlacementAuditFahrbahnTest::RunTest(const FString& Parameters)
{
	// Eine gerade 100-m-Strasse entlang +X, halbe Fahrbahn 325 cm.
	FRoadNetwork Network;
	Network.Segments.Add(AuditSegment(0, 1, 2,
		{ FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0) }));

	FRoadFurnitureLayout Layout;
	// Ein Schild MITTEL auf der Fahrbahn, zwei daneben (rechts und links).
	Layout.Signs.Add(AuditSign(TEXT("274-50"), FVector(5000.0, 0.0, 0.0), 180.0, 11));
	Layout.Signs.Add(AuditSign(TEXT("274-50"), FVector(5000.0, 500.0, 0.0), 180.0, 12));
	Layout.Signs.Add(AuditSign(TEXT("206"), FVector(5000.0, -500.0, 0.0), 0.0, 13));

	// Leitpfosten und Laterne ebenfalls mittig - der Regel R4-Basisfaelle.
	{
		FDelineatorInstance D;
		D.Location = FVector(2000.0, 0.0, 0.0);
		Layout.Delineators.Add(D);
		FStreetLampInstance L;
		L.Location = FVector(3000.0, 0.0, 0.0);
		Layout.StreetLamps.Add(L);
	}

	FRegionAssetLayout Assets;
	FOSMDataSet OSMData;
	UGeoCoordinateConverter* Converter = AuditConverter();

	const FPlacementAuditReport Report = FWiesbadenPlacementAudit::Run(
		Network, Layout, Assets, TArray<FGeneratedBuilding>(), OSMData, *Converter);

	TestTrue(TEXT("Audit erfolgreich"), Report.bSuccess);
	TestEqual(TEXT("Bestand: 3 Schilder"), Report.SignCount, 3);

	const FPlacementRuleCount* R1 = Regel(Report, TEXT("R1-SchildFahrbahn"));
	TestNotNull(TEXT("Regel R1-SchildFahrbahn vorhanden"), R1);
	if (R1)
	{
		TestEqual(TEXT("Alle 3 Schilder geprueft"), R1->Geprueft, 3);
		TestEqual(TEXT("Nur das Schild auf der Fahrbahn zaehlt"), R1->Verstoesse, 1);
		TestTrue(TEXT("Belegstelle liegt auf der Fahrbahn"),
			R1->Beispiele.Num() == 1 && R1->Beispiele[0].Location.Y == 0.0);
	}

	const FPlacementRuleCount* R4 = Regel(Report, TEXT("R4-Fahrbahn"));
	TestNotNull(TEXT("Regel R4-Fahrbahn vorhanden"), R4);
	if (R4)
	{
		TestEqual(TEXT("Leitpfosten und Laterne geprueft"), R4->Geprueft, 2);
		TestEqual(TEXT("Beide stehen auf der Fahrbahn"), R4->Verstoesse, 2);
	}

	// Sauberer Lauf: dieselbe Strasse, alles am Rand - null Verstoesse.
	FRoadFurnitureLayout Sauber;
	Sauber.Signs.Add(AuditSign(TEXT("274-50"), FVector(5000.0, 500.0, 0.0), 180.0, 12));
	const FPlacementAuditReport SauberReport = FWiesbadenPlacementAudit::Run(
		Network, Sauber, FRegionAssetLayout(), TArray<FGeneratedBuilding>(),
		FOSMDataSet(), *Converter);

	int32 Gesamt = 0;
	for (const FPlacementRuleCount& Regel : SauberReport.Regeln)
	{
		Gesamt += Regel.Verstoesse;
	}
	TestEqual(TEXT("Saubere Platzierung meldet null Verstoesse"), Gesamt, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementAuditBlickrichtungTest,
	"WiesbadenReal.GIS.PlacementAudit.Blickrichtung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlacementAuditBlickrichtungTest::RunTest(const FString& Parameters)
{
	// Strasse entlang +X: rechts ist +Y, links -Y. Ein Schild rechts mit
	// Blick Richtung +X schaut NICHHT in den dortigen Verkehr (der faehrt
	// +X und muesste die Tafel von vorne sehen) - genau das zaehlt R1.
	FRoadNetwork Network;
	Network.Segments.Add(AuditSegment(0, 1, 2,
		{ FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0) }));

	FRoadFurnitureLayout Layout;
	// Rechts der Achse, Blick entlang der Achse => falsch (erwartet 180).
	Layout.Signs.Add(AuditSign(TEXT("274-30"), FVector(5000.0, 500.0, 0.0), 0.0, 21));
	// Links der Achse, Blick entlang der Achse => richtig (erwartet 0).
	Layout.Signs.Add(AuditSign(TEXT("274-30"), FVector(5000.0, -500.0, 0.0), 0.0, 22));

	FRegionAssetLayout Assets;
	FOSMDataSet OSMData;
	UGeoCoordinateConverter* Converter = AuditConverter();

	const FPlacementAuditReport Report = FWiesbadenPlacementAudit::Run(
		Network, Layout, Assets, TArray<FGeneratedBuilding>(), OSMData, *Converter);

	TestTrue(TEXT("Audit erfolgreich"), Report.bSuccess);

	const FPlacementRuleCount* R1 = Regel(Report, TEXT("R1-Blickrichtung"));
	TestNotNull(TEXT("Regel R1-Blickrichtung vorhanden"), R1);
	if (R1)
	{
		TestEqual(TEXT("Beide Knotenschilder geprueft"), R1->Geprueft, 2);
		TestEqual(TEXT("Nur das verdrehte zaehlt"), R1->Verstoesse, 1);
	}

	// Abgeleitetes Tempolimitschild auf der rechten Seite: die Platzierung
	// setzt es auf DirYaw, erwartet wird DirYaw + 180 - der ganze Bestand
	// muss also waechseln. Das misst R3.
	FRoadFurnitureLayout TempoLayout;
	TempoLayout.Signs.Add(AuditSign(TEXT("274-30"), FVector(0.0, 500.0, 0.0), 0.0, 0, /*SourceSegment=*/0));
	const FPlacementAuditReport TempoReport = FWiesbadenPlacementAudit::Run(
		Network, TempoLayout, FRegionAssetLayout(), TArray<FGeneratedBuilding>(),
		FOSMDataSet(), *Converter);

	const FPlacementRuleCount* R3 = Regel(TempoReport, TEXT("R3-TempolimitBlick"));
	TestNotNull(TEXT("Regel R3-TempolimitBlick vorhanden"), R3);
	if (R3)
	{
		TestEqual(TEXT("Tempolimitschild geprueft"), R3->Geprueft, 1);
		TestEqual(TEXT("Auf der rechten Seite schaut es falsch herum"), R3->Verstoesse, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementAuditDopplungTest,
	"WiesbadenReal.GIS.PlacementAudit.Dopplung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlacementAuditDopplungTest::RunTest(const FString& Parameters)
{
	// Kreuzung aus vier Armen: jeder Arm leitet sein eigenes Tempolimit ab,
	// am selben Knoten entstehen also vier gleiche Zeichen an derselben
	// Stelle - genau der Fall der Regel R3.
	FRoadNetwork Network;
	Network.Segments.Add(AuditSegment(0, 1, 9, { FVector(-5000.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0) }, 30.0));
	Network.Segments.Add(AuditSegment(1, 2, 9, { FVector(5000.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0) }, 30.0));
	Network.Segments.Add(AuditSegment(2, 3, 9, { FVector(0.0, 5000.0, 0.0), FVector(0.0, 0.0, 0.0) }, 30.0));
	Network.Segments.Add(AuditSegment(3, 4, 9, { FVector(0.0, -5000.0, 0.0), FVector(0.0, 0.0, 0.0) }, 30.0));

	FRoadFurnitureLayout Layout;
	// Vier gleiche Zeichen im Umkreis von 3 m + ein fremdes weiter weg.
	Layout.Signs.Add(AuditSign(TEXT("274-30"), FVector(100.0, 100.0, 0.0), 0.0, 1));
	Layout.Signs.Add(AuditSign(TEXT("274-30"), FVector(250.0, 0.0, 0.0), 0.0, 2));
	Layout.Signs.Add(AuditSign(TEXT("274-30"), FVector(0.0, 280.0, 0.0), 0.0, 3));
	Layout.Signs.Add(AuditSign(TEXT("274-30"), FVector(-150.0, 50.0, 0.0), 0.0, 4));
	Layout.Signs.Add(AuditSign(TEXT("206"), FVector(20000.0, 20000.0, 0.0), 0.0, 5));

	// Zusaetzlich25 Dopplungen fuer die Belegstellen-Grenze.
	FRoadFurnitureLayout Viele;
	for (int32 i = 0; i < 25; ++i)
	{
		Viele.Signs.Add(AuditSign(TEXT("274-30"),
			FVector(40000.0 + i * 50.0, 40000.0, 0.0), 0.0, 1000 + i));
	}

	FRegionAssetLayout Assets;
	FOSMDataSet OSMData;
	UGeoCoordinateConverter* Converter = AuditConverter();

	const FPlacementAuditReport Report = FWiesbadenPlacementAudit::Run(
		Network, Layout, Assets, TArray<FGeneratedBuilding>(), OSMData, *Converter);

	const FPlacementRuleCount* R3 = Regel(Report, TEXT("R3-Dopplung"));
	TestNotNull(TEXT("Regel R3-Dopplung vorhanden"), R3);
	if (R3)
	{
		TestEqual(TEXT("Fuenf Schilder geprueft"), R3->Geprueft, 5);
		// Vier gleiche Zeichen: Keep-first (Spec R3 "nur ein Schild je
		// Zeichen") laesst das erste stehen, also DREI zusaetzliche. "4"
		// waere unerreichbar - dieselbe Rechnung wie der 25er-Fall unten.
		TestEqual(TEXT("Zusaetzliche Zeichen: 3 + 0 (andere Id)"), R3->Verstoesse, 3);
	}

	const FPlacementAuditReport VieleReport = FWiesbadenPlacementAudit::Run(
		Network, Viele, FRegionAssetLayout(), TArray<FGeneratedBuilding>(),
		FOSMDataSet(), *Converter);
	const FPlacementRuleCount* VieleRegel = Regel(VieleReport, TEXT("R3-Dopplung"));
	if (VieleRegel)
	{
		// 25 gleiche Zeichen im 50-cm-Raster: das erste bleibt, die restlichen
		// 24 liegen jeweils innerhalb von 3 m an einem Vorgaenger.
		TestEqual(TEXT("24 Zusatzzeichen gezaehlt"), VieleRegel->Verstoesse, 24);
		TestEqual(TEXT("Beispiele gedeckelt"),
			VieleRegel->Beispiele.Num(), FWiesbadenPlacementAudit::MaxBeispieleJeRegel);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementAuditGebaeudeTest,
	"WiesbadenReal.GIS.PlacementAudit.Gebaeude",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlacementAuditGebaeudeTest::RunTest(const FString& Parameters)
{
	// Strasse weit weg, damit die Fahrbahnpruefung nicht mitzaehlt - es geht
	// ausschliesslich um den Grundriss.
	FRoadNetwork Network;
	Network.Segments.Add(AuditSegment(0, 1, 2,
		{ FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0) }));

	// Haus: Mitte (50000, 50000), halbe Kanten 1000 x 800, um 45 Grad gedreht.
	TArray<FGeneratedBuilding> Buildings;
	Buildings.Add(AuditBuilding(FVector2D(50000.0, 50000.0), FVector2D(1000.0, 800.0), 45.0f));

	FRoadFurnitureLayout Layout;
	FRegionAssetLayout Assets;
	auto Baum = [](const FVector& Loc)
	{
		FPlacedRegionAsset A;
		A.Category = ERegionAssetCategory::Tree;
		A.Location = Loc;
		return A;
	};

	// Mitten im Grundriss und - im Gegenversuch - daneben.
	Assets.Assets.Add(Baum(FVector(50000.0, 50000.0, 0.0)));
	Assets.Assets.Add(Baum(FVector(56000.0, 56000.0, 0.0)));

	FOSMDataSet OSMData;
	UGeoCoordinateConverter* Converter = AuditConverter();

	const FPlacementAuditReport Report = FWiesbadenPlacementAudit::Run(
		Network, Layout, Assets, Buildings, OSMData, *Converter);

	TestTrue(TEXT("Audit erfolgreich"), Report.bSuccess);
	TestEqual(TEXT("Ein Gebaeude in der Pruefung"), Report.BuildingCount, 1);

	const FPlacementRuleCount* R6 = Regel(Report, TEXT("R6-Gebaeude"));
	TestNotNull(TEXT("Regel R6-Gebaeude vorhanden"), R6);
	if (R6)
	{
		TestEqual(TEXT("Beide Baeume geprueft"), R6->Geprueft, 2);
		TestEqual(TEXT("Nur der drin stehende zaehlt"), R6->Verstoesse, 1);
		TestTrue(TEXT("Belegstelle ist der Baum im Haus"),
			R6->Beispiele.Num() == 1 && R6->Beispiele[0].Location.X == 50000.0);
	}

	// Fahrbahnkontrolle: der Baum auf der Strasse bleibt draussen.
	const FPlacementRuleCount* R6Strasse = Regel(Report, TEXT("R6-Fahrbahn"));
	TestNotNull(TEXT("Regel R6-Fahrbahn vorhanden"), R6Strasse);
	if (R6Strasse)
	{
		TestEqual(TEXT("Kein Baum auf der Fahrbahn"), R6Strasse->Verstoesse, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementAuditBahnTest,
	"WiesbadenReal.GIS.PlacementAudit.Bahn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlacementAuditBahnTest::RunTest(const FString& Parameters)
{
	FRoadNetwork Network;
	Network.Segments.Add(AuditSegment(0, 1, 2,
		{ FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0) }));

	// Ein Bahnweg in der Naehe der Strasse (Nerobergbreite: way 39223618
	// liegt bei 50.0947 / 8.2255 - hier nur ein Stueck davon).
	FOSMDataSet OSMData;
	FOSMNode A(101, 8.2254544, 50.0947441);
	FOSMNode B(102, 8.2255669, 50.0948482);
	OSMData.Nodes.Add(A.Id, A);
	OSMData.Nodes.Add(B.Id, B);

	FOSMWay Way;
	Way.Id = 39223618;
	Way.NodeIds = { A.Id, B.Id };
	Way.Tags.Add(TEXT("railway"), TEXT("funicular"));
	OSMData.Ways.Add(Way.Id, Way);

	UGeoCoordinateConverter* Converter = AuditConverter();
	const FVector Mitte = Converter->GeoToUnrealGround(A.Location);

	FRoadFurnitureLayout Layout;
	FRegionAssetLayout Assets;
	auto Baum = [](const FVector& Loc)
	{
		FPlacedRegionAsset Asset;
		Asset.Category = ERegionAssetCategory::Tree;
		Asset.Location = Loc;
		return Asset;
	};

	// Exakt auf der Achse (im Korridor) und 500 m daneben.
	Assets.Assets.Add(Baum(Mitte));
	Assets.Assets.Add(Baum(Mitte + FVector(50000.0, 0.0, 0.0)));

	const FPlacementAuditReport Report = FWiesbadenPlacementAudit::Run(
		Network, Layout, Assets, TArray<FGeneratedBuilding>(), OSMData, *Converter);

	TestTrue(TEXT("Audit erfolgreich"), Report.bSuccess);
	TestTrue(TEXT("Bahnkorridor ist aktiv"), Report.bRailCheckActive);
	TestTrue(TEXT("Bahnweg als Segmente erfasst"), Report.RailSegmentCount >= 1);

	const FPlacementRuleCount* R6 = Regel(Report, TEXT("R6-Bahn"));
	TestNotNull(TEXT("Regel R6-Bahn vorhanden"), R6);
	if (R6)
	{
		TestEqual(TEXT("Beide Baeume geprueft"), R6->Geprueft, 2);
		TestEqual(TEXT("Nur der auf der Achse stehende zaehlt"), R6->Verstoesse, 1);
	}

	// Ohne Bahnwege und ohne Geo-Bezug darf die Regel NICHT als "null Fehler"
	// erscheinen - der Audit meldet das ueber bRailCheckActive.
	FOSMDataSet Leer;
	const FPlacementAuditReport OhneBahn = FWiesbadenPlacementAudit::Run(
		Network, Layout, FRegionAssetLayout(), TArray<FGeneratedBuilding>(), Leer, *Converter);
	TestFalse(TEXT("Ohne Bahnwege ist der Korridor nicht gemessen"),
		OhneBahn.bRailCheckActive);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementAuditMoebelTest,
	"WiesbadenReal.GIS.PlacementAudit.Moebel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlacementAuditMoebelTest::RunTest(const FString& Parameters)
{
	// Strasse 1000 m entfernt: der kartierte Ort der Moebel ist frei, das
	// Netz ist also nicht der Grund fuer eine Verschiebung.
	FRoadNetwork Network;
	Network.Segments.Add(AuditSegment(0, 1, 2,
		{ FVector(-1000000.0, 0.0, 0.0), FVector(-990000.0, 0.0, 0.0) }));

	// Zwei Moebelknoten weit weg vom Netz.
	FOSMDataSet OSMData;
	FOSMNode Heimisch(201, 8.2400, 50.0800);
	FOSMNode Verschoben(202, 8.2410, 50.0810);
	OSMData.Nodes.Add(Heimisch.Id, Heimisch);
	OSMData.Nodes.Add(Verschoben.Id, Verschoben);

	UGeoCoordinateConverter* Converter = AuditConverter();
	const FVector Ort1 = Converter->GeoToUnrealGround(Heimisch.Location);
	const FVector Ort2 = Converter->GeoToUnrealGround(Verschoben.Location);

	FRoadFurnitureLayout Layout;
	auto Bank = [](const FVector& Loc, int64 NodeId)
	{
		FFurnitureInstance Item;
		Item.Kind = EStreetFurnitureKind::Bench;
		Item.Location = Loc;
		Item.NodeId = NodeId;
		return Item;
	};

	// Genau am kartierten Ort und 400 cm daneben (angedockt).
	Layout.Furniture.Add(Bank(Ort1, Heimisch.Id));
	Layout.Furniture.Add(Bank(Ort2 + FVector(400.0, 0.0, 0.0), Verschoben.Id));

	const FPlacementAuditReport Report = FWiesbadenPlacementAudit::Run(
		Network, Layout, FRegionAssetLayout(), TArray<FGeneratedBuilding>(), OSMData, *Converter);

	TestTrue(TEXT("Audit erfolgreich"), Report.bSuccess);
	TestEqual(TEXT("Beide Moebel in der Pruefung"), Report.FurnitureCount, 2);

	const FPlacementRuleCount* Versatz = Regel(Report, TEXT("R5-Moebelversatz"));
	TestNotNull(TEXT("Regel R5-Moebelversatz vorhanden"), Versatz);
	if (Versatz)
	{
		TestEqual(TEXT("Beide Moebel geprueft"), Versatz->Geprueft, 2);
		TestEqual(TEXT("Nur das angedockte zaehlt"), Versatz->Verstoesse, 1);
	}

	const FPlacementRuleCount* Weit = Regel(Report, TEXT("R5-Moebelweit"));
	TestNotNull(TEXT("Regel R5-Moebelweit vorhanden"), Weit);
	if (Weit)
	{
		TestEqual(TEXT("400 cm > 250 cm"), Weit->Verstoesse, 1);
	}

	return true;
}
