// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/RoadNetworkTypes.h"

#include "WiesbadenTrafficSimulation.generated.h"

struct FWiesbadenTrafficLightSystem;

/**
 * Sichtbare Fahrzeug-Platzierung (fuer den ISM-Spawner): Position/Rotation
 * aus der Simulation plus deterministischer Farb-Index.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FPlacedTrafficVehicle
{
	GENERATED_BODY()

	/** Fahrzeug-Id aus der Simulation (stabil ueber Ticks). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 VehicleId = INDEX_NONE;

	/** Welt-Transform (Location + Yaw aus der Fahrtrichtung). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FTransform Transform = FTransform::Identity;

	/** Deterministischer Farb-Index (0..PaletteSize-1) aus der Fahrzeug-Id. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 ColorIndex = 0;

	/** Einschlag der Vorderraeder in Radiant (positiv = links). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float SteerAngleRad = 0.0f;
};

/** Ein Fahrzeug der Verkehrs-Simulation auf dem Spur-Graph. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FTrafficVehicle
{
	GENERATED_BODY()

	/** Eindeutige, aufsteigend vergebene Fahrzeug-Id (auch Spawn-Zaehler). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 VehicleId = INDEX_NONE;

	/** True, wenn das Fahrzeug auf einer Spur faehrt (sonst auf einer Verbindung). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	bool bOnLane = true;

	/** Aktuelle Spur (Index in FRoadNetwork::Lanes), wenn bOnLane. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneId = INDEX_NONE;

	/** Aktuelle Verbindung (Index in FRoadNetwork::Connections), wenn !bOnLane. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 ConnectionIndex = INDEX_NONE;

	/** Entfernung entlang der aktuellen Bahn (Spur-Mittellinie bzw. Verbindungskurve). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double DistanceCm = 0.0;

	/** Aktuelle Geschwindigkeit in cm/s (durch Kopf-zu-Schwanz begrenzt). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double SpeedCmS = 0.0;

	/** Wunschgeschwindigkeit in cm/s (Tempolimit der Spur * Fahrzeug-Charakter). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double DesiredSpeedCmS = 0.0;

	/** Weltposition auf der Sollbahn (pro Tick berechnet). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FVector Location = FVector::ZeroVector;

	/** Normierte Fahrtrichtung (fuer spaetere Fahrzeug-Meshes). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FVector Forward = FVector::ForwardVector;

	/**
	 * Restliche Sperrzeit bis zum naechsten Spurwechsel in Sekunden.
	 *
	 * Ohne Sperre wechselt ein Fahrzeug, das zwischen zwei gleich vollen
	 * Spuren steht, in jedem Tick hin und her und zappelt sichtbar.
	 */
	float LaneChangeCooldown = 0.0f;

	/**
	 * Weltposition der KAROSSERIE - das, was man sieht.
	 *
	 * Location oben ist die Sollposition auf der Bahn. Sie traegt Abstaende,
	 * Stau und Kreuzungslogik und muss exakt auf dem Graphen bleiben. Die
	 * Karosserie folgt ihr nur nach, mit Lenkeinschlag und Wendekreis. Genau
	 * dieser Unterschied nimmt dem Verkehr das "wie auf Schienen".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FVector BodyLocation = FVector::ZeroVector;

	/** Gierwinkel der Karosserie in Radiant (integriert, nicht abgelesen). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float BodyYawRad = 0.0f;

	/**
	 * Einschlag der Vorderraeder in Radiant (positiv = links).
	 *
	 * Wird gebraucht, um die Raeder mitzudrehen: ein Auto, das um die Ecke
	 * faehrt und dabei geradeaus stehende Raeder hat, faellt sofort auf.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float SteerAngleRad = 0.0f;

	/** False bis zum ersten Tick; dann wird die Karosserie auf die Bahn gesetzt. */
	bool bBodyInitialized = false;

	/** Intern: zum Entfernen markiert (am Bahnende ohne Folgebahn). */
	bool bRemoved = false;

	/**
	 * Intern: war das Fahrzeug im letzten Tick im Anfahr-Fenster einer
	 * signalisierten Verbindung bzw. dort an Rot gehalten?
	 *
	 * Fuer die Ampel-Diagnose werden DISTINKTE Ereignisse gezaehlt (die
	 * FALSE->TRUE-Flanke), nicht pro Tick: sonst inflationiert ein einziges
	 * wartendes Fahrzeug die "Anfahrten" in unter einer Sekunde auf beliebige
	 * Hoehe, und die Kennzahl luegt (20 "Anfahrten" = ein Fahrzeug, 20 Frames).
	 */
	bool bWasApproachingSignal = false;
	bool bWasHeldAtRed = false;
};

/** Parameter der Verkehrs-Simulation. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficSettings
{
	GENERATED_BODY()

	/**
	 * Verkehrsdichte 0..1 (aus dem City-Prompt: "leere Strassen" 0.2, "Stau"
	 * 0.95). Steuert die Spawn-Rate linear.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrafficDensity = 0.5f;

	/** Fahrzeuge pro Sekunde bei voller Dichte (1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	float MaxSpawnRatePerSecond = 2.0f;

	/** Anteil des Tempolimits als Basis-Wunschgeschwindigkeit (0.75 = 75 %). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TargetSpeedFraction = 0.75f;

	/**
	 * Mindestabstand zwischen zwei Fahrzeugen (Spitze an Spitze) in cm.
	 * Der Folger bremst so, dass diese Luecke nie unterschritten wird.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "100.0"))
	double MinGapCm = 700.0;

	/** Untergrenze der Wunschgeschwindigkeit in km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "5.0"))
	double MinSpeedKmh = 20.0;

	/** Obergrenze der gleichzeitig aktiven Fahrzeuge (Performance-Guard). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "1"))
	int32 MaxVehicles = 3000;

	/**
	 * Umkreis um den Spieler, in dem neue Fahrzeuge einsetzen, in Metern.
	 *
	 * Ohne diese Begrenzung verteilen sich die Fahrzeuge ueber das gesamte
	 * Netz - in Wiesbaden 2.700 km. Selbst 3.000 Fahrzeuge ergaeben dann rund
	 * eines je Kilometer, und in Sichtweite waere so gut wie nie eines. Ein
	 * Verkehr, den niemand sieht, kostet nur Rechenzeit.
	 *
	 * 600 m liegt knapp ausserhalb der Sichtweite: Fahrzeuge tauchen nicht vor
	 * den Augen des Spielers auf, sind aber schnell erreichbar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "50.0"))
	double SpawnRadiusMeters = 600.0;

	/**
	 * Entfernung, ab der Fahrzeuge wieder entfernt werden, in Metern.
	 *
	 * Muss deutlich groesser als SpawnRadiusMeters sein, sonst entsteht am
	 * Rand ein Flackern aus staendigem Einsetzen und Entfernen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "100.0"))
	double DespawnRadiusMeters = 900.0;

	/**
	 * Fahrzeuge im Umkreis bei voller Dichte. Bestimmt zusammen mit
	 * TrafficDensity, wie belebt die Strassen wirken.
	 *
	 * MaxVehicles bleibt die harte Obergrenze; dieser Wert ist das Ziel, das
	 * die Simulation im Umkreis anstrebt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0"))
	int32 TargetVehiclesInRadius = 110;

	/**
	 * Deterministischer Streu-Startwert: beeinflusst die individuelle
	 * Wunschgeschwindigkeit je Fahrzeug (0.8..1.0 des TargetSpeedFraction).
	 * Gleicher Seed + gleiche Eingaben -> identisches Verhalten.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	int32 RandomSeed = 20260814;

	/**
	 * Groesste Beschleunigung in cm/s^2.
	 *
	 * Die Simulation hat die Geschwindigkeit frueher in einem Tick auf den
	 * Zielwert gesetzt - ein Fahrzeug sprang damit zwischen Stillstand und
	 * Vollgas. Daraus entstanden Stop-and-Go-Wellen, die sich rueckwaerts
	 * durch die Kolonne fortpflanzten und wie ein Stau aus dem Nichts aussahen.
	 *
	 * 250 cm/s^2 (2,5 m/s^2) entspricht zuegigem Anfahren eines Pkw.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "10.0"))
	double MaxAccelerationCmS2 = 250.0;

	/**
	 * Angenommene Verzoegerung fuer die Vorausschau in cm/s^2.
	 *
	 * Bestimmt NICHT, wie stark ein Fahrzeug bremsen darf - das legt die
	 * Abstandsregel fest, und sie muss dafuer frei bleiben, sonst faehrt der
	 * Verkehr auf. Dieser Wert beantwortet nur die Frage, ab welcher Entfernung
	 * vor einem Bahnende ein Fahrzeug ueber die Bahngrenze hinausschauen muss:
	 * Mindestluecke plus Bremsweg aus der aktuellen Geschwindigkeit.
	 *
	 * 800 cm/s^2 sind 0,8 g - eine kraeftige Bremsung.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "10.0"))
	double MaxDecelerationCmS2 = 800.0;

	/** True: Fahrzeuge duerfen zum Ueberholen die Spur wechseln. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bAllowLaneChange = true;

	/**
	 * Anteil der Wunschgeschwindigkeit, unter dem ein Fahrzeug als behindert
	 * gilt und einen Spurwechsel erwaegt (0.7 = unter 70 %).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	double LaneChangeSpeedDeficit = 0.7;

	/**
	 * Mindestluecke nach vorn UND nach hinten auf der Zielspur in cm.
	 *
	 * Nach hinten ist genauso wichtig wie nach vorn: Wer vor einen schnelleren
	 * Nachfolger zieht, loest dort dieselbe Bremswelle aus, der er selbst
	 * entkommen wollte.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "100.0"))
	double LaneChangeMinGapCm = 1400.0;

	/** Sperrzeit zwischen zwei Spurwechseln desselben Fahrzeugs in Sekunden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	float LaneChangeCooldownSeconds = 4.0f;

	// -- Einspurmodell der Karosserie ----------------------------------------
	//
	// "KI-Autos sollen nicht wie auf Schienen fahren, sondern echte
	// Fahrphysik, Wendezirkel und Radstellungen beachten."
	//
	// Die LAENGSbewegung bleibt auf dem Spur-Graphen - sie traegt Abstand,
	// Stau und Ampeln, und dort ist der Graph unschlagbar guenstig. Nur die
	// QUERbewegung war das Problem: Position und Gierwinkel wurden direkt von
	// der Polylinie abgelesen. Damit sprang die Ausrichtung an jedem
	// Stuetzpunkt um, jede Kurve wurde auf den Zentimeter genau getroffen, und
	// kein Fahrzeug hatte je einen Wendekreis.
	//
	// Darum faehrt die Karosserie jetzt ein eigenes Einspurmodell: Sie zielt
	// auf einen Punkt weiter vorn auf der Bahn (reine Verfolgung), daraus
	// folgt ein Radeinschlag; der Einschlag ist begrenzt (Wendekreis) und
	// aendert sich nur mit endlicher Geschwindigkeit (Lenkraddrehung); erst
	// daraus ergeben sich Gierrate und Position.

	/** Radstand in cm (Mittelklasse-Pkw). Bestimmt zusammen mit dem groessten
	 *  Einschlag den Wendekreis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "100.0"))
	double WheelbaseCm = 265.0;

	/**
	 * Groesster Radeinschlag in Grad.
	 *
	 * 33 Grad bei 265 cm Radstand ergeben einen Wendekreisradius von
	 * 265/tan(33 Grad) = 408 cm, also gut 8 m Durchmesser - der Wert eines
	 * ueblichen Pkw. Groesser laesst die Autos um die eigene Achse drehen,
	 * kleiner laesst sie enge Kreuzungen ueberfahren.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "5.0", ClampMax = "60.0"))
	double MaxSteerAngleDeg = 33.0;

	/** Wie schnell sich der Einschlag aendern darf, in Grad je Sekunde. Ohne
	 *  diese Grenze zuckt das Rad in einem Tick von Anschlag zu Anschlag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "10.0"))
	double MaxSteerRateDegS = 220.0;

	/** Grundvorausschau der reinen Verfolgung in cm (bei Stillstand). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "50.0"))
	double LookaheadBaseCm = 320.0;

	/**
	 * Zusaetzliche Vorausschau je Sekunde Fahrzeit.
	 *
	 * Schnelle Fahrzeuge muessen weiter vorausschauen, sonst pendeln sie: Sie
	 * korrigieren auf einen Punkt, den sie laengst passiert haben. 0,55 s sind
	 * bei 50 km/h rund 7,6 m Vorausschau.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "0.0"))
	double LookaheadSeconds = 0.55;

	/**
	 * Groesster geduldeter Abstand der Karosserie zur Sollbahn in cm.
	 *
	 * Das Einspurmodell kann die Bahn nicht immer halten - in sehr engen
	 * Kurven schneidet oder weitet es. Das ist erwuenscht. Laeuft der Fehler
	 * aber davon (Bahnwechsel, kaputtes Netz), landet das Auto im Gruenen. Ab
	 * dieser Grenze wird es weich zur Bahn zurueckgezogen.
	 *
	 * 400 cm liegt bewusst UEBER einer Spurbreite: Ein Spurwechsel versetzt
	 * die Sollbahn schlagartig um rund 350 cm, und die Karosserie soll in
	 * diesem Moment nicht am Sicherheitsnetz haengen, sondern gemaechlich
	 * herueberziehen - genau so, wie ein Spurwechsel aussehen muss.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "20.0"))
	double MaxBodyDeviationCm = 400.0;
};

/** Momentaufnahme der Simulation (pro Tick, fuer HUD/Blueprint). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficReport
{
	GENERATED_BODY()

	/** Aktive Fahrzeuge (nach dem letzten Tick). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 ActiveVehicleCount = 0;

	/** Kumulativ gespawnte Fahrzeuge. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int64 TotalSpawnedCount = 0;

	/** Kumulativ entfernte Fahrzeuge (Sackgassen-Ende, Netzende). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int64 TotalRemovedCount = 0;

	/** Kumulativ zurueckgelegte Strecke aller Fahrzeuge in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double TotalDistanceCm = 0.0;

	/** Mittlere Geschwindigkeit der aktiven Fahrzeuge in km/h. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double MeanSpeedKmh = 0.0;

	/** Aktive Fahrzeuge je km befahrbares Netz (Dichte-Kennzahl). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double ActiveVehiclesPerKm = 0.0;

	/**
	 * Fahrzeuge, die im letzten Tick praktisch standen (< 5 km/h), obwohl sie
	 * schneller fahren wollten.
	 *
	 * Ohne diese Zahl laesst sich "die Autos stauen sich" nicht pruefen. Dass
	 * die Simulation laeuft, heisst nicht, dass der Verkehr fliesst.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 StalledVehicleCount = 0;

	/** Spurwechsel (Ueberholvorgaenge) im letzten Tick. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneChangesThisTick = 0;
};

/**
 * Datenreine Verkehrs-Simulation auf dem Fahrspur-Graphen (kein Welt-/Actor-
 * Zugriff, deterministisch, testbar).
 *
 * - Fahrzeuge folgen dem Graph: Spur-Mittellinie -> Kreuzungs-Verbindung
 *   (FLaneConnection::ConnectionPath) -> Folgespur. An einer Kreuzung waehlt
 *   ein Fahrzeug deterministisch (FNV-1a-Hash aus Fahrzeug-Id + Knoten-Id)
 *   eine erlaubte Verbindung; Sackgassen entfernen das Fahrzeug.
 * - TrafficDensity (aus dem City-Prompt) steuert die Spawn-Rate linear:
 *   Rate = MaxSpawnRatePerSecond * Density. Der Spawn-Lane wird per
 *   Round-Robin ueber alle befahrbaren Spuren gewaehlt (deterministisch);
 *   ein blockierter Spur-Anfang verschiebt den Spawn (kein Stapeln).
 * - Kopf-zu-Schwanz: je Bahn wird der Folger so begrenzt, dass die MinGapCm-
 *   Luecke zum Vordermann nie unterschritten wird - hohe Dichte erzeugt
 *   dadurch natuerlich Stau.
 *
 * Initialisierung: Initialize(Network, Settings) einmal nach dem Laden der
 * Stadt (die Simulation haelt nur einen schwach interpretierten Verweis auf
 * das Netz; der Aufrufer stellt die Lebensdauer sicher).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficSimulation
{
	GENERATED_BODY()

	/** Parameter der Simulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FWiesbadenTrafficSettings Settings;

	/** Aktive Fahrzeuge (Determinismus: Reihenfolge = Spawn-Reihenfolge). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	TArray<FTrafficVehicle> Vehicles;

	/** Momentaufnahme nach dem letzten Tick. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FWiesbadenTrafficReport Report;

	/** Initialisiert die Simulation auf einem Strassennetz (idempotent). */
	void Initialize(const FRoadNetwork& InNetwork, const FWiesbadenTrafficSettings& InSettings);

	/**
	 * Ein Temporary als Netz ist immer ein Fehler - deshalb hier geloescht.
	 *
	 * Initialize merkt sich nur einen Zeiger auf das Netz; der Aufrufer stellt
	 * die Lebensdauer sicher. `Sim.Initialize(MakeNetwork(), ...)` uebergibt
	 * ein Temporary, das am Ende des Ausdrucks stirbt - der Zeiger haengt
	 * danach in der Luft. Elf Testaufrufe machten genau das und liefen
	 * jahrelang unauffaellig durch, weil der freigegebene Speicher zufaellig
	 * unangetastet blieb. Erst eine zusaetzliche Allokation in Initialize hat
	 * ihn neu belegt - und die Simulation stuerzte mit ungueltigen Spur-Indizes
	 * ab, an einer Stelle, die mit der Ursache nichts zu tun hatte.
	 *
	 * Als geloeschte Ueberladung faengt der Compiler das ab, statt es dem
	 * Zufall zu ueberlassen.
	 */
	void Initialize(FRoadNetwork&& InNetwork, const FWiesbadenTrafficSettings& InSettings) = delete;

	/**
	 * Aktiviert die Stopp-Regel: Fahrzeuge, die sich einer roten Ampel
	 * (FWiesbadenTrafficLightSystem) naehern, halten an der Haltelinie. Der
	 * Zeiger ist schwach (der Aufrufer stellt die Lebensdauer sicher, analog
	 * zum Netz-Zeiger). nullptr = keine Ampel-Steuerung.
	 */
	void SetTrafficLightSystem(const FWiesbadenTrafficLightSystem* InTrafficLights);

	/**
	 * Fahrzeuge, die im letzten Tick von einer roten Ampel gehalten wurden.
	 *
	 * Wirksamkeits-Kennzahl: dass das Ampelsystem laeuft, heisst nicht, dass es
	 * den Verkehr beeinflusst - der Zeiger darauf war lange gar nicht gesetzt.
	 */
	int32 GetVehiclesHeldAtRed() const { return LastVehiclesHeldAtRed; }

	/** Seit Initialize aufsummiert: wie oft ein Fahrzeug an einer signalisierten
	 *  Verbindung angehalten wurde bzw. eine solche ueberhaupt anfuhr. Erlaubt der
	 *  Diagnose, "Kopplung defekt" von "keine Ampel auf den befahrenen Spuren" zu
	 *  unterscheiden (nur ~5% der Kreuzungen sind Ampeln). */
	int32 GetLifetimeVehiclesHeldAtRed() const { return LifetimeVehiclesHeldAtRed; }
	int32 GetLifetimeVehiclesApproachingSignal() const { return LifetimeVehiclesApproachingSignal; }

	/** Setzt die Simulation zurueck (kein Netz, keine Fahrzeuge). */
	void Reset();

	/** True, wenn ein Netz initialisiert wurde. */
	bool IsInitialized() const { return Network != nullptr; }

	/** Treibt die Simulation einen Schritt weiter (Spawn, Folgen, Entfernen). */
	void Tick(float DeltaSeconds);

	/**
	 * Setzt die Bezugsposition, um die herum Verkehr entsteht und wieder
	 * entfernt wird - normalerweise die Kameraposition des Spielers.
	 *
	 * Ohne gesetzte Position faellt die Simulation auf das alte Verhalten
	 * zurueck und verteilt Fahrzeuge ueber das ganze Netz. Das ist fuer
	 * datenreine Tests brauchbar, im Spiel aber nicht: dort saehe man nie ein
	 * Fahrzeug.
	 */
	void SetObserverLocation(const FVector& InLocation);

	/**
	 * Meldet das Spielerfahrzeug als Hindernis.
	 *
	 * Ohne das faehrt der Verkehr stur seine Spur ab und ignoriert den
	 * Spieler vollstaendig: er wird gerammt, statt dass gebremst wird. Mit
	 * Kollisionskoerpern allein entstuende sogar der umgekehrte Eindruck -
	 * die Fahrzeuge schoeben den Spieler vor sich her.
	 *
	 * @param Location Position des Spielerfahrzeugs (Radaufstandspunkt).
	 * @param HalfLengthCm Halbe Fahrzeuglaenge - Puffer im Bremsabstand.
	 */
	void SetPlayerObstacle(const FVector& Location, double HalfLengthCm);

	/** Hebt die Hindernis-Meldung auf (kein Spielerfahrzeug in der Welt). */
	void ClearPlayerObstacle();

	/**
	 * Zulaessige Geschwindigkeit eines Fahrzeugs angesichts eines Hindernisses
	 * voraus (datenrein, testbar).
	 *
	 * Das Hindernis wird als stehend angenommen - konservativ und damit
	 * sicher: bremst der Verkehr zu frueh, faellt das kaum auf; bremst er zu
	 * spaet, faehrt er in den Spieler.
	 *
	 * @param CorridorHalfWidthCm Seitlicher Abstand, bis zu dem das Hindernis
	 *        als "in der eigenen Spur" gilt.
	 * @param ReactionDistanceCm  Entfernung, ab der ueberhaupt reagiert wird.
	 * @param DecelerationCmS2    Komfortable Verzoegerung in cm/s^2
	 *        (400 = 4 m/s^2, ein deutliches, aber nicht ruppiges Bremsen).
	 * @return Die (ggf. verringerte) Geschwindigkeit in cm/s.
	 */
	static double ComputeObstacleAwareSpeed(
		double CurrentSpeedCmS,
		const FVector& VehicleLocation,
		const FVector& VehicleForward,
		const FVector& ObstacleLocation,
		double ObstacleHalfLengthCm,
		double CorridorHalfWidthCm,
		double ReactionDistanceCm,
		double MinGapCm,
		double DecelerationCmS2);

	/**
	 * Waehlt Spawn-Spuren im Umkreis einer Position (datenrein, testbar).
	 *
	 * @param Lanes        Spuren des Netzes.
	 * @param Candidates   Zulaessige Spur-Ids (keine Busspuren).
	 * @param Center       Bezugspunkt.
	 * @param RadiusCm     Suchradius; <= 0 liefert alle Kandidaten.
	 * @param OutLaneIds   Spuren im Umkreis, Reihenfolge stabil.
	 */
	static void SelectSpawnLanesNear(
		const TArray<FRoadLane>& Lanes,
		const TArray<int32>& Candidates,
		const FVector& Center,
		double RadiusCm,
		TArray<int32>& OutLaneIds);

	/**
	 * Reine Platzierungsfunktion fuer den Fahrzeug-Spawner (datenrein,
	 * deterministisch, testbar): wandelt die Fahrzeuge in sichtbare
	 * Platzierungen um - 1-km-Culling um ObserverLocation, Yaw aus der
	 * Fahrtrichtung und ein deterministischer Farb-Index (FNV-1a-Hash der
	 * Fahrzeug-Id mod PaletteSize) fuer den ISM-Pool. Radius <= 0 = kein
	 * Culling. PaletteSize wird auf >= 1 geklemmt.
	 */
	static void PlaceTrafficVehicles(
		const TArray<FTrafficVehicle>& Vehicles,
		const FVector& ObserverLocation,
		double CullRadiusCm,
		int32 PaletteSize,
		TArray<FPlacedTrafficVehicle>& OutPlaced);

	/** Liefert die aktuelle Dichte 0..1 (aus den Settings). */
	float GetDensity() const { return Settings.TrafficDensity; }

private:
	/** Wunschgeschwindigkeit eines Fahrzeugs auf seiner aktuellen Spur. */
	double ComputeDesiredSpeed(const FTrafficVehicle& Vehicle) const;

	/** Laenge der aktuellen Bahn (Spur oder Verbindung) in cm. */
	double GetEdgeLengthCm(const FTrafficVehicle& Vehicle) const;

	/**
	 * Wechselt an ein Bahnende auf die Folgebahn. Rueckgabe false = keine
	 * Folgebahn (Sackgasse / kaputtes Netz) - der Aufrufer entfernt.
	 */
	bool AdvanceEdge(FTrafficVehicle& Vehicle);

	/**
	 * Deterministisch gewaehlte Folgespur-Verbindung einer Spur (wie
	 * AdvanceEdge sie nutzt) - fuer die Ampel-Stopp-Regel, damit das Fahrzeug
	 * vor seiner gewaehlten Route haelt. INDEX_NONE = keine Verbindung.
	 */
	int32 PickSuccessorConnection(const FTrafficVehicle& Vehicle) const;

	/** Gewicht der Zielspur einer Verbindung (Strassenklasse). */
	double GetSuccessorWeight(int32 ConnectionIndex) const;

public:
	/**
	 * Wie attraktiv eine Strassenklasse fuer den Durchgangsverkehr ist
	 * (datenrein, testbar).
	 *
	 * Die Wahl an Kreuzungen war zuvor GLEICHVERTEILT - eine Wohnstrasse
	 * wurde so oft genommen wie die Bundesstrasse daneben, und der Verkehr
	 * irrte durch Wohngebiete, waehrend die Hauptachsen leer blieben.
	 */
	static double GetRoadClassWeight(EOSMHighwayType Type);

	/**
	 * Ein Schritt des Einspurmodells (datenrein, testbar).
	 *
	 * Reine Verfolgung: Aus dem Zielpunkt vor dem Fahrzeug folgt der noetige
	 * Radeinschlag delta = atan(2*L*sin(alpha)/Ld), wobei alpha der Winkel
	 * zwischen Fahrzeuglaengsachse und der Sichtlinie zum Ziel ist. Der
	 * Einschlag wird auf MaxSteerRad begrenzt - das IST der Wendekreis - und
	 * darf sich je Sekunde nur um MaxSteerRateRadS aendern. Erst daraus
	 * ergibt sich die Gierrate omega = v/L * tan(delta).
	 *
	 * Nur X und Y werden integriert; die Hoehe setzt der Aufrufer aus der
	 * Bahn, damit die Fahrzeuge auf der Strasse bleiben.
	 */
	static void StepBicycleModel(
		const FVector2D& TargetXY,
		double SpeedCmS,
		double WheelbaseCm,
		double MaxSteerRad,
		double MaxSteerRateRadS,
		double Dt,
		FVector2D& InOutBodyXY,
		float& InOutYawRad,
		float& InOutSteerRad);

private:

	/**
	 * Punkt auf der Sollbahn, AheadCm vor dem Fahrzeug - ueber Bahngrenzen
	 * hinweg.
	 *
	 * Ohne den Blick auf die Folgebahn zielt ein Fahrzeug am Spurende auf das
	 * Spurende selbst und beginnt erst einzulenken, wenn es schon in der
	 * Kreuzung steht. Es wuerde jede Abbiegung anschneiden.
	 */
	FVector GetPathPointAhead(const FTrafficVehicle& Vehicle, double AheadCm) const;

	/** Fuehrt die Karosserie eines Fahrzeugs der Sollbahn nach. */
	void UpdateBodyPose(FTrafficVehicle& Vehicle, double Dt) const;

private:

	/** Position und Fahrtrichtung auf einer Polylinie bei Distanz abtasten. */
	static void SamplePolyline(const TArray<FVector>& Polyline, double DistanceCm,
		FVector& OutLocation, FVector& OutForward);

	/**
	 * Begrenzt die Geschwindigkeit auf das Fahrzeug, das auf der NAECHSTEN
	 * Bahn am weitesten hinten steht.
	 *
	 * Die Kopf-zu-Schwanz-Regel gruppiert nur je Spur bzw. je Verbindung. Ein
	 * Fahrzeug am Spurende sah damit das Fahrzeug nicht, das auf der
	 * Kreuzungsverbindung davor stand - es fuhr hinein, und erst im naechsten
	 * Tick, mit bereits negativem Abstand, wurde es auf 0 geklemmt. Genau so
	 * entstanden die ineinander steckenden Fahrzeuge an den Kreuzungen.
	 */
	void ApplyCrossEdgeHeadway(double Dt);

	/**
	 * Freie Strecke vor der Position AtDistanceCm auf einer Spur, ohne das
	 * Fahrzeug IgnoreVehicleId. Rueckgabe TNumericLimits<double>::Max(), wenn
	 * die Spur frei ist.
	 */
	double ComputeGapAheadOnLane(int32 LaneId, double AtDistanceCm, int32 IgnoreVehicleId) const;

	/** Freie Strecke HINTER der Position - fuer den Spurwechsel. */
	double ComputeGapBehindOnLane(int32 LaneId, double AtDistanceCm, int32 IgnoreVehicleId) const;

	/** Ueberholen: behinderte Fahrzeuge auf eine freie Nachbarspur setzen. */
	void ApplyLaneChanges(float DeltaSeconds);

	/** Deterministischer FNV-1a-Hash ueber zwei uint32. */
	static uint32 Hash2(uint32 A, uint32 B);

	/** 0..0.999 aus einem Hash - fuer die individuelle Geschwindigkeitsstreuung. */
	static float HashFraction(uint32 Hash);

	// Nicht-reflektierte Laufzeit-Daten (kein UPROPERTY - bewusst).
	const FRoadNetwork* Network = nullptr;
	const FWiesbadenTrafficLightSystem* TrafficLights = nullptr;

	/** Zaehler des letzten Ticks - siehe GetVehiclesHeldAtRed(). */
	int32 LastVehiclesHeldAtRed = 0;

	/** Seit Initialize aufsummierte Kennzahlen fuer die ehrliche Ampel-Diagnose. */
	int32 LifetimeVehiclesHeldAtRed = 0;
	int32 LifetimeVehiclesApproachingSignal = 0;
	TArray<int32> SpawnLaneIds;
	TMap<int32, double> ConnectionLengthCm;
	TMap<int32, TArray<int32>> LaneSuccessorIndices; // LaneId -> Verbindungs-Indizes

	/**
	 * Nachbarspuren gleicher Fahrtrichtung: LaneId -> benachbarte LaneIds.
	 *
	 * Zwei Spuren sind benachbart, wenn sie zum selben Strassenabschnitt
	 * gehoeren, dieselbe Richtung haben und ihr Spurindex sich um genau eins
	 * unterscheidet. Gegenspuren sind damit ausgeschlossen - ein Ueberholen
	 * ueber die Gegenfahrbahn will hier niemand.
	 */
	TMap<int32, TArray<int32>> LaneNeighbours;

	/** Fahrzeug-Indizes je Spur, absteigend nach Distanz. Pro Tick neu. */
	TMap<int32, TArray<int32>> VehiclesByLaneCache;
	double SpawnAccumulator = 0.0;
	int64 TotalSpawned = 0;
	int64 TotalRemoved = 0;
	double TotalDistanceCm = 0.0;

	/** Bezugspunkt fuer Spawn und Entfernen (Spielerposition). */
	FVector ObserverLocation = FVector::ZeroVector;
	bool bHasObserver = false;

	/** Spielerfahrzeug als Hindernis, auf das der Verkehr reagiert. */
	FVector PlayerObstacleLocation = FVector::ZeroVector;
	double PlayerObstacleHalfLengthCm = 0.0;
	bool bHasPlayerObstacle = false;

	/**
	 * Spuren im aktuellen Umkreis. Wird nur neu bestimmt, wenn sich der
	 * Beobachter merklich bewegt hat - die Suche laeuft ueber alle 111.000
	 * Spuren und waere je Frame zu teuer.
	 */
	TArray<int32> NearbySpawnLaneIds;
	FVector LastSpawnSearchLocation = FVector::ZeroVector;
	bool bNearbyLanesValid = false;
};
