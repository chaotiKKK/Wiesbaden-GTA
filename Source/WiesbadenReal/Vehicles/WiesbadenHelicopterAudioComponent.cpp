// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenHelicopterAudioComponent.h"

#include "Audio/WiesbadenAudioPropagation.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include "WiesbadenReal.h"

#include "Audio/WiesbadenAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
	constexpr int32 ProceduralSampleRate = 44100;
	constexpr int32 SamplesPerPush = 2048;

	// Bezugsdrehzahlen der Ka-52-Loop-Assets (Tools/make_ka52_audio.py).
	// Konstant und keine UPROPERTYs: sie gehoeren zum Klang, nicht zur
	// Einstellung. Aendert man sie hier, ohne die WAV neu zu bauen, liegt
	// der Ton bei Reiseflug falsch - und genau das war der Fehler, den die
	// alten Teiler 560/3800 machten.
	constexpr float RotorBezugsRpm = 300.0f;
	constexpr float TriebwerkBezugsRpm = 600.0f;
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

	// Der Wind hat seine eigene Quelle: er haengt an der Geschwindigkeit
	// und darf deshalb nicht mit der Drehzahl hoch- und heruntergezogen
	// werden wie Rotor und Turbine.
	WindAudio = NewObject<UAudioComponent>(Owner, TEXT("WindAudio"));
	WindAudio->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	WindAudio->RegisterComponent();

	// Rotor + Turbine in den Fahrzeug-Bus des Mischpults einordnen (nullptr, falls
	// die Mix-Assets fehlen -> dann ohne Bus, kein Fehler).
	if (USoundClass* VehicleBus = UWiesbadenAudioSubsystem::LoadBusSoundClass(EWbAudioBus::Vehicle))
	{
		RotorAudio->SoundClassOverride = VehicleBus;
		EngineAudio->SoundClassOverride = VehicleBus;
		WindAudio->SoundClassOverride = VehicleBus;
	}

	ApplyMasterVolume();

	// Ausbreitung: weit (Rotor/Turbine tragen) inkl. Occlusion + Hall-Send.
	WiesbadenAudioPropagation::ConfigureSource(RotorAudio, EWbAudioRange::Far);
	WiesbadenAudioPropagation::ConfigureSource(EngineAudio, EWbAudioRange::Far);
	// Der Wind ist an der eigenen Quelle am lautesten und verliert mit der
	// Entfernung zuerst - sonst fliegt man mit ausgeschaltetem Triebwerk
	// schneller, als der eigene Fahrtwind hoeren laesst.
	WiesbadenAudioPropagation::ConfigureSource(WindAudio, EWbAudioRange::Mid);

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
		// Fehlt der Wind, ist das kein Fehler: er ist zusaetzlich zum
		// Rotor, nicht Voraussetzung fuer den Start.
		if (WindSound)
		{
			WindAudio->SetSound(WindSound);
			WindAudio->Play();
		}
		UE_LOG(LogWbVehicles, Log,
			TEXT("Flugsound-Assets aktiv (Rotor %s, Triebwerk %s, Wind %s)."),
			RotorSound ? TEXT("ja") : TEXT("nein"),
			EngineSound ? TEXT("ja") : TEXT("nein"),
			WindSound ? TEXT("ja") : TEXT("nein"));
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
	if (WindAudio)
	{
		WindAudio->SetVolumeMultiplier(Volume);
	}
}

void UWiesbadenHelicopterAudioComponent::UpdateAssetAudio()
{
	if (!bEnabled || !RotorAudio || !EngineAudio)
	{
		return;
	}

	// Asset-Parameter: Pitch folgt der Drehzahl, Lautstaerke der Blattlast.
	// Die Teiler sind die Bezugsdrehzahlen der Ka-52-Loops aus
	// Tools/make_ka52_audio.py: 300 rpm Rotor, 600 rpm Triebwerk. Bei
	// Reiseflug (350 rpm / 700 rpm) liegt der Pitch knapp ueber 1, der Ton
	// bleibt also, wie er gebaut wurde. Mit den aelteren Teilern 560 und
	// 3800 lag er bei Reiseflug eine Oktave zu tief und klang wie ein
	// Hubschrauber im Leerlauf auf 200 m.
	// Doppler ueber die Relativbewegung - der lange gemerkte ForwardSpeed-
	// Parameter ist damit verbraucht: Vorbeifahrt klingt auf und ab.
	APawn* ListenerPawn = nullptr;
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			ListenerPawn = PC->GetPawn();
		}
	}
	const float Doppler = WiesbadenAudioPropagation::ComputeDopplerForActors(GetOwner(), ListenerPawn);

	const float RotorPitch = FMath::Max(Params.MainRotorRpm, 0.0f) / RotorBezugsRpm;
	RotorAudio->SetPitchMultiplier(FMath::Max(RotorPitch, 0.01f) * Doppler);
	RotorAudio->SetVolumeMultiplier(
		MasterVolume * (0.3f + 0.7f * Params.Collective));

	const float EnginePitch = FMath::Max(Params.EngineRpm, 0.0f) / TriebwerkBezugsRpm;
	EngineAudio->SetPitchMultiplier(FMath::Max(EnginePitch, 0.01f));
	EngineAudio->SetVolumeMultiplier(Params.bEngineRunning ? MasterVolume : 0.0f);

	// Wind: nur Geschwindigkeit, kein Pitch. Bei 80 m/s (288 km/h) voll.
	if (WindAudio)
	{
		const float Anteil = FMath::Clamp(Params.ForwardSpeedMetersPerS
			/ FMath::Max(WindFullSpeedMetersPerS, 1.0f), 0.0f, 1.0f);
		// Quadriert: ein linearer Anteil hiess im Reiseflug fast nichts und
		// kam erst kurz vor dem Tempolimit auf.
		WindAudio->SetVolumeMultiplier(MasterVolume * (0.08f + 0.62f * Anteil * Anteil));
	}
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

	// MetaSound-Bruecke: Blattschlag ueber RotorRpm/Collective, Heulen ueber
	// Rpm - dieselben Werte wie die prozedurale Referenzsynthese.
	if (RotorAudio)
	{
		RotorAudio->SetFloatParameter(FName(TEXT("RotorRpm")), Params.MainRotorRpm);
		RotorAudio->SetFloatParameter(FName(TEXT("Collective")), Params.Collective);
		RotorAudio->SetFloatParameter(FName(TEXT("SlapDepth")), Params.BladeSlapDepth);
	}
	if (EngineAudio)
	{
		EngineAudio->SetFloatParameter(FName(TEXT("Rpm")), Params.EngineRpm);
		EngineAudio->SetFloatParameter(FName(TEXT("Running")), Params.bEngineRunning ? 1.0f : 0.0f);
	}

	if (RotorSound || EngineSound)
	{
		UpdateAssetAudio();
	}
	else
	{
		PushProceduralAudio();
	}
}
