// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenMissionSubsystem.h"

#include "Missions/WiesbadenMissionLoader.h"
#include "Missions/WiesbadenMissionRunner.h"
#include "Missions/WiesbadenMissionDispatcher.h"
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
	MissionPool = MoveTemp(Loaded.Missions);
	UE_LOG(LogWbCore, Log, TEXT("Missionen geladen: %d."), MissionPool.Num());
}

void UWiesbadenMissionSubsystem::BeginMission(const FMission& Mission)
{
	ActiveMission = Mission;
	bHasActiveMission = true;
	ActiveObjectiveIndex = 0;
	UE_LOG(LogWbCore, Log, TEXT("Mission gestartet: %s"), *ActiveMission.Title);
	OnObjectiveChanged.Broadcast();
}

bool UWiesbadenMissionSubsystem::StartMission(FName MissionId)
{
	for (const FMission& M : MissionPool)
	{
		if (M.Id == MissionId)
		{
			BeginMission(M);
			return true;
		}
	}
	return false;
}

const FMissionObjective* UWiesbadenMissionSubsystem::GetCurrentObjective() const
{
	if (!bHasActiveMission || !ActiveMission.Objectives.IsValidIndex(ActiveObjectiveIndex))
	{
		return nullptr;
	}
	return &ActiveMission.Objectives[ActiveObjectiveIndex];
}

FString UWiesbadenMissionSubsystem::GetActiveMissionTitle() const
{
	return bHasActiveMission ? ActiveMission.Title : FString();
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
	if (MissionPool.Num() == 0)
	{
		return; // ohne Vorlagen keine Auftraege
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

	// Kein aktiver Auftrag -> naechsten vergeben (nachladend). Der Dispatcher
	// liefert erst die handgeschriebenen Vorlagen, danach endlos prozedurale
	// Kurierjobs. CompletedCount ist der Cursor: dadurch wird nie derselbe
	// gerade abgeschlossene Auftrag erneut angeboten.
	if (!bHasActiveMission)
	{
		const FMissionDispatchResult Next =
			FWiesbadenMissionDispatcher::NextMission(MissionPool, CompletedCount);
		if (Next.bHasMission)
		{
			BeginMission(Next.Mission);
		}
		return;
	}

	FMissionContext Ctx;
	Ctx.PlayerLocation = PlayerLocation;
	const FMissionProgressResult Result = FWiesbadenMissionRunner::Step(
		ActiveMission, ActiveObjectiveIndex, Ctx);
	if (!Result.bAdvanced)
	{
		return;
	}

	ActiveObjectiveIndex = Result.NextObjectiveIndex;
	if (Result.bMissionCompleted)
	{
		const FMission Completed = ActiveMission;
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

		bHasActiveMission = false;
		ActiveObjectiveIndex = 0;
		++CompletedCount;
		UE_LOG(LogWbCore, Log,
			TEXT("Auftrag abgeschlossen (%d gesamt) - naechster Auftrag folgt."), CompletedCount);
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
