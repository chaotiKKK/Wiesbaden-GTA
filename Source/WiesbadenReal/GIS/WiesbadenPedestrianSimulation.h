// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/RoadNetworkTypes.h"
#include "WiesbadenPedestrianSimulation.generated.h"

/** Ein gezeichneter Fussgaenger - Ergebnis der Simulation je Bild. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FPlacedPedestrian
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Fussgaenger")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Fussgaenger")
	FRotator Rotation = FRotator::ZeroRotator;

	/**
	 * Auf- und Abbewegung des Gangs, 0..1.
	 *
	 * Ohne sie stehen die Figuren wie Pfosten auf dem Gehweg. Eine echte
	 * Schrittanimation braucht ein Skelettmesh; die Hebung liefert auf
	 * Entfernung den entscheidenden Teil des Eindrucks.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Fussgaenger")
	float StridePhase = 0.0f;

	/**
	 * Zusaetzliche Verzerrung, multiplikativ auf den Grundmassstab.
	 *
	 * Traegt den Zerplatz-Effekt: Wer ueberfahren oder von der Kettensaege
	 * getroffen wird, wird in einem Wimpernschlag flach und breit gezogen.
	 * Ein echtes Partikelsystem waere schoener, braeuchte aber ein neues
	 * Niagara-Asset; diese Verzerrung kostet nichts und liest sich auf
	 * Entfernung genauso.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Fussgaenger")
	FVector ScaleFactor = FVector::OneVector;
};

/** Ein simulierter Fussgaenger. */
USTRUCT()
struct WIESBADENREAL_API FWiesbadenPedestrian
{
	GENERATED_BODY()

	/** Index des Strassensegments, an dessen Gehweg er laeuft. */
	int32 SegmentIndex = INDEX_NONE;

	/** True = rechter Gehweg (in Segmentrichtung), False = linker. */
	bool bRightSide = true;

	/** Zurueckgelegte Strecke entlang der Segmentachse, in cm. */
	double DistanceAlongCm = 0.0;

	/** Gehgeschwindigkeit in cm/s. */
	double SpeedCmS = 135.0;

	/** Laufrichtung entlang der Achse. */
	bool bForward = true;

	/** Phasenversatz des Gangs, damit nicht alle im Gleichschritt laufen. */
	float StrideOffset = 0.0f;

	/**
	 * Restzeit am Boden, in Sekunden. 0 = laeuft.
	 *
	 * Getroffene Fussgaenger laufen nicht weiter, sondern liegen. Ohne
	 * diesen Zustand haette ein Saegehieb gar keine sichtbare Folge: die
	 * Figuren sind Instanzen OHNE Kollision, ein Treffer im Sinne der
	 * Physik ist an ihnen nicht moeglich.
	 */
	float DownSeconds = 0.0f;

	/**
	 * Restzeit des Zerplatzens in Sekunden. 0 = unversehrt.
	 *
	 * Getrennt von DownSeconds: Umfallen ist ein Zustand, Zerplatzen ein
	 * kurzer Vorgang mit eigenem Verlauf.
	 */
	float BurstSeconds = 0.0f;
};

/** Einstellungen der Fussgaenger-Simulation. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenPedestrianSettings
{
	GENERATED_BODY()

	/** Dichte 0..1. 0 schaltet die Fussgaenger ab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fussgaenger", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Density = 1.0f;

	/**
	 * Angestrebte Zahl gleichzeitig sichtbarer Fussgaenger im Spawn-Radius.
	 *
	 * Halbiert von 140 auf 70: 140 Personen in einem Umkreis von 160 m sind
	 * fuer Wiesbadener Wohnstrassen zu dicht - das wirkt wie Fussgaengerzone
	 * zur Mittagszeit, nicht wie ein normaler Wohnbezirk.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fussgaenger", meta = (ClampMin = "0"))
	int32 TargetPedestriansInRadius = 200;

	/**
	 * Mittelpunkt der Innenstadt in Weltkoordinaten (cm).
	 *
	 * Bezugspunkt fuer die Ausduennung nach aussen. Vorgabe ist der
	 * Wiesbadener Georeferenz-Ursprung, also die Innenstadt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrians")
	FVector2D CityCentreCm = FVector2D(0.0, 0.0);

	/**
	 * Ringbreite fuer die Ausduennung, in Metern.
	 *
	 * Je angefangenem Ring nach aussen sinkt die Fussgaengerzahl um
	 * OuterFalloffPerRing. Ohne das laufen am Nordfriedhof und auf der
	 * Platter Strasse Richtung Taunusstein genauso viele Menschen herum wie
	 * in der Fussgaengerzone - dort geht in Wirklichkeit niemand zu Fuss.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrians", meta = (ClampMin = "50.0"))
	double FalloffRingMeters = 900.0;

	/** Anteil, der je Ring nach aussen wegfaellt (0,20 = 20 Prozent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrians", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	double OuterFalloffPerRing = 0.20;

	/** Untergrenze, damit der Stadtrand nicht voellig menschenleer wirkt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrians", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MinOuterFraction = 0.05;

	/** Radius um den Spieler, in dem gespawnt wird, in Metern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fussgaenger", meta = (ClampMin = "10.0"))
	double SpawnRadiusMeters = 160.0;

	/**
	 * Radius, ausserhalb dessen entfernt wird, in Metern.
	 *
	 * Muss deutlich groesser als der Spawn-Radius sein, sonst entstehen und
	 * verschwinden Figuren am Rand im Wechsel und flackern.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fussgaenger", meta = (ClampMin = "20.0"))
	double DespawnRadiusMeters = 240.0;

	/** Mittlere Gehgeschwindigkeit in m/s (1,35 entspricht dem Fussgaengerdurchschnitt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fussgaenger", meta = (ClampMin = "0.2"))
	double WalkSpeedMetersPerS = 1.35;

	/** Streuung der Gehgeschwindigkeit, 0..1 (0.25 = plus/minus 25 %). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fussgaenger", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	double WalkSpeedVariation = 0.25;

	/** Hoehe der Figur ueber der Gehwegoberflaeche in cm (Mittelpunkt des Meshes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fussgaenger", meta = (ClampMin = "0.0"))
	double BodyCenterHeightCm = 88.0;   // nicht mehr verwendet, siehe PlacePedestrians

	/** Schritthoehe der Auf-/Abbewegung in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fussgaenger", meta = (ClampMin = "0.0"))
	double StrideBobCm = 3.5;
};

/** Kennzahlen der Fussgaenger-Simulation. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenPedestrianReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Fussgaenger")
	int32 SimulatedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Fussgaenger")
	int32 WalkablePathCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Fussgaenger")
	double TotalSidewalkKm = 0.0;
};

/**
 * Fussgaenger auf den Gehwegen der Stadt.
 *
 * Bewusst KEIN eigener Wegegraph: die Figuren laufen an den Gehwegen der
 * vorhandenen Strassensegmente entlang und kehren am Segmentende um. Ein
 * echter Fussgaengergraph mit Querungen und Knotenlogik waere ein eigenes
 * Vorhaben; die Umkehr am Ende ist die ehrliche, sichtbar begrenzte Loesung -
 * aus Fahrersicht ist der Unterschied auf dem Gehweg kaum auszumachen.
 *
 * Aufbau spiegelt bewusst FWiesbadenTrafficSimulation: datenreine statische
 * Funktionen fuer alles Rechenbare, damit es ohne Welt testbar bleibt.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenPedestrianSimulation
{
	GENERATED_BODY()

	/** Bereitet die begehbaren Gehwege aus dem Strassennetz vor. */
	void Initialize(const FRoadNetwork& InNetwork, const FWiesbadenPedestrianSettings& InSettings);

	/**
	 * Temporaries ablehnen: Initialize merkt sich nur einen Zeiger auf das
	 * Netz. Siehe die ausfuehrliche Begruendung an
	 * FWiesbadenTrafficSimulation::Initialize - dort hat genau dieser Fehler
	 * einen Absturz an voellig anderer Stelle erzeugt.
	 */
	void Initialize(FRoadNetwork&& InNetwork, const FWiesbadenPedestrianSettings& InSettings) = delete;

	/** Setzt den Beobachterpunkt (Spielerposition) fuer Spawn und Entfernen. */
	void SetObserverLocation(const FVector& InLocation);

	/** Schritt der Simulation. */
	void Tick(float DeltaSeconds);

	/** Fuellt die zu zeichnenden Figuren. */
	void CollectPlaced(TArray<FPlacedPedestrian>& OutPlaced) const;

	const FWiesbadenPedestrianReport& GetReport() const { return Report; }

	int32 GetPedestrianCount() const { return Pedestrians.Num(); }

	/**
	 * Anteil der Fussgaenger an einem Ort, verglichen mit der Innenstadt
	 * (datenrein, testbar).
	 *
	 * Je angefangenem Ring nach aussen faellt der Anteil um
	 * OuterFalloffPerRing, bis zur Untergrenze MinOuterFraction.
	 */
	/** Gehweglaenge im Spawn-Umkreis in Kilometern (Bezugsgroesse der Dichte). */
	double GetNearbySidewalkKm() const { return NearbySidewalkLengthCm / 100000.0; }

	/** Zahl der Gehweg-Abschnitte im Spawn-Umkreis. */
	int32 GetNearbySegmentCount() const { return NearbySegmentIndices.Num(); }

	/** Zielzahl am aktuellen Ort (Dichte und Aussen-Ausduennung eingerechnet). */
	int32 GetTargetPedestrianCount() const;

	/** Ausduennungs-Anteil am aktuellen Ort (1 = Innenstadt). */
	double GetOuterFractionHere() const;

	/** Eingestellte Dichte 0..1 (fuer die Diagnose). */
	float GetDensity() const { return Settings.Density; }

	static double ComputeOuterFraction(
		const FVector2D& Location, const FWiesbadenPedestrianSettings& Settings);

	/**
	 * Faellt alle Fussgaenger innerhalb eines Radius um einen Punkt.
	 *
	 * Rueckgabe: wie viele getroffen wurden.
	 *
	 * Warum ueberhaupt hier und nicht ueber die Physik: Die Fussgaenger
	 * werden als Instanzen EINER Komponente gezeichnet und tragen
	 * ausdruecklich keine Kollision (PedestrianSpawnerComponent:
	 * SetCollisionEnabled(NoCollision)). Ein Kugel-Sweep auf ECC_Pawn traf
	 * deshalb zuverlaessig nichts - die Kettensaege schwang ins Leere.
	 */
	int32 StrikeNear(const FVector& Location, double RadiusCm, float DownForSeconds);

	/**
	 * Laesst Fussgaenger im Umkreis ZERPLATZEN.
	 *
	 * Anders als StrikeNear kein Umfallen, sondern ein kurzer Vorgang: die
	 * Figur wird flach und breit gezogen und verschwindet danach. Wird vom
	 * Fahrzeug beim Ueberfahren und von der Kettensaege ausgeloest.
	 *
	 * @return Wie viele getroffen wurden.
	 */
	int32 BurstNear(const FVector& Location, double RadiusCm);

	/** Dauer des Zerplatzens in Sekunden. */
	static constexpr float BurstDurationSeconds = 0.45f;

	// -- Datenreine Helfer (ohne Welt testbar) -------------------------------

	/**
	 * Seitlicher Abstand der Gehweg-Mitte von der Fahrbahnachse, in cm.
	 *
	 * Der Gehweg beginnt an der Bordsteinkante (halbe Fahrbahnbreite) und ist
	 * SidewalkWidthCm breit; gelaufen wird in seiner Mitte.
	 */
	static double ComputeSidewalkCenterOffsetCm(double CarriagewayWidthCm, double SidewalkWidthCm);

	/** True, wenn auf der jeweiligen Seite ein Gehweg liegt. */
	static bool HasSidewalkOnSide(EOSMSidewalkType Type, bool bRightSide);

	/**
	 * Bewegt einen Fussgaenger und kehrt am Segmentende um.
	 *
	 * Gibt die neue Strecke zurueck; bInOutForward wird bei Umkehr gedreht.
	 */
	static double AdvanceAlongSegment(double DistanceCm, double SpeedCmS, double SegmentLengthCm,
		float DeltaSeconds, bool& bInOutForward);

	/** Schrittphase 0..1 aus zurueckgelegter Strecke und Schrittlaenge. */
	static float ComputeStridePhase(double DistanceCm, double StrideLengthCm, float Offset);

private:
	/** Punkt und Blickrichtung auf dem Gehweg. */
	bool SampleSidewalk(const FWiesbadenPedestrian& Walker, FVector& OutLocation, FRotator& OutRotation) const;

	/** Spawnt fehlende Figuren in Spielernaehe. */
	void SpawnMissing();

	/** Entfernt Figuren ausserhalb des Despawn-Radius. */
	void DespawnDistant();

	/** Segmente mit Gehweg in Spielernaehe neu bestimmen. */
	void RefreshNearbySegments();

	UPROPERTY()
	TArray<FWiesbadenPedestrian> Pedestrians;

	UPROPERTY()
	FWiesbadenPedestrianSettings Settings;

	UPROPERTY()
	FWiesbadenPedestrianReport Report;

	/** Kopie der Segmente mit Gehweg (Index in Network.Segments). */
	TArray<int32> WalkableSegmentIndices;

	/** Davon die in Spielernaehe - daraus wird gespawnt. */
	TArray<int32> NearbySegmentIndices;

	/**
	 * Gehweglaenge im Umkreis in cm (beide Seiten, wo vorhanden).
	 *
	 * Eine nackte Personenzahl sagt nichts darueber, ob ein Gehweg belebt
	 * WIRKT: 45 Personen sind auf einer Gasse viel und auf 12 km Gehweg
	 * nichts. Erst Personen JE KILOMETER ist die Groesse, die man sieht -
	 * dieselbe Lehre wie beim Verkehr.
	 */
	double NearbySidewalkLengthCm = 0.0;

	const FRoadNetwork* Network = nullptr;

	FVector ObserverLocation = FVector::ZeroVector;
	bool bHasObserver = false;

	/** Zaehler fuer die Streuung von Geschwindigkeit und Schrittphase. */
	int32 SpawnCounter = 0;

	/** Sekunden bis zur naechsten Aktualisierung der Segmentliste. */
	float NearbyRefreshTimer = 0.0f;
};
