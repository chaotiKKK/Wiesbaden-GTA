// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Weapons/WiesbadenWeaponComponent.h"

#include "WiesbadenReal.h"

#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
	/** Engine-Grundkoerper. Der Wuerfel ist 100 cm, der Zylinder 100 cm hoch. */
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	/** Zwei Bytes je Abtastwert, mono. */
	constexpr int32 BytesPerSample = 2;
}

UWiesbadenWeaponComponent::UWiesbadenWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;   // Der Pawn treibt TickWeapon.
}

UStaticMeshComponent* UWiesbadenWeaponComponent::AddPart(
	const TCHAR* Name, const TCHAR* MeshPath,
	const FVector& PartLocation, const FVector& Scale,
	const FRotator& Rotation, UMaterialInterface* Material)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
	if (!Mesh || !GetOwner())
	{
		return nullptr;
	}

	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(GetOwner(), Name);
	if (!Part)
	{
		return nullptr;
	}

	Part->SetStaticMesh(Mesh);
	Part->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	Part->SetRelativeLocation(PartLocation);
	Part->SetRelativeScale3D(Scale);
	Part->SetRelativeRotation(Rotation);

	// Die Waffe wird getragen, nicht angestossen: Kollision waere nur im Weg
	// und wuerde den Traeger an jeder Hauswand haengen lassen.
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCastShadow(true);

	if (Material)
	{
		Part->SetMaterial(0, Material);
	}

	Part->RegisterComponent();
	Parts.Add(Part);
	return Part;
}

void UWiesbadenWeaponComponent::BuildWeaponMesh()
{
	// Masse einer Maschinenpistole, in Zentimetern:
	//
	//   Gesamtlaenge etwa 60 cm, Gehaeuse 26 cm, Lauf 20 cm, Magazin 18 cm.
	//
	// Der Engine-Wuerfel ist 100 cm gross, der Zylinder 100 cm hoch bei 100 cm
	// Durchmesser - die Skalierungen unten sind deshalb schlicht die Masse in
	// Metern. Vorne ist +X, oben +Z, rechts +Y.
	//
	// Hier stand vorher EIN flacher Quader. Der war als Platzhalter markiert,
	// sah aber auch nach nichts aus, an dem man eine Blickrichtung ablesen
	// koennte.
	UMaterialInterface* Metal = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Materials/City/M_WbPole.M_WbPole"));

	// Gehaeuse mit Verschluss.
	AddPart(TEXT("WeaponReceiver"), CubePath,
		FVector(4.0, 0.0, 0.0), FVector(0.26, 0.055, 0.075), FRotator::ZeroRotator, Metal);

	// Lauf.
	AddPart(TEXT("WeaponBarrel"), CylinderPath,
		FVector(26.0, 0.0, 1.2), FVector(0.022, 0.022, 0.20),
		FRotator(90.0f, 0.0f, 0.0f), Metal);

	// Muendungsbremse - etwas dicker als der Lauf, damit die Spitze ablesbar ist.
	AddPart(TEXT("WeaponMuzzleBrake"), CylinderPath,
		FVector(37.0, 0.0, 1.2), FVector(0.034, 0.034, 0.05),
		FRotator(90.0f, 0.0f, 0.0f), Metal);

	// Magazin, leicht nach vorn geneigt wie bei einer MP.
	AddPart(TEXT("WeaponMagazine"), CubePath,
		FVector(2.0, 0.0, -11.0), FVector(0.10, 0.035, 0.18),
		FRotator(-8.0f, 0.0f, 0.0f), Metal);

	// Pistolengriff.
	AddPart(TEXT("WeaponGrip"), CubePath,
		FVector(-6.0, 0.0, -10.0), FVector(0.055, 0.045, 0.16),
		FRotator(-14.0f, 0.0f, 0.0f), Metal);

	// Schulterstuetze.
	AddPart(TEXT("WeaponStock"), CubePath,
		FVector(-16.0, 0.0, 0.5), FVector(0.16, 0.035, 0.055), FRotator::ZeroRotator, Metal);

	// Visier oben auf dem Gehaeuse.
	AddPart(TEXT("WeaponSight"), CubePath,
		FVector(6.0, 0.0, 5.5), FVector(0.09, 0.025, 0.030), FRotator::ZeroRotator, Metal);

	// Muendungspunkt: Ursprung der Leuchtspuren und Sitz des Muendungsfeuers.
	if (GetOwner())
	{
		Muzzle = NewObject<USceneComponent>(GetOwner(), TEXT("WeaponMuzzle"));
		if (Muzzle)
		{
			Muzzle->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
			Muzzle->SetRelativeLocation(FVector(40.0, 0.0, 1.2));
			Muzzle->RegisterComponent();
		}

		MuzzleLight = NewObject<UPointLightComponent>(GetOwner(), TEXT("WeaponMuzzleLight"));
		if (MuzzleLight)
		{
			MuzzleLight->AttachToComponent(
				Muzzle ? Muzzle : static_cast<USceneComponent*>(this),
				FAttachmentTransformRules::KeepRelativeTransform);
			MuzzleLight->SetIntensity(0.0f);
			MuzzleLight->SetAttenuationRadius(900.0f);
			MuzzleLight->SetLightColor(FLinearColor(1.0f, 0.78f, 0.42f));
			MuzzleLight->SetCastShadows(false);
			MuzzleLight->RegisterComponent();
		}
	}
}

void UWiesbadenWeaponComponent::SetupAudio()
{
	if (!GetOwner())
	{
		return;
	}

	ShotAudio = NewObject<UAudioComponent>(GetOwner(), TEXT("WeaponShotAudio"));
	if (!ShotAudio)
	{
		return;
	}

	ShotAudio->AttachToComponent(
		Muzzle ? Muzzle : static_cast<USceneComponent*>(this),
		FAttachmentTransformRules::KeepRelativeTransform);
	ShotAudio->bAllowSpatialization = true;
	ShotAudio->bAutoActivate = false;
	ShotAudio->RegisterComponent();

	// Dasselbe Verfahren wie beim Motor: eine laufende prozedurale Welle, in
	// die Abtastwerte geschoben werden. Sie spielt dauerhaft und ist still,
	// solange nichts eingereiht ist - dadurch klingt der Schuss ohne
	// Anlaufverzoegerung, die ein Neustart der Quelle mit sich braechte.
	ShotWave = NewObject<USoundWaveProcedural>(GetOwner(), TEXT("WeaponShotProceduralSound"));
	if (!ShotWave)
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Waffe: prozedurale Klangquelle konnte nicht erzeugt werden."));
		return;
	}

	ShotWave->NumChannels = 1;
	ShotWave->SetSampleRate(FMath::Max(GunshotParams.SampleRate, 8000));
	ShotWave->SampleByteSize = BytesPerSample;

	ShotAudio->SetSound(ShotWave);
	ShotAudio->Play();

	UE_LOG(LogWbVehicles, Log,
		TEXT("Waffe: prozeduraler Schussklang aktiviert (%d Hz, mono, %.0f ms je Schuss)."),
		GunshotParams.SampleRate, GunshotParams.DurationSeconds * 1000.0f);
}

void UWiesbadenWeaponComponent::SetupWeapon()
{
	if (bSetupDone)
	{
		return;
	}
	bSetupDone = true;

	RestLocation = GetRelativeLocation();

	BuildWeaponMesh();
	SetupAudio();
}

FVector UWiesbadenWeaponComponent::GetMuzzleLocation() const
{
	return Muzzle ? Muzzle->GetComponentLocation() : GetComponentLocation();
}

void UWiesbadenWeaponComponent::PlayGunshot()
{
	if (!ShotWave)
	{
		return;
	}

	// Jeder Schuss bekommt einen eigenen Seed - zwei Schuesse hintereinander
	// klingen sonst identisch, und das faellt bei Dauerfeuer sofort auf.
	TArray<int16> Samples;
	FWiesbadenGunshotSynth::RenderShot(GunshotParams, ShotCounter++, Samples);

	if (Samples.Num() == 0)
	{
		return;
	}

	ShotWave->QueueAudio(
		reinterpret_cast<const uint8*>(Samples.GetData()),
		Samples.Num() * BytesPerSample);
}

void UWiesbadenWeaponComponent::Fire(const FVector& AimStart, const FVector& AimDirection)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Streuung: ohne sie landet jeder Schuss auf demselben Punkt, was nach
	// Laserpointer aussieht statt nach Waffe.
	FVector Direction = AimDirection.GetSafeNormal();
	if (SpreadDegrees > 0.0f)
	{
		Direction = FMath::VRandCone(Direction, FMath::DegreesToRadians(SpreadDegrees));
	}

	const FVector End = AimStart + Direction * (RangeMeters * 100.0f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbWeapon), /*bTraceComplex=*/true);
	Params.AddIgnoredActor(GetOwner());

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, AimStart, End, ECC_Visibility, Params);
	const FVector ImpactPoint = bHit ? Hit.ImpactPoint : End;

	// Die Leuchtspur startet am LAUF, nicht an der Kamera: Sonst entstuende
	// sie sichtbar im Gesicht des Spielers.
	FWiesbadenTracer Tracer;
	Tracer.Start = GetMuzzleLocation();
	Tracer.End = ImpactPoint;
	Tracer.Alpha = 0.0f;

	const float DistanceCm = FMath::Max(FVector::Dist(Tracer.Start, Tracer.End), 1.0f);
	Tracer.Speed = (MuzzleVelocityMetersPerS * 100.0f) / DistanceCm;
	Tracers.Add(Tracer);

	// Muendungsfeuer und Klang.
	MuzzleFlashRemaining = MuzzleFlashSeconds;
	if (MuzzleLight)
	{
		MuzzleLight->SetIntensity(MuzzleFlashIntensity);
	}
	RecoilOffset = -FMath::Abs(RecoilOffsetCm);

	PlayGunshot();

	if (bHit)
	{
		// Einschlag sichtbar machen.
		DrawDebugPoint(World, Hit.ImpactPoint, 9.0f, FColor(255, 200, 90), false, 1.2f);
	}
}

float UWiesbadenWeaponComponent::AdvanceTracerAlpha(float Alpha, float Speed, float DeltaSeconds)
{
	return FMath::Clamp(Alpha + FMath::Max(Speed, 0.0f) * FMath::Max(DeltaSeconds, 0.0f), 0.0f, 1.0f);
}

void UWiesbadenWeaponComponent::TickWeapon(float DeltaSeconds)
{
	UWorld* World = GetWorld();

	// Muendungsfeuer abklingen lassen.
	if (MuzzleFlashRemaining > 0.0f)
	{
		MuzzleFlashRemaining -= DeltaSeconds;
		if (MuzzleFlashRemaining <= 0.0f && MuzzleLight)
		{
			MuzzleLight->SetIntensity(0.0f);
		}
	}

	// Rueckstoss zurueckfedern.
	if (!FMath::IsNearlyZero(RecoilOffset))
	{
		RecoilOffset = FMath::FInterpTo(RecoilOffset, 0.0f, DeltaSeconds, RecoilRecoveryRate);
		SetRelativeLocation(RestLocation + FVector(RecoilOffset, 0.0, 0.0));
	}

	if (!World)
	{
		return;
	}

	// Leuchtspuren weiterfliegen lassen. Gezeichnet wird ein kurzes Stueck
	// entlang der Bahn, kein Punkt - ein Punkt waere bei 380 m/s nie sichtbar.
	constexpr float TracerLengthFraction = 0.06f;

	for (int32 Index = Tracers.Num() - 1; Index >= 0; --Index)
	{
		FWiesbadenTracer& Tracer = Tracers[Index];

		const float Previous = Tracer.Alpha;
		Tracer.Alpha = AdvanceTracerAlpha(Tracer.Alpha, Tracer.Speed, DeltaSeconds);

		const float TailAlpha = FMath::Max(Previous - TracerLengthFraction, 0.0f);
		const FVector Head = FMath::Lerp(Tracer.Start, Tracer.End, Tracer.Alpha);
		const FVector Tail = FMath::Lerp(Tracer.Start, Tracer.End, TailAlpha);

		DrawDebugLine(World, Tail, Head, FColor(255, 214, 120), false, 0.0f, 0, 2.5f);

		if (Tracer.Alpha >= 1.0f)
		{
			Tracers.RemoveAtSwap(Index);
		}
	}
}
