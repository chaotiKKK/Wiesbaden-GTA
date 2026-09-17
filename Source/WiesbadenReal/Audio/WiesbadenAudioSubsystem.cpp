// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WiesbadenAudioSubsystem.h"

#include "Audio/WiesbadenAudioMix.h"
#include "WiesbadenReal.h"

#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbAudio, Log, All);

namespace
{
	// Master zuerst - wirkt als Gesamtlautstaerke ueber der Bus-Hierarchie.
	constexpr EWbAudioBus GControlledBuses[] = {
		EWbAudioBus::Master, EWbAudioBus::Music, EWbAudioBus::SFX,
		EWbAudioBus::Ambience, EWbAudioBus::UI, EWbAudioBus::Voice, EWbAudioBus::Vehicle };

	const TCHAR* GAudioConfigSection = TEXT("WiesbadenReal.Audio");
}

FString UWiesbadenAudioSubsystem::BusClassObjectPath(EWbAudioBus Bus)
{
	const TCHAR* Name = TEXT("SC_Master");
	switch (Bus)
	{
	case EWbAudioBus::Master:   Name = TEXT("SC_Master"); break;
	case EWbAudioBus::Music:    Name = TEXT("SC_Music"); break;
	case EWbAudioBus::SFX:      Name = TEXT("SC_SFX"); break;
	case EWbAudioBus::Ambience: Name = TEXT("SC_Ambience"); break;
	case EWbAudioBus::UI:       Name = TEXT("SC_UI"); break;
	case EWbAudioBus::Voice:    Name = TEXT("SC_Voice"); break;
	case EWbAudioBus::Vehicle:  Name = TEXT("SC_Vehicle"); break;
	default: break;
	}
	return FString::Printf(TEXT("/Game/Audio/Mix/%s.%s"), Name, Name);
}

const TCHAR* UWiesbadenAudioSubsystem::BusConfigKey(EWbAudioBus Bus)
{
	switch (Bus)
	{
	case EWbAudioBus::Master:   return TEXT("MasterVolume");
	case EWbAudioBus::Music:    return TEXT("MusicVolume");
	case EWbAudioBus::SFX:      return TEXT("SFXVolume");
	case EWbAudioBus::Ambience: return TEXT("AmbienceVolume");
	case EWbAudioBus::UI:       return TEXT("UIVolume");
	case EWbAudioBus::Voice:    return TEXT("VoiceVolume");
	case EWbAudioBus::Vehicle:  return TEXT("VehicleVolume");
	default: return TEXT("Volume");
	}
}

USoundClass* UWiesbadenAudioSubsystem::LoadBusSoundClass(EWbAudioBus Bus)
{
	return LoadObject<USoundClass>(nullptr, *BusClassObjectPath(Bus));
}

void UWiesbadenAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	BusSliders.Init(1.0f, static_cast<int32>(EWbAudioBus::MAX));
	LoadSettings();
	LoadAssets();

	UE_LOG(LogWbAudio, Log, TEXT("Audio-Mixer bereit=%d (Klassen %d, BasisMix %d)."),
		bAssetsReady ? 1 : 0, BusClasses.Num(), BaseMix ? 1 : 0);
}

void UWiesbadenAudioSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UWiesbadenAudioSubsystem::LoadAssets()
{
	BusClasses.Init(nullptr, static_cast<int32>(EWbAudioBus::MAX));
	int32 Loaded = 0;
	for (const EWbAudioBus Bus : GControlledBuses)
	{
		USoundClass* SC = LoadObject<USoundClass>(nullptr, *BusClassObjectPath(Bus));
		BusClasses[static_cast<int32>(Bus)] = SC;
		if (SC) { ++Loaded; }
	}
	BaseMix = LoadObject<USoundMix>(nullptr, TEXT("/Game/Audio/Mix/SM_WbMaster.SM_WbMaster"));
	bAssetsReady = (BaseMix != nullptr) && (Loaded == UE_ARRAY_COUNT(GControlledBuses));
}

void UWiesbadenAudioSubsystem::LoadSettings()
{
	if (!GConfig)
	{
		return;
	}
	for (const EWbAudioBus Bus : GControlledBuses)
	{
		float Value = 1.0f;
		if (GConfig->GetFloat(GAudioConfigSection, BusConfigKey(Bus), Value, GGameUserSettingsIni))
		{
			const int32 Idx = static_cast<int32>(Bus);
			if (BusSliders.IsValidIndex(Idx)) { BusSliders[Idx] = FMath::Clamp(Value, 0.0f, 1.0f); }
		}
	}
}

void UWiesbadenAudioSubsystem::SaveSetting(EWbAudioBus Bus)
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetFloat(GAudioConfigSection, BusConfigKey(Bus), GetBusVolume(Bus), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UWiesbadenAudioSubsystem::ApplyMix()
{
	if (!bAssetsReady)
	{
		return;
	}
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	UGameplayStatics::SetBaseSoundMix(World, BaseMix);
	bMixApplied = true;
	for (const EWbAudioBus Bus : GControlledBuses)
	{
		ApplyBus(Bus, 0.0f);
	}
}

void UWiesbadenAudioSubsystem::ApplyBus(EWbAudioBus Bus, float FadeSeconds)
{
	if (!bAssetsReady || !bMixApplied)
	{
		return;
	}
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const int32 Idx = static_cast<int32>(Bus);
	USoundClass* Class = BusClasses.IsValidIndex(Idx) ? BusClasses[Idx].Get() : nullptr;
	if (!World || !Class)
	{
		return;
	}

	float Linear = WiesbadenAudioMix::SliderToLinear(GetBusVolume(Bus));
	// Ducking senkt Musik + Ambiente zusaetzlich ab.
	if (bDucked && (Bus == EWbAudioBus::Music || Bus == EWbAudioBus::Ambience))
	{
		Linear *= WiesbadenAudioMix::DbToLinear(DuckDb);
	}

	// bApplyToChildren=false: jede Klasse traegt ihren eigenen Faktor; die
	// Master-Gruppe multipliziert ueber die SoundClass-Hierarchie automatisch.
	UGameplayStatics::SetSoundMixClassOverride(
		World, BaseMix, Class, Linear, /*Pitch=*/1.0f, FadeSeconds, /*bApplyToChildren=*/false);
}

void UWiesbadenAudioSubsystem::SetBusVolume(EWbAudioBus Bus, float Slider01)
{
	const int32 Idx = static_cast<int32>(Bus);
	if (BusSliders.IsValidIndex(Idx)) { BusSliders[Idx] = FMath::Clamp(Slider01, 0.0f, 1.0f); }
	SaveSetting(Bus);
	ApplyBus(Bus, 0.05f);
}

float UWiesbadenAudioSubsystem::GetBusVolume(EWbAudioBus Bus) const
{
	const int32 Idx = static_cast<int32>(Bus);
	return BusSliders.IsValidIndex(Idx) ? BusSliders[Idx] : 1.0f;
}

void UWiesbadenAudioSubsystem::SetDuckingActive(bool bActive)
{
	if (bDucked == bActive)
	{
		return;
	}
	bDucked = bActive;
	const float Fade = bActive ? DuckAttackSeconds : DuckReleaseSeconds;
	UE_LOG(LogWbAudio, Log, TEXT("Ducking %s (%.2f s; Mix bereit=%d angewendet=%d)."),
		bActive ? TEXT("AN") : TEXT("AUS"), Fade, bAssetsReady ? 1 : 0, bMixApplied ? 1 : 0);
	ApplyBus(EWbAudioBus::Music, Fade);
	ApplyBus(EWbAudioBus::Ambience, Fade);
}
