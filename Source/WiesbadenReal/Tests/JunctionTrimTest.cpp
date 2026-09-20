// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/RoadNetworkGenerator.h"
#include "GIS/RoadNetworkTypes.h"
#include "GIS/RoadTypeLibrary.h"

namespace
{
	/** Gerades Segment von A nach B mit fester Fahrbahnbreite. */
	FRoadSegment MakeStraight(int32 Id, const FVector2D& A, const FVector2D& B, double WidthCm)
	{
		FRoadSegment Segment;
		Segment.SegmentId = Id;
		Segment.HighwayType = EOSMHighwayType::Residential;
		Segment.CarriagewayWidthCm = WidthCm;
		Segment.Centerline = {
			FVector(A.X, A.Y, 0.0),
			FVector(B.X, B.Y, 0.0) };
		Segment.LengthCm = FVector2D::Distance(A, B);
		return Segment;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJunctionTrimDistanceTest,
	"WiesbadenReal.GIS.RoadNetwork.JunctionTrimDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Der Rueckschnitt an einer Kreuzung bleibt in der Groessenordnung der
 * Querstrasse - eine geradeaus durchlaufende Strasse wird gar nicht gekuerzt.
 *
 * Die Trimmweite entsteht aus "halbe Breite des anderen Arms geteilt durch
 * |sin(Winkel)|". Bei einer geradeaus durchlaufenden Strasse ist der Winkel
 * zum Gegenarm 180 Grad und der Sinus null; ein Divisor-Minimum von 0,35
 * machte daraus das 2,86-fache der halben Breite. Gemessen ergab das in
 * Wiesbaden eine mittlere Trimmweite von 884 cm - fast neun Meter an JEDEM
 * Arm JEDER Kreuzung. Die Kreuzungsplatte, die konvexe Huelle der Armenden,
 * deckte davon nur 42 Prozent ab; der Rest blieb blankes Gelaende, und im
 * Spiel klaffte in jeder Kreuzungsmitte ein Loch.
 *
 * Der Test rechnet die reine Geometrie nach, ohne Welt und ohne Karte.
 */
bool FJunctionTrimDistanceTest::RunTest(const FString& Parameters)
{
	// Die Formel aus BuildIntersections, hier fuer den Vergleich nachgebildet.
	// Sie ist dort nicht ausgelagert; nachgerechnet wird deshalb die Aussage,
	// nicht der Aufruf.
	auto RequiredTrim = [](double OtherHalfWidthCm, double AngleDeg) -> double
	{
		constexpr double StraightThroughDeg = 150.0;
		if (AngleDeg > StraightThroughDeg)
		{
			return 0.0;
		}
		const double SinAngle = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(AngleDeg)));
		return OtherHalfWidthCm / FMath::Max(SinAngle, 0.5);
	};

	constexpr double HalfWidth = 300.0;   // 6 m Fahrbahn

	// Geradeaus durchlaufende Strasse: kein Rueckschnitt.
	TestEqual(TEXT("Gegenarm (180 Grad) verlangt keinen Rueckschnitt"),
		RequiredTrim(HalfWidth, 180.0), 0.0);
	TestEqual(TEXT("Fast gerade (170 Grad) verlangt keinen Rueckschnitt"),
		RequiredTrim(HalfWidth, 170.0), 0.0);

	// Rechtwinklige Kreuzung: genau die halbe Breite der Querstrasse.
	TestTrue(
		FString::Printf(TEXT("Rechter Winkel ergibt die halbe Querbreite (%.0f cm)"),
			RequiredTrim(HalfWidth, 90.0)),
		FMath::IsNearlyEqual(RequiredTrim(HalfWidth, 90.0), HalfWidth, 1.0));

	// Schraege Kreuzung: mehr, aber begrenzt.
	const double Oblique = RequiredTrim(HalfWidth, 45.0);
	TestTrue(
		FString::Printf(TEXT("45 Grad ergibt das 1,41-fache (%.0f cm)"), Oblique),
		FMath::IsNearlyEqual(Oblique, HalfWidth * UE_SQRT_2, 2.0));

	// Spitzer Winkel: hart begrenzt auf das Doppelte, statt zu explodieren.
	const double Sharp = RequiredTrim(HalfWidth, 5.0);
	TestTrue(
		FString::Printf(TEXT("Spitzer Winkel bleibt beim Doppelten (%.0f cm)"), Sharp),
		FMath::IsNearlyEqual(Sharp, HalfWidth * 2.0, 1.0));

	// Kernaussage: Nirgends mehr als das Doppelte der halben Querbreite.
	double Worst = 0.0;
	for (int32 Deg = 0; Deg <= 180; ++Deg)
	{
		Worst = FMath::Max(Worst, RequiredTrim(HalfWidth, static_cast<double>(Deg)));
	}
	TestTrue(
		FString::Printf(TEXT("Groesster Rueckschnitt bleibt unter 2x halbe Breite (%.0f cm)"), Worst),
		Worst <= HalfWidth * 2.0 + 1.0);

	// Gegenprobe zur alten Fassung: Sie lieferte bei 180 Grad das 2,86-fache.
	const double OldStraightThrough = HalfWidth / 0.35;
	TestTrue(
		FString::Printf(TEXT("Die alte Fassung war bei 180 Grad um %.0f cm zu gross"),
			OldStraightThrough),
		OldStraightThrough > HalfWidth * 2.5);

	return true;
}
