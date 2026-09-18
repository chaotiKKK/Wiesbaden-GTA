// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenTrafficLights.h"

namespace
{
	// Schutz vor degenerierten Einstellungen.
	constexpr double MinCycleSeconds = 2.0;
}

uint32 FWiesbadenTrafficLightSystem::Hash2(uint32 A, uint32 B)
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

float FWiesbadenTrafficLightSystem::HashFraction(uint32 Hash)
{
	return static_cast<float>(Hash % 1000u) / 1000.0f;
}

int32 FWiesbadenTrafficLightSystem::ComputeGroupIndex(const FRoadLane& Lane, int32 GroupCount) const
{
	if (GroupCount <= 1 || Lane.Centerline.Num() < 2)
	{
		return 0;
	}

	// Richtung der ankommenden Spur am Kreuzungsende (letztes Liniensegment).
	const FVector Last = Lane.Centerline.Last();
	const FVector Prev = Lane.Centerline[Lane.Centerline.Num() - 2];
	const double BearingDeg = FMath::RadiansToDegrees(FMath::Atan2(Last.Y - Prev.Y, Last.X - Prev.X));

	// Zwei Hauptachsen (0/180 und 90/270) - der Richtungsgruppen-Index ist der
	// Achsen-Slot. Mehr als 2 Gruppen werden auf 2 aufgeteilt (Slot = Index mod
	// GroupCount ist bei GroupCount=2 identisch zur Achse).
	const int32 Axis = (BearingDeg >= 0.0 && BearingDeg < 180.0) ? 0 : 1;
	return Axis % GroupCount;
}

void FWiesbadenTrafficLightSystem::Initialize(
	const FRoadNetwork& InNetwork, const FWiesbadenTrafficLightSettings& InSettings)
{
	Network = &InNetwork;
	Settings = InSettings;
	Settings.CycleSeconds = FMath::Max(Settings.CycleSeconds, MinCycleSeconds);
	Lights.Reset();
	ConnectionToLight.Reset();
	ElapsedSeconds = 0.0;

	// Connections einmal nach Kreuzungsknoten buendeln. Vorher suchte jede
	// Ampel das GANZE Connections-Array ab (1073 Ampeln x Hunderttausende
	// Verbindungen); jetzt ein Durchgang plus direkter Zugriff.
	TMap<int64, TArray<int32>> ConnectionsByNode;
	ConnectionsByNode.Reserve(InNetwork.Intersections.Num());
	for (int32 ConnectionIndex = 0; ConnectionIndex < InNetwork.Connections.Num(); ++ConnectionIndex)
	{
		ConnectionsByNode.FindOrAdd(InNetwork.Connections[ConnectionIndex].IntersectionNodeId)
			.Add(ConnectionIndex);
	}

	for (const FRoadIntersection& Intersection : InNetwork.Intersections)
	{
		if (Intersection.Control != EIntersectionControl::TrafficSignals)
		{
			continue;
		}

		// Index der Ampel im GEFILTERTEN Lights-Array - nicht der Laufindex
		// ueber Intersections. Genau diese Verwechslung war der Kopplungs-
		// Defekt: nur ~1073 von ~20213 Kreuzungen sind Ampeln, der
		// Intersections-Index lag also fast immer ausserhalb von Lights,
		// GetConnectionAspect fiel auf "gruen" zurueck und kein Fahrzeug
		// hielt je an Rot.
		const int32 LightIndex = Lights.Num();

		FWiesbadenTrafficLight Light;
		Light.NodeId = Intersection.NodeId;
		Light.Location = Intersection.Location;
		Light.GroupCount = 2; // Zwei Achsen-Gruppen (0/180 vs 90/270).

		// Deterministischer Phasen-Offset aus Node-Id + Seed, damit nicht alle
		// Ampeln der Stadt synchron schalten.
		Light.PhaseOffsetSeconds = HashFraction(
			Hash2(static_cast<uint32>(Intersection.NodeId), static_cast<uint32>(Settings.RandomSeed)))
			* Settings.CycleSeconds;

		// Connections dieser Kreuzung einer Richtungsgruppe zuordnen.
		if (const TArray<int32>* NodeConnections = ConnectionsByNode.Find(Intersection.NodeId))
		{
			for (const int32 ConnectionIndex : *NodeConnections)
			{
				const FLaneConnection& Connection = InNetwork.Connections[ConnectionIndex];
				if (!InNetwork.Lanes.IsValidIndex(Connection.FromLaneId))
				{
					continue;
				}

				const int32 Group = ComputeGroupIndex(InNetwork.Lanes[Connection.FromLaneId], Light.GroupCount);
				Light.ConnectionGroups.Add(ConnectionIndex, Group);
				ConnectionToLight.Add(ConnectionIndex, LightIndex);
			}
		}

		Lights.Add(MoveTemp(Light));
	}
}

void FWiesbadenTrafficLightSystem::Reset()
{
	Network = nullptr;
	Lights.Reset();
	ConnectionToLight.Reset();
	ElapsedSeconds = 0.0;
	Settings = FWiesbadenTrafficLightSettings();
}

void FWiesbadenTrafficLightSystem::Tick(float DeltaSeconds)
{
	if (DeltaSeconds > 0.0f)
	{
		ElapsedSeconds += DeltaSeconds;
	}
}

bool FWiesbadenTrafficLightSystem::HasTrafficLightAt(int64 NodeId) const
{
	for (const FWiesbadenTrafficLight& Light : Lights)
	{
		if (Light.NodeId == NodeId)
		{
			return true;
		}
	}
	return false;
}

ESignalAspect FWiesbadenTrafficLightSystem::GetGroupAspect(int32 LightIndex, int32 Group) const
{
    if (!Lights.IsValidIndex(LightIndex))
    {
        return ESignalAspect::Green;
    }
    const FWiesbadenTrafficLight& Light = Lights[LightIndex];
    const int32 Groups = FMath::Max(Light.GroupCount, 1);

    const double Cycle = FMath::Max(Settings.CycleSeconds, MinCycleSeconds);
    const double Half = Cycle / static_cast<double>(Groups);
    double Phase = FMath::Fmod(ElapsedSeconds + Light.PhaseOffsetSeconds, Cycle);
    if (Phase < 0.0)
    {
        Phase += Cycle;
    }

    // Nur die gerade aktive Achse ist ueberhaupt nicht-rot.
    const int32 Active = FMath::Clamp(static_cast<int32>(Phase / Half), 0, Groups - 1);
    if (Group != Active)
    {
        return ESignalAspect::Red;
    }

    const double RA = FMath::Max(Settings.RedAmberSeconds, 0.0);
    const double AM = FMath::Max(Settings.AmberSeconds, 0.0);
    const double AR = FMath::Max(Settings.AllRedSeconds, 0.0);
    const double GreenAvail = FMath::Max(Half - RA - AM - AR, 0.0);
    const double G = FMath::Clamp(Settings.GreenSecondsPerCycle, 0.0, GreenAvail);

    const double T = Phase - static_cast<double>(Active) * Half;
    if (T < RA) { return ESignalAspect::RedAmber; }
    if (T < RA + G) { return ESignalAspect::Green; }
    if (T < RA + G + AM) { return ESignalAspect::Amber; }
    return ESignalAspect::Red; // Allrot-Raeumzeit
}

ESignalAspect FWiesbadenTrafficLightSystem::GetConnectionAspect(int32 ConnectionIndex) const
{
    const int32* LightIdx = ConnectionToLight.Find(ConnectionIndex);
    if (!LightIdx || !Lights.IsValidIndex(*LightIdx))
    {
        return ESignalAspect::Green;
    }
    const FWiesbadenTrafficLight& Light = Lights[*LightIdx];
    const int32* GroupPtr = Light.ConnectionGroups.Find(ConnectionIndex);
    if (!GroupPtr)
    {
        return ESignalAspect::Green;
    }
    return GetGroupAspect(*LightIdx, *GroupPtr);
}

bool FWiesbadenTrafficLightSystem::IsConnectionGreen(int32 ConnectionIndex) const
{
    return GetConnectionAspect(ConnectionIndex) == ESignalAspect::Green;
}

bool FWiesbadenTrafficLightSystem::AnyControlledConnectionRed() const
{
	// Frueh-Ausstieg beim ersten Rot: bei staffelphasigen Kreuzungen ist fast
	// immer eine kontrollierte Verbindung rot, die Schleife bricht praktisch
	// sofort ab. Die Sim ruft das nur, bis EINMAL Rot beobachtet wurde.
	for (const TPair<int32, int32>& Pair : ConnectionToLight)
	{
		if (!IsConnectionGreen(Pair.Key))
		{
			return true;
		}
	}
	return false;
}
