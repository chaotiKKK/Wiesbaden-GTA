// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenHealthReport.h"

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

	// Ampel-Kopplung EVIDENZ-gewichtet: erst warnen, wenn genug Fahrzeuge eine
	// signalisierte Verbindung anfuhren (>=20) UND nie hielten. Bei ~75% Rot je
	// Kreuzung waere durchgehend Gruen dann astronomisch unwahrscheinlich. Wenige
	// Anfahrten -> kein Urteil (nur ~5% der Kreuzungen sind Ampeln).
	if (TrafficLightCount > 0 && VehiclesApproachingSignal >= 20 && VehiclesHeldAtRed == 0)
	{
		W.Add(TEXT("ampel-kopplung: viele Anfahrten auf signalisierte Verbindungen, aber KEIN Halten - defekt."));
	}

	// Gebaeude-Kollision: bei fertiger Stadt sollten Koerper um den Spieler aktiv
	// sein; 0 deutet auf einen abgeschalteten/leeren Pool (Durchfahren moeglich).
	if (bStreamingComplete && BuildingCollisionBodies == 0)
	{
		W.Add(TEXT("gebaeude-kollision: keine aktiven Koerper - Durchfahren moeglich."));
	}

	// Fussgaenger: bei fertiger Stadt simuliert, aber KEINER gezeichnet -> echter
	// Zeichnen-Defekt (Startup-Transient ist zum WbHealth-Zeitpunkt vorbei).
	if (bStreamingComplete && PedestriansSimulated > 0 && PedestriansDrawn == 0)
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
		TEXT("\"vehiclesApproachingSignal\": %d, ")
		TEXT("\"vehiclesHeldAtRed\": %d, ")
		TEXT("\"pedestriansSimulated\": %d, ")
		TEXT("\"pedestriansDrawn\": %d, ")
		TEXT("\"buildingCollisionBodies\": %d, ")
		TEXT("\"healthy\": %s, ")
		TEXT("\"warnings\": [%s]")
		TEXT("}"),
		B(bCityLoaded), B(bStreamingComplete), TrafficLightCount, ActiveVehicles,
		VehiclesApproachingSignal, VehiclesHeldAtRed, PedestriansSimulated,
		PedestriansDrawn, BuildingCollisionBodies, B(W.Num() == 0), *WarnJson);
}
