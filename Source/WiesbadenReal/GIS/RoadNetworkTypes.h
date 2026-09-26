// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/OSMTypes.h"
#include "RoadNetworkTypes.generated.h"

/** Fahrtrichtung einer Spur bezogen auf die Richtung des Quell-Ways. */
UENUM(BlueprintType)
enum class ELaneDirection : uint8
{
	Forward		UMETA(DisplayName = "In Way-Richtung"),
	Backward	UMETA(DisplayName = "Gegen Way-Richtung"),
	MAX			UMETA(Hidden)
};

/** Abbiegemoeglichkeiten einer Spur aus turn:lanes. Bitmaske. */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ETurnIndication : uint8
{
	None			= 0			UMETA(Hidden),
	Through			= 1 << 0	UMETA(DisplayName = "Geradeaus"),
	Left			= 1 << 1	UMETA(DisplayName = "Links"),
	Right			= 1 << 2	UMETA(DisplayName = "Rechts"),
	SlightLeft		= 1 << 3	UMETA(DisplayName = "Halblinks"),
	SlightRight		= 1 << 4	UMETA(DisplayName = "Halbrechts"),
	SharpLeft		= 1 << 5	UMETA(DisplayName = "Scharf links"),
	SharpRight		= 1 << 6	UMETA(DisplayName = "Scharf rechts"),
	UTurn			= 1 << 7	UMETA(DisplayName = "Wenden"),
};
ENUM_CLASS_FLAGS(ETurnIndication);

/** Verkehrsregelung an einer Kreuzung. */
UENUM(BlueprintType)
enum class EIntersectionControl : uint8
{
	/** Rechts vor links. */
	Uncontrolled	UMETA(DisplayName = "Rechts vor links"),
	/** Vorfahrt achten (Zeichen 205). */
	Yield			UMETA(DisplayName = "Vorfahrt achten"),
	/** Stopp (Zeichen 206). */
	Stop			UMETA(DisplayName = "Stopp"),
	/** Lichtzeichenanlage. */
	TrafficSignals	UMETA(DisplayName = "Ampel"),
	/** Kreisverkehr. */
	Roundabout		UMETA(DisplayName = "Kreisverkehr"),
	/** Vorfahrtstrasse - die durchgehende Strasse hat Vorrang. */
	PriorityRoad	UMETA(DisplayName = "Vorfahrtstrasse"),
	MAX				UMETA(Hidden)
};

/** Klassifikation einer Abbiegebeziehung nach Winkel. */
UENUM(BlueprintType)
enum class ETurnType : uint8
{
	Through			UMETA(DisplayName = "Geradeaus"),
	SlightLeft		UMETA(DisplayName = "Halblinks"),
	Left			UMETA(DisplayName = "Links"),
	SharpLeft		UMETA(DisplayName = "Scharf links"),
	SlightRight		UMETA(DisplayName = "Halbrechts"),
	Right			UMETA(DisplayName = "Rechts"),
	SharpRight		UMETA(DisplayName = "Scharf rechts"),
	UTurn			UMETA(DisplayName = "Wenden"),
	MAX				UMETA(Hidden)
};

/**
 * Markierungsstil einer Spurgrenze (StVO). Treibt die Laengsmarkierung.
 * Der konkrete Strich (durchgezogen/gestrichelt, Breite) wird beim Bau der
 * Markierungen aus diesem Semantik-Wert abgeleitet - z. B. wird DirSplit auf
 * klassifizierten Strassen/>=50 durchgezogen, auf Tempo-30 gestrichelt gezeichnet.
 */
UENUM(BlueprintType)
enum class ELaneBoundaryStyle : uint8
{
	None		UMETA(DisplayName = "keine"),
	Dashed		UMETA(DisplayName = "Leitlinie (gestrichelt)"),
	Solid		UMETA(DisplayName = "Fahrstreifenbegrenzung (Sonderspur)"),
	Edge		UMETA(DisplayName = "Fahrbahnbegrenzung (Rand)"),
	DirSplit	UMETA(DisplayName = "Richtungstrennung"),
	MAX			UMETA(Hidden)
};

/**
 * Pro-Spur-Attribute aus den OSM-Tags eines Ways, in der Reihenfolge
 * LaneIndexFromLeft (0 = aeusserste Linksspur in Way-Richtung). Wird einmal je
 * Way aufgeloest (URoadTypeLibrary::ResolveLaneAttributes) und beim Spurbau auf
 * die FRoadLane uebertragen. Datenrein - Fakten (Bus/Rad/Abbiegen) nur aus OSM,
 * Grenzstile als Konvention aus Klasse/Anordnung.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FLaneAttributes
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Road", meta = (Bitmask, BitmaskEnum = "/Script/WiesbadenReal.ETurnIndication"))
	uint8 TurnFlags = static_cast<uint8>(ETurnIndication::Through);

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bIsBusLane = false;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bIsBikeLane = false;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	ELaneBoundaryStyle LeftBoundary = ELaneBoundaryStyle::None;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	ELaneBoundaryStyle RightBoundary = ELaneBoundaryStyle::None;
};

/**
 * Eine einzelne Fahrspur.
 *
 * Die Mittellinie ist die Sollbahn der Verkehrs-KI. Sie liegt bereits auf
 * Terrainhoehe und ist so dicht abgetastet, dass ein Fahrzeug ihr ohne
 * sichtbares Schneiden folgen kann.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadLane
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 LaneId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 SegmentId = INDEX_NONE;

	/**
	 * Spurindex von links, aus Sicht der Way-Richtung. 0 ist die aeusserste
	 * linke Spur der Fahrbahn (bei Gegenverkehr also die aeussere Gegenspur).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 LaneIndexFromLeft = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	ELaneDirection Direction = ELaneDirection::Forward;

	/**
	 * Sollbahn in Weltkoordinaten (cm), bereits in Fahrtrichtung sortiert -
	 * bei Backward-Spuren also gegenlaeufig zum Quell-Way.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FVector> Centerline;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double WidthCm = 325.0;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double SpeedLimitKmh = 50.0;

	/** Bitmaske aus ETurnIndication. */
	UPROPERTY(BlueprintReadOnly, Category = "Road", meta = (Bitmask, BitmaskEnum = "/Script/WiesbadenReal.ETurnIndication"))
	uint8 TurnFlags = static_cast<uint8>(ETurnIndication::Through);

	/** True fuer Busspuren (lanes:psv, bus=designated). */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bIsBusLane = false;

	/** True fuer On-Street-Radfahrstreifen (cycleway=lane/track). */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bIsBikeLane = false;

	/** Markierungsstil der linken bzw. rechten Spurgrenze (StVO), aus den
	 *  Way-Tags/der Spuranordnung. Treibt die Laengsmarkierung. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	ELaneBoundaryStyle LeftBoundary = ELaneBoundaryStyle::None;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	ELaneBoundaryStyle RightBoundary = ELaneBoundaryStyle::None;

	/** Laenge der Sollbahn in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double LengthCm = 0.0;

	bool IsValid() const { return LaneId != INDEX_NONE && Centerline.Num() >= 2; }

	FVector GetStartPoint() const { return Centerline.Num() > 0 ? Centerline[0] : FVector::ZeroVector; }
	FVector GetEndPoint() const { return Centerline.Num() > 0 ? Centerline.Last() : FVector::ZeroVector; }

	/** Fahrtrichtung am Spurende (normiert), fuer die Kreuzungsverknuepfung. */
	FVector GetExitDirection() const
	{
		if (Centerline.Num() < 2)
		{
			return FVector::ForwardVector;
		}
		return (Centerline.Last() - Centerline[Centerline.Num() - 2]).GetSafeNormal();
	}

	/** Fahrtrichtung am Spuranfang (normiert). */
	FVector GetEntryDirection() const
	{
		if (Centerline.Num() < 2)
		{
			return FVector::ForwardVector;
		}
		return (Centerline[1] - Centerline[0]).GetSafeNormal();
	}
};

/**
 * Ein Strassenabschnitt zwischen zwei Knotenpunkten.
 *
 * Ein OSM-Way wird an jedem Kreuzungsknoten in mehrere Segmente zerlegt. Das
 * ist zwingend: die Wilhelmstrasse ist in OSM ein einziger Way ueber 1,3 km mit
 * einem Dutzend Kreuzungen. Ohne Zerlegung liesse sich weder die
 * Kreuzungsgeometrie erzeugen noch ein Routing-Graph aufbauen.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadSegment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 SegmentId = INDEX_NONE;

	/** Quell-Way in OSM - fuer Rueckverfolgbarkeit und Debugging. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int64 SourceWayId = 0;

	/** Strassenname aus name=*, z. B. "Wilhelmstrasse". Fuer GPS-Ansagen. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FString StreetName;

	/**
	 * True fuer FLAECHEN statt Baender: Plaetze, Fussgaengerzonen, Hofflaechen.
	 *
	 * In OSM sind das geschlossene Wege mit area=yes oder ein geschlossener
	 * Ring mit highway=pedestrian/footway. Wiesbaden hat 297 davon, 101 mit
	 * Namen - Markt, Bischofsplatz, Karmeliterplatz, Rebstockplatz.
	 *
	 * Ohne diese Unterscheidung wurde ein Platz wie jeder andere Weg
	 * behandelt: ein schmales Band ENTLANG SEINES UMRISSES. Im Spiel war das
	 * ein Pfad rundherum, innen Wiese - der Platz selbst fehlte.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bIsArea = false;

	/**
	 * Umriss der Flaeche in Weltkoordinaten (nur wenn bIsArea).
	 *
	 * Getrennt von Centerline: Die Mittellinie eines Flaechen-Ways ist sein
	 * Umriss, und die uebrige Pipeline (Spuren, Kreuzungen, Verkehr) wuerde
	 * daraus einen Rundkurs machen.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FVector> AreaOutline;

	/** Referenz aus ref=*, z. B. "B 263" fuer die Rheinstrasse. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FString RoadReference;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	EOSMHighwayType HighwayType = EOSMHighwayType::Residential;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	EOSMSurfaceType Surface = EOSMSurfaceType::Asphalt;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	EOSMOnewayType Oneway = EOSMOnewayType::No;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	EOSMSidewalkType SidewalkType = EOSMSidewalkType::None;

	/** Achse der Fahrbahn in Weltkoordinaten (cm), auf Terrainhoehe. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FVector> Centerline;

	/**
	 * Wie Centerline, aber an den Kreuzungen gekuerzt. Die Fahrbahndecke wird
	 * aus dieser Linie erzeugt, die Spuren dagegen aus Centerline - Fahrzeuge
	 * sollen durch die Kreuzung durchfahren, nicht davor enden.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FVector> TrimmedCenterline;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 ForwardLaneCount = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 BackwardLaneCount = 1;

	/**
	 * Pro-Spur-Attribute (Bus/Rad/Abbiegen/Grenzstil) in LaneIndexFromLeft-
	 * Reihenfolge, Groesse = ForwardLaneCount + BackwardLaneCount. Einmal je Way
	 * aufgeloest; leer = Defaults (Through, keine Sonderspur, keine Grenze).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FLaneAttributes> LaneAttributes;

	/** Gesamtbreite der Fahrbahn in cm (ohne Gehwege). */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double CarriagewayWidthCm = 650.0;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double SidewalkWidthCm = 250.0;

	/** Bordsteinhoehe. 4 cm statt 12 (25.09.2026): der hohe Absatz stand im
	 *  Spiel als "Kante" zwischen Fahrbahn und Gehweg - Nutzerwunsch: weg. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double KerbHeightCm = 4.0;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double MaxSpeedKmh = 50.0;

	/** OSM layer=* - vertikale Stapelung bei Bruecken. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 Layer = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bIsBridge = false;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bIsTunnel = false;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bIsRoundabout = false;

	/**
	 * Separat erfasster Fussweg, der im Gehwegstreifen einer Fahrbahn parallel
	 * laeuft, die ihren Gehweg auf dieser Seite schon selbst erzeugt
	 * (URoadNetworkGenerator::AlignCompanionFootways). Er bekommt die Hoehe des
	 * Strassengehwegs, wird aber NICHT als zweites Pflaster gebaut und ebnet das
	 * Gelaende nicht ein - Fussgaenger duerfen ihn weiter benutzen.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bBegleitweg = false;

	/** OSM-Node am Anfang bzw. Ende des Segments. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int64 StartNodeId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int64 EndNodeId = 0;

	/** Spuren dieses Segments, Index in FRoadNetwork::Lanes. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<int32> LaneIds;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double LengthCm = 0.0;

	int32 GetTotalLaneCount() const { return ForwardLaneCount + BackwardLaneCount; }

	bool IsOneway() const
	{
		return Oneway == EOSMOnewayType::Forward
			|| Oneway == EOSMOnewayType::Backward
			|| Oneway == EOSMOnewayType::Reversible;
	}

	/** Anzeigename fuer HUD und GPS. Faellt auf die Referenz oder den Typ zurueck. */
	FString GetDisplayName() const
	{
		if (!StreetName.IsEmpty())
		{
			return StreetName;
		}
		if (!RoadReference.IsEmpty())
		{
			return RoadReference;
		}
		return FOSMTagParser::HighwayTypeToString(HighwayType);
	}
};

/** Ein an einer Kreuzung ankommender Strassenarm. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FIntersectionArm
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 SegmentId = INDEX_NONE;

	/** True, wenn der Kreuzungsknoten der Segmentanfang ist. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bIsSegmentStart = true;

	/** Einheitsvektor vom Kreuzungsmittelpunkt in den Arm hinein. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FVector OutwardDirection = FVector::ForwardVector;

	/** Halbe Fahrbahnbreite dieses Arms in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double HalfWidthCm = 325.0;

	/** Gehwegbreite dieses Arms in cm (0, wenn kein Gehweg). */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double SidewalkWidthCm = 0.0;

	/** Winkel der Auswaertsrichtung in Grad, [0,360), fuer die Sortierung. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double BearingDegrees = 0.0;

	/** Laenge, um die dieser Arm gekuerzt wurde. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double TrimDistanceCm = 0.0;

	// -- Anschlusskante ("Tor") -------------------------------------------
	//
	// Die beiden Randpunkte der Fahrbahn dieses Arms auf dem Kreuzungsumriss,
	// in Weltkoordinaten mit Hoehe.
	//
	// Der Sinn: Kreuzungsplatte UND Fahrbahnband benutzen GENAU DIESE Punkte.
	// Bisher berechnete jede Seite ihre eigenen Randpunkte aus Mittellinie und
	// halber Breite - rechnerisch dasselbe, praktisch nie exakt gleich, weil
	// die Tangente am Bandende anders gemittelt wird als am Kreuzungsarm.
	// Jede Luecke und jede Ueberlappung dieser Stadt kam aus diesem
	// Unterschied.
	//
	// "Links" und "rechts" beziehen sich auf die AUSWAERTSRICHTUNG, also von
	// der Kreuzung weg gesehen.

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FVector GateLeft = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FVector GateRight = FVector::ZeroVector;

	/** Dieselben Punkte einschliesslich der Gehwegbreite. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FVector SidewalkGateLeft = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FVector SidewalkGateRight = FVector::ZeroVector;

	/** True, sobald die Tore berechnet sind. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bGateValid = false;
};

/**
 * Ein Knotenpunkt mit drei oder mehr Armen.
 *
 * Knoten mit genau zwei Armen sind keine Kreuzungen, sondern Fortsetzungen
 * (dort wechselt z. B. nur das Tempolimit) und werden nicht erfasst.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadIntersection
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int64 NodeId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FIntersectionArm> Arms;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	EIntersectionControl Control = EIntersectionControl::Uncontrolled;

	/** Umriss der Kreuzungsflaeche in Weltkoordinaten (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FVector> Polygon;

	/**
	 * Umriss einschliesslich der Gehwege, in Weltkoordinaten (cm).
	 *
	 * Ohne diese Flaeche endeten die Gehwege an JEDER Kreuzung: Die Fahrbahnen
	 * trafen sich sauber, aber zwischen den Armen blieben gruene Keile stehen,
	 * wo der Buergersteig um die Ecke haette laufen muessen. Aus dem Fahrzeug
	 * sah das aus, als sei die Strasse nicht angeschlossen.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FVector> SidewalkPolygon;

	/** True, wenn am Knoten ein Fussgaengerueberweg gemappt ist. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bHasPedestrianCrossing = false;

	/** True fuer crossing=zebra / crossing:markings=zebra. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bHasZebraCrossing = false;

	/** Radius der Kreuzung in cm - fuer Traffic-Culling und Ampelplatzierung. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double RadiusCm = 0.0;

	int32 GetArmCount() const { return Arms.Num(); }
};

/**
 * Gepflasterte Wendeplatte am Ende einer Sackgasse.
 *
 * Deckt die Wendeschleife des Verkehrs (WiesbadenTurnaround::LoopCircle) samt
 * halber Fahrzeugbreite ab - vorher fuhren wendende Autos ueber die Wiese.
 * Eine EBENE, die das Laengsgefaelle der Strasse fortsetzt: dieselbe Hoehe
 * gilt fuer Pflaster, Gelaendeanschmiegen und die Wendeschleife.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadTurningPlate
{
	GENERATED_BODY()

	/** Index des Abschnitts, der hier endet. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 SegmentIndex = INDEX_NONE;

	/** Spur, die hier ohne Nachfolger endet (Index in FRoadNetwork::Lanes). */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 LaneId = INDEX_NONE;

	/** Mittelpunkt der Platte (= der Wendeschleife), Z auf der Plattenebene. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FVector Center = FVector::ZeroVector;

	/** Radius des Pflasters in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	double RadiusCm = 0.0;

	/** Hoehenaenderung je cm in X und Y (Laengsgefaelle der Strasse). */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FVector2D Gradient = FVector2D::ZeroVector;

	/** Umriss (konvex, gegen den Uhrzeigersinn) auf der Plattenebene. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FVector> Polygon;

	/** Hoehe der Plattenebene an einem Punkt. */
	double HeightAt(const FVector2D& P) const
	{
		return Center.Z + Gradient.X * (P.X - Center.X) + Gradient.Y * (P.Y - Center.Y);
	}

	/**
	 * Als Kreuzungsplatte mit einem Arm - so vermaschen Generator und
	 * Gelaendeanschmiegen sie mit demselben Code wie Kreuzungen.
	 */
	FRoadIntersection AsJunction() const
	{
		FRoadIntersection Junction;
		Junction.Polygon = Polygon;
		Junction.Location = Center;
		Junction.RadiusCm = RadiusCm;
		FIntersectionArm Arm;
		Arm.SegmentId = SegmentIndex;
		Junction.Arms.Add(Arm);
		return Junction;
	}
};

namespace WiesbadenTurnaround
{
	/** Zuschlag vom Schleifenradius zum Pflasterrand: halbe Fahrzeugbreite + Rand, cm. */
	constexpr double PlateMarginCm = 130.0;

	/**
	 * Kreis der Wendeschleife HINTER dem Spurende E (Richtung Dir), durch E und
	 * - bei zweispurigen Strassen - durch den Start S der Gegenspur. EINE
	 * Rechnung fuer Verkehr (Schleife) und Generator (Pflaster), sonst fuehre
	 * die Schleife neben der Platte.
	 */
	inline void LoopCircle(const FVector& E, const FVector& Dir, const FVector& S,
		FVector2D& OutCenter, double& OutRadiusCm)
	{
		const FVector2D D = FVector2D(Dir.X, Dir.Y).GetSafeNormal();
		const FVector2D Links(D.Y, -D.X);
		const FVector2D E2(E.X, E.Y);
		const double W = FVector2D::DotProduct(FVector2D(S.X, S.Y) - E2, Links);
		if (FMath::Abs(W) < 50.0)
		{
			// Einspurig (S = E): Kreis mit 4 m Radius, E liegt darauf.
			OutRadiusCm = 400.0;
			OutCenter = E2 + D * (OutRadiusCm * 0.9) + Links * (OutRadiusCm * FMath::Sqrt(1.0 - 0.81));
			return;
		}
		OutRadiusCm = FMath::Clamp(FMath::Abs(W) * 0.5 + 150.0, 300.0, 600.0);
		const double Half = FMath::Clamp(W * 0.5, -OutRadiusCm * 0.95, OutRadiusCm * 0.95);
		OutCenter = E2 + Links * Half + D * FMath::Sqrt(OutRadiusCm * OutRadiusCm - Half * Half);
	}
}

/** Erlaubte Fahrbeziehung von einer Spur auf eine andere ueber eine Kreuzung. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FLaneConnection
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 FromLaneId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 ToLaneId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int64 IntersectionNodeId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	ETurnType TurnType = ETurnType::Through;

	/**
	 * Stuetzpunkte der Verbindungskurve durch die Kreuzung (quadratische
	 * Bezier ueber den Kreuzungsmittelpunkt). Ohne diese Kurve wuerden
	 * Fahrzeuge an Kreuzungen einen Knick fahren.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FVector> ConnectionPath;

	/** True, wenn die Beziehung durch eine OSM-Abbiegevorschrift verboten ist. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bRestricted = false;

	/**
	 * Nachtraeglich fuer den Verkehr ergaenzt (Wenden am Sackgassenende bzw.
	 * Rueckweg einer einspurigen Sackgasse, FWiesbadenTrafficSimulation::
	 * AddDeadEndTurnarounds). Ampeln lassen solche Verbindungen aus ihrem
	 * Signalprogramm - es bleibt, wie es war.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bAddedTurnaround = false;
};

/**
 * Vollstaendiges Strassennetz: Geometrie plus Fahrspur-Graph.
 *
 * Der Graph ist die gemeinsame Datenbasis fuer Verkehrs-KI (Spurfolge),
 * GPS-Navigation (A*) und Polizei-KI (Verfolgungsrouten). Er wird einmal beim
 * Laden erzeugt und danach nur gelesen - damit ist er ohne Sperren aus mehreren
 * Threads nutzbar.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadNetwork
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FRoadSegment> Segments;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FRoadLane> Lanes;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FRoadIntersection> Intersections;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FLaneConnection> Connections;

	/** Wendeplatten an Sackgassen (je Spur ohne Nachfolger hoechstens eine). */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FRoadTurningPlate> TurningPlates;

	/** Nachfolgerliste je Spur: LaneId -> Indizes in Connections. */
	TMap<int32, TArray<int32>> LaneSuccessors;

	/** Knoten-ID -> Index in Intersections. */
	TMap<int64, int32> IntersectionByNode;

	/** Strassenname (kleingeschrieben) -> Segment-Indizes. Fuer die GPS-Suche. */
	TMap<FString, TArray<int32>> SegmentsByName;

	void Reset()
	{
		Segments.Reset();
		Lanes.Reset();
		Intersections.Reset();
		Connections.Reset();
		LaneSuccessors.Reset();
		IntersectionByNode.Reset();
		SegmentsByName.Reset();
	}

	bool IsEmpty() const { return Segments.Num() == 0; }

	const FRoadSegment* GetSegment(int32 SegmentId) const
	{
		return Segments.IsValidIndex(SegmentId) ? &Segments[SegmentId] : nullptr;
	}

	const FRoadLane* GetLane(int32 LaneId) const
	{
		return Lanes.IsValidIndex(LaneId) ? &Lanes[LaneId] : nullptr;
	}

	const FRoadIntersection* GetIntersectionByNode(int64 NodeId) const
	{
		if (const int32* Index = IntersectionByNode.Find(NodeId))
		{
			return Intersections.IsValidIndex(*Index) ? &Intersections[*Index] : nullptr;
		}
		return nullptr;
	}

	/** Nachfolgeverbindungen einer Spur. Leeres Array bei Sackgassen. */
	TArray<const FLaneConnection*> GetSuccessors(int32 LaneId) const
	{
		TArray<const FLaneConnection*> Result;
		if (const TArray<int32>* Indices = LaneSuccessors.Find(LaneId))
		{
			Result.Reserve(Indices->Num());
			for (const int32 ConnectionIndex : *Indices)
			{
				if (Connections.IsValidIndex(ConnectionIndex) && !Connections[ConnectionIndex].bRestricted)
				{
					Result.Add(&Connections[ConnectionIndex]);
				}
			}
		}
		return Result;
	}

	/** Gesamtlaenge des befahrbaren Netzes in Kilometern - Plausibilitaetskennzahl. */
	double GetTotalDrivableLengthKm() const
	{
		double TotalCm = 0.0;
		for (const FRoadSegment& Segment : Segments)
		{
			if (FOSMTagParser::IsDrivable(Segment.HighwayType))
			{
				TotalCm += Segment.LengthCm;
			}
		}
		return TotalCm / 100000.0;
	}

	FString GetStatisticsString() const
	{
		int32 SignalCount = 0;
		int32 RoundaboutCount = 0;
		for (const FRoadIntersection& Intersection : Intersections)
		{
			if (Intersection.Control == EIntersectionControl::TrafficSignals) { ++SignalCount; }
			if (Intersection.Control == EIntersectionControl::Roundabout) { ++RoundaboutCount; }
		}

		return FString::Printf(
			TEXT("%d Segmente, %d Spuren, %d Kreuzungen (%d Ampeln, %d Kreisverkehre), ")
			TEXT("%d Verbindungen, %.1f km befahrbar"),
			Segments.Num(), Lanes.Num(), Intersections.Num(), SignalCount, RoundaboutCount,
			Connections.Num(), GetTotalDrivableLengthKm());
	}
};
