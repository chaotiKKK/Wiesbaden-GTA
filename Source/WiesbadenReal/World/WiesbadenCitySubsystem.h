// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Core/WiesbadenCityData.h"
#include "GIS/WiesbadenBuildSummary.h"
#include "GIS/WiesbadenPedestrianSimulation.h"
#include "World/WiesbadenHealthReport.h"
#include "GIS/WiesbadenTrafficLights.h"
#include "GIS/WiesbadenTrafficSimulation.h"
#include "World/WiesbadenWeatherSystem.h"

#include "WiesbadenCitySubsystem.generated.h"

class AWiesbadenCityActor;
class AWiesbadenStreamingSource;
class AWiesbadenWorldBuilder;
class UBuildingCollisionSpawnerComponent;
class UWiesbadenGameInstance;

/**
 * Per-Welt-Orchestrierung der Stadt (UWorldSubsystem).
 *
 * ABLAUF (OnWorldBeginPlay):
 *  1. World Partition pruefen (IsPartitionedWorld) und Streaming-Quelle
 *     registrieren (folgt dem Player-Pawn -> Zellen streamen mit).
 *  2. Stadt-Daten aus dem UWiesbadenGameInstance holen:
 *     - bereits geladen (Levelwechsel)  -> direkt spawnen,
 *     - bGenerateAtRuntime + keine Daten -> Laufzeit-Build anstossen und auf
 *       OnCityDataLoaded warten, dann spawnen,
 *     - Produktion (kein Laufzeit-Build) -> gebackene World-Partition-Map
 *       erwartet, nichts generieren.
 *  3. Stadt-Geometrie als AWiesbadenCityActor spawnen (Procedural-Meshes,
 *     Materialien aus der GameInstance-Konfiguration).
 *
 * STREAMING-ZUSTAND: Jede Tick wird UWorldPartitionSubsystem::
 * IsStreamingCompleted() abgefragt und der Zustand als Status gemeldet -
 * damit ist sichtbar, ob die Zellen rund um den Player vollstaendig geladen
 * sind (z. B. fuer einen "Weiter"-Gate beim Spielstart).
 *
 * Alle Zustandswechsel werden ueber OnCityStateChanged (Blueprints) und die
 * Status-Getter gemeldet. Das Subsystem ist pro Welt genau einmal vorhanden.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenCitySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// -- Zustand ------------------------------------------------------------

	/** True, wenn die Welt eine partitionierte Welt ist (World Partition). */
	bool IsWorldPartitionActive() const { return bWorldPartitionActive; }

	/** True, wenn die Stadt-Geometrie gespawnt wurde (Daten vorhanden). */
	bool IsCityReady() const { return CityActor != nullptr; }

	/** True, wenn alle World-Partition-Zellen um die Quellen geladen sind. */
	bool IsCityStreamingComplete() const { return bStreamingComplete; }

	/** Aktueller menschenlesbarer Status der Stadt. */
	FString GetCityStatus() const { return CityStatus; }

	/** Leer bei Erfolg, sonst die letzte Fehlermeldung. */
	FString GetLastCityError() const { return LastCityError; }

	/**
	 * Letzter Laufzeit-Build als gemeinsame USTRUCT (delegiert an den
	 * GameInstance; leer, wenn keiner gelaufen). Die Einzel-Getter darunter
	 * sind Komfort-Wrapper auf diese eine Quelle.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FLastBuildInfo GetLastBuildInfo() const;

	/** Zeitpunkt des letzten Laufzeit-Builds (lokal); leer, wenn keiner gelaufen. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetLastBuildTimestamp() const;

	/** Dauer des letzten Laufzeit-Builds in Sekunden (0 = keiner gelaufen). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	double GetLastBuildDurationSeconds() const;

	/** Ergebnis des letzten Laufzeit-Builds ("ok" / "fehlgeschlagen: ..."). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetLastBuildResult() const;

	/** Kompakter Einzeiler des letzten Laufzeit-Builds (leer, wenn keiner). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetLastBuildSummary() const;

	/**
	 * Ergebnis der Terrain-Qualitaetskontrolle des letzten Laufzeit-Builds
	 * (leere WarningMessage = ok). Delegiert null-sicher an den GameInstance;
	 * damit erreichen Level-Blueprints die Warnung ohne GameInstance-Zugriff.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FTerrainQualityReport GetTerrainQuality() const;

	/** Der gespawnte Stadt-Actor (nullptr, solange keine Stadt existiert). */
	AWiesbadenCityActor* GetCityActor() const { return CityActor; }

	// -- Steuerung ------------------------------------------------------------

	/**
	 * Datenreine Laufzeit-Entscheidung: Soll der Laufzeit-Build gestartet
	 * werden? false, wenn die Stadt bereits gebacken im Level liegt (Map nach
	 * Editor-Build), wenn Daten vorhanden sind, wenn der Laufzeit-Build
	 * deaktiviert ist oder wenn ein Build bereits laeuft.
	 */
	static bool ShouldRunRuntimeBuild(
		bool bCityBakedInLevel,
		bool bHasCityData,
		bool bGenerateAtRuntime,
		bool bIsCityDataLoading)
	{
		if (bCityBakedInLevel) { return false; }
		if (bHasCityData) { return false; }
		if (!bGenerateAtRuntime) { return false; }
		if (bIsCityDataLoading) { return false; }
		return true;
	}

	/**
	 * Initialisiert die Stadt (idempotent): Daten holen/generieren und
	 * Geometrie spawnen. Wird automatisch in OnWorldBeginPlay aufgerufen und
	 * kann vom GameMode/Blueprint erneut aufgerufen werden.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden")
	void InitializeCity();

	/** Entfernt die gespawnte Stadt-Geometrie (nicht die Daten im GameInstance). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden")
	void ClearCity();

	// -- Wetter ------------------------------------------------------------

	/**
	 * Setzt die Ziel-Wetterlage (ueberschreibt die aus dem City-Prompt).
	 * Der Wechsel blendet sanft ueber die TransitionSeconds des Wetter-Systems.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Weather")
	void SetWeatherTarget(ECityWeatherPreset NewWeather);

	/** Aktueller Wetter-/Tageszeit-Zustand (fuer HUD/Rendering/Blueprint). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Weather")
	FWiesbadenWeatherState GetWeatherState() const { return Weather.GetState(); }

	/** Wetter-Zustandsmaschine (datenrein, pro Welt; Tageszeit + Wetterlage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Weather")
	FWiesbadenWeatherSystem Weather;

	// -- Verkehr ------------------------------------------------------------

	/**
	 * Datenreine Verkehrs-Simulation auf dem Strassennetz (Fahrzeuge folgen
	 * dem Spur-Graph; TrafficDensity aus dem City-Prompt steuert die
	 * Spawn-Rate). Wird beim Stadt-Spawn initialisiert und pro Tick getrieben.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Traffic")
	FWiesbadenTrafficSimulation TrafficSimulation;

	/**
	 * Ampel-Steuerung der Kreuzungen mit highway=traffic_signals.
	 *
	 * Muss die Simulation ueberleben, weil diese nur einen ZEIGER darauf haelt
	 * (SetTrafficLightSystem). Ein lokales System waere nach der
	 * Initialisierung zerstoert und der Zeiger ungueltig.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Traffic")
	FWiesbadenTrafficLightSystem TrafficLightSystem;

	/** Fussgaenger auf den Gehwegen. Laeuft parallel zum Verkehr. */
	FWiesbadenPedestrianSimulation PedestrianSimulation;

	/** Momentaufnahme der Verkehrs-Simulation (HUD/Blueprint). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Traffic")
	FWiesbadenTrafficReport GetTrafficReport() const { return TrafficSimulation.Report; }

	/** Aktive Fahrzeuge der Verkehrs-Simulation (fuer Spawner/HUD). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Traffic")
	const TArray<FTrafficVehicle>& GetTrafficVehicles() const { return TrafficSimulation.Vehicles; }

	/** Fuellt den maschinenlesbaren Gesundheitsbericht aus dem Live-Zustand (nur
	 *  Rohzahlen; Interpretation/JSON liegen in FWiesbadenHealthReport). Fuer den
	 *  WbHealth-Exec und externe Analyse. */
	FWiesbadenHealthReport BuildHealthReport() const;

	// -- Ereignisse ------------------------------------------------------------

	/** Status der Stadt hat sich geaendert (Status + Erfolg). */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWiesbadenCityStateChanged, FString, Status, bool, bSuccess);
	UPROPERTY(BlueprintAssignable, Category = "Wiesbaden")
	FOnWiesbadenCityStateChanged OnCityStateChanged;	protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	/** GameInstance der aktuellen Welt (oder nullptr). */
	UWiesbadenGameInstance* GetGameInstance() const;

	/** Wird ausgeloest, wenn der Laufzeit-Build des GameInstance fertig ist. */
	void OnCityDataLoaded();

	/** Spawnt den Stadt-Actor aus den Daten. */
	void SpawnCityActor(const FWiesbadenCityData& Data);

	/**
	 * Erzeugt einen CityActor als reinen Wirt fuer den Fahrzeug-Pool.
	 *
	 * Fuer die gebackene Stadt: Strassen, Gebaeude und Terrain liegen bereits
	 * im Level, es fehlt nur der InstancedStaticMesh-Pool, der die Fahrzeuge
	 * der Verkehrs-Simulation zeichnet. ApplyCityData wird daher nicht
	 * aufgerufen - die Procedural-Meshes des Actors bleiben leer.
	 */
	void SpawnTrafficHostActor();

	/** True, sobald die einmalige Verkehrsbilanz geloggt wurde. */
	bool bTrafficReported = false;

	/** Wartezeit bis zur Verkehrsbilanz - die Simulation braucht einen Anlauf. */
	float TrafficReportDelay = 0.0f;

	/** True, sobald die einmalige Geometrie-Bilanz geloggt wurde. */
	/** Verzoegerung und Sperre der einmaligen Fussgaenger-Bilanz. */
	/**
	 * Kollisionskoerper fuer die Gebaeude in Spielernaehe.
	 *
	 * Die Stadt-Meshes werden ohne Kollision gebacken (bCreateCollision =
	 * false) - ohne diese Koerper faehrt der Spieler durch die Haeuser.
	 */
	UPROPERTY(Transient)
	UBuildingCollisionSpawnerComponent* BuildingCollision = nullptr;

	/** Verzoegerung und Sperre der einmaligen Ampel-Bilanz. */
	float TrafficLightReportDelay = 0.0f;
	bool bTrafficLightsReported = false;

	float PedestrianReportDelay = 0.0f;
	bool bPedestriansReported = false;

	bool bGeometryReported = false;

	/** True, sobald -WbTime einmal ausgewertet wurde. */
	bool bTimeOverrideApplied = false;

	/** True, sobald die Streaming-Reichweite einmal gesetzt wurde. */
	bool bLoadingRangeApplied = false;

	// -- Bildzeit-Messung ----------------------------------------------------
	//
	// "Es ruckelt" ist keine Groesse, mit der sich arbeiten laesst. Diese
	// Zaehler liefern eine: mittlere Bildzeit, schlechteste Bildzeit und die
	// Zahl der Aussetzer ueber 50 ms. Ein hoher Mittelwert heisst
	// durchgaengige Last, ein niedriger Mittelwert mit Aussetzern heisst
	// Nachladen - das sind zwei voellig verschiedene Ursachen.
	double FrameTimeSumMs = 0.0;
	double WorstFrameMs = 0.0;
	int32 FrameCount = 0;
	int32 HitchCount = 0;

	/** Bilder, die mehr als doppelt so lange dauerten wie das laufende Mittel. */
	int32 SpikeCount = 0;

	/** Vorlauf, bis die Messung beginnt (Streaming und Shader sind dann durch). */
	float MeasurementDelay = 0.0f;

	/** Verstrichene Zeit fuer -WbQuitAfter. */
	float QuitAfterElapsed = 0.0f;
	bool bMeasurementStarted = false;

	/** Aufsummierte Zeit je Simulation, fuer die Aufteilung der Bildzeit. */
	double LightTimeMs = 0.0;
	double TrafficTimeMs = 0.0;
	double PedestrianTimeMs = 0.0;
	double SubsystemTimeMs = 0.0;
	double GameThreadTimeMs = 0.0;
	double RenderThreadTimeMs = 0.0;

	/** Summierte Zeit der Grafikkarte je Bild.
	 *
	 * Getrennt von RenderThreadTimeMs, weil beide etwas anderes messen:
	 * Der Render-Strang schiebt Befehle weg, die Karte arbeitet sie ab.
	 * Ist die Karte der Engpass, bleibt der Strang klein und die Bildzeit
	 * trotzdem gross - genau diese Luecke hat mich einmal zu der falschen
	 * Aussage gebracht, die Materialien seien die Last. */
	double GpuTimeMs = 0.0;

	// -- Perf-Snapshot-Cache ------------------------------------------------
	// EINMAL in CachePerfSnapshot() erhoben (8-s-Block), danach von
	// BuildHealthReport gelesen. So bleibt der Report-Bau billig, obwohl die
	// Erhebung alle Actors durchlaeuft.
	int32 PerfPrimComps = 0;
	int32 PerfMovableComps = 0;
	int32 PerfCollisionComps = 0;
	int32 PerfInstanceComps = 0;
	int32 PerfInstanceCount = 0;
	int32 PerfSectionsTotal = 0;
	int32 PerfSectionsNoMaterial = 0;
	bool bPerfSnapshotValid = false;

	/** Laufzeit bis zum einmaligen GPU-Profil (-WbProfileGPU=<Sekunden>). */
	/** Das Strassennetz wurde bereits ausgeschrieben (-WbDumpStreets). */
	bool bStreetsDumped = false;

	/**
	 * Schreibt jedes Segment des gebauten Netzes als CSV.
	 *
	 * Spalten: OSM-Way-Id, Name, Strassenart, Punktzahl, Laenge in Metern,
	 * Anfangs- und Endpunkt. Damit laesst sich gegen die Quelldaten pruefen,
	 * WELCHE Wege fehlen - nicht nur, dass an einer Stelle etwas fehlt.
	 */
	void DumpStreetNetwork(const FString& Path) const;

	/** Laufzeit seit Spielbeginn fuer die gleichmaessige Fahrt (-WbAutoDrive). */
	float AutoDriveElapsed = 0.0f;

	/** Die Fahrt hat begonnen; AutoDriveOrigin ist gesetzt. */
	bool bAutoDriveStarted = false;

	/** Startpunkt der Fahrt - fuer die zurueckgelegte Strecke im Protokoll. */
	FVector AutoDriveOrigin = FVector::ZeroVector;

	/** Zurueckgelegte Strecke der gleichmaessigen Fahrt in cm. */
	double AutoDriveDistanceCm = 0.0;

	/** Laufzeit bis zum Bild der Messstelle (-WbShot=<Sekunden>). */
	float ShotElapsed = 0.0f;

	float ProfileGpuElapsed = 0.0f;

	/** Materialnamen sind eingeschaltet, das Profil folgt zwei Sekunden spaeter. */
	bool bProfileGpuArmed = false;

public:
	/**
	 * Ladereichweite der World Partition in Metern.
	 *
	 * Strassen und Gebaeude liegen in gestreamten Chunks, Baeume, Schilder und
	 * Laternen dagegen am immer geladenen WorldBuilder. Ist die Reichweite zu
	 * klein, stehen in der Ferne Baeume und Schilder auf blanker Wiese,
	 * waehrend die Strassen fehlen - im Spiel sah das aus wie zerrissene
	 * Strassen, obwohl die Geometrie vollstaendig ist (aus 400 m Hoehe ist das
	 * Netz lueckenlos).
	 *
	 * Der UE-Standard von 768 m reicht fuer die Fahrt, nicht fuer den Flug.
	 * Nach oben begrenzt der Speicher: die Stadt hat 1.984 Chunks, alle
	 * gleichzeitig geladen passen nicht ins RAM.
	 *
	 * Mit -WbLoadRange=<Meter> zur Laufzeit uebersteuerbar.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "200.0"))
	float WorldPartitionLoadingRangeMeters = 2500.0f;

private:

	/** Countdown bis zum Beenden nach dem Diagnose-Screenshot (negativ = aus). */
	float ScreenshotQuitDelay = -1.0f;

	/** Countdown bis zur Luftaufnahme nach dem Kamerawechsel (negativ = aus). */
	float AerialShotDelay = -1.0f;

	// -- WbShotWhenReady ------------------------------------------------------
	// Automatischer 2x-HighResShot, sobald die Stadt fertig ist. Loest das
	// Timing-Problem des Laufzeit-Builds: der ist asynchron (~22 s), waehrend
	// -WbScreenshot fix bei 8 s feuert und HighResShot per -ExecCmds zum Start.
	// Scharf ueber -WbShotWhenReady; Skalierung -WbShotScale=<n> (Default 2);
	// beendet nach dem Schreiben, ausser -WbShotNoQuit.
	/** Scharfgeschaltet, sobald IsCityReady() true wurde (dann Settle-Countdown). */
	bool bShotWhenReadyArmed = false;
	/** Bereits ausgeloest (einmalig je Sitzung). */
	bool bShotWhenReadyFired = false;
	/** Settle-Countdown, damit Rendering/Streaming/Shader eingeschwungen sind. */
	float ShotWhenReadyDelay = -1.0f;

	/** Loest den 2x-HighResShot aus (einmalig, wenn die Stadt bereit ist). */
	void FireReadyHighResShot();

	// -- Kreuzungs-Rundgang ---------------------------------------------------
	//
	// "Die Kreuzungen sind kaputt" liess sich aus der Fahrerkamera nie
	// beurteilen: Man kommt nicht gezielt hin, und ein einzelner Blickwinkel
	// verbirgt mehr als er zeigt. Der Rundgang stellt die Kamera an jede
	// ausgewaehlte Kreuzung - einmal senkrecht von oben, dann aus vier
	// Himmelsrichtungen schraeg - und legt je Ansicht ein Bild ab.
	//
	// Aufruf: -WbTour=<Anzahl Kreuzungen>

	/** Kameraposen des Rundgangs, in der Reihenfolge der Aufnahme. */
	struct FTourPose
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FString Label;
	};

	TArray<FTourPose> TourPoses;

	/** Index der naechsten Pose; INDEX_NONE = kein Rundgang. */
	int32 TourIndex = INDEX_NONE;

	/** True, wenn fuer den Rundgang versetzt wurde - dann laenger warten. */
	bool bTourTeleported = false;

	/** Restzeit, bis die aktuelle Pose fotografiert wird. */
	float TourDelay = 0.0f;

	/** Kamera des Rundgangs (wird wiederverwendet). */
	UPROPERTY(Transient)
	AActor* TourCamera = nullptr;

	/** Baut die Posenliste. Rueckgabe false = keine Kreuzungen gefunden. */
	bool SetupJunctionTour(int32 JunctionCount);

	/** Treibt den Rundgang; wird aus Tick gerufen. */
	void TickJunctionTour(float DeltaSeconds);

	/** Wartezeit bis zur Geometrie-Bilanz - World Partition braucht Anlauf. */
	float GeometryReportDelay = 0.0f;

	/**
	 * Erhebt den Perf-Snapshot (Primitive-Komponenten, Instanzen, Mesh-Abschnitte
	 * ohne Material) EINMAL und legt ihn in den Cache-Feldern ab. Wird im
	 * 8-s-Diagnoseblock gerufen; BuildHealthReport liest danach nur noch den Cache
	 * (kein Actor-Durchlauf auf dem Hot-Path von WbHealth-Gate/Inline-Diagnosen).
	 */
	void CachePerfSnapshot();

	/** Zaehlt geladene Chunk-Actors und ihre Mesh-Abschnitte. Die Last-Inventar-
	 *  Zeile liest ihre Zahlen aus dem uebergebenen Report. */
	void LogGeometryBalance(const FWiesbadenHealthReport& Report) const;

	/**
	 * Zaehlt Mesh-Abschnitte ohne zugewiesenes Material.
	 *
	 * Solche Abschnitte rendert Unreal mit dem Default-Material (graues
	 * Schachbrett). Genau das war der Fall, als die fertig gebaute Stadt
	 * "leer" aussah - die Geometrie-Bilanz meldete korrekt tausende
	 * Abschnitte, aber keine Kennzahl verriet, dass keiner davon ein
	 * Material hatte.
	 *
	 * Liest die Zahlen aus dem uebergebenen Report (Perf-Snapshot).
	 */
	void LogMaterialBalance(const FWiesbadenHealthReport& Report) const;

	/**
	 * Misst am Spielerort die Hoehenlage von Strassen-, Gebaeude-Mesh und
	 * Gelaende.
	 *
	 * Aus einem Bild allein ist nicht zu unterscheiden, ob die Strassen fehlen,
	 * leer sind oder unter dem Gelaende liegen - alle drei sehen von oben wie
	 * "keine Strasse" aus. Diese Zahlen trennen die Faelle.
	 */
	void LogHeightStackNearPlayer() const;

	/**
	 * Schreibt einen Screenshot nach Saved/Diagnose und beendet das Spiel
	 * danach. Nur aktiv mit -WbScreenshot auf der Kommandozeile, damit ein
	 * normaler Spielstart unberuehrt bleibt.
	 */
	void RequestDiagnosticScreenshot();

	/**
	 * Setzt die Ansicht auf eine Kamera ueber dem Spieler (-WbAerial=<Meter>).
	 *
	 * Aus der Fahrerkamera laesst sich nicht beurteilen, ob das Strassennetz
	 * ueberhaupt gerendert wird oder vom Gelaende verdeckt ist - von oben zeigt
	 * ein einziges Bild beides.
	 */
	bool SetupAerialView(float HeightMeters);

	/** Loest den Screenshot aus und startet den Countdown zum Beenden. */
	void CaptureDiagnosticScreenshot();

	/** Erzeugt die World-Partition-Streaming-Quelle (folgt dem Player). */
	void EnsureStreamingSource();

	/** Spawnt einmalig eine unbegrenzte PostProcessVolume, die die Belichtung
	 *  klemmt (gegen den flachen/ueberbelichteten Look) und dezent Kontrast/
	 *  Saettigung anhebt - fuer einen kohaerenten Bildeindruck. */
	void EnsureCinematicLighting(UWorld& World);

	/** Fragt den World-Partition-Streaming-Zustand ab und meldet Wechsel. */
	void UpdateStreamingState();

	/** Setzt Status/Fehler und broadcastet das BP-Ereignis. */
	void BroadcastState(const FString& Status, bool bSuccess);

	/** True, wenn ein AWiesbadenWorldBuilder mit bCityBaked im Level liegt. */
	bool HasBakedCityInLevel() const;

	/**
	 * Liefert den gebackenen WorldBuilder-Actor (bCityBaked) im Level - oder
	 * nullptr. Dessen RoadNetwork/TrafficSettings (nicht-transiente
	 * UPROPERTYs, in der Map serialisiert) treiben im gebackenen Pfad die
	 * Verkehrs-Simulation.
	 */
	AWiesbadenWorldBuilder* FindBakedCityBuilder() const;

	bool bWorldPartitionActive = false;
	bool bStreamingComplete = false;
	bool bStreamingCheckedOnce = false;

	/** Aus der GameInstance-Konfiguration uebernommene Spawn-Parameter. */
	bool bCreateCollision = false;

	FString CityStatus = TEXT("Nicht initialisiert");
	FString LastCityError;

	/** Handle auf das GameInstance-Ereignis (zum Abbinden in Deinitialize). */
	FDelegateHandle OnCityDataLoadedHandle;

	/** Schwacher Verweis auf den GameInstance (fuer das Delegate-Abbinden). */
	TWeakObjectPtr<UWiesbadenGameInstance> WeakGameInstance;

	UPROPERTY(Transient)
	AWiesbadenCityActor* CityActor = nullptr;

	UPROPERTY(Transient)
	AWiesbadenStreamingSource* StreamingSource = nullptr;
};
