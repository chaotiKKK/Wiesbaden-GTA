// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Vehicles/WiesbadenEngineAudio.h"

#include "WiesbadenCarAudioComponent.generated.h"

class UAudioComponent;
class USoundBase;
class USoundWaveProcedural;

/**
 * Motorklang des Fahrzeugs.
 *
 * Erzeugt den Klang zur Laufzeit aus Drehzahl und Last
 * (FWiesbadenEngineAudioModel) und schiebt ihn in eine USoundWaveProcedural.
 * Damit klingt das Fahrzeug ohne jedes Audio-Asset - das ist hier keine
 * Notloesung, sondern die Voraussetzung dafuer, dass der Klang stufenlos der
 * Drehzahl folgt. Ein geloopter Sample-Satz braucht Dutzende Aufnahmen je
 * Lastzustand und klingt an den Uebergaengen trotzdem gestuft.
 *
 * Liegt ein EngineSound-Asset vor, wird stattdessen dieses abgespielt und in
 * der Tonhoehe an die Drehzahl angepasst.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenCarAudioComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWiesbadenCarAudioComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Betriebsdaten setzen; wird vom Fahrzeug je Tick aufgerufen. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug|Audio")
	void SetEngineState(float EngineRpm, float Throttle, float SpeedKmh);

	/** Motor an/aus. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug|Audio")
	void SetEngineRunning(bool bRunning);

	/** Hupe an oder aus (Zweiklang 410/490 Hz im Motorsynthesizer). */
	void SetHorn(bool bPressed);

	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Fahrzeug|Audio")
	bool IsEngineRunning() const { return AudioParams.bEngineRunning; }

	/** Optionales Klang-Asset. Ohne Zuweisung wird synthetisiert. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Audio")
	USoundBase* EngineSound = nullptr;

	/** Gesamtlautstaerke. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterGain = 0.7f;

	/** Leerlaufdrehzahl des Kaefer-Boxers. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Audio", meta = (ClampMin = "300.0"))
	float IdleRpm = 850.0f;

	/** Zylinderzahl - beim Kaefer vier. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Audio", meta = (ClampMin = "1", ClampMax = "16"))
	int32 CylinderCount = 4;

	/**
	 * Vorhaltezeit des Audiopuffers in Sekunden. Zu klein fuehrt zu
	 * Aussetzern bei Frame-Einbruechen, zu gross verzoegert die Reaktion auf
	 * Gasstoesse hoerbar.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Audio", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float BufferSeconds = 0.25f;

private:
	void CreateAudioSource();
	void PushProceduralAudio();

	UPROPERTY(Transient)
	UAudioComponent* EngineAudio = nullptr;

	UPROPERTY(Transient)
	USoundWaveProcedural* ProceduralWave = nullptr;

	FWiesbadenEngineAudioParams AudioParams;
	FWiesbadenEngineAudioState AudioState;

	/** Wiederverwendeter Sample-Puffer - keine Allokation je Tick. */
	TArray<int16> SampleBuffer;

	/** True, wenn synthetisiert wird (kein Asset zugewiesen). */
	bool bProcedural = false;
};
