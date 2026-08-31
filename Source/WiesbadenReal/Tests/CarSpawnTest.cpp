// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicles/WiesbadenCarSpawn.h"

/**
 * Einsetzen des Spielerfahrzeugs an einer Adresse.
 *
 * Die Adresse liegt im Gebaeude, das Fahrzeug muss auf der Fahrbahn stehen.
 * Die Spursuche entscheidet damit darueber, ob der Spieler im Haus oder auf
 * der Strasse startet - und ob er in oder gegen die Fahrtrichtung steht.
 * Beides laesst sich nur mit einem synthetischen Netz sauber pruefen, weil im
 * echten Netz jede Aussage von 121.000 Segmenten abhaengt.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarSpawnTest,
	"WiesbadenReal.Vehicles.CarSpawn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** Baut ein Netz mit einer geraden Spur entlang +X bei Y = 0. */
	FRoadNetwork MakeStraightNetwork(EOSMHighwayType Type = EOSMHighwayType::Residential)
	{
		FRoadNetwork Network;

		FRoadSegment Segment;
		Segment.SegmentId = 0;
		Segment.HighwayType = Type;
		Network.Segments.Add(Segment);

		FRoadLane Lane;
		Lane.LaneId = 0;
		Lane.SegmentId = 0;
		Lane.Direction = ELaneDirection::Forward;
		Lane.Centerline = {
			FVector(0.0, 0.0, 0.0),
			FVector(1000.0, 0.0, 0.0),
			FVector(2000.0, 0.0, 0.0),
		};
		Network.Lanes.Add(Lane);

		return Network;
	}
}

bool FCarSpawnTest::RunTest(const FString& Parameters)
{
	// -- 1. Punkt-auf-Streckenzug -------------------------------------------
	TArray<FVector> Line = {
		FVector(0.0, 0.0, 0.0),
		FVector(1000.0, 0.0, 0.0),
	};

	FVector Closest;
	FVector Direction;

	// Punkt seitlich der Mitte: Lot faellt auf das Segment.
	double DistSq = FWiesbadenCarSpawn::DistanceToPolylineSquared(
		Line, FVector(500.0, 300.0, 0.0), Closest, Direction);

	TestEqual(TEXT("Lotfusspunkt liegt auf der Linie"), Closest.X, 500.0);
	TestEqual(TEXT("Lotfusspunkt hat Y = 0"), Closest.Y, 0.0);
	TestTrue(TEXT("Abstand entspricht dem Lot (300)"),
		FMath::IsNearlyEqual(FMath::Sqrt(DistSq), 300.0, 0.01));

	// Punkt hinter dem Anfang: MUSS auf den Anfangspunkt begrenzt werden.
	// Ohne Begrenzung laege er auf der Verlaengerung und damit neben der Strasse.
	FWiesbadenCarSpawn::DistanceToPolylineSquared(
		Line, FVector(-5000.0, 0.0, 0.0), Closest, Direction);
	TestEqual(TEXT("Punkt vor dem Anfang wird auf den Anfang begrenzt"), Closest.X, 0.0);

	FWiesbadenCarSpawn::DistanceToPolylineSquared(
		Line, FVector(9000.0, 0.0, 0.0), Closest, Direction);
	TestEqual(TEXT("Punkt hinter dem Ende wird auf das Ende begrenzt"), Closest.X, 1000.0);

	// -- 1b. Hoehenunterschied darf die Suche nicht dominieren --------------
	// Die Adresse liefert keine Hoehe (Z = 0), die Fahrbahn liegt auf
	// Terrainhoehe. In Wiesbaden sind das am Hang ueber 100 m. Wuerde in 3D
	// gemessen, waere der gemeldete Abstand fast reine Hoehendifferenz und
	// eine weiter entfernte Strasse auf aehnlicher Hoehe koennte gewinnen.
	// Genau das ist beim ersten Spielstart passiert: 31 m Luftlinie wurden
	// als 117 m gemeldet.
	TArray<FVector> ElevatedLine = {
		FVector(0.0, 0.0, 11328.0),
		FVector(1000.0, 0.0, 11328.0),
	};

	const double ElevatedDistSq = FWiesbadenCarSpawn::DistanceToPolylineSquared(
		ElevatedLine, FVector(500.0, 300.0, 0.0), Closest, Direction);

	TestTrue(TEXT("Abstand ist horizontal, nicht raeumlich (300 statt 11332)"),
		FMath::IsNearlyEqual(FMath::Sqrt(ElevatedDistSq), 300.0, 0.01));
	TestEqual(TEXT("Der Punkt behaelt aber die Hoehe der Fahrbahn"), Closest.Z, 11328.0);

	// Leerer Streckenzug darf nicht abstuerzen.
	TArray<FVector> Empty;
	const double EmptyDist = FWiesbadenCarSpawn::DistanceToPolylineSquared(
		Empty, FVector::ZeroVector, Closest, Direction);
	TestTrue(TEXT("Leerer Streckenzug liefert unendlichen Abstand"),
		EmptyDist >= TNumericLimits<double>::Max());

	// -- 2. Spursuche -------------------------------------------------------
	const FRoadNetwork Network = MakeStraightNetwork();

	FVector SpawnLoc;
	FRotator SpawnRot;
	int32 LaneId = INDEX_NONE;

	// Zielpunkt 400 cm neben der Fahrbahn (etwa ein Gebaeude am Strassenrand).
	TestTrue(TEXT("Spur in Reichweite wird gefunden"),
		FWiesbadenCarSpawn::FindNearestDrivableLanePoint(
			Network, FVector(1000.0, 400.0, 0.0), 10000.0, SpawnLoc, SpawnRot, LaneId));

	TestEqual(TEXT("Gefundene Spur ist Spur 0"), LaneId, 0);
	TestEqual(TEXT("Fahrzeug steht auf der Spurmitte (Y = 0)"), SpawnLoc.Y, 0.0);
	TestEqual(TEXT("Fahrzeug steht laengs am Zielpunkt"), SpawnLoc.X, 1000.0);

	// Anhebung, damit das Fahrzeug nicht in der Fahrbahn steckt.
	TestEqual(TEXT("Fahrzeug wird ueber die Fahrbahn angehoben"),
		SpawnLoc.Z, FWiesbadenCarSpawn::SpawnHeightOffsetCm);

	// Ausrichtung in Fahrtrichtung der Spur (+X = 0 Grad Gierwinkel).
	TestTrue(TEXT("Fahrzeug zeigt in Fahrtrichtung"),
		FMath::IsNearlyEqual(SpawnRot.Yaw, 0.0, 0.01));
	TestEqual(TEXT("Keine Laengsneigung uebernommen"), SpawnRot.Pitch, 0.0);
	TestEqual(TEXT("Keine Querneigung uebernommen"), SpawnRot.Roll, 0.0);

	// -- 3. Suchradius ------------------------------------------------------
	// Ausserhalb des Radius darf NICHTS geliefert werden - lieber kein
	// Fahrzeug als eines irgendwo in der Stadt.
	TestFalse(TEXT("Spur ausserhalb des Radius wird abgelehnt"),
		FWiesbadenCarSpawn::FindNearestDrivableLanePoint(
			Network, FVector(1000.0, 50000.0, 0.0), 1000.0, SpawnLoc, SpawnRot, LaneId));
	TestEqual(TEXT("Abgelehnte Suche liefert keine Spur"), LaneId, INDEX_NONE);

	// -- 4. Nur befahrbare Wege ---------------------------------------------
	// Ein Fussweg direkt neben dem Ziel darf das Fahrzeug nicht aufnehmen.
	const FRoadNetwork FootwayOnly = MakeStraightNetwork(EOSMHighwayType::Footway);
	TestFalse(TEXT("Fussweg wird nicht als Startspur akzeptiert"),
		FWiesbadenCarSpawn::FindNearestDrivableLanePoint(
			FootwayOnly, FVector(1000.0, 100.0, 0.0), 10000.0, SpawnLoc, SpawnRot, LaneId));

	// -- 5. Leeres Netz -----------------------------------------------------
	const FRoadNetwork EmptyNetwork;
	TestFalse(TEXT("Leeres Strassennetz liefert keinen Startpunkt"),
		FWiesbadenCarSpawn::FindNearestDrivableLanePoint(
			EmptyNetwork, FVector::ZeroVector, 10000.0, SpawnLoc, SpawnRot, LaneId));

	// -- 6. Richtige Spur bei mehreren --------------------------------------
	FRoadNetwork TwoLanes = MakeStraightNetwork();

	FRoadSegment Far;
	Far.SegmentId = 1;
	Far.HighwayType = EOSMHighwayType::Residential;
	TwoLanes.Segments.Add(Far);

	FRoadLane FarLane;
	FarLane.LaneId = 1;
	FarLane.SegmentId = 1;
	FarLane.Centerline = {
		FVector(0.0, 5000.0, 0.0),
		FVector(2000.0, 5000.0, 0.0),
	};
	TwoLanes.Lanes.Add(FarLane);

	TestTrue(TEXT("Suche mit zwei Spuren gelingt"),
		FWiesbadenCarSpawn::FindNearestDrivableLanePoint(
			TwoLanes, FVector(1000.0, 200.0, 0.0), 10000.0, SpawnLoc, SpawnRot, LaneId));
	TestEqual(TEXT("Die naehere Spur wird gewaehlt"), LaneId, 0);

	// -- 6b. Hangfall: die naehere Spur liegt hoeher ------------------------
	// Die nahe Spur liegt 113 m ueber dem Zielpunkt, die ferne auf dessen
	// Hoehe. Bei raeumlicher Messung gewaenne die ferne Spur - obwohl sie in
	// der Draufsicht viermal weiter weg ist.
	FRoadNetwork Slope = MakeStraightNetwork();
	Slope.Lanes[0].Centerline = {
		FVector(0.0, 0.0, 11300.0),
		FVector(2000.0, 0.0, 11300.0),
	};

	FRoadSegment Level;
	Level.SegmentId = 1;
	Level.HighwayType = EOSMHighwayType::Residential;
	Slope.Segments.Add(Level);

	FRoadLane LevelLane;
	LevelLane.LaneId = 1;
	LevelLane.SegmentId = 1;
	LevelLane.Centerline = {
		FVector(0.0, 8000.0, 0.0),
		FVector(2000.0, 8000.0, 0.0),
	};
	Slope.Lanes.Add(LevelLane);

	TestTrue(TEXT("Suche am Hang gelingt"),
		FWiesbadenCarSpawn::FindNearestDrivableLanePoint(
			Slope, FVector(1000.0, 2000.0, 0.0), 30000.0, SpawnLoc, SpawnRot, LaneId));
	TestEqual(TEXT("Die horizontal naehere Spur gewinnt trotz Hoehenversatz"), LaneId, 0);
	TestEqual(TEXT("Fahrzeug steht auf Fahrbahnhoehe, nicht auf Zielpunkthoehe"),
		SpawnLoc.Z, 11300.0 + FWiesbadenCarSpawn::SpawnHeightOffsetCm);

	// Startadresse als Konstante hinterlegt - ein vertauschtes Lon/Lat waere
	// sonst erst beim Spielstart aufgefallen (Wiesbaden liegt bei 50 N, 8 O).
	TestTrue(TEXT("Startadresse liegt auf Wiesbadener Breite"),
		FWiesbadenCarSpawn::PlatterStrasse144Latitude > 50.0
		&& FWiesbadenCarSpawn::PlatterStrasse144Latitude < 50.2);
	TestTrue(TEXT("Startadresse liegt auf Wiesbadener Laenge"),
		FWiesbadenCarSpawn::PlatterStrasse144Longitude > 8.1
		&& FWiesbadenCarSpawn::PlatterStrasse144Longitude < 8.4);

	return true;
}
