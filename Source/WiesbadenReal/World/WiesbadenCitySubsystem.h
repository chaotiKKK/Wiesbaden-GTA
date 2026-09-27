// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Core/WiesbadenCityData.h"
#include "GIS/WiesbadenBuildSummary.h"
#include "GIS/WiesbadenPedestrianSimulation.h"
#include "NPC/WiesbadenWanted.h"
#include "World/WiesbadenHealthReport.h"
#include "World/WiesbadenFrameProfiler.h"
#include "World/WiesbadenFallThroughMonitor.h"
#include "GIS/WiesbadenTrafficLights.h"
#include "GIS/WiesbadenTrafficSimulation.h"
#include "World/WiesbadenWeatherSystem.h"

#include "WiesbadenCitySubsystem.generated.h"

class AWiesbadenCityActor;
class AWiesbadenStreamingSource;
class AWiesbadenWorldBuilder;
class UBuildingCollisionSpawnerComponent;
class UWiesbadenGameInstance;
class USoundBase;

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

	/**
	 * Zielort aus `-WbGoto=<Ziel>`: entweder Koordinaten oder ein Strassenname.
	 *
	 * Vorher gab es drei Schalter fuer dieselbe Sache (-WbAtX/-WbAtY,
	 * -WbAtStreet, -WbTour), und ALLE drei hingen hinter -WbScreenshot: ohne
	 * das Flag taten sie stillschweigend nichts. Zwei Messlaeufe gingen so
	 * verloren, ohne dass im Protokoll etwas davon stand.
	 */
	struct FWbGotoTarget
	{
		/** Koordinaten wurden direkt angegeben (cm). */
		bool bHasCoordinates = false;
		FVector2D LocationCm = FVector2D::ZeroVector;

		/** Sonst: der gesuchte Strassenname. */
		FString StreetName;

		bool IsValid() const { return bHasCoordinates || !StreetName.IsEmpty(); }
	};

	/**
	 * Zerlegt die Angabe hinter -WbGoto= (datenrein, testbar).
	 *
	 * "-121964,-119658" -> Koordinaten in cm; alles andere -> Strassenname.
	 * Leerzeichen um das Komma sind erlaubt; ein Name darf Kommas enthalten,
	 * solange nicht BEIDE Teile Zahlen sind.
	 */
	static FWbGotoTarget ParseGotoTarget(const FString& Raw);

	/**
	 * Zerlegt den Ablaufplan aus `-WbShotSteps=<plan>` (datenrein, testbar).
	 *
	 * AUFBE-WERKZEUG, KEIN SPIELVERHALTEN: der Plan sagt einer Aufnahme-
	 * sitzung nur, was sie in welcher Reihenfolge knipsen soll. Er wird
	 * ausserhalb von -WbShotWhenReady gelesen und steuert nichts, was ein
	 * Spielzustand bemerkt.
	 *
	 * Schritte: "modus=N" (Fahrzeugkamera 0 Folge, 1 Orbit, 2 Cockpit),
	 * "hold" (ein Bild aus der aktuellen Sicht), "turm" (Hubschrauber auf
	 * den markierten Helipad des Sebbotower). Alles andere wird als
	 * Posenzeile gelesen - derselbe Weg wie bei -WbShotPoseFile.
	 *
	 * Warum das Pluszeichen der Trenner ist: FParse::Value haelt den Wert am
	 * ersten Komma an, aus "modus=2,hold,hold" wurde deshalb "modus=2" und
	 * die ganze Serie lief als EIN Schritt. Das Pluszeichen umgeht das, ohne
	 * dass die Schritte Kommas enthalten duerfen.
	 */
	static void ParseShotPlan(const FString& Raw, TArray<FString>& OutSteps);

	/**
	 * Mittelpunkt des LAENGSTEN Segments mit diesem Namen (datenrein, testbar).
	 *
	 * Das laengste ist bei Strassen, die es in mehreren Stadtteilen gibt, der
	 * Hauptzug. Der Vergleich ist unabhaengig von Gross-/Kleinschreibung -
	 * "rheinstrasse" soll die "Rheinstraße" finden, ohne dass man die Schreibung
	 * der OSM-Daten kennt.
	 */
	static bool FindStreetLocation(const FRoadNetwork& Network, const FString& Name,
		FVector2D& OutLocationCm, double& OutLengthCm);
	// -- Zustand ------------------------------------------------------------

	/** True, wenn die Welt eine partitionierte Welt ist (World Partition). */
	bool IsWorldPartitionActive() const { return bWorldPartitionActive; }

	/** True, wenn die Stadt-Geometrie gespawnt wurde (Daten vorhanden). */
	bool IsCityReady() const { return CityActor != nullptr; }

	/** Setzt die Kamera aus einer Posenzeile (Format wie -WbShotPoseFile) -
	 *  fuer den Clip-Modus (UWiesbadenClipRecorder, -WbClipPoseFile). */
	void ApplyClipPose(const FString& PoseLine) { ApplyShotPose(PoseLine); }

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

	/** -WbGoto ist erledigt (versetzt oder endgueltig gescheitert). */
	bool bGotoApplied = false;

	/** Wie lange schon auf Pawn/Strassennetz gewartet wird (s). */
	float GotoWaitSeconds = 0.0f;

	/** Der Hinweis auf wirkungslose Aufnahme-Schalter ist raus. */
	bool bStaleGotoFlagsReported = false;

	/** Zuletzt an die Zellen gereichte Stunde des Fensterlichts. */
	float LastWindowLightHours = -1.0f;

	/** Die einmalige Fensterlicht-Bilanz ist raus. */
	bool bWindowLightReported = false;

	/** Sekunden seit dem letzten Durchgang durch die geladenen Zellen. */
	float WindowLightSweepTimer = 0.0f;

	/** Gemeldete Aussetzer - gedeckelt, damit eine lange Fahrt das Protokoll nicht flutet. */
	int32 HitchesReported = 0;

	/** Fahndungskonto (Stufe, Punkte, Grace-Zeit) - FWiesbadenWanted::Step tickt es. */
	FWiesbadenWantedState WantedState;
	FWiesbadenWantedParams WantedParams;

	/** Fussgaenger auf den Gehwegen. Laeuft parallel zum Verkehr. */
	FWiesbadenPedestrianSimulation PedestrianSimulation;

	// -- Fahndungskonto (Polizei) ---------------------------------------------

	/**
	 * MeldeTat: Ein Verbrechen ins Fahndungskonto buchen.
	 *
	 * Rufen die Gewalt-Quellen auf (Waffen-Aufschlag, Explosion, Saegenhieb,
	 * spaeter: zerstoerte Fahrzeuge, getroffene Beamte). Das Konto lebt hier,
	 * weil es weltpersistent sein muss - laenger als jeder Pawn/Actor - und
	 * das Subsystem ohnehin tickt (Abbau dort).
	 *
	 * Bewusst KEIN UFUNCTION: der Enum-Typ ist absichtlich nicht reflektiert
	 * (datenreines Modul wie FWiesbadenPursuer); gerufen wird nur aus C++.
	 */
	void ReportCrime(EWiesbadenCrimeEvent Event);

	/** Fahndungsstand (Stufe 0..6) fuer HUD/Blueprint. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei")
	int32 GetWantedLevel() const { return WantedState.Level; }

	/** Punktekonto (Diagnose/Tests). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei")
	double GetWantedPoints() const { return WantedState.Points; }

	// -- Passanten-Audio (Treffer) ---------------------------------------------

	/**
	 * Treffer auf einen Fussgaenger hoeren (echte Aufnahme).
	 *
	 * Rufen die Gewalt-Quellen, die zugleich melden: Waffen-Aufschlag,
	 * Explosion, Saegenhieb, Ueberfahren. bHeavy waehlt den lauteren Treffer
	 * mit Sturz (Explosion, Volltreffer), sonst den weichen; das Zerplatzen
	 * unter Rad und Saege hat einen eigenen Ton.
	 *
	 * Die Samples kommen aus /Game/Audio/Samples und werden beim ersten
	 * Bedarf geladen und hier gepuffert - derselbe Weg wie beim Schussklang,
	 * nur an einer Stelle fuer alle Quellen.
	 */
	void PlayPedestrianHitSound(const FVector& At, bool bHeavy);

	/** Zerplatzen eines Fussgaengers (Ueberfahren, Saege). */
	void PlayPedestrianBurstSound(const FVector& At);

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

	/** Laufende Bildzeit-Mittelwerte des Fenster-Profilers - die Datenquelle
	 *  der halbtransparenten Profil-Tafel im HUD (WiesbadenProfilOverlay). */
	FWbFrameReport GetFrameReport() const { return FrameProfiler.Report(); }

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

	/** Passanten-Trefferklaenge aus /Game/Audio/Samples (Cache, Uebergang). */
	UPROPERTY(Transient)
	USoundBase* PedestrianHitSample = nullptr;

	UPROPERTY(Transient)
	USoundBase* PedestrianHitHeavySample = nullptr;

	UPROPERTY(Transient)
	USoundBase* PedestrianBurstSample = nullptr;

	bool bGeometryReported = false;

	/** True, sobald -WbTime einmal ausgewertet wurde. */
	bool bTimeOverrideApplied = false;

	/** Stunde aus dem City-Prompt (-1 = keine); geht in ResolveTimeSource ein. */
	float PromptTimeOfDayHours = -1.0f;

	/** EINZIGE Stelle, die die Zeitquelle waehlt: -WbTime vor Prompt-Stunde vor
	 *  lokaler Systemzeit. */
	void ResolveTimeSource();

	/** True, sobald die Streaming-Reichweite einmal gesetzt wurde. */
	bool bLoadingRangeApplied = false;

	// -- Bildzeit-Messung ----------------------------------------------------
	// Die gefensterte Statistik (Mittel/Worst/Ausreisser/Aussetzer + Strangzeiten)
	// liegt datenrein + getestet im FWbFrameProfiler; das Subsystem speist ihn und
	// liest Report() ab. Frueher standen hier ~14 Felder mit ZWEIMAL wortgleicher
	// Reset-Logik im Tick.
	FWbFrameProfiler FrameProfiler;

	/** Verstrichene Zeit fuer -WbQuitAfter. */
	float QuitAfterElapsed = 0.0f;

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

	/** -WbTeleportTo=<X,Y,Z> (cm): setzt Pawn + Streaming-Quelle EINMALIG an eine
	 *  beliebige Koordinate, damit ferne Bauwerke (Tunnel/Bruecken) zum Shot-
	 *  Zeitpunkt gestreamt sind. Verzoegert (WbTeleportToStart, Default 5 s), bis
	 *  Pawn + erstes Streaming stehen. */
	float TeleportToElapsed = 0.0f;
	bool bTeleportToDone = false;

	// Durchfall-Waechter (-WbAutoDrive): Liegt unter dem schnell fahrenden Pawn
	// jederzeit geladene WorldStatic-Kollision? Die Mess-/Verdikt-Logik liegt
	// datenrein + getestet im FWbFallThroughMonitor (World.FallThroughMonitor);
	// das Subsystem faehrt den Pawn und speist ihn ueber einen Welt-Bodenprobe.
	FWbFallThroughMonitor FallMonitor;
	/** Das Ergebnis wurde bereits geschrieben (nur einmal). */
	bool bFallSummaryWritten = false;
	/** Sekundentakt-Drossel fuer das Wagen-Diagnose-Log. */
	int32 LastCarLogSecond = -1;

	/** Schreibt das Durchfall-Test-Ergebnis nach Saved/Diagnose/Durchfall.txt. */
	void WriteFallThroughSummary();

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

	// Optionale Posen-Serie: -WbShotPoseFile=<Pfad> faehrt in EINEM Lauf eine
	// Liste von Kamera-Posen ab und schreibt je Pose einen nummerierten
	// 2x-HighResShot (WbSeries_000.png, _001.png, ...). Eine Pose je Zeile:
	//   Hoehe_m, AtX_cm, AtY_cm, Yaw, Pitch, Vorwaerts_m, LookYaw, LookPitch
	/** Geladene Posenzeilen der Serie (leer = Einzelbild wie bisher). */
	TArray<FString> ShotPoseLines;
	/** Aktuelle Pose in der Serie. */
	int32 ShotPoseIndex = 0;
	/** true = Bild ausgeloest, wartet aufs Rendern, bevor die Kamera weiterzieht. */
	bool bShotCapturing = false;
	/** Settle-Zeit je Folge-Pose (Streaming/Belichtung), -WbPoseSettle=<s>. */
	float ShotPoseSettle = 5.0f;

	/** Loest den 2x-HighResShot aus. SeriesIndex >= 0 nummeriert (WbSeries_NNN),
	 *  < 0 schreibt das Einzelbild WbReadyShot. */
	void FireReadyHighResShot(int32 SeriesIndex);

	/** Setzt die Aufnahme-Kamera aus einer Posenzeile der Serie. */
	void ApplyShotPose(const FString& PoseLine);

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
	 * Versetzt Spieler UND Kamera an den Zielort. Wirkt in JEDEM Startmodus,
	 * nicht nur bei der Aufnahme.
	 *
	 * @return true, wenn versetzt wurde (oder endgueltig aufgegeben wurde).
	 *         false = noch nicht bereit, im naechsten Tick erneut versuchen.
	 */
	bool TryApplyGotoTarget();

public:
	/** -WbGoto noch einmal ausfuehren - fuer die Figurprobe, die damit den
	 *  FUSS-Pawn versetzt (beim Start trifft Goto das Auto). */
	void RepeatGoto() { bGotoApplied = false; GotoWaitSeconds = 0.0f; }

private:

	/**
	 * Setzt die Ansicht auf eine Kamera ueber dem Spieler (-WbAerial=<Meter>).
	 *
	 * Aus der Fahrerkamera laesst sich nicht beurteilen, ob das Strassennetz
	 * ueberhaupt gerendert wird oder vom Gelaende verdeckt ist - von oben zeigt
	 * ein einziges Bild beides.
	 */
	bool SetupAerialView(float HeightMeters);

	/**
	 * Sucht in den geladenen Chunks eine Relief-Fassade (Backstein/Sandstein)
	 * und stellt die Aufnahme-Kamera nah davor. Fuer -WbGotoFacade=<brick|
	 * sandstone>: Nah-Aufnahmen der neuen Fassaden ohne bekannte Koordinaten.
	 */
	bool SetupFacadeCloseup(const FString& Variant);

	/** Setzt die Aufnahme-Kamera aus expliziten Werten (Serie und Einzelbild
	 *  teilen sich diesen Kern). bHasAt = absolute Zielkoordinaten AtX/AtY. */
	bool SetupAerialViewParams(float HeightMeters, bool bHasAt, float AtX, float AtY,
		float Yaw, float Pitch, float ForwardMeters, float LookYaw, float LookPitch);

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
