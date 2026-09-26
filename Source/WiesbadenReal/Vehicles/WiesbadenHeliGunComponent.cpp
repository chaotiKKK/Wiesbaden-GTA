// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenHeliGunComponent.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbHeliGun, Log, All);

namespace
{
	/** Schwenkgrenzen des Turms in Grad. */
	constexpr float TurmGierungMax = 60.0f;
	constexpr float TurmNickungMin = -25.0f;
	constexpr float TurmNickungMax = 25.0f;

	/** Wie schnell der Turm der Zielrichtung folgt (1/s). */
	constexpr float TurmFolgt = 3.2f;

	/**
	 * Sitz des Geschuetzes in Modell-Koordinaten (cm).
	 *
	 * Die Ka-52 hat die 30-mm-Kanone rechts auf dem vorderen Pylon. Aus der
	 * Vermessung des importierten FBX (Tools/ka52_fbxlage.py): der Rumpf
	 * laeuft von Modell-Y = -580 (Nase) bis +826 (Heck), der Fluegelansatz
	 * liegt um Modell-Y = 0, die Rumpfoberkante bei Z = 295. Der Pylon
	 * sitzt damit knapp vor der Mitte und rechts (Modell-X = +).
	 */
	const FVector GeschuetzSitz(360.0f, -230.0f, 95.0f);
}

UWiesbadenHeliGunComponent::UWiesbadenHeliGunComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Modellraum -> Gier -> Nick -> Rohr: das Geschuetz schwenkt in zwei
	// Schritten, damit die Nickung nicht um die Rumpfachse wandert.
	ModelSpace = CreateDefaultSubobject<USceneComponent>(TEXT("Geraeteraum"));
	ModelSpace->SetupAttachment(this);
	ModelSpace->SetRelativeLocation(GeschuetzSitz);

	TurretYaw = CreateDefaultSubobject<USceneComponent>(TEXT("TurmGierung"));
	TurretYaw->SetupAttachment(ModelSpace);
	// Ruhelage der Kanone: das importierte Rohr zeigt in Modell-+X, der
	// Hubschrauber faehrt aber nach Modell--Y. Ohne diese Drehung zeigte
	// die Kanone im Ruhezustand nach Steuerbord statt nach vorn - ein
	// Schuss waere seitlich weg, noch bevor man gezielt hat.
	TurretYaw->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	TurretPitch = CreateDefaultSubobject<USceneComponent>(TEXT("TurmNickung"));
	TurretPitch->SetupAttachment(TurretYaw);

	Barrel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rohr"));
	Barrel->SetupAttachment(TurretPitch);
	Barrel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Barrel->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GunMesh(
		TEXT("/Game/Vehicles/Ka52/SM_Ka52GunTurret.SM_Ka52GunTurret"));
	if (GunMesh.Succeeded())
	{
		Barrel->SetStaticMesh(GunMesh.Object);
	}
	else
	{
		// Ohne Mesh bleibt die Kanone unsichtbar, feuert aber. Das ist der
		// Rueckfall, kein Ziel: das Mesh wird aus Blender importiert.
		UE_LOG(LogWbHeliGun, Warning,
			TEXT("Ka52-Geschuetzmesh fehlt - feuert unsichtbar."));
	}

	// Modellraum: Massstab und Gierdrehung kommen vom Pawn, der Sitz ist
	// hier festgeschrieben (Modellkoordinaten, siehe GeschuetzSitz).

	MuzzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("Muendung"));
	MuzzlePoint->SetupAttachment(TurretPitch);
	// Rohrmundung: 210 cm vor dem Drehpunkt, 18 cm ueber ihm - das Rohr
	// sitzt im Asset auf Local-Z 18 (Blender-Skript), die Mündung stand
	// mit (210, 0, 0) 18 cm daneben und der Mündungsfeuer lief daneben
	// statt aus dem Lauf.
	MuzzlePoint->SetRelativeLocation(FVector(210.0f, 0.0f, 18.0f));

	MuzzleFlash = CreateDefaultSubobject<UPointLightComponent>(TEXT("Muendungsfeuer"));
	MuzzleFlash->SetupAttachment(MuzzlePoint);
	MuzzleFlash->SetLightColor(FLinearColor(1.0f, 0.85f, 0.55f));
	MuzzleFlash->SetIntensity(0.0f);
	MuzzleFlash->SetAttenuationRadius(3600.0f);
	MuzzleFlash->SetCastShadows(false);
	MuzzleFlash->SetVisibility(false);

	// Schussklang der 2A42 aus Tools/make_ka52_audio.py -> S_Ka52_MG.
	static ConstructorHelpers::FObjectFinder<USoundBase> GunSound(
		TEXT("/Game/Audio/Ka52/S_Ka52_MG.S_Ka52_MG"));
	if (GunSound.Succeeded())
	{
		FireSound = GunSound.Object;
	}
	else
	{
		UE_LOG(LogWbHeliGun, Warning,
			TEXT("Ka52-Schussklang fehlt - das Bordgeschuetz feuert lautlos."));
	}
}

void UWiesbadenHeliGunComponent::SetModelTransform(float InScale, const FRotator& InYaw)
{
	if (!ModelSpace)
	{
		return;
	}
	ModelSpace->SetRelativeScale3D(FVector(InScale));
	ModelSpace->SetRelativeRotation(InYaw);
}

void UWiesbadenHeliGunComponent::BeginPlay()
{
	Super::BeginPlay();
	RoundsLeft = Magazine;
	UE_LOG(LogWbHeliGun, Log,
		TEXT("Ka52-Bordgeschuetz bereit: %d Schuss, %.0f/min, Rohr sitzt auf (%.0f, %.0f, %.0f)."),
		RoundsLeft, RoundsPerMinute, GeschuetzSitz.X, GeschuetzSitz.Y, GeschuetzSitz.Z);
}

void UWiesbadenHeliGunComponent::Reload()
{
	RoundsLeft = Magazine;
	Heat = 0.0f;
	UE_LOG(LogWbHeliGun, Log, TEXT("Ka52-Geschuetz nachgeladen: %d Schuss."), RoundsLeft);
}

FVector UWiesbadenHeliGunComponent::GetMuzzleLocation() const
{
	return MuzzlePoint ? MuzzlePoint->GetComponentLocation() : GetComponentLocation();
}

FRotator UWiesbadenHeliGunComponent::GetAimRotation() const
{
	// Die Rohrachse ist die lokale X-Achse; Recoil ist der Rueckstoss, der
	// die Mündung nach oben schiebt und mit abgezogen wird.
	return MuzzlePoint
		? MuzzlePoint->GetComponentRotation()
		: GetComponentRotation();
}

void UWiesbadenHeliGunComponent::SetTriggerHeld(bool bPressed)
{
	bTriggerHeld = bPressed;
}

void UWiesbadenHeliGunComponent::Aim(float Horizontal, float Vertical)
{
	WantedYaw = FMath::Clamp(Horizontal, -1.0f, 1.0f) * TurmGierungMax;
	WantedPitch = FMath::Clamp(Vertical, -1.0f, 1.0f)
		* (TurmNickungMax - TurmNickungMin) + TurmNickungMin;
}

void UWiesbadenHeliGunComponent::AimAt(const FVector& Weltziel)
{
	const FVector Von = TurretYaw ? TurretYaw->GetComponentLocation() : GetComponentLocation();
	const FVector Z = Weltziel - Von;
	if (Z.SizeSquared() < 1.0f)
	{
		return;
	}
	// Bezug ist die lokale Achse des Turms: +X vorn, +Y rechts, +Z oben.
	// Der Winkel zwischen der lokalen Vorwaertsachse und dem Ziel wird in
	// Grad gerechnet, damit die Grenzen aus Aim() gelten.
	const FVector Vorn = TurretYaw ? TurretYaw->GetForwardVector() : FVector::ForwardVector;
	const FVector Rechts = TurretYaw ? TurretYaw->GetRightVector() : FVector::RightVector;
	const FVector Oben = TurretYaw ? TurretYaw->GetUpVector() : FVector::UpVector;

	const float Laenge = Z.Size();
	const FVector R = Z / Laenge;
	const float Nickung = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(R, Oben), -1.0f, 1.0f)));
	const float Vorwaerts = FVector::DotProduct(R, Vorn);
	const float Seitlich = FVector::DotProduct(R, Rechts);
	const float Gierung = FMath::RadiansToDegrees(FMath::Atan2(Seitlich, FMath::Max(Vorwaerts, 0.01f)));

	Aim(Gierung / TurmGierungMax, (Nickung - TurmNickungMin)
		/ (TurmNickungMax - TurmNickungMin));
}

void UWiesbadenHeliGunComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// -- Abkuehlen ----------------------------------------------------------
	// Waerme bleibt auch stehen: ein durchgeheiztes Rohr braucht seine
	// Sperre, sonst feuert man endlos durch.
	Heat = FMath::Max(0.0f, Heat - CoolingPerSecond * DeltaTime);

	// -- Turm fuehren -------------------------------------------------------
	TurretYawDeg = FMath::FInterpTo(TurretYawDeg, WantedYaw, DeltaTime, TurmFolgt);
	TurretPitchDeg = FMath::FInterpTo(TurretPitchDeg, WantedPitch, DeltaTime, TurmFolgt);

	// -- Rueckstoss abklingen lassen ---------------------------------------
	// Der Rueckstoss schiebt die Mündung nach oben und federt zurueck. Ohne
	// das Federn schlaegt das Rohr dauerhaft durch.
	Recoil = FMath::FInterpTo(Recoil, 0.0f, DeltaTime, RecoilReturn);

	if (TurretYaw && TurretPitch)
	{
		TurretYaw->SetRelativeRotation(FRotator(0.0f, TurretYawDeg, 0.0f));
		TurretPitch->SetRelativeRotation(FRotator(TurretPitchDeg + Recoil, 0.0f, 0.0f));
	}

	// -- Muendungsfeuer ausklingen -----------------------------------------
	if (FlashTimer > 0.0f)
	{
		FlashTimer -= DeltaTime;
		if (MuzzleFlash)
		{
			const float Rest = FMath::Max(0.0f, FlashTimer / 0.055f);
			MuzzleFlash->SetIntensity(Rest * 90000.0f);
			MuzzleFlash->SetVisibility(FlashTimer > 0.0f);
		}
	}
	else if (MuzzleFlash)
	{
		MuzzleFlash->SetIntensity(0.0f);
		MuzzleFlash->SetVisibility(false);
	}

	// -- Feuern -------------------------------------------------------------
	if (!bTriggerHeld || RoundsLeft <= 0 || Heat >= OverheatAt)
	{
		return;
	}

	// Salve statt Einzelimpuls: bei 500/min kommt alle 0,12 s ein Schuss.
	FireAccumulator += DeltaTime;
	const float SekundenJeSchuss = 60.0f / FMath::Max(1.0f, RoundsPerMinute);
	int32 Schuesse = 0;
	while (FireAccumulator >= SekundenJeSchuss && Schuesse < 8)
	{
		FireAccumulator -= SekundenJeSchuss;
		--RoundsLeft;
		++ShotsFired;
		// Jeder zehnte Schuss schreibt eine Zeitmarke. Das ist der Beleg
		// dafuer, DASS gefeuert wurde und WANN: das Geschaeft schweigt
		// sonst vollstaendig, und das Muendungsfeuer ist nur 55 ms von je
		// 120 ms an - ein Bild kann es nur per Zufall treffen (gemessen am
		// 26.09.2026: 39 s Abzug, keine einzige Zeile).
		if (ShotsFired % 10 == 0)
		{
			UE_LOG(LogWbHeliGun, Log,
				TEXT("Ka52-Geschuetz: Schuss %d, Rest %d, Waerme %.0f%%."),
				ShotsFired, RoundsLeft, Heat / FMath::Max(1.0f, OverheatAt) * 100.0f);
		}
		Heat = FMath::Min(OverheatAt * 1.05f, Heat + HeatPerShot);
		Recoil += RecoilPitch;
		FlashTimer = 0.055f;
		if (MuzzleFlash)
		{
			MuzzleFlash->SetIntensity(90000.0f);
			MuzzleFlash->SetVisibility(true);
		}
		FeuereSchuss();
		++Schuesse;

		if (Heat >= OverheatAt)
		{
			UE_LOG(LogWbHeliGun, Warning,
				TEXT("Ka52-Geschuetz ueberhitzt bei %d Schuss - gesperrt."), ShotsFired);
			break;
		}
		if (RoundsLeft <= 0)
		{
			UE_LOG(LogWbHeliGun, Warning, TEXT("Ka52-Munition leer."));
			break;
		}
	}
}

void UWiesbadenHeliGunComponent::FeuereSchuss()
{
	const FVector Start = GetMuzzleLocation();
	const FRotator Basis = GetAimRotation();

	// Streuung: ein Strahl waere unmoeglich zu treffen. Die Streuung ist
	// bewusst klein (0,65 Grad) - sie sichtbar zu machen ist Sache der
	// Fluggeschwindigkeit, nicht des Waffenwerts.
	const FRotator Spread = FRotator(
		FMath::FRandRange(-ConeHalfAngleDeg, ConeHalfAngleDeg),
		FMath::FRandRange(-ConeHalfAngleDeg, ConeHalfAngleDeg),
		0.0f);
	const FRotator Richtung = (Basis + Spread).GetNormalized();
	const FVector Ende = Start + Richtung.Vector() * TraceRange;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbHeliGun), true, GetOwner());
	Params.bReturnPhysicalMaterial = false;

	FHitResult Treffer;
	const bool bGetroffen = GetWorld()->LineTraceSingleByChannel(
		Treffer, Start, Ende, ECC_Visibility, Params);

	const FVector TatsaechlichesEnde = bGetroffen ? Treffer.ImpactPoint : Ende;

	// Schaden: an allem, was einen Schadensbehandler traegt. Fahrzeuge und
	// Fussgaenger im Projekt nutzen die uebliche Punkt-Schadensfunktion.
	if (bGetroffen)
	{
		// Die Signatur verlangt einen VEKTOR als Trefferrichtung, und
		// "Richtung" ist hier eine FRotator (die muendungsrichtung). Also
		// .Vector() - sonst meldet der Compiler eine Argumentliste, die
		// kuerzer ist als die Vorlage (gemessen, nicht geraten).
		UGameplayStatics::ApplyPointDamage(
			Treffer.GetActor(), Damage, Richtung.Vector(), Treffer, nullptr,
			GetOwner(), UDamageType::StaticClass());
	}

	// Tracer: ein dünner Strahl von der Mündung zum Einschlag. Ohne ihn
	// sieht man aus 300 m nicht, wohin geschossen wurde.
	DrawDebugLine(GetWorld(), Start, TatsaechlichesEnde,
		bGetroffen ? FColor(255, 150, 40) : FColor(255, 220, 120),
		false, 0.045f, 0, 1.6f);

	// Ton an der Mündung, damit er nicht aus dem Rumpfmittelpunkt kommt.
	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Start);
	}
}
