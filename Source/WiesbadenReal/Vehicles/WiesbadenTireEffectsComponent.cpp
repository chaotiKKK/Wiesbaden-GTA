// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenTireEffectsComponent.h"

#include "WiesbadenReal.h"

#include "Audio/WiesbadenAudioPropagation.h"
#include "Audio/WiesbadenAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/DecalComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr int32 SqSampleRate = 44100;
	constexpr int32 SqBytesPerSample = 2;
	constexpr int32 SqSamplesPerPush = 2048;   // ~46 ms
	constexpr float SqBufferSeconds = 0.2f;

	const TCHAR* SkidDecalPath = TEXT("/Game/Materials/City/M_WbTireMark.M_WbTireMark");
}

UWiesbadenTireEffectsComponent::UWiesbadenTireEffectsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UWiesbadenTireEffectsComponent::BeginPlay()
{
	Super::BeginPlay();

	SampleBuffer.SetNumUninitialized(SqSamplesPerPush);
	SkidDecalMaterial = LoadObject<UMaterialInterface>(nullptr, SkidDecalPath);
	if (!SkidDecalMaterial)
	{
		UE_LOG(LogWbVehicles, Warning, TEXT("Reifenspur-Material %s fehlt - keine Spuren."), SkidDecalPath);
	}

	CreateAudioSource();
}

void UWiesbadenTireEffectsComponent::CreateAudioSource()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	SquealAudio = NewObject<UAudioComponent>(Owner, TEXT("TireSquealAudio"));
	if (!SquealAudio)
	{
		return;
	}
	SquealAudio->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	SquealAudio->RegisterComponent();

	// Ueber den SFX-Bus des Mischpults (Master-Lautstaerke/Ducking); nullptr, falls
	// die Mix-Assets fehlen -> dann ohne Bus, kein Fehler.
	SquealAudio->SoundClassOverride = UWiesbadenAudioSubsystem::LoadBusSoundClass(EWbAudioBus::SFX);
	// Ausbreitung: mittlere Distanzkurve inkl. Occlusion + Hall-Send.
	WiesbadenAudioPropagation::ConfigureSource(SquealAudio, EWbAudioRange::Mid);

	SquealWave = NewObject<USoundWaveProcedural>(Owner, TEXT("TireSquealProceduralSound"));
	if (!SquealWave)
	{
		return;
	}
	SquealWave->NumChannels = 1;
	SquealWave->SetSampleRate(SqSampleRate);
	SquealWave->SampleByteSize = SqBytesPerSample;

	SquealAudio->SetSound(SquealWave);
	SquealAudio->SetVolumeMultiplier(1.0f);
	SquealAudio->Play();
}

float UWiesbadenTireEffectsComponent::ComputeSquealIntensity(
	bool bWheelSpin, bool bWheelLock, float SlipAngleDeg, float SpeedKmh)
{
	// Ein rutschendes Rad braucht Relativgeschwindigkeit zum Boden. Beim Radspin
	// liefert die das durchdrehende Rad selbst -> quietscht auch langsam. Beim
	// Blockieren/Drift muss das Fahrzeug fahren (ein stehendes Rad rutscht nicht).
	const float SpeedGate = FMath::Clamp((SpeedKmh - 3.0f) / 9.0f, 0.0f, 1.0f);
	const float Spin = bWheelSpin ? 1.0f : 0.0f;
	const float Lock = bWheelLock ? SpeedGate : 0.0f;
	const float Slip = FMath::Clamp((FMath::Abs(SlipAngleDeg) - 8.0f) / 12.0f, 0.0f, 1.0f) * SpeedGate;
	return FMath::Clamp(FMath::Max3(Spin, Lock, Slip), 0.0f, 1.0f);
}

void UWiesbadenTireEffectsComponent::UpdateTireEffects(
	bool bWheelSpin, bool bWheelLock, float SlipAngleDeg, float SpeedKmh,
	const TArray<FVector>& GroundContactsToMark, const FVector& TravelDir)
{
	TargetIntensity = ComputeSquealIntensity(bWheelSpin, bWheelLock, SlipAngleDeg, SpeedKmh);

	// Ereigniszeile auf der steigenden Flanke - der Nachweis, dass das Quietschen
	// bei Radspin/Blockieren ausloest.
	const bool bNowAudible = TargetIntensity > 0.05f;
	if (bNowAudible != bSquealAudible)
	{
		bSquealAudible = bNowAudible;
		if (bNowAudible)
		{
			const TCHAR* Cause = bWheelLock ? TEXT("Blockieren") : (bWheelSpin ? TEXT("Radspin") : TEXT("Drift"));
			UE_LOG(LogWbVehicles, Log, TEXT("Reifenquietschen EIN (%s, %.0f km/h)."), Cause, SpeedKmh);
		}
	}

	// Spur-Decals distanzgetaktet: Strecke aus der Owner-Bewegung aufsummieren.
	if (const AActor* Owner = GetOwner())
	{
		const FVector Pos = Owner->GetActorLocation();
		if (bHaveLastOwnerPos)
		{
			AccumMarkCm += static_cast<float>((Pos - LastOwnerPos).Size());
		}
		LastOwnerPos = Pos;
		bHaveLastOwnerPos = true;
	}

	if (bNowAudible && GroundContactsToMark.Num() > 0 && AccumMarkCm >= MarkIntervalCm)
	{
		AccumMarkCm = 0.0f;
		SpawnMarks(GroundContactsToMark, TravelDir);
	}
}

void UWiesbadenTireEffectsComponent::SpawnMarks(const TArray<FVector>& Contacts, const FVector& TravelDir)
{
	UWorld* World = GetWorld();
	if (!World || !SkidDecalMaterial)
	{
		return;
	}

	// Decal projiziert entlang -X nach unten (Standard-Pitch -90) und wird um die
	// Fahrtrichtung gedreht, damit der Fleck der Spur folgt.
	const float Yaw = TravelDir.IsNearlyZero() ? 0.0f : static_cast<float>(TravelDir.Rotation().Yaw);
	const FRotator Rot(-90.0f, Yaw, 0.0f);
	// Grosse Projektionstiefe (X), damit das Decal den Boden auch bei kleinem
	// Hoehenversatz des Aufstandspunkts sicher trifft; Y/Z = Fleckgroesse.
	const FVector Size(60.0f, 45.0f, 18.0f);

	for (const FVector& Contact : Contacts)
	{
		UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(
			World, SkidDecalMaterial, Size, Contact, Rot, MarkLifeSeconds);
		if (Decal)
		{
			Decal->SetFadeOut(FMath::Max(MarkLifeSeconds - 2.0f, 0.1f), 2.0f, false);
		}
	}
}

void UWiesbadenTireEffectsComponent::TickComponent(
	float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);

	// Lautstaerke weich nachfuehren (schnell an, langsamer aus) - kein Knacken.
	const float Rate = (TargetIntensity > CurrentIntensity) ? 22.0f : 9.0f;
	CurrentIntensity = FMath::FInterpTo(CurrentIntensity, TargetIntensity, DeltaSeconds, Rate);

	PushProceduralAudio();
}

void UWiesbadenTireEffectsComponent::PushProceduralAudio()
{
	if (!SquealWave)
	{
		return;
	}

	const int32 MaxQueuedBytes =
		FMath::Max(1, FMath::RoundToInt(SqBufferSeconds * SqSampleRate)) * SqBytesPerSample;
	if (SquealWave->GetAvailableAudioByteCount() >= MaxQueuedBytes)
	{
		return;
	}

	if (SampleBuffer.Num() != SqSamplesPerPush)
	{
		SampleBuffer.SetNumUninitialized(SqSamplesPerPush);
	}

	const float Amp = FMath::Clamp(CurrentIntensity, 0.0f, 1.0f) * FMath::Clamp(MasterGain, 0.0f, 1.0f);
	const double TwoPi = 2.0 * PI;
	const double Base1 = 1150.0;   // Screech-Grundton
	const double Base2 = 1725.0;   // Obertoene fuer den scharfen Klang
	const double VibHz = 6.0;

	for (int32 i = 0; i < SqSamplesPerPush; ++i)
	{
		const double Vib = 1.0 + 0.03 * FMath::Sin(VibratoPhase);
		VibratoPhase += TwoPi * VibHz / SqSampleRate;

		Phase1 += TwoPi * (Base1 * Vib) / SqSampleRate;
		Phase2 += TwoPi * (Base2 * Vib) / SqSampleRate;
		if (Phase1 > TwoPi) { Phase1 -= TwoPi; }
		if (Phase2 > TwoPi) { Phase2 -= TwoPi; }
		if (VibratoPhase > TwoPi) { VibratoPhase -= TwoPi; }

		const double Noise = FMath::FRandRange(-1.0f, 1.0f);
		const double S = Amp * (0.50 * FMath::Sin(Phase1) + 0.28 * FMath::Sin(Phase2) + 0.22 * Noise);
		SampleBuffer[i] = static_cast<int16>(FMath::Clamp(S, -1.0, 1.0) * 32767.0);
	}

	SquealWave->QueueAudio(
		reinterpret_cast<const uint8*>(SampleBuffer.GetData()),
		SampleBuffer.Num() * SqBytesPerSample);
}
