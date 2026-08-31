// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Geometrische Basisoperationen fuer die Erzeugung von Strassen- und
 * Gebaeudegeometrie aus 2D-Grundrissen.
 *
 * Alle Funktionen arbeiten in der XY-Ebene (Unreal Units, cm) und sind
 * seiteneffektfrei. Die Z-Komponente wird ausschliesslich beim Terrain-Sampling
 * gesetzt, damit Grundrisslogik und Hoehenprojektion getrennt bleiben - sonst
 * waere jede Aenderung am Hoehenmodell ein Eingriff in die Triangulierung.
 *
 * TOLERANZEN: Als Epsilon wird durchgehend 1 uu (= 1 cm) verwendet.
 * OSM-Koordinaten haben eine Genauigkeit von 7 Dezimalstellen (~1 cm am
 * Aequator); alles darunter ist Rauschen und wuerde bei kleineren Epsilons zu
 * Nulldreiecken und Rissen in der Geometrie fuehren.
 */
class WIESBADENREAL_API FPolygonUtils
{
public:
	/** Geometrische Toleranz in Unreal Units (1 cm). */
	static constexpr double GeometryEpsilon = 1.0;

	/** Flaechentoleranz in uu^2 - Dreiecke darunter werden verworfen. */
	static constexpr double AreaEpsilon = 1.0;

	// -- Grundlegende Polygonoperationen ------------------------------------

	/**
	 * Vorzeichenbehaftete Flaeche (Shoelace-Formel).
	 * Positiv bei Gegen-Uhrzeigersinn (CCW), negativ bei CW.
	 * Der Betrag ist die tatsaechliche Flaeche.
	 */
	static double ComputeSignedArea(const TArray<FVector2D>& Polygon);

	/** Betrag der Flaeche. */
	static double ComputeArea(const TArray<FVector2D>& Polygon)
	{
		return FMath::Abs(ComputeSignedArea(Polygon));
	}

	/** True, wenn das Polygon gegen den Uhrzeigersinn orientiert ist. */
	static bool IsCounterClockwise(const TArray<FVector2D>& Polygon)
	{
		return ComputeSignedArea(Polygon) > 0.0;
	}

	/** Dreht die Reihenfolge, falls die Orientierung nicht der gewuenschten entspricht. */
	static void EnsureWinding(TArray<FVector2D>& Polygon, bool bCounterClockwise);

	/** Flaechenschwerpunkt. Bei degeneriertem Polygon der arithmetische Mittelpunkt. */
	static FVector2D ComputeCentroid(const TArray<FVector2D>& Polygon);

	/**
	 * Entfernt aufeinanderfolgende Duplikate und einen ggf. doppelten
	 * Schlusspunkt. OSM-Ringe sind geschlossen (erster == letzter Node); die
	 * Triangulierung erwartet offene Ringe.
	 * @return Anzahl entfernter Punkte.
	 */
	static int32 RemoveDuplicatePoints(TArray<FVector2D>& Polygon, double Tolerance = GeometryEpsilon);

	/**
	 * Entfernt Punkte, die auf der Verbindungslinie ihrer Nachbarn liegen.
	 * OSM-Ways enthalten viele solche Stuetzpunkte (aus Digitalisierung); sie
	 * erzeugen Nulldreiecke und ueberfluessige Vertices.
	 */
	static int32 RemoveCollinearPoints(TArray<FVector2D>& Polygon, double AngleToleranceDegrees = 0.5);

	/** Punkt-in-Polygon nach Ray-Casting (ungerade Kreuzungszahl). */
	static bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon);

	/** Punkt-in-Dreieck ueber baryzentrische Vorzeichen. Randpunkte gelten als innen. */
	static bool IsPointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C);

	/**
	 * Schnittpunkt zweier Segmente.
	 * @return false bei parallelen, kollinearen oder sich nicht schneidenden Segmenten.
	 */
	static bool SegmentIntersection(
		const FVector2D& A1, const FVector2D& A2,
		const FVector2D& B1, const FVector2D& B2,
		FVector2D& OutIntersection);

	/** Achsenparallele Bounding-Box eines Polygons. */
	static FBox2D ComputeBounds2D(const TArray<FVector2D>& Polygon);

	/**
	 * Flaechenminimale GEDREHTE Bounding-Box eines Polygons.
	 *
	 * Fuer Gebaeude-Kollision: die achsparallele Box deckt bei einem gedrehten
	 * Grundriss im Mittel das 2,1-fache seiner Flaeche ab (gemessen an 107
	 * Wiesbadener Gebaeuden, schlimmster Fall 3,8-fach) und ragt damit weit auf
	 * die Fahrbahn - der Spieler waere gegen unsichtbare Waende quer ueber der
	 * Strasse gefahren. Die gedrehte Box ist fuer rechteckige Grundrisse exakt.
	 *
	 * Verfahren: Bei einem flaechenminimalen Rechteck liegt stets eine Kante
	 * auf einer Polygonkante. Es genuegt daher, jede Kantenrichtung als Achse zu
	 * pruefen und die kleinste Flaeche zu behalten.
	 *
	 *  OutYawRadians Drehung der Box um die Z-Achse.
	 *  false bei weniger als drei Punkten oder entartetem Polygon.
	 */
	static bool ComputeMinimumAreaBox2D(
		const TArray<FVector2D>& Polygon,
		FVector2D& OutCenter,
		FVector2D& OutExtent,
		double& OutYawRadians);

	// -- Triangulierung -----------------------------------------------------

	/**
	 * Trianguliert ein einfaches Polygon per Ear-Clipping.
	 *
	 * @param Polygon Offener Ring (kein doppelter Schlusspunkt), beliebige
	 *        Orientierung, ohne Selbstueberschneidungen.
	 * @param OutIndices Dreiecksindizes in Dreiergruppen, Wicklung CCW.
	 * @return false bei weniger als 3 Punkten oder degeneriertem Polygon.
	 *
	 * Komplexitaet O(n^2). Fuer Gebaeudegrundrisse (typisch 4-40 Punkte, im
	 * Extremfall wie dem Hauptbahnhof ~200) ist das schneller als ein
	 * Monotone-Partitioning mit hoeherem konstanten Aufwand, und deutlich
	 * robuster gegen die Ungenauigkeiten in OSM-Geometrie.
	 */
	static bool TriangulatePolygon(const TArray<FVector2D>& Polygon, TArray<int32>& OutIndices);

	/**
	 * Trianguliert ein Polygon mit Loechern (Innenhoefe).
	 *
	 * Die Loecher werden per Bruecken-Verfahren in den Aussenring eingefuegt
	 * (jedes Loch wird durch ein Diagonalen-Paar mit dem Aussenring verbunden),
	 * anschliessend wird der resultierende einfache Ring per Ear-Clipping
	 * trianguliert. Das ist das Verfahren, das auch earcut.hpp und Mapbox
	 * verwenden.
	 *
	 * @param Outer Aussenring, offen.
	 * @param Holes Innenringe, offen. Muessen vollstaendig im Aussenring liegen
	 *        und sich nicht gegenseitig ueberschneiden - genau das garantiert
	 *        eine gueltige OSM-Multipolygon-Relation.
	 * @param OutVertices Kombinierte Vertexliste (Aussenring + Bruecken +
	 *        Loecher). Enthaelt Duplikate an den Brueckenstellen; das ist
	 *        korrekt und notwendig.
	 * @param OutIndices Dreiecksindizes in OutVertices, Wicklung CCW.
	 */
	static bool TriangulatePolygonWithHoles(
		const TArray<FVector2D>& Outer,
		const TArray<TArray<FVector2D>>& Holes,
		TArray<FVector2D>& OutVertices,
		TArray<int32>& OutIndices);

	// -- Polylinien-Operationen --------------------------------------------

	/**
	 * Versetzt eine offene Polylinie senkrecht um Offset.
	 *
	 * Positiver Offset versetzt nach links bezogen auf die Laufrichtung
	 * (Linkssystem Unreal: "links" = Rotation der Richtung um -90 Grad um Z).
	 *
	 * An Knicken wird ein Miter-Join gebildet. Bei sehr spitzen Winkeln waechst
	 * die Miter-Laenge unbegrenzt (Nadelspitzen in der Strassengeometrie);
	 * ueberschreitet sie MiterLimit * |Offset|, wird auf einen Bevel-Join
	 * zurueckgefallen. MiterLimit = 4 entspricht dem SVG/Illustrator-Default
	 * und begrenzt den Ausschlag auf Winkel ab ~29 Grad.
	 *
	 * @return false, wenn weniger als 2 verwertbare Punkte vorliegen.
	 */
	static bool OffsetPolyline(
		const TArray<FVector2D>& Points,
		double Offset,
		TArray<FVector2D>& OutOffsetPoints,
		double MiterLimit = 4.0);

	/**
	 * Erzeugt aus einer Mittellinie einen Streifen konstanter Breite als
	 * Dreiecksnetz - die Basisoperation fuer Fahrbahnen, Gehwege und Markierungen.
	 *
	 * @param Centerline Mittellinie.
	 * @param Width Gesamtbreite (jeweils Width/2 nach links und rechts).
	 * @param OutVertices Vertices, abwechselnd links/rechts je Stuetzpunkt.
	 * @param OutIndices Dreiecksindizes.
	 * @param OutUVs UV-Koordinaten. U ueber die Breite [0,1], V ueber die
	 *        Laenge in Metern - dadurch ist die Texturskalierung
	 *        weltmassstaeblich und Markierungen laufen ueber Segmentgrenzen
	 *        hinweg durch.
	 */
	static bool BuildRibbonMesh(
		const TArray<FVector2D>& Centerline,
		double Width,
		TArray<FVector2D>& OutVertices,
		TArray<int32>& OutIndices,
		TArray<FVector2D>& OutUVs);

	/** Gesamtlaenge einer Polylinie in uu. */
	static double ComputePolylineLength(const TArray<FVector2D>& Points);

	/**
	 * Douglas-Peucker-Vereinfachung. Reduziert die Stuetzpunktzahl von
	 * OSM-Ways, ohne die Linienfuehrung sichtbar zu veraendern.
	 * @param Tolerance Maximale Abweichung in uu. 20 uu (20 cm) ist bei
	 *        Strassen visuell nicht wahrnehmbar und entfernt typischerweise
	 *        30-40 % der Punkte.
	 */
	static void SimplifyPolyline(const TArray<FVector2D>& Points, double Tolerance, TArray<FVector2D>& OutPoints);

	/**
	 * Fuegt Zwischenpunkte ein, sodass kein Segment laenger als MaxSegmentLength
	 * ist. Notwendig, damit Strassen dem Terrain folgen: ohne Unterteilung
	 * schneidet ein 200 m langes Segment ueber eine Kuppe hinweg durch den
	 * Hang - bei der Taunusstrasse waere die Strasse abschnittsweise im Boden.
	 */
	static void ResamplePolyline(const TArray<FVector2D>& Points, double MaxSegmentLength, TArray<FVector2D>& OutPoints);

	/**
	 * Rundet Kurven per Chaikin-Verfahren ab (Corner-Cutting).
	 * OSM-Kurven sind Polygonzuege; ohne Glaettung sind an
	 * Kurvenradien Knicke sichtbar und die Fahrzeugphysik erzeugt Rucke.
	 * @param Iterations 2 Iterationen sind ein guter Kompromiss aus Glaettung
	 *        und Vertexzahl (Punktzahl waechst je Iteration etwa auf das Doppelte).
	 */
	static void SmoothPolylineChaikin(
		const TArray<FVector2D>& Points,
		int32 Iterations,
		TArray<FVector2D>& OutPoints,
		bool bClosed = false);

	/**
	 * Kuerzt eine Polylinie an ihrem Anfang bzw. Ende um die angegebene Laenge.
	 * Wird zur Kreuzungsbildung gebraucht: die Fahrbahnen werden vor der
	 * Kreuzungsmitte abgeschnitten, damit dort die Kreuzungsflaeche gesetzt
	 * werden kann, ohne dass sich Fahrbahndecken durchdringen (Z-Fighting).
	 * @return false, wenn die Restlaenge unter GeometryEpsilon faellt.
	 */
	static bool TrimPolyline(
		const TArray<FVector2D>& Points,
		double TrimStart,
		double TrimEnd,
		TArray<FVector2D>& OutPoints);

	/** Tangente (normiert) am Anfang bzw. Ende einer Polylinie. */
	static FVector2D GetStartTangent(const TArray<FVector2D>& Points);
	static FVector2D GetEndTangent(const TArray<FVector2D>& Points);

	/**
	 * Rotiert einen 2D-Vektor um -90 Grad um Z, was im linkshaendigen
	 * Unreal-System der Normalen "nach links" bezogen auf die Richtung
	 * entspricht. Zentral definiert, weil ein Vorzeichenfehler hier saemtliche
	 * Gehwege auf die falsche Strassenseite legt.
	 */
	static FVector2D GetLeftNormal(const FVector2D& Direction)
	{
		return FVector2D(Direction.Y, -Direction.X);
	}

	/**
	 * Konvexe Huelle (Andrew's Monotone Chain), CCW orientiert.
	 * Wird fuer Kreuzungsflaechen verwendet: die getrimmten Fahrbahnenden
	 * liefern eine Punktwolke, deren Huelle die Kreuzungsdecke bildet.
	 */
	static bool ComputeConvexHull(const TArray<FVector2D>& Points, TArray<FVector2D>& OutHull);

private:
	/** Prueft, ob Dreieck (Prev, Curr, Next) ein gueltiges Ohr des Polygons ist. */
	static bool IsValidEar(
		const TArray<FVector2D>& Polygon,
		const TArray<int32>& Indices,
		int32 PrevPos,
		int32 CurrPos,
		int32 NextPos);

	/** Rekursionsschritt fuer Douglas-Peucker. */
	static void SimplifyRecursive(
		const TArray<FVector2D>& Points,
		int32 FirstIndex,
		int32 LastIndex,
		double ToleranceSquared,
		TArray<bool>& OutKeep);

	/** Quadrierter Abstand eines Punkts von einem Segment. */
	static double PointSegmentDistanceSquared(const FVector2D& P, const FVector2D& A, const FVector2D& B);

	/** Fuegt ein einzelnes Loch per Bruecke in den Arbeitsring ein. */
	static bool BridgeHoleIntoRing(TArray<FVector2D>& Ring, const TArray<FVector2D>& Hole);
};
