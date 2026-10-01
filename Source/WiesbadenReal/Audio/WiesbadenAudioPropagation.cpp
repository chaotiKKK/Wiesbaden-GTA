// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WiesbadenAudioPropagation.h"

#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundSubmix.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Vehicles/WiesbadenEngineAudio.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbAudioProp, Log, All);

namespace
{
	/** Registrierte Quellen fuer die Raumzustands-Umschaltung. */
	TArray<TWeakObjectPtr<UAudioComponent>> GRegisteredSources;

	/** Zuletzt angewandter Raumzustand (Start: draussen, kein Hall). */
	// Startzustand: solange keine Raumsonde lief, gilt Outdoor - aber noch nicht
	// ANGEWANDT. Die erste Sonde wendet den Zustand zwingend an (auch wenn er
	// Outdoor ist), damit Preset und Sendepegel von Beginn an stehen.
	bool GSpaceInitialized = false;
	EWbReverbSpace GCurrentSpace = EWbReverbSpace::Outdoor;

	/** Tote Eintraege entsorgen (zerstoerte Komponenten). */
	void PruneSources()
	{
		GRegisteredSources.RemoveAll(
			[](const TWeakObjectPtr<UAudioComponent>& Ptr) { return !Ptr.IsValid(); });
	}
}

namespace WiesbadenAudioPropagation
{

float DopplerPitchFactor(float SourceSpeedTowardListenerMetersPerS, float SpeedOfSoundMetersPerS)
{
	const float C = FMath::Max(SpeedOfSoundMetersPerS, 1.0f);
	const float V = FMath::Clamp(SourceSpeedTowardListenerMetersPerS, -0.45f * C, 0.45f * C);
	return FMath::Clamp(C / (C - V), 0.7f, 1.4f);
}

EWbReverbSpace ClassifySpace(float SkyBlocked01, float WallClosure01, float CeilingHeightMeters)
{
	const float Closure = FMath::Clamp(WallClosure01, 0.0f, 1.0f);
	const float SkyBlocked = FMath::Clamp(SkyBlocked01, 0.0f, 1.0f);

	// Offen: weniger als ein Drittel der Horizontalstrahlen trifft Geometrie.
	if (Closure <= 0.34f)
	{
		return EWbReverbSpace::Outdoor;
	}

	// Geschlossen: zwei Drittel oder mehr der Strahlen. Die Deckenhoehe
	// trennt Zimmer von Halle (4 m Schwelle).
	if (Closure >= 0.67f)
	{
		return (CeilingHeightMeters >= 4.0f) ? EWbReverbSpace::Hall : EWbReverbSpace::Indoor;
	}

	// Halb offen mit verdecktem Himmel: Unterfuehrung, Bruecke, Tunnelmaul -
	// sonst ist es einfach nur eine Baumgruppe im Freien.
	return (SkyBlocked >= 0.5f) ? EWbReverbSpace::Tunnel : EWbReverbSpace::Outdoor;
}

FWbReverbTuning ReverbTuningForSpace(EWbReverbSpace Space)
{
	FWbReverbTuning Tuning;
	switch (Space)
	{
	case EWbReverbSpace::Indoor:
		// Zimmer: kurzer, dumpfer Hall - die fruehen Reflexionen dominieren.
		Tuning.bBypass = false;
		Tuning.ReflectionsDelay = 0.005f;
		Tuning.GainHF = 0.80f;
		Tuning.ReflectionsGain = 0.15f;
		Tuning.LateDelay = 0.03f;
		Tuning.DecayTime = 0.45f;
		Tuning.Density = 0.5f;
		Tuning.Diffusion = 0.6f;
		Tuning.AirAbsorptionGainHF = 0.85f;
		Tuning.DecayHFRatio = 0.7f;
		Tuning.LateGain = 0.6f;
		break;

	case EWbReverbSpace::Hall:
		// Halle/Kirche: langer, dichter Nachhall.
		Tuning.bBypass = false;
		Tuning.ReflectionsDelay = 0.012f;
		Tuning.GainHF = 0.90f;
		Tuning.ReflectionsGain = 0.08f;
		Tuning.LateDelay = 0.08f;
		Tuning.DecayTime = 2.2f;
		Tuning.Density = 0.9f;
		Tuning.Diffusion = 0.9f;
		Tuning.AirAbsorptionGainHF = 0.95f;
		Tuning.DecayHFRatio = 0.85f;
		Tuning.LateGain = 1.4f;
		break;

	case EWbReverbSpace::Tunnel:
		// Rohr: mittellang, dicht, schnell dunkel.
		Tuning.bBypass = false;
		Tuning.ReflectionsDelay = 0.008f;
		Tuning.GainHF = 0.85f;
		Tuning.ReflectionsGain = 0.12f;
		Tuning.LateDelay = 0.05f;
		Tuning.DecayTime = 1.2f;
		Tuning.Density = 0.7f;
		Tuning.Diffusion = 0.4f;
		Tuning.AirAbsorptionGainHF = 0.90f;
		Tuning.DecayHFRatio = 0.6f;
		Tuning.LateGain = 1.0f;
		break;

	default:
		// Outdoor: komplett by-passed, kein Nassanteil.
		Tuning.bBypass = true;
		Tuning.LateGain = 0.0f;
		break;
	}
	return Tuning;
}

float ReverbSendForSpace(EWbReverbSpace Space)
{
	switch (Space)
	{
	case EWbReverbSpace::Indoor: return 0.18f;
	case EWbReverbSpace::Hall:   return 0.30f;
	case EWbReverbSpace::Tunnel: return 0.25f;
	default:                     return 0.0f;
	}
}

float DayBedGain(float TimeOfDayHours)
{
	const float H = FMath::Fmod(FMath::Max(TimeOfDayHours, 0.0f), 24.0f);
	if (H >= 7.0f && H <= 19.0f)
	{
		return 1.0f;
	}
	if (H >= 21.0f || H <= 5.0f)
	{
		return 0.0f;
	}
	if (H > 19.0f)
	{
		// Abenddaemmerung 19 -> 21 Uhr.
		return 1.0f - (H - 19.0f) / 2.0f;
	}
	// Morgendaemmerung 5 -> 7 Uhr.
	return (H - 5.0f) / 2.0f;
}

float NightBedGain(float TimeOfDayHours)
{
	return 1.0f - DayBedGain(TimeOfDayHours);
}

TArray<TPair<FName, float>> EngineParamPairs(const FWiesbadenEngineAudioParams& Params)
{
	TArray<TPair<FName, float>> Pairs;
	Pairs.Emplace(FName(TEXT("Rpm")), Params.EngineRpm);
	Pairs.Emplace(FName(TEXT("Throttle")), Params.Throttle);
	Pairs.Emplace(FName(TEXT("SpeedKmh")), Params.SpeedKmh);
	Pairs.Emplace(FName(TEXT("EngineRunning")), Params.bEngineRunning ? 1.0f : 0.0f);
	Pairs.Emplace(FName(TEXT("Horn")), Params.bHorn ? 1.0f : 0.0f);
	return Pairs;
}

FString AttenuationPath(EWbAudioRange Range)
{
	const TCHAR* Name = TEXT("ATT_Mid");
	switch (Range)
	{
	case EWbAudioRange::Near: Name = TEXT("ATT_Near"); break;
	case EWbAudioRange::Far:  Name = TEXT("ATT_Far");  break;
	default: break;
	}
	return FString::Printf(TEXT("/Game/Audio/Mix/%s.%s"), Name, Name);
}

FString ReverbSubmixPath()
{
	return TEXT("/Game/Audio/Mix/SBX_Reverb.SBX_Reverb");
}

FString ReverbPresetPath()
{
	return TEXT("/Game/Audio/Mix/SFXP_Reverb.SFXP_Reverb");
}

FString AmbienceBedPath(FName BedName)
{
	const FString Name = FString::Printf(TEXT("MS_Amb%s"), *BedName.ToString());
	return FString::Printf(TEXT("/Game/Audio/Meta/%s.%s"), *Name, *Name);
}

FString AmbienceSamplePath(FName SampleName)
{
	const FString Name = SampleName.ToString();
	return FString::Printf(TEXT("/Game/Audio/Samples/%s.%s"), *Name, *Name);
}

FString EngineMetaSoundPath()
{
	return TEXT("/Game/Audio/Meta/MS_EngineBoxer.MS_EngineBoxer");
}

USoundAttenuation* LoadAttenuation(EWbAudioRange Range)
{
	return LoadObject<USoundAttenuation>(nullptr, *AttenuationPath(Range));
}

USoundSubmix* LoadReverbSubmix()
{
	return LoadObject<USoundSubmix>(nullptr, *ReverbSubmixPath());
}

USubmixEffectReverbPreset* LoadReverbPreset()
{
	return LoadObject<USubmixEffectReverbPreset>(nullptr, *ReverbPresetPath());
}

void ConfigureSource(UAudioComponent* Source, EWbAudioRange Range, bool bSpatialized)
{
	if (!Source)
	{
		return;
	}

	Source->bAllowSpatialization = bSpatialized;

	if (USoundAttenuation* Attenuation = LoadAttenuation(Range))
	{
		// Distanzkurve inklusive Occlusion und Entfernungs-Tiefpass; die
		// Werte stecken im Asset (Tools/make_audio_assets), nicht im Code.
		Source->bOverrideAttenuation = true;
		Source->AttenuationSettings = Attenuation;
	}

	GRegisteredSources.AddUnique(Source);

	if (USoundSubmix* Reverb = LoadReverbSubmix())
	{
		Source->SetSubmixSend(Reverb, ReverbSendForSpace(GCurrentSpace));
	}
}

void UnregisterSource(UAudioComponent* Source)
{
	if (!Source)
	{
		return;
	}
	GRegisteredSources.RemoveAll(
		[Source](const TWeakObjectPtr<UAudioComponent>& Ptr) { return !Ptr.IsValid() || Ptr.Get() == Source; });
}

void ApplySpaceState(EWbReverbSpace Space)
{
	if (GSpaceInitialized && GCurrentSpace == Space)
	{
		return;
	}
	GSpaceInitialized = true;
	GCurrentSpace = Space;

	if (USubmixEffectReverbPreset* Preset = LoadReverbPreset())
	{
		const FWbReverbTuning Tuning = ReverbTuningForSpace(Space);
		FSubmixEffectReverbSettings Settings;
		Settings.bBypass = Tuning.bBypass;
		Settings.bBypassEarlyReflections = Tuning.bBypassEarlyReflections;
		Settings.bBypassLateReflections = Tuning.bBypassLateReflections;
		Settings.ReflectionsDelay = Tuning.ReflectionsDelay;
		Settings.GainHF = Tuning.GainHF;
		Settings.ReflectionsGain = Tuning.ReflectionsGain;
		Settings.LateDelay = Tuning.LateDelay;
		Settings.DecayTime = Tuning.DecayTime;
		Settings.Density = Tuning.Density;
		Settings.Diffusion = Tuning.Diffusion;
		Settings.AirAbsorptionGainHF = Tuning.AirAbsorptionGainHF;
		Settings.DecayHFRatio = Tuning.DecayHFRatio;
		Settings.LateGain = Tuning.LateGain;
		Preset->SetSettings(Settings);
	}

	USoundSubmix* Reverb = LoadReverbSubmix();
	PruneSources();
	if (Reverb)
	{
		const float Send = ReverbSendForSpace(Space);
		for (const TWeakObjectPtr<UAudioComponent>& Ptr : GRegisteredSources)
		{
			if (UAudioComponent* Component = Ptr.Get())
			{
				Component->SetSubmixSend(Reverb, Send);
			}
		}
	}

	UE_LOG(LogWbAudioProp, Log, TEXT("Raumzustand %d (Send %.2f) angewandt auf %d Quellen."),
		static_cast<int32>(Space), ReverbSendForSpace(Space), GRegisteredSources.Num());
}

EWbReverbSpace GetCurrentSpace()
{
	return GCurrentSpace;
}

float ComputeDopplerForActors(const AActor* SourceActor, const AActor* ListenerActor)
{
	if (!SourceActor || !ListenerActor)
	{
		return 1.0f;
	}
	const FVector ToListener = ListenerActor->GetActorLocation() - SourceActor->GetActorLocation();
	if (ToListener.IsNearlyZero())
	{
		return 1.0f;
	}
	// Annaeherungsgeschwindigkeit der Quelle entlang der Hoerlinie.
	const float ApproachMetersPerS =
		ToListener.GetSafeNormal() | (SourceActor->GetVelocity() - ListenerActor->GetVelocity());
	return DopplerPitchFactor(ApproachMetersPerS);
}

} // namespace WiesbadenAudioPropagation
