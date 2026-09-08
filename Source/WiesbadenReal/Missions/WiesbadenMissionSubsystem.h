// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Missions/WiesbadenMissionTypes.h"
#include "WiesbadenMissionSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnMissionObjectiveChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMissionCompleted, const FMission& /*Completed*/);

/**
 * Fuehrt Missionen zur Laufzeit: laedt die Missions-JSON, bietet die erste
 * Mission automatisch an, prueft je Tick (throttled) das aktuelle Ziel gegen die
 * Spielerposition, schaltet weiter und vergibt bei Abschluss die Belohnung.
 * Die eigentliche Fortschritts-/Ziel-Logik liegt datenrein in
 * FWiesbadenMissionRunner / FMissionObjective (unit-getestet); dieses Subsystem
 * ist nur die Welt-Anbindung. HUD/Minimap lesen GetCurrentObjective().
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenMissionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Startet die Mission mit dieser Id (falls geladen). */
	bool StartMission(FName MissionId);

	/** Aktuelles Ziel oder nullptr (keine aktive Mission). Fuer HUD/Minimap. */
	const FMissionObjective* GetCurrentObjective() const;

	FString GetActiveMissionTitle() const;
	bool HasActiveMission() const { return ActiveMissionIndex != INDEX_NONE; }

	FOnMissionObjectiveChanged OnObjectiveChanged;
	FOnMissionCompleted OnMissionCompleted;

private:
	void LoadMissions();
	bool TryGetPlayerLocation(FVector& OutLocation) const;

	TArray<FMission> Missions;
	int32 ActiveMissionIndex = INDEX_NONE;
	int32 ActiveObjectiveIndex = 0;
	float CheckAccumulator = 0.0f;

	// Auto-Angebot nur EINMAL je Sitzung: verhindert, dass die Mission direkt
	// nach dem Abschluss wieder von vorn startet (siehe Tick).
	bool bAutoOfferConsumed = false;
};
