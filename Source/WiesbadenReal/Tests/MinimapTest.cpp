// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "UI/WiesbadenMinimap.h"

namespace
{
	/** Gerades Segment mit Namen und Typ. */
	FRoadSegment MakeNamedSegment(
		int32 Id, const FVector2D& A, const FVector2D& B,
		const FString& Name, EOSMHighwayType Type)
	{
		FRoadSegment Segment;
		Segment.SegmentId = Id;
		Segment.HighwayType = Type;
		Segment.StreetName = Name;
		Segment.CarriagewayWidthCm = 650.0;
		Segment.Centerline = { FVector(A.X, A.Y, 0.0), FVector(B.X, B.Y, 0.0) };
		Segment.LengthCm = FVector2D::Distance(A, B);
		return Segment;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapStreetNameTest,
	"WiesbadenReal.UI.Minimap.StreetName",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Der Strassenname stammt vom Abstand zur MITTELLINIE, nicht vom
 * naechstgelegenen Stuetzpunkt.
 *
 * Eine lange gerade Strasse hat weit auseinanderliegende Stuetzpunkte. Wer
 * den naechsten PUNKT sucht, bekommt mitten auf der Strasse den Namen einer
 * ganz anderen, weil deren Ecke zufaellig naeher liegt.
 */
bool FMinimapStreetNameTest::RunTest(const FString& Parameters)
{
	FRoadNetwork Network;

	// Lange Gerade in X-Richtung, Stuetzpunkte 100 m auseinander.
	Network.Segments.Add(MakeNamedSegment(0,
		FVector2D(0.0, 0.0), FVector2D(10000.0, 0.0),
		TEXT("Wilhelmstrasse"), EOSMHighwayType::Secondary));

	// Kurze Querstrasse, deren Endpunkt naeher an der Mitte der Geraden liegt
	// als deren eigene Stuetzpunkte.
	Network.Segments.Add(MakeNamedSegment(1,
		FVector2D(5000.0, 900.0), FVector2D(5000.0, 4000.0),
		TEXT("Nebenweg"), EOSMHighwayType::Residential));

	// Standort: mitten auf der Geraden.
	const FString Name = FWiesbadenMinimap::FindStreetName(
		Network, FVector(5000.0, 0.0, 0.0));

	TestEqual(TEXT("Mitten auf der Geraden gilt deren Name"), Name, FString(TEXT("Wilhelmstrasse")));

	// Direkt auf der Querstrasse gilt deren Name.
	const FString Side = FWiesbadenMinimap::FindStreetName(
		Network, FVector(5000.0, 2500.0, 0.0));
	TestEqual(TEXT("Auf der Querstrasse gilt deren Name"), Side, FString(TEXT("Nebenweg")));

	// Weit weg: kein Name statt eines falschen.
	const FString Far = FWiesbadenMinimap::FindStreetName(
		Network, FVector(5000.0, 90000.0, 0.0), 3000.0);
	TestTrue(TEXT("Ausser Reichweite bleibt der Name leer"), Far.IsEmpty());

	// Namenlose Segmente (Feldwege, Zufahrten) werden uebersprungen statt als
	// leerer Balken angezeigt.
	FRoadNetwork Unnamed;
	Unnamed.Segments.Add(MakeNamedSegment(0,
		FVector2D(0.0, 0.0), FVector2D(10000.0, 0.0),
		FString(), EOSMHighwayType::Residential));
	TestTrue(TEXT("Namenloses Segment liefert keinen Namen"),
		FWiesbadenMinimap::FindStreetName(Unnamed, FVector(5000.0, 0.0, 0.0)).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapLinesTest,
	"WiesbadenReal.UI.Minimap.Lines",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Karte zeigt die Umgebung, dreht sich mit und bleibt begrenzt.
 */
bool FMinimapLinesTest::RunTest(const FString& Parameters)
{
	FRoadNetwork Network;
	Network.Segments.Add(MakeNamedSegment(0,
		FVector2D(-20000.0, 0.0), FVector2D(20000.0, 0.0),
		TEXT("Ost-West"), EOSMHighwayType::Secondary));
	Network.Segments.Add(MakeNamedSegment(1,
		FVector2D(0.0, -20000.0), FVector2D(0.0, 20000.0),
		TEXT("Nord-Sued"), EOSMHighwayType::Residential));

	FMinimapSettings Settings;
	Settings.DiameterPx = 200.0f;
	Settings.RangeCm = 25000.0;
	Settings.bRotateWithPlayer = false;

	const FVector2D Centre(500.0, 400.0);

	TArray<FMinimapLine> Lines;
	FWiesbadenMinimap::BuildLines(
		Network, FVector::ZeroVector, 0.0, Centre, Settings, Lines);

	TestTrue(FString::Printf(TEXT("Beide Strassen erscheinen (%d Linien)"), Lines.Num()),
		Lines.Num() >= 2);

	// Hauptstrassen zuerst und dicker - sonst verschwindet die Achse, an der
	// man sich orientiert, zwischen den Wohnstrassen.
	TestTrue(TEXT("Hauptstrasse steht vorn"), Lines[0].bMajor);
	TestTrue(TEXT("Hauptstrasse ist dicker"), Lines[0].Thickness > 2.0f);

	// Die Mitte der Karte ist der Standort des Spielers.
	//
	// Geprueft wird, ob eine Linie DURCH die Mitte laeuft - nicht, ob dort ein
	// Endpunkt liegt. Eine durchgehende Strasse hat ihre Stuetzpunkte am Rand
	// der Karte; auf den Endpunkt zu pruefen war die falsche Frage.
	auto DistanceToLine = [](const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double LengthSq = AB.SizeSquared();
		if (LengthSq <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FVector2D::Distance(P, A);
		}
		const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LengthSq, 0.0, 1.0);
		return FVector2D::Distance(P, A + AB * T);
	};

	bool bPassesCentre = false;
	for (const FMinimapLine& Line : Lines)
	{
		if (DistanceToLine(Centre, Line.Start, Line.End) < 1.0)
		{
			bPassesCentre = true;
		}
	}
	TestTrue(TEXT("Eine Strasse laeuft durch die Kartenmitte"), bPassesCentre);

	// Obergrenze wird eingehalten - ohne sie koennte die Karte in dichter
	// Bebauung tausende Linien zeichnen und die Bildzeit verschlechtern.
	FRoadNetwork Dense;
	for (int32 i = 0; i < 400; ++i)
	{
		Dense.Segments.Add(MakeNamedSegment(i,
			FVector2D(-5000.0, i * 40.0 - 8000.0), FVector2D(5000.0, i * 40.0 - 8000.0),
			TEXT("Dicht"), EOSMHighwayType::Residential));
	}

	FMinimapSettings Capped = Settings;
	Capped.MaxLines = 50;

	TArray<FMinimapLine> DenseLines;
	FWiesbadenMinimap::BuildLines(
		Dense, FVector::ZeroVector, 0.0, Centre, Capped, DenseLines);

	TestTrue(FString::Printf(TEXT("Obergrenze eingehalten (%d von hoechstens 50)"), DenseLines.Num()),
		DenseLines.Num() <= 50);

	// Mitdrehende Karte: Bei 90 Grad Gierwinkel liegt die Ost-West-Strasse
	// senkrecht statt waagerecht.
	FMinimapSettings Rotating = Settings;
	Rotating.bRotateWithPlayer = true;

	TArray<FMinimapLine> Rotated;
	FWiesbadenMinimap::BuildLines(
		Network, FVector::ZeroVector, 90.0, Centre, Rotating, Rotated);

	TestTrue(TEXT("Mitdrehende Karte liefert Linien"), Rotated.Num() >= 2);

	bool bAnyVertical = false;
	for (const FMinimapLine& Line : Rotated)
	{
		if (FMath::Abs(Line.End.X - Line.Start.X) < FMath::Abs(Line.End.Y - Line.Start.Y))
		{
			bAnyVertical = true;
		}
	}
	TestTrue(TEXT("Nach 90 Grad Drehung liegt eine Strasse senkrecht"), bAnyVertical);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapStreetSearchTest,
	"WiesbadenReal.UI.Minimap.StreetSearch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Weltkarten-Suche findet eine Strasse per (Teil-)Name und liefert ihren
 * Mittelpunkt in Welt-XY, damit das Eingabefeld die Karte darauf zentrieren kann.
 */
bool FMinimapStreetSearchTest::RunTest(const FString& Parameters)
{
	FRoadNetwork Network;
	// Wilhelmstrasse aus ZWEI Segmenten: Mittel aller Mittellinienpunkte = (10000, 0).
	Network.Segments.Add(MakeNamedSegment(0,
		FVector2D(0.0, 0.0), FVector2D(10000.0, 0.0),
		TEXT("Wilhelmstrasse"), EOSMHighwayType::Secondary));
	Network.Segments.Add(MakeNamedSegment(1,
		FVector2D(10000.0, 0.0), FVector2D(20000.0, 0.0),
		TEXT("Wilhelmstrasse"), EOSMHighwayType::Secondary));
	// Eine andere Strasse weit im Norden.
	Network.Segments.Add(MakeNamedSegment(2,
		FVector2D(0.0, 50000.0), FVector2D(2000.0, 50000.0),
		TEXT("Platter Strasse"), EOSMHighwayType::Residential));

	FVector2D Centre;
	TestTrue(TEXT("Exakter Name wird gefunden"),
		FWiesbadenMinimap::FindStreetCenter(Network, TEXT("Wilhelmstrasse"), Centre));
	TestTrue(TEXT("Mittelpunkt liegt auf der Wilhelmstrasse (10000,0)"),
		FVector2D::Distance(Centre, FVector2D(10000.0, 0.0)) < 1.0);

	// Gross-/Kleinschreibung egal - der Nutzer tippt selten exakt.
	TestTrue(TEXT("Suche ist case-insensitive"),
		FWiesbadenMinimap::FindStreetCenter(Network, TEXT("wilhelmSTRASSE"), Centre));

	// Teiltreffer: "Platter" trifft die Platter Strasse (weit im Norden).
	FVector2D Partial;
	TestTrue(TEXT("Teiltreffer per enthaltenem Text"),
		FWiesbadenMinimap::FindStreetCenter(Network, TEXT("Platter"), Partial));
	TestTrue(TEXT("Teiltreffer trifft die richtige Strasse (Y~50000)"),
		FMath::Abs(Partial.Y - 50000.0) < 1.0);

	// Kein Treffer -> false.
	FVector2D None;
	TestFalse(TEXT("Unbekannter Name liefert false"),
		FWiesbadenMinimap::FindStreetCenter(Network, TEXT("Gibtsnichtstrasse"), None));

	return true;
}
