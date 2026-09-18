// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenCitySubsystem.h"

namespace
{
	FRoadSegment MakeNamedSegment(int32 Id, const FString& Name,
		const FVector& Start, const FVector& End)
	{
		FRoadSegment Segment;
		Segment.SegmentId = Id;
		Segment.StreetName = Name;
		Segment.HighwayType = EOSMHighwayType::Residential;
		Segment.Centerline = { Start, (Start + End) * 0.5, End };
		Segment.LengthCm = FVector::Dist(Start, End);
		return Segment;
	}

	/**
	 * Netz mit drei Strassen; "Rheinstraße" gibt es ZWEIMAL, in stark
	 * unterschiedlicher Laenge - damit laesst sich pruefen, dass der Hauptzug
	 * gewinnt und nicht der erste Treffer.
	 */
	FRoadNetwork MakeNamedNetwork()
	{
		FRoadNetwork Network;
		Network.Segments.Add(MakeNamedSegment(0, TEXT("Rheinstraße"),
			FVector(0.0, 0.0, 0.0), FVector(2000.0, 0.0, 0.0)));          // 20 m
		Network.Segments.Add(MakeNamedSegment(1, TEXT("Kaiser-Friedrich-Ring"),
			FVector(0.0, 5000.0, 0.0), FVector(0.0, 15000.0, 0.0)));      // 100 m
		Network.Segments.Add(MakeNamedSegment(2, TEXT("Rheinstraße"),
			FVector(100000.0, 0.0, 0.0), FVector(140000.0, 0.0, 0.0)));   // 400 m
		return Network;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbGotoTargetTest,
	"WiesbadenReal.World.Zielort",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbGotoTargetTest::RunTest(const FString& Parameters)
{
	using FTarget = UWiesbadenCitySubsystem::FWbGotoTarget;

	// -- 1. Koordinaten ----------------------------------------------------
	{
		const FTarget T = UWiesbadenCitySubsystem::ParseGotoTarget(TEXT("-121964,-119658"));
		TestTrue(TEXT("Zwei Zahlen sind Koordinaten"), T.bHasCoordinates);
		TestTrue(TEXT("Koordinaten werden uebernommen"),
			T.LocationCm.Equals(FVector2D(-121964.0, -119658.0), 0.5));
		TestTrue(TEXT("Koordinaten sind eine gueltige Angabe"), T.IsValid());
	}

	{
		// Leerzeichen um das Komma sind der Normalfall beim Abtippen.
		const FTarget T = UWiesbadenCitySubsystem::ParseGotoTarget(TEXT("  1000 , 2000  "));
		TestTrue(TEXT("Leerzeichen stoeren die Koordinaten nicht"), T.bHasCoordinates);
		TestTrue(TEXT("Beide Werte stimmen"),
			T.LocationCm.Equals(FVector2D(1000.0, 2000.0), 0.5));
	}

	// -- 2. Strassennamen --------------------------------------------------
	{
		const FTarget T = UWiesbadenCitySubsystem::ParseGotoTarget(TEXT("Rheinstraße"));
		TestFalse(TEXT("Ein Name sind keine Koordinaten"), T.bHasCoordinates);
		TestEqual(TEXT("Der Name bleibt unveraendert"), T.StreetName, FString(TEXT("Rheinstraße")));
	}

	{
		// Ein Komma im Namen darf ihn nicht zu Koordinaten machen - genau hier
		// waere eine naive Zerlegung falsch abgebogen.
		const FTarget T = UWiesbadenCitySubsystem::ParseGotoTarget(TEXT("Berliner Strasse, Ost"));
		TestFalse(TEXT("Komma im Namen bleibt ein Name"), T.bHasCoordinates);
		TestEqual(TEXT("Der ganze Name bleibt erhalten"),
			T.StreetName, FString(TEXT("Berliner Strasse, Ost")));
	}

	{
		const FTarget T = UWiesbadenCitySubsystem::ParseGotoTarget(TEXT("   "));
		TestFalse(TEXT("Nur Leerzeichen ist keine gueltige Angabe"), T.IsValid());
	}

	// -- 3. Suche im Netz --------------------------------------------------
	const FRoadNetwork Network = MakeNamedNetwork();

	{
		FVector2D Found = FVector2D::ZeroVector;
		double LengthCm = 0.0;
		const bool bOk = UWiesbadenCitySubsystem::FindStreetLocation(
			Network, TEXT("Rheinstraße"), Found, LengthCm);

		TestTrue(TEXT("Die Strasse wird gefunden"), bOk);
		// Der LANGE Zug (400 m) muss gewinnen, nicht der erste Treffer (20 m).
		TestTrue(FString::Printf(TEXT("Der Hauptzug gewinnt (%.0f m)"), LengthCm / 100.0),
			LengthCm > 39000.0);
		TestTrue(TEXT("Der Punkt liegt in der Mitte des langen Zugs"),
			Found.Equals(FVector2D(120000.0, 0.0), 1.0));
	}

	{
		// Gross-/Kleinschreibung darf nicht entscheiden: die Schreibung der
		// OSM-Daten kennt man beim Aufrufen nicht.
		FVector2D Found = FVector2D::ZeroVector;
		double LengthCm = 0.0;
		TestTrue(TEXT("Kleinschreibung findet dieselbe Strasse"),
			UWiesbadenCitySubsystem::FindStreetLocation(
				Network, TEXT("rheinstraße"), Found, LengthCm));
		TestTrue(TEXT("Und denselben Punkt"), Found.Equals(FVector2D(120000.0, 0.0), 1.0));
	}

	{
		// Teilnamen sollen reichen - "Kaiser" statt des ganzen Bindestrich-Namens.
		FVector2D Found = FVector2D::ZeroVector;
		double LengthCm = 0.0;
		TestTrue(TEXT("Ein Teilname genuegt"),
			UWiesbadenCitySubsystem::FindStreetLocation(
				Network, TEXT("Kaiser"), Found, LengthCm));
		TestTrue(TEXT("Er trifft den richtigen Zug"),
			Found.Equals(FVector2D(0.0, 10000.0), 1.0));
	}

	{
		FVector2D Found = FVector2D(999.0, 999.0);
		double LengthCm = 0.0;
		TestFalse(TEXT("Ein unbekannter Name wird nicht gefunden"),
			UWiesbadenCitySubsystem::FindStreetLocation(
				Network, TEXT("Gibtsnicht"), Found, LengthCm));
		TestEqual(TEXT("Und die Laenge bleibt null"), LengthCm, 0.0);
	}

	{
		FVector2D Found = FVector2D::ZeroVector;
		double LengthCm = 0.0;
		TestFalse(TEXT("Ein leerer Name findet nichts (sonst traefe er alles)"),
			UWiesbadenCitySubsystem::FindStreetLocation(
				Network, FString(), Found, LengthCm));
	}

	return true;
}
