// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Audio/WiesbadenAudioPropagation.h"
#include "Audio/WiesbadenAudioZones.h"
#include "WiesbadenAmbienceSubsystem.generated.h"

class AActor;
class UAudioComponent;

/**
 * Ambience-Layer und Raumklang.
 *
 * - Die Klanglagen sind ECHTE Field-Recordings (/Game/Audio/Samples/A_Amb*,
 *   Import: Tools/fetch_ambience_samples.py + Tools/import_audio_samples.py):
 *   diffus Wind/Verkehr/Industrie + Innen-Roomtone, lokal Voegel, Nacht,
 *   Menschenmenge und Strassenleben. Fehlt eine Sample-Lage, faellt die
 *   Bett-Erzeugung auf das synthetische MetaSound-Bett zurueck (/Game/Audio/
 *   Meta); fehlt beides, bleibt die Lage still (kein Fehler).
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

	/**
	 * Erzeugt eine Bett-Quelle am Rig; nullptr, wenn keine Klanglage existiert.
	 * Zuerst wird das echte Sample (/Game/Audio/Samples/<SampleName>) geladen,
	 * dann das synthetische MetaSound-Bett (<BedName>) als Rueckfall.
	 */
	UAudioComponent* MakeBed(AActor* Rig, const TCHAR* BedName, const TCHAR* SampleName,
		bool bSpatialized, EWbAudioRange Range);

	/** Diffuse Lagen: [0] Wind, [1] Verkehr, [2] Innen-Roomtone, [3] Industrie. */
	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> DiffuseBeds;
	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> LocalBirdBeds;
	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> LocalNightBeds;
	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> LocalCrowdBeds;
	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> LocalChildBeds;
	UPROPERTY(Transient) TObjectPtr<AActor> RigActor = nullptr;

	FWbAmbienceMix CurrentZoneMix;
	FWbAmbienceMix TargetZoneMix;
	float ProbeAccumulator = 0.0f;
	EWbReverbSpace CurrentSpace = EWbReverbSpace::Outdoor;
	FVector LastSeedLocation = FVector::ZeroVector;
	bool bWarnedMissingBeds = false;
};
