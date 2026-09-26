// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "Vehicles/WiesbadenHelicopterAudio.h"

#include "WiesbadenHelicopterAudioComponent.generated.h"

class UAudioComponent;
class USoundWave;
class USoundWaveProcedural;

/**
 * Flugsound fuer den Helikopter.
 *
 * Zwei Betriebsarten (fuer Rotor- und Triebwerk je eine):
 *  - Asset-basiert: RotorSound/EngineSound/WindSound (USoundWave) werden mit
 *    Pitch/Volume aus Drehzahl, Blattlast und Geschwindigkeit abgespielt. Die
 *    Pitch-Teiler sind BEZUGSDREHZAHLEN, keine Einheiten: die Ka-52-Loops
 *    (Tools/make_ka52_audio.py) sind auf 300 rpm bzw. 600 rpm gebaut, damit
 *    der Ton bei Reiseflug unveraendert bleibt und nur beim Hoch- und
 *    Herunterlaufen einzieht. Mit den aelteren Teilern 560/3800 lag der Ton
 *    bei Reiseflug eine Oktave zu tief.
 *  - Prozedural (bUseProceduralFallback, ohne zugewiesene Assets):
 *    FWiesbadenHelicopterAudioModel erzeugt einen Rotor-/"Wop-Wop"- und
 *    Motor-Klang als int16-PCM und pusht ihn in einen USoundWaveProcedural -
 *    das ist der Rueckfall, kein Ziel: eine Saegezahn-Approximation ohne
 *    Transienten hoert man sofort als Rechner.
 *
 * Die Werte werden vom Heli-Pawn pro Tick per Setter uebergeben
 * (SetRotorState/SetEngineState/SetForwardSpeed) - die Komponente bleibt
 * dadurch generisch und die Parameter-Schnittstelle testbar.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenHelicopterAudioComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWiesbadenHelicopterAudioComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Rotor-Zustand (Drehzahl + Blattlast) aus der Rotor-Physik. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void SetRotorState(float MainRotorRpm, float Collective);

	/** Triebwerkszustand (Drehzahl + an/aus) aus der Rotor-Physik. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void SetEngineState(float EngineRpm, bool bEngineRunning);

	/** Vorwaertsgeschwindigkeit (m/s) - Reserve fuer spaetere Doppler-/Lautstaerke-Anpassung. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void SetForwardSpeed(float MetersPerS);

	/** Schaltet den Flugsound komplett aus/an. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void SetSoundEnabled(bool bEnabled);

	/** Rotor-Sound-Asset (optional; sonst prozeduraler Fallback). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Audio")
	USoundWave* RotorSound = nullptr;

	/** Triebwerk-Sound-Asset (optional; sonst prozeduraler Fallback). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Audio")
	USoundWave* EngineSound = nullptr;

	/** Fahrtwind-Asset: haengt an der Geschwindigkeit, nicht an der Drehzahl. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Audio")
	USoundWave* WindSound = nullptr;

	/**
	 * Geschwindigkeit (m/s), bei der der Wind voll hoechst (Ka-52 Reiseflug
	 * 300 km/h = 83 m/s). Darueber wird die Windlautstaerke linear gerechnet.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Audio", meta = (ClampMin = "1.0"))
	float WindFullSpeedMetersPerS = 80.0f;

	/** Ohne Assets einen prozeduralen Rotor-/Motor-Klang erzeugen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Audio")
	bool bUseProceduralFallback = true;

	/** Blattzahl je Hauptrotor (bestimmt die Wop-Wop-Frequenz). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Audio", meta = (ClampMin = "1"))
	int32 BladeCount = 4;

	/** AM-Tiefe des Rotorschlags (0 = glatt, 1 = harter Blade Slap wie beim Ka-52). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BladeSlapDepth = 0.38f;

	/** Basis-Grenzfrequenz des Rotor-Tiefpasses (niedriger = dumpfer Kampfheli-Schlag). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Audio", meta = (ClampMin = "40.0", ClampMax = "2000.0"))
	float RotorCutoffBaseHz = 180.0f;

	/** Maximale Puffergroesse des prozeduralen Sounds (Sekunden). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Audio", meta = (ClampMin = "0.05"))
	float MaxQueueSeconds = 0.5f;

	/** Abspiel-Lautstaerke des Flugsounds (Master). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Audio", meta = (ClampMin = "0.0"))
	float MasterVolume = 0.55f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Audio")
	UAudioComponent* RotorAudio = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Audio")
	UAudioComponent* EngineAudio = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Audio")
	UAudioComponent* WindAudio = nullptr;

private:
	void CreateAudioSources();
	void UpdateAssetAudio();
	void PushProceduralAudio();
	void ApplyMasterVolume();

	UPROPERTY()
	USoundWaveProcedural* ProceduralWave = nullptr;

	FWiesbadenHelicopterAudioParams Params;
	TArray<int16> SampleBuffer;
	uint32 AudioSeed = 0x5EED0001u;
	// Fortlaufende Wiedergabe-Zeit ueber alle Puffer - haelt die Sinus-Phasen
	// (Turbine/Rotor) puffueberdeckend stetig, damit nichts mit der Pufferrate buzzt.
	double AudioTimeSeconds = 0.0;
	bool bEnabled = true;
};
