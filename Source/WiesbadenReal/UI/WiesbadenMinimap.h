// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/RoadNetworkTypes.h"

class UCanvas;
struct FGeneratedBuilding;

/** Ein projiziertes Gebaeude-Viereck der Weltkarte (Bildschirm-Eckpunkte). */
struct FWorldMapQuad
{
	FVector2D A = FVector2D::ZeroVector;
	FVector2D B = FVector2D::ZeroVector;
	FVector2D C = FVector2D::ZeroVector;
	FVector2D D = FVector2D::ZeroVector;
};

/**
 * Eine Linie der Minikarte, bereits in Bildschirmkoordinaten.
 *
 * Datenrein gehalten: Die Auswahl und Umrechnung laesst sich damit ohne
 * Canvas und ohne laufende Welt pruefen - das Zeichnen selbst ist der
 * triviale Teil.
 */
struct FMinimapLine
{
	FVector2D Start = FVector2D::ZeroVector;
	FVector2D End = FVector2D::ZeroVector;

	/** Dicke in Pixeln - Hauptstrassen werden breiter gezeichnet. */
	float Thickness = 1.0f;

	/** True fuer Hauptstrassen (andere Farbe). */
	bool bMajor = false;
};

/**
 * Wegpunkt, auf die Minikarte projiziert.
 *
 * Datenrein, damit sich die Richtung/Randklemmung ohne Canvas pruefen laesst
 * (Test World.MinimapWaypoint).
 */
struct FMinimapWaypoint
{
	/** Bildschirmposition des Markers in Pixeln (am Kartenrand geklemmt, wenn ausserhalb). */
	FVector2D ScreenPos = FVector2D::ZeroVector;

	/** True, wenn der Wegpunkt ausserhalb der Kartenreichweite liegt (Marker am Rand). */
	bool bOffMap = false;

	/** Planare Entfernung Spieler -> Wegpunkt in Zentimetern. */
	double DistanceCm = 0.0;
};

/** Einstellungen der Minikarte. */
struct FMinimapSettings
{
	/** Sichtbarer Umkreis in Zentimetern. */
	double RangeCm = 25000.0;

	/** Durchmesser der Karte in Pixeln. */
	float DiameterPx = 260.0f;

	/**
	 * True: Die Karte dreht sich mit dem Spieler (Fahrtrichtung immer oben).
	 * False: Norden ist immer oben.
	 */
	bool bRotateWithPlayer = true;

	/**
	 * Groesste Zahl gezeichneter Linien - Schutz gegen Bildzeit-Einbrueche.
	 *
	 * 900 war zu knapp: In dichter Bebauung war die Grenze erreicht, bevor
	 * alle Strassen im Umkreis gezeichnet waren, und es fehlten welche. Eine
	 * Linie ist ein Canvas-Aufruf; 4.000 davon sind gegenueber 136 ms Bildzeit
	 * nicht der Engpass.
	 */
	int32 MaxLines = 4000;
};

/**
 * Fit-Projektion fuer die VOLLBILD-Weltkarte (M / Gamepad-Select): das GANZE
 * Strassennetz in ein Bildschirmrechteck, Norden oben, Seitenverhaeltnis
 * erhalten. Anders als die Minikarte NICHT spielerzentriert - der Spieler ist
 * nur ein Punkt an seiner projizierten Position.
 *
 * Datenrein, damit die Umrechnung ohne Canvas/Welt pruefbar ist
 * (Test World.WorldMapProjection).
 */
struct WIESBADENREAL_API FWorldMapProjection
{
	/** XY-Grenzen des Netzes in Zentimetern (Welt: X=Ost, Y=Nord). */
	FVector2D WorldMin = FVector2D::ZeroVector;
	FVector2D WorldMax = FVector2D::ZeroVector;

	/** Mitte des Kartenbereichs in Pixeln. */
	FVector2D ScreenCentre = FVector2D::ZeroVector;

	/** Gleichmaessiger Massstab Pixel je Zentimeter (fit an die engere Achse). */
	float ScalePxPerCm = 1.0f;

	/**
	 * Weltpunkt (cm), der auf ScreenCentre abgebildet wird - der "Blick-Mittelpunkt".
	 *
	 * Bei der Voll-Einpassung ist das die Netzmitte; Zoom/Pan verschieben ihn.
	 * Frueher zentrierte Project() fest auf (WorldMin+WorldMax)/2; ueber dieses
	 * Feld wird die Ansicht schwenkbar, ohne die Netzgrenzen zu veraendern.
	 */
	FVector2D ViewCentreWorld = FVector2D::ZeroVector;

	/**
	 * Weltpunkt -> Bildschirm (px). Norden oben: Welt +Y (Nord) wird zu
	 * kleinerem Bildschirm-Y, Welt +X (Ost) zu groesserem Bildschirm-X.
	 */
	FVector2D Project(const FVector& World) const;

	/** Bildschirm (px) -> Weltpunkt (cm), die Umkehrung von Project (fuer Pan/Zoom). */
	FVector2D Unproject(const FVector2D& Screen) const;

	bool IsValid() const
	{
		return ScalePxPerCm > 0.0f && WorldMax.X > WorldMin.X && WorldMax.Y > WorldMin.Y;
	}
};

/**
 * Baut die Linien der Minikarte aus dem Strassennetz.
 *
 * Bewusst KEIN SceneCapture: Eine zweite Kameraansicht der Stadt zu rendern
 * kostet noch einmal so viel wie das Hauptbild, und das Spiel laeuft bereits
 * mit 7 Bildern je Sekunde. Aus den Mittellinien gezeichnet ist die Karte
 * schaerfer, lesbarer und praktisch umsonst.
 */
struct WIESBADENREAL_API FWiesbadenMinimap
{
	/**
	 * XY-Huellbox aller Segment-Mittellinienpunkte (cm). Rueckgabe false bei
	 * leerem Netz (OutMin/OutMax dann unveraendert).
	 */
	static bool ComputeNetworkBoundsXY(
		const FRoadNetwork& Network, FVector2D& OutMin, FVector2D& OutMax);

	/**
	 * Fit-Projektion: das ganze Netz (WorldMin..WorldMax) in ein Rechteck der
	 * Groesse ScreenSizePx um ScreenCentre, Seitenverhaeltnis erhalten.
	 * MarginFrac < 1 laesst einen Rand (0.88 = 6 % Rand je Seite).
	 */
	static FWorldMapProjection MakeWorldMapProjection(
		const FVector2D& WorldMin, const FVector2D& WorldMax,
		const FVector2D& ScreenCentre, const FVector2D& ScreenSizePx, float MarginFrac);

	/** Kleinste/groesste Zoomstufe der Weltkarte (1 = ganzes Netz eingepasst). */
	static constexpr float WorldMapMinZoom = 1.0f;
	static constexpr float WorldMapMaxZoom = 8.0f;

	/**
	 * Zoomt/verschiebt eine Voll-Einpass-Projektion (Fit) auf ZoomFactor
	 * (geklemmt auf [WorldMapMinZoom..WorldMapMaxZoom]) um DesiredCentreWorld.
	 *
	 * Das Blickzentrum wird so geklemmt, dass das Sichtfenster die Netzgrenzen
	 * nicht verlaesst; ist das Fenster groesser als das Netz (u. a. bei Zoom 1),
	 * wird auf die Netzmitte zentriert. Datenrein/testbar (Test World.WorldMapZoomPan).
	 */
	static FWorldMapProjection MakeZoomedProjection(
		const FWorldMapProjection& Fit, float ZoomFactor, const FVector2D& DesiredCentreWorld);

	/**
	 * Projiziert das ganze Netz in Bildschirmlinien: Nebenstrassen zuerst,
	 * Hauptstrassen zuletzt (liegen oben). Segmente, die kuerzer als
	 * MinSegmentPx projizieren, werden uebersprungen (bei Stadt-Zoom unsichtbar);
	 * das haelt die Zahl der Canvas-Aufrufe unter MaxLines.
	 */
	static void BuildWorldMapLines(
		const FRoadNetwork& Network, const FWorldMapProjection& Proj,
		int32 MaxLines, float MinSegmentPx, TArray<FMinimapLine>& OutLines);

	/**
	 * Projiziert die gedrehten Grundriss-Boxen der Gebaeude in Bildschirm-Vierecke.
	 * Groesste zuerst (kleine sind bei Stadt-Zoom sub-pixel), gedeckelt auf
	 * MaxQuads; Vierecke unter MinAreaPx werden uebersprungen. Als bebautes-Gebiet-
	 * Schattierung unter den Strassen gedacht.
	 */
	static void BuildWorldMapBuildings(
		const TArray<FGeneratedBuilding>& Buildings, const FWorldMapProjection& Proj,
		int32 MaxQuads, float MinAreaPx, TArray<FWorldMapQuad>& OutQuads);

	/**
	 * Waehlt die Segmente im Umkreis und rechnet sie in Bildschirmkoordinaten
	 * um. Center ist die Mitte der Karte in Pixeln.
	 */
	static void BuildLines(
		const FRoadNetwork& Network,
		const FVector& PlayerLocation,
		double PlayerYawDegrees,
		const FVector2D& CenterPx,
		const FMinimapSettings& Settings,
		TArray<FMinimapLine>& OutLines);

	/**
	 * Projiziert einen Wegpunkt in die Minikarte - dieselbe Dreh-/Massstab-Abbildung
	 * wie BuildLines, damit der Marker deckungsgleich mit den Strassen liegt. Liegt
	 * er weiter weg als die Reichweite, wird der Marker richtungserhaltend an den
	 * Kartenrand geklemmt (bOffMap = true). Die Entfernung ist planar (XY).
	 */
	static FMinimapWaypoint ProjectWaypointToMinimap(
		const FVector& PlayerLocation,
		double PlayerYawDegrees,
		const FVector& WaypointLocation,
		const FVector2D& CenterPx,
		const FMinimapSettings& Settings);

	/**
	 * Name der Strasse, auf der sich der Spieler befindet.
	 *
	 * Massgeblich ist der ABSTAND ZUR MITTELLINIE, nicht die Naehe zu einem
	 * Stuetzpunkt: Eine lange gerade Strasse hat weit auseinanderliegende
	 * Stuetzpunkte, und die naechstgelegene Ecke kann zu einer ganz anderen
	 * Strasse gehoeren.
	 *
	 * Leerer String, wenn nichts in Reichweite ist oder die Strasse namenlos
	 * ist (Feldwege, Zufahrten).
	 */
	static FString FindStreetName(
		const FRoadNetwork& Network,
		const FVector& PlayerLocation,
		double MaxDistanceCm = 3000.0);

	/** True fuer Strassentypen, die auf der Karte hervorgehoben werden. */
	static bool IsMajorRoad(EOSMHighwayType Type);
};
