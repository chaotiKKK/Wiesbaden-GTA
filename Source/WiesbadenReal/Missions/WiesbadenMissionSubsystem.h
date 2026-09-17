// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Missions/WiesbadenMissionTypes.h"
#include "WiesbadenMissionSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnMissionObjectiveChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMissionCompleted, const FMission& /*Completed*/);

/**
 * Fuehrt Missionen zur Laufzeit: laedt den Missions-Pool (JSON) und vergibt
 * ueber FWiesbadenMissionDispatcher nachladend Auftraege - erst die
 * handgeschriebenen Vorlagen, danach endlos prozedurale Kurierjobs. Je Tick
 * (throttled) prueft es das aktuelle Ziel gegen die Spielerposition, schaltet
 * weiter und vergibt bei Abschluss die Belohnung; danach folgt automatisch der
 * naechste Auftrag. Die Fortschritts-/Ziel-/Vergabe-Logik liegt datenrein in
 * FWiesbadenMissionRunner / FWiesbadenMissionDispatcher / FMissionObjective
 * (unit-getestet); dieses Subsystem ist nur die Welt-Anbindung. HUD/Minimap
 * lesen GetCurrentObjective().
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenMissionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Startet die (im Pool vorhandene) Mission mit dieser Id. Fuer skriptbare
	 *  Ausloeser. */
	bool StartMission(FName MissionId);

	/** Vergibt den naechsten Auftrag aus dem Dispatcher-Pool, WENN gerade keiner
	 *  aktiv ist. Ausgeloest durch das Gespraech mit dem NPC am Nordfriedhof -
	 *  ersetzt die frueher automatische Vergabe im Tick. Gibt true zurueck, wenn
	 *  ein Auftrag begonnen wurde (false, wenn schon einer laeuft oder der Pool
	 *  leer ist). */
	bool RequestNextMission();

	/** Aktuelles Ziel oder nullptr (keine aktive Mission). Fuer HUD/Minimap. */
	const FMissionObjective* GetCurrentObjective() const;

	FString GetActiveMissionTitle() const;
	bool HasActiveMission() const { return bHasActiveMission; }

	/** Wie viele Auftraege in dieser Sitzung bereits abgeschlossen wurden. */
	int32 GetCompletedCount() const { return CompletedCount; }

	/**
	 * Restzeit des aktiven Auftrags in Sekunden (>= 0), oder -1 wenn kein Zeitlimit
	 * gilt bzw. kein Auftrag laeuft. Billiger HUD-Haken fuer die Countdown-Anzeige.
	 */
	double GetActiveMissionRemainingSeconds() const;

	FOnMissionObjectiveChanged OnObjectiveChanged;
	FOnMissionCompleted OnMissionCompleted;

private:
	void LoadMissions();
	void BeginMission(const FMission& Mission);

	/** Beendet den aktiven Auftrag (Erfolg ODER Fehlschlag) und rueckt den Vergabe-
	 *  Cursor vor - der gemeinsame Abschluss-Pfad beider Zweige. */
	void EndActiveMission();

	/** Loggt ~1x/s die Restzeit eines befristeten Auftrags (rein diagnostisch). */
	void LogRemainingTime(float DeltaTime);

	/** Persistenter Spielzustand (Guthaben) oder nullptr - fuer Belohnung/Strafe. */
	class UWiesbadenGameStateSubsystem* GetGameStateSubsystem() const;

	/** Schreibt die Missions-Praemie gut (inkl. Kurierlizenz-Bonus) - eigene
	 *  Guthaben-Naht; die Praemien-Politik liegt in FWiesbadenStore::ApplyLicenseBonus. */
	void CreditMissionReward(int32 BaseReward);

	bool TryGetPlayerLocation(FVector& OutLocation) const;

	// Handgeschriebene Vorlagen aus der JSON - Grundlage der Auftragsvergabe.
	TArray<FMission> MissionPool;

	// Aktuell laufender Auftrag (als Wert, da prozedurale Auftraege nicht im Pool
	// stehen). Gueltig nur solange bHasActiveMission.
	FMission ActiveMission;
	bool bHasActiveMission = false;
	int32 ActiveObjectiveIndex = 0;

	// Vergabe-Cursor: Zahl der abgeschlossenen Auftraege. Wird nach jedem
	// Abschluss erhoeht, sodass der Dispatcher stets den NAECHSTEN Auftrag liefert
	// (und nie denselben gerade abgeschlossenen erneut) - ersetzt die fruehere
	// Einmal-Sperre gegen den Wiederhol-Loop.
	int32 CompletedCount = 0;

	float CheckAccumulator = 0.0f;

	// Verstrichene Zeit seit Start des aktiven Auftrags (fuer Zeitlimit-Ziele).
	// Laeuft pro Tick ungedrosselt mit; in BeginMission auf 0 zurueckgesetzt.
	double ActiveMissionElapsed = 0.0;

	// Drosselt den Restzeit-Log auf ~1 Hz, damit die Deadline nicht jeden Frame spammt.
	double RemainingLogAccumulator = 0.0;

	// Ununterbrochene Verweildauer im Radius des AKTUELLEN Ziels (fuer Dwell-Ziele).
	// Bei Zielwechsel und Missionsstart auf 0 zurueckgesetzt.
	double ActiveObjectiveDwell = 0.0;

	// Traegt der Spieler gerade die Missions-Fracht? Bei PickUpCargo-Abschluss
	// gesetzt, bei DropOffCargo-Abschluss und Missionsstart geloescht (Ctx-Zustand
	// fuer DropOffCargo).
	bool bMissionCarryingCargo = false;
};
