// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenTrafficLights.h"

namespace
{
	FRoadLane MakeSignalLane(int32 Id, const TArray<FVector>& Line)
	{
		FRoadLane Lane;
		Lane.LaneId = Id;
		Lane.SegmentId = 0;
		Lane.Direction = ELaneDirection::Forward;
		Lane.Centerline = Line;
		Lane.LengthCm = 0.0;
		for (int32 i = 1; i < Line.Num(); ++i)
		{
			Lane.LengthCm += FVector::Dist(Line[i], Line[i - 1]);
		}
		Lane.SpeedLimitKmh = 50.0;
		return Lane;
	}

	FLaneConnection MakeConnection(int32 From, int32 To, int64 Node, ETurnType Turn)
	{
		FLaneConnection C;
		C.FromLaneId = From;
		C.ToLaneId = To;
		C.IntersectionNodeId = Node;
		C.TurnType = Turn;
		C.ConnectionPath = { FVector(10000.0, 0.0, 0.0), FVector(10500.0, 0.0, 0.0) };
		return C;
	}

	/**
	 * Kreuzung mit Linksabbiegern: Spur 0 kommt von Westen (Peilung 0, Achse 0)
	 * und faehrt entweder geradeaus oder links; Spur 1 kommt von Norden
	 * (Peilung -90, Achse 1) und faehrt geradeaus.
	 */
	FRoadNetwork MakeLeftTurnNetwork()
	{
		FRoadNetwork Network;
		Network.Lanes.Add(MakeSignalLane(0, { FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0) }));
		Network.Lanes.Add(MakeSignalLane(1, { FVector(10000.0, 10000.0, 0.0), FVector(10000.0, 0.0, 0.0) }));
		Network.Lanes.Add(MakeSignalLane(2, { FVector(10500.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0) }));
		Network.Lanes.Add(MakeSignalLane(3, { FVector(10000.0, 500.0, 0.0), FVector(10000.0, 10000.0, 0.0) }));

		FRoadIntersection Intersection;
		Intersection.NodeId = 42;
		Intersection.Location = FVector(10000.0, 0.0, 0.0);
		Intersection.Control = EIntersectionControl::TrafficSignals;
		Intersection.RadiusCm = 500.0;
		Network.Intersections.Add(Intersection);

		Network.Connections.Add(MakeConnection(0, 2, 42, ETurnType::Through));   // Achse 0 geradeaus
		Network.Connections.Add(MakeConnection(0, 3, 42, ETurnType::Left));      // Achse 0 LINKS
		Network.Connections.Add(MakeConnection(1, 3, 42, ETurnType::Through));   // Achse 1 geradeaus

		FRoadSegment Segment;
		Segment.SegmentId = 0;
		Segment.HighwayType = EOSMHighwayType::Secondary;
		Segment.LengthCm = 20000.0;
		Network.Segments.Add(Segment);

		return Network;
	}

	/**
	 * Kreuzung mit Armdaten - das ist die Eingabe der Groessenbestimmung.
	 *
	 * @param HalfWidthCm Halbe Fahrbahnbreite JE Arm (325 cm = gewoehnliche
	 *        zweispurige Strasse, das ist der Standardwert im Netz).
	 * @param ArmCount    Zahl der Arme.
	 */
	FRoadIntersection MakeArmedIntersection(double HalfWidthCm, int32 ArmCount)
	{
		FRoadIntersection Intersection;
		Intersection.NodeId = 42;
		Intersection.Location = FVector(10000.0, 0.0, 0.0);
		Intersection.Control = EIntersectionControl::TrafficSignals;
		Intersection.RadiusCm = 500.0;

		for (int32 i = 0; i < ArmCount; ++i)
		{
			FIntersectionArm Arm;
			Arm.SegmentId = 0;
			Arm.HalfWidthCm = HalfWidthCm;
			Arm.BearingDegrees = (360.0 / FMath::Max(ArmCount, 1)) * i;
			Intersection.Arms.Add(Arm);
		}
		return Intersection;
	}

	/** Das Linksabbieger-Netz, aber mit Armdaten an der Kreuzung. */
	FRoadNetwork MakeSizedNetwork(double HalfWidthCm, int32 ArmCount)
	{
		FRoadNetwork Network = MakeLeftTurnNetwork();
		Network.Intersections[0].Arms = MakeArmedIntersection(HalfWidthCm, ArmCount).Arms;
		return Network;
	}

	FWiesbadenTrafficLightSettings MakeProgramSettings()
	{
		FWiesbadenTrafficLightSettings S;
		S.GreenSecondsPerCycle = 20.0;
		S.RedAmberSeconds = 1.0;
		S.AmberSeconds = 3.0;
		S.AllRedSeconds = 2.0;
		S.LeftTurnGreenSeconds = 5.0;
		S.bProtectedLeftTurns = true;
		S.bGreenWave = false;      // fuer die Programm-Tests stoert der Ortsversatz
		S.RandomSeed = 4242;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalGroupTest,
	"WiesbadenReal.Traffic.Signalgruppen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSignalGroupTest::RunTest(const FString& Parameters)
{
	using FSys = FWiesbadenTrafficLightSystem;

	// -- 1. Achse aus der Peilung, auch bei negativen Winkeln. --------------
	//
	// Atan2 liefert -180..180. Ohne Normierung faellt -90 Grad in die falsche
	// Achse, und eine ganze Anfahrtsrichtung bekommt das Signal der Querachse.
	TestEqual(TEXT("Peilung 0 -> Achse 0"), FSys::AxisForBearing(0.0), 0);
	TestEqual(TEXT("Peilung 90 -> Achse 0"), FSys::AxisForBearing(90.0), 0);
	TestEqual(TEXT("Peilung 200 -> Achse 1"), FSys::AxisForBearing(200.0), 1);
	TestEqual(TEXT("Peilung -90 ist 270 -> Achse 1"), FSys::AxisForBearing(-90.0), 1);
	TestEqual(TEXT("Peilung 360 ist 0 -> Achse 0"), FSys::AxisForBearing(360.0), 0);

	// -- 2. Gruppen: geradeaus und rechts teilen sich eine, links bekommt
	//       eine eigene. ---------------------------------------------------
	TestEqual(TEXT("Achse 0 geradeaus -> Gruppe 0"), FSys::GroupForApproach(0, false), 0);
	TestEqual(TEXT("Achse 0 links -> Gruppe 1"), FSys::GroupForApproach(0, true), 1);
	TestEqual(TEXT("Achse 1 geradeaus -> Gruppe 2"), FSys::GroupForApproach(1, false), 2);
	TestEqual(TEXT("Achse 1 links -> Gruppe 3"), FSys::GroupForApproach(1, true), 3);

	TestTrue(TEXT("Links zaehlt als Linksabbieger"), FSys::IsLeftTurn(ETurnType::Left));
	TestTrue(TEXT("Wenden kreuzt denselben Gegenverkehr"), FSys::IsLeftTurn(ETurnType::UTurn));
	TestFalse(TEXT("Rechts braucht keine eigene Phase"), FSys::IsLeftTurn(ETurnType::Right));
	TestFalse(TEXT("Geradeaus braucht keine eigene Phase"), FSys::IsLeftTurn(ETurnType::Through));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalProgramTest,
	"WiesbadenReal.Traffic.Signalprogramm",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSignalProgramTest::RunTest(const FString& Parameters)
{
	const FRoadNetwork Network = MakeLeftTurnNetwork();
	FWiesbadenTrafficLightSystem Sys;
	Sys.Initialize(Network, MakeProgramSettings());

	TestEqual(TEXT("Eine Ampel"), Sys.GetTrafficLightCount(), 1);
	if (Sys.Lights.Num() != 1)
	{
		return false;
	}
	const FWiesbadenTrafficLight& Light = Sys.Lights[0];

	// -- 1. Die Linksabbieger-Verbindung liegt in einer EIGENEN Gruppe. -----
	const int32* GroupThrough = Light.ConnectionGroups.Find(0);
	const int32* GroupLeft = Light.ConnectionGroups.Find(1);
	const int32* GroupCross = Light.ConnectionGroups.Find(2);
	TestTrue(TEXT("Alle drei Verbindungen sind zugeordnet"),
		GroupThrough && GroupLeft && GroupCross);
	if (!GroupThrough || !GroupLeft || !GroupCross)
	{
		return false;
	}
	TestNotEqual(TEXT("Linksabbieger nicht in der Geradeaus-Gruppe"), *GroupLeft, *GroupThrough);
	TestEqual(TEXT("Geradeaus Achse 0 -> Gruppe 0"), *GroupThrough, 0);
	TestEqual(TEXT("Links Achse 0 -> Gruppe 1"), *GroupLeft, 1);
	TestEqual(TEXT("Querachse geradeaus -> Gruppe 2"), *GroupCross, 2);

	// -- 2. Das Programm hat eine Abbiegephase - aber nur fuer die Achse,
	//       die auch Linksabbieger hat. ---------------------------------
	int32 LeftPhases = 0;
	for (const FWiesbadenSignalPhase& Phase : Light.Phases)
	{
		if ((Phase.Group % 2) == 1)
		{
			++LeftPhases;
			TestEqual(TEXT("Die Abbiegephase gehoert zu Achse 0"), Phase.Group, 1);
		}
	}
	TestEqual(TEXT("Genau eine Abbiegephase (nur Achse 0 hat Linksabbieger)"), LeftPhases, 1);
	TestEqual(TEXT("Drei Phasen: zwei Hauptrichtungen plus eine Abbiegephase"),
		Light.Phases.Num(), 3);

	// -- 3. KONFLIKTFREIHEIT: nie zwei Gruppen gleichzeitig nicht-rot. ------
	//
	// Das ist die Zusage, an der alles haengt. Waeren Linksabbieger und
	// Gegenverkehr gleichzeitig frei, fuehren sie ineinander - der Verkehr
	// beachtet keinen Gegenverkehr.
	{
		constexpr double Dt = 0.05;
		const int32 Steps = FMath::RoundToInt(Light.CycleSeconds * 2.0 / Dt);
		bool bNeverTwo = true;
		int32 GreenSeen[4] = { 0, 0, 0, 0 };

		for (int32 i = 0; i < Steps; ++i)
		{
			int32 NonRed = 0;
			for (int32 Group = 0; Group < 4; ++Group)
			{
				const ESignalAspect A = Sys.GetGroupAspect(0, Group);
				if (A != ESignalAspect::Red)
				{
					++NonRed;
				}
				if (A == ESignalAspect::Green)
				{
					++GreenSeen[Group];
				}
			}
			bNeverTwo = bNeverTwo && (NonRed <= 1);
			Sys.Tick(static_cast<float>(Dt));
		}

		TestTrue(TEXT("Nie zwei Gruppen gleichzeitig nicht-rot"), bNeverTwo);
		TestTrue(TEXT("Geradeaus Achse 0 wird gruen"), GreenSeen[0] > 0);
		TestTrue(TEXT("Linksabbieger Achse 0 wird gruen"), GreenSeen[1] > 0);
		TestTrue(TEXT("Geradeaus Achse 1 wird gruen"), GreenSeen[2] > 0);
		TestEqual(TEXT("Die Gruppe ohne Phase bleibt dauerhaft rot"), GreenSeen[3], 0);

		// Die Abbiegephase ist KURZ, die Hauptphase lang - sonst warten alle
		// anderen fuer eine Handvoll Abbieger.
		TestTrue(FString::Printf(
			TEXT("Hauptphase (%d Schritte gruen) traegt mehr als die Abbiegephase (%d)"),
			GreenSeen[0], GreenSeen[1]),
			GreenSeen[0] > GreenSeen[1]);
	}

	// -- 4. Eine Kreuzung OHNE Linksabbieger bekommt keine Abbiegephase. ----
	//
	// Sonst stuenden bei 60 s Umlauf 11 s fuer eine Bewegung, die es dort
	// nicht gibt - und alle anderen warten laenger.
	{
		FRoadNetwork Plain = MakeLeftTurnNetwork();
		Plain.Connections.RemoveAt(1);   // die Linksabbieger-Verbindung raus

		FWiesbadenTrafficLightSystem PlainSys;
		PlainSys.Initialize(Plain, MakeProgramSettings());
		TestEqual(TEXT("Ohne Linksabbieger nur zwei Phasen"),
			PlainSys.Lights[0].Phases.Num(), 2);

		// Die Hauptphase bleibt GLEICH lang - der UMLAUF wird kuerzer.
		// Genau das ist der Gewinn der abgeleiteten Umlaufzeit: eine
		// zusaetzliche Phase verlaengert den Umlauf, statt der Hauptrichtung
		// ihr Gruen wegzunehmen.
		TestTrue(TEXT("Die Hauptphase bleibt gleich lang"),
			FMath::IsNearlyEqual(PlainSys.Lights[0].Phases[0].DurationSeconds,
				Light.Phases[0].DurationSeconds, 0.01f));
		TestTrue(FString::Printf(TEXT("Der Umlauf ist kuerzer (%.0f statt %.0f s)"),
			PlainSys.Lights[0].CycleSeconds, Light.CycleSeconds),
			PlainSys.Lights[0].CycleSeconds < Light.CycleSeconds - 1.0);
	}

	// -- 5. Abbiegephasen abschaltbar. --------------------------------------
	{
		FWiesbadenTrafficLightSettings S = MakeProgramSettings();
		S.bProtectedLeftTurns = false;
		FWiesbadenTrafficLightSystem NoLeft;
		NoLeft.Initialize(Network, S);
		TestEqual(TEXT("Ohne Abbiegephasen bleiben zwei Phasen"),
			NoLeft.Lights[0].Phases.Num(), 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenWaveTest,
	"WiesbadenReal.Traffic.GrueneWelle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGreenWaveTest::RunTest(const FString& Parameters)
{
	// Zwei Ampeln auf DERSELBEN geraden Achse, 300 m auseinander. Ihre Arme
	// zeigen bewusst in ENTGEGENGESETZTE Richtungen - ohne Normierung auf eine
	// Halbebene haetten die Versaetze verschiedene Vorzeichen, und aus der
	// Welle wuerde ein Gegentakt.
	FRoadNetwork Network;

	FRoadSegment Segment;
	Segment.SegmentId = 0;
	Segment.HighwayType = EOSMHighwayType::Primary;
	Segment.LengthCm = 100000.0;
	Network.Segments.Add(Segment);

	auto AddLight = [&Network](int64 NodeId, double XCm, const FVector& Outward)
	{
		FRoadIntersection I;
		I.NodeId = NodeId;
		I.Location = FVector(XCm, 0.0, 0.0);
		I.Control = EIntersectionControl::TrafficSignals;
		FIntersectionArm Arm;
		Arm.SegmentId = 0;
		Arm.OutwardDirection = Outward;
		I.Arms.Add(Arm);
		Network.Intersections.Add(I);
	};

	AddLight(1, 0.0, FVector(1.0, 0.0, 0.0));
	AddLight(2, 30000.0, FVector(-1.0, 0.0, 0.0));   // Arm zeigt zurueck

	FWiesbadenTrafficLightSettings S;
	S.bGreenWave = true;
	S.GreenWaveSpeedKmh = 50.0;

	FWiesbadenTrafficLightSystem Sys;
	Sys.Initialize(Network, S);
	TestEqual(TEXT("Zwei Ampeln"), Sys.GetTrafficLightCount(), 2);
	if (Sys.Lights.Num() != 2)
	{
		return false;
	}

	// 300 m bei 50 km/h sind 21,6 s.
	const double ExpectedSeconds = 300.0 / (50.0 * 1000.0 / 3600.0);
	const double Actual = Sys.Lights[1].PhaseOffsetSeconds - Sys.Lights[0].PhaseOffsetSeconds;

	TestTrue(FString::Printf(
		TEXT("Versatz %.2f s entspricht der Fahrzeit %.2f s"), Actual, ExpectedSeconds),
		FMath::Abs(Actual - ExpectedSeconds) < 0.2);

	// -- Abschaltbar: dann wieder der Hash, also NICHT die Fahrzeit. --------
	{
		FWiesbadenTrafficLightSettings NoWave = S;
		NoWave.bGreenWave = false;
		FWiesbadenTrafficLightSystem Hashed;
		Hashed.Initialize(Network, NoWave);
		const double HashDelta =
			Hashed.Lights[1].PhaseOffsetSeconds - Hashed.Lights[0].PhaseOffsetSeconds;
		TestTrue(TEXT("Ohne gruene Welle folgt der Versatz nicht der Fahrzeit"),
			FMath::Abs(HashDelta - ExpectedSeconds) > 0.5);
	}

	// -- Die Geschwindigkeit geht ein: schneller ausgelegt, kleinerer Versatz.
	{
		FWiesbadenTrafficLightSettings Fast = S;
		Fast.GreenWaveSpeedKmh = 100.0;
		FWiesbadenTrafficLightSystem FastSys;
		FastSys.Initialize(Network, Fast);
		const double FastDelta =
			FastSys.Lights[1].PhaseOffsetSeconds - FastSys.Lights[0].PhaseOffsetSeconds;
		TestTrue(FString::Printf(TEXT("Doppelte Auslegung halbiert den Versatz (%.2f s)"), FastDelta),
			FMath::Abs(FastDelta - ExpectedSeconds * 0.5) < 0.2);
	}

	return true;
}

// ===========================================================================
//  Die GROESSE der Kreuzung bestimmt ihre Zeiten - der Umlauf bleibt im Raster
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJunctionCycleSizeTest,
	"WiesbadenReal.Traffic.UmlaufGroesse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Vorher trug JEDE Kreuzung dieselbe Gruenzeit und damit denselben Umlauf: die
 * Wohnstrassenkreuzung mit zwei Fahrstreifen genauso wie der sechsspurige
 * Knoten. Wer vor einer leeren Querstrasse steht, wartet so lange wie vor einer
 * Hauptachse.
 *
 * Geprueft werden die datenreinen Regeln selbst und die Zusage, die sie
 * zusammenhaelt: der Umlauf bleibt auf dem gemeinsamen Raster. Nur dann kann es
 * eine gruene Welle geben - sie setzt gleichen Takt voraus.
 */
bool FJunctionCycleSizeTest::RunTest(const FString& Parameters)
{
	using FSys = FWiesbadenTrafficLightSystem;

	// Liegt ein Umlauf auf dem Raster?
	//
	// NICHT einfach Fmod == 0 pruefen: die Phasendauern sind float, die Summe
	// trifft 70 s als 69,99999 - und Fmod liefert dann ein fast VOLLES Raster
	// statt null. Beide Enden zaehlen.
	const auto IsOnGrid = [](double Cycle, double Quantum, double Tolerance)
	{
		if (Quantum <= 0.0)
		{
			return true;
		}
		const double Rest = FMath::Fmod(Cycle, Quantum);
		return (Rest < Tolerance) || ((Quantum - Rest) < Tolerance);
	};

	FWiesbadenTrafficLightSettings S = MakeProgramSettings();
	S.GreenSecondsPerCycle = 20.0;      // groesste Kreuzung
	S.MinGreenSecondsPerCycle = 7.0;    // kleinste Kreuzung
	S.CycleQuantumSeconds = 10.0;
	S.ClearanceSpeedKmh = 25.0;
	S.AllRedSeconds = 2.0;

	// -- 1. Die Groesse selbst ----------------------------------------------
	{
		// 6,5 m = zweispurige Wohnstrasse (FIntersectionArm::HalfWidthCm steht
		// ohne OSM-Angabe auf 325 cm) - das ist die kleinste Kreuzung im Netz.
		TestTrue(TEXT("Schmale 3-arm-Kreuzung ist klein"),
			FSys::JunctionSize01(MakeArmedIntersection(325.0, 3)) < 0.05);

		TestTrue(TEXT("Breite 6-arm-Kreuzung ist gross"),
			FSys::JunctionSize01(MakeArmedIntersection(1000.0, 6)) > 0.95);

		// Ohne Armdaten (synthetische Netze) eine MITTLERE Kreuzung - keine
		// Groesse aus fehlenden Daten erfinden.
		FRoadIntersection Bare;
		TestTrue(TEXT("Ohne Armdaten: mittlere Groesse"),
			FMath::IsNearlyEqual(FSys::JunctionSize01(Bare), 0.5, 0.001));

		// Monoton in der Breite - sonst waere die Zuordnung nicht erklaerbar.
		double Previous = -1.0;
		bool bMonotone = true;
		for (double HalfCm = 325.0; HalfCm <= 1200.0; HalfCm += 25.0)
		{
			const double Size = FSys::JunctionSize01(MakeArmedIntersection(HalfCm, 4));
			bMonotone = bMonotone && (Size >= Previous - 0.0001);
			Previous = Size;
		}
		TestTrue(TEXT("Breiter heisst nie kleiner"), bMonotone);

		// Die BREITESTE Zufahrt zaehlt, nicht der Mittelwert: eine Hauptstrasse
		// mit einmuendenden Wohnstrassen ist eine grosse Kreuzung.
		{
			FRoadIntersection Mixed = MakeArmedIntersection(325.0, 4);
			Mixed.Arms[0].HalfWidthCm = 1000.0;
			TestTrue(TEXT("Die breiteste Zufahrt bestimmt die Breite"),
				FMath::IsNearlyEqual(FSys::WidestApproachMeters(Mixed), 20.0, 0.01));
		}
	}

	// -- 2. Gruenzeit und Raeumzeit folgen der Groesse ----------------------
	{
		TestTrue(TEXT("Kleinste Kreuzung bekommt das Mindestgruen"),
			FMath::IsNearlyEqual(FSys::GreenSecondsFor(S, 0.0), S.MinGreenSecondsPerCycle, 0.01));
		TestTrue(TEXT("Groesste Kreuzung bekommt die volle Gruenzeit"),
			FMath::IsNearlyEqual(FSys::GreenSecondsFor(S, 1.0), S.GreenSecondsPerCycle, 0.01));

		// DIE GEGENPROBE zum kaputten Stand: vorher bekamen beide dasselbe.
		TestTrue(TEXT("Gross und klein bekommen NICHT dieselbe Gruenzeit"),
			FSys::GreenSecondsFor(S, 1.0) - FSys::GreenSecondsFor(S, 0.0) > 5.0);

		// Die Abbiegephase folgt schwaecher: auch an einem kleinen Knoten
		// muessen die Wartenden herauskommen.
		const double LeftSmall = FSys::LeftGreenSecondsFor(S, 0.0);
		const double LeftLarge = FSys::LeftGreenSecondsFor(S, 1.0);
		TestTrue(TEXT("Abbiegegruen waechst mit der Groesse"), LeftLarge > LeftSmall);
		TestTrue(TEXT("Abbiegegruen faellt nie unter die Haelfte"),
			LeftSmall > LeftLarge * 0.5);

		// Raeumzeit: ueber eine breite Kreuzung braucht man laenger.
		const double ClearNarrow = FSys::ClearanceSecondsFor(S, 6.5);
		const double ClearWide = FSys::ClearanceSecondsFor(S, 20.0);
		TestTrue(TEXT("Breite Kreuzung braucht mehr Raeumzeit"), ClearWide > ClearNarrow);
		TestTrue(TEXT("Raeumzeit faellt nie unter die Mindest-Allrotzeit"),
			ClearNarrow >= S.AllRedSeconds - 0.001);
		// 20 m bei 25 km/h (6,94 m/s) sind 2,88 s.
		TestTrue(FString::Printf(TEXT("20 m bei 25 km/h sind rund 2,9 s (%.2f)"), ClearWide),
			FMath::Abs(ClearWide - 2.88) < 0.05);
	}

	// -- 3. DAS RASTER: der Umlauf liegt immer darauf ------------------------
	//
	// Das Raster ist der Preis fuer die gruene Welle - sie setzt gleichen Takt
	// voraus. Kaufmaennisch gerundet konnte der Umlauf unter die Summe aus
	// festen Zeiten und Mindestgruen fallen; die Gruenzeit wurde dann
	// hochgezogen und der tatsaechliche Umlauf lag ZWISCHEN zwei Rasterstufen.
	{
		TestTrue(TEXT("34 s werden auf 30 s gerundet"),
			FMath::IsNearlyEqual(FSys::QuantiseCycle(S, 34.0), 30.0, 0.01));
		TestTrue(TEXT("36 s werden auf 40 s gerundet"),
			FMath::IsNearlyEqual(FSys::QuantiseCycle(S, 36.0), 40.0, 0.01));

		// Mit Untergrenze wird AUFgerundet, nicht kaufmaennisch.
		TestTrue(TEXT("Untergrenze 34 s erzwingt 40 s statt 30 s"),
			FMath::IsNearlyEqual(FSys::QuantiseCycle(S, 34.0, 34.0), 40.0, 0.01));
		TestTrue(TEXT("Eine schon erfuellte Untergrenze aendert nichts"),
			FMath::IsNearlyEqual(FSys::QuantiseCycle(S, 34.0, 25.0), 30.0, 0.01));

		// Ergebnis ist IMMER ein Vielfaches des Rasters.
		bool bOnGrid = true;
		for (double Desired = 5.0; Desired <= 200.0; Desired += 1.0)
		{
			for (double Required = 0.0; Required <= 120.0; Required += 7.0)
			{
				const double C = FSys::QuantiseCycle(S, Desired, Required);
				bOnGrid = bOnGrid
					&& IsOnGrid(C, S.CycleQuantumSeconds, 0.001)
					&& (C >= Required - 0.001);
			}
		}
		TestTrue(TEXT("Jeder Umlauf ist ein Vielfaches des Rasters und traegt die Untergrenze"),
			bOnGrid);

		// Raster 0 schaltet es ab - dann gilt der Wunsch.
		FWiesbadenTrafficLightSettings NoGrid = S;
		NoGrid.CycleQuantumSeconds = 0.0;
		TestTrue(TEXT("Ohne Raster gilt der Wunschumlauf"),
			FMath::IsNearlyEqual(FSys::QuantiseCycle(NoGrid, 33.7), 33.7, 0.01));
	}

	// -- 4. Am gebauten Programm: klein wartet kuerzer als gross ------------
	{
		// Die Netze in BENANNTE Variablen: Initialize haelt eine Referenz, die
		// Ueberladung fuer Temporaries ist geloescht (ein Temporary haenge den
		// Zeiger in die Luft).
		const FRoadNetwork SmallNet = MakeSizedNetwork(325.0, 3);
		const FRoadNetwork LargeNet = MakeSizedNetwork(1000.0, 6);

		FWiesbadenTrafficLightSystem Small;
		Small.Initialize(SmallNet, S);
		FWiesbadenTrafficLightSystem Large;
		Large.Initialize(LargeNet, S);

		TestEqual(TEXT("Kleine Kreuzung: eine Ampel"), Small.GetTrafficLightCount(), 1);
		TestEqual(TEXT("Grosse Kreuzung: eine Ampel"), Large.GetTrafficLightCount(), 1);
		if (Small.Lights.Num() != 1 || Large.Lights.Num() != 1)
		{
			return false;
		}

		// DIE GEGENPROBE: vorher waren beide Umlaeufe gleich lang.
		TestTrue(FString::Printf(TEXT("Die kleine Kreuzung hat den kuerzeren Umlauf (%.0f < %.0f s)"),
			Small.Lights[0].CycleSeconds, Large.Lights[0].CycleSeconds),
			Small.Lights[0].CycleSeconds < Large.Lights[0].CycleSeconds - 0.5);

		TestTrue(TEXT("Die grosse Kreuzung hat mehr Gruen in der Hauptrichtung"),
			Large.Lights[0].GreenSeconds > Small.Lights[0].GreenSeconds + 0.5);

		// Die Groesse steht im Ergebnis, damit ein Umlauf im Spiel erklaerbar
		// ist und nicht nur eine Zahl bleibt.
		TestTrue(TEXT("Kleine Kreuzung traegt einen kleinen Groessenwert"),
			Small.Lights[0].SizeScore < 0.2f);
		TestTrue(TEXT("Grosse Kreuzung traegt einen grossen Groessenwert"),
			Large.Lights[0].SizeScore > 0.8f);

		// Und beide liegen auf dem Raster - sonst gibt es keine Welle.
		for (const FWiesbadenTrafficLightSystem* Sys : { &Small, &Large })
		{
			const double C = Sys->Lights[0].CycleSeconds;
			TestTrue(FString::Printf(TEXT("Umlauf %.1f s liegt auf dem 10-s-Raster"), C),
				IsOnGrid(C, S.CycleQuantumSeconds, 0.05));

			// Der Umlauf IST die Summe der Phasen - an ihr laeuft die Schaltung.
			double PhaseSum = 0.0;
			for (const FWiesbadenSignalPhase& Phase : Sys->Lights[0].Phases)
			{
				PhaseSum += Phase.DurationSeconds;
			}
			TestTrue(FString::Printf(TEXT("Phasensumme %.2f trifft den Umlauf %.2f"), PhaseSum, C),
				FMath::IsNearlyEqual(PhaseSum, C, 0.05));
		}

		// Das Mindestgruen wird nie unterschritten - auch an der kleinsten
		// Kreuzung mit Abbiegephase nicht.
		TestTrue(FString::Printf(TEXT("Mindestgruen gehalten (%.1f s)"),
			Small.Lights[0].GreenSeconds),
			Small.Lights[0].GreenSeconds >= S.MinGreenSecondsPerCycle - 0.01);
	}

	return true;
}
