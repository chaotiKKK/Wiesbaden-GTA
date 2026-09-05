// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenStreamingSource.h"

#include "WiesbadenReal.h"

#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/SoftObjectPath.h"
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
		// Diagnose: fester Radius -> Adaptivitaet aus, damit die A/B-Messung den
		// erzwungenen Wert misst und nicht die Hoehenkurve.
		bForceFixedRadius = true;
		UE_LOG(LogWbCore, Log, TEXT("WbRadius: Streaming-Radius fest auf %.0f m erzwungen (adaptiv aus)."), ForcedRadius);
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
	UpdateSource(DeltaSeconds);
}

void AWiesbadenStreamingSource::UpdateSource(float DeltaSeconds)
{
	FVector SourceLocation = GetActorLocation();
	FRotator SourceRotation = GetActorRotation();
	bool bValid = true;
	const APawn* FollowPawn = nullptr;

	if (bFollowPlayerPawn)
	{
		const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn)
		{
			SourceLocation = Pawn->GetActorLocation();
			SourceRotation = Pawn->GetActorRotation();
			FollowPawn = Pawn;
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

	// Hoehenadaptiver Radius: am Boden eng (schnell), in der Luft weit (Sicht).
	// Hoehe ueber Grund per Abwaerts-Trace vom Pawn; fuer Streaming ist etwas
	// Rauschen unkritisch, zusaetzlich zeitkonstant geglaettet. -WbRadius laesst
	// den Wert fest (Diagnose).
	float EffectiveRadiusMeters = StreamingRadiusMeters;
	if (!bForceFixedRadius)
	{
		float MeasuredAltMeters = AdaptiveFullAltitudeMeters; // Trace-Fehlschlag -> weit
		if (UWorld* World = GetWorld())
		{
			FHitResult Hit;
			const FVector Start = SourceLocation;
			const FVector End = Start - FVector(0.0f, 0.0f, 2000000.0f); // 20 km abwaerts
			FCollisionQueryParams Params(FName(TEXT("StreamingAltitude")), /*bTraceComplex=*/false);
			if (FollowPawn)
			{
				Params.AddIgnoredActor(FollowPawn);
			}
			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
			{
				MeasuredAltMeters = FMath::Max(0.0f, (Start.Z - Hit.ImpactPoint.Z)) * 0.01f;
			}
		}
		// Zeitkonstante Glaettung (~0.8 s), framerate-unabhaengig - der Radius
		// soll beim Ueberfliegen von Daechern/Luecken nicht springen.
		const float Tau = 0.8f;
		const float SmoothAlpha = (DeltaSeconds > 0.0f) ? (1.0f - FMath::Exp(-DeltaSeconds / Tau)) : 1.0f;
		SmoothedAltitudeMeters = FMath::Lerp(SmoothedAltitudeMeters, MeasuredAltMeters, SmoothAlpha);
		EffectiveRadiusMeters = ComputeAdaptiveRadiusMeters(SmoothedAltitudeMeters,
			GroundRadiusMeters, StreamingRadiusMeters, AdaptiveStartAltitudeMeters, AdaptiveFullAltitudeMeters);
	}

	// Aktuellen Radius ans Strassen-Material geben (MPC_WbStreaming.FadeRadiusM).
	//
	// Das entfernungsbasierte Einblenden im Strassen-Material braucht den
	// GELEBTEN Radius: am Boden 900 m, im Flug bis 6000 m. Ein festes Band wuerde
	// im Flug ferne Strassen ausblenden. Die MPC wird bis zum Erfolg NACHGELADEN
	// (nicht nach einem einzigen Fehlversuch aufgeben, sonst haengt das Material
	// dauerhaft auf dem Default 900 m); der Wert wird je Bild geschrieben.
	bool bFadeMpcSet = false;
	if (!FadeMpc)
	{
		FadeMpc = Cast<UMaterialParameterCollection>(FSoftObjectPath(
			TEXT("/Game/Materials/AAA/MPC_WbStreaming.MPC_WbStreaming")).TryLoad());
	}
	if (FadeMpc)
	{
		if (UMaterialParameterCollectionInstance* Inst =
			GetWorld() ? GetWorld()->GetParameterCollectionInstance(FadeMpc) : nullptr)
		{
			bFadeMpcSet = Inst->SetScalarParameterValue(
				FName(TEXT("FadeRadiusM")), EffectiveRadiusMeters);
		}
	}

	// Diagnose-Log im Sekundentakt: laesst die Hoehe->Radius-Kurve im Flug
	// pruefen - UND ob der Radius wirklich in die Fade-MPC geschrieben wurde
	// (sonst haengt das Strassen-Einblenden still auf dem Default 900 m).
	if (const UWorld* W = GetWorld())
	{
		const int32 Sec = FMath::FloorToInt(W->GetTimeSeconds());
		if (Sec != LastRadiusLogSecond)
		{
			LastRadiusLogSecond = Sec;
			UE_LOG(LogWbCore, Log,
				TEXT("WbStreaming: Hoehe %.0f m -> Radius %.0f m%s. Fade-MPC: %s"),
				SmoothedAltitudeMeters, EffectiveRadiusMeters,
				bForceFixedRadius ? TEXT(" (fest, -WbRadius)") : TEXT(""),
				bFadeMpcSet ? TEXT("FadeRadiusM gesetzt")
				            : (FadeMpc ? TEXT("Instanz fehlt!") : TEXT("MPC nicht geladen!")));
		}
	}

	// Form: Kugel um den Spieler mit adaptivem Radius (cm). Die Quelle haengt
	// NICHT am Grid-Loading-Range, damit der Radius unabhaengig vom Level-Design
	// eingestellt werden kann.
	FStreamingSourceShape Shape;
	Shape.bUseGridLoadingRange = false;
	Shape.Radius = FMath::Max(100.0f, EffectiveRadiusMeters * 100.0f);
	Shape.bIsSector = false;
	Shape.Location = FVector::ZeroVector;
	Shape.Rotation = FRotator::ZeroRotator;

	CurrentSource.Shapes.Reset();
	CurrentSource.Shapes.Add(Shape);

	bHasValidSource = true;
}

float AWiesbadenStreamingSource::ComputeAdaptiveRadiusMeters(float AltitudeMeters, float GroundRadiusM,
	float AirRadiusM, float StartAltM, float FullAltM)
{
	// Degenerierte Baender (Full <= Start): harte Stufe statt Division durch <=0.
	if (FullAltM <= StartAltM)
	{
		return (AltitudeMeters >= FullAltM) ? AirRadiusM : GroundRadiusM;
	}
	const float T = FMath::Clamp((AltitudeMeters - StartAltM) / (FullAltM - StartAltM), 0.0f, 1.0f);
	const float S = T * T * (3.0f - 2.0f * T); // smoothstep - weiche Blende ohne Knick
	return FMath::Lerp(GroundRadiusM, AirRadiusM, S);
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
