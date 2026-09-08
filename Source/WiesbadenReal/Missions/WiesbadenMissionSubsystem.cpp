// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenMissionSubsystem.h"

#include "Missions/WiesbadenMissionLoader.h"
#include "Missions/WiesbadenMissionRunner.h"
#include "Core/WiesbadenGameStateSubsystem.h"
#include "WiesbadenReal.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

void UWiesbadenMissionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadMissions();
}

void UWiesbadenMissionSubsystem::LoadMissions()
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data/Missions/missions.json");
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Missionen: '%s' nicht gefunden - Sandkasten ohne Missionen."), *Path);
		return;
	}

	FMissionLoadResult Loaded = FWiesbadenMissionLoader::ParseMissions(Json);
	for (const FString& Err : Loaded.Errors)
	{
		UE_LOG(LogWbCore, Warning, TEXT("Missionen-Ladefehler: %s"), *Err);
	}
	Missions = MoveTemp(Loaded.Missions);
	UE_LOG(LogWbCore, Log, TEXT("Missionen geladen: %d."), Missions.Num());
}

bool UWiesbadenMissionSubsystem::StartMission(FName MissionId)
{
	for (int32 Index = 0; Index < Missions.Num(); ++Index)
	{
		if (Missions[Index].Id == MissionId)
		{
			ActiveMissionIndex = Index;
			ActiveObjectiveIndex = 0;
			UE_LOG(LogWbCore, Log, TEXT("Mission gestartet: %s"), *Missions[Index].Title);
			OnObjectiveChanged.Broadcast();
			return true;
		}
	}
	return false;
}

const FMissionObjective* UWiesbadenMissionSubsystem::GetCurrentObjective() const
{
	if (!Missions.IsValidIndex(ActiveMissionIndex))
	{
		return nullptr;
	}
	const FMission& Mission = Missions[ActiveMissionIndex];
	if (!Mission.Objectives.IsValidIndex(ActiveObjectiveIndex))
	{
		return nullptr;
	}
	return &Mission.Objectives[ActiveObjectiveIndex];
}

FString UWiesbadenMissionSubsystem::GetActiveMissionTitle() const
{
	return Missions.IsValidIndex(ActiveMissionIndex)
		? Missions[ActiveMissionIndex].Title
		: FString();
}

bool UWiesbadenMissionSubsystem::TryGetPlayerLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return false;
	}
	const APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return false;
	}
	OutLocation = Pawn->GetActorLocation();
	return true;
}

void UWiesbadenMissionSubsystem::Tick(float DeltaTime)
{
	if (Missions.Num() == 0)
	{
		return;
	}

	// ~5 Hz genuegt fuer Ankunfts-Pruefung.
	CheckAccumulator += DeltaTime;
	if (CheckAccumulator < 0.2f)
	{
		return;
	}
	CheckAccumulator = 0.0f;

	FVector PlayerLocation;
	if (!TryGetPlayerLocation(PlayerLocation))
	{
		return; // z. B. waehrend Streaming/Fahrzeugwechsel
	}

	// Auto-Angebot der ersten Mission, solange keine aktiv ist.
	if (ActiveMissionIndex == INDEX_NONE)
	{
		StartMission(Missions[0].Id);
		return;
	}

	FMissionContext Ctx;
	Ctx.PlayerLocation = PlayerLocation;
	const FMissionProgressResult Result = FWiesbadenMissionRunner::Step(
		Missions[ActiveMissionIndex], ActiveObjectiveIndex, Ctx);
	if (!Result.bAdvanced)
	{
		return;
	}

	ActiveObjectiveIndex = Result.NextObjectiveIndex;
	if (Result.bMissionCompleted)
	{
		const FMission Completed = Missions[ActiveMissionIndex];
		UE_LOG(LogWbCore, Log, TEXT("Mission erfuellt: %s."), *Completed.Title);

		// Belohnung zentral gutschreiben (persistenter Spielzustand).
		if (const UWorld* CompletionWorld = GetWorld())
		{
			if (UGameInstance* GI = CompletionWorld->GetGameInstance())
			{
				if (UWiesbadenGameStateSubsystem* GameState =
					GI->GetSubsystem<UWiesbadenGameStateSubsystem>())
				{
					GameState->AddGuthaben(Result.GuthabenAwarded);
				}
				else
				{
					UE_LOG(LogWbCore, Warning,
						TEXT("Kein GameState-Subsystem - Missionsbelohnung nicht gutgeschrieben."));
				}
			}
		}

		ActiveMissionIndex = INDEX_NONE;
		ActiveObjectiveIndex = 0;
		OnMissionCompleted.Broadcast(Completed);
	}
	else
	{
		if (const FMissionObjective* Next = GetCurrentObjective())
		{
			UE_LOG(LogWbCore, Log, TEXT("Ziel erreicht - naechstes Ziel: %s"), *Next->Label);
		}
		OnObjectiveChanged.Broadcast();
	}
}

TStatId UWiesbadenMissionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWiesbadenMissionSubsystem, STATGROUP_Tickables);
}
