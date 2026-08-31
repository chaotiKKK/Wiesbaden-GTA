// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/IHeightSampler.h"
#include "GIS/RoadNetworkTypes.h"
#include "GIS/WiesbadenRoadMarkings.h"
#include "GIS/WiesbadenTrafficSignCatalog.h"
#include "RoadFurnitureGenerator.generated.h"

class UGeoCoordinateConverter;

/** Art einer Fahrbahnmarkierung, die dieser Pass platziert. */
UENUM(BlueprintType)
enum class ERoadMarkingKind : uint8
{
	/** Haltlinie (Breitstrich) an Stopp-Kreuzungen. */
	StopLine    UMETA(DisplayName = "Haltlinie"),
	/** Wartelinie (unterbrochen) an Vorfahrt-gewaehren-Kreuzungen. */
	GiveWayLine UMETA(DisplayName = "Wartelinie"),
	MAX         UMETA(Hidden)
};

/** Ein platziertes Verkehrszeichen. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FSignInstance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FString SignId;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FWiesbadenTrafficSign Sign;

	/** Position der Schildunterkante in Weltkoordinaten (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FVector Location = FVector::ZeroVector;

	/** Blickrichtung des Schilds (zeigt in den ankommenden Verkehr). */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Quell-Segment (Tempolimit-Schilder), sonst INDEX_NONE. */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 SourceSegmentId = INDEX_NONE;

	/** Quell-OSM-Node (Kreuzungs-/Node-Schilder), sonst 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int64 SourceNodeId = 0;
};

/** Ein Leitpfosten (Delineator). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FDelineatorInstance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FVector Location = FVector::ZeroVector;

	/** Blickrichtung des Reflektors (quer zur Fahrbahn). */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 SegmentId = INDEX_NONE;
};

/** Eine Fahrbahnmarkierung (Halt-/Wartelinie). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FMarkingInstance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	ERoadMarkingKind Kind = ERoadMarkingKind::StopLine;

	/** Mittelpunkt der Linie in Weltkoordinaten (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FVector Center = FVector::ZeroVector;

	/** Richtung der Linienachse (normiert, quer zur Fahrbahn). */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FVector Direction = FVector::ForwardVector;

	/** Linienlaenge entlang Direction (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	double LengthCm = 0.0;

	/** Linienbreite/-dicke (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	double WidthCm = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int64 IntersectionNodeId = 0;
};

/**
 * Eine Strassenlaterne.
 *
 * Die Standorte stehen als highway=street_lamp in den OSM-Daten und wurden von
 * der Overpass-Abfrage schon immer mitgeholt - ausgewertet hat sie nur nie
 * jemand. Fuer Wiesbaden sind es rund 3.300 Stueck.
 *
 * Sichtbar wurde die Luecke erst, als die Tageszeit im Spiel weit genug
 * fortgeschritten war: Die Stadt hat dann KEINE Beleuchtung, und man faehrt
 * durch voellige Schwaerze.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FStreetLampInstance
{
	GENERATED_BODY()

	/** Fusspunkt des Masten in Weltkoordinaten (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FVector Location = FVector::ZeroVector;

	/** Knoten-Id der Quelle - fuer Nachvollziehbarkeit gegen die OSM-Daten. */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int64 NodeId = 0;
};

/** Vollstaendiges Ergebnis des Strassenausstattungs-Passes. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadFurnitureLayout
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	TArray<FSignInstance> Signs;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	TArray<FDelineatorInstance> Delineators;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	TArray<FMarkingInstance> Markings;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	TArray<FStreetLampInstance> StreetLamps;

	void Reset()
	{
		Signs.Reset();
		Delineators.Reset();
		Markings.Reset();
		StreetLamps.Reset();
	}

	FString GetStatisticsString() const
	{
		return FString::Printf(TEXT("%d Schilder, %d Leitpfosten, %d Markierungen"),
			Signs.Num(), Delineators.Num(), Markings.Num());
	}
};

/** Parameter des Strassenausstattungs-Passes. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadFurnitureSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bPlaceSigns = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bPlaceDelineators = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bPlaceMarkings = true;

	/** Strassenlaternen aus highway=street_lamp uebernehmen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bPlaceStreetLamps = true;

	/**
	 * Fehlende Strassenlaternen entlang befahrbarer Strassen ergaenzen.
	 *
	 * OSM hat fuer Wiesbaden nur 3.332 Leuchten erfasst - ein Bruchteil des
	 * Bestands. Gemessen lag die naechste kartierte Laterne 915 m vom
	 * Startpunkt entfernt, und dort ist nachts nichts zu sehen. Deutsche
	 * Ortsstrassen sind dagegen praktisch durchgaengig beleuchtet (DIN 13201).
	 *
	 * Ergaenzt wird nur, wo keine kartierte Leuchte in der Naehe steht - die
	 * echten Standorte behalten also Vorrang.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bSynthesiseMissingStreetLamps = true;

	/** Sollabstand der ergaenzten Leuchten in cm (30 m ist ueblich). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "500.0"))
	double StreetLampSpacingCm = 3000.0;

	/** Abstand des Masts vom Fahrbahnrand in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "0.0"))
	double StreetLampKerbOffsetCm = 120.0;

	/** Abstand der Leitpfosten (cm). Default: RMS 50 m auf Geraden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "10.0"))
	double DelineatorSpacingCm = WiesbadenRoadMarkings::DelineatorSpacingStraightCm;

	/** Seitlicher Abstand der Leitpfosten vom Fahrbahnrand (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "0.0"))
	double DelineatorOffsetCm = WiesbadenRoadMarkings::DelineatorOffsetFromEdgeCm;

	/** Seitlicher Abstand der Schilder vom Fahrbahnrand (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "0.0"))
	double SignLateralOffsetCm = WiesbadenRoadMarkings::SignLateralOffsetCm;

	/** Schildunterkante ueber dem Boden (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "0.0"))
	double SignHeightAboveGroundCm = WiesbadenRoadMarkings::SignHeightAboveWalkwayCm;

	/** Abstand des Schilds hinter der Kreuzungsgrenze (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "0.0"))
	double SignBacksetCm = 300.0;

	/** Abstand der Haltlinie vor der Kreuzungsgrenze (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "0.0"))
	double StopLineDistanceCm = 100.0;

	/** Dicke der Halt-/Wartelinie (cm). Default: Breitstrich 50 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "1.0"))
	double StopLineWidthCm = WiesbadenRoadMarkings::StopLineWidthCm;
};

/** Diagnose eines Strassenausstattungs-Laufs. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadFurnitureReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 SignCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 DelineatorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 MarkingCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 StreetLampCount = 0;

	/** Davon ergaenzt, weil OSM dort keine Leuchte kennt. */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 SynthesisedStreetLampCount = 0;

	/**
	 * Objekte, die auf einer Fahrbahn standen und entfernt wurden.
	 *
	 * Steht bewusst im Bericht: Ist die Zahl 0, arbeitet die Freiraeumung
	 * nicht - und das saehe man erst im Spiel, wenn man um ein Schild
	 * herumfahren muss, das mitten auf der Strasse steht.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 RemovedOnCarriagewayCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	double DurationSeconds = 0.0;

	FString ToString() const;
};

/**
 * Strassenausstattungs-Pass: platziert Verkehrszeichen, Leitpfosten und
 * Halt-/Wartelinien entlang des Strassennetzes.
 *
 * QUELLEN:
 *  - Kreuzungs-Kontrolle (Stop/Yield/Roundabout) -> 206/205/215 je Arm.
 *  - Segment-Tempolimit != 50 -> 274-x, verkehrsberuhigter Bereich -> 325.
 *  - Explizite OSM-`traffic_sign`-Tags an Nodes -> Katalog-Aufloesung.
 *
 * Der Pass erzeugt reine Platzierungsdaten (Position + Ausrichtung + Breite),
 * kein Rendering - ein spaeterer Spawner/Mesh-Builder konsumiert das Layout.
 * Zebrastreifen und Laengsmarkierungen bleiben beim RoadNetworkGenerator
 * (dort bereits ueber den Crossing-/LaneMarking-Kanal erzeugt) und werden hier
 * bewusst nicht dupliziert.
 */
UCLASS(BlueprintType)
class WIESBADENREAL_API URoadFurnitureGenerator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Fuehrt den Ausstattungs-Pass aus.
	 *
	 * @param Network      Erzeugtes Strassennetz.
	 * @param DataSet      OSM-Daten (optional): liefert explizite traffic_sign-Tags.
	 * @param Converter    Georeferenzierung (optional): noetig fuer Node-Schilder.
	 * @param HeightSampler Terrainhoehen (darf nullptr sein -> Z=0).
	 * @param Settings     Parameter.
	 * @param OutLayout    Ergebnis (Platzierungen).
	 */
	/**
	 * Ergaenzt Leuchten entlang befahrbarer Strassen, wo keine kartiert ist.
	 *
	 * Oeffentlich, weil datenrein pruefbar: der Test
	 * GIS.RoadFurniture.StreetLampSynthesis haelt Abstand, Strassenseite und
	 * den Vorrang kartierter Leuchten fest.
	 */
	void SynthesiseStreetLamps(
		const FRoadNetwork& Network,
		const IHeightSampler* HeightSampler,
		const FRoadFurnitureSettings& Settings,
		FRoadFurnitureLayout& OutLayout) const;

	FRoadFurnitureReport Generate(
		const FRoadNetwork& Network,
		const FOSMDataSet* DataSet,
		const UGeoCoordinateConverter* Converter,
		const IHeightSampler* HeightSampler,
		const FRoadFurnitureSettings& Settings,
		FRoadFurnitureLayout& OutLayout);

	/**
	 * Entfernt Schilder, Leitpfosten und Laternen, die auf einer Fahrbahn
	 * stehen. Markierungen bleiben - die gehoeren dorthin.
	 *
	 * @return Anzahl der entfernten Objekte.
	 */
	static int32 RemoveFurnitureOnCarriageway(
		const FRoadNetwork& Network, FRoadFurnitureLayout& Layout);

private:
	/** Uebernimmt die highway=street_lamp-Knoten als Laternenstandorte. */
	void PlaceStreetLamps(
		const FOSMDataSet* DataSet,
		const UGeoCoordinateConverter* Converter,
		const IHeightSampler* HeightSampler,
		const FRoadFurnitureSettings& Settings,
		FRoadFurnitureLayout& OutLayout) const;


	void PlaceSigns(
		const FRoadNetwork& Network,
		const FOSMDataSet* DataSet,
		const UGeoCoordinateConverter* Converter,
		const IHeightSampler* HeightSampler,
		const FRoadFurnitureSettings& Settings,
		FRoadFurnitureLayout& Layout);

	void PlaceDelineators(
		const FRoadNetwork& Network,
		const IHeightSampler* HeightSampler,
		const FRoadFurnitureSettings& Settings,
		FRoadFurnitureLayout& Layout);

	void PlaceMarkings(
		const FRoadNetwork& Network,
		const IHeightSampler* HeightSampler,
		const FRoadFurnitureSettings& Settings,
		FRoadFurnitureLayout& Layout);
};
