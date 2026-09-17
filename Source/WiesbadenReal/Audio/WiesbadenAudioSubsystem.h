// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WiesbadenAudioSubsystem.generated.h"

class USoundClass;
class USoundMix;

/** Mischpult-Kanaele (SoundClass-Gruppen). Jeder Klang laeuft in genau einen. */
UENUM(BlueprintType)
enum class EWbAudioBus : uint8
{
	Master		UMETA(DisplayName = "Master"),
	Music		UMETA(DisplayName = "Musik"),
	SFX			UMETA(DisplayName = "Effekte"),
	Ambience	UMETA(DisplayName = "Ambiente"),
	UI			UMETA(DisplayName = "Bedienung"),
	Voice		UMETA(DisplayName = "Stimme"),
	Vehicle		UMETA(DisplayName = "Fahrzeug"),
	MAX			UMETA(Hidden)
};

/**
 * Zentrales Mischpult: routet alle Klaenge ueber wenige Bus-Gruppen
 * (SoundClasses) statt Einzel-Lautstaerken, regelt Gruppen in dB und senkt
 * Musik/Ambiente unter Stimme ab (Ducking).
 *
 * Die Kanaele sind SoundClass-Assets unter /Game/Audio/Mix (SC_Master ->
 * SC_Music/SFX/Ambience/UI/Voice/Vehicle), ihre Lautstaerke wird ueber den
 * Basis-SoundMix SM_WbMaster als Klassen-Override gesetzt (Tools/make_audio_mixer.py).
 * Assets fehlen -> Subsystem bleibt inaktiv, ohne Fehler.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Aktiviert den Basis-Mix und wendet alle gespeicherten Bus-Lautstaerken an.
	 *  Braucht eine Welt + Audio-Device -> aus GameMode::BeginPlay aufrufen. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void ApplyMix();

	/** Setzt die Lautstaerke eines Busses aus einem 0..1-Regler (perzeptiv, dB).
	 *  Wird gespeichert und ueberdauert Neustarts. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void SetBusVolume(EWbAudioBus Bus, float Slider01);

	/** Zuletzt gesetzter 0..1-Regler eines Busses (Default 1.0). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Audio")
	float GetBusVolume(EWbAudioBus Bus) const;

	/** Senkt Musik + Ambiente ab (z. B. unter Dialog/Ansage) und wieder an. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void SetDuckingActive(bool bActive);

	/** SoundClass eines Busses laden (fuer AudioComponent::SoundClassOverride).
	 *  Statisch, damit Audio-Komponenten ihre Klaenge ohne Subsystem-Zugriff
	 *  in den richtigen Bus einordnen koennen. nullptr, wenn das Asset fehlt. */
	static USoundClass* LoadBusSoundClass(EWbAudioBus Bus);

private:
	void LoadAssets();
	void LoadSettings();
	void SaveSetting(EWbAudioBus Bus);
	void ApplyBus(EWbAudioBus Bus, float FadeSeconds);

	static FString BusClassObjectPath(EWbAudioBus Bus);
	static const TCHAR* BusConfigKey(EWbAudioBus Bus);

	/** SoundClass je Bus, indiziert mit (int32)EWbAudioBus (Groesse = MAX). */
	UPROPERTY(Transient) TArray<TObjectPtr<USoundClass>> BusClasses;
	UPROPERTY(Transient) TObjectPtr<USoundMix> BaseMix;

	/** 0..1-Regler je Bus, indiziert mit (int32)EWbAudioBus (Default 1.0). */
	TArray<float> BusSliders;

	bool bAssetsReady = false;
	bool bMixApplied = false;
	bool bDucked = false;

	/** Absenkung von Musik/Ambiente beim Ducking. */
	float DuckDb = -10.0f;
	/** Ducking-Zeiten: schnell rein (Musik weicht zuegig), langsam raus (kein Pumpen). */
	float DuckAttackSeconds = 0.15f;
	float DuckReleaseSeconds = 0.4f;
};
