// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Was Denno gerade tut. */
enum class EDennoTask : uint8
{
	Idle,       // nur Ruhe (Spieler weit weg)
	Sweep,      // Boden fegen
	Wipe,       // Tisch abwischen
	Tidy,       // Tresen, Handtuecher, Empfang aufraeumen
	CutHair,    // Friseurgast bedienen
	Fetch,      // am Tresen die Bestellung holen (danach Serve)
	Serve,      // Tasse an den Tisch des Cafegasts bringen
	Handover,   // Lieferpaket an der Cafetuer ueberreichen, zwinkern
};

/** Dennos Bewegungen (A_Denno_*, Tools/Blender/rig_denno.py). */
enum class EDennoAnim : uint8 { Idle, Walk, WalkCarry, Sweep, Wipe, Tidy, CutHair, Serve, Handover, Count };

/** Ein Arbeitsplatz: wo sie steht, wohin sie blickt, von welchem Wegpunkt aus. */
struct FDennoSpot
{
	FVector2D Pos = FVector2D::ZeroVector;   // Laden-lokal (cm): X laengs der Front, +Y ins Haus
	double YawDeg = 0.0;                     // Blickrichtung (0 = +X, 90 = +Y ins Haus)
	int32 Node = INDEX_NONE;                 // Wegpunkt, von dem aus der Platz gerade erreichbar ist
	EDennoTask Task = EDennoTask::Idle;
};

/** Ein Gastplatz: Stuhl im Cafe (am Tisch) oder im Friseur (vor dem Spiegel). */
struct FDennoSeat
{
	FVector2D Pos = FVector2D::ZeroVector;
	double YawDeg = 0.0;                     // Blickrichtung des Sitzenden
	double LiftCm = 0.0;                     // Friseurstuhl: Sitz 10 cm hoeher als der Bistrostuhl
	FVector2D Approach = FVector2D::ZeroVector;   // letzter freier Punkt vor dem Stuhl
	int32 Node = INDEX_NONE;                 // Wegpunkt vor Approach
	bool bSalon = false;
	FDennoSpot Work;                         // wo Denno bedient (servieren / schneiden)
	FVector2D CupPos = FVector2D::ZeroVector; // Cafe: wo die Tasse auf dem Tisch landet
};

/** Ein Gast aus Sicht der Planung. */
struct FDennoGuestView
{
	int32 Seat = INDEX_NONE;
	bool bSeated = false;
	bool bServed = false;
};

/** Die naechste Aufgabe: was, wo (Spot oder Sitz), wie lange. */
struct FDennoTaskPick
{
	EDennoTask Task = EDennoTask::Idle;
	FDennoSpot Spot;
	int32 Seat = INDEX_NONE;
	double Seconds = 0.0;
};

/**
 * Dennos Arbeitstag im Laden - datenrein und darum testbar. Laden-lokale
 * Koordinaten in cm wie die Laden-Meshes (X laengs der Front nach Norden,
 * +Y ins Haus, Z ab Ladenboden); Masse aus Tools/Blender/build_denno_shop.py.
 *
 * Sie geht nur auf einem Wegenetz (Knoten, Kanten) und von dort gerade auf
 * einen Platz; die Tests pruefen jede Kante und jeden Zugang gegen die Moebel
 * (Hindernis-Kaesten). Gaeste haben Vorrang: ein wartender Friseurgast bekommt
 * einen Haarschnitt, ein Cafegast seine Tasse (erst holen, dann bringen).
 * Sonst raeumt sie hektisch auf - fegen, Tische wischen, Tresen, Handtuecher,
 * Empfang - und nie zweimal hintereinander am selben Platz.
 */
namespace WiesbadenDennoWork
{
	/** Hektisch: 2 m/s zwischen den Plaetzen, kurze Arbeitsschuebe. */
	constexpr double WalkSpeedCmS = 200.0;
	/** Tempo, fuer das A_Denno_Walk gebaut ist (rig_denno.py: WALK_SPEED_MPS). */
	constexpr double WalkAnimSpeedCmS = 220.0;
	constexpr double TurnRateDegS = 540.0;
	/** Gaeste gehen gemaechlich (Schrittzyklus der Kundenfiguren: 1,30 m/s). */
	constexpr double GuestWalkSpeedCmS = 120.0;
	/** Das Paket verlaesst ihre Haende (A_Denno_Handover, Bild 52 von 90). */
	constexpr double HandoverReleaseSeconds = 52.0 / 30.0;
	/** Die Tasse steht auf dem Tisch (A_Denno_Serve, Bild 22 von 45). */
	constexpr double ServeReleaseSeconds = 22.0 / 30.0;
	/** Bestellung am Tresen holen (s). */
	constexpr double FetchSeconds = 2.5;
	/** Wartet ein Gast so lange unbedient, geht er wieder (s). */
	constexpr double GuestPatienceSeconds = 75.0;
	/** Nach dem Servieren bleibt ein Cafegast noch ... (s). */
	constexpr double CafeLingerMinSeconds = 14.0;
	constexpr double CafeLingerMaxSeconds = 24.0;
	/** Gaeste kommen alle ... (s), hoechstens zwei im Cafe und einer im Friseur. */
	constexpr double GuestArrivalMinSeconds = 14.0;
	constexpr double GuestArrivalMaxSeconds = 32.0;
	constexpr int32 MaxCafeGuests = 2;
	constexpr int32 MaxSalonGuests = 1;

	// -- Raum ----------------------------------------------------------------
	const TArray<FVector2D>& Nodes();
	/** Kanten des Wegenetzes (ungerichtet, Knotenindizes). */
	const TArray<TPair<int32, int32>>& Edges();
	/** Kuerzester Weg zwischen zwei Knoten (inklusive beider); leer = unerreichbar. */
	TArray<int32> FindNodePath(int32 From, int32 To);
	/** Naechster Knoten zu einem Punkt (Luftlinie). */
	int32 NearestNode(const FVector2D& Pos);

	const TArray<FDennoSpot>& ChoreSpots();
	const TArray<FDennoSeat>& Seats();
	/** Am Tresen die Bestellung holen. */
	FDennoSpot FetchSpot();
	/** Vor der Cafetuer auf dem Gehweg: dort reicht sie das Lieferpaket. */
	FDennoSpot HandoverSpot();
	/** Wo sie steht, bevor jemand hinsieht (bisheriger Denno-Platz). */
	FDennoSpot RestSpot();
	/** Gaeste kommen von draussen: vor der Cafe- bzw. Friseurtuer, und der Tuerknoten. */
	FVector2D DoorOutside(bool bSalon);
	int32 DoorNode(bool bSalon);

	/**
	 * Wegpunkte von einem Wegpunkt-Knoten zu einem Ziel: Knotenkette (ohne den
	 * Startknoten) und zum Schluss die Zielpunkte `Tail` (z. B. Zugang, Platz).
	 */
	TArray<FVector2D> PathFromNode(int32 FromNode, int32 ToNode, const TArray<FVector2D>& Tail);

	// -- Hindernisse (Moebel, Waende) - fuer Tests und Plausibilitaet ---------
	struct FObstacle { FVector2D Min; FVector2D Max; const TCHAR* Name; };
	const TArray<FObstacle>& Obstacles();
	/** Beruehrt die Strecke A-B ein Hindernis (um Margin vergroessert)? Name oder nullptr. */
	const TCHAR* SegmentHit(const FVector2D& A, const FVector2D& B, double MarginCm);

	// -- Planung -------------------------------------------------------------
	/**
	 * Naechste Aufgabe. Vorrang: sitzender, unbedienter Friseurgast -> CutHair
	 * (10-14 s); sitzender, unbedienter Cafegast -> Fetch (dann Serve). Sonst
	 * ein Putz-/Aufraeumplatz (Sweep 5-8 s, Wipe 4-6 s, Tidy 4-7 s), nie der
	 * Platz `LastSpot` (Index in ChoreSpots, INDEX_NONE = keiner).
	 */
	FDennoTaskPick PickTask(const TArray<FDennoGuestView>& Guests, int32 LastSpot, FRandomStream& Random,
		int32& OutSpotIndex);

	/** Welche Bewegung eine Aufgabe am Platz spielt (Fetch: Aufraeumen am Tresen). */
	EDennoAnim WorkAnim(EDennoTask Task);
	/** Einmal-Bewegung (Servieren, Uebergabe) statt Schleife? */
	bool IsOneShot(EDennoTask Task);

	/** Freier Sitz fuer einen neuen Gast (zufaellig unter den freien der Art); INDEX_NONE = keiner. */
	int32 PickFreeSeat(const TArray<FDennoGuestView>& Guests, bool bSalon, FRandomStream& Random);
}
