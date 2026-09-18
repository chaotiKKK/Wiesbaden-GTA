// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/RoadNetworkTypes.h"

#include "WiesbadenTrafficLights.generated.h"

/** Signalbegriff einer Ampelgruppe (deutsche Reihenfolge). */
UENUM(BlueprintType)
enum class ESignalAspect : uint8
{
    Red      UMETA(DisplayName = "Rot"),
    RedAmber UMETA(DisplayName = "Rot-Gelb"),
    Green    UMETA(DisplayName = "Gruen"),
    Amber    UMETA(DisplayName = "Gelb")
};

/** Parameter der Ampel-Steuerung. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficLightSettings
{
	GENERATED_BODY()

	/** Dauer eines vollstaendigen Zyklus in Sekunden (alle Richtungen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "2.0"))
	double CycleSeconds = 30.0;

	/**
	 * Gruenzeit je Richtungsgruppe in Sekunden. Die Richtungsgruppen einer
	 * Kreuzung bekommen versetzte, nicht ueberlappende Zeitfenster - es sind
	 * nie zwei Achsen gleichzeitig gruen. Je Gruppe gilt: gruen ab
	 * (Gruppe * Cycle/Anzahl) fuer GreenSecondsPerCycle (gekappt auf das
	 * Zeitfenster).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "1.0"))
	double GreenSecondsPerCycle = 15.0;

	/** Distanz vor der Haltelinie, ab der ein Fahrzeug bei Rot stoppt (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
	double StopDistanceCm = 300.0;

	/**
	 * Deterministischer Startwert: versetzt die Phasen je Kreuzung (aus
	 * Node-Id + Seed), damit nicht alle Ampeln der Stadt synchron schalten.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights")
	int32 RandomSeed = 20260814;

    /** Rot-Gelb-Dauer vor Gruen (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
    double RedAmberSeconds = 1.0;

    /** Gelb-Dauer am Ende der Gruenzeit (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
    double AmberSeconds = 3.0;

    /** Allrot-Raeumzeit nach Gelb, bevor die andere Achse startet (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
    double AllRedSeconds = 2.0;
};

/** Eine einzelne Ampel an einer Kreuzung (datenrein). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficLight
{
	GENERATED_BODY()

	/** OSM-Kreuzungsknoten (NodeId aus FRoadIntersection). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	int64 NodeId = 0;

	/** Position der Kreuzung in Weltkoordinaten (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	FVector Location = FVector::ZeroVector;

	/** Anzahl der Richtungsgruppen dieser Kreuzung (>= 1). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	int32 GroupCount = 1;

	/** Deterministischer Phasen-Offset in Sekunden (aus NodeId + Seed). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	double PhaseOffsetSeconds = 0.0;

	/** Connections dieser Kreuzung -> Richtungsgruppen-Id (Netz-Indizes). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	TMap<int32, int32> ConnectionGroups;
};

/**
 * Datenreine Ampel-Steuerung (kein Welt-/Actor-Zugriff, deterministisch,
 * testbar).
 *
 *  - Uebernimmt OSM-Kreuzungen mit EIntersectionControl::TrafficSignals
 *    (vom RoadNetworkGenerator aus highway=traffic_signals gesetzt).
 *  - Jede Ampel-Kreuzung teilt ihre Connections in Richtungsgruppen (je
 *    Gruppe ein Zeitfenster im Zyklus); die Gruppen schalten deterministisch
 *    aus ElapsedSeconds + per-Kreuzung-Offset - nie zwei Achsen gleichzeitig.
 *  - IsConnectionGreen(Index) fragt den aktuellen Zustand einer Verbindung
 *    ab; die Verkehrs-Simulation stoppt damit Fahrzeuge vor der Haltelinie.
 *
 * Initialisierung: Initialize(Network, Settings) nach dem Laden der Stadt.
 * Die Ampel-Simulation wird pro Tick fortgeschritten (Tick(DeltaSeconds)).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficLightSystem
{
	GENERATED_BODY()

	/** Baut die Ampeln aus den TrafficSignals-Kreuzungen des Netzes. */
	void Initialize(const FRoadNetwork& InNetwork, const FWiesbadenTrafficLightSettings& InSettings);

	/**
	 * Temporaries ablehnen: Initialize merkt sich nur einen Zeiger auf das
	 * Netz. Siehe die ausfuehrliche Begruendung an
	 * FWiesbadenTrafficSimulation::Initialize - dort hat genau dieser Fehler
	 * einen Absturz an voellig anderer Stelle erzeugt.
	 */
	void Initialize(FRoadNetwork&& InNetwork, const FWiesbadenTrafficLightSettings& InSettings) = delete;

	/** Setzt das System zurueck (keine Ampeln, Zeit 0). */
	void Reset();

	/** Schreitet die Ampel-Zeit fort (deterministisch). */
	void Tick(float DeltaSeconds);

	/** Anzahl der Ampeln (TrafficSignals-Kreuzungen). */
	int32 GetTrafficLightCount() const { return Lights.Num(); }

	/** True, wenn die Kreuzung eine Ampel besitzt. */
	bool HasTrafficLightAt(int64 NodeId) const;

	/**
	 * True, wenn die Verbindung (Index in FRoadNetwork::Connections) gerade
	 * gruen ist. Connections ohne zugehoerige Ampel sind immer gruen (keine
	 * Einschraenkung).
	 */
	bool IsConnectionGreen(int32 ConnectionIndex) const;

    /** Signalbegriff einer Verbindung (Rot/RotGelb/Gruen/Gelb). */
    ESignalAspect GetConnectionAspect(int32 ConnectionIndex) const;

    /** Signalbegriff einer Richtungsgruppe (0..GroupCount-1). */
    ESignalAspect GetGroupAspect(int32 LightIndex, int32 Group) const;

	/** True, wenn diese Verbindung ueberhaupt von einer Ampel kontrolliert wird
	 *  (Kreuzung mit TrafficSignals). Diagnose: unterscheidet "keine Ampel an der
	 *  Verbindung" von "Ampel steht auf gruen". */
	bool IsConnectionControlled(int32 ConnectionIndex) const { return ConnectionToLight.Contains(ConnectionIndex); }

	/**
	 * Index der Ampel (in Lights), die diese Verbindung steuert, sonst
	 * INDEX_NONE. Der Wert IST der Index in Lights - nicht in
	 * FRoadNetwork::Intersections. Genau diese Verwechslung liess in der
	 * echten Stadt jede Verbindung gruen erscheinen, weil nur ~1073 der
	 * ~20213 Kreuzungen Ampeln sind und der Intersections-Index daher meist
	 * ausserhalb von Lights lag.
	 */
	int32 GetLightIndexForConnection(int32 ConnectionIndex) const
	{
		const int32* Found = ConnectionToLight.Find(ConnectionIndex);
		return Found ? *Found : INDEX_NONE;
	}

	/**
	 * True, wenn AKTUELL mindestens eine kontrollierte Verbindung rot ist
	 * (Frueh-Ausstieg beim ersten Rot). VerkehrsUNABHAENGIGE Diagnose-Sonde: die
	 * Verkehrs-Simulation beobachtet damit ueber ihren eigenen Ampel-Zeiger, ob
	 * das System ueberhaupt Rot-Phasen erzeugt und an sie durchreicht - so faellt
	 * der "SetTrafficLightSystem nie gerufen"-Bug auf, auch wenn am stationaeren
	 * Spawn zu wenige Fahrzeuge eine Ampel anfahren. Bei staffelphasigen Kreuzungen
	 * (Gruen < Zyklus) ist fast immer irgendeine kontrollierte Verbindung rot.
	 */
	bool AnyControlledConnectionRed() const;

	/** Einstellungen (fuer Diagnose/HUD). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	FWiesbadenTrafficLightSettings Settings;

	/** Aktive Ampeln (je TrafficSignals-Kreuzung eine). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	TArray<FWiesbadenTrafficLight> Lights;

	/** Fortgeschrittene Ampel-Zeit in Sekunden. */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	double ElapsedSeconds = 0.0;

private:
	/** Richtungsgruppe einer Connection aus der Spur-Richtung (0..GroupCount-1). */
	int32 ComputeGroupIndex(const FRoadLane& Lane, int32 GroupCount) const;

	/** Deterministischer FNV-1a-Hash ueber zwei uint32. */
	static uint32 Hash2(uint32 A, uint32 B);

	/** 0..0.999 aus einem Hash - fuer den Phasen-Offset. */
	static float HashFraction(uint32 Hash);

	// Nicht-reflektierte Laufzeit-Daten.
	const FRoadNetwork* Network = nullptr;
	TMap<int32, int32> ConnectionToLight; // ConnectionIndex -> Lights-Index
};
