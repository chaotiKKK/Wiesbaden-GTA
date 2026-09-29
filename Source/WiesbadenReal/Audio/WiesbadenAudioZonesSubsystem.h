// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Audio/WiesbadenAudioZones.h"
#include "WiesbadenAudioZonesSubsystem.generated.h"

class UAudioComponent;
class USoundBase;

/**
 * Ort -> Klang. Die einzige Stelle, die weiss, welcher Ton an welcher
 * Position erklingt.
 *
 * Drei Konsumenten, alle fragen nur hier: der Fuss-Pawn (Task 4), der
 * Passanten-Pool (Task 5) und spaeter UWiesbadenAmbienceSubsystem (Teil 3a).
 *
 * Fehlende Assets bleiben still: loggt einmal und rechnet weiter. Genau das
 * haelt die bestehenden Audio-Subsysteme robust.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenAudioZonesSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	/** Legt den Schritt-Pool an. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void EnsureRig();

	/**
	 * Untergrund unter einer Position: Strahl nach unten, Materialname des
	 * Treffers. Kein Treffer -> Pflaster, kein Fehler.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Audio")
	EWbFootstepSurface SurfaceUnderFoot(const FVector& Location);

	/**
	 * Einen Fussschritt an der Position ablegen.
	 *
	 * Der Pool ist fest gross und recycelt seine Plaetze - ein Schritt bricht
	 * den laufenden desselben Platzes ab, statt zu stapeln. Deshalb ist die
	 * Zahl der gleichzeitig hoerbaren Schritte durch die Poolgroesse begrenzt
	 * und nicht durch einen Zaehler.
	 *
	 * @return false, wenn kein Klang geladen ist oder der Schalter aus ist.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	bool PlayFootstepAt(const FVector& Location, EWbFootstepSurface Surface);

	/** Schalter fuer den A/B-Vergleich im selben Build. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void SetFootstepsEnabled(bool bEnabled)
	{
		bFootstepsEnabled = bEnabled;
	}

	/** Zahl der abgelegten Schritte seit BeginPlay - der Laufbeleg. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Audio")
	int32 GetPlayedFootstepCount() const { return PlayedFootsteps; }

	/** Anzahl der Klang-Plaetze im Pool. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Audio")
	int32 GetActiveStepCount() const { return StepPool.Num(); }

private:
	/** Poolplatz, der als naechster recycelt wird. */
	UPROPERTY()
	int32 NextStepSlot = 0;

	/** Fester Stimmen-Pool; die Groesse entspricht den geladenen Schrittklangen. */
	UPROPERTY()
	TArray<TObjectPtr<UAudioComponent>> StepPool;

	/** Geladene Sounds und Flaechen, im selben Index angeordnet. */
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> StepSounds;

	TArray<EWbFootstepSurface> StepSoundSurfaces;

	UPROPERTY()
	int32 PlayedFootsteps = 0;

	bool bRigReady = false;
	bool bFootstepsEnabled = true;
	bool bWarnedMissingBeds = false;
};
