// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Audio/WiesbadenAudioPropagation.h"
#include "WiesbadenAmbienceSubsystem.generated.h"

class AActor;
class UAudioComponent;

/**
 * Ambience-Layer und Raumklang.
 *
 * - Diffuse Betten (Wind, Stadtsummen, Innen-Roomtone) und raeumliche
 *   Quellen (Voegel bei Tag, Nachtambiente) auf SC_Ambience; die
 *   MetaSound-Betten aus /Game/Audio/Meta fehlen sie, bleibt es still.
 * - Tag/Nacht-Mischung ueber die Spieluhr, weich ueberblendet.
 * - Raumsonde (Strahlen um den Hoerer) -> Hall-Preset + Sendepegel ueber
 *   WiesbadenAudioPropagation::ApplySpaceState.
 * - Spawner: die raeumlichen Quellen liegen auf Bodenpunkten im Ring um
 *   den Hoerer und wandern mit; die Anbindung an Region-/POI-Punkte
 *   (Strassenzug, Park) ist die offene Naht dafuer.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenAmbienceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void EnsureRig();
	void ProbeSpaceAndTime(float DeltaSeconds);
	void UpdateBeds(float TimeOfDayHours, EWbReverbSpace Space);
	void ReseedLocalEmitters(const FVector& ListenerLoc);
	float ResolveTimeOfDayHours() const;

	/** Erzeugt eine Bett-Quelle am Rig; nullptr, wenn das Bett fehlt. */
	UAudioComponent* MakeBed(AActor* Rig, const TCHAR* BedName, bool bSpatialized, EWbAudioRange Range);

	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> DiffuseBeds;
	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> LocalBirdBeds;
	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> LocalNightBeds;
	UPROPERTY(Transient) TObjectPtr<AActor> RigActor = nullptr;

	float ProbeAccumulator = 0.0f;
	float LastAppliedHour = -1.0f;
	FVector LastSeedLocation = FVector::ZeroVector;
	bool bWarnedMissingBeds = false;
};
