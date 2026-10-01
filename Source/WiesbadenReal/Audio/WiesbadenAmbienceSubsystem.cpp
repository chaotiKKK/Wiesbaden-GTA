// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WiesbadenAmbienceSubsystem.h"

#include "Audio/WiesbadenAudioSubsystem.h"
#include "Audio/WiesbadenAudioZonesSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "World/WiesbadenCitySubsystem.h"
#include "World/WiesbadenSolar.h"
#include "World/WiesbadenWeatherSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbAmbience, Log, All);

namespace
{
	/** Abstand der Raumsonde (Sekunden). */
	constexpr float ProbeIntervalSeconds = 0.5f;

	/** Ring-Radius der raeumlichen Betten um den Hoerer (cm). */
	constexpr float LocalBedRadiusCm = 1800.0f;

	/** Neusetzen der Betten ab dieser Hoerer-Bewegung (cm). */
	constexpr float ReseedDistanceCm = 3500.0f;

	/** Ziele-Hoehe der raeumlichen Betten ueber dem Boden (cm). */
	constexpr float LocalBedHeightCm = 60.0f;

	FVector ListenerLocation(const UWorld* World)
	{
		if (const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
		{
			if (PC->PlayerCameraManager)
			{
				return PC->PlayerCameraManager->GetCameraLocation();
			}
		}
		return FVector::ZeroVector;
	}

	void DestroyBeds(TArray<TObjectPtr<UAudioComponent>>& Beds)
	{
		for (UAudioComponent* Bed : Beds)
		{
			if (Bed)
			{
				WiesbadenAudioPropagation::UnregisterSource(Bed);
				Bed->DestroyComponent();
			}
		}
		Beds.Reset();
	}
}

void UWiesbadenAmbienceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogWbAmbience, Log, TEXT("Ambience-Subsystem bereit."));
}

void UWiesbadenAmbienceSubsystem::Deinitialize()
{
	DestroyBeds(DiffuseBeds);
	DestroyBeds(LocalBirdBeds);
	DestroyBeds(LocalNightBeds);
	if (RigActor)
	{
		RigActor->Destroy();
		RigActor = nullptr;
	}
	Super::Deinitialize();
}

TStatId UWiesbadenAmbienceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWiesbadenAmbienceSubsystem, STATGROUP_Tickables);
}

void UWiesbadenAmbienceSubsystem::Tick(float DeltaTime)
{
	ProbeSpaceAndTime(DeltaTime);
}

float UWiesbadenAmbienceSubsystem::ResolveTimeOfDayHours() const
{
	// Die Spieluhr ist die EINE Zeitquelle (-WbTime vor Prompt vor Systemzeit);
	// hier wird nur abgelesen, nie neu gerechnet.
	UWorld* World = GetWorld();
	if (UWiesbadenCitySubsystem* City = World ? World->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr)
	{
		return City->Weather.GetState().TimeOfDayHours;
	}
	return WiesbadenSolar::LocalHours(FDateTime::Now());
}

UAudioComponent* UWiesbadenAmbienceSubsystem::MakeBed(
	AActor* Rig, const TCHAR* BedName, bool bSpatialized, EWbAudioRange Range)
{
	if (!Rig)
	{
		return nullptr;
	}

	USoundBase* Bed = LoadObject<USoundBase>(
		nullptr, *WiesbadenAudioPropagation::AmbienceBedPath(FName(BedName)));
	if (!Bed)
	{
		// MetaSound-Bett fehlt noch (Tools/make_audio_assets.cmd) - still, kein Fehler.
		return nullptr;
	}

	UAudioComponent* Component = NewObject<UAudioComponent>(
		Rig, FName(*(FString::Printf(TEXT("Amb%s"), BedName))));
	Component->SetAbsolute(true, true, true);
	Component->bAutoActivate = false;
	Component->SoundClassOverride = UWiesbadenAudioSubsystem::LoadBusSoundClass(EWbAudioBus::Ambience);
	Component->RegisterComponent();
	WiesbadenAudioPropagation::ConfigureSource(Component, Range, bSpatialized);
	Component->SetSound(Bed);
	Component->Play();
	return Component;
}

void UWiesbadenAmbienceSubsystem::EnsureRig()
{
	UWorld* World = GetWorld();
	if (!World || RigActor)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	RigActor = World->SpawnActor<AActor>(SpawnParams);
	if (!RigActor)
	{
		return;
	}

	// Feste Schleussen: [0] Wind, [1] Stadtsummen, [2] Innen-Roomtone.
	// Ein fehlendes Bett bleibt leer (nullptr) - die Reihenfolge bleibt stabil.
	DiffuseBeds.SetNum(3);
	DiffuseBeds[0] = MakeBed(RigActor, TEXT("Wind"), false, EWbAudioRange::Far);
	DiffuseBeds[1] = MakeBed(RigActor, TEXT("City"), false, EWbAudioRange::Far);
	DiffuseBeds[2] = MakeBed(RigActor, TEXT("Room"), false, EWbAudioRange::Far);

	if (DiffuseBeds[0] == nullptr && !bWarnedMissingBeds)
	{
		bWarnedMissingBeds = true;
		UE_LOG(LogWbAmbience, Warning,
			TEXT("Ambience-Betten fehlen unter /Game/Audio/Meta - erst Tools/make_audio_assets.cmd ausfuehren."));
	}

	// Sofort neu setzen, damit die lokalen Betten gleich danach entstehen.
	LastSeedLocation = FVector::ZeroVector;
}

void UWiesbadenAmbienceSubsystem::ReseedLocalEmitters(const FVector& ListenerLoc)
{
	DestroyBeds(LocalBirdBeds);
	DestroyBeds(LocalNightBeds);
	UWorld* World = GetWorld();
	if (!World || !RigActor)
	{
		return;
	}

	// Drei Bodenpunkte im Ring um den Hoerer (Spawner-Prinzip; die Anbindung
	// an echte Region-/POI-Punkte bleibt die offene Naht dafuer).
	LocalBirdBeds.SetNum(3);
	LocalNightBeds.SetNum(3);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float AngleRad = FMath::DegreesToRadians(30.0f + 120.0f * Index);
		const FVector Ring = ListenerLoc + FVector(
			FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.0f) * LocalBedRadiusCm;

		FHitResult Hit;
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(WbAmbienceGround), false);
		FVector Place = Ring;
		if (World->LineTraceSingleByChannel(
			Hit, Ring + FVector(0, 0, 400), Ring - FVector(0, 0, 800), ECC_Visibility, TraceParams))
		{
			Place = Hit.ImpactPoint + FVector(0, 0, LocalBedHeightCm);
		}

		if (UAudioComponent* Birds = MakeBed(RigActor, TEXT("Birds"), true, EWbAudioRange::Mid))
		{
			Birds->SetWorldLocation(Place);
			LocalBirdBeds[Index] = Birds;
		}
		if (UAudioComponent* Night = MakeBed(RigActor, TEXT("Night"), true, EWbAudioRange::Mid))
		{
			Night->SetWorldLocation(Place);
			LocalNightBeds[Index] = Night;
		}
	}
	LastSeedLocation = ListenerLoc;
}

void UWiesbadenAmbienceSubsystem::ProbeSpaceAndTime(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	EnsureRig();
	if (!RigActor)
	{
		return;
	}

	const FVector Listener = ListenerLocation(World);
	if (LastSeedLocation.IsZero() ||
		FVector::DistSquared(LastSeedLocation, Listener) > FMath::Square(ReseedDistanceCm))
	{
		ReseedLocalEmitters(Listener);
	}

	CurrentZoneMix.Wind = FMath::FInterpTo(CurrentZoneMix.Wind, TargetZoneMix.Wind, DeltaSeconds, 1.5f);
	CurrentZoneMix.City = FMath::FInterpTo(CurrentZoneMix.City, TargetZoneMix.City, DeltaSeconds, 1.5f);
	CurrentZoneMix.Birds = FMath::FInterpTo(CurrentZoneMix.Birds, TargetZoneMix.Birds, DeltaSeconds, 1.5f);
	CurrentZoneMix.Night = FMath::FInterpTo(CurrentZoneMix.Night, TargetZoneMix.Night, DeltaSeconds, 1.5f);
	// Nur Klassifikation/Strahlen im Halbsekundentakt, Pegel jeden Frame:
	// andernfalls war die weiche Interpolation eine hoerbare Treppe.
	UpdateBeds(ResolveTimeOfDayHours(), CurrentSpace);

	ProbeAccumulator += DeltaSeconds;
	if (ProbeAccumulator < ProbeIntervalSeconds)
	{
		return;
	}
	ProbeAccumulator = 0.0f;
	if (UWiesbadenAudioZonesSubsystem* Zones = World->GetSubsystem<UWiesbadenAudioZonesSubsystem>())
	{
		TargetZoneMix = WiesbadenAudioZones::AmbienceMix(Zones->ZoneAt(Listener));
	}

	// Raumsonde: 5 Aufwaerts- und 8 Horizontalstrahlen. Der Hoerer steckt
	// haeufig im Fahrzeug - der eigene Rumpf wird ignoriert.
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(WbRoomSense), false);
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			TraceParams.AddIgnoredActor(Pawn);
			if (Pawn->GetOwner())
			{
				TraceParams.AddIgnoredActor(Pawn->GetOwner());
			}
		}
	}

	int32 SkyHits = 0;
	float CeilingSum = 0.0f;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const FVector Offset = (Index == 0) ? FVector::ZeroVector
			: FVector(FMath::Cos(Index * PI / 2), FMath::Sin(Index * PI / 2), 0.0f) * 120.0f;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(
			Hit, Listener + Offset, Listener + Offset + FVector(0, 0, 800), ECC_Visibility, TraceParams))
		{
			++SkyHits;
			CeilingSum += Hit.ImpactPoint.Z - Listener.Z;
		}
	}
	const float SkyBlocked01 = SkyHits / 5.0f;
	const float CeilingHeightM = (SkyHits > 0) ? CeilingSum / SkyHits / 100.0f : 100.0f;

	int32 WallHits = 0;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const FVector Dir = FVector(FMath::Cos(Index * PI / 4), FMath::Sin(Index * PI / 4), 0.0f);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(
			Hit, Listener + FVector(0, 0, 150),
			Listener + FVector(0, 0, 150) + Dir * 300.0f, ECC_Visibility, TraceParams))
		{
			++WallHits;
		}
	}

	CurrentSpace = WiesbadenAudioPropagation::ClassifySpace(SkyBlocked01, WallHits / 8.0f, CeilingHeightM);
	WiesbadenAudioPropagation::ApplySpaceState(CurrentSpace);
}

void UWiesbadenAmbienceSubsystem::UpdateBeds(float TimeOfDayHours, EWbReverbSpace Space)
{
	const float Day = WiesbadenAudioPropagation::DayBedGain(TimeOfDayHours);
	const float Night = WiesbadenAudioPropagation::NightBedGain(TimeOfDayHours);
	const float Indoor = (Space == EWbReverbSpace::Indoor || Space == EWbReverbSpace::Tunnel)
		? 1.0f : (Space == EWbReverbSpace::Hall ? 0.5f : 0.0f);

	if (DiffuseBeds.Num() >= 3)
	{
		if (UAudioComponent* Wind = DiffuseBeds[0])
		{
			Wind->SetVolumeMultiplier(CurrentZoneMix.Wind * (1.0f - 0.65f * Indoor));
		}
		if (UAudioComponent* City = DiffuseBeds[1])
		{
			City->SetVolumeMultiplier(CurrentZoneMix.City * (1.0f - 0.7f * Indoor));
		}
		if (UAudioComponent* Room = DiffuseBeds[2])
		{
			Room->SetVolumeMultiplier(0.1f + 0.7f * Indoor);
		}
	}
	for (const TObjectPtr<UAudioComponent>& Bed : LocalBirdBeds)
	{
		if (Bed)
		{
			Bed->SetVolumeMultiplier(CurrentZoneMix.Birds * Day * (1.0f - 0.8f * Indoor));
		}
	}
	for (const TObjectPtr<UAudioComponent>& Bed : LocalNightBeds)
	{
		if (Bed)
		{
			Bed->SetVolumeMultiplier(CurrentZoneMix.Night * Night * (1.0f - 0.8f * Indoor));
		}
	}
}
