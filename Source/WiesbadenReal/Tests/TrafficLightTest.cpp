// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenTrafficLights.h"
#include "GIS/WiesbadenTrafficSimulation.h"

namespace
{
	FRoadLane MakeLane(int32 Id, const TArray<FVector>& Line, double SpeedKmh = 50.0)
	{
		FRoadLane Lane;
		Lane.LaneId = Id;
		Lane.SegmentId = Id;
		Lane.Direction = ELaneDirection::Forward;
		Lane.Centerline = Line;
		Lane.LengthCm = 0.0;
		for (int32 i = 1; i < Line.Num(); ++i)
		{
			Lane.LengthCm += FVector::Dist(Line[i], Line[i - 1]);
		}
		Lane.SpeedLimitKmh = SpeedKmh;
		return Lane;
	}

	/**
	 * Netz mit einer Ampel-Kreuzung (Node 42): Lane 0 (von Westen, Bearing 0)
	 * und Lane 1 (von Norden, Bearing 90) muenden in die Kreuzung; Lane 2 und
	 * Lane 3 sind die Fortsetzungen Richtung Osten/Sueden. Connections:
	 * 0 -> 2 (durch, Achse 0) und 1 -> 3 (durch, Achse 1).
	 */
	FRoadNetwork MakeSignalNetwork()
	{
		FRoadNetwork Network;
		// Lane 0: von (0,0) nach (10000,0) - endet an der Kreuzung, Bearing 0.
		Network.Lanes.Add(MakeLane(0, { FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0) }));
		// Lane 1: von (10000,10000) nach (10000,0) - endet an der Kreuzung, Bearing 90.
		Network.Lanes.Add(MakeLane(1, { FVector(10000.0, 10000.0, 0.0), FVector(10000.0, 0.0, 0.0) }));
		// Fortsetzungen: Lane 2 Richtung Osten, Lane 3 Richtung Sueden.
		Network.Lanes.Add(MakeLane(2, { FVector(10000.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0) }));
		Network.Lanes.Add(MakeLane(3, { FVector(10000.0, 0.0, 0.0), FVector(10000.0, -10000.0, 0.0) }));

		FRoadIntersection Intersection;
		Intersection.NodeId = 42;
		Intersection.Location = FVector(10000.0, 0.0, 0.0);
		Intersection.Control = EIntersectionControl::TrafficSignals;
		Intersection.RadiusCm = 500.0;
		// Zwei Arme (West- und Nordzubringer) -> zwei Richtungsgruppen.
		FIntersectionArm ArmWest;
		ArmWest.SegmentId = 0;
		ArmWest.bIsSegmentStart = false;
		ArmWest.OutwardDirection = FVector(-1.0, 0.0, 0.0);
		ArmWest.BearingDegrees = 0.0;
		Intersection.Arms.Add(ArmWest);
		FIntersectionArm ArmNorth;
		ArmNorth.SegmentId = 0;
		ArmNorth.bIsSegmentStart = false;
		ArmNorth.OutwardDirection = FVector(0.0, -1.0, 0.0);
		ArmNorth.BearingDegrees = 90.0;
		Intersection.Arms.Add(ArmNorth);
		Network.Intersections.Add(Intersection);

		// Connection 0: Lane 0 -> Lane 2 (Achse West-Ost).
		FLaneConnection Connection0;
		Connection0.FromLaneId = 0;
		Connection0.ToLaneId = 2;
		Connection0.IntersectionNodeId = 42;
		Connection0.TurnType = ETurnType::Through;
		Connection0.ConnectionPath = { FVector(10000.0, 0.0, 0.0), FVector(15000.0, 0.0, 0.0) };
		Network.Connections.Add(Connection0);

		// Connection 1: Lane 1 -> Lane 3 (Achse Nord-Sued).
		FLaneConnection Connection1;
		Connection1.FromLaneId = 1;
		Connection1.ToLaneId = 3;
		Connection1.IntersectionNodeId = 42;
		Connection1.TurnType = ETurnType::Through;
		Connection1.ConnectionPath = { FVector(10000.0, 0.0, 0.0), FVector(10000.0, -5000.0, 0.0) };
		Network.Connections.Add(Connection1);

		FRoadSegment Segment;
		Segment.SegmentId = 0;
		Segment.HighwayType = EOSMHighwayType::Residential;
		Segment.LengthCm = 20000.0;
		Network.Segments.Add(Segment);

		Network.LaneSuccessors.FindOrAdd(0).Add(0);
		Network.LaneSuccessors.FindOrAdd(1).Add(1);
		return Network;
	}

	/**
	 * Dasselbe Netz, aber die Ampel-Kreuzung ist NICHT die erste im Array:
	 * davor stehen `Leading` ampellose Kreuzungen. In der echten Stadt sind nur
	 * ~1073 von ~20213 Kreuzungen Ampeln, die Ampel liegt also praktisch nie an
	 * Index 0 - genau diese Lage deckte der bisherige Ein-Kreuzungs-Test nie ab.
	 */
	FRoadNetwork MakeSignalNetworkAt(int32 Leading)
	{
		FRoadNetwork Network = MakeSignalNetwork();
		for (int32 i = 0; i < Leading; ++i)
		{
			FRoadIntersection Plain;
			Plain.NodeId = 1000 + i;
			Plain.Location = FVector(-100000.0 - i * 1000.0, 0.0, 0.0);
			Plain.Control = EIntersectionControl::Uncontrolled;
			Network.Intersections.Insert(Plain, 0);
		}
		return Network;
	}

	FWiesbadenTrafficLightSettings MakeLightSettings()
	{
		FWiesbadenTrafficLightSettings Settings;
		Settings.GreenSecondsPerCycle = 15.0;
		Settings.RandomSeed = 424242;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficLightSystemTest,
	"WiesbadenReal.Traffic.TrafficLights",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficLightSystemTest::RunTest(const FString& Parameters)
{
	// -- 1. Uebernahme: nur TrafficSignals-Kreuzungen werden Ampeln. ---------
	{
		FRoadNetwork Network = MakeSignalNetwork();
		Network.Intersections[0].Control = EIntersectionControl::Uncontrolled;
		FWiesbadenTrafficLightSystem System;
		System.Initialize(Network, MakeLightSettings());
		TestEqual(TEXT("Unkontrollierte Kreuzung -> keine Ampel"), System.GetTrafficLightCount(), 0);
	}

	{
		FWiesbadenTrafficLightSystem System;
		const FRoadNetwork ScopedNetwork1 = MakeSignalNetwork();
		System.Initialize(ScopedNetwork1, MakeLightSettings());
		TestEqual(TEXT("Ampel-Kreuzung -> 1 Ampel"), System.GetTrafficLightCount(), 1);
		TestTrue(TEXT("Ampel an Node 42 registriert"), System.HasTrafficLightAt(42));
	}

	// -- 2. Deterministische Phasen: gleiche Zeit/Seed -> gleicher Zustand. --
	FWiesbadenTrafficLightSystem A;
	FWiesbadenTrafficLightSystem B;
	const FRoadNetwork ScopedNetwork2 = MakeSignalNetwork();
	A.Initialize(ScopedNetwork2, MakeLightSettings());
	const FRoadNetwork ScopedNetwork3 = MakeSignalNetwork();
	B.Initialize(ScopedNetwork3, MakeLightSettings());
	for (int32 t = 0; t < 80; ++t)
	{
		A.Tick(0.5f);
		B.Tick(0.5f);
		TestTrue(TEXT("Determinismus: gleiche Gruen-Zustaende"),
			A.IsConnectionGreen(0) == B.IsConnectionGreen(0)
			&& A.IsConnectionGreen(1) == B.IsConnectionGreen(1));
	}

	// -- 3. Zyklus: beide Achsen werden innerhalb von 2 Zyklen gruen, nie
	//       gleichzeitig (eine Achse = eine Richtungsgruppe je Kreuzung). ----
	{
		FWiesbadenTrafficLightSystem System;
		const FRoadNetwork ScopedNetwork4 = MakeSignalNetwork();
		System.Initialize(ScopedNetwork4, MakeLightSettings());
		bool bSawGreenA = false;
		bool bSawGreenB = false;
		bool bNeverBoth = true;
		for (int32 t = 0; t < 80; ++t)
		{
			System.Tick(0.5f);
			const bool GreenA = System.IsConnectionGreen(0);
			const bool GreenB = System.IsConnectionGreen(1);
			bSawGreenA = bSawGreenA || GreenA;
			bSawGreenB = bSawGreenB || GreenB;
			bNeverBoth = bNeverBoth && !(GreenA && GreenB);
		}
		TestTrue(TEXT("Zyklus: Achse A wird gruen"), bSawGreenA);
		TestTrue(TEXT("Zyklus: Achse B wird gruen"), bSawGreenB);
		TestTrue(TEXT("Zyklus: nie beide Achsen gleichzeitig gruen"), bNeverBoth);
	}

	// -- 4. Stopp-Regel in der Verkehrs-Simulation: Fahrzeug haelt bei Rot
	//       an der Kreuzung und faehrt bei Gruen weiter. ----------------------
	{
		FWiesbadenTrafficLightSystem Lights;
		const FRoadNetwork ScopedNetwork5 = MakeSignalNetwork();
		Lights.Initialize(ScopedNetwork5, MakeLightSettings());

		FWiesbadenTrafficSimulation Sim;
		FWiesbadenTrafficSettings SimSettings;
		SimSettings.TrafficDensity = 0.0f; // kein Spawn - nur das Hand-Fahrzeug.
		const FRoadNetwork ScopedNetwork6 = MakeSignalNetwork();
		Sim.Initialize(ScopedNetwork6, SimSettings);
		Sim.SetTrafficLightSystem(&Lights);

		// Fahrzeug von Hand auf Lane 0 innerhalb der Stopp-Zone vor der
		// Haltelinie (Distanz 9800 von 10000; Stop-Zone ab ~9650).
		FTrafficVehicle Vehicle;
		Vehicle.VehicleId = 7;
		Vehicle.LaneId = 0;
		Vehicle.bOnLane = true;
		Vehicle.DistanceCm = 9800.0;
		Vehicle.SpeedCmS = 500.0;
		Vehicle.DesiredSpeedCmS = 500.0;
		Sim.Vehicles.Add(Vehicle);

		// Erst die Phase ermitteln: Connection 0 (Lane 0) muss rot sein, sonst
		// haelt das Fahrzeug nicht. Drehe die Zeit bis Connection 0 rot ist.
		int32 Guard = 0;
		while (Lights.IsConnectionGreen(0) && Guard++ < 100)
		{
			Lights.Tick(0.5f);
		}
		TestTrue(TEXT("Testvorbereitung: Connection 0 ist rot"), !Lights.IsConnectionGreen(0));

		const double SpeedBefore = Sim.Vehicles[0].SpeedCmS;
		Sim.Tick(1.0f);
		TestTrue(TEXT("Stopp-Regel: Fahrzeug haelt bei roter Ampel"),
			Sim.Vehicles[0].SpeedCmS < SpeedBefore || Sim.Vehicles[0].SpeedCmS < 1.0);

		// Bis Connection 0 gruen drehen, dann faehrt das Fahrzeug weiter.
		Guard = 0;
		while (!Lights.IsConnectionGreen(0) && Guard++ < 100)
		{
			Lights.Tick(0.5f);
		}
		TestTrue(TEXT("Testvorbereitung: Connection 0 ist gruen"), Lights.IsConnectionGreen(0));

		Sim.Vehicles[0].SpeedCmS = 500.0;
		Sim.Tick(1.0f);
		TestTrue(TEXT("Gruene Ampel: Fahrzeug beschleunigt weiter"),
			Sim.Vehicles[0].SpeedCmS > 100.0);
	}

	    // -- 5. Aspektmodell: Konfliktfreiheit, Dauern, Gruen-Delegation. --------
    {
        FWiesbadenTrafficLightSettings S;
        // Die Gruenzeit ist die EINGABE, nicht der Rest eines festen Umlaufs -
        // und sie haengt seit den groessenabhaengigen Programmen an der
        // Kreuzung, nicht mehr fest an den Einstellungen.
        //
        // Das Testnetz hat KEINE Armdaten, die Kreuzung gilt damit als mittlere
        // (JunctionSize01 = 0,5). Daraus: Gruen = Lerp(7, 15, 0,5) = 11 s,
        // feste Zeiten 1 + 3 + 2 = 6 s (Raeumzeit bleibt bei Breite 0 auf der
        // Mindest-Allrotzeit), Wunschumlauf 2 * 17 = 34 s. Das 10-s-Raster -
        // der Preis fuer die gruene Welle - macht daraus 30 s, und die
        // verbleibenden 30 - 2*6 = 18 s teilen sich die beiden
        // Geradeaus-Phasen: 9 s Gruen je Achse.
        //
        // Frueher stand hier 15 s Gruen und 42 s Umlauf - fuer JEDE Kreuzung
        // gleich, vom Wohnweg bis zum sechsspurigen Knoten. Genau das ist der
        // Stand, den die groessenabhaengigen Zeiten abloesen.
        S.GreenSecondsPerCycle = 15.0;
        S.RedAmberSeconds = 1.0;
        S.AmberSeconds = 3.0;
        S.AllRedSeconds = 2.0;
        S.RandomSeed = 1;
        FWiesbadenTrafficLightSystem Sys;
        const FRoadNetwork Net = MakeSignalNetwork();
        Sys.Initialize(Net, S);

        const double Dt = 0.05;
        double Dur[4] = { 0.0, 0.0, 0.0, 0.0 };
        bool bNeverBothNonRed = true;
        bool bGreenDelegationOk = true;
        // Ueber einen GANZEN Umlauf messen, sonst sind die Dauern
        // abgeschnitten und der Test misst das Messfenster statt des Programms.
        const double CycleC = Sys.Lights[0].CycleSeconds;
        const int32 StepsC = FMath::RoundToInt(CycleC / Dt);
        for (int32 i = 0; i < StepsC; ++i)
        {
            // Gruppe = Achse * 2 + (links ? 1 : 0). Das Testnetz hat nur
            // Geradeaus-Verbindungen, die Gruppen sind also 0 und 2 - nicht
            // mehr 0 und 1 wie vor den Abbiegephasen.
            const ESignalAspect A0 = Sys.GetGroupAspect(0, 0);
            const ESignalAspect A1 = Sys.GetGroupAspect(0, 2);
            Dur[static_cast<int32>(A0)] += Dt;
            const bool N0 = A0 != ESignalAspect::Red;
            const bool N1 = A1 != ESignalAspect::Red;
            bNeverBothNonRed = bNeverBothNonRed && !(N0 && N1);
            bGreenDelegationOk = bGreenDelegationOk
                && (Sys.IsConnectionGreen(0) == (Sys.GetConnectionAspect(0) == ESignalAspect::Green));
            Sys.Tick(static_cast<float>(Dt));
        }
        TestTrue(TEXT("Aspekt: nie zwei Gruppen gleichzeitig nicht-rot"), bNeverBothNonRed);
        TestTrue(TEXT("Aspekt: Gruen-Delegation stimmt mit Aspekt ueberein"), bGreenDelegationOk);
        TestTrue(TEXT("Aspekt: Rot-Gelb ~1 s"),
            FMath::Abs(Dur[static_cast<int32>(ESignalAspect::RedAmber)] - 1.0) < 0.3);
        TestTrue(FString::Printf(TEXT("Aspekt: Umlauf %.0f s (mittlere Kreuzung, 10-s-Raster)"),
            CycleC),
            FMath::Abs(CycleC - 30.0) < 0.1);
        // Der Umlauf MUSS auf dem Raster liegen: nur gleicher Takt traegt die
        // gruene Welle. Eine Ampel dazwischen laeuft ihr unsichtbar davon.
        // Beide Enden zaehlen - die Phasendauern sind float, eine Summe trifft
        // 30 s auch als 29,99999 und Fmod liefert dann fast ein volles Raster.
        const double GridRest = FMath::Fmod(CycleC, S.CycleQuantumSeconds);
        TestTrue(FString::Printf(TEXT("Aspekt: Umlauf %.1f s liegt auf dem Raster"), CycleC),
            (GridRest < 0.05) || ((S.CycleQuantumSeconds - GridRest) < 0.05));
        TestTrue(FString::Printf(TEXT("Aspekt: Gruen ~9 s (Rest des Rasterumlaufs, %.1f s)"),
            Dur[static_cast<int32>(ESignalAspect::Green)]),
            FMath::Abs(Dur[static_cast<int32>(ESignalAspect::Green)] - 9.0) < 0.3);
        TestTrue(TEXT("Aspekt: Gelb ~3 s"),
            FMath::Abs(Dur[static_cast<int32>(ESignalAspect::Amber)] - 3.0) < 0.3);
    }

    // -- 6. Zuordnung Verbindung -> Ampel, wenn die Ampel nicht die erste
    //       Kreuzung ist. Der Index wurde frueher als Index in Intersections
    //       gespeichert, aber in Lights nachgeschlagen: in der echten Stadt
    //       zeigte er damit ins Leere und JEDE Verbindung galt als gruen -
    //       kein Fahrzeug hielt je an Rot. ---------------------------------
    {
        FWiesbadenTrafficLightSystem Sys;
        const FRoadNetwork Net = MakeSignalNetworkAt(7);
        Sys.Initialize(Net, MakeLightSettings());

        TestEqual(TEXT("Versetzte Ampel: genau 1 Ampel"), Sys.GetTrafficLightCount(), 1);
        TestTrue(TEXT("Versetzte Ampel: Verbindung 0 ist signalisiert"),
            Sys.IsConnectionControlled(0));

        const int32 LightIndex = Sys.GetLightIndexForConnection(0);
        TestTrue(TEXT("Versetzte Ampel: Verbindung findet eine gueltige Ampel"),
            Sys.Lights.IsValidIndex(LightIndex));
        if (Sys.Lights.IsValidIndex(LightIndex))
        {
            TestEqual(TEXT("Versetzte Ampel: es ist die Ampel an Node 42"),
                Sys.Lights[LightIndex].NodeId, static_cast<int64>(42));
        }

        // Die eigentliche Nutzer-Eigenschaft: die Verbindung wird ueberhaupt rot.
        bool bSawRed = false;
        for (int32 i = 0; i < 1200 && !bSawRed; ++i)
        {
            bSawRed = !Sys.IsConnectionGreen(0);
            Sys.Tick(0.05f);
        }
        TestTrue(TEXT("Versetzte Ampel: Verbindung 0 wird im Zyklus rot"), bSawRed);
        TestTrue(TEXT("Versetzte Ampel: Diagnose-Sonde sieht Rot"),
            Sys.AnyControlledConnectionRed() || bSawRed);
    }

    return true;
}
