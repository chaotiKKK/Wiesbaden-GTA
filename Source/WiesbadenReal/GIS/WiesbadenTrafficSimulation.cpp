// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenTrafficSimulation.h"

#include "WiesbadenReal.h"

#include "GIS/WiesbadenTrafficLights.h"
#include "Misc/FileHelper.h"

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
	NearbySpawnLaneLengthCm = 0.0;
	NearbySpawnCumulativeWeights.Reset();
	LaneNeighbours.Reset();
	ConnectionConflicts.Reset();
	VehiclesByLaneCache.Reset();
	SpawnAccumulator = 0.0;
	TotalSpawned = 0;
	TotalRemoved = 0;
	LifetimeVehiclesHeldAtRed = 0;
	LifetimeLaneChanges = 0;
	LifetimeLaneChangeCandidates = 0;
	LifetimeLaneChangeNoNeighbour = 0;
	LifetimeLaneChangeBlockedByGap = 0;
	LifetimeLaneChangeNoGain = 0;
	LifetimeTightAheadOnly = 0;
	LifetimeTightBehindOnly = 0;
	LifetimeTightBoth = 0;
	LaneFlow.Reset();
	LifetimeVehiclesApproachingSignal = 0;
	bAnySignalizedConnectionEverRed = false;
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

	// Welche Wege durch eine Kreuzung liegen einander im Weg? Einmal hier -
	// das Netz aendert sich nicht mehr, und je Tick waeren es Zehntausende
	// Strecken-Schnitte.
	BuildConnectionConflicts();
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
		Placed.bBraking = Vehicle.bBraking;
		Placed.Indicator = Vehicle.Indicator;

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
	NearbySpawnLaneLengthCm = 0.0;
	NearbySpawnCumulativeWeights.Reset();
	LaneNeighbours.Reset();
	ConnectionConflicts.Reset();
	VehiclesByLaneCache.Reset();
	SpawnAccumulator = 0.0;
	TotalSpawned = 0;
	TotalRemoved = 0;
	LifetimeVehiclesHeldAtRed = 0;
	LifetimeVehiclesApproachingSignal = 0;
	bAnySignalizedConnectionEverRed = false;
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

int32 FWiesbadenTrafficSimulation::PickWeightedIndex(
	const TArray<double>& Cumulative, uint32 Roll)
{
	if (Cumulative.Num() == 0)
	{
		return INDEX_NONE;
	}

	const double Total = Cumulative.Last();
	if (Total <= KINDA_SMALL_NUMBER)
	{
		return INDEX_NONE;
	}

	// Hash auf [0, Total) abbilden - dieselbe Rechnung wie bei der Wahl der
	// Folgeverbindung, damit beide Stellen gleich zufaellig und gleich
	// reproduzierbar sind.
	const double Target = (static_cast<double>(Roll % 1000000u) / 1000000.0) * Total;

	// Binaere Suche: bei mehreren hundert Spuren im Umkreis ist eine lineare
	// Suche je Einsatz unnoetig teuer.
	int32 Low = 0;
	int32 High = Cumulative.Num() - 1;
	while (Low < High)
	{
		const int32 Mid = (Low + High) / 2;
		if (Cumulative[Mid] <= Target)
		{
			Low = Mid + 1;
		}
		else
		{
			High = Mid;
		}
	}
	return Low;
}

void FWiesbadenTrafficSimulation::CollectClassDistribution(
	TMap<EOSMHighwayType, int32>& Out) const
{
	Out.Reset();
	if (!Network)
	{
		return;
	}

	for (const FTrafficVehicle& Vehicle : Vehicles)
	{
		if (!Vehicle.bOnLane || !Network->Lanes.IsValidIndex(Vehicle.LaneId))
		{
			continue;   // auf einer Verbindung - die gehoert keiner Klasse
		}
		const int32 SegmentId = Network->Lanes[Vehicle.LaneId].SegmentId;
		if (!Network->Segments.IsValidIndex(SegmentId))
		{
			continue;
		}
		++Out.FindOrAdd(Network->Segments[SegmentId].HighwayType, 0);
	}
}

bool FWiesbadenTrafficSimulation::ShouldShowBrakeLight(
	double PrevSpeedCmS, double SpeedCmS, double DesiredSpeedCmS, double DeltaSeconds)
{
	// Stillstand trotz Fahrwunsch: die Kolonne vor der Ampel leuchtet.
	constexpr double CrawlCmS = 60.0;          // gut 2 km/h
	if (SpeedCmS < CrawlCmS && DesiredSpeedCmS > CrawlCmS)
	{
		return true;
	}

	if (DeltaSeconds <= 0.0)
	{
		return false;
	}

	// Sonst: nennenswerte Verzoegerung. 150 cm/s^2 ist deutlich weniger als
	// eine Vollbremsung, aber mehr als das Ausrollen eine Kuppe hinauf -
	// sonst flackerten die Lichter der ganzen Stadt im Takt des Gelaendes.
	constexpr double BrakeThresholdCmS2 = 150.0;
	const double Deceleration = (PrevSpeedCmS - SpeedCmS) / DeltaSeconds;
	return Deceleration > BrakeThresholdCmS2;
}

EVehicleIndicator FWiesbadenTrafficSimulation::IndicatorForTurn(ETurnType Turn)
{
	switch (Turn)
	{
	case ETurnType::Left:
	case ETurnType::UTurn:      // Wenden wird links angezeigt
		return EVehicleIndicator::Left;
	case ETurnType::Right:
		return EVehicleIndicator::Right;
	default:
		return EVehicleIndicator::None;
	}
}

bool FWiesbadenTrafficSimulation::IsIndicatorLit(int32 VehicleId, double TimeSeconds)
{
	constexpr double PeriodSeconds = 1.0 / 1.5;   // 1,5 Hz

	// Phase je Fahrzeug aus der Id - sonst blinkt eine ganze Kreuzung im
	// Gleichtakt, was unnatuerlich aussieht.
	const double Phase = static_cast<double>(
		Hash2(static_cast<uint32>(VehicleId), 0x9E3779B9u) % 1000u) / 1000.0;

	double T = FMath::Fmod(TimeSeconds / PeriodSeconds + Phase, 1.0);
	if (T < 0.0)
	{
		T += 1.0;
	}
	return T < 0.5;
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

bool FWiesbadenTrafficSimulation::PeekNextEdge(
	const FTrafficVehicle& Vehicle, bool& bOutOnLane, int32& OutIndex) const
{
	if (Vehicle.bOnLane)
	{
		const int32 Pick = PickSuccessorConnection(Vehicle);
		if (Pick == INDEX_NONE)
		{
			return false;   // Sackgasse
		}
		bOutOnLane = false;
		OutIndex = Pick;
		return true;
	}

	if (!Network->Connections.IsValidIndex(Vehicle.ConnectionIndex))
	{
		return false;
	}
	const FLaneConnection& Connection = Network->Connections[Vehicle.ConnectionIndex];
	if (!Network->Lanes.IsValidIndex(Connection.ToLaneId)
		|| !Network->Lanes[Connection.ToLaneId].IsValid())
	{
		return false;
	}
	bOutOnLane = true;
	OutIndex = Connection.ToLaneId;
	return true;
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

double FWiesbadenTrafficSimulation::BodyDeviationLimitCm(
	const FWiesbadenTrafficSettings& InSettings, double SpeedCmS, bool bChangingLane)
{
	const double Voll = FMath::Max(InSettings.MaxBodyDeviationCm, 1.0);

	// Waehrend des Herueberziehens gilt die grosse Grenze - dafuer ist sie da.
	if (bChangingLane)
	{
		return Voll;
	}

	const double Stand = FMath::Clamp(InSettings.StandingBodyDeviationCm, 0.0, Voll);
	const double Fahrt = FMath::Clamp(InSettings.DrivingBodyDeviationCm, Stand, Voll);
	const double VollAb = FMath::Max(InSettings.BodyDeviationFullSpeedCmS, 1.0);
	const double Anteil = FMath::Clamp(FMath::Max(SpeedCmS, 0.0) / VollAb, 0.0, 1.0);
	return FMath::Lerp(Stand, Fahrt, Anteil);
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

	// Die Grenze haengt am TEMPO. Im Stand bewegt das Einspurmodell die
	// Karosserie gar nicht mehr (Step = Tempo * Dt) - wer mit Versatz zum
	// Stehen kommt, bliebe fuer immer neben seiner Spur stehen. Genau daraus
	// entstanden die ineinander steckenden Kolonnen im Stau.
	const double MaxDeviation = BodyDeviationLimitCm(
		Settings, Vehicle.SpeedCmS, Vehicle.LaneChangeCooldown > 0.0f);
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

void FWiesbadenTrafficSimulation::CollectStalledVehicles(
	int32 MaxCount, TArray<FStalledVehicle>& Out) const
{
	Out.Reset();
	if (!Network || MaxCount <= 0)
	{
		return;
	}

	// Dieselbe Schwelle wie die Bilanz: unter 5 km/h trotz hoeherem Fahrwunsch.
	constexpr double StalledSpeedCmS = 139.0;

	for (const FTrafficVehicle& Vehicle : Vehicles)
	{
		if (Vehicle.SpeedCmS >= StalledSpeedCmS
			|| Vehicle.DesiredSpeedCmS <= StalledSpeedCmS)
		{
			continue;
		}

		FStalledVehicle Entry;
		Entry.VehicleId = Vehicle.VehicleId;
		Entry.LaneId = Vehicle.LaneId;
		Entry.Location = Vehicle.Location;
		Entry.SpeedCmS = Vehicle.SpeedCmS;
		Entry.DesiredSpeedCmS = Vehicle.DesiredSpeedCmS;

		if (bHasPlayerObstacle)
		{
			Entry.PlayerDistanceCm = FVector::Dist2D(Vehicle.Location, PlayerObstacleLocation);
		}

		// Vordermann: das naechste Fahrzeug VOR diesem, in Fahrtrichtung.
		double BestAhead = TNumericLimits<double>::Max();
		for (const FTrafficVehicle& Other : Vehicles)
		{
			if (Other.VehicleId == Vehicle.VehicleId)
			{
				continue;
			}
			const FVector Delta = Other.Location - Vehicle.Location;
			const double Along = FVector::DotProduct(Delta, Vehicle.Forward);
			if (Along <= 0.0)
			{
				continue;   // hinter uns
			}
			const double Lateral = FVector::Dist2D(
				Other.Location, Vehicle.Location + Vehicle.Forward * Along);
			if (Lateral > 200.0)
			{
				continue;   // nicht in derselben Spur
			}
			BestAhead = FMath::Min(BestAhead, Along);
		}
		if (BestAhead < TNumericLimits<double>::Max())
		{
			Entry.AheadDistanceCm = BestAhead;
		}

		Entry.bHeldAtRed = Vehicle.bWasHeldAtRed;

		if (const TArray<int32>* Successors = LaneSuccessorIndices.Find(Vehicle.LaneId))
		{
			Entry.bHasSuccessor = Successors->Num() > 0;
		}
		else
		{
			Entry.bHasSuccessor = false;
		}

		Out.Add(Entry);
	}

	// Die naechsten zuerst - die sieht der Spieler.
	if (bHasPlayerObstacle)
	{
		Out.Sort([](const FStalledVehicle& A, const FStalledVehicle& B)
		{
			return A.PlayerDistanceCm < B.PlayerDistanceCm;
		});
	}

	if (Out.Num() > MaxCount)
	{
		Out.SetNum(MaxCount);
	}
}

int32 FWiesbadenTrafficSimulation::GetTargetVehicleCount() const
{
	// Ohne Beobachter gibt es keinen Umkreis - dann gilt die harte Obergrenze.
	// Darauf stuetzen sich die datenreinen Tests, die ohne Welt laufen.
	if (!bHasObserver)
	{
		return Settings.MaxVehicles;
	}

	// Die Spurlaenge im Umkreis traegt den Zielbestand: dieselbe Dichte ergibt
	// am Stadtrand wenige, in der Innenstadt viele Fahrzeuge - dort ist mehr
	// Fahrbahn zu fuellen.
	const float Density = FMath::Clamp(Settings.TrafficDensity, 0.0f, 1.0f);
	const double LaneKm = NearbySpawnLaneLengthCm / 100000.0;
	return FMath::Clamp(
		FMath::RoundToInt32(LaneKm * Settings.VehiclesPerLaneKm * Density),
		0, Settings.MaxVehicles);
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

		// Bezugsgroesse fuer die erlebte Dichte (Fahrzeuge je Kilometer) UND
		// die Klassengewichte fuer den Einsatzort. Fallen hier praktisch
		// umsonst ab - die Auswahl laeuft ohnehin nur alle paar hundert Meter
		// Fahrt, nicht je Bild.
		NearbySpawnLaneLengthCm = 0.0;
		NearbySpawnCumulativeWeights.Reset();
		NearbySpawnCumulativeWeights.Reserve(NearbySpawnLaneIds.Num());

		double RunningWeight = 0.0;
		for (const int32 LaneId : NearbySpawnLaneIds)
		{
			const FRoadLane& Lane = Network->Lanes[LaneId];
			NearbySpawnLaneLengthCm += Lane.LengthCm;

			// Gewicht der Klasse MAL Laenge: eine 300-m-Hauptstrasse traegt
			// mehr Verkehr als ein 30-m-Stummel derselben Klasse. Ohne die
			// Laenge zaehlte jede noch so kurze Spur gleich viel, und in
			// kleinteiligen Netzen kippt das Bild wieder.
			const double ClassWeight = Network->Segments.IsValidIndex(Lane.SegmentId)
				? GetRoadClassWeight(Network->Segments[Lane.SegmentId].HighwayType)
				: 1.0;

			RunningWeight += ClassWeight * FMath::Max(Lane.LengthCm, 1.0);
			NearbySpawnCumulativeWeights.Add(RunningWeight);
		}

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

namespace
{
	/**
	 * Schnittpunkt zweier Strecken in der Ebene, mit den Parametern auf beiden.
	 *
	 * Hoehe bleibt aussen vor - wie ueberall hier: sonst gilt die Bruecke ueber
	 * der Strasse als Kreuzungskonflikt.
	 */
	bool WbSegmentIntersection2D(const FVector& A0, const FVector& A1,
		const FVector& B0, const FVector& B1, double& OutTA, double& OutTB)
	{
		OutTA = 0.0;
		OutTB = 0.0;

		const FVector2D P(A0.X, A0.Y);
		const FVector2D R(A1.X - A0.X, A1.Y - A0.Y);
		const FVector2D Q(B0.X, B0.Y);
		const FVector2D S(B1.X - B0.X, B1.Y - B0.Y);

		const double Cross = R.X * S.Y - R.Y * S.X;
		if (FMath::IsNearlyZero(Cross, 1e-9))
		{
			// Parallel. Kollinear ueberlappende Strecken zaehlen NICHT als
			// Kreuzung: das sind die parallel gefuehrten Wege derselben Achse,
			// und die stoeren einander nicht.
			return false;
		}

		const FVector2D QP = Q - P;
		const double T = (QP.X * S.Y - QP.Y * S.X) / Cross;
		const double U = (QP.X * R.Y - QP.Y * R.X) / Cross;
		if (T < 0.0 || T > 1.0 || U < 0.0 || U > 1.0)
		{
			return false;
		}

		OutTA = T;
		OutTB = U;
		return true;
	}
}

bool FWiesbadenTrafficSimulation::SegmentsIntersect2D(
	const FVector& A0, const FVector& A1, const FVector& B0, const FVector& B1)
{
	double TA = 0.0;
	double TB = 0.0;
	return WbSegmentIntersection2D(A0, A1, B0, B1, TA, TB);
}

bool FWiesbadenTrafficSimulation::FindConnectionConflict(
	const FLaneConnection& A, const FLaneConnection& B,
	double& OutClearOnA, double& OutClearOnB)
{
	OutClearOnA = 0.0;
	OutClearOnB = 0.0;

	// Aus DERSELBEN Spur: kein Konflikt. Die beiden faechern aus einer Kolonne
	// auf, und dort haelt die Folgeregel sie bereits auseinander. Wer sie hier
	// sperrt, laesst jede Kreuzung nur noch im Gaensemarsch abfliessen.
	if (A.FromLaneId == B.FromLaneId)
	{
		return false;
	}

	const auto PathLength = [](const FLaneConnection& C)
	{
		double Sum = 0.0;
		for (int32 i = 1; i < C.ConnectionPath.Num(); ++i)
		{
			Sum += FVector::Dist(C.ConnectionPath[i], C.ConnectionPath[i - 1]);
		}
		return Sum;
	};

	// In DIESELBE Spur: Konflikt bis zum Ende. Die beiden treffen sich beim
	// Einfaedeln, auch wenn sich die Wege vorher nicht schneiden - frei wird es
	// erst, wenn einer die Verbindung verlassen hat.
	if (A.ToLaneId == B.ToLaneId)
	{
		OutClearOnA = PathLength(A);
		OutClearOnB = PathLength(B);
		return true;
	}

	return FindPathCrossing(A, B, OutClearOnA, OutClearOnB);
}

bool FWiesbadenTrafficSimulation::FindPathCrossing(
	const FLaneConnection& A, const FLaneConnection& B,
	double& OutClearOnA, double& OutClearOnB)
{
	OutClearOnA = 0.0;
	OutClearOnB = 0.0;

	double AlongA = 0.0;
	for (int32 i = 1; i < A.ConnectionPath.Num(); ++i)
	{
		const double SegmentA = FVector::Dist(A.ConnectionPath[i], A.ConnectionPath[i - 1]);
		double AlongB = 0.0;
		for (int32 k = 1; k < B.ConnectionPath.Num(); ++k)
		{
			const double SegmentB = FVector::Dist(B.ConnectionPath[k], B.ConnectionPath[k - 1]);
			double TA = 0.0;
			double TB = 0.0;
			if (WbSegmentIntersection2D(A.ConnectionPath[i - 1], A.ConnectionPath[i],
				B.ConnectionPath[k - 1], B.ConnectionPath[k], TA, TB))
			{
				// Erster Schnittpunkt genuegt: ab da liegen die Wege ineinander.
				OutClearOnA = AlongA + TA * SegmentA;
				OutClearOnB = AlongB + TB * SegmentB;
				return true;
			}
			AlongB += SegmentB;
		}
		AlongA += SegmentA;
	}
	return false;
}

bool FWiesbadenTrafficSimulation::DoConnectionsConflictForGroup(
	const FLaneConnection& A, const FLaneConnection& B, bool bSameTargetLaneBlocks)
{
	// Dieselbe Rechnung wie fuer die Laufzeitregel, aber eine andere FRAGE.
	//
	// Die Laufzeitregel fragt: darf dieses Fahrzeug jetzt losfahren? Dort ist
	// eine gemeinsame ZIELSPUR ein Konflikt - der Hintere wartet, bis der
	// Vordere die Verbindung verlassen hat.
	//
	// Eine FREIGABEGRUPPE fragt etwas anderes: duerfen diese beiden Stroeme
	// gleichzeitig Gruen bekommen? Ein Verkehrsplaner gibt zwei einfaedelnde
	// Stroeme sehr wohl gemeinsam frei; sie sortieren sich ueber Luecken, und
	// genau dafuer gibt es die Laufzeitregel. Wer sie auch hier trennt, kauft
	// Konfliktfreiheit mit zusaetzlichen Phasen - und jede Phase verlaengert
	// den Umlauf fuer ALLE.
	//
	// Die Geometrie kommt aus FindPathCrossing, derselben Funktion, die auch
	// die Laufzeitregel benutzt. Zwei Rechnungen fuer dieselbe Frage laufen
	// auseinander; hier ist es EINE Rechnung mit zwei Regelwerken darum.
	if (A.FromLaneId == B.FromLaneId)
	{
		return false;       // aus einer Kolonne aufgefaechert
	}
	if (bSameTargetLaneBlocks && A.ToLaneId == B.ToLaneId)
	{
		return true;
	}
	double ClearA = 0.0;
	double ClearB = 0.0;
	return FindPathCrossing(A, B, ClearA, ClearB);
}

bool FWiesbadenTrafficSimulation::DoConnectionsConflict(
	const FLaneConnection& A, const FLaneConnection& B)
{
	double ClearA = 0.0;
	double ClearB = 0.0;
	return FindConnectionConflict(A, B, ClearA, ClearB);
}

void FWiesbadenTrafficSimulation::BuildConnectionConflicts()
{
	ConnectionConflicts.Reset();
	if (!Network)
	{
		return;
	}

	// Nach Knoten gruppieren: nur Verbindungen DESSELBEN Knotens koennen
	// einander im Weg liegen.
	TMap<int64, TArray<int32>> ConnectionsByNode;
	for (int32 i = 0; i < Network->Connections.Num(); ++i)
	{
		ConnectionsByNode.FindOrAdd(Network->Connections[i].IntersectionNodeId).Add(i);
	}

	int32 ConflictPairs = 0;
	for (const TPair<int64, TArray<int32>>& Node : ConnectionsByNode)
	{
		const TArray<int32>& Indices = Node.Value;
		for (int32 a = 0; a < Indices.Num(); ++a)
		{
			for (int32 b = a + 1; b < Indices.Num(); ++b)
			{
				const int32 IndexA = Indices[a];
				const int32 IndexB = Indices[b];
				double ClearOnA = 0.0;
				double ClearOnB = 0.0;
				if (!FindConnectionConflict(Network->Connections[IndexA],
					Network->Connections[IndexB], ClearOnA, ClearOnB))
				{
					continue;
				}

				// Gemerkt wird jeweils, ab welcher Bogenlaenge der ANDERE aus
				// dem Weg ist - das ist die Zahl, die die Laufzeitregel braucht.
				ConnectionConflicts.FindOrAdd(IndexA).Add({ IndexB, ClearOnB });
				ConnectionConflicts.FindOrAdd(IndexB).Add({ IndexA, ClearOnA });
				++ConflictPairs;
			}
		}
	}

	UE_LOG(LogWbTraffic, Log,
		TEXT("Verkehr: %d Verbindungen an %d Knoten, %d kreuzende Paare vorgemerkt."),
		Network->Connections.Num(), ConnectionsByNode.Num(), ConflictPairs);
}

double FWiesbadenTrafficSimulation::ApproachSpeedForBlockedJunctionCmS(
	double CurrentSpeedCmS, double DistanceToLineCm,
	double StopBufferCm, double DecelerationCmS2)
{
	if (DecelerationCmS2 <= 0.0)
	{
		return CurrentSpeedCmS;
	}

	// Freie Strecke bis zum Halteort. Am Halteort selbst ist sie null, und
	// damit auch das zulaessige Tempo - dort steht das Fahrzeug.
	const double Frei = FMath::Max(0.0, DistanceToLineCm - FMath::Max(StopBufferCm, 0.0));
	const double Moeglich = FMath::Sqrt(2.0 * DecelerationCmS2 * Frei);

	// NUR begrenzen, nie beschleunigen: wer ohnehin langsamer faehrt, bleibt es.
	return FMath::Min(FMath::Max(CurrentSpeedCmS, 0.0), Moeglich);
}

void FWiesbadenTrafficSimulation::ApplyJunctionConflicts()
{
	if (!Network || ConnectionConflicts.Num() == 0 || !Settings.bJunctionConflicts)
	{
		return;
	}

	// Wie weit ist das HINTERSTE Fahrzeug auf jeder Verbindung? Nur das kann
	// einen Konfliktpunkt noch besetzen - alle davor haben ihn passiert.
	//
	// Genau daran haengt die Brauchbarkeit der Regel: sperrt jedes Fahrzeug
	// seine ganze Verbindung, bis es sie verlassen hat, steht die Stadt
	// (gemessen 56 % Steher statt 36 %). Wer den Kreuzungspunkt hinter sich
	// hat, ist aus dem Weg.
	TMap<int32, double> RearmostOnConnection;
	for (const FTrafficVehicle& Vehicle : Vehicles)
	{
		if (Vehicle.bOnLane || Vehicle.ConnectionIndex == INDEX_NONE)
		{
			continue;
		}
		double& Rearmost = RearmostOnConnection.FindOrAdd(
			Vehicle.ConnectionIndex, TNumericLimits<double>::Max());
		Rearmost = FMath::Min(Rearmost, Vehicle.DistanceCm);
	}

	// Wer bereits IM Knoten ist, wird nie gestoppt - sonst steht er quer und
	// blockiert alles, was noch kommt.
	//
	// Das eigene Heck zaehlt mit: der Konfliktpunkt ist erst frei, wenn das
	// ganze Fahrzeug darueber hinaus ist. Die Bogenlaenge misst die MITTE des
	// Fahrzeugs - hinter ihr liegt eine halbe Laenge, nicht eine ganze. Eine
	// ganze Laenge anzusetzen haelt den Weg zwei Meter laenger besetzt, als er
	// es ist.
	const double TailCm = FMath::Max(Settings.VehicleHalfLengthCm, 0.0);

	const auto IsConflictBlocked = [&RearmostOnConnection, TailCm]
		(const FConnectionConflict& Conflict)
	{
		const double* Rearmost = RearmostOnConnection.Find(Conflict.OtherConnection);
		return Rearmost && (*Rearmost <= Conflict.ClearDistanceOnOtherCm + TailCm);
	};

	// Anwaerter: Fahrzeuge kurz vor der Haltelinie mit gewaehlter Verbindung.
	struct FEntryCandidate
	{
		int32 VehicleIndex = INDEX_NONE;
		int32 ConnectionIndex = INDEX_NONE;
		double DistanceToLineCm = 0.0;

		/** An der Haltelinie - nur diese belegen den Weg fuer sich. */
		bool bAtLine = false;

		int32 VehicleId = INDEX_NONE;
	};

	const double StopDistance = FMath::Max(Settings.MinGapCm * 0.5, 100.0);

	// Komfortable Verzoegerung fuer das Anfahren einer belegten Kreuzung -
	// dieselbe Groessenordnung wie bei der Ruecksicht auf den Spieler. Deutlich
	// spuerbar, aber kein Notbremsen.
	const double ComfortDeceleration = 400.0;

	TArray<FEntryCandidate> Candidates;
	for (int32 i = 0; i < Vehicles.Num(); ++i)
	{
		const FTrafficVehicle& Vehicle = Vehicles[i];
		if (!Vehicle.bOnLane || !Network->Lanes.IsValidIndex(Vehicle.LaneId))
		{
			continue;
		}
		const double LaneLength = Network->Lanes[Vehicle.LaneId].LengthCm;
		if (LaneLength <= 0.0)
		{
			continue;
		}

		// Das Fenster reicht so weit, wie das Fahrzeug zum Bremsen braucht -
		// nicht nur bis zur Haltelinie. Wer erst 3,5 m davor erfaehrt, dass der
		// Weg belegt ist, kann nur noch stehenbleiben; das kostet jedes Mal
		// Anfahren und zwingt die ganze Kolonne dahinter in dieselbe Bremsung.
		const double Bremsweg = (Vehicle.SpeedCmS * Vehicle.SpeedCmS)
			/ (2.0 * FMath::Max(ComfortDeceleration, 1.0));
		const double BisZurLinie = LaneLength - Vehicle.DistanceCm;
		if (BisZurLinie > StopDistance + Bremsweg)
		{
			continue;
		}

		const int32 Next = PickSuccessorConnection(Vehicle);
		if (Next == INDEX_NONE || !ConnectionConflicts.Contains(Next))
		{
			continue;   // freie Fahrt: dieser Weg kreuzt keinen anderen
		}


		FEntryCandidate Candidate;
		Candidate.VehicleIndex = i;
		Candidate.ConnectionIndex = Next;
		Candidate.DistanceToLineCm = BisZurLinie;
		Candidate.bAtLine = (BisZurLinie <= StopDistance);
		Candidate.VehicleId = Vehicle.VehicleId;
		Candidates.Add(Candidate);
	}

	// Wer zuerst da ist, faehrt zuerst - und bei Gleichstand entscheidet die
	// Fahrzeug-Id. Ohne diese feste Reihenfolge waere die Vergabe von der
	// Array-Reihenfolge abhaengig und der Lauf nicht mehr wiederholbar; ohne
	// Reihenfolge ueberhaupt fahren zwei gleichzeitig los und stecken wieder
	// ineinander.
	Candidates.Sort([](const FEntryCandidate& A, const FEntryCandidate& B)
	{
		if (A.DistanceToLineCm != B.DistanceToLineCm)
		{
			return A.DistanceToLineCm < B.DistanceToLineCm;
		}
		return A.VehicleId < B.VehicleId;
	});

	// Wie weit ist das hinterste Fahrzeug auf jeder SPUR? Braucht die
	// Blockierfreihaltung: wer keinen Platz hinter der Kreuzung hat, darf nicht
	// hinein.
	TMap<int32, double> RearmostOnLane;
	for (const FTrafficVehicle& Vehicle : Vehicles)
	{
		if (!Vehicle.bOnLane || Vehicle.LaneId == INDEX_NONE)
		{
			continue;
		}
		double& Rearmost = RearmostOnLane.FindOrAdd(
			Vehicle.LaneId, TNumericLimits<double>::Max());
		Rearmost = FMath::Min(Rearmost, Vehicle.DistanceCm);
	}

	// Vorfahrt: Gewicht der Strassenklasse, aus der eine Verbindung kommt.
	const auto ApproachWeight = [this](int32 ConnectionIndex)
	{
		if (!Network->Connections.IsValidIndex(ConnectionIndex))
		{
			return 1.0;
		}
		const int32 FromLane = Network->Connections[ConnectionIndex].FromLaneId;
		if (!Network->Lanes.IsValidIndex(FromLane))
		{
			return 1.0;
		}
		const int32 SegmentId = Network->Lanes[FromLane].SegmentId;
		if (!Network->Segments.IsValidIndex(SegmentId))
		{
			return 1.0;
		}
		return GetRoadClassWeight(Network->Segments[SegmentId].HighwayType);
	};

	// Kommt auf der kreuzenden Zufahrt gleich jemand? Gemessen wird die ZEIT
	// bis zur Haltelinie, nicht die Strecke: ein stehendes Fahrzeug kommt
	// nicht, ein schnelles ist frueher da als es aussieht.
	const auto TimeToLineSeconds = [this](int32 ConnectionIndex)
	{
		if (!Network->Connections.IsValidIndex(ConnectionIndex))
		{
			return TNumericLimits<double>::Max();
		}
		const int32 FromLane = Network->Connections[ConnectionIndex].FromLaneId;
		const TArray<int32>* Queue = VehiclesByLaneCache.Find(FromLane);
		if (!Queue || Queue->Num() == 0 || !Network->Lanes.IsValidIndex(FromLane))
		{
			return TNumericLimits<double>::Max();
		}

		// Der Korb ist absteigend nach Distanz sortiert - vorne steht, wer der
		// Kreuzung am naechsten ist.
		const FTrafficVehicle& Front = Vehicles[(*Queue)[0]];
		const double Remaining = Network->Lanes[FromLane].LengthCm - Front.DistanceCm;
		if (Remaining <= 0.0)
		{
			return 0.0;
		}
		if (Front.SpeedCmS <= 1.0)
		{
			return TNumericLimits<double>::Max();   // steht: kommt nicht
		}
		return Remaining / Front.SpeedCmS;
	};

	for (const FEntryCandidate& Candidate : Candidates)
	{
		const double MyWeight = ApproachWeight(Candidate.ConnectionIndex);
		bool bBlocked = false;

		// BLOCKIERFREIHALTUNG: nicht in die Kreuzung fahren, wenn dahinter kein
		// Platz ist.
		//
		// Ohne diese Regel rollt das Fahrzeug in den Knoten und bleibt dort
		// stehen, weil die Zielspur voll ist. Damit ist sein Konfliktpunkt
		// dauerhaft belegt, alle kreuzenden Stroeme halten, deren Spuren laufen
		// voll - und die Sperre wandert durch das Netz. Genau so entsteht der
		// Unterschied zwischen "Kreuzung kostet etwas Zeit" und "Stadt steht".
		if (Settings.bKeepJunctionsClear
			&& Network->Connections.IsValidIndex(Candidate.ConnectionIndex))
		{
			const int32 ToLane = Network->Connections[Candidate.ConnectionIndex].ToLaneId;
			if (const double* RearmostAhead = RearmostOnLane.Find(ToLane))
			{
				if (*RearmostAhead < FMath::Max(Settings.JunctionExitSpaceCm, 0.0))
				{
					bBlocked = true;
					++LastBlockedNoRoomAhead;
				}
			}
		}
		const TArray<FConnectionConflict>* Conflicts = bBlocked
			? nullptr : ConnectionConflicts.Find(Candidate.ConnectionIndex);
		if (Conflicts)
		{
			for (const FConnectionConflict& Conflict : *Conflicts)
			{
				// Trennt das Signalprogramm die beiden schon? Dann nicht noch
				// einmal sperren. Zwei Verbindungen verschiedener
				// Richtungsgruppen derselben Ampel sind nie zugleich frei -
				// eine zweite Absicherung kostet dort nur Fluss. Bei GLEICHER
				// Gruppe (Einfaedeln in dieselbe Spur, sich schneidende
				// Abbieger) schuetzt das Programm nichts, dort bleibt die
				// Regel scharf.
				if (TrafficLights && !TrafficLights->CanBeGreenTogether(
					Candidate.ConnectionIndex, Conflict.OtherConnection))
				{
					continue;
				}

				// Jemand steht im Konfliktpunkt: da faehrt niemand hinein,
				// egal wer Vorfahrt hat.
				if (IsConflictBlocked(Conflict))
				{
					bBlocked = true;
					++LastBlockedConflictBusy;
					break;
				}

				// VORFAHRT: wer aus der kleineren Strasse kommt, wartet auf eine
				// Luecke in der groesseren. Ohne das haelt auch die Hauptachse
				// an jeder Wohnstrassen-Einmuendung - und die Stadt steht.
				if (Settings.JunctionYieldSeconds > 0.0
					&& ApproachWeight(Conflict.OtherConnection) > MyWeight
					&& TimeToLineSeconds(Conflict.OtherConnection) < Settings.JunctionYieldSeconds)
				{
					bBlocked = true;
					++LastBlockedYielding;
					break;
				}
			}
		}

		if (bBlocked)
		{
			// FRUEHER BREMSEN STATT HART HALTEN. Wer noch weit weg ist, nimmt
			// nur Tempo heraus und kommt oft an, wenn der Weg wieder frei ist;
			// erst an der Haltelinie wird daraus ein Stillstand. Ein harter
			// Halt an der Linie zwang dagegen jedes Mal die ganze Kolonne
			// dahinter in dieselbe Vollbremsung.
			FTrafficVehicle& Wartender = Vehicles[Candidate.VehicleIndex];
			if (Network->Connections.IsValidIndex(Candidate.ConnectionIndex))
			{
				const ETurnType Abbiegen =
					Network->Connections[Candidate.ConnectionIndex].TurnType;
				if (Abbiegen == ETurnType::Left || Abbiegen == ETurnType::UTurn)
				{
					++LastBlockedLeftTurners;
				}
			}
			Wartender.SpeedCmS = ApproachSpeedForBlockedJunctionCmS(
				Wartender.SpeedCmS, Candidate.DistanceToLineCm,
				StopDistance, ComfortDeceleration);
			++LastVehiclesHeldAtJunction;
			continue;
		}

		// Freigegeben - aber belegen darf den Weg nur, wer auch wirklich an der
		// Linie steht. Ein Fahrzeug 40 m davor wuerde sonst die Kreuzung fuer
		// sich reservieren, waehrend es noch anrollt.
		if (!Candidate.bAtLine)
		{
			continue;
		}

		// Das Fahrzeug steht ab sofort am Anfang seiner Verbindung
		// (Bogenlaenge 0) und besetzt damit deren Konfliktpunkte. Sonst faehrt
		// im selben Tick ein zweites in denselben Konflikt.
		double& Rearmost = RearmostOnConnection.FindOrAdd(
			Candidate.ConnectionIndex, TNumericLimits<double>::Max());
		Rearmost = 0.0;
	}
}

bool FWiesbadenTrafficSimulation::AreVehiclesOverlapping(
	const FVector& LocationA, const FVector& ForwardA,
	const FVector& LocationB, const FVector& ForwardB,
	double HalfLengthCm, double HalfWidthCm)
{
	const FVector2D FwdA = FVector2D(ForwardA.X, ForwardA.Y).GetSafeNormal();
	const FVector2D FwdB = FVector2D(ForwardB.X, ForwardB.Y).GetSafeNormal();
	if (FwdA.IsNearlyZero() || FwdB.IsNearlyZero())
	{
		return false;
	}

	const FVector2D RightA(FwdA.Y, -FwdA.X);
	const FVector2D RightB(FwdB.Y, -FwdB.X);
	const FVector2D Delta(LocationB.X - LocationA.X, LocationB.Y - LocationA.Y);

	const double HalfL = FMath::Max(HalfLengthCm, 0.0);
	const double HalfW = FMath::Max(HalfWidthCm, 0.0);

	// Separating Axis Theorem: findet sich EINE Achse, auf der sich die
	// Projektionen nicht ueberschneiden, stehen die beiden frei.
	const FVector2D Axes[4] = { FwdA, RightA, FwdB, RightB };
	for (const FVector2D& Axis : Axes)
	{
		const double Distance = FMath::Abs(FVector2D::DotProduct(Delta, Axis));
		const double ReachA = HalfL * FMath::Abs(FVector2D::DotProduct(FwdA, Axis))
			+ HalfW * FMath::Abs(FVector2D::DotProduct(RightA, Axis));
		const double ReachB = HalfL * FMath::Abs(FVector2D::DotProduct(FwdB, Axis))
			+ HalfW * FMath::Abs(FVector2D::DotProduct(RightB, Axis));
		if (Distance >= ReachA + ReachB)
		{
			return false;
		}
	}
	return true;
}

void FWiesbadenTrafficSimulation::CountVehicleOverlaps(FOverlapReport& Out) const
{
	Out = FOverlapReport();

	TSet<int32> Involved;
	for (int32 i = 0; i < Vehicles.Num(); ++i)
	{
		const FTrafficVehicle& A = Vehicles[i];
		for (int32 j = i + 1; j < Vehicles.Num(); ++j)
		{
			const FTrafficVehicle& B = Vehicles[j];

			// Geprueft wird, was der Spieler SIEHT: die Karosserie. Die
			// Sollposition auf der Bahn ist exakt, die Karosserie laeuft ihr
			// mit Lenkeinschlag und Wendekreis nach - und genau die steht im
			// Bild. Frueher mass diese Diagnose die Bahn und haette ein
			// ausschwingendes Nachlaufmodell nie gesehen.
			const FVector PosA = A.bBodyInitialized ? A.BodyLocation : A.Location;
			const FVector PosB = B.bBodyInitialized ? B.BodyLocation : B.Location;
			const FVector DirA = A.bBodyInitialized
				? FVector(FMath::Cos(A.BodyYawRad), FMath::Sin(A.BodyYawRad), 0.0) : A.Forward;
			const FVector DirB = B.bBodyInitialized
				? FVector(FMath::Cos(B.BodyYawRad), FMath::Sin(B.BodyYawRad), 0.0) : B.Forward;

			// Grobfilter zuerst: der genaue Test lohnt nur in Reichweite.
			const double Dx = PosB.X - PosA.X;
			const double Dy = PosB.Y - PosA.Y;
			if ((Dx * Dx + Dy * Dy) > FMath::Square(2.0 * Settings.VehicleHalfLengthCm))
			{
				continue;
			}

			if (!AreVehiclesOverlapping(PosA, DirA, PosB, DirB,
				Settings.VehicleHalfLengthCm, Settings.VehicleHalfWidthCm))
			{
				continue;
			}

			// Liegen schon die SOLLPOSITIONEN ineinander? Das trennt "Spur zu
			// schmal" von "Karosserie schwingt zu weit aus".
			if (!AreVehiclesOverlapping(A.Location, A.Forward, B.Location, B.Forward,
				Settings.VehicleHalfLengthCm, Settings.VehicleHalfWidthCm))
			{
				++Out.OnlyBodies;
			}

			// Beteiligte Spurbreiten und Seitenversatz mitschreiben - ohne sie
			// bleibt "zu eng" eine Behauptung.
			for (const FTrafficVehicle* V : { &A, &B })
			{
				if (V->bOnLane && Network && Network->Lanes.IsValidIndex(V->LaneId))
				{
					const double Breite = Network->Lanes[V->LaneId].WidthCm;
					if (Breite > 0.0
						&& (Out.NarrowestLaneCm <= 0.0 || Breite < Out.NarrowestLaneCm))
					{
						Out.NarrowestLaneCm = Breite;
					}
				}
				if (V->bBodyInitialized)
				{
					const double Versatz = FVector::Dist2D(V->BodyLocation, V->Location);
					Out.MaxBodyOffsetCm = FMath::Max(Out.MaxBodyOffsetCm, Versatz);
				}
			}

			++Out.Pairs;
			Involved.Add(A.VehicleId);
			Involved.Add(B.VehicleId);

			if (A.bOnLane && B.bOnLane)
			{
				if (A.LaneId == B.LaneId)
				{
					++Out.SameEdge;
				}
				else
				{
					++Out.Other;

					// Derselbe Abschnitt hiesse: die Spuraufteilung legt die
					// Bahnen zu eng. Verschiedene Abschnitte hiessen: zwei
					// Strassen des Netzes liegen zu dicht beieinander. Ohne
					// diese Trennung raet man beim Beheben.
					if (Network && Network->Lanes.IsValidIndex(A.LaneId)
						&& Network->Lanes.IsValidIndex(B.LaneId))
					{
						if (Network->Lanes[A.LaneId].SegmentId
							== Network->Lanes[B.LaneId].SegmentId)
						{
							++Out.SameSegmentLanes;
						}
						else
						{
							++Out.CrossSegmentLanes;
						}
					}

					const double RailAbstand = FVector::Dist2D(A.Location, B.Location);
					if (Out.MinLaneRailDistanceCm <= 0.0
						|| RailAbstand < Out.MinLaneRailDistanceCm)
					{
						Out.MinLaneRailDistanceCm = RailAbstand;
					}
				}
			}
			else if (!A.bOnLane && !B.bOnLane)
			{
				if (A.ConnectionIndex == B.ConnectionIndex)
				{
					++Out.SameEdge;
				}
				else if (Network
					&& Network->Connections.IsValidIndex(A.ConnectionIndex)
					&& Network->Connections.IsValidIndex(B.ConnectionIndex)
					&& Network->Connections[A.ConnectionIndex].IntersectionNodeId
						== Network->Connections[B.ConnectionIndex].IntersectionNodeId)
				{
					++Out.SameJunction;
				}
				else
				{
					++Out.Other;
				}
			}
			else
			{
				++Out.LaneAndConnection;
			}
		}
	}

	Out.VehiclesInvolved = Involved.Num();
}

double FWiesbadenTrafficSimulation::RequiredLaneChangeGapCm(
	const FWiesbadenTrafficSettings& InSettings, double SpeedCmS)
{
	// Zeitluecke statt fester Strecke. Im Stand bleibt die Untergrenze - sonst
	// koennte ein stehendes Fahrzeug in eine Luecke von null Zentimetern
	// ziehen.
	const double ByTime = FMath::Max(InSettings.LaneChangeGapSeconds, 0.0)
		* FMath::Max(SpeedCmS, 0.0);
	return FMath::Max(FMath::Max(InSettings.LaneChangeMinGapCm, 0.0), ByTime);
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
	int32 LaneId, double AtDistanceCm, int32 IgnoreVehicleId,
	double* OutFollowerSpeedCmS) const
{
	double Best = TNumericLimits<double>::Max();
	if (OutFollowerSpeedCmS)
	{
		*OutFollowerSpeedCmS = 0.0;
	}
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
		if (Behind >= 0.0 && Behind < Best)
		{
			Best = Behind;
			// Das Tempo des NAECHSTEN Nachfolgers, nicht irgendeines: er ist
			// der, dem man vor die Nase zieht.
			if (OutFollowerSpeedCmS)
			{
				*OutFollowerSpeedCmS = Other.SpeedCmS;
			}
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
	Report.LaneChangeCandidates = 0;
	Report.LaneChangeNoNeighbour = 0;
	Report.LaneChangeBlockedByGap = 0;
	Report.LaneChangeNoGain = 0;
	Report.LaneChangeTightAheadOnly = 0;
	Report.LaneChangeTightBehindOnly = 0;
	Report.LaneChangeTightBoth = 0;

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

		// An Rot wird gewartet, nicht ueberholt.
		//
		// Ein Fahrzeug vor einer roten Ampel ist langsam - aber nicht
		// BEHINDERT, und die Nebenspur bringt es keinen Meter weiter. Ohne
		// diese Ausnahme zaehlte jede wartende Kolonne jeden Tick als
		// Ueberholwunsch (gemessen ein guter Teil der 594.580 Anlaeufe) und
		// verwischte, woran es wirklich hakt. Die Ampel-Regel laeuft in
		// diesem Tick bereits vor dem Spurwechsel, der Wert steht also.
		if (Vehicle.bWasHeldAtRed)
		{
			continue;
		}

		if (!Network->Lanes.IsValidIndex(Vehicle.LaneId))
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

		// ZWEI Gruende, hinauszuziehen - und der zweite ist der wichtigere.
		//
		// (1) Man ist bereits langsamer als gewollt. Das war bisher der
		//     einzige Grund, und es ist der SPAETE: wer schon steht, steht in
		//     einer Kolonne, in der ringsum nichts mehr frei ist.
		// (2) Man laeuft auf: die Luecke nach vorn reicht nur noch fuer
		//     wenige Sekunden. Genau dann wechselt ein Fahrer - solange es
		//     neben ihm noch Platz gibt.
		const bool bSlowedDown = Vehicle.DesiredSpeedCmS > 0.0
			&& Vehicle.SpeedCmS <= Vehicle.DesiredSpeedCmS * Settings.LaneChangeSpeedDeficit;
		const bool bClosingIn = Settings.LaneChangeLookAheadSeconds > 0.0
			&& CurrentGap < Vehicle.SpeedCmS * Settings.LaneChangeLookAheadSeconds;
		if (!bSlowedDown && !bClosingIn)
		{
			continue;
		}

		// Ab hier ist das Fahrzeug ein Kandidat. Alles Weitere sind Gruende,
		// es doch nicht zu tun - und die werden gezaehlt.
		++Report.LaneChangeCandidates;

		const TArray<int32>* Neighbours = LaneNeighbours.Find(Vehicle.LaneId);
		if (!Neighbours)
		{
			++Report.LaneChangeNoNeighbour;
			continue;
		}

		int32 BestLane = INDEX_NONE;
		double BestGap = CurrentGap;
		double BestDistance = 0.0;
		bool bAnyNeighbourUsable = false;
		bool bAnyNeighbourHadRoom = false;
		bool bTightAhead = false;
		bool bTightBehind = false;

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
			bAnyNeighbourUsable = true;

			// Parallele Spuren desselben Abschnitts sind etwa gleich lang; der
			// Laengenanteil ist deshalb die richtige Uebertragung.
			const double NeighbourDistance = Fraction * Neighbour.LengthCm;

			double FollowerSpeedCmS = 0.0;
			const double GapAhead = ComputeGapAheadOnLane(
				NeighbourLaneId, NeighbourDistance, Vehicle.VehicleId);
			const double GapBehind = ComputeGapBehindOnLane(
				NeighbourLaneId, NeighbourDistance, Vehicle.VehicleId, &FollowerSpeedCmS);

			// Nach hinten zaehlt genauso wie nach vorn: Wer vor einen
			// schnelleren Nachfolger zieht, loest dort dieselbe Bremswelle aus,
			// der er selbst entkommen wollte.
			// Vorn das eigene Tempo, hinten das des Nachfolgers - er ist der,
			// der bremsen muss.
			const double NeedAhead = RequiredLaneChangeGapCm(Settings, Vehicle.SpeedCmS);
			const double NeedBehind = RequiredLaneChangeGapCm(Settings,
				FMath::Max(FollowerSpeedCmS, Vehicle.SpeedCmS));
			if (GapAhead < NeedAhead || GapBehind < NeedBehind)
			{
				// WELCHE Seite klemmt, entscheidet ueber die Abhilfe: vorn zu
				// eng heisst dichter Verkehr auf der Zielspur, hinten zu eng
				// heisst, dass die Regel zu streng ist (ein Nachfolger kann
				// vom Gas gehen, ein Vordermann nicht).
				bTightAhead = bTightAhead || (GapAhead < NeedAhead);
				bTightBehind = bTightBehind || (GapBehind < NeedBehind);
				continue;
			}
			bAnyNeighbourHadRoom = true;

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
			// Das Nein einordnen: gar keine brauchbare Nachbarspur, kein Platz
			// darin, oder Platz ohne Gewinn. Drei verschiedene Befunde, die
			// drei verschiedene Abhilfen verlangen.
			if (!bAnyNeighbourUsable)
			{
				++Report.LaneChangeNoNeighbour;
			}
			else if (!bAnyNeighbourHadRoom)
			{
				++Report.LaneChangeBlockedByGap;
				if (bTightAhead && bTightBehind)
				{
					++Report.LaneChangeTightBoth;
				}
				else if (bTightAhead)
				{
					++Report.LaneChangeTightAheadOnly;
				}
				else
				{
					++Report.LaneChangeTightBehindOnly;
				}
			}
			else
			{
				++Report.LaneChangeNoGain;
			}
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
		++LifetimeLaneChanges;
	}

	LifetimeLaneChangeCandidates += Report.LaneChangeCandidates;
	LifetimeLaneChangeNoNeighbour += Report.LaneChangeNoNeighbour;
	LifetimeLaneChangeBlockedByGap += Report.LaneChangeBlockedByGap;
	LifetimeLaneChangeNoGain += Report.LaneChangeNoGain;
	LifetimeTightAheadOnly += Report.LaneChangeTightAheadOnly;
	LifetimeTightBehindOnly += Report.LaneChangeTightBehindOnly;
	LifetimeTightBoth += Report.LaneChangeTightBoth;
}

int32 FWiesbadenTrafficSimulation::WriteCongestionMap(const FString& Path, int32 MinSamples) const
{
	if (!Network)
	{
		return -1;
	}

	FString Out;
	Out.Reserve(8 * 1024 * 1024);
	Out += TEXT("# Stau-Karte Wiesbaden. Koordinaten in cm (Weltkoordinaten).\n");
	Out += TEXT("# NETZ x1 y1 x2 y2            - Stadtplan (nur Vorwaertsspuren)\n");
	Out += TEXT("# STAU x y MittelKmh LimitKmh StehAnteil Messwerte Strassenname\n");

	// Stadtplan: nur die Vorwaertsspuren, sonst liegt jede Strasse doppelt.
	for (const FRoadLane& Lane : Network->Lanes)
	{
		if (!Lane.IsValid() || Lane.Direction != ELaneDirection::Forward
			|| Lane.Centerline.Num() < 2)
		{
			continue;
		}
		const FVector& A = Lane.Centerline[0];
		const FVector& B = Lane.Centerline.Last();
		Out += FString::Printf(TEXT("NETZ %.0f %.0f %.0f %.0f\n"), A.X, A.Y, B.X, B.Y);
	}

	int32 Written = 0;
	for (const TPair<int32, FLaneFlowSample>& Pair : LaneFlow)
	{
		const FLaneFlowSample& Flow = Pair.Value;
		// Spuren mit wenigen Messwerten fliegen raus: ein einzelnes Fahrzeug,
		// das zufaellig bremst, ist kein Stauschwerpunkt.
		if (Flow.Samples < MinSamples || !Network->Lanes.IsValidIndex(Pair.Key))
		{
			continue;
		}
		const FRoadLane& Lane = Network->Lanes[Pair.Key];
		if (Lane.Centerline.Num() == 0)
		{
			continue;
		}
		const FVector Mid = Lane.Centerline[Lane.Centerline.Num() / 2];

		const double MeanKmh = (Flow.SpeedSumCmS / Flow.Samples) / KmhToCmS;
		const double LimitKmh = Flow.LimitCmS / KmhToCmS;
		const double StallFraction = static_cast<double>(Flow.Stalled) / Flow.Samples;

		// Der NAME macht aus einem Punkt einen Ort. Ohne ihn liest sich die
		// Auswertung als Koordinatenliste, mit der niemand etwas anfangen
		// kann. GetDisplayName faellt auf Referenz bzw. Strassenklasse
		// zurueck, wenn der Way in OSM keinen Namen traegt - das ist
		// haeufig und kein Fehler.
		FString Name = TEXT("(ohne Namen)");
		if (Network->Segments.IsValidIndex(Lane.SegmentId))
		{
			Name = Network->Segments[Lane.SegmentId].GetDisplayName();
		}
		// Der Name steht als LETZTES Feld der Zeile, Leerzeichen darin
		// sind also unschaedlich - Zeilenumbrueche nicht.
		Name.ReplaceInline(TEXT("\n"), TEXT(" "));
		Name.ReplaceInline(TEXT("\r"), TEXT(" "));

		Out += FString::Printf(TEXT("STAU %.0f %.0f %.1f %.1f %.3f %d %s\n"),
			Mid.X, Mid.Y, MeanKmh, LimitKmh, StallFraction, Flow.Samples, *Name);
		++Written;
	}

	// FEST UTF-8. Ohne die Vorgabe wechselt FFileHelper auf UTF-16, sobald ein
	// Zeichen ausserhalb von ASCII vorkommt - und deutsche Strassennamen tun
	// das. Die Kodierung derselben Datei haenge damit davon ab, WELCHE
	// Strassen gemessen wurden; jedes Auswertewerkzeug muesste raten.
	if (!FFileHelper::SaveStringToFile(Out, *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return -1;
	}
	return Written;
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
	LastVehiclesHeldAtJunction = 0;
	LastBlockedNoRoomAhead = 0;
	LastBlockedConflictBusy = 0;
	LastBlockedYielding = 0;
	LastBlockedLeftTurners = 0;

	if (TrafficLights && Network)
	{
		// Ehrliches, verkehrsUNABHAENGIGES Signal fuer die Diagnose: sobald das
		// Ampelsystem EINMAL eine kontrollierte Verbindung auf Rot zeigt, ist die
		// Kopplung nachweislich aktiv. Laeuft ausserhalb der Fahrzeug-Schleife,
		// also auch bei 0 Fahrzeugen. Ist TrafficLights nie gesetzt worden (der
		// historische Bug), wird dieser Block nie betreten und das Flag bleibt
		// false -> die Diagnose meldet "defekt" statt "unschluessig". Sobald das
		// Flag steht, entfaellt die Abfrage (Frueh-Ausstieg in AnyControlled...).
		if (!bAnySignalizedConnectionEverRed && TrafficLights->AnyControlledConnectionRed())
		{
			bAnySignalizedConnectionEverRed = true;
		}

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

	// -- 1b2) Kreuzungskonflikte ------------------------------------------------
	//
	// NACH der Ampelregel: an signalisierten Knoten trennt schon das
	// Signalprogramm die Stroeme, dort greift diese Regel praktisch nie. Nur
	// rund 1.073 der ~20.213 Kreuzungen sind Ampeln - an allen uebrigen war
	// bisher gar nichts, was zwei kreuzende Fahrzeuge auseinandergehalten
	// haette.
	ApplyJunctionConflicts();

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

	// -- 1f) Lampenzustand -----------------------------------------------------
	//
	// NACH allen Geschwindigkeitsregeln: erst hier steht die Geschwindigkeit
	// dieses Schritts endgueltig fest. Davor waere das Bremslicht die Antwort
	// auf einen Zwischenwert, den das Fahrzeug nie gefahren ist.
	for (int32 i = 0; i < Vehicles.Num(); ++i)
	{
		FTrafficVehicle& Vehicle = Vehicles[i];

		const double PrevSpeed = PreviousSpeedCmS.IsValidIndex(i)
			? PreviousSpeedCmS[i] : Vehicle.SpeedCmS;
		Vehicle.bBraking = ShouldShowBrakeLight(
			PrevSpeed, Vehicle.SpeedCmS, Vehicle.DesiredSpeedCmS, Dt);

		// Blinker: auf der Verbindung die gefahrene Richtung, davor die
		// gewaehlte - und nur im Anfahr-Fenster vor der Kreuzung. Wer 300 m
		// vorher blinkt, blinkt die halbe Stadt entlang.
		Vehicle.Indicator = EVehicleIndicator::None;
		if (!Network)
		{
			continue;
		}

		if (!Vehicle.bOnLane)
		{
			if (Network->Connections.IsValidIndex(Vehicle.ConnectionIndex))
			{
				Vehicle.Indicator = IndicatorForTurn(
					Network->Connections[Vehicle.ConnectionIndex].TurnType);
			}
			continue;
		}

		if (!Network->Lanes.IsValidIndex(Vehicle.LaneId))
		{
			continue;
		}

		constexpr double IndicateDistanceCm = 3000.0;   // 30 m vor der Kreuzung
		const double LaneLength = Network->Lanes[Vehicle.LaneId].LengthCm;
		if (LaneLength <= 0.0 || Vehicle.DistanceCm < LaneLength - IndicateDistanceCm)
		{
			continue;
		}

		const int32 Next = PickSuccessorConnection(Vehicle);
		if (Network->Connections.IsValidIndex(Next))
		{
			Vehicle.Indicator = IndicatorForTurn(Network->Connections[Next].TurnType);
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
	//
	// VORFAHRT AM BAHNANFANG: Wer auf eine Bahn wechselt, deren Anfang schon
	// besetzt ist, wartet am Ende seiner jetzigen Bahn.
	//
	// Ohne diese Regel setzte der Wechsel Fahrzeuge aus verschiedenen
	// Zufluessen im selben Tick auf denselben Punkt. Gemessen am Startplatz:
	// drei Fahrzeuge auf Spur 47168 bei (-144427, -158587), auf 1 cm genau
	// uebereinander, Vordermann-Abstand 0,0 m. Danach kamen sie nie wieder
	// auseinander - die Abstandsregel HAELT eine Luecke, sie STELLT KEINE HER:
	// bei Abstand 0 setzt sie beiden Tempo 0, und der Stapel steht fuer immer.
	// Genau dieses Ineinanderstecken war im Spiel als "die Autos stapeln sich"
	// zu sehen.
	{
		// Vorderstes Fahrzeug je Bahn = kleinste Distanz auf ihr. Genau das
		// trifft ein Neuankoemmling, der am Anfang einsetzt.
		TMap<int64, double> FrontDistanceOnEdge;
		FrontDistanceOnEdge.Reserve(Vehicles.Num());
		for (const FTrafficVehicle& Vehicle : Vehicles)
		{
			if (Vehicle.bRemoved)
			{
				continue;
			}
			const int64 Key = EdgeKey(Vehicle.bOnLane,
				Vehicle.bOnLane ? Vehicle.LaneId : Vehicle.ConnectionIndex);
			double& Front = FrontDistanceOnEdge.FindOrAdd(Key, TNumericLimits<double>::Max());
			Front = FMath::Min(Front, Vehicle.DistanceCm);
		}

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

				bool bNextOnLane = false;
				int32 NextIndex = INDEX_NONE;
				if (!PeekNextEdge(Vehicle, bNextOnLane, NextIndex))
				{
					Vehicle.bRemoved = true;
					break;
				}

				// Wo saesse es auf der neuen Bahn? Der Ueberlauf ueber das Ende
				// der jetzigen ist die neue Distanz - dieselbe Rechnung wie in
				// AdvanceEdge.
				const double EntryDistance = Vehicle.DistanceCm - EdgeLength;
				const int64 NextKey = EdgeKey(bNextOnLane, NextIndex);
				const double* Front = FrontDistanceOnEdge.Find(NextKey);

				if (Front != nullptr && *Front < EntryDistance + MinGap)
				{
					// Besetzt: am Bahnende warten statt hineinzufahren.
					Vehicle.DistanceCm = FMath::Max(0.0, EdgeLength - 1.0);
					Vehicle.SpeedCmS = 0.0;
					break;
				}

				if (!AdvanceEdge(Vehicle))
				{
					Vehicle.bRemoved = true;
					break;
				}

				// Die neue Bahn ist jetzt (auch) von diesem Fahrzeug besetzt -
				// sonst faehrt der Naechste im selben Tick daneben hinein.
				double& NewFront = FrontDistanceOnEdge.FindOrAdd(
					NextKey, TNumericLimits<double>::Max());
				NewFront = FMath::Min(NewFront, Vehicle.DistanceCm);

				if (++Guard > MaxEdgeTransitionsPerTick)
				{
					Vehicle.bRemoved = true;
					break;
				}
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
		// gedauert. Eine Quelle fuer den Wert - die Diagnose liest denselben.
		const int32 TargetCount = GetTargetVehicleCount();

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

			// Einsatzort nach STRASSENKLASSE, nicht reihum.
			//
			// Reihum bekam jede Spur im Umkreis gleich viele Fahrzeuge. Weil
			// Wohn- und Servicestrassen die Hauptstrassen zahlenmaessig weit
			// uebertreffen, landete der Verkehr ueberwiegend in Seitenstrassen -
			// also gerade nicht dort, wo der Spieler faehrt. Die Gewichte sind
			// dieselben wie bei der Wahl an Kreuzungen (GetRoadClassWeight);
			// zwei Tabellen wuerden auseinanderlaufen.
			//
			// Ohne Beobachter bleibt es reihum: dort gibt es keinen Umkreis und
			// damit keine Gewichte - darauf stuetzen sich die datenreinen Tests.
			int32 SpawnSlot = INDEX_NONE;
			if (bHasObserver
				&& NearbySpawnCumulativeWeights.Num() == ActiveSpawnLanes.Num())
			{
				SpawnSlot = PickWeightedIndex(NearbySpawnCumulativeWeights,
					Hash2(static_cast<uint32>(TotalSpawned),
						static_cast<uint32>(Settings.RandomSeed)));
			}
			if (SpawnSlot == INDEX_NONE)
			{
				SpawnSlot = static_cast<int32>(TotalSpawned % ActiveSpawnLanes.Num());
			}
			const int32 SpawnLaneId = ActiveSpawnLanes[SpawnSlot];

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
		const bool bStalled = Vehicle.SpeedCmS < StallSpeedCmS
			&& Vehicle.DesiredSpeedCmS > StallSpeedCmS;
		if (bStalled)
		{
			++Stalled;
		}

		// Fluss je Spur mitschreiben (nur auf Anforderung). Fahrzeuge IN einer
		// Kreuzung zaehlen nicht mit: sie stehen dort wegen der Ampel, und das
		// wuerde jede signalisierte Kreuzung als Dauerstau einfaerben.
		if (bCollectLaneFlow && Vehicle.bOnLane
			&& Network->Lanes.IsValidIndex(Vehicle.LaneId))
		{
			FLaneFlowSample& Flow = LaneFlow.FindOrAdd(Vehicle.LaneId);
			++Flow.Samples;
			Flow.Stalled += bStalled ? 1 : 0;
			Flow.SpeedSumCmS += Vehicle.SpeedCmS;
			Flow.LimitCmS = Network->Lanes[Vehicle.LaneId].SpeedLimitKmh * KmhToCmS;
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
