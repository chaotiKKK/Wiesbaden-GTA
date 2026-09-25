// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "WiesbadenTireEffectsComponent.generated.h"

class UAudioComponent;
class USoundWaveProcedural;
class UMaterialInterface;

/**
 * Macht den Reifenschlupf hoer- und sichtbar - OHNE die Fahrphysik zu aendern.
 *
 * Das Fahrzeug reicht je Tick den Schlupf-Zustand herein (Radspin/Blockieren aus
 * FWiesbadenVehiclePhysicsOutput, Schwimmwinkel, Tempo, die Bodenaufstandspunkte
 * der rutschenden Raeder). Diese Komponente KONSUMIERT das nur:
 *  - QUIETSCHEN: prozedural synthetisiert (wie der Motorklang - kein Audio-Asset)
 *    in eine USoundWaveProcedural, geroutet ueber den SFX-Bus des Mischpults.
 *    Lautstaerke folgt der Schlupf-Intensitaet, im Stand/ohne Schlupf still.
 *  - SPUREN: kurze Boden-Decals (M_WbTireMark) entlang der Spur, distanzgetaktet.
 *
 * Eigene Komponente statt Code im Fahrzeug-Tick: dieselbe Trennung wie
 * Motorklang/Licht/Kamera - Reifen-Effekte haben EINEN Ort.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenTireEffectsComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWiesbadenTireEffectsComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Schlupf-Zustand eines Ticks setzen (vom Fahrzeug aufgerufen).
	 * @param GroundContactsToMark Bodenpunkte der rutschenden Raeder (leer = keine Spur).
	 * @param TravelDir Fahrtrichtung (Weltraum, normiert) - Ausrichtung der Spur.
	 */
	void UpdateTireEffects(bool bWheelSpin, bool bWheelLock, float SlipAngleDeg, float SpeedKmh,
		const TArray<FVector>& GroundContactsToMark, const FVector& TravelDir);

	/**
	 * Quietsch-Intensitaet 0..1 aus dem Schlupf-Zustand. Datenrein und statisch,
	 * damit die Kennlinie ohne Welt pruefbar ist (Test Vehicles.TireEffects):
	 * Radspin quietscht auch bei geringem Tempo (das Rad dreht schneller als der
	 * Boden), Blockieren/Drift erst mit Fahrt (ein stehendes Rad rutscht nicht).
	 */
	static float ComputeSquealIntensity(bool bWheelSpin, bool bWheelLock, float SlipAngleDeg, float SpeedKmh);

	/** Gesamtlautstaerke des Quietschens. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Reifen", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterGain = 0.55f;

	/** Abstand zwischen zwei Spur-Decals (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Reifen", meta = (ClampMin = "5.0"))
	float MarkIntervalCm = 35.0f;

	/** Lebensdauer eines Spur-Decals (s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Reifen", meta = (ClampMin = "1.0"))
	float MarkLifeSeconds = 30.0f;

private:
	void CreateAudioSource();
	void PushProceduralAudio();
	void SpawnMarks(const TArray<FVector>& Contacts, const FVector& TravelDir);

	UPROPERTY(Transient)
	UAudioComponent* SquealAudio = nullptr;

	UPROPERTY(Transient)
	USoundWaveProcedural* SquealWave = nullptr;

	/** True, wenn die echte Aufnahme laeuft (dann steuert die Intensitaet
	 *  nur die Lautstaerke, kein Synth-Nachschub). */
	bool bSampleSqueal = false;

	UPROPERTY(Transient)
	UMaterialInterface* SkidDecalMaterial = nullptr;

	/** Wiederverwendeter Sample-Puffer. */
	TArray<int16> SampleBuffer;

	/** Ziel- und geglaettete Quietsch-Intensitaet (0..1). */
	float TargetIntensity = 0.0f;
	float CurrentIntensity = 0.0f;

	/** Phasen der beiden Screech-Partiale + Vibrato-Phase. */
	double Phase1 = 0.0;
	double Phase2 = 0.0;
	double VibratoPhase = 0.0;

	/** Rising-Edge-Merker fuer die Quietsch-Ereigniszeile (Nachweis). */
	bool bSquealAudible = false;

	/** Distanz-Taktung der Spur-Decals. */
	FVector LastOwnerPos = FVector::ZeroVector;
	bool bHaveLastOwnerPos = false;
	float AccumMarkCm = 0.0f;
};
