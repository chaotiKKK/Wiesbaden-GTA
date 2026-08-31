// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/RoadNetworkTypes.h"

class UCanvas;

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
