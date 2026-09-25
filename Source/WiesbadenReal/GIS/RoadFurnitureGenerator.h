// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/IHeightSampler.h"
#include "GIS/RoadNetworkTypes.h"
#include "GIS/WiesbadenRoadMarkings.h"
#include "GIS/WiesbadenTrafficSignCatalog.h"
#include "RoadFurnitureGenerator.generated.h"

class UGeoCoordinateConverter;
struct FGeneratedBuilding;

/** Art einer Fahrbahnmarkierung, die dieser Pass platziert. */
UENUM(BlueprintType)
enum class ERoadMarkingKind : uint8
{
	/** Haltlinie (Breitstrich) an Stopp-Kreuzungen. */
	StopLine    UMETA(DisplayName = "Haltlinie"),
	/** Wartelinie (unterbrochen) an Vorfahrt-gewaehren-Kreuzungen. */
	GiveWayLine UMETA(DisplayName = "Wartelinie"),
	/** Auf die Fahrbahn gemalte "30" in Tempo-30-Zonen (Symbol, keine Linie). */
	SpeedZone30 UMETA(DisplayName = "Zone-30-Symbol"),
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

/**
 * Art eines Strassenmoebels aus OSM.
 *
 * Die acht Kategorien des Specs "Strassenrand-Schmuck" - genau die, die in
 * Wiesbaden mit mehr als hundert Knoten erfasst sind. Die Zuordnung
 * Tag -> Art trifft der Nachzug `Tools/fetch_street_furniture.py`; er schreibt
 * sie als EIN Tag `wb:furniture` in die OSM-Kopie, damit hier keine acht
 * Tag-Kombinationen nachgebaut werden muessen.
 */
UENUM(BlueprintType)
enum class EStreetFurnitureKind : uint8
{
	Bench          UMETA(DisplayName = "Bank"),
	Bollard        UMETA(DisplayName = "Poller"),
	WasteBasket    UMETA(DisplayName = "Abfallkorb"),
	VendingMachine UMETA(DisplayName = "Automat"),
	Recycling      UMETA(DisplayName = "Recycling-Container"),
	FireHydrant    UMETA(DisplayName = "Hydrant"),
	PostBox        UMETA(DisplayName = "Briefkasten"),
	PicnicTable    UMETA(DisplayName = "Picknick-Tisch"),
	MAX            UMETA(Hidden)
};

/** Ein platziertes Strassenmoebel (Bank, Poller, Korb, ...). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FFurnitureInstance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	EStreetFurnitureKind Kind = EStreetFurnitureKind::Bench;

	/** Standflaeche in Weltkoordinaten (cm) - Unterkante des Objekts. */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Mesh-Variante der Art (0-basiert), deterministisch aus der Knoten-Id. */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 Variant = 0;

	/** Quell-OSM-Node - jedes Moebel steht an einem echten kartierten Ort. */
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

	/** Strassenmoebel aus OSM (Baenke, Poller, Koerbe, ...). */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	TArray<FFurnitureInstance> Furniture;

	void Reset()
	{
		Signs.Reset();
		Delineators.Reset();
		Markings.Reset();
		StreetLamps.Reset();
		Furniture.Reset();
	}

	FString GetStatisticsString() const
	{
		return FString::Printf(TEXT("%d Schilder, %d Leitpfosten, %d Markierungen, %d Moebel"),
			Signs.Num(), Delineators.Num(), Markings.Num(), Furniture.Num());
	}

	/**
	 * Enthaelt das Layout ueberhaupt etwas?
	 *
	 * Steht hier und nicht als Aufzaehlung an der Aufrufstelle: der Spawner
	 * wurde dort mit "Schilder ODER Leitpfosten ODER Markierungen" bewacht.
	 * Als die Laternen dazukamen und spaeter die Moebel, blieb die Bedingung
	 * stehen - eine Stadt, die NUR Moebel hat, haette ihren Spawner nie
	 * gerufen. Eine Stelle, die alle Kanaele kennt, kann das nicht passieren.
	 */
	bool IsEmpty() const
	{
		return Signs.Num() == 0
			&& Delineators.Num() == 0
			&& Markings.Num() == 0
			&& StreetLamps.Num() == 0
			&& Furniture.Num() == 0;
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

	/** Strassenmoebel (Baenke, Poller, Koerbe, ...) aus OSM uebernehmen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bPlaceStreetFurniture = true;

	/**
	 * Wie weit ein Moebel hoechstens an den befestigten Rand rueckt (cm).
	 *
	 * OSM verortet Baenke und Koerbe oft einen Meter daneben - auf der Wiese
	 * oder halb im Strassenkoerper. Bis zu diesem Abstand wird das Objekt auf
	 * den naechsten Gehweg gezogen, darueber hinaus verworfen: ein Moebel, das
	 * weit ab vom befestigten Rand steht, ist ein Datenfehler und kein Ort.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "0.0"))
	double FurnitureDockingRangeCm = 250.0;

	/**
	 * Bankett neben Wegen OHNE Gehweg (cm) - die eigentliche Kalibrierung.
	 *
	 * GEMESSEN am 20.09.2026: von 3607 Moebelknoten fanden 1464 keinen
	 * befestigten Rand in Reichweite. 78 Prozent davon liegen an
	 * `footway`, `path` und `track` - Wegtypen, denen RoadTypeLibrary die
	 * Gehwegbreite 0,0 gibt. Das ist dort RICHTIG (ein Fussweg hat keinen
	 * Gehweg), macht den "befestigten Streifen" aber nur so breit wie der Weg
	 * selbst: bei 1,80 m Fussweg endet er 0,90 m von der Achse, und eine Bank
	 * einen Meter daneben liegt schon ausserhalb.
	 *
	 * In Wirklichkeit steht neben einem Fussweg ein begehbarer Streifen -
	 * Bankett, Rasenkante, wassergebundene Decke. Genau dort stehen Baenke,
	 * Koerbe und Papierkoerbe. Dieses Mass gilt ERSATZWEISE als
	 * Gehwegbreite, wenn der Wegtyp keine hat.
	 *
	 * NEBENWIRKUNG, und zwar die erwuenschte: was innerhalb dieses Streifens
	 * liegt, wird NICHT mehr herangezogen, sondern bleibt, wo OSM es verortet
	 * hat. Vorher rueckten solche Objekte im Median 2,08 m von ihrer
	 * kartierten Stelle weg.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "0.0"))
	double FurnitureVergeCm = 250.0;

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

	/**
	 * Hoehe der Fahrbahnoberkante ueber dem Terrain (cm).
	 *
	 * MUSS mit FRoadNetworkSettings::RoadSurfaceOffsetCm uebereinstimmen: Die
	 * Strassen-/Kreuzungsgeometrie wird um genau diesen Betrag ueber das Terrain
	 * gehoben. Die Ausstattung sampelte bisher die ROHE Terrainhoehe und stand
	 * damit um diesen Betrag IM Boden ("Ampeln/Schilder stecken im Boden").
	 * Hier durchgereicht, damit Ausstattung und Fahrbahn dieselbe Referenz haben.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "0.0"))
	double RoadSurfaceOffsetCm = 20.0;

	/**
	 * Bordsteinhoehe (cm) - Gehwegoberkante liegt bei Terrain + RoadSurfaceOffset
	 * + KerbHeight. MUSS mit FRoadSegment::KerbHeightCm uebereinstimmen. Schilder,
	 * Laternen und Leitpfosten stehen auf dem Gehweg, also um diesen Betrag hoeher
	 * als die Fahrbahn; Markierungen liegen auf der Fahrbahn (ohne Bordstein).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture", meta = (ClampMin = "0.0"))
	double KerbHeightCm = 4.0;

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

	/** Uebernommene Strassenmoebel. */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 FurnitureCount = 0;

	/** Moebel, die im Grundriss eines Gebaeudes lagen (OSM-Verortungsfehler). */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 FurnitureInBuildingCount = 0;

	/** Moebel, die auf den befestigten Rand gerueckt wurden. */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 FurnitureDockedCount = 0;

	/** Moebel ohne befestigten Rand in Reichweite - verworfen. */
	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	int32 FurnitureWithoutEdgeCount = 0;

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

	/**
	 * Uebernimmt die OSM-Strassenmoebel (wb:furniture-Knoten).
	 *
	 * Oeffentlich aus demselben Grund wie SynthesiseStreetLamps: der Pass ist
	 * datenrein pruefbar. Der Test GIS.RoadFurniture.StreetFurniture haelt die
	 * Platzierungsregeln fest (Gebaeude, Fahrbahn, Andocken, Ausrichtung,
	 * Determinismus).
	 *
	 * @param Buildings Gebaeude des Builds (optional): deren Grundriss-Boxen
	 *                  fangen die typischen OSM-Verortungsfehler ab.
	 */
	void PlaceStreetFurniture(
		const FRoadNetwork& Network,
		const FOSMDataSet* DataSet,
		const UGeoCoordinateConverter* Converter,
		const IHeightSampler* HeightSampler,
		const TArray<FGeneratedBuilding>* Buildings,
		const FRoadFurnitureSettings& Settings,
		FRoadFurnitureLayout& OutLayout,
		FRoadFurnitureReport& OutReport) const;

	/** Zahl der Mesh-Varianten einer Art (Variantenwahl im Bake). */
	static int32 GetFurnitureVariantCount(EStreetFurnitureKind Kind);

	/** Wandelt das `wb:furniture`-Tag in eine Art. False, wenn unbekannt. */
	static bool TryParseFurnitureKind(const FString& Tag, EStreetFurnitureKind& OutKind);

	FRoadFurnitureReport Generate(
		const FRoadNetwork& Network,
		const FOSMDataSet* DataSet,
		const UGeoCoordinateConverter* Converter,
		const IHeightSampler* HeightSampler,
		const FRoadFurnitureSettings& Settings,
		FRoadFurnitureLayout& OutLayout,
		const TArray<FGeneratedBuilding>* Buildings = nullptr);

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
