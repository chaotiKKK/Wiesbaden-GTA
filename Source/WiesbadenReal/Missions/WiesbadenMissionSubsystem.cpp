// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenMissionSubsystem.h"

#include "Missions/WiesbadenMissionLoader.h"
#include "Missions/WiesbadenMissionRunner.h"
#include "Missions/WiesbadenMissionDispatcher.h"
#include "Missions/WiesbadenMissionDeadline.h"
#include "Core/WiesbadenGameStateSubsystem.h"
#include "Store/WiesbadenStore.h"
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
	ActiveMissionElapsed = 0.0;
	RemainingLogAccumulator = 0.0;

	// Auto-Modus: faire, distanzabhaengige Frist aus der Route (Spielerposition ->
	// Ziele) berechnen, damit jede befristete Mission SCHAFFBAR bleibt. Danach ist
	// die Frist ein fester positiver Wert.
	if (ActiveMission.IsAutoDeadline())
	{
		FVector Start = FVector::ZeroVector;
		TryGetPlayerLocation(Start); // ohne Pawn: Ursprung als Route-Start
		ActiveMission.DeadlineSeconds = FWiesbadenMissionDeadline::ComputeSeconds(
			Start, ActiveMission.Objectives, FMissionDeadlineParams());
	}

	if (ActiveMission.HasDeadline())
	{
		UE_LOG(LogWbCore, Log, TEXT("Mission gestartet: %s (Zeitlimit %.0f s)."),
			*ActiveMission.Title, ActiveMission.DeadlineSeconds);
	}
	else
	{
		UE_LOG(LogWbCore, Log, TEXT("Mission gestartet: %s"), *ActiveMission.Title);
	}
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

double UWiesbadenMissionSubsystem::GetActiveMissionRemainingSeconds() const
{
	if (!bHasActiveMission || !ActiveMission.HasDeadline())
	{
		return -1.0; // unbefristet oder kein Auftrag
	}
	return FMath::Max(0.0, ActiveMission.DeadlineSeconds - ActiveMissionElapsed);
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

UWiesbadenGameStateSubsystem* UWiesbadenMissionSubsystem::GetGameStateSubsystem() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UWiesbadenGameStateSubsystem>() : nullptr;
}

void UWiesbadenMissionSubsystem::CreditMissionReward(int32 BaseReward)
{
	// Belohnungsgutschrift eines abgeschlossenen Auftrags - die eigene Naht fuers
	// Guthaben. Die Praemien-POLITIK (Kurierlizenz +50%) liegt datenrein und
	// unit-getestet in FWiesbadenStore::ApplyLicenseBonus; hier nur die Anbindung
	// an den persistenten Spielzustand.
	UWiesbadenGameStateSubsystem* GameState = GetGameStateSubsystem();
	if (!GameState)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Kein GameState-Subsystem - Missionsbelohnung nicht gutgeschrieben."));
		return;
	}

	// Kurierlizenz (gekaufte Freischaltung) erhoeht die Praemie um 50% - die
	// Ausgabe-Senke zahlt sich im Kurier-Loop wieder aus.
	const bool bLicensed = GameState->HasUnlock(FWiesbadenStore::KurierlizenzId());
	const int32 Award = FWiesbadenStore::ApplyLicenseBonus(BaseReward, bLicensed);
	if (bLicensed)
	{
		UE_LOG(LogWbCore, Log, TEXT("Kurierlizenz-Bonus: %d -> %d."), BaseReward, Award);
	}
	GameState->AddGuthaben(Award);
}

void UWiesbadenMissionSubsystem::EndActiveMission()
{
	// Gemeinsamer Abschluss-Pfad fuer Erfolg UND Fehlschlag: aktiven Auftrag beenden
	// und den Vergabe-Cursor vorruecken, damit der Dispatcher im naechsten Tick den
	// NAECHSTEN Auftrag liefert (nie denselben erneut). Das erfolgs-/fehlschlag-
	// spezifische (Belohnung, Meldung, Delegate) bleibt beim Aufrufer.
	bHasActiveMission = false;
	ActiveObjectiveIndex = 0;
	++CompletedCount;
}

void UWiesbadenMissionSubsystem::LogRemainingTime(float DeltaTime)
{
	// Rein diagnostisch: ~1x/s die Restzeit eines befristeten Auftrags loggen. Die
	// eigentliche Frist-Auswertung macht der Runner (ueber Ctx.ElapsedSeconds).
	if (!ActiveMission.HasDeadline())
	{
		return; // unbefristet -> nichts zu melden
	}
	RemainingLogAccumulator += DeltaTime;
	if (RemainingLogAccumulator >= 1.0)
	{
		RemainingLogAccumulator = 0.0;
		const double Remaining = ActiveMission.DeadlineSeconds - ActiveMissionElapsed;
		UE_LOG(LogWbCore, Log, TEXT("Auftrag '%s': noch %.0f s."),
			*ActiveMission.Title, FMath::Max(0.0, Remaining));
	}
}

void UWiesbadenMissionSubsystem::Tick(float DeltaTime)
{
	if (MissionPool.Num() == 0)
	{
		return; // ohne Vorlagen keine Auftraege
	}

	// Verstrichene Zeit ungedrosselt mitzaehlen (die Ankunfts-Pruefung unten laeuft
	// nur ~5 Hz, die Frist braucht aber die ECHTE Zeit); das Restzeit-Log haengt an
	// seinem eigenen Helfer.
	if (bHasActiveMission)
	{
		ActiveMissionElapsed += DeltaTime;
		LogRemainingTime(DeltaTime);
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
	Ctx.ElapsedSeconds = ActiveMissionElapsed;
	const FMissionProgressResult Result = FWiesbadenMissionRunner::Step(
		ActiveMission, ActiveObjectiveIndex, Ctx);
	if (!Result.bAdvanced)
	{
		return;
	}

	ActiveObjectiveIndex = Result.NextObjectiveIndex;

	if (Result.bMissionFailed)
	{
		// Zeitlimit gerissen: statt Belohnung eine Vertragsstrafe (Anteil der
		// entgangenen Praemie), damit Zeitdruck etwas kostet. Der Auftrag verfaellt;
		// AddGuthaben klemmt ueber ApplyDelta bei 0 -> nie ins Minus.
		const FString FailedTitle = ActiveMission.Title;
		const int32 Penalty =
			FWiesbadenStore::ComputeFailurePenalty(ActiveMission.Reward.Guthaben);
		EndActiveMission();
		if (Penalty > 0)
		{
			if (UWiesbadenGameStateSubsystem* GameState = GetGameStateSubsystem())
			{
				GameState->AddGuthaben(-Penalty);
			}
		}
		UE_LOG(LogWbCore, Warning,
			TEXT("Auftrag GESCHEITERT (Zeitlimit ueberschritten): %s. Vertragsstrafe: %d Guthaben. Naechster Auftrag folgt."),
			*FailedTitle, Penalty);
		OnObjectiveChanged.Broadcast(); // HUD-Panel leeren
		return;
	}

	if (Result.bMissionCompleted)
	{
		const FMission Completed = ActiveMission;
		UE_LOG(LogWbCore, Log, TEXT("Mission erfuellt: %s."), *Completed.Title);

		CreditMissionReward(Result.GuthabenAwarded);

		EndActiveMission();
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
