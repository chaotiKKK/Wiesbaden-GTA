// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/GeoCoordinateConverter.h"
#include "OSMTypes.generated.h"

/** OSM-Element-Identifier. OSM-IDs uebersteigen 2^31, daher zwingend int64. */
using FOSMId = int64;

/**
 * Strassenklassifikation nach OSM highway=*.
 * Reihenfolge entspricht absteigender Verkehrsbedeutung - der Wert wird direkt
 * als Vorfahrts-Prioritaet in der Kreuzungslogik verwendet (kleiner = wichtiger).
 */
UENUM(BlueprintType)
enum class EOSMHighwayType : uint8
{
	Motorway		UMETA(DisplayName = "Autobahn"),
	MotorwayLink	UMETA(DisplayName = "Autobahn-Auffahrt"),
	Trunk			UMETA(DisplayName = "Kraftfahrstrasse"),
	TrunkLink		UMETA(DisplayName = "Kraftfahrstrasse-Auffahrt"),
	Primary			UMETA(DisplayName = "Bundesstrasse"),
	PrimaryLink		UMETA(DisplayName = "Bundesstrasse-Verbindung"),
	Secondary		UMETA(DisplayName = "Landesstrasse"),
	SecondaryLink	UMETA(DisplayName = "Landesstrasse-Verbindung"),
	Tertiary		UMETA(DisplayName = "Kreisstrasse"),
	TertiaryLink	UMETA(DisplayName = "Kreisstrasse-Verbindung"),
	Unclassified	UMETA(DisplayName = "Unklassifiziert"),
	Residential		UMETA(DisplayName = "Wohnstrasse"),
	LivingStreet	UMETA(DisplayName = "Verkehrsberuhigter Bereich"),
	Service			UMETA(DisplayName = "Erschliessungsweg"),
	Pedestrian		UMETA(DisplayName = "Fussgaengerzone"),
	Footway			UMETA(DisplayName = "Fussweg"),
	Cycleway		UMETA(DisplayName = "Radweg"),
	Path			UMETA(DisplayName = "Pfad"),
	Steps			UMETA(DisplayName = "Treppe"),
	Track			UMETA(DisplayName = "Wirtschaftsweg"),
	None			UMETA(DisplayName = "Keine Strasse"),
	MAX				UMETA(Hidden)
};

/** Oberflaechenmaterial nach OSM surface=*. Bestimmt Material und Reibungswert. */
UENUM(BlueprintType)
enum class EOSMSurfaceType : uint8
{
	Asphalt			UMETA(DisplayName = "Asphalt"),
	Concrete		UMETA(DisplayName = "Beton"),
	PavingStones	UMETA(DisplayName = "Pflasterstein"),
	Sett			UMETA(DisplayName = "Behauener Naturstein"),
	Cobblestone		UMETA(DisplayName = "Kopfsteinpflaster"),
	Gravel			UMETA(DisplayName = "Schotter"),
	Compacted		UMETA(DisplayName = "Verdichteter Kies"),
	Ground			UMETA(DisplayName = "Erde"),
	Grass			UMETA(DisplayName = "Gras"),
	Wood			UMETA(DisplayName = "Holz"),
	Metal			UMETA(DisplayName = "Metall"),
	Unknown			UMETA(DisplayName = "Unbekannt"),
	MAX				UMETA(Hidden)
};

/** Gehweg-Konfiguration nach OSM sidewalk=*. */
UENUM(BlueprintType)
enum class EOSMSidewalkType : uint8
{
	None		UMETA(DisplayName = "Kein Gehweg"),
	Left		UMETA(DisplayName = "Links"),
	Right		UMETA(DisplayName = "Rechts"),
	Both		UMETA(DisplayName = "Beidseitig"),
	Separate	UMETA(DisplayName = "Separat gemappt"),
	MAX			UMETA(Hidden)
};

/** Einbahnstrassen-Zustand nach OSM oneway=*. */
UENUM(BlueprintType)
enum class EOSMOnewayType : uint8
{
	/** Beide Richtungen befahrbar. */
	No			UMETA(DisplayName = "Keine Einbahnstrasse"),
	/** Nur in Way-Richtung befahrbar (oneway=yes). */
	Forward		UMETA(DisplayName = "In Way-Richtung"),
	/** Nur gegen Way-Richtung befahrbar (oneway=-1). */
	Backward	UMETA(DisplayName = "Gegen Way-Richtung"),
	/** Wechselnde Richtung (oneway=reversible) - wird als Forward behandelt. */
	Reversible	UMETA(DisplayName = "Wechselnd"),
	MAX			UMETA(Hidden)
};

/** Gebaeudeklassifikation nach OSM building=*. */
UENUM(BlueprintType)
enum class EOSMBuildingType : uint8
{
	Residential		UMETA(DisplayName = "Wohnhaus"),
	Apartments		UMETA(DisplayName = "Mehrfamilienhaus"),
	House			UMETA(DisplayName = "Einfamilienhaus"),
	Detached		UMETA(DisplayName = "Freistehendes Haus"),
	Office			UMETA(DisplayName = "Buerogebaeude"),
	Commercial		UMETA(DisplayName = "Geschaeftsgebaeude"),
	Retail			UMETA(DisplayName = "Einzelhandel"),
	Industrial		UMETA(DisplayName = "Industriegebaeude"),
	Warehouse		UMETA(DisplayName = "Lagerhalle"),
	Church			UMETA(DisplayName = "Kirche"),
	Civic			UMETA(DisplayName = "Oeffentliches Gebaeude"),
	School			UMETA(DisplayName = "Schule"),
	University		UMETA(DisplayName = "Universitaet"),
	Hospital		UMETA(DisplayName = "Krankenhaus"),
	TrainStation	UMETA(DisplayName = "Bahnhof"),
	Hotel			UMETA(DisplayName = "Hotel"),
	Garage			UMETA(DisplayName = "Garage"),
	Roof			UMETA(DisplayName = "Ueberdachung"),
	Generic			UMETA(DisplayName = "Allgemein"),
	MAX				UMETA(Hidden)
};

/** Dachform nach OSM roof:shape=*. */
UENUM(BlueprintType)
enum class EOSMRoofShape : uint8
{
	Flat		UMETA(DisplayName = "Flachdach"),
	Gabled		UMETA(DisplayName = "Satteldach"),
	Hipped		UMETA(DisplayName = "Walmdach"),
	Pyramidal	UMETA(DisplayName = "Zeltdach"),
	Skillion	UMETA(DisplayName = "Pultdach"),
	Dome		UMETA(DisplayName = "Kuppel"),
	MAX			UMETA(Hidden)
};

/**
 * Ein OSM-Node. Traegt Position und optional Tags (Ampeln, Zebrastreifen,
 * Bushaltestellen).
 *
 * Speichergroesse ist kritisch: Wiesbaden enthaelt > 400.000 Nodes. Tags
 * werden daher nur fuer getaggte Nodes allokiert (TMap ist leer und damit
 * quasi kostenlos fuer die ~95 % untagged Nodes).
 */
USTRUCT()
struct WIESBADENREAL_API FOSMNode
{
	GENERATED_BODY()

	FOSMId Id = 0;

	FGeoCoordinate Location;

	TMap<FName, FString> Tags;

	FOSMNode() = default;

	FOSMNode(FOSMId InId, double InLongitude, double InLatitude)
		: Id(InId)
		, Location(InLongitude, InLatitude, 0.0)
	{
	}

	bool HasTag(FName Key) const { return Tags.Contains(Key); }

	FString GetTag(FName Key, const FString& Default = FString()) const
	{
		if (const FString* Found = Tags.Find(Key))
		{
			return *Found;
		}
		return Default;
	}

	bool HasTagValue(FName Key, const TCHAR* Value) const
	{
		if (const FString* Found = Tags.Find(Key))
		{
			return Found->Equals(Value, ESearchCase::IgnoreCase);
		}
		return false;
	}

	/** True fuer highway=traffic_signals. */
	bool IsTrafficSignal() const { return HasTagValue(TEXT("highway"), TEXT("traffic_signals")); }

	/** True fuer highway=street_lamp. */
	bool IsStreetLamp() const { return HasTagValue(TEXT("highway"), TEXT("street_lamp")); }

	/** True fuer highway=crossing oder footway=crossing. */
	bool IsCrossing() const
	{
		return HasTagValue(TEXT("highway"), TEXT("crossing"))
			|| HasTagValue(TEXT("footway"), TEXT("crossing"));
	}

	/** True fuer highway=stop. */
	bool IsStopSign() const { return HasTagValue(TEXT("highway"), TEXT("stop")); }

	/** True fuer highway=give_way. */
	bool IsGiveWay() const { return HasTagValue(TEXT("highway"), TEXT("give_way")); }

	/** True fuer highway=bus_stop oder public_transport=platform. */
	bool IsBusStop() const
	{
		return HasTagValue(TEXT("highway"), TEXT("bus_stop"))
			|| HasTagValue(TEXT("public_transport"), TEXT("platform"));
	}
};

/**
 * Ein OSM-Way: geordnete Node-Referenzliste plus Tags.
 * Kann Strasse, Gebaeudeumriss, Flaeche oder Barriere sein.
 */
USTRUCT()
struct WIESBADENREAL_API FOSMWay
{
	GENERATED_BODY()

	FOSMId Id = 0;

	TArray<FOSMId> NodeIds;

	TMap<FName, FString> Tags;

	bool HasTag(FName Key) const { return Tags.Contains(Key); }

	FString GetTag(FName Key, const FString& Default = FString()) const
	{
		if (const FString* Found = Tags.Find(Key))
		{
			return *Found;
		}
		return Default;
	}

	bool HasTagValue(FName Key, const TCHAR* Value) const
	{
		if (const FString* Found = Tags.Find(Key))
		{
			return Found->Equals(Value, ESearchCase::IgnoreCase);
		}
		return false;
	}

	/** Geschlossener Ring: erster == letzter Node und mindestens 4 Referenzen. */
	bool IsClosed() const
	{
		return NodeIds.Num() >= 4 && NodeIds[0] == NodeIds.Last();
	}

	bool IsHighway() const { return Tags.Contains(TEXT("highway")); }
	bool IsBuilding() const { return Tags.Contains(TEXT("building")) || Tags.Contains(TEXT("building:part")); }
	bool IsRoundabout() const { return HasTagValue(TEXT("junction"), TEXT("roundabout")); }
	bool IsArea() const { return HasTagValue(TEXT("area"), TEXT("yes")) || IsClosed(); }
	bool IsBridge() const { return HasTag(TEXT("bridge")) && !HasTagValue(TEXT("bridge"), TEXT("no")); }
	bool IsTunnel() const { return HasTag(TEXT("tunnel")) && !HasTagValue(TEXT("tunnel"), TEXT("no")); }

	/**
	 * layer=* aus OSM. Bestimmt die vertikale Stapelung an Bruecken und
	 * Unterfuehrungen. Ohne Tag: 0.
	 */
	int32 GetLayer() const
	{
		if (const FString* Found = Tags.Find(TEXT("layer")))
		{
			return FCString::Atoi(**Found);
		}
		return 0;
	}
};

/** Rolle eines Members in einer OSM-Relation. */
UENUM()
enum class EOSMMemberType : uint8
{
	Node,
	Way,
	Relation
};

USTRUCT()
struct WIESBADENREAL_API FOSMRelationMember
{
	GENERATED_BODY()

	FOSMId Ref = 0;
	EOSMMemberType Type = EOSMMemberType::Way;

	/** OSM-Rolle: "outer", "inner", "from", "to", "via", "platform", ... */
	FName Role;
};

/**
 * Eine OSM-Relation. Relevant fuer:
 *  - type=multipolygon: Gebaeude mit Innenhoefen (Kurhaus, Rathaus-Block)
 *  - type=restriction: Abbiegeverbote fuer die Verkehrs-KI
 *  - type=route: ESWE-Buslinien
 */
USTRUCT()
struct WIESBADENREAL_API FOSMRelation
{
	GENERATED_BODY()

	FOSMId Id = 0;

	TArray<FOSMRelationMember> Members;

	TMap<FName, FString> Tags;

	FString GetTag(FName Key, const FString& Default = FString()) const
	{
		if (const FString* Found = Tags.Find(Key))
		{
			return *Found;
		}
		return Default;
	}

	bool HasTagValue(FName Key, const TCHAR* Value) const
	{
		if (const FString* Found = Tags.Find(Key))
		{
			return Found->Equals(Value, ESearchCase::IgnoreCase);
		}
		return false;
	}

	bool IsMultipolygon() const { return HasTagValue(TEXT("type"), TEXT("multipolygon")); }
	bool IsTurnRestriction() const { return HasTagValue(TEXT("type"), TEXT("restriction")); }
	bool IsRoute() const { return HasTagValue(TEXT("type"), TEXT("route")); }
};

/**
 * Vollstaendiger OSM-Datensatz nach dem Parsen.
 * Nodes werden in einer TMap gehalten, weil Ways sie per ID referenzieren und
 * die Aufloesung im Generator O(1) sein muss - bei 400k Nodes und 60k Ways
 * waere eine lineare Suche nicht praktikabel.
 */
USTRUCT()
struct WIESBADENREAL_API FOSMDataSet
{
	GENERATED_BODY()

	TMap<FOSMId, FOSMNode> Nodes;
	TMap<FOSMId, FOSMWay> Ways;
	TMap<FOSMId, FOSMRelation> Relations;

	/** Aus den Node-Positionen berechnete Ausdehnung des Datensatzes. */
	FGeoBounds Bounds;

	void Reset()
	{
		Nodes.Reset();
		Ways.Reset();
		Relations.Reset();
		Bounds = FGeoBounds();
	}

	bool IsEmpty() const { return Nodes.Num() == 0 && Ways.Num() == 0; }

	/** Berechnet Bounds neu aus allen Node-Positionen. */
	void RecomputeBounds()
	{
		if (Nodes.Num() == 0)
		{
			Bounds = FGeoBounds();
			return;
		}

		bool bFirst = true;
		for (const TPair<FOSMId, FOSMNode>& Pair : Nodes)
		{
			if (bFirst)
			{
				Bounds = FGeoBounds(
					Pair.Value.Location.Longitude, Pair.Value.Location.Latitude,
					Pair.Value.Location.Longitude, Pair.Value.Location.Latitude);
				bFirst = false;
			}
			else
			{
				Bounds.Include(Pair.Value.Location);
			}
		}
	}

	/**
	 * Loest die Node-Referenzen eines Ways zu Koordinaten auf.
	 * @param OutMissingCount Anzahl nicht aufloesbarer Referenzen (kommt bei
	 *        an der BBox-Grenze abgeschnittenen Extrakten regelmaessig vor).
	 * @return false, wenn weniger als 2 Nodes aufloesbar waren - der Way ist
	 *         dann geometrisch nutzlos.
	 */
	bool ResolveWayCoordinates(const FOSMWay& Way, TArray<FGeoCoordinate>& OutCoords, int32& OutMissingCount) const
	{
		OutCoords.Reset();
		OutCoords.Reserve(Way.NodeIds.Num());
		OutMissingCount = 0;

		for (const FOSMId NodeId : Way.NodeIds)
		{
			if (const FOSMNode* Node = Nodes.Find(NodeId))
			{
				OutCoords.Add(Node->Location);
			}
			else
			{
				++OutMissingCount;
			}
		}

		return OutCoords.Num() >= 2;
	}

	FString GetStatisticsString() const
	{
		return FString::Printf(TEXT("%d Nodes, %d Ways, %d Relations"),
			Nodes.Num(), Ways.Num(), Relations.Num());
	}
};

/**
 * Tag-Interpretation. Alle Funktionen sind statisch, seiteneffektfrei und
 * tolerant gegenueber fehlerhaften Daten - OSM ist von Menschen gepflegt und
 * enthaelt reproduzierbar Schreibfehler, Einheitensuffixe und Wertebereiche,
 * die die Spezifikation nicht vorsieht. Jede Funktion hat daher einen
 * definierten Fallback statt eines Fehlers.
 */
class WIESBADENREAL_API FOSMTagParser
{
public:
	/** highway=* -> Enum. Unbekannte Werte -> None. */
	static EOSMHighwayType ParseHighwayType(const FString& Value);

	static FString HighwayTypeToString(EOSMHighwayType Type);

	/** surface=* -> Enum. Unbekannte Werte -> Unknown. */
	static EOSMSurfaceType ParseSurfaceType(const FString& Value);

	/** sidewalk=* -> Enum. Beruecksichtigt auch sidewalk:both/left/right=yes. */
	static EOSMSidewalkType ParseSidewalkType(const FOSMWay& Way);

	/** oneway=* -> Enum. Erkennt yes/true/1/-1/reversible/no. */
	static EOSMOnewayType ParseOneway(const FOSMWay& Way);

	/** building=* -> Enum. */
	static EOSMBuildingType ParseBuildingType(const FString& Value);

	/** roof:shape=* -> Enum. Unbekannte Werte -> Flat. */
	static EOSMRoofShape ParseRoofShape(const FString& Value);

	/**
	 * Laengenangabe in Meter. Unterstuetzt:
	 *   "12"      -> 12.0 m
	 *   "12 m"    -> 12.0 m
	 *   "12.5m"   -> 12.5 m
	 *   "40'"     -> 12.192 m  (Fuss)
	 *   "40'6\""  -> 12.344 m  (Fuss + Zoll)
	 *   "12,5"    -> 12.5 m    (deutsches Dezimalkomma - in DE-Daten haeufig)
	 * @return false, wenn kein Wert extrahierbar ist.
	 */
	static bool ParseLengthMeters(const FString& Value, double& OutMeters);

	/**
	 * Geschwindigkeitsangabe in km/h. Unterstuetzt:
	 *   "50"          -> 50
	 *   "30 mph"      -> 48.28
	 *   "walk"        -> 7    (Schrittgeschwindigkeit)
	 *   "none"        -> 250  (Autobahn ohne Limit; Kappung fuer die KI)
	 *   "DE:urban"    -> 50   (implizite deutsche Limits)
	 *   "DE:rural"    -> 100
	 *   "DE:living_street" -> 7
	 * @return false, wenn kein Wert extrahierbar ist.
	 */
	static bool ParseMaxSpeedKmh(const FString& Value, double& OutKmh);

	/**
	 * lanes=*. Liefert die Gesamtspurzahl.
	 * @return false, wenn kein plausibler Wert (1..12) extrahierbar ist.
	 */
	static bool ParseLaneCount(const FString& Value, int32& OutLanes);

	/**
	 * Zerlegt einen |-separierten Wert wie turn:lanes="left|through|through;right".
	 * Leere Segmente bleiben als leere Strings erhalten, damit der Index der
	 * Spurposition entspricht.
	 */
	static TArray<FString> SplitLaneValues(const FString& Value);

	/** Anzahl Stockwerke aus building:levels. Fallback: 0 (= unbekannt). */
	static bool ParseBuildingLevels(const FOSMWay& Way, int32& OutLevels);

	/**
	 * Gebaeudehoehe in Metern. Prioritaet:
	 *   1. height=*
	 *   2. building:levels * Geschosshoehe
	 *   3. Typabhaengiger Default
	 */
	static double ResolveBuildingHeightMeters(const FOSMWay& Way, EOSMBuildingType Type, double MetersPerLevel = 3.2);

	/** Reibungskoeffizient der Oberflaeche - Eingabe fuer Chaos Vehicle. */
	static double GetSurfaceFriction(EOSMSurfaceType Surface);

	/** True fuer Wegtypen, die von Kraftfahrzeugen befahrbar sind. */
	static bool IsDrivable(EOSMHighwayType Type);

	/** True fuer Wegtypen, die Fussgaenger nutzen. */
	static bool IsWalkable(EOSMHighwayType Type);
};
