// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenStreamingSource.h"

#include "WiesbadenReal.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

AWiesbadenStreamingSource::AWiesbadenStreamingSource()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AWiesbadenStreamingSource::BeginPlay()
{
	Super::BeginPlay();

	// PERF-DIAGNOSE-SCHALTER: Radius per Kommandozeile erzwingen (ueberschreibt den in
	// der Karte serialisierten Wert). Damit wurde bewiesen, dass die ~6-10 FPS
	// STREAMING-HITCHES sind, nicht residente Foliage: 800 m -> 76 FPS, 6000 m ->
	// 6 FPS, bei fast gleicher Instanzenzahl (~560k vs ~578k). Zum Nachmessen/Tunen
	// des Boden-Radius: `-WbRadius=800`.
	float ForcedRadius = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WbRadius="), ForcedRadius) && ForcedRadius > 0.0f)
	{
		StreamingRadiusMeters = ForcedRadius;
		UE_LOG(LogWbCore, Log, TEXT("WbRadius: Streaming-Radius auf %.0f m erzwungen."), ForcedRadius);
	}

	WorldPartitionSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
	if (WorldPartitionSubsystem)
	{
		WorldPartitionSubsystem->RegisterStreamingSourceProvider(this);
		UE_LOG(LogWbCore, Log, TEXT("World-Partition-Streaming-Quelle %s registriert."), *GetName());
	}
	else
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Kein UWorldPartitionSubsystem verfuegbar - Streaming-Quelle bleibt inaktiv (Welt nicht partitioniert?)."));
	}
}

void AWiesbadenStreamingSource::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (WorldPartitionSubsystem)
	{
		WorldPartitionSubsystem->UnregisterStreamingSourceProvider(this);
		WorldPartitionSubsystem = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AWiesbadenStreamingSource::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateSource();
}

void AWiesbadenStreamingSource::UpdateSource()
{
	FVector SourceLocation = GetActorLocation();
	FRotator SourceRotation = GetActorRotation();
	bool bValid = true;

	if (bFollowPlayerPawn)
	{
		const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn)
		{
			SourceLocation = Pawn->GetActorLocation();
			SourceRotation = Pawn->GetActorRotation();
		}
		else
		{
			// Noch kein Pawn (z. B. waehrend des Ladens): Quelle deaktiviert.
			bValid = false;
		}
	}

	if (!bValid)
	{
		bHasValidSource = false;
		return;
	}

	CurrentSource.Name = TEXT("WiesbadenPlayerStreaming");
	CurrentSource.Location = SourceLocation;
	CurrentSource.Rotation = SourceRotation;
	CurrentSource.TargetState = EStreamingSourceTargetState::Activated;
	CurrentSource.bBlockOnSlowLoading = false;
	CurrentSource.Priority = EStreamingSourcePriority::High;
	CurrentSource.Velocity = FVector::ZeroVector;
	CurrentSource.bUseVelocityContributionToCellsSorting = false;
	CurrentSource.DebugColor = FColor::Cyan;

	// Form: Kugel um den Spieler mit konfiguriertem Radius (cm). Die Quelle
	// haengt NICHT am Grid-Loading-Range, damit der Radius unabhaengig vom
	// Level-Design eingestellt werden kann (StreamingRadiusMeters).
	FStreamingSourceShape Shape;
	Shape.bUseGridLoadingRange = false;
	Shape.Radius = FMath::Max(100.0f, StreamingRadiusMeters * 100.0f);
	Shape.bIsSector = false;
	Shape.Location = FVector::ZeroVector;
	Shape.Rotation = FRotator::ZeroRotator;

	CurrentSource.Shapes.Reset();
	CurrentSource.Shapes.Add(Shape);

	bHasValidSource = true;
}

bool AWiesbadenStreamingSource::GetStreamingSource(FWorldPartitionStreamingSource& OutStreamingSource) const
{
	if (!bHasValidSource)
	{
		return false;
	}
	OutStreamingSource = CurrentSource;
	return true;
}
