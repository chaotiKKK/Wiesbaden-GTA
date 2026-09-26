// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenTrafficSimulation.h"

/**
 * Zusammenstoss Spielerauto <-> Verkehrsauto (25.09.).
 *
 * Vorher waren die Verkehrsautos fuer das Spielerauto eine Mauer: es glitt an
 * einem unsichtbaren Kasten entlang, das Verkehrsauto merkte nichts. Jetzt
 * ein Stoss nach Impulserhaltung - das Verkehrsauto wird verschoben und
 * gedreht, sein Fahrer bremst, bleibt kurz stehen und faehrt dann weiter.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficImpactTest,
	"WiesbadenReal.Traffic.Zusammenstoss",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficImpactTest::RunTest(const FString& Parameters)
{
	using FSim = FWiesbadenTrafficSimulation;

	// -- Stossrechnung ------------------------------------------------------------
	{
		// Mittiger Stoss, gleiche Massen, plastisch: beide fahren danach gleich schnell.
		FSim::FImpactResult R;
		TestTrue(TEXT("mittig: Stoss"), FSim::ComputeImpact(FVector2D(0, 0), FVector2D(1, 0),
			FVector2D(1000, 0), 1000.0, FVector2D(0, 0), FVector2D::ZeroVector, 1000.0, 1600.0, 0.0, R));
		TestTrue(FString::Printf(TEXT("Impulserhaltung: Spieler %.0f, Verkehr %.0f cm/s"),
			1000.0 + R.PlayerDeltaVCmS.X, R.TrafficDeltaVCmS.X),
			FMath::IsNearlyEqual(1000.0 + R.PlayerDeltaVCmS.X, 500.0, 0.5) && FMath::IsNearlyEqual(R.TrafficDeltaVCmS.X, 500.0, 0.5));
		TestTrue(TEXT("mittig: keine Drehung"), FMath::IsNearlyZero(R.TrafficDeltaYawRateRadS, 1e-9));

		// Stoss am vorderen Ende (Hebelarm): dreht, und der Schwerpunkt bekommt weniger.
		FSim::FImpactResult Off;
		FSim::ComputeImpact(FVector2D(200, 0), FVector2D(0, 1), FVector2D(0, 1000), 1000.0,
			FVector2D(0, 0), FVector2D::ZeroVector, 1000.0, 1600.0, 0.0, Off);
		TestTrue(FString::Printf(TEXT("ausser der Mitte: dreht (%.2f rad/s)"), Off.TrafficDeltaYawRateRadS),
			Off.TrafficDeltaYawRateRadS > 0.1);
		TestTrue(TEXT("ausser der Mitte: weniger Schub als mittig"), Off.TrafficDeltaVCmS.Y < 500.0);

		// Schwerer Transporter nimmt weniger Tempo auf als ein leichter Kaefer.
		FSim::FImpactResult Heavy;
		FSim::ComputeImpact(FVector2D(0, 0), FVector2D(1, 0), FVector2D(1000, 0), 1000.0,
			FVector2D(0, 0), FVector2D::ZeroVector, 2400.0, 3500.0, 0.0, Heavy);
		TestTrue(TEXT("schwerer: weniger Schub"), Heavy.TrafficDeltaVCmS.X < R.TrafficDeltaVCmS.X);

		// Entfernen sich beide schon: kein Stoss.
		FSim::FImpactResult Away;
		TestFalse(TEXT("auseinander: kein Stoss"), FSim::ComputeImpact(FVector2D(0, 0), FVector2D(1, 0),
			FVector2D(-100, 0), 1000.0, FVector2D(0, 0), FVector2D::ZeroVector, 1000.0, 1600.0, 0.0, Away));
	}

	// -- In der Simulation: rutschen, stehen, zurueck in die Spur, weiterfahren ---
	{
		FRoadLane Lane;
		Lane.LaneId = 0;
		Lane.SegmentId = 0;
		Lane.Centerline = { FVector(0, 0, 0), FVector(50000, 0, 0) };
		Lane.LengthCm = 50000.0;
		FRoadNetwork Network;
		Network.Lanes.Add(Lane);
		FRoadSegment Segment;
		Segment.SegmentId = 0;
		Segment.HighwayType = EOSMHighwayType::Residential;
		Segment.LengthCm = 50000.0;
		Network.Segments.Add(Segment);

		FWiesbadenTrafficSettings Settings;
		Settings.TrafficDensity = 0.0f;
		Settings.bAllowLaneChange = false;
		FSim Sim;
		Sim.Initialize(Network, Settings);
		FTrafficVehicle V;
		V.VehicleId = 7;
		V.TypeIndex = 0;   // Golf
		V.LaneId = 0;
		V.bOnLane = true;
		V.DistanceCm = 10000.0;
		V.SpeedCmS = 800.0;
		V.DesiredSpeedCmS = 800.0;
		Sim.Vehicles.Add(V);
		for (int32 i = 0; i < 60; ++i)
		{
			Sim.Tick(1.0f / 60.0f);
		}
		const FVector Before = Sim.Vehicles[0].BodyLocation;
		const double DistBefore = Sim.Vehicles[0].DistanceCm;

		// Das Spielerauto (1200 kg, 36 km/h) trifft von der Seite (+Y) knapp vor der Mitte.
		FVector PlayerDeltaV;
		const bool bHit = Sim.ApplyPlayerImpact(7, Before + FVector(100, -80, 50), FVector(0, 1, 0),
			FVector(0, 1000, 0), 1200.0, PlayerDeltaV);
		TestTrue(TEXT("Stoss angenommen"), bHit);
		TestTrue(FString::Printf(TEXT("Spielerauto verliert Tempo (%.0f cm/s)"), PlayerDeltaV.Y), PlayerDeltaV.Y < -100.0);
		TestEqual(TEXT("unbekanntes Fahrzeug: nichts"), Sim.ApplyPlayerImpact(99, Before, FVector(0, 1, 0),
			FVector(0, 1000, 0), 1200.0, PlayerDeltaV), false);

		double MaxOffset = 0.0, MaxYawDeg = 0.0, MaxSollSpeedWhileSliding = 0.0;
		bool bRested = false;
		for (int32 i = 0; i < 60 * 3; ++i)
		{
			Sim.Tick(1.0f / 60.0f);
			const FTrafficVehicle& Now = Sim.Vehicles[0];
			MaxOffset = FMath::Max(MaxOffset, Now.KnockOffsetCm.Size());
			MaxYawDeg = FMath::Max(MaxYawDeg, FMath::Abs(FMath::RadiansToDegrees(Now.KnockYawRad)));
			if (Now.bKnocked && !Now.KnockVelCmS.IsNearlyZero(1.0))
			{
				MaxSollSpeedWhileSliding = FMath::Max(MaxSollSpeedWhileSliding, Now.SpeedCmS);
			}
			bRested = bRested || (Now.bKnocked && Now.KnockVelCmS.IsNearlyZero(1.0));
		}
		TestTrue(FString::Printf(TEXT("verschoben (%.0f cm)"), MaxOffset), MaxOffset > 50.0);
		TestTrue(FString::Printf(TEXT("gedreht (%.1f Grad)"), MaxYawDeg), MaxYawDeg > 2.0);
		TestTrue(TEXT("der Fahrer bremst waehrend des Rutschens"), MaxSollSpeedWhileSliding < 1.0);
		TestTrue(TEXT("kommt zur Ruhe"), bRested);

		for (int32 i = 0; i < 60 * 30 && Sim.Vehicles[0].bKnocked; ++i)
		{
			Sim.Tick(1.0f / 60.0f);
		}
		for (int32 i = 0; i < 60 * 3; ++i)
		{
			Sim.Vehicles[0].DesiredSpeedCmS = 800.0;
			Sim.Tick(1.0f / 60.0f);
		}
		const FTrafficVehicle& End = Sim.Vehicles[0];
		TestFalse(TEXT("zurueck in der Spur"), End.bKnocked);
		TestTrue(FString::Printf(TEXT("faehrt weiter (%.0f cm/s, %.0f m weiter)"), End.SpeedCmS, (End.DistanceCm - DistBefore) / 100.0),
			End.SpeedCmS > 300.0 && End.DistanceCm > DistBefore + 500.0);
		TestTrue(FString::Printf(TEXT("Karosserie wieder auf der Linie (Y %.0f cm)"), End.BodyLocation.Y),
			FMath::Abs(End.BodyLocation.Y) < 30.0);
	}
	return true;
}
