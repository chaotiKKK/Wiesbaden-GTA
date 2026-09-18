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

	/**
	 * Gruenzeit der Hauptrichtung je Umlauf in Sekunden.
	 *
	 * Die Gruenzeit ist die EINGABE, der Umlauf das Ergebnis - so wie ein
	 * Signalprogramm wirklich entworfen wird. Andersherum (fester Umlauf,
	 * Gruen als Rest) frisst jede zusaetzliche Phase die Hauptrichtung auf:
	 * gemessen sank das Geradeaus-Gruen mit einer Abbiegephase in 30 s Umlauf
	 * von 9 auf 3,5 Sekunden, und der Verkehr stand (42 % Steher statt 26 %,
	 * Tempo 15 statt 19 km/h).
	 *
	 * Der Umlauf einer Kreuzung steht in FWiesbadenTrafficLight::CycleSeconds.
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

	/**
	 * Eigene Phase fuer Linksabbieger.
	 *
	 * Ohne sie teilen sich Linksabbieger die Freigabe mit dem Geradeausverkehr
	 * der GEGENRICHTUNG - im Spiel fuhren beide gleichzeitig durcheinander,
	 * weil niemand Gegenverkehr beachtet. Eine geschuetzte Phase loest das im
	 * Signalprogramm statt im Fahrverhalten.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights")
	bool bProtectedLeftTurns = true;

	/** Gruenzeit der Abbiegephase (s). Kurz - es sind wenige Fahrzeuge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
	double LeftTurnGreenSeconds = 5.0;

	/**
	 * Gruene Welle: der Phasenversatz folgt dem Ort statt einem Hash.
	 *
	 * Der Versatz war bisher ein Hash aus der Knoten-Id - gut, damit nicht die
	 * ganze Stadt gleichzeitig schaltet, aber fuer den Fahrer bedeutungslos.
	 * Entlang derselben Achse ergibt der Ort geteilt durch die Auslegungs-
	 * geschwindigkeit genau die Welle: wer mit dieser Geschwindigkeit faehrt,
	 * trifft die naechste Ampel wieder bei Gruen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights")
	bool bGreenWave = true;

	/** Auslegungsgeschwindigkeit der gruenen Welle in km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "5.0"))
	double GreenWaveSpeedKmh = 50.0;
};

/**
 * Ein Zeitfenster im Signalprogramm einer Kreuzung.
 *
 * Gleich lange Fenster genuegen nicht, sobald es Abbiegephasen gibt: bei vier
 * Phasen in 30 s Zyklus blieben je 7,5 s, davon 6 s fuer Rot-Gelb, Gelb und
 * Raeumzeit - eineinhalb Sekunden Gruen. Deshalb traegt jede Phase ihre eigene
 * Dauer.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenSignalPhase
{
	GENERATED_BODY()

	/** Richtungsgruppe, die in diesem Fenster freigegeben wird. */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	int32 Group = 0;

	/** Laenge des Fensters inklusive Rot-Gelb, Gelb und Raeumzeit (s). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	float DurationSeconds = 0.0f;
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

	/**
	 * Anzahl der Richtungsgruppen dieser Kreuzung.
	 *
	 * Zwei Achsen mal zwei Freigaben: Gruppe = Achse * 2 + (Linksabbieger ?
	 * 1 : 0). Geradeaus und rechts teilen sich eine Gruppe - beide kreuzen
	 * keinen Gegenverkehr.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	int32 GroupCount = 4;

	/**
	 * Das Signalprogramm dieser Kreuzung, der Reihe nach.
	 *
	 * Kreuzungen OHNE Linksabbieger bekommen gar keine Abbiegephase - sonst
	 * stuenden 22 der 30 Sekunden fuer eine Bewegung, die es dort nicht gibt.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	TArray<FWiesbadenSignalPhase> Phases;

	/** Summe der Phasendauern (s) - der tatsaechliche Umlauf dieser Kreuzung. */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	double CycleSeconds = 0.0;

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

	/**
	 * Achse einer Anfahrt aus ihrer Peilung (datenrein, testbar).
	 *
	 * Zwei Hauptachsen: 0 fuer Peilungen in [0, 180), sonst 1. Wer einen
	 * Ampelzustand fuer eine Fahrtrichtung braucht, MUSS diese Fassung
	 * benutzen - eine zweite, gleich aussehende Rechnung an anderer Stelle
	 * laeuft frueher oder spaeter auseinander.
	 */
	static int32 AxisForBearing(double BearingDeg);

	/**
	 * Richtungsgruppe aus Achse und Abbiegerichtung (datenrein, testbar).
	 *
	 * Geradeaus und rechts teilen sich die Gruppe der Achse; Linksabbieger
	 * bekommen eine eigene.
	 */
	static int32 GroupForApproach(int32 Axis, bool bLeftTurn);

	/** True, wenn diese Abbiegeart eine eigene Linksphase braucht. */
	static bool IsLeftTurn(ETurnType Turn);

	/**
	 * Wie viele Kreuzungen eine eigene Abbiegephase bekommen haben, und wie
	 * lang der Umlauf im Mittel ist (Diagnose).
	 *
	 * Eine Abbiegephase kostet Umlaufzeit, und die zahlen ALLE Richtungen.
	 * Ohne diese beiden Zahlen laesst sich nicht beurteilen, ob der laengere
	 * Umlauf die Konfliktfreiheit wert ist.
	 */
	void GetProgramStatistics(int32& OutWithLeftPhase, double& OutMeanCycleSeconds) const;

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
	/** Richtungsgruppe einer Connection aus Spur-Richtung und Abbiegeart. */
	int32 ComputeGroupIndex(const FRoadLane& Lane, ETurnType Turn) const;

	/** Baut das Signalprogramm einer Kreuzung aus ihren Richtungsgruppen. */
	void BuildSignalProgram(FWiesbadenTrafficLight& Light) const;

	/** Phasenversatz: gruene Welle entlang der Hauptachse, sonst Hash. */
	double ComputePhaseOffset(const FRoadIntersection& Intersection,
		const FRoadNetwork& InNetwork, double CycleSeconds) const;

	/** Rangfolge der Strassenklassen - nur zum Finden der Hauptachse. */
	static double RoadClassRank(EOSMHighwayType Type);

	/** Deterministischer FNV-1a-Hash ueber zwei uint32. */
	static uint32 Hash2(uint32 A, uint32 B);

	/** 0..0.999 aus einem Hash - fuer den Phasen-Offset. */
	static float HashFraction(uint32 Hash);

	// Nicht-reflektierte Laufzeit-Daten.
	const FRoadNetwork* Network = nullptr;
	TMap<int32, int32> ConnectionToLight; // ConnectionIndex -> Lights-Index
};
