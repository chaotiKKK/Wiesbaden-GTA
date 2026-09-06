// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/IHeightSampler.h"
#include "GIS/OSMTypes.h"
#include "GIS/RoadNetworkTypes.h"
#include "WiesbadenPickupSpots.generated.h"

/**
 * Art eines Pickup-Standorts (datenrein).
 *
 * Getrennt von EWiesbadenPickupKind (World/WiesbadenPickup.h), damit die
 * GIS-Schicht nicht vom Gameplay-Actor abhaengt; der Spawner bildet sie
 * um. Die Zuordnung ist 1:1:
 *   Fuel   -> EWiesbadenPickupKind::Fuel
 *   Health -> EWiesbadenPickupKind::Health
 */
UENUM(BlueprintType)
enum class EWiesbadenPickupSpotKind : uint8
{
	/** Tankstelle (amenity=fuel) - Treibstoff fuer das Fahrzeug. */
	Fuel,
	/** Apotheke/Krankenhaus (amenity=pharmacy|hospital) - Gesundheit zu Fuss. */
	Health,
	MAX UMETA(Hidden)
};

/** Ein platzierter Pickup-Standort (Weltkoordinaten, cm). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenPickupSpot
{
	GENERATED_BODY()

	/** Art des Pickups an dieser Stelle. */
	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	EWiesbadenPickupSpotKind Kind = EWiesbadenPickupSpotKind::Fuel;

	/** Position in Weltkoordinaten (cm), bereits auf Terrainhoehe. */
	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	FVector Location = FVector::ZeroVector;

	/** OSM-Name der Quelle (z. B. "Esso-Station") - fuer Debug und HUD. */
	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	FString SourceName;

	/** OSM-Node-Id der Quelle (Rueckverfolgbarkeit). */
	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	int64 SourceNodeId = 0;

	/** True, wenn die Position auf eine Fahrbahn/Sidewalk-Kante gerastet wurde. */
	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	bool bSnappedToRoad = false;
};

/** Vollstaendiges Ergebnis des Pickup-Passes. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenPickupSpotLayout
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	TArray<FWiesbadenPickupSpot> Spots;

	void Reset() { Spots.Reset(); }

	FString GetStatisticsString() const;
};

/** Parameter des Pickup-Passes. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenPickupSpotSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickups")
	bool bPlaceFuelPickups = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickups")
	bool bPlaceHealthPickups = true;

	/**
	 * Maximale Reichweite zum Rasten auf eine Fahrspur (cm).
	 *
	 * Tankstellen-Applikatoren mappen die Station meist neben die Strasse;
	 * ohne Rasten stuende das Pickup im Hinterhof. Liegt kein Spurpunkt in
	 * der Reichweite, bleibt die Node-Position (stationseigener Hof).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickups", meta = (ClampMin = "0.0"))
	double SnapRadiusCm = 1500.0;

	/** Mindestabstand zweier Pickups derselben Art (cm) - duennt Cluster aus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickups", meta = (ClampMin = "0.0"))
	double MinSpacingCm = 10000.0;

	/** Obergrenze der Treibstoff-Pickups (Tankstellenzahl in Wiesbaden: ~40). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickups", meta = (ClampMin = "0"))
	int32 MaxFuelPickups = 32;

	/** Obergrenze der Gesundheits-Pickups (Apotheken sind deutlich dichter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickups", meta = (ClampMin = "0"))
	int32 MaxHealthPickups = 48;
};

/** Diagnose des Pickup-Passes. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FPickupSpotReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	int32 FuelCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	int32 HealthCount = 0;

	/** Nodes mit passendem amenity, die wegen MinSpacingCm weggelassen wurden. */
	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	int32 SkippedTooCloseCount = 0;

	/** Durch das Obergrenzen-Capping weggelassene Placements. */
	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	int32 SkippedOverCapCount = 0;

	/** Placements, die auf einen Spurpunkt gerastet wurden. */
	UPROPERTY(BlueprintReadOnly, Category = "Pickups")
	int32 SnappedToRoadCount = 0;

	FString ToString() const;
};

/**
 * Pickup-Spot-Pass: platziert Sammelobjekte an sinnvollen Orten der Stadt.
 *
 * Die Quelle sind die bereits geparsten OSM-Amenities - die Overpass-Query
 * holt amenity=fuel/pharmacy/hospital ohnehin als getaggte Nodes:
 *   - amenity=fuel                  -> Treibstoff-Pickup
 *   - amenity=pharmacy|hospital     -> Gesundheits-Pickup
 *
 * Die Position wird auf Terrainhoehe gehoben und - wenn ein Spurpunkt des
 * Strassennetzes in Reichweite liegt - auf die Sidewalk-Kante neben der
 * Fahrbahn gerastet, damit das Pickup erreichbar ist statt im Innenhof.
 *
 * Rein datenrein: erzeugt Platzierungsdaten, kein Rendering und keine Actors.
 * Der Spawner (World/WiesbadenPickupSpawnerComponent) konsumiert das Layout.
 */
UCLASS(BlueprintType)
class WIESBADENREAL_API UWiesbadenPickupSpotGenerator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Fuehrt den Pickup-Pass aus.
	 *
	 * @param OsmData       Geparster OSM-Datensatz (getaggte Nodes).
	 * @param Converter     Geo->Welt-Umrechnung (muss initialisiert sein).
	 * @param Network       Strassennetz zum Rasten; nullptr = keine Rastung.
	 * @param HeightSampler Terrainhoehen; nullptr = Z=0.
	 * @param Settings      Parameter.
	 * @param OutLayout     Ergebnis (Platzierungen).
	 * @return Bericht mit Kennzahlen (bSuccess ist immer true - der Pass
	 *         scheitert nicht, er liefert im Zweifel eine leere Liste).
	 */
	FPickupSpotReport Generate(
		const FOSMDataSet& OsmData,
		const UGeoCoordinateConverter& Converter,
		const FRoadNetwork* Network,
		const IHeightSampler* HeightSampler,
		const FWiesbadenPickupSpotSettings& Settings,
		FWiesbadenPickupSpotLayout& OutLayout);

	/** amenity-Tag -> Spot-Art; false, wenn die Node kein Pickup-Qualitaet hat. */
	static bool ClassifyNode(const FOSMNode& Node, EWiesbadenPickupSpotKind& OutKind);

	/**
	 * Naechster Punkt auf einer Spur-Mittellinie zum gegebenen Ort.
	 *
	 * @param OutPoint      Naechster Punkt auf der Sollbahn.
	 * @param OutLaneWidthCm Breite dieser Spur.
	 * @param OutSidewalkWidthCm Gehwegbreite des Segmentes (0 ohne Gehweg).
	 * @return Abstand in cm; TNumericLimits<double>::Max(), wenn das Netz leer ist.
	 */
	static double FindNearestLanePoint(
		const FRoadNetwork& Network, const FVector& WorldXY,
		FVector& OutPoint, double& OutLaneWidthCm, double& OutSidewalkWidthCm);

	/** Sidewalk-Kante neben dem Spurpunkt: Richtung vom Fahrbahnrand. */
	static FVector ComputeRoadsidePosition(
		const FVector& LanePoint, const FVector& NodeWorldXY,
		double LaneWidthCm, double SidewalkWidthCm);

private:
	/**
	 * Duennt eine Spot-Liste aus: Overrides hat den Mindestabstand zu jedem
	 * behaltenen Spot, darueber hinaus wird ab der Obergrenze abgeschnitten.
	 * Die Listen kommen in OSM-Map-Reihenfolge - bei der Grundgesamtheit
	 * deterministisch, auch wenn die Reihenfolge der Nodes nicht sortiert ist.
	 */
	static void ApplySpacingAndCap(
		TArray<FWiesbadenPickupSpot>& Spots, double MinSpacingCm, int32 MaxCount,
		int32& InOutSkippedTooClose, int32& InOutSkippedOverCap);
};
