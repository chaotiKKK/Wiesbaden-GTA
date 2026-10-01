// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "NPC/WiesbadenPoliceCar.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/AudioComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundWaveProcedural.h"
#include "Audio/WiesbadenAudioSubsystem.h"
#include "Audio/WiesbadenAudioPropagation.h"
#include "World/WiesbadenCitySubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

AWiesbadenPoliceCar::AWiesbadenPoliceCar()
{
	LightBar = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PoliceLightBar"));
	LightBar->SetupAttachment(VisualRoot);
	LightBar->SetRelativeLocation(FVector(0, 0, 150));
	LightBar->SetRelativeScale3D(FVector(.25, .95, .12));
	LightBar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) { LightBar->SetStaticMesh(Cube.Object); }
	BlueLeft = CreateDefaultSubobject<UPointLightComponent>(TEXT("PoliceBlueLeft"));
	BlueRight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PoliceBlueRight"));
	for (UPointLightComponent* Light : { BlueLeft.Get(), BlueRight.Get() })
	{
		Light->SetupAttachment(VisualRoot); Light->SetLightColor(FLinearColor(0.02f, 0.12f, 1.0f));
		Light->SetIntensity(0); Light->SetAttenuationRadius(900); Light->SetCastShadows(false);
	}
	BlueLeft->SetRelativeLocation(FVector(0, -40, 160));
	BlueRight->SetRelativeLocation(FVector(0, 40, 160));
	PoliceLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PoliceLabel"));
	PoliceLabel->SetupAttachment(VisualRoot);
	PoliceLabel->SetRelativeLocation(FVector(0, 78, 85));
	PoliceLabel->SetRelativeRotation(FRotator(0, 90, 0));
	PoliceLabel->SetText(FText::FromString(TEXT("POLIZEI")));
	PoliceLabel->SetHorizontalAlignment(EHTA_Center); PoliceLabel->SetWorldSize(24);
	PoliceLabel->SetTextRenderColor(FColor::White);
	Siren = CreateDefaultSubobject<UAudioComponent>(TEXT("PoliceSiren"));
	Siren->SetupAttachment(VisualRoot); Siren->bAutoActivate = false;
}
void AWiesbadenPoliceCar::SetSek(bool bInSek)
{
	if (PoliceLabel)
	{
		PoliceLabel->SetText(FText::FromString(bInSek ? TEXT("SEK") : TEXT("POLIZEI")));
	}
}

void AWiesbadenPoliceCar::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInstanceDynamic* Blue = LightBar->CreateDynamicMaterialInstance(0))
	{ Blue->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.02f, 0.12f, 1.0f)); }
	SirenWave = NewObject<USoundWaveProcedural>(this);
	SirenWave->NumChannels = 1; SirenWave->SetSampleRate(24000); SirenWave->SampleByteSize = sizeof(int16);
	Siren->SoundClassOverride = UWiesbadenAudioSubsystem::LoadBusSoundClass(EWbAudioBus::SFX);
	WiesbadenAudioPropagation::ConfigureSource(Siren, EWbAudioRange::Far, true);
	Siren->SetSound(SirenWave); Siren->SetVolumeMultiplier(.25f); Siren->Play();
}
void AWiesbadenPoliceCar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FlashTime += DeltaSeconds; CrimeCooldown = FMath::Max(0.0f, CrimeCooldown - DeltaSeconds);
	const bool Left = FMath::Fmod(FlashTime, .6f) < .3f;
	BlueLeft->SetIntensity(Left ? 1800 : 0); BlueRight->SetIntensity(Left ? 0 : 1800);
	// 100 ms Queue-Obergrenze. Fehlendes AudioDevice laesst keine Queue wachsen.
	if (SirenWave && SirenWave->GetAvailableAudioByteCount() < 4800)
	{
		TArray<int16> Samples; Samples.SetNumUninitialized(2400);
		for (int16& Sample : Samples)
		{
			const double Frequency = FMath::Fmod(SirenTime, .8) < .4 ? 435.0 : 580.0;
			SirenPhase = FMath::Fmod(SirenPhase + Frequency / 24000.0, 1.0);
			Sample = static_cast<int16>(FMath::Sin(SirenPhase * 2.0 * PI) * 7000.0);
			SirenTime += 1.0 / 24000.0;
		}
		SirenWave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
	}
}
void AWiesbadenPoliceCar::EndPlay(const EEndPlayReason::Type Reason)
{
	WiesbadenAudioPropagation::UnregisterSource(Siren);
	if (Siren) { Siren->Stop(); }
	Super::EndPlay(Reason);
}
float AWiesbadenPoliceCar::TakeDamage(float Damage, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	const float Applied = Super::TakeDamage(Damage, Event, EventInstigator, Causer);
	if (Damage > 0 && CrimeCooldown <= 0 && EventInstigator && EventInstigator->IsPlayerController())
	{
		if (UWiesbadenCitySubsystem* City = GetWorld()->GetSubsystem<UWiesbadenCitySubsystem>())
		{ City->ReportCrime(EWiesbadenCrimeEvent::OfficerHit); CrimeCooldown = 1.0f; }
	}
	return Applied;
}
