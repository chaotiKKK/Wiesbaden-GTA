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
	 *  Ausloeser; die automatische Vergabe laeuft ueber den Dispatcher. */
	bool StartMission(FName MissionId);

	/** Aktuelles Ziel oder nullptr (keine aktive Mission). Fuer HUD/Minimap. */
	const FMissionObjective* GetCurrentObjective() const;

	FString GetActiveMissionTitle() const;
	bool HasActiveMission() const { return bHasActiveMission; }

	/** Wie viele Auftraege in dieser Sitzung bereits abgeschlossen wurden. */
	int32 GetCompletedCount() const { return CompletedCount; }

	FOnMissionObjectiveChanged OnObjectiveChanged;
	FOnMissionCompleted OnMissionCompleted;

private:
	void LoadMissions();
	void BeginMission(const FMission& Mission);
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
};
