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

	for (int32 LightIndex = 0; LightIndex < InNetwork.Intersections.Num(); ++LightIndex)
	{
		const FRoadIntersection& Intersection = InNetwork.Intersections[LightIndex];
		if (Intersection.Control != EIntersectionControl::TrafficSignals)
		{
			continue;
		}

		FWiesbadenTrafficLight Light;
		Light.NodeId = Intersection.NodeId;
		Light.Location = Intersection.Location;
		Light.GroupCount = FMath::Max(Intersection.GetArmCount(), 1);

		// Deterministischer Phasen-Offset aus Node-Id + Seed, damit nicht alle
		// Ampeln der Stadt synchron schalten.
		Light.PhaseOffsetSeconds = HashFraction(
			Hash2(static_cast<uint32>(Intersection.NodeId), static_cast<uint32>(Settings.RandomSeed)))
			* Settings.CycleSeconds;

		// Connections dieser Kreuzung einer Richtungsgruppe zuordnen.
		for (int32 ConnectionIndex = 0; ConnectionIndex < InNetwork.Connections.Num(); ++ConnectionIndex)
		{
			const FLaneConnection& Connection = InNetwork.Connections[ConnectionIndex];
			if (Connection.IntersectionNodeId != Intersection.NodeId)
			{
				continue;
			}
			if (!InNetwork.Lanes.IsValidIndex(Connection.FromLaneId))
			{
				continue;
			}

			const int32 Group = ComputeGroupIndex(InNetwork.Lanes[Connection.FromLaneId], Light.GroupCount);
			Light.ConnectionGroups.Add(ConnectionIndex, Group);
			ConnectionToLight.Add(ConnectionIndex, LightIndex);
		}

		Lights.Add(Light);
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

bool FWiesbadenTrafficLightSystem::IsConnectionGreen(int32 ConnectionIndex) const
{
	const int32* LightIdx = ConnectionToLight.Find(ConnectionIndex);
	if (!LightIdx || !Lights.IsValidIndex(*LightIdx))
	{
		// Keine Ampel an dieser Verbindung - keine Einschraenkung.
		return true;
	}

	const FWiesbadenTrafficLight& Light = Lights[*LightIdx];
	const int32* GroupPtr = Light.ConnectionGroups.Find(ConnectionIndex);
	if (!GroupPtr || Light.GroupCount <= 0)
	{
		return true;
	}

	// Zeitfenster der Gruppe im Zyklus: Gruppe g ist gruen im Fenster
	// [g * Slot, g * Slot + Green), Slot = Cycle / GroupCount (nicht
	// ueberlappend - nie zwei Achsen gleichzeitig gruen).
	const double Cycle = FMath::Max(Settings.CycleSeconds, MinCycleSeconds);
	const double Slot = Cycle / static_cast<double>(Light.GroupCount);
	const double Green = FMath::Clamp(Settings.GreenSecondsPerCycle, 0.0, Slot);
	const double CyclePhase = FMath::Fmod(ElapsedSeconds + Light.PhaseOffsetSeconds, Cycle);
	if (CyclePhase < 0.0)
	{
		return false;
	}

	const double SlotStart = static_cast<double>(*GroupPtr) * Slot;
	return CyclePhase >= SlotStart && CyclePhase < SlotStart + Green;
}
