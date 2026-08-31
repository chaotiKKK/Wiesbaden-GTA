// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenCarAudioComponent.h"

#include "WiesbadenReal.h"

#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
	/** Abtastrate der Synthese. */
	constexpr int32 EngineSampleRate = 44100;

	/** Bytes je Sample (int16 mono). */
	constexpr int32 BytesPerSample = 2;

	/**
	 * Samples je Nachfuellvorgang. 2048 entspricht rund 46 ms - kurz genug,
	 * dass ein Gasstoss unmittelbar hoerbar wird, lang genug, dass der
	 * Aufwand je Block nicht ins Gewicht faellt.
	 */
	constexpr int32 SamplesPerPush = 2048;
}

UWiesbadenCarAudioComponent::UWiesbadenCarAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UWiesbadenCarAudioComponent::BeginPlay()
{
	Super::BeginPlay();

	AudioParams.CylinderCount = FMath::Clamp(CylinderCount, 1, 16);
	AudioParams.IdleRpm = FMath::Max(300.0f, IdleRpm);
	AudioParams.MasterGain = FMath::Clamp(MasterGain, 0.0f, 1.0f);
	AudioParams.EngineRpm = AudioParams.IdleRpm;

	AudioState.Reset();
	SampleBuffer.SetNumUninitialized(SamplesPerPush);

	CreateAudioSource();
}

void UWiesbadenCarAudioComponent::CreateAudioSource()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogWbVehicles, Warning, TEXT("Motorsound-Komponente ohne Owner - kein Klang."));
		return;
	}

	EngineAudio = NewObject<UAudioComponent>(Owner, TEXT("CarEngineAudio"));
	if (!EngineAudio)
	{
		return;
	}

	EngineAudio->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	EngineAudio->RegisterComponent();

	// Raeumlich: der Motor sitzt beim Kaefer hinten, und beim Vorbeifahren
	// soll der Klang von dort kommen.
	EngineAudio->bAllowSpatialization = true;
	EngineAudio->SetVolumeMultiplier(FMath::Clamp(MasterGain, 0.0f, 1.0f));

	if (EngineSound)
	{
		bProcedural = false;
		EngineAudio->SetSound(EngineSound);
		EngineAudio->Play();
		UE_LOG(LogWbVehicles, Log, TEXT("Motorsound: Asset '%s' wird verwendet."), *EngineSound->GetName());
		return;
	}

	bProcedural = true;

	ProceduralWave = NewObject<USoundWaveProcedural>(Owner, TEXT("CarEngineProceduralSound"));
	if (!ProceduralWave)
	{
		UE_LOG(LogWbVehicles, Warning, TEXT("Motorsound: prozedurale Welle konnte nicht erzeugt werden."));
		return;
	}

	ProceduralWave->NumChannels = 1;
	ProceduralWave->SetSampleRate(EngineSampleRate);
	ProceduralWave->SampleByteSize = BytesPerSample;

	EngineAudio->SetSound(ProceduralWave);
	EngineAudio->Play();

	UE_LOG(LogWbVehicles, Log,
		TEXT("Motorsound: prozeduraler Vierzylinder-Boxer aktiviert (%d Hz, mono)."), EngineSampleRate);
}

void UWiesbadenCarAudioComponent::SetEngineState(float EngineRpm, float Throttle, float SpeedKmh)
{
	AudioParams.EngineRpm = FMath::Max(0.0f, EngineRpm);
	AudioParams.Throttle = FMath::Clamp(Throttle, 0.0f, 1.0f);
	AudioParams.SpeedKmh = SpeedKmh;
	AudioParams.MasterGain = FMath::Clamp(MasterGain, 0.0f, 1.0f);
	AudioParams.CylinderCount = FMath::Clamp(CylinderCount, 1, 16);
	AudioParams.IdleRpm = FMath::Max(300.0f, IdleRpm);
}

void UWiesbadenCarAudioComponent::SetHorn(bool bPressed)
{
	// Die Hupe laeuft ueber denselben Synthesizer wie der Motor (siehe
	// FWiesbadenEngineAudioParams::bHorn). Eine zweite Klangquelle am selben
	// Fahrzeug braeuchte eigenen Puffer, eigene Mischung und eigene
	// Entfernungsdaempfung - fuer zwei Sinustoene lohnt das nicht.
	AudioParams.bHorn = bPressed;
}

void UWiesbadenCarAudioComponent::SetEngineRunning(bool bRunning)
{
	if (AudioParams.bEngineRunning == bRunning)
	{
		return;
	}

	AudioParams.bEngineRunning = bRunning;

	// Beim Abstellen den Syntheserzustand zuruecksetzen, damit der Motor
	// beim naechsten Start nicht mitten in einer Phase wieder einsetzt.
	if (!bRunning)
	{
		AudioState.Reset();
	}

	UE_LOG(LogWbVehicles, Verbose, TEXT("Motor %s."), bRunning ? TEXT("gestartet") : TEXT("abgestellt"));
}

void UWiesbadenCarAudioComponent::PushProceduralAudio()
{
	if (!ProceduralWave || !EngineAudio)
	{
		return;
	}

	// Nur nachfuellen, wenn der Vorrat zur Neige geht. Ohne diese Schranke
	// wuerde je Tick nachgeschoben, die Warteschlange waechst unbegrenzt und
	// der Klang laeuft der Fahrt hinterher.
	const int32 MaxQueuedBytes =
		FMath::Max(1, FMath::RoundToInt(FMath::Clamp(BufferSeconds, 0.05f, 1.0f) * EngineSampleRate))
		* BytesPerSample;

	if (ProceduralWave->GetAvailableAudioByteCount() >= MaxQueuedBytes)
	{
		return;
	}

	if (SampleBuffer.Num() != SamplesPerPush)
	{
		SampleBuffer.SetNumUninitialized(SamplesPerPush);
	}

	FWiesbadenEngineAudioModel::GenerateSamples(
		AudioParams, EngineSampleRate, SamplesPerPush, AudioState, SampleBuffer.GetData());

	ProceduralWave->QueueAudio(
		reinterpret_cast<const uint8*>(SampleBuffer.GetData()),
		SampleBuffer.Num() * BytesPerSample);
}

void UWiesbadenCarAudioComponent::TickComponent(
	float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);

	if (!EngineAudio)
	{
		return;
	}

	if (bProcedural)
	{
		PushProceduralAudio();
		return;
	}

	// Asset-Betrieb: Tonhoehe und Lautstaerke folgen Drehzahl und Last.
	const float Pitch = FMath::Clamp(AudioParams.EngineRpm / FMath::Max(1.0f, IdleRpm * 3.0f), 0.4f, 2.5f);
	EngineAudio->SetPitchMultiplier(Pitch);
	EngineAudio->SetVolumeMultiplier(
		AudioParams.bEngineRunning
			? FMath::Clamp(MasterGain, 0.0f, 1.0f) * (0.4f + 0.6f * AudioParams.Throttle)
			: 0.0f);
}
