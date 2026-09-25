// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Weapons/WiesbadenWeaponComponent.h"

#include "WiesbadenReal.h"

#include "Weapons/WiesbadenDamageTarget.h"
#include "Weapons/WiesbadenWeaponSpec.h"
#include "GameFramework/Actor.h"
#include "World/WiesbadenCitySubsystem.h"

#include "Audio/WiesbadenAudioPropagation.h"
#include "Audio/WiesbadenAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "CollisionQueryParams.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
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

	// In den Effekt-Bus (SFX) des Mischpults einordnen - so laeuft der Schuss
	// ueber Master-Lautstaerke und Ducking wie die Fahrzeugklaenge, nicht am
	// Mischpult vorbei. nullptr, falls die Mix-Assets fehlen -> dann ohne Bus.
	ShotAudio->SoundClassOverride = UWiesbadenAudioSubsystem::LoadBusSoundClass(EWbAudioBus::SFX);

	// Ausbreitung: mittlere Distanzkurve (Schuss) inkl. Occlusion + Hall-Send.
	WiesbadenAudioPropagation::ConfigureSource(ShotAudio, EWbAudioRange::Mid);

	// Echte Aufnahme statt Synth (Nutzerwunsch 2026-09): je Waffe ein Sample
	// aus /Game/Audio/Samples. Das Sample wird je Schuss neu gestartet -
	// der Sample-Pfad ist damit die erste Wahl, die Synth-Welle bleibt
	// Rueckfall, wenn die Assets fehlen (z. B. im CI-Lauf).
	if (USoundBase* Sample = LoadObject<USoundBase>(nullptr,
		TEXT("/Game/Audio/Samples/A_ShotBerettaM12.A_ShotBerettaM12")))
	{
		ShotSample = Sample;
		ShotAudio->SetSound(ShotSample);
		ShotAudio->bAutoActivate = false;
		UE_LOG(LogWbVehicles, Log, TEXT("Waffe: Schuss-Sample 'Beretta M12' aktiviert (Synth als Rueckfall)."));
		return;   // keine prozedurale Welle noetig
	}

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

bool UWiesbadenWeaponComponent::IsValidWeaponIndex(int32 Index)
{
	return Index >= 0 && Index < WiesbadenWeapons::Table().Num();
}

void UWiesbadenWeaponComponent::SetWeaponIndex(int32 InIndex)
{
	if (!IsValidWeaponIndex(InIndex) || InIndex == WeaponIndex)
	{
		return;
	}
	WeaponIndex = InIndex;

	// Fliegende Schuesse der alten Waffe ausklingen lassen: ein MG-Feuerstoss
	// gehoert zum MG, nicht zur naechstgewaehlten Pistole.
	LiveShots.Reset();
}

void UWiesbadenWeaponComponent::PlayGunshot()
{
	// Sample-Pfad: Schussklang je Waffe (Beretta fuer die schnellen Kaliber,
	// Rifle fuer den Scharfschuetzen), Explosion beim Granatwerfer. Ein
	// leicht zufaelliger Pitch-Versatz (0,97..1,03) nimmt den Schuessen
	// die Gleichfoermigkeit, die zwei identische Samples sofort verraten.
	if (ShotSample && ShotAudio)
	{
		const int32 WeaponSlot = FMath::Clamp(WeaponIndex, 0,
			static_cast<int32>(EWiesbadenWeaponId::Count) - 1);
		const TCHAR* Path = TEXT("/Game/Audio/Samples/A_ShotBerettaM12.A_ShotBerettaM12");
		if (WeaponSlot == static_cast<int32>(EWiesbadenWeaponId::Scharfschuetze))
		{
			Path = TEXT("/Game/Audio/Samples/A_ShotRifle.A_ShotRifle");
		}
		else if (WeaponSlot == static_cast<int32>(EWiesbadenWeaponId::Granatwerfer))
		{
			Path = TEXT("/Game/Audio/Samples/A_Explosion.A_Explosion");
		}
		if (USoundBase* Wanted = LoadObject<USoundBase>(nullptr, Path))
		{
			ShotSample = Wanted;
			ShotAudio->SetSound(ShotSample);
		}
		ShotAudio->SetPitchMultiplier(FMath::RandRange(0.97f, 1.03f));
		ShotAudio->Play(0.0f);
		return;
	}

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

	const FWiesbadenWeaponSpec& Spec = WiesbadenWeapons::Spec(WeaponIndex);
	if (Spec.bMelee)
	{
		// Nahkampf: Schwung-Sweep folgt (Adapter-Test gegen Reach); hier nur
		// Rueckkopplung - ein sichtbarer Schwung ist Schritt "Ego-Modus".
		RecoilOffset = -FMath::Abs(RecoilOffsetCm) * 2.0f;
		PlayGunshot();
		return;
	}

	// Streuung: ohne sie landet jeder Schuss auf demselben Punkt, was nach
	// Laserpointer aussieht statt nach Waffe.
	const FVector Up = FVector::UpVector;
	const FVector Right = FVector::CrossProduct(Up, AimDirection.GetSafeNormal()).GetSafeNormal();
	const int32 Pellets = FMath::Max(Spec.PelletsPerShot, 1);
	for (int32 Pellet = 0; Pellet < Pellets; ++Pellet)
	{
		FVector Direction = AimDirection.GetSafeNormal();
		if (SpreadDegrees > 0.0f)
		{
			Direction = FMath::VRandCone(Direction, FMath::DegreesToRadians(SpreadDegrees));
		}

		// Projektil mit echter Flugbahn: Start an der Muendung, Zielrichtung
		// aus dem Blick - die Flugzeit ist sichtbar (Leuchtspur folgt).
		FWiesbadenProjectile P;
		P.Position = GetMuzzleLocation();
		P.Velocity = Direction * Spec.MuzzleVelocityCmPerS;
		P.RemainingRangeCm = Spec.RangeCm;
		P.MassKg = Spec.ProjectileMassKg;
		P.GravityCmPerS2 = Spec.GravityCmPerS2;
		P.Damage = Spec.Damage;
		P.bExplosive = Spec.bExplosive;
		P.BlastRadiusCm = Spec.BlastRadiusCm;
		P.BlastDamage = Spec.BlastDamage;
		P.SelfDamage = Spec.SelfDamage;

		FWiesbadenTracer Tracer;
		Tracer.Start = P.Position;
		Tracer.End = P.Position + Direction * (Spec.RangeCm);
		Tracer.Alpha = 0.0f;
		Tracer.Speed = Spec.MuzzleVelocityCmPerS / FMath::Max(Spec.RangeCm, 1.0f);
		Tracers.Add(Tracer);

		FWiesbadenLiveShot Live;
		Live.Projectile = P;
		Live.TracerIndex = Tracers.Num() - 1;
		LiveShots.Add(Live);
	}

	// Muendungsfeuer und Klang.
	MuzzleFlashRemaining = MuzzleFlashSeconds;
	if (MuzzleLight)
	{
		MuzzleLight->SetIntensity(MuzzleFlashIntensity);
	}
	RecoilOffset = -FMath::Abs(RecoilOffsetCm);

	PlayGunshot();
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

	// Projektile zuerst: ihre Aufschlage bestimmen den Leuchtspur-Endpunkt.
	StepProjectiles(DeltaSeconds);

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

void UWiesbadenWeaponComponent::StepProjectiles(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		LiveShots.Reset();
		return;
	}

	for (int32 Index = LiveShots.Num() - 1; Index >= 0; --Index)
	{
		const FWiesbadenLiveShot& Live = LiveShots[Index];
		const FWiesbadenFlightStep StepResult = WiesbadenBallistics::Step(Live.Projectile, DeltaSeconds);

		// Segment-Sweep: Welt zuerst (Complex-Trace gegen alles ausser dem
		// Schuetzen), dann Ziel-Adapter per Ueberschneidung mit dem Segment.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WbProjectile), /*bTraceComplex=*/true);
		Params.AddIgnoredActor(GetOwner());

		FHitResult WorldHit;
		const bool bWorldHit = World->LineTraceSingleByChannel(WorldHit,
			StepResult.SegmentStart, StepResult.SegmentEnd, ECC_Visibility, Params);

		const FVector SweepEnd = bWorldHit ? WorldHit.Location : StepResult.SegmentEnd;

		// Ziel-Adapter: Objekte nahe am Segment finden. Ein Kugel-Overlap am
		// Segmentende wuerde schnelle Ziele verpassen; darum Segment als Reihe
		// von Kugeln abtasten (Schritt = Zieldurchmesser-Schaetzung 60 cm).
		TArray<FOverlapResult> Overlaps;
		const double SegmentLength = static_cast<double>(FVector::Dist(StepResult.SegmentStart, SweepEnd));
		const int32 Samples = FMath::Clamp(FMath::CeilToInt(SegmentLength / 60.0), 1, 24);
		bool bTargetHit = false;
		AActor* TargetActor = nullptr;
		UPrimitiveComponent* TargetComponent = nullptr;
		FVector TargetPoint = FVector::ZeroVector;
		for (int32 Sample = 0; Sample <= Samples && !bTargetHit; ++Sample)
		{
			const FVector Probe = FMath::Lerp(StepResult.SegmentStart, SweepEnd,
				static_cast<float>(Sample) / Samples);
			Overlaps.Reset();
			if (!World->OverlapMultiByObjectType(Overlaps, Probe, FQuat::Identity,
				FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(45.0f), Params))
			{
				continue;
			}
			for (const FOverlapResult& Overlap : Overlaps)
			{
				AActor* Actor = Overlap.GetActor();
				if (!Actor || Actor == GetOwner())
				{
					continue;
				}
				if (Actor->Implements<UWiesbadenDamageTarget>())
				{
					TargetActor = Actor;
					TargetComponent = Overlap.Component.Get();
					TargetPoint = Probe;
					bTargetHit = true;
					break;
				}
			}
		}

		if (bTargetHit)
		{
			ResolveImpact(TargetActor, TargetComponent, TargetPoint, Live.Projectile);
			LiveShots.RemoveAtSwap(Index);
			continue;
		}

		if (bWorldHit)
		{
			// Welt-Aufschlag: Explosivgeschosse detonieren, andere schlagen ein.
			ResolveImpact(nullptr, nullptr, WorldHit.Location, Live.Projectile);
			LiveShots.RemoveAtSwap(Index);
			continue;
		}

		if (StepResult.bRangeEnd)
		{
			// Reichweitenende ohne Treffer: verlogen leise ausblenden.
			if (Live.Projectile.bExplosive)
			{
				ApplyExplosionAt(StepResult.SegmentEnd, Live.Projectile);
			}
			LiveShots.RemoveAtSwap(Index);
			continue;
		}

		// Weiterfliegen; die Leuchtspur reitet auf dem Projektil: Kopf = neue
		// Position, sichtbares Stueck = das Bewegungssegment dieses Bilds (bei
		// Alpha=1 und Speed=0 bleibt der Schwanz am Segmentanfang haengen).
		LiveShots[Index].Projectile = StepResult.Projectile;
		if (LiveShots[Index].TracerIndex >= 0 && LiveShots[Index].TracerIndex < Tracers.Num())
		{
			FWiesbadenTracer& Tracer = Tracers[LiveShots[Index].TracerIndex];
			Tracer.Start = StepResult.SegmentStart;
			Tracer.End = StepResult.SegmentEnd;
			Tracer.Alpha = 1.0f;
			Tracer.Speed = 0.0f;
		}
	}
}void UWiesbadenWeaponComponent::ResolveImpact(AActor* HitActor, UPrimitiveComponent* HitComponent,
	const FVector& ImpactPoint, const FWiesbadenProjectile& P)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (P.bExplosive)
	{
		ApplyExplosionAt(ImpactPoint, P);
		return;
	}

	if (HitActor && HitActor->Implements<UWiesbadenDamageTarget>())
	{
		IWiesbadenDamageTarget* Target = Cast<IWiesbadenDamageTarget>(HitActor);
		if (Target && Target->IsAlive())
		{
			const FVector Direction = (ImpactPoint - P.Position).GetSafeNormal();
			Target->ApplyProjectileHit(P, ImpactPoint, Direction);

			// Impuls: nur gegen Physik-Koerper sinnvoll; Primitive wirft den
			// Impuls selbst weg, wenn keine Simulation laeuft. Impuls =
			// Masse * Geschwindigkeit (UE-Einheit kg*cm/s).
			if (HitComponent && HitComponent->IsAnyRigidBodyAwake())
			{
				HitComponent->AddImpulseAtLocation(
					Direction * P.Velocity.Size() * P.MassKg, ImpactPoint);
			}
			ReportPedestrianAndWanted(World, ImpactPoint, P.Damage);
		}
	}

	// Einschlag sichtbar machen (wie bisher, jetzt am echten Punkt).
	DrawDebugPoint(World, ImpactPoint, 9.0f, FColor(255, 200, 90), false, 1.2f);
}

void UWiesbadenWeaponComponent::ReportPedestrianAndWanted(UWorld* World,
	const FVector& ImpactPoint, float Damage)
{
	// Passanten sind Instanzen OHNE Kollision (PedestrianSpawnerComponent) -
	// kein Sweep findet sie. Der Adapter fragt die Simulation direkt: im
	// Umkreis des Aufschlags zu Boden schicken und die Tat ins Konto buchen.
	if (UWiesbadenCitySubsystem* City = World->GetSubsystem<UWiesbadenCitySubsystem>())
	{
		// Treffergenausigkeit ueber Schadenshoehe steuern: starke Munition
		// trifft den Passanten auch knapp daneben (Kugelradius skaliert).
		const double StrikeRadius = FMath::Clamp(60.0 + Damage, 70.0, 150.0);
		const int32 Felled = City->PedestrianSimulation.StrikeNear(
			ImpactPoint, StrikeRadius, /*DownForSeconds=*/12.0f);
		if (Felled > 0)
		{
			// Mehrere Figuren auf einmal = schwerer Trefferklang.
			City->PlayPedestrianHitSound(ImpactPoint, Felled > 1);
		}
		for (int32 HitIndex = 0; HitIndex < Felled; ++HitIndex)
		{
			City->ReportCrime(EWiesbadenCrimeEvent::PedestrianDowned);
		}
	}
}

void UWiesbadenWeaponComponent::ApplyExplosionAt(const FVector& Centre, const FWiesbadenProjectile& P)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Ziele im Radius ueber Overlap sammeln; Schaden faellt linear zum Rand.
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbBlast), false);
	Params.AddIgnoredActor(GetOwner());
	if (World->OverlapMultiByObjectType(Overlaps, Centre, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(P.BlastRadiusCm), Params))
	{
		TSet<AActor*> Visited;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* Actor = Overlap.GetActor();
			if (!Actor || Actor == GetOwner() || Visited.Contains(Actor)
				|| !Actor->Implements<UWiesbadenDamageTarget>())
			{
				continue;
			}
			Visited.Add(Actor);
			if (IWiesbadenDamageTarget* Target = Cast<IWiesbadenDamageTarget>(Actor))
			{
				if (Target->IsAlive())
				{
					const float Damage = WiesbadenBallistics::BlastDamageAt(
						Centre, P.BlastRadiusCm, P.BlastDamage, Actor->GetActorLocation());
					Target->ApplyExplosion(Damage, Centre, P.BlastRadiusCm);
				}
			}
		}
	}

	// Eigenschaden: der Schuetze nimmt die VOLLLE Blast-Punkte (Entwurf).
	if (AActor* Owner = GetOwner())
	{
		if (Owner->Implements<UWiesbadenDamageTarget>())
		{
		const float Self = WiesbadenBallistics::BlastDamageAt(
			Centre, P.BlastRadiusCm, P.SelfDamage > KINDA_SMALL_NUMBER ? P.SelfDamage : P.BlastDamage,
			Owner->GetActorLocation());
			if (Self > 0.0f)
			{
				if (IWiesbadenDamageTarget* SelfTarget = Cast<IWiesbadenDamageTarget>(Owner))
				{
					SelfTarget->ApplyExplosion(Self, Centre, P.BlastRadiusCm);
				}
			}
		}
	}

	// Sicht- und Hoer-Rueckkopplung: heller Blitz am Punkt, Explosions-
	// Aufnahme raeumlich am Ort (auch fuer NPC-Schuetzen hoerbar).
	DrawDebugPoint(World, Centre, 18.0f, FColor(255, 120, 40), false, 0.6f);
	DrawDebugSphere(World, Centre, P.BlastRadiusCm, 12, FColor(255, 140, 60), false, 0.5f, 0, 2.0f);
	if (USoundBase* Boom = LoadObject<USoundBase>(nullptr,
		TEXT("/Game/Audio/Samples/A_Explosion.A_Explosion")))
	{
		UGameplayStatics::SpawnSoundAtLocation(World, Boom, Centre);
	}

	// Explosion trifft Passanten IMMER radial (der Blast reisst mit), und
	// jede erfasste Figur ist eine Tat ins Konto.
	if (UWiesbadenCitySubsystem* City = World->GetSubsystem<UWiesbadenCitySubsystem>())
	{
		const int32 Felled = City->PedestrianSimulation.StrikeNear(
			Centre, P.BlastRadiusCm, /*DownForSeconds=*/12.0f);
		if (Felled > 0)
		{
			City->PlayPedestrianHitSound(Centre, /*bHeavy=*/true);
		}
		for (int32 HitIndex = 0; HitIndex < Felled; ++HitIndex)
		{
			City->ReportCrime(EWiesbadenCrimeEvent::PedestrianDowned);
		}
	}
}
