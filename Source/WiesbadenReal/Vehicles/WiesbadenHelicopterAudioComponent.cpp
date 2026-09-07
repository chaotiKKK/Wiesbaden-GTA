// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenHelicopterAudioComponent.h"

#include "WiesbadenReal.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
	constexpr int32 ProceduralSampleRate = 44100;
	constexpr int32 SamplesPerPush = 2048;
}

UWiesbadenHelicopterAudioComponent::UWiesbadenHelicopterAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SampleBuffer.SetNum(SamplesPerPush);
}

void UWiesbadenHelicopterAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	CreateAudioSources();
}

void UWiesbadenHelicopterAudioComponent::CreateAudioSources()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogWbVehicles, Warning, TEXT("Flugsound-Komponente ohne Owner - keine Audio-Quellen."));
		return;
	}

	const bool bUseAssets = (RotorSound != nullptr || EngineSound != nullptr);

	RotorAudio = NewObject<UAudioComponent>(Owner, TEXT("RotorAudio"));
	RotorAudio->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	RotorAudio->RegisterComponent();

	EngineAudio = NewObject<UAudioComponent>(Owner, TEXT("EngineAudio"));
	EngineAudio->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	EngineAudio->RegisterComponent();

	ApplyMasterVolume();

	if (bUseAssets)
	{
		if (RotorSound)
		{
			RotorAudio->SetSound(RotorSound);
			RotorAudio->Play();
		}
		if (EngineSound)
		{
			EngineAudio->SetSound(EngineSound);
			EngineAudio->Play();
		}
	}
	else if (bUseProceduralFallback)
	{
		ProceduralWave = NewObject<USoundWaveProcedural>(Owner, TEXT("HeliProceduralSound"));
		ProceduralWave->NumChannels = 1;
		ProceduralWave->SetSampleRate(ProceduralSampleRate);
		ProceduralWave->SampleByteSize = 2; // int16 mono

		RotorAudio->SetSound(ProceduralWave);
		RotorAudio->Play();
		EngineAudio->SetVolumeMultiplier(0.0f);

		UE_LOG(LogWbVehicles, Log, TEXT("Flugsound: prozeduraler Rotor-/Motor-Klang aktiviert (44100 Hz, mono)."));
	}
	else
	{
		UE_LOG(LogWbVehicles, Warning, TEXT("Flugsound: weder Assets noch prozeduraler Fallback - stumm."));
	}
}

void UWiesbadenHelicopterAudioComponent::SetRotorState(float MainRotorRpm, float Collective)
{
	Params.MainRotorRpm = FMath::Max(MainRotorRpm, 0.0f);
	Params.Collective = FMath::Clamp(Collective, 0.0f, 1.0f);
	Params.BladeCount = FMath::Max(1, BladeCount);
	Params.BladeSlapDepth = FMath::Clamp(BladeSlapDepth, 0.0f, 1.0f);
	Params.RotorCutoffBaseHz = FMath::Clamp(RotorCutoffBaseHz, 40.0f, 2000.0f);
}

void UWiesbadenHelicopterAudioComponent::SetEngineState(float EngineRpm, bool bEngineRunning)
{
	Params.EngineRpm = FMath::Max(EngineRpm, 0.0f);
	Params.bEngineRunning = bEngineRunning;
}

void UWiesbadenHelicopterAudioComponent::SetForwardSpeed(float MetersPerS)
{
	Params.ForwardSpeedMetersPerS = FMath::Max(MetersPerS, 0.0f);
}

void UWiesbadenHelicopterAudioComponent::SetSoundEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
	ApplyMasterVolume();
}

void UWiesbadenHelicopterAudioComponent::ApplyMasterVolume()
{
	const float Volume = bEnabled ? FMath::Clamp(MasterVolume, 0.0f, 1.0f) : 0.0f;
	if (RotorAudio)
	{
		RotorAudio->SetVolumeMultiplier(Volume);
	}
	if (EngineAudio)
	{
		EngineAudio->SetVolumeMultiplier(Volume);
	}
}

void UWiesbadenHelicopterAudioComponent::UpdateAssetAudio()
{
	if (!bEnabled || !RotorAudio || !EngineAudio)
	{
		return;
	}

	// Asset-Parameter: Pitch folgt der Drehzahl, Lautstaerke der Blattlast.
	const float RotorPitch = FMath::Max(Params.MainRotorRpm, 0.0f) / 420.0f;
	RotorAudio->SetPitchMultiplier(RotorPitch);
	RotorAudio->SetVolumeMultiplier(
		MasterVolume * (0.3f + 0.7f * Params.Collective));

	const float EnginePitch = FMath::Max(Params.EngineRpm, 0.0f) / 3000.0f;
	EngineAudio->SetPitchMultiplier(EnginePitch);
	EngineAudio->SetVolumeMultiplier(Params.bEngineRunning ? MasterVolume : 0.0f);
}

void UWiesbadenHelicopterAudioComponent::PushProceduralAudio()
{
	if (!bEnabled || !ProceduralWave || !RotorAudio)
	{
		return;
	}

	// Nur nachfuellen, wenn die Warteschlange zu leeren droht - sonst drifft
	// der Puffer (mehr gepusht als konsumiert) und der Sound laeuft nach.
	const int32 BytesPerSample = 2;
	const int32 MaxQueuedBytes = FMath::Max(1, FMath::RoundToInt(MaxQueueSeconds * ProceduralSampleRate)) * BytesPerSample;
	if (ProceduralWave->GetAvailableAudioByteCount() >= MaxQueuedBytes)
	{
		return;
	}

	// Laufzeit-Seed pro Push: identische Parameter klingen nie exakt gleich.
	AudioSeed = (AudioSeed * 1664525u) + 1013904223u;

	FWiesbadenHelicopterAudioModel::GenerateSamples(
		Params, ProceduralSampleRate, SamplesPerPush, AudioSeed, SampleBuffer.GetData(),
		AudioTimeSeconds);

	// Zeitbasis um exakt die erzeugte Pufferlaenge weiterzaehlen -> der naechste
	// Puffer setzt phasenstetig an (kein Klick/Buzz an der Puffergrenze).
	AudioTimeSeconds += static_cast<double>(SamplesPerPush) / ProceduralSampleRate;

	ProceduralWave->QueueAudio(
		reinterpret_cast<const uint8*>(SampleBuffer.GetData()),
		SampleBuffer.Num() * BytesPerSample);
}

void UWiesbadenHelicopterAudioComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (RotorSound || EngineSound)
	{
		UpdateAssetAudio();
	}
	else
	{
		PushProceduralAudio();
	}
}
