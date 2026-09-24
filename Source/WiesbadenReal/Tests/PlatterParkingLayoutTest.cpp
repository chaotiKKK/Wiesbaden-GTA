// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "World/WiesbadenPlatterParking.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlatterParkingLayoutTest,
	"WiesbadenReal.World.PlatterParking.Layout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlatterParkingLayoutTest::RunTest(const FString& Parameters)
{
	// Strassen-Rahmen zurueckrechnen: U laengs, N quer von der Fahrbahnachse.
	const FVector2D Origin = AWiesbadenPlatterParking::FrameToEastNorth(0.0, 0.0);
	const FVector2D U = AWiesbadenPlatterParking::FrameToEastNorth(1.0, 0.0) - Origin;
	const FVector2D N = AWiesbadenPlatterParking::FrameToEastNorth(0.0, 1.0) - Origin;
	auto ToFrame = [&](const FVector2D& EN)
	{
		return FVector2D(FVector2D::DotProduct(EN - Origin, U), FVector2D::DotProduct(EN - Origin, N));
	};

	// Der Hof liegt NORDOESTLICH von Nr. 144 an der Platter Strasse (Luftbild),
	// nicht suedwestlich von Nr. 146 im Baumbestand wie die erste Fassung.
	double GarageN, GarageU0, GarageU1;
	AWiesbadenPlatterParking::GetGarageFront(GarageN, GarageU0, GarageU1);
	TestTrue(FString::Printf(TEXT("Garagenfront liegt zur Strasse hin (N %.1f m)"), GarageN),
		GarageN > 15.0 && GarageN < 22.0);
	TestTrue(FString::Printf(TEXT("Garagenzeile ist ~14 m lang (%.1f m)"), GarageU1 - GarageU0),
		GarageU1 - GarageU0 > 12.0 && GarageU1 - GarageU0 < 16.0);

	const double Edge = AWiesbadenPlatterParking::DefaultEdgeOffsetM;
	const TArray<FWiesbadenPlatterSpace> Spaces = AWiesbadenPlatterParking::BuildSpaces(Edge);
	TestEqual(TEXT("Zwoelf seitliche Stellplaetze"), Spaces.Num(), 12);
	for (int32 i = 0; i < Spaces.Num(); ++i)
	{
		const FVector2D F = ToFrame(Spaces[i].EastNorthM);
		TestTrue(FString::Printf(TEXT("Bucht %d liegt noerdlich der Hausnummer (N-Ost %.1f m)"), i,
			Spaces[i].EastNorthM.Y), Spaces[i].EastNorthM.Y > 5.0);
		TestTrue(FString::Printf(TEXT("Bucht %d ragt nicht in den Gehweg"), i),
			F.Y - Spaces[i].LengthM * 0.5 >= Edge);
		TestTrue(FString::Printf(TEXT("Vor Bucht %d bleiben >= 6 m Fahrgasse bis zu den Garagen"), i),
			GarageN - (F.Y + Spaces[i].LengthM * 0.5) >= 6.0);
		if (i > 0)
		{
			TestEqual(TEXT("Buchten liegen im 2,5-m-Raster"),
				FVector2D::Distance(Spaces[i].EastNorthM, Spaces[i - 1].EastNorthM), 2.5, 0.01);
		}
	}

	// Startbucht: gegenueber der Garagenzeile, Nase in den Hof (rueckwaerts
	// eingeparkt) - ausgeparkt wird vorwaerts in die Fahrgasse.
	const FWiesbadenPlatterSpace Start = AWiesbadenPlatterParking::PlayerStartSpace(Edge);
	const FVector2D StartFrame = ToFrame(Start.EastNorthM);
	TestTrue(TEXT("Startbucht liegt gegenueber der Garagenzeile"),
		StartFrame.X > GarageU0 && StartFrame.X < GarageU1);
	// Unreal-Gier aus Ost/Nord: X = Ost, Y = Sued.
	const double IntoCourtYaw = FMath::RadiansToDegrees(FMath::Atan2(-N.Y, N.X));
	TestEqual(TEXT("Spieler blickt in den Hof, nicht zur Strasse"),
		double(Start.HeadingDeg), IntoCourtYaw, 0.5);
	return true;
}
