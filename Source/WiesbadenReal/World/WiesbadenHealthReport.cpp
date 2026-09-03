// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenHealthReport.h"

EWiesbadenTrafficLightVerdict FWiesbadenHealthReport::TrafficLightVerdict() const
{
	// Verdikt auf FAHR-Evidenz statt Geometrie: eine Ampel im Netz heisst nicht,
	// dass der Verkehr sie auch befaehrt. Nur ~5% der Kreuzungen sind Ampeln, der
	// Verkehr um den Spieler quert meist ampellose Knoten - dann ist "0 an Rot"
	// KEIN Fehler. Erst genug Anfahrten (>=20) OHNE ein einziges Halten belegen bei
	// ~75% Rot-Anteil je Kreuzung einen echten Kopplungs-Defekt.
	if (TrafficLightCount <= 0)
	{
		return EWiesbadenTrafficLightVerdict::Inconclusive;
	}
	if (VehiclesHeldAtRed > 0)
	{
		return EWiesbadenTrafficLightVerdict::Effective;
	}
	if (VehiclesApproachingSignal >= 20)
	{
		return EWiesbadenTrafficLightVerdict::Broken;
	}
	return EWiesbadenTrafficLightVerdict::Inconclusive;
}

TArray<FString> FWiesbadenHealthReport::Warnings() const
{
	TArray<FString> W;

	if (!bCityLoaded)
	{
		// Ohne geladene Stadt sind alle Null-Werte normal - keine Warnung.
		return W;
	}

	if (!bStreamingComplete)
	{
		W.Add(TEXT("streaming: noch nicht abgeschlossen (Zellen laden)."));
	}

	// Ampel-Kopplung: das Drei-Wege-Verdikt liegt zentral in TrafficLightVerdict();
	// gewarnt wird nur beim echten Defekt (Broken).
	if (TrafficLightVerdict() == EWiesbadenTrafficLightVerdict::Broken)
	{
		W.Add(TEXT("ampel-kopplung: viele Anfahrten auf signalisierte Verbindungen, aber KEIN Halten - defekt."));
	}

	// Gebaeude-Kollision: bei fertiger Stadt sollten Koerper um den Spieler aktiv
	// sein; 0 deutet auf einen abgeschalteten/leeren Pool (Durchfahren moeglich).
	if (bStreamingComplete && BuildingCollisionBodies == 0)
	{
		W.Add(TEXT("gebaeude-kollision: keine aktiven Koerper - Durchfahren moeglich."));
	}

	// Verkehr: bei fertiger Stadt simuliert, aber KEINES gezeichnet -> Traeger/Mesh/
	// Cull-Defekt. Spiegelt die Inline-Bilanz, die genau dieses Praedikat liest.
	if (bStreamingComplete && HasTrafficDrawDefect())
	{
		W.Add(TEXT("verkehr: simuliert, aber KEINES gezeichnet - Traeger/Mesh/Cull fehlt."));
	}

	// Fussgaenger: bei fertiger Stadt simuliert, aber KEINER gezeichnet -> echter
	// Zeichnen-Defekt (Startup-Transient ist zum WbHealth-Zeitpunkt vorbei).
	if (bStreamingComplete && HasPedestrianDrawDefect())
	{
		W.Add(TEXT("fussgaenger: simuliert, aber KEINER gezeichnet - Mesh/Spawner fehlt."));
	}

	return W;
}

FString FWiesbadenHealthReport::ToJson() const
{
	auto B = [](bool b) { return b ? TEXT("true") : TEXT("false"); };

	FString WarnJson;
	const TArray<FString> W = Warnings();
	for (int32 i = 0; i < W.Num(); ++i)
	{
		WarnJson += FString::Printf(TEXT("%s\"%s\""), (i > 0 ? TEXT(", ") : TEXT("")), *W[i]);
	}

	return FString::Printf(
		TEXT("{")
		TEXT("\"cityLoaded\": %s, ")
		TEXT("\"streamingComplete\": %s, ")
		TEXT("\"trafficLightCount\": %d, ")
		TEXT("\"activeVehicles\": %d, ")
		TEXT("\"trafficVehiclesVisible\": %d, ")
		TEXT("\"vehiclesApproachingSignal\": %d, ")
		TEXT("\"vehiclesHeldAtRed\": %d, ")
		TEXT("\"pedestriansSimulated\": %d, ")
		TEXT("\"pedestriansDrawn\": %d, ")
		TEXT("\"buildingCollisionBodies\": %d, ")
		TEXT("\"healthy\": %s, ")
		TEXT("\"warnings\": [%s]")
		TEXT("}"),
		B(bCityLoaded), B(bStreamingComplete), TrafficLightCount, ActiveVehicles,
		TrafficVehiclesVisible, VehiclesApproachingSignal, VehiclesHeldAtRed,
		PedestriansSimulated, PedestriansDrawn, BuildingCollisionBodies,
		B(W.Num() == 0), *WarnJson);
}
