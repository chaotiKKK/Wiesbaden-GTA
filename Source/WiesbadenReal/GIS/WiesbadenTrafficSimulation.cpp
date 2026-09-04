// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenTrafficSimulation.h"

#include "WiesbadenReal.h"

#include "GIS/WiesbadenTrafficLights.h"

namespace
{
	// Umrechnung km/h -> cm/s.
	constexpr double KmhToCmS = 100000.0 / 3600.0;

	// Schutz vor Endlos-Schleifen bei degenerierten Bahnen (Laenge 0).
	constexpr int32 MaxEdgeTransitionsPerTick = 8;
}

uint32 FWiesbadenTrafficSimulation::Hash2(uint32 A, uint32 B)
{
	uint32 H = 2166136261u;
	const uint8 Bytes[8] = {
		static_cast<uint8>(A), static_cast<uint8>(A >> 8),
		static_cast<uint8>(A >> 16), static_cast<uint8>(A >> 24),
		static_cast<uint8>(B), static_cast<uint8>(B >> 8),
		static_cast<uint8>(B >> 16), static_cast<uint8>(B >> 24) };
	for (const uint8 Byte : Bytes)
	{
		H ^= Byte;
		H *= 16777619u;
	}
	return H;
}

float FWiesbadenTrafficSimulation::HashFraction(uint32 Hash)
{
	return static_cast<float>(Hash % 1000u) / 1000.0f;
}

void FWiesbadenTrafficSimulation::Initialize(const FRoadNetwork& InNetwork,
	const FWiesbadenTrafficSettings& InSettings)
{
	Network = &InNetwork;
	Settings = InSettings;
	Vehicles.Reset();
	SpawnLaneIds.Reset();
	ConnectionLengthCm.Reset();
	LaneSuccessorIndices.Reset();
	LaneNeighbours.Reset();
	VehiclesByLaneCache.Reset();
	SpawnAccumulator = 0.0;
	TotalSpawned = 0;
	TotalRemoved = 0;
	LifetimeVehiclesHeldAtRed = 0;
	LifetimeVehiclesApproachingSignal = 0;
	TotalDistanceCm = 0.0;
	Report = FWiesbadenTrafficReport();

	// Befahrbare Spawn-Spuren (keine Busspuren).
	for (const FRoadLane& Lane : Network->Lanes)
	{
		if (Lane.IsValid() && !Lane.bIsBusLane)
		{
			SpawnLaneIds.Add(Lane.LaneId);
		}
	}

	// Nachbarspuren fuer das Ueberholen.
	//
	// Gruppiert wird nach Abschnitt UND Fahrtrichtung; benachbart sind Spuren,
	// deren Spurindex sich um genau eins unterscheidet. Damit bleibt der
	// Gegenverkehr aussen vor - ein Ueberholen ueber die Gegenfahrbahn ist
	// nicht gemeint.
	{
		TMap<TPair<int32, uint8>, TArray<int32>> LanesBySegmentAndDirection;
		for (const FRoadLane& Lane : Network->Lanes)
		{
			if (!Lane.IsValid())
			{
				continue;
			}
			LanesBySegmentAndDirection
				.FindOrAdd(TPair<int32, uint8>(Lane.SegmentId, static_cast<uint8>(Lane.Direction)))
				.Add(Lane.LaneId);
		}

		int32 NeighbourPairs = 0;
		for (const TPair<TPair<int32, uint8>, TArray<int32>>& Group : LanesBySegmentAndDirection)
		{
			if (Group.Value.Num() < 2)
			{
				continue;
			}
			for (const int32 LaneIdA : Group.Value)
			{
				for (const int32 LaneIdB : Group.Value)
				{
					if (LaneIdA == LaneIdB)
					{
						continue;
					}
					const int32 IndexDelta = FMath::Abs(
						Network->Lanes[LaneIdA].LaneIndexFromLeft
						- Network->Lanes[LaneIdB].LaneIndexFromLeft);
					if (IndexDelta == 1)
					{
						LaneNeighbours.FindOrAdd(LaneIdA).Add(LaneIdB);
						++NeighbourPairs;
					}
				}
			}
		}

		UE_LOG(LogWbTraffic, Log,
			TEXT("Verkehr: %d Spuren, davon %d mit Nachbarspur zum Ueberholen (%d Paare)."),
			Network->Lanes.Num(), LaneNeighbours.Num(), NeighbourPairs);
	}

	// Verbindungslaengen + Nachfolgerlisten (deterministisch, Array-Reihenfolge).
	for (int32 i = 0; i < Network->Connections.Num(); ++i)
	{
		const FLaneConnection& Connection = Network->Connections[i];
		double LengthCm = 0.0;
		for (int32 k = 1; k < Connection.ConnectionPath.Num(); ++k)
		{
			LengthCm += FVector::Dist(Connection.ConnectionPath[k], Connection.ConnectionPath[k - 1]);
		}
		ConnectionLengthCm.Add(i, LengthCm);
		if (!Connection.bRestricted)
		{
			LaneSuccessorIndices.FindOrAdd(Connection.FromLaneId).Add(i);
		}
	}
}

void FWiesbadenTrafficSimulation::PlaceTrafficVehicles(
	const TArray<FTrafficVehicle>& Vehicles,
	const FVector& ObserverLocation,
	double CullRadiusCm,
	int32 PaletteSize,
	TArray<FPlacedTrafficVehicle>& OutPlaced)
{
	OutPlaced.Reset();
	OutPlaced.Reserve(Vehicles.Num());

	const int32 Palette = FMath::Max(PaletteSize, 1);
	const bool bCull = CullRadiusCm > 0.0;
	const double CullSq = CullRadiusCm * CullRadiusCm;

	for (const FTrafficVehicle& Vehicle : Vehicles)
	{
		// 1-km-Culling um den Beobachter (Player/Streaming-Quelle).
		if (bCull && FVector::DistSquared(Vehicle.Location, ObserverLocation) > CullSq)
		{
			continue;
		}

		FPlacedTrafficVehicle Placed;
		Placed.VehicleId = Vehicle.VehicleId;
		Placed.SteerAngleRad = Vehicle.SteerAngleRad;

		// Gezeigt wird die KAROSSERIE, nicht die Sollbahn.
		//
		// Frueher standen hier Vehicle.Location und die Segmentrichtung der
		// Polylinie. Beides ist exakt - und genau deshalb sah der Verkehr aus
		// wie auf Schienen: kein Fahrzeug wich je einen Zentimeter von der
		// Mittellinie ab, und der Gierwinkel sprang an jedem Stuetzpunkt.
		//
		// Vor dem ersten Tick ist die Karosserie noch nicht gesetzt; dann
		// bleibt die Bahn die beste Auskunft.
		const bool bHasBody = Vehicle.bBodyInitialized;
		const FVector ShownLocation = bHasBody ? Vehicle.BodyLocation : Vehicle.Location;

		double YawDeg;
		if (bHasBody)
		{
			YawDeg = FMath::RadiansToDegrees(Vehicle.BodyYawRad);
		}
		else
		{
			const FVector Forward = Vehicle.Forward.GetSafeNormal2D();
			YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
		}

		Placed.Transform = FTransform(FRotator(0.0, YawDeg, 0.0), ShownLocation, FVector::OneVector);

		// Deterministische Farbe: FNV-1a-Hash der Fahrzeug-Id mod Palette.
		uint32 H = 2166136261u;
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			H ^= static_cast<uint8>(static_cast<uint32>(Vehicle.VehicleId) >> Shift);
			H *= 16777619u;
		}
		Placed.ColorIndex = static_cast<int32>(H % static_cast<uint32>(Palette));

		OutPlaced.Add(Placed);
	}
}

void FWiesbadenTrafficSimulation::SetTrafficLightSystem(const FWiesbadenTrafficLightSystem* InTrafficLights)
{
	TrafficLights = InTrafficLights;
}

void FWiesbadenTrafficSimulation::Reset()
{
	Network = nullptr;
	TrafficLights = nullptr;
	Vehicles.Reset();
	SpawnLaneIds.Reset();
	ConnectionLengthCm.Reset();
	LaneSuccessorIndices.Reset();
	LaneNeighbours.Reset();
	VehiclesByLaneCache.Reset();
	SpawnAccumulator = 0.0;
	TotalSpawned = 0;
	TotalRemoved = 0;
	LifetimeVehiclesHeldAtRed = 0;
	LifetimeVehiclesApproachingSignal = 0;
	TotalDistanceCm = 0.0;
	Report = FWiesbadenTrafficReport();
}

double FWiesbadenTrafficSimulation::ComputeDesiredSpeed(const FTrafficVehicle& Vehicle) const
{
	const FRoadLane& Lane = Network->Lanes[Vehicle.LaneId];
	// Individueller Charakter je Fahrzeug: 0.8..1.0 des Basis-Anteils,
	// deterministisch aus Fahrzeug-Id + Seed.
	const float Fraction = Settings.TargetSpeedFraction
		* (0.8f + 0.2f * HashFraction(Hash2(static_cast<uint32>(Vehicle.VehicleId), Settings.RandomSeed)));
	return FMath::Max(Lane.SpeedLimitKmh * KmhToCmS * Fraction, Settings.MinSpeedKmh * KmhToCmS);
}

double FWiesbadenTrafficSimulation::GetEdgeLengthCm(const FTrafficVehicle& Vehicle) const
{
	if (Vehicle.bOnLane)
	{
		return Network->Lanes.IsValidIndex(Vehicle.LaneId) ? Network->Lanes[Vehicle.LaneId].LengthCm : 0.0;
	}
	return ConnectionLengthCm.FindRef(Vehicle.ConnectionIndex);
}

double FWiesbadenTrafficSimulation::GetRoadClassWeight(EOSMHighwayType Type)
{
	// Wie attraktiv eine Strasse fuer den Durchgangsverkehr ist.
	//
	// Ohne diese Gewichtung war die Wahl an jeder Kreuzung GLEICHVERTEILT:
	// eine Wohnstrasse wurde so oft genommen wie die Bundesstrasse daneben.
	// Das Ergebnis war Verkehr, der planlos durch Wohngebiete irrt, waehrend
	// die Hauptachsen leer bleiben - genau umgekehrt zur Wirklichkeit.
	//
	// Die Zahlen sind Verhaeltnisse, keine Messwerte: eine Bundesstrasse wird
	// rund zwoelfmal so oft gewaehlt wie ein Erschliessungsweg.
	switch (Type)
	{
	case EOSMHighwayType::Motorway:
	case EOSMHighwayType::Trunk:			return 16.0;
	case EOSMHighwayType::MotorwayLink:
	case EOSMHighwayType::TrunkLink:		return 10.0;
	case EOSMHighwayType::Primary:			return 12.0;
	case EOSMHighwayType::PrimaryLink:		return 8.0;
	case EOSMHighwayType::Secondary:		return 7.0;
	case EOSMHighwayType::SecondaryLink:	return 5.0;
	case EOSMHighwayType::Tertiary:			return 4.0;
	case EOSMHighwayType::TertiaryLink:		return 3.0;
	case EOSMHighwayType::Unclassified:		return 2.0;
	case EOSMHighwayType::Residential:		return 1.5;
	case EOSMHighwayType::LivingStreet:		return 0.6;
	case EOSMHighwayType::Service:			return 1.0;
	default:								return 1.0;
	}
}

int32 FWiesbadenTrafficSimulation::PickSuccessorConnection(const FTrafficVehicle& Vehicle) const
{
	// Spur-Ende: deterministisch eine erlaubte Verbindung waehlen (FNV-1a-Hash
	// aus Fahrzeug-Id + Kreuzungsknoten) - identisch zu AdvanceEdge.
	const TArray<int32>* Successors = LaneSuccessorIndices.Find(Vehicle.LaneId);
	if (!Successors || Successors->Num() == 0)
	{
		return INDEX_NONE;
	}

	const uint32 NodeId = static_cast<uint32>(
		Network->Connections[(*Successors)[0]].IntersectionNodeId);
	const uint32 Roll = Hash2(static_cast<uint32>(Vehicle.VehicleId), NodeId);

	// Gewichtete Wahl: Hauptstrassen werden bevorzugt.
	//
	// Der Hash bleibt die Quelle des Zufalls, damit die Wahl deterministisch
	// und ueber Laeufe reproduzierbar ist - ein Fahrzeug an derselben
	// Kreuzung entscheidet sich immer gleich.
	double TotalWeight = 0.0;
	for (const int32 ConnectionIndex : *Successors)
	{
		TotalWeight += GetSuccessorWeight(ConnectionIndex);
	}

	if (TotalWeight <= KINDA_SMALL_NUMBER)
	{
		const int32 Pick = static_cast<int32>(Roll % static_cast<uint32>(Successors->Num()));
		return (*Successors)[Pick];
	}

	// Hash auf [0, TotalWeight) abbilden und das Rad drehen.
	const double Target = (static_cast<double>(Roll % 100000u) / 100000.0) * TotalWeight;
	double Running = 0.0;
	for (const int32 ConnectionIndex : *Successors)
	{
		Running += GetSuccessorWeight(ConnectionIndex);
		if (Target < Running)
		{
			return ConnectionIndex;
		}
	}

	return (*Successors)[Successors->Num() - 1];
}

double FWiesbadenTrafficSimulation::GetSuccessorWeight(int32 ConnectionIndex) const
{
	if (!Network || !Network->Connections.IsValidIndex(ConnectionIndex))
	{
		return 1.0;
	}

	const int32 ToLaneId = Network->Connections[ConnectionIndex].ToLaneId;
	if (!Network->Lanes.IsValidIndex(ToLaneId))
	{
		return 1.0;
	}

	const int32 SegmentId = Network->Lanes[ToLaneId].SegmentId;
	if (!Network->Segments.IsValidIndex(SegmentId))
	{
		return 1.0;
	}

	return GetRoadClassWeight(Network->Segments[SegmentId].HighwayType);
}

bool FWiesbadenTrafficSimulation::AdvanceEdge(FTrafficVehicle& Vehicle)
{
	if (Vehicle.bOnLane)
	{
		const int32 Pick = PickSuccessorConnection(Vehicle);
		if (Pick == INDEX_NONE)
		{
			return false; // Sackgasse -> Aufrufer entfernt.
		}
		Vehicle.DistanceCm -= Network->Lanes[Vehicle.LaneId].LengthCm;
		Vehicle.ConnectionIndex = Pick;
		Vehicle.bOnLane = false;
		return true;
	}

	// Verbindungs-Ende: auf die Folgespur wechseln (Wunschgeschwindigkeit
	// wird an das Tempolimit der neuen Spur angepasst).
	const FLaneConnection& Connection = Network->Connections[Vehicle.ConnectionIndex];
	Vehicle.DistanceCm -= ConnectionLengthCm.FindRef(Vehicle.ConnectionIndex);
	if (!Network->Lanes.IsValidIndex(Connection.ToLaneId) || !Network->Lanes[Connection.ToLaneId].IsValid())
	{
		return false;
	}
	Vehicle.LaneId = Connection.ToLaneId;
	Vehicle.bOnLane = true;
	Vehicle.DesiredSpeedCmS = ComputeDesiredSpeed(Vehicle);
	return true;
}

FVector FWiesbadenTrafficSimulation::GetPathPointAhead(const FTrafficVehicle& Vehicle,
	double AheadCm) const
{
	FVector Location = Vehicle.Location;
	FVector Forward = Vehicle.Forward.GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}

	const TArray<FVector>* Path = nullptr;
	if (Network)
	{
		if (Vehicle.bOnLane && Network->Lanes.IsValidIndex(Vehicle.LaneId))
		{
			Path = &Network->Lanes[Vehicle.LaneId].Centerline;
		}
		else if (!Vehicle.bOnLane && Network->Connections.IsValidIndex(Vehicle.ConnectionIndex))
		{
			Path = &Network->Connections[Vehicle.ConnectionIndex].ConnectionPath;
		}
	}

	if (Path == nullptr || Path->Num() < 2)
	{
		return Location + Forward * AheadCm;
	}

	const double EdgeLength = GetEdgeLengthCm(Vehicle);
	const double Target = Vehicle.DistanceCm + AheadCm;
	if (Target <= EdgeLength)
	{
		SamplePolyline(*Path, Target, Location, Forward);
		return Location;
	}

	// Ueber die Bahngrenze hinaus: auf der Folgebahn weitersuchen.
	//
	// Ohne diesen Schritt zielt ein Fahrzeug am Spurende auf das Spurende
	// selbst und beginnt erst einzulenken, wenn es bereits in der Kreuzung
	// steht - jede Abbiegung wuerde angeschnitten.
	const double Rest = Target - EdgeLength;
	const TArray<FVector>* NextPath = nullptr;
	if (Network)
	{
		if (Vehicle.bOnLane)
		{
			const int32 NextConnection = PickSuccessorConnection(Vehicle);
			if (Network->Connections.IsValidIndex(NextConnection))
			{
				NextPath = &Network->Connections[NextConnection].ConnectionPath;
			}
		}
		else
		{
			const int32 ToLaneId = Network->Connections[Vehicle.ConnectionIndex].ToLaneId;
			if (Network->Lanes.IsValidIndex(ToLaneId))
			{
				NextPath = &Network->Lanes[ToLaneId].Centerline;
			}
		}
	}

	if (NextPath == nullptr || NextPath->Num() < 2)
	{
		// Sackgasse: die letzte bekannte Richtung gerade verlaengern.
		//
		// SamplePolyline liefert am Bahnende FVector::ForwardVector als
		// Richtung - ein Platzhalter, der hier nach Norden zeigen wuerde.
		// Deshalb wird kurz VOR dem Ende abgetastet und der Rest addiert.
		constexpr double BackOffCm = 10.0;
		SamplePolyline(*Path, FMath::Max(0.0, EdgeLength - BackOffCm), Location, Forward);
		return Location + Forward.GetSafeNormal2D() * (Rest + BackOffCm);
	}

	SamplePolyline(*NextPath, Rest, Location, Forward);
	return Location;
}

void FWiesbadenTrafficSimulation::StepBicycleModel(
	const FVector2D& TargetXY,
	double SpeedCmS,
	double WheelbaseCm,
	double MaxSteerRad,
	double MaxSteerRateRadS,
	double Dt,
	FVector2D& InOutBodyXY,
	float& InOutYawRad,
	float& InOutSteerRad)
{
	if (Dt <= 0.0)
	{
		return;
	}

	const double Wheelbase = FMath::Max(WheelbaseCm, 1.0);

	// Reine Verfolgung: aus dem Zielpunkt folgt der noetige Einschlag.
	const FVector2D ToTarget = TargetXY - InOutBodyXY;
	const double Distance = ToTarget.Size();

	double DesiredSteer = 0.0;
	if (Distance > 1.0)
	{
		const double TargetAngle = FMath::Atan2(ToTarget.Y, ToTarget.X);
		const double Alpha = FMath::UnwindRadians(TargetAngle - static_cast<double>(InOutYawRad));
		DesiredSteer = FMath::Atan2(2.0 * Wheelbase * FMath::Sin(Alpha), Distance);
	}

	// Anschlag: DAS ist der Wendekreis.
	DesiredSteer = FMath::Clamp(DesiredSteer, -MaxSteerRad, MaxSteerRad);

	// Das Lenkrad dreht mit endlicher Geschwindigkeit. Ohne diese Grenze
	// springt das Rad in einem einzigen Tick von Anschlag zu Anschlag, und
	// der Wagen knickt sichtbar ab, statt einzulenken.
	const double MaxChange = FMath::Max(MaxSteerRateRadS, 0.0) * Dt;
	const double Change = FMath::Clamp(DesiredSteer - static_cast<double>(InOutSteerRad),
		-MaxChange, MaxChange);
	InOutSteerRad = static_cast<float>(static_cast<double>(InOutSteerRad) + Change);

	// Gierrate des Einspurmodells: omega = v / L * tan(delta).
	const double YawRate = (SpeedCmS / Wheelbase) * FMath::Tan(static_cast<double>(InOutSteerRad));
	InOutYawRad = static_cast<float>(FMath::UnwindRadians(
		static_cast<double>(InOutYawRad) + YawRate * Dt));

	const double Step = SpeedCmS * Dt;
	InOutBodyXY.X += Step * FMath::Cos(static_cast<double>(InOutYawRad));
	InOutBodyXY.Y += Step * FMath::Sin(static_cast<double>(InOutYawRad));
}

void FWiesbadenTrafficSimulation::UpdateBodyPose(FTrafficVehicle& Vehicle, double Dt) const
{
	// Beim Einsetzen steht die Karosserie exakt auf der Bahn - erst ab dem
	// naechsten Tick darf sie abweichen.
	if (!Vehicle.bBodyInitialized)
	{
		FVector Forward = Vehicle.Forward.GetSafeNormal2D();
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::ForwardVector;
		}
		Vehicle.BodyLocation = Vehicle.Location;
		Vehicle.BodyYawRad = static_cast<float>(FMath::Atan2(Forward.Y, Forward.X));
		Vehicle.SteerAngleRad = 0.0f;
		Vehicle.bBodyInitialized = true;
		return;
	}

	const double Lookahead = Settings.LookaheadBaseCm
		+ FMath::Max(Vehicle.SpeedCmS, 0.0) * Settings.LookaheadSeconds;
	const FVector Target = GetPathPointAhead(Vehicle, Lookahead);

	FVector2D BodyXY(Vehicle.BodyLocation.X, Vehicle.BodyLocation.Y);
	StepBicycleModel(
		FVector2D(Target.X, Target.Y),
		Vehicle.SpeedCmS,
		Settings.WheelbaseCm,
		FMath::DegreesToRadians(Settings.MaxSteerAngleDeg),
		FMath::DegreesToRadians(Settings.MaxSteerRateDegS),
		Dt,
		BodyXY,
		Vehicle.BodyYawRad,
		Vehicle.SteerAngleRad);

	// Sicherheitsnetz gegen davonlaufenden Fehler.
	//
	// Die Verfolgung holt seitlichen Versatz von allein ein - auch den eines
	// Spurwechsels, und genau daraus entsteht das erwuenschte allmaehliche
	// Herueberziehen. Reisst der Abstand aber auf (Bahnwechsel, kaputtes
	// Netz), landet das Auto im Gruenen. Dann wird der Ueberschuss WEICH
	// abgebaut: ein harter Schnitt auf die Grenze saehe aus wie ein Teleport.
	const FVector2D PathXY(Vehicle.Location.X, Vehicle.Location.Y);
	const double Deviation = FVector2D::Distance(BodyXY, PathXY);
	const double MaxDeviation = FMath::Max(Settings.MaxBodyDeviationCm, 1.0);
	if (Deviation > MaxDeviation)
	{
		const double Excess = Deviation - MaxDeviation;
		const double Recover = Excess * FMath::Min(1.0, 3.0 * Dt);
		BodyXY += (PathXY - BodyXY).GetSafeNormal() * Recover;
	}

	Vehicle.BodyLocation.X = BodyXY.X;
	Vehicle.BodyLocation.Y = BodyXY.Y;

	// Die Hoehe kommt weiter aus der Bahn: die Strasse steigt und faellt, und
	// ein mitintegriertes Z wuerde durch den Belag sinken.
	Vehicle.BodyLocation.Z = Vehicle.Location.Z;
}

void FWiesbadenTrafficSimulation::SamplePolyline(const TArray<FVector>& Polyline, double DistanceCm,
	FVector& OutLocation, FVector& OutForward)
{
	if (Polyline.Num() == 0)
	{
		OutLocation = FVector::ZeroVector;
		OutForward = FVector::ForwardVector;
		return;
	}
	if (Polyline.Num() == 1)
	{
		OutLocation = Polyline[0];
		OutForward = FVector::ForwardVector;
		return;
	}

	double Accumulated = 0.0;
	for (int32 i = 1; i < Polyline.Num(); ++i)
	{
		const FVector Segment = Polyline[i] - Polyline[i - 1];
		const double SegmentLength = Segment.Size();
		if (Accumulated + SegmentLength >= DistanceCm || i == Polyline.Num() - 1)
		{
			const double T = SegmentLength > 0.0 ? (DistanceCm - Accumulated) / SegmentLength : 0.0;
			OutLocation = FMath::Lerp(Polyline[i - 1], Polyline[i], T);
			OutForward = SegmentLength > 0.0 ? Segment.GetSafeNormal() : FVector::ForwardVector;
			return;
		}
		Accumulated += SegmentLength;
	}
	OutLocation = Polyline.Last();
	OutForward = FVector::ForwardVector;
}

void FWiesbadenTrafficSimulation::SelectSpawnLanesNear(
	const TArray<FRoadLane>& Lanes,
	const TArray<int32>& Candidates,
	const FVector& Center,
	double RadiusCm,
	TArray<int32>& OutLaneIds)
{
	OutLaneIds.Reset();

	if (RadiusCm <= 0.0)
	{
		OutLaneIds = Candidates;
		return;
	}

	const double RadiusSq = RadiusCm * RadiusCm;

	for (const int32 LaneId : Candidates)
	{
		if (!Lanes.IsValidIndex(LaneId))
		{
			continue;
		}

		const FRoadLane& Lane = Lanes[LaneId];
		if (Lane.Centerline.Num() == 0)
		{
			continue;
		}

		// Gemessen wird am Spuranfang - dort setzen die Fahrzeuge ein.
		// Horizontal, weil die Stadt ueber 100 m Hoehenunterschied hat und
		// eine 3D-Messung am Hang Spuren ausschliessen wuerde, die in der
		// Draufsicht direkt nebenan liegen.
		const FVector& Start = Lane.Centerline[0];
		const double Dx = Start.X - Center.X;
		const double Dy = Start.Y - Center.Y;

		if ((Dx * Dx + Dy * Dy) <= RadiusSq)
		{
			OutLaneIds.Add(LaneId);
		}
	}
}

void FWiesbadenTrafficSimulation::SetObserverLocation(const FVector& InLocation)
{
	ObserverLocation = InLocation;
	bHasObserver = true;

	// Die Umkreissuche laeuft ueber alle Spuren des Netzes - bei 111.000
	// Spuren je Frame zu teuer. Sie wird daher nur erneuert, wenn sich der
	// Beobachter um mehr als ein Viertel des Spawn-Radius bewegt hat.
	const double RefreshDistanceCm = FMath::Max(50.0, Settings.SpawnRadiusMeters * 100.0 * 0.25);

	const double Dx = InLocation.X - LastSpawnSearchLocation.X;
	const double Dy = InLocation.Y - LastSpawnSearchLocation.Y;
	const bool bMovedFar = (Dx * Dx + Dy * Dy) > (RefreshDistanceCm * RefreshDistanceCm);

	if (Network && (!bNearbyLanesValid || bMovedFar))
	{
		SelectSpawnLanesNear(
			Network->Lanes, SpawnLaneIds, InLocation,
			Settings.SpawnRadiusMeters * 100.0, NearbySpawnLaneIds);

		LastSpawnSearchLocation = InLocation;
		bNearbyLanesValid = true;
	}
}

void FWiesbadenTrafficSimulation::SetPlayerObstacle(const FVector& Location, double HalfLengthCm)
{
	PlayerObstacleLocation = Location;
	PlayerObstacleHalfLengthCm = FMath::Max(0.0, HalfLengthCm);
	bHasPlayerObstacle = true;
}

void FWiesbadenTrafficSimulation::ClearPlayerObstacle()
{
	bHasPlayerObstacle = false;
}

double FWiesbadenTrafficSimulation::ComputeObstacleAwareSpeed(
	double CurrentSpeedCmS,
	const FVector& VehicleLocation,
	const FVector& VehicleForward,
	const FVector& ObstacleLocation,
	double ObstacleHalfLengthCm,
	double CorridorHalfWidthCm,
	double ReactionDistanceCm,
	double MinGapCm,
	double DecelerationCmS2)
{
	if (DecelerationCmS2 <= 0.0)
	{
		return CurrentSpeedCmS;
	}

	// Horizontal rechnen: bei ueber 100 m Hoehenunterschied in der Stadt
	// wuerde eine raeumliche Messung ein Fahrzeug auf der Bruecke darueber
	// faelschlich als Hindernis behandeln.
	const FVector2D Forward2D = FVector2D(VehicleForward.X, VehicleForward.Y).GetSafeNormal();
	if (Forward2D.IsNearlyZero())
	{
		return CurrentSpeedCmS;
	}

	const FVector2D ToObstacle(
		ObstacleLocation.X - VehicleLocation.X,
		ObstacleLocation.Y - VehicleLocation.Y);

	// Laengs- und Querabstand im Fahrzeug-Koordinatensystem.
	const double Along = FVector2D::DotProduct(ToObstacle, Forward2D);
	const FVector2D Right2D(Forward2D.Y, -Forward2D.X);
	const double Side = FMath::Abs(FVector2D::DotProduct(ToObstacle, Right2D));

	// Nur reagieren, wenn das Hindernis VORAUS und im eigenen Fahrschlauch
	// liegt. Ohne die Querpruefung wuerde der Gegenverkehr bremsen, sobald der
	// Spieler ihm entgegenkommt.
	if (Along <= 0.0 || Along > ReactionDistanceCm || Side > CorridorHalfWidthCm)
	{
		return CurrentSpeedCmS;
	}

	// Freier Weg bis zum Hindernis, abzueglich seiner halben Laenge und des
	// Mindestabstands.
	const double FreeDistance = Along - ObstacleHalfLengthCm - MinGapCm;

	if (FreeDistance <= 0.0)
	{
		return 0.0;
	}

	// Bremswegmodell: v = sqrt(2 * a * s) ist die hoechste Geschwindigkeit,
	// aus der das Fahrzeug mit der Verzoegerung a noch auf der Strecke s zum
	// Stehen kommt.
	//
	// NICHT die Formel aus der Folgeabstand-Berechnung ((Gap-MinGap)/Dt):
	// die gilt fuer einen fahrenden Vordermann und beruecksichtigt nur den
	// naechsten Zeitschritt. Bei einem STEHENDEN Hindernis und 60 Hz ergaebe
	// sie fuer 10 m Abstand ueber 5.500 cm/s - also gar keine Bremsung. Der
	// Verkehr wuerde bis zum letzten Frame voll draufhalten und dann
	// schlagartig stehen.
	const double MaxSpeed = FMath::Sqrt(2.0 * DecelerationCmS2 * FreeDistance);

	return FMath::Min(CurrentSpeedCmS, MaxSpeed);
}

double FWiesbadenTrafficSimulation::ComputeGapAheadOnLane(
	int32 LaneId, double AtDistanceCm, int32 IgnoreVehicleId) const
{
	double Best = TNumericLimits<double>::Max();
	const TArray<int32>* Bucket = VehiclesByLaneCache.Find(LaneId);
	if (!Bucket)
	{
		return Best;
	}
	for (const int32 Index : *Bucket)
	{
		const FTrafficVehicle& Other = Vehicles[Index];
		if (Other.VehicleId == IgnoreVehicleId)
		{
			continue;
		}
		const double Ahead = Other.DistanceCm - AtDistanceCm;
		if (Ahead >= 0.0)
		{
			Best = FMath::Min(Best, Ahead);
		}
	}
	return Best;
}

double FWiesbadenTrafficSimulation::ComputeGapBehindOnLane(
	int32 LaneId, double AtDistanceCm, int32 IgnoreVehicleId) const
{
	double Best = TNumericLimits<double>::Max();
	const TArray<int32>* Bucket = VehiclesByLaneCache.Find(LaneId);
	if (!Bucket)
	{
		return Best;
	}
	for (const int32 Index : *Bucket)
	{
		const FTrafficVehicle& Other = Vehicles[Index];
		if (Other.VehicleId == IgnoreVehicleId)
		{
			continue;
		}
		const double Behind = AtDistanceCm - Other.DistanceCm;
		if (Behind >= 0.0)
		{
			Best = FMath::Min(Best, Behind);
		}
	}
	return Best;
}

void FWiesbadenTrafficSimulation::ApplyCrossEdgeHeadway(double Dt)
{
	if (!Network || Dt <= 0.0)
	{
		return;
	}

	// Hinterstes Fahrzeug je Bahn. Nur dieses kann einem Fahrzeug, das von der
	// vorherigen Bahn nachrueckt, im Weg stehen.
	TMap<int32, double> RearmostOnLane;
	TMap<int32, double> RearmostOnConnection;
	TMap<int32, double> RearmostSpeedOnLane;
	TMap<int32, double> RearmostSpeedOnConnection;

	for (const FTrafficVehicle& Vehicle : Vehicles)
	{
		TMap<int32, double>& Distances = Vehicle.bOnLane ? RearmostOnLane : RearmostOnConnection;
		TMap<int32, double>& Speeds = Vehicle.bOnLane ? RearmostSpeedOnLane : RearmostSpeedOnConnection;
		const int32 Key = Vehicle.bOnLane ? Vehicle.LaneId : Vehicle.ConnectionIndex;

		const double* Existing = Distances.Find(Key);
		if (!Existing || Vehicle.DistanceCm < *Existing)
		{
			Distances.Add(Key, Vehicle.DistanceCm);
			Speeds.Add(Key, Vehicle.SpeedCmS);
		}
	}

	for (FTrafficVehicle& Vehicle : Vehicles)
	{
		const double EdgeLength = GetEdgeLengthCm(Vehicle);
		if (EdgeLength <= 0.0)
		{
			continue;
		}

		// Nur wer nah genug am Bahnende ist, muss ueber die Grenze schauen.
		// Massstab ist die Mindestluecke plus der eigene Bremsweg.
		const double ToEdgeEnd = EdgeLength - Vehicle.DistanceCm;
		const double LookAhead = Settings.MinGapCm
			+ Vehicle.SpeedCmS * Vehicle.SpeedCmS / (2.0 * FMath::Max(Settings.MaxDecelerationCmS2, 1.0));
		if (ToEdgeEnd > LookAhead)
		{
			continue;
		}

		double RearDistance = 0.0;
		double RearSpeed = 0.0;
		bool bFound = false;

		if (Vehicle.bOnLane)
		{
			const int32 NextConnection = PickSuccessorConnection(Vehicle);
			const double* Rear = (NextConnection != INDEX_NONE)
				? RearmostOnConnection.Find(NextConnection) : nullptr;
			if (Rear)
			{
				RearDistance = *Rear;
				RearSpeed = RearmostSpeedOnConnection.FindRef(NextConnection);
				bFound = true;
			}
		}
		else if (Network->Connections.IsValidIndex(Vehicle.ConnectionIndex))
		{
			const int32 NextLane = Network->Connections[Vehicle.ConnectionIndex].ToLaneId;
			if (const double* Rear = RearmostOnLane.Find(NextLane))
			{
				RearDistance = *Rear;
				RearSpeed = RearmostSpeedOnLane.FindRef(NextLane);
				bFound = true;
			}
		}

		if (!bFound)
		{
			continue;
		}

		// Dieselbe Regel wie innerhalb einer Bahn, nur ueber die Bahngrenze
		// hinweg gerechnet: Der Folger darf hoechstens so schnell fahren, dass
		// die Mindestluecke im naechsten Schritt erhalten bleibt.
		const double Gap = ToEdgeEnd + RearDistance;
		const double SafeSpeed = RearSpeed + (Gap - Settings.MinGapCm) / Dt;
		Vehicle.SpeedCmS = FMath::Min(Vehicle.SpeedCmS, FMath::Max(0.0, SafeSpeed));
	}
}

void FWiesbadenTrafficSimulation::ApplyLaneChanges(float DeltaSeconds)
{
	Report.LaneChangesThisTick = 0;

	if (!Network || !Settings.bAllowLaneChange)
	{
		return;
	}

	for (int32 VehicleIndex = 0; VehicleIndex < Vehicles.Num(); ++VehicleIndex)
	{
		FTrafficVehicle& Vehicle = Vehicles[VehicleIndex];
		Vehicle.LaneChangeCooldown = FMath::Max(0.0f, Vehicle.LaneChangeCooldown - DeltaSeconds);

		if (!Vehicle.bOnLane || Vehicle.LaneChangeCooldown > 0.0f)
		{
			continue;
		}

		// Nur wer tatsaechlich behindert wird, wechselt. Wer seine
		// Wunschgeschwindigkeit faehrt, hat keinen Grund dazu - sonst entstuende
		// ein staendiges Hin und Her ohne Nutzen.
		if (Vehicle.DesiredSpeedCmS <= 0.0
			|| Vehicle.SpeedCmS > Vehicle.DesiredSpeedCmS * Settings.LaneChangeSpeedDeficit)
		{
			continue;
		}

		const TArray<int32>* Neighbours = LaneNeighbours.Find(Vehicle.LaneId);
		if (!Neighbours || !Network->Lanes.IsValidIndex(Vehicle.LaneId))
		{
			continue;
		}

		const double CurrentLength = Network->Lanes[Vehicle.LaneId].LengthCm;
		if (CurrentLength <= 0.0)
		{
			continue;
		}
		const double Fraction = FMath::Clamp(Vehicle.DistanceCm / CurrentLength, 0.0, 1.0);

		const double CurrentGap = ComputeGapAheadOnLane(
			Vehicle.LaneId, Vehicle.DistanceCm, Vehicle.VehicleId);

		int32 BestLane = INDEX_NONE;
		double BestGap = CurrentGap;
		double BestDistance = 0.0;

		for (const int32 NeighbourLaneId : *Neighbours)
		{
			if (!Network->Lanes.IsValidIndex(NeighbourLaneId))
			{
				continue;
			}
			const FRoadLane& Neighbour = Network->Lanes[NeighbourLaneId];
			if (Neighbour.bIsBusLane || Neighbour.LengthCm <= 0.0)
			{
				continue;
			}

			// Parallele Spuren desselben Abschnitts sind etwa gleich lang; der
			// Laengenanteil ist deshalb die richtige Uebertragung.
			const double NeighbourDistance = Fraction * Neighbour.LengthCm;

			const double GapAhead = ComputeGapAheadOnLane(
				NeighbourLaneId, NeighbourDistance, Vehicle.VehicleId);
			const double GapBehind = ComputeGapBehindOnLane(
				NeighbourLaneId, NeighbourDistance, Vehicle.VehicleId);

			// Nach hinten zaehlt genauso wie nach vorn: Wer vor einen
			// schnelleren Nachfolger zieht, loest dort dieselbe Bremswelle aus,
			// der er selbst entkommen wollte.
			if (GapAhead < Settings.LaneChangeMinGapCm || GapBehind < Settings.LaneChangeMinGapCm)
			{
				continue;
			}

			// Nur wechseln, wenn es dort spuerbar freier ist - ohne diesen
			// Abstand wechselte ein Fahrzeug auch fuer wenige Zentimeter.
			if (GapAhead > BestGap + Settings.MinGapCm)
			{
				BestGap = GapAhead;
				BestLane = NeighbourLaneId;
				BestDistance = NeighbourDistance;
			}
		}

		if (BestLane == INDEX_NONE)
		{
			continue;
		}

		// Belegungsliste MITFUEHREN.
		//
		// Ohne diese Buchung bliebe der Wechsel fuer alle folgenden Fahrzeuge
		// dieses Ticks unsichtbar: Zwei Fahrzeuge derselben Kolonne wuerden
		// beide dieselbe Luecke als frei ansehen und gleichzeitig
		// hineinziehen. Die Luecken-Suche liest Vehicles[Index] live, deshalb
		// genuegt das Umhaengen des Index.
		if (TArray<int32>* OldBucket = VehiclesByLaneCache.Find(Vehicle.LaneId))
		{
			OldBucket->Remove(VehicleIndex);
		}
		VehiclesByLaneCache.FindOrAdd(BestLane).Add(VehicleIndex);

		Vehicle.LaneId = BestLane;
		Vehicle.DistanceCm = BestDistance;
		Vehicle.DesiredSpeedCmS = ComputeDesiredSpeed(Vehicle);
		Vehicle.LaneChangeCooldown = Settings.LaneChangeCooldownSeconds;
		++Report.LaneChangesThisTick;
	}
}

void FWiesbadenTrafficSimulation::Tick(float DeltaSeconds)
{
	if (!Network || DeltaSeconds <= 0.0f)
	{
		return;
	}
	const double Dt = DeltaSeconds;
	const double MinGap = Settings.MinGapCm;

	// Geschwindigkeit vor diesem Tick sichern. Sie ist der Bezugspunkt fuer die
	// Beschleunigungsgrenze am Ende - ohne sie liesse sich nicht sagen, um
	// wieviel sich die Geschwindigkeit in diesem Schritt aendern darf.
	TArray<double> PreviousSpeedCmS;
	PreviousSpeedCmS.Reserve(Vehicles.Num());
	for (const FTrafficVehicle& Vehicle : Vehicles)
	{
		PreviousSpeedCmS.Add(Vehicle.SpeedCmS);
	}

	// -- 1) Kopf-zu-Schwanz: Sollgeschwindigkeit je Fahrzeug ------------------
	TMap<int32, TArray<int32>> VehiclesByLane;
	TMap<int32, TArray<int32>> VehiclesByConnection;
	for (int32 i = 0; i < Vehicles.Num(); ++i)
	{
		const FTrafficVehicle& Vehicle = Vehicles[i];
		if (Vehicle.bOnLane)
		{
			VehiclesByLane.FindOrAdd(Vehicle.LaneId).Add(i);
		}
		else
		{
			VehiclesByConnection.FindOrAdd(Vehicle.ConnectionIndex).Add(i);
		}
	}

	const auto ApplyHeadway = [this, &MinGap, &Dt](TArray<int32>& Bucket)
	{
		// Absteigend nach Distanz (Vordermann zuerst); bei Gleichstand nach
		// Fahrzeug-Id - totale Ordnung, damit der Sort deterministisch ist.
		Bucket.Sort([this](int32 A, int32 B)
		{
			const FTrafficVehicle& VA = Vehicles[A];
			const FTrafficVehicle& VB = Vehicles[B];
			if (VA.DistanceCm != VB.DistanceCm)
			{
				return VA.DistanceCm > VB.DistanceCm;
			}
			return VA.VehicleId < VB.VehicleId;
		});
		for (int32 k = 0; k < Bucket.Num(); ++k)
		{
			FTrafficVehicle& Vehicle = Vehicles[Bucket[k]];
			double Speed = Vehicle.DesiredSpeedCmS;
			if (k > 0)
			{
				const FTrafficVehicle& Leader = Vehicles[Bucket[k - 1]];
				const double Gap = Leader.DistanceCm - Vehicle.DistanceCm;
				const double SafeSpeed = Leader.SpeedCmS + (Gap - MinGap) / Dt;
				Speed = FMath::Min(Speed, FMath::Max(0.0, SafeSpeed));
			}
			Vehicle.SpeedCmS = Speed;
		}
	};
	for (TPair<int32, TArray<int32>>& Pair : VehiclesByLane)
	{
		ApplyHeadway(Pair.Value);
	}
	for (TPair<int32, TArray<int32>>& Pair : VehiclesByConnection)
	{
		ApplyHeadway(Pair.Value);
	}

	// Sortierte Spur-Belegung fuer die Luecken-Suche des Spurwechsels.
	VehiclesByLaneCache = VehiclesByLane;

	// -- 1a) Abstand ueber die Bahngrenze hinweg -------------------------------
	//
	// Muss NACH der Kopf-zu-Schwanz-Regel laufen: Sie liefert die
	// Geschwindigkeit des hintersten Fahrzeugs auf der Folgebahn, auf die hier
	// Bezug genommen wird.
	ApplyCrossEdgeHeadway(Dt);

	// -- 1b) Ampel-Stopp-Regel --------------------------------------------------
	// Fahrzeug auf einer Spur, das sich der Kreuzung naehert (innerhalb
	// StopDistanceCm der Haltelinie) und dessen deterministisch gewaehlte
	// Verbindung rot ist: anhalten. Bei Gruen/ohne Ampel bleibt die
	// Geschwindigkeit aus der Kopf-zu-Schwanz-Berechnung.
	LastVehiclesHeldAtRed = 0;

	if (TrafficLights && Network)
	{
		const double StopDistance = FMath::Max(Settings.MinGapCm * 0.5, 100.0);
		for (FTrafficVehicle& Vehicle : Vehicles)
		{
			// Fahrzeug quert gerade den Knoten (auf einer Verbindung) oder hat keine
			// gueltige Spur -> es faehrt keine Haltelinie an. Zustand zuruecksetzen,
			// damit ein spaeteres Anfahren wieder als NEUES Ereignis zaehlt.
			if (!Vehicle.bOnLane || !Network->Lanes.IsValidIndex(Vehicle.LaneId))
			{
				Vehicle.bWasApproachingSignal = false;
				Vehicle.bWasHeldAtRed = false;
				continue;
			}
			const double LaneLength = Network->Lanes[Vehicle.LaneId].LengthCm;
			if (LaneLength <= 0.0 || Vehicle.DistanceCm < LaneLength - StopDistance)
			{
				// Noch nicht im Anfahr-Fenster der Haltelinie.
				Vehicle.bWasApproachingSignal = false;
				Vehicle.bWasHeldAtRed = false;
				continue;
			}
			const int32 NextConnection = PickSuccessorConnection(Vehicle);

			// Faehrt das Fahrzeug ueberhaupt auf eine SIGNALISIERTE Verbindung zu?
			// Nur dann kann die Kopplung wirken. Ohne diese Kennzahl liesse sich
			// "0 an Rot gehalten" nicht von "keine Ampel auf der Fahrspur" trennen -
			// nur ~1073 der ~20213 Kreuzungen sind Ampeln, der Verkehr quert meist
			// ampellose Knoten (das ist KEIN Kopplungsfehler).
			const bool bApproachingSignal =
				(NextConnection != INDEX_NONE) && TrafficLights->IsConnectionControlled(NextConnection);
			const bool bHeldAtRed =
				bApproachingSignal && !TrafficLights->IsConnectionGreen(NextConnection);

			// LIFETIME-Zaehler (speisen das Diagnose-Verdikt): DISTINKTE Ereignisse,
			// nur die FALSE->TRUE-Flanke - sonst zaehlt ein einziges wartendes
			// Fahrzeug hunderte "Anfahrten" und die Kennzahl luegt.
			if (bApproachingSignal && !Vehicle.bWasApproachingSignal)
			{
				++LifetimeVehiclesApproachingSignal;
			}

			// Die Stopp-Regel wirkt weiterhin JEDEN Tick (Verhalten unveraendert).
			if (bHeldAtRed)
			{
				Vehicle.SpeedCmS = 0.0;
				// Live-Kennzahl: aktuell gehaltene Fahrzeuge DIESEN Tick (fuer die
				// Momentan-Anzeige, GetVehiclesHeldAtRed).
				++LastVehiclesHeldAtRed;
				// Lifetime: nur das Einsetzen des Haltens (distinktes Ereignis).
				if (!Vehicle.bWasHeldAtRed)
				{
					++LifetimeVehiclesHeldAtRed;
				}
			}

			Vehicle.bWasApproachingSignal = bApproachingSignal;
			Vehicle.bWasHeldAtRed = bHeldAtRed;
		}
	}

	// -- 1c) Ruecksicht auf das Spielerfahrzeug ---------------------------------
	//
	// Bewusst NACH der Ampelregel: bremsen fuer den Spieler muss in jedem Fall
	// greifen, auch bei Gruen. Ohne diesen Durchgang faehrt der Verkehr stur
	// weiter und rammt den Spieler von hinten - mit Kollisionskoerpern wuerde
	// er ihn sogar vor sich herschieben.
	if (bHasPlayerObstacle)
	{
		// Fahrschlauch etwas schmaler als eine Fahrspur: der Gegenverkehr auf
		// der Nachbarspur soll nicht mitbremsen.
		const double CorridorHalfWidth = 160.0;

		// Reaktionsweg: bei 50 km/h (1389 cm/s) reichen 25 m fuer ein
		// komfortables Bremsmanoever.
		const double ReactionDistance = 2500.0;

		// Komfortable Verzoegerung: 4 m/s^2. Deutlich spuerbar, aber kein
		// Notbremsen - der Verkehr soll fuer den Spieler abbremsen, nicht
		// vor ihm zum Stehen springen.
		const double ComfortDeceleration = 400.0;

		for (FTrafficVehicle& Vehicle : Vehicles)
		{
			Vehicle.SpeedCmS = ComputeObstacleAwareSpeed(
				Vehicle.SpeedCmS,
				Vehicle.Location,
				Vehicle.Forward,
				PlayerObstacleLocation,
				PlayerObstacleHalfLengthCm,
				CorridorHalfWidth,
				ReactionDistance,
				Settings.MinGapCm,
				ComfortDeceleration);
		}
	}

	// -- 1d) Ueberholen ---------------------------------------------------------
	//
	// Nach allen Geschwindigkeits-Regeln: Erst dort steht fest, WELCHE
	// Fahrzeuge tatsaechlich behindert werden - nur die wechseln die Spur.
	ApplyLaneChanges(DeltaSeconds);

	// -- 1e) Beschleunigungsgrenze ---------------------------------------------
	//
	// Bis hierhin ist SpeedCmS ein Zielwert. Die Simulation hat ihn frueher
	// unmittelbar uebernommen - ein Fahrzeug sprang damit in einem Tick
	// zwischen Stillstand und Vollgas. Aus dieser Sprunghaftigkeit entstanden
	// Bremswellen, die rueckwaerts durch die Kolonne liefen und wie ein Stau
	// aus dem Nichts aussahen.
	//
	// Begrenzt wird NUR das Beschleunigen, nicht das Bremsen.
	//
	// Das ist kein Versehen. Die Abstandsregel loest exakt auf: Sie setzt genau
	// die Geschwindigkeit, bei der die Mindestluecke im naechsten Schritt noch
	// eingehalten ist. Wuerde das Bremsen gedeckelt, koennte ein Fahrzeug diese
	// Geschwindigkeit nicht erreichen und fuehre auf seinen Vordermann auf -
	// also genau das ineinander Stecken, das hier abgestellt werden soll.
	// Gemessen verlangt schon der Kopf-zu-Schwanz-Test 1100 cm/s^2.
	//
	// Die sichtbare Sprunghaftigkeit kam ohnehin vom Anfahren: Ein Fahrzeug,
	// dessen Vordermann weiterrollte, sprang in einem Tick von 0 auf
	// Wunschgeschwindigkeit. Genau das ist jetzt begrenzt.
	{
		const double MaxSpeedUp = Settings.MaxAccelerationCmS2 * Dt;

		for (int32 i = 0; i < Vehicles.Num(); ++i)
		{
			if (!PreviousSpeedCmS.IsValidIndex(i))
			{
				continue;
			}
			const double Ceiling = PreviousSpeedCmS[i] + MaxSpeedUp;
			Vehicles[i].SpeedCmS = FMath::Max(0.0, FMath::Min(Vehicles[i].SpeedCmS, Ceiling));
		}
	}

	// -- 2) Vorsprung ----------------------------------------------------------
	double DistanceThisTick = 0.0;
	for (FTrafficVehicle& Vehicle : Vehicles)
	{
		Vehicle.DistanceCm += Vehicle.SpeedCmS * Dt;
		DistanceThisTick += Vehicle.SpeedCmS * Dt;
	}
	TotalDistanceCm += DistanceThisTick;

	// -- 3) Bahnwechsel (Spur -> Verbindung -> Folgespur; Sackgasse -> raus) ---
	for (FTrafficVehicle& Vehicle : Vehicles)
	{
		int32 Guard = 0;
		while (true)
		{
			const double EdgeLength = GetEdgeLengthCm(Vehicle);
			if (EdgeLength <= 0.0)
			{
				Vehicle.bRemoved = true;
				break;
			}
			if (Vehicle.DistanceCm < EdgeLength)
			{
				break;
			}
			if (!AdvanceEdge(Vehicle))
			{
				Vehicle.bRemoved = true;
				break;
			}
			if (++Guard > MaxEdgeTransitionsPerTick)
			{
				Vehicle.bRemoved = true;
				break;
			}
		}
	}
	// Fahrzeuge ausserhalb des Entfernungsradius abraeumen. Ohne das wandern
	// sie beim Fahren durch die Stadt endlos mit und belegen das Budget, das
	// in Sichtweite gebraucht wird.
	if (bHasObserver && Settings.DespawnRadiusMeters > 0.0)
	{
		const double DespawnRadiusCmSq =
			FMath::Square(Settings.DespawnRadiusMeters * 100.0);

		Vehicles.RemoveAll([this, DespawnRadiusCmSq](const FTrafficVehicle& Vehicle)
		{
			// Nur horizontal messen: die Stadt hat ueber 100 m Hoehenunterschied,
			// eine 3D-Messung wuerde am Hang Fahrzeuge entfernen, die in der
			// Draufsicht direkt neben dem Spieler stehen.
			const double Dx = Vehicle.Location.X - ObserverLocation.X;
			const double Dy = Vehicle.Location.Y - ObserverLocation.Y;
			return (Dx * Dx + Dy * Dy) > DespawnRadiusCmSq;
		});
	}

	const int32 CountBefore = Vehicles.Num();
	Vehicles.RemoveAll([](const FTrafficVehicle& Vehicle) { return Vehicle.bRemoved; });
	TotalRemoved += CountBefore - Vehicles.Num();

	// -- 4) Spawn ---------------------------------------------------------------
	//
	// Gespawnt wird IM UMKREIS des Beobachters, nicht ueber das ganze Netz.
	// Bei 2.700 km Netz und 111.000 Spuren waere gleichmaessige Verteilung
	// gleichbedeutend mit einer leeren Stadt: 3.000 Fahrzeuge ergaeben rund
	// eines je Kilometer.
	//
	// Ohne gesetzten Beobachter bleibt das alte Verhalten erhalten - das
	// brauchen die datenreinen Tests, die ohne Welt laufen.
	const TArray<int32>& ActiveSpawnLanes = bHasObserver ? NearbySpawnLaneIds : SpawnLaneIds;

	if (ActiveSpawnLanes.Num() > 0)
	{
		const float Density = FMath::Clamp(Settings.TrafficDensity, 0.0f, 1.0f);

		// Zielbestand im Umkreis. Solange er nicht erreicht ist, wird
		// nachgefuellt statt mit fester Rate zu tropfen: der Spieler soll
		// nicht erst nach einer halben Stunde Verkehr sehen. Bei 1 Fahrzeug
		// je Sekunde haette das Erreichen von 3.000 Fahrzeugen 50 Minuten
		// gedauert.
		const int32 TargetCount = bHasObserver
			? FMath::Min(
				FMath::RoundToInt32(Settings.TargetVehiclesInRadius * Density),
				Settings.MaxVehicles)
			: Settings.MaxVehicles;

		const int32 Deficit = TargetCount - Vehicles.Num();

		// Schnelles Auffuellen NUR im spielerzentrierten Betrieb.
		//
		// Ohne Beobachter gilt weiterhin der dokumentierte Vertrag
		// "Rate = MaxSpawnRatePerSecond * Dichte" - darauf stuetzen sich die
		// datenreinen Tests, und ohne Bezugspunkt gibt es auch keinen
		// sinnvollen Zielbestand: MaxVehicles als Ziel zu nehmen wuerde den
		// Schub bei jedem Tick ausloesen und die Rate faktisch aushebeln.
		if (bHasObserver && Deficit > 0)
		{
			// Im eingeschwungenen Zustand traegt die normale Rate; solange
			// der Zielbestand fehlt, wird deutlich schneller nachgefuellt,
			// damit die Strassen nicht erst nach Minuten belebt sind.
			const double FillRate = FMath::Max<double>(
				Settings.MaxSpawnRatePerSecond * Density,
				static_cast<double>(Deficit) * 4.0);
			SpawnAccumulator += FillRate * Dt;
		}
		else
		{
			SpawnAccumulator += Settings.MaxSpawnRatePerSecond * Density * Dt;
		}

		int32 SpawnBudget = Settings.MaxVehicles; // Schutz vor Endlosschleife.
		while (SpawnAccumulator >= 1.0
			&& Vehicles.Num() < FMath::Min(TargetCount, Settings.MaxVehicles)
			&& SpawnBudget-- > 0)
		{
			SpawnAccumulator -= 1.0;
			const int32 SpawnLaneId = ActiveSpawnLanes[TotalSpawned % ActiveSpawnLanes.Num()];

			// Spur-Anfang blockiert? (Fahrzeug steht noch am Spawnpunkt)
			bool bBlocked = false;
			for (const FTrafficVehicle& Vehicle : Vehicles)
			{
				if (Vehicle.bOnLane && Vehicle.LaneId == SpawnLaneId && Vehicle.DistanceCm < MinGap)
				{
					bBlocked = true;
					break;
				}
			}
			if (bBlocked)
			{
				continue; // Spawn wartet (deterministisch).
			}

			FTrafficVehicle Vehicle;
			Vehicle.VehicleId = static_cast<int32>(TotalSpawned);
			Vehicle.LaneId = SpawnLaneId;
			Vehicle.bOnLane = true;
			Vehicle.DistanceCm = 0.0;
			Vehicle.DesiredSpeedCmS = ComputeDesiredSpeed(Vehicle);
			Vehicle.SpeedCmS = Vehicle.DesiredSpeedCmS;
			++TotalSpawned;
			Vehicles.Add(Vehicle);
		}
	}

	// -- 5) Positionen + Report -------------------------------------------------
	double SpeedSum = 0.0;
	for (FTrafficVehicle& Vehicle : Vehicles)
	{
		SpeedSum += Vehicle.SpeedCmS;
		if (Vehicle.bOnLane)
		{
			SamplePolyline(Network->Lanes[Vehicle.LaneId].Centerline, Vehicle.DistanceCm,
				Vehicle.Location, Vehicle.Forward);
		}
		else
		{
			SamplePolyline(Network->Connections[Vehicle.ConnectionIndex].ConnectionPath,
				Vehicle.DistanceCm, Vehicle.Location, Vehicle.Forward);
		}

		// Die Karosserie faehrt der eben bestimmten Sollpose mit eigenem
		// Lenkeinschlag hinterher.
		UpdateBodyPose(Vehicle, DeltaSeconds);
	}

	// Steher zaehlen: Fahrzeuge unter 5 km/h, die schneller fahren wollten.
	//
	// Ohne diese Zahl laesst sich "die Autos stauen sich" nicht nachpruefen.
	// Dass die Simulation laeuft, heisst nicht, dass der Verkehr fliesst.
	constexpr double StallSpeedCmS = 139.0;   // 5 km/h
	int32 Stalled = 0;
	for (const FTrafficVehicle& Vehicle : Vehicles)
	{
		if (Vehicle.SpeedCmS < StallSpeedCmS && Vehicle.DesiredSpeedCmS > StallSpeedCmS)
		{
			++Stalled;
		}
	}
	Report.StalledVehicleCount = Stalled;

	Report.ActiveVehicleCount = Vehicles.Num();
	Report.TotalSpawnedCount = TotalSpawned;
	Report.TotalRemovedCount = TotalRemoved;
	Report.TotalDistanceCm = TotalDistanceCm;
	Report.MeanSpeedKmh = Vehicles.Num() > 0
		? (SpeedSum / Vehicles.Num()) * 3600.0 / 100000.0 : 0.0;
	const double DrivableKm = Network->GetTotalDrivableLengthKm();
	Report.ActiveVehiclesPerKm = DrivableKm > 0.0 ? Vehicles.Num() / DrivableKm : 0.0;
}
