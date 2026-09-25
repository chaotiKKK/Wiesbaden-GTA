// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenFootPawn.h"

#include "Vehicles/WiesbadenHelicopter.h"   // ApplyStickShaping: eine Kennlinie fuer alle Sticks
#include "Weapons/WiesbadenWeaponComponent.h"
#include "Weapons/WiesbadenWeaponSpec.h"

#include "WiesbadenReal.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Vehicles/WiesbadenCarAudioComponent.h"
#include "World/WiesbadenVisualTuning.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Components/SpotLightComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Vehicles/WiesbadenCarLightsComponent.h"
#include "World/WiesbadenCitySubsystem.h"
#include "Materials/MaterialInterface.h"

namespace
{
	constexpr float KmhToCmPerS = 100000.0f / 3600.0f;
}

AWiesbadenFootPawn::AWiesbadenFootPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(40.0f, 90.0f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	SetRootComponent(Capsule);

	// Verfolgerkamera wie beim Fahrzeug - so bleibt der Wechsel zwischen zu
	// Fuss und am Steuer optisch ruhig.
	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(Capsule);
	CameraArm->TargetArmLength = 300.0f;
	CameraArm->bUsePawnControlRotation = false;
	CameraArm->bDoCollisionTest = true;
	CameraArm->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm);
	// Explizites Bildfeld statt Engine-Default 90 - wie die Fahrzeugkamera
	// (World/WiesbadenVisualTuning.h), damit der Wechsel zu Fuss optisch ruhig bleibt.
	Camera->SetFieldOfView(WiesbadenVisualTuning::FootFieldOfView);

	// Sichtbarer Koerper. Die Meshes selbst kollidieren nicht - dafuer ist die
	// Kapsel da; zwei Kollisionskoerper wuerden sich gegenseitig blockieren.
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Capsule);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
	HeadMesh->SetupAttachment(Capsule);
	HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Die animierte Figur. Sie kollidiert nicht - dafuer ist die Kapsel da.
	FigureMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FigureMesh"));
	FigureMesh->SetupAttachment(Capsule);
	FigureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Kettensaegen-Klang: derselbe Synthesizer wie der Fahrzeugmotor, nur mit
	// Zweitakter-Drehzahlen. Sitzt an der Saege, vorn rechts.
	SawAudio = CreateDefaultSubobject<UWiesbadenCarAudioComponent>(TEXT("SawAudio"));
	SawAudio->SetupAttachment(Capsule);
	SawAudio->SetRelativeLocation(FVector(35.0f, 15.0f, 30.0f));

	// Die Waffe haengt an der Kapsel, vorn rechts auf Hufthoehe: So ist im
	// Bild ablesbar, wohin gezielt wird, ohne dass sie die Sicht verstellt.
	//
	// Der Versatz ist groesser als beim Strichmaennchen von vorher. Sebbo ist
	// ein Fotoscan mit echten Huften und Oberschenkeln; auf (18, 14) steckte
	// der Lauf im Hosenbein.
	Weapon = CreateDefaultSubobject<UWiesbadenWeaponComponent>(TEXT("Weapon"));
	Weapon->SetupAttachment(Capsule);
	Weapon->SetRelativeLocation(FVector(30.0f, 26.0f, 2.0f));

	// Handlampe: sitzt am Kapselkopf und leuchtet in Blickrichtung.
	Torch = CreateDefaultSubobject<USpotLightComponent>(TEXT("Torch"));
	Torch->SetupAttachment(Capsule);
	Torch->SetRelativeLocation(FVector(20.0f, 12.0f, 60.0f));
	Torch->SetIntensity(0.0f);
	Torch->SetVisibility(false);
	Torch->SetCastShadows(false);
	Torch->SetLightColor(FLinearColor(1.0f, 0.96f, 0.88f));
}

void AWiesbadenFootPawn::BeginPlay()
{
	Super::BeginPlay();

	// Erst hier, nicht im Konstruktor: die Materialien liegen als Assets vor
	// und sind zur Konstruktionszeit des CDO noch nicht sicher ladbar.
	BuildBody();

	// Dasselbe gilt fuer die Waffe: Modell aus Grundkoerpern, Muendungslicht
	// und prozeduraler Schussklang brauchen eine laufende Welt.
	if (Weapon)
	{
		Weapon->SetupWeapon();
	}
}

void AWiesbadenFootPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	// Blickrichtung ueber die Pfeiltasten - identisch zur Fahrzeugkamera.
	// Umschauen ueber die MAUS - Pfeiltasten bleiben als Ersatz.
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	PC->GetInputMouseDelta(MouseX, MouseY);

	// Umschauen: Maus, Pfeiltasten UND rechter Stick.
	//
	// Gamepad-Belegung (uebliche Shooter-Belegung):
	//
	//   Linker Stick      Gehen
	//   Rechter Stick     Umschauen
	//   Rechter Trigger   Schiessen
	//   Linker Stick      Druecken = Sprint (ersatzweise A)
	//   B                 Fackel
	//
	// Der rechte Stick bekommt dieselbe Totzone und Expo-Kennlinie wie der
	// Hubschrauber: ohne Totzone dreht sich die Figur dauerhaft, weil kein
	// Stick exakt mittig ruht.
	const float LookX = AWiesbadenHelicopter::ApplyStickShaping(
		PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX), GamepadDeadzone, GamepadLookExpo);
	const float LookY = AWiesbadenHelicopter::ApplyStickShaping(
		PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY), GamepadDeadzone, GamepadLookExpo);

	const float Turn = LookSpeedDegPerS * DeltaSeconds;
	FRotator Rotation = GetActorRotation();
	Rotation.Yaw += MouseX * MouseSensitivity;
	Rotation.Yaw += LookX * GamepadLookSpeedDegPerS * DeltaSeconds;
	if (PC->IsInputKeyDown(EKeys::Left))  { Rotation.Yaw -= Turn; }
	if (PC->IsInputKeyDown(EKeys::Right)) { Rotation.Yaw += Turn; }
	SetActorRotation(FRotator(0.0f, Rotation.Yaw, 0.0f));

	if (CameraArm)
	{
		FRotator ArmRotation = CameraArm->GetRelativeRotation();
		ArmRotation.Pitch += MouseY * MouseSensitivity;
		ArmRotation.Pitch += LookY * GamepadLookSpeedDegPerS * DeltaSeconds;
		if (PC->IsInputKeyDown(EKeys::Up))   { ArmRotation.Pitch += Turn; }
		if (PC->IsInputKeyDown(EKeys::Down)) { ArmRotation.Pitch -= Turn; }
		ArmRotation.Pitch = FMath::Clamp(ArmRotation.Pitch, -70.0f, 30.0f);
		CameraArm->SetRelativeRotation(ArmRotation);
	}

	// Waehrend der Bahnfahrt: Umschauen ja, alles andere macht der Wagen.
	if (bRiding)
	{
		UpdateFigure(DeltaSeconds, 0.0f);
		return;
	}

	// Bewegung in Blickrichtung.
	FVector Move = FVector::ZeroVector;
	if (PC->IsInputKeyDown(EKeys::W)) { Move += GetActorForwardVector(); }
	if (PC->IsInputKeyDown(EKeys::S)) { Move -= GetActorForwardVector(); }
	if (PC->IsInputKeyDown(EKeys::D)) { Move += GetActorRightVector(); }
	if (PC->IsInputKeyDown(EKeys::A)) { Move -= GetActorRightVector(); }

	// Linker Stick: analog, damit Schleichen und Gehen moeglich sind.
	const float MoveX = AWiesbadenHelicopter::ApplyStickShaping(
		PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX), GamepadDeadzone, 0.0f);
	const float MoveY = AWiesbadenHelicopter::ApplyStickShaping(
		PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY), GamepadDeadzone, 0.0f);
	Move += GetActorForwardVector() * MoveY;
	Move += GetActorRightVector() * MoveX;

	if (!Move.IsNearlyZero())
	{
		// A am Gamepad ist SPRINGEN, nicht mehr Rennen: das ist die uebliche
		// Belegung, und beide auf derselben Taste hiesse, dass jeder Sprung
		// zugleich einen Sprint ausloest.
		const bool bSprint = PC->IsInputKeyDown(EKeys::LeftShift)
			|| PC->IsInputKeyDown(EKeys::Gamepad_LeftThumbstick);
		const float SpeedCmPerS = (bSprint ? SprintSpeedKmh : WalkSpeedKmh) * KmhToCmPerS;
		const FVector Wanted = Move.GetSafeNormal() * SpeedCmPerS * DeltaSeconds;

		// An Hindernissen entlanggleiten statt stehenzubleiben.
		//
		// Ohne das blockiert JEDE Beruehrung die gesamte Bewegung - man bleibt
		// an einer Hauswand kleben, die man nur streift. Dieselbe Behandlung
		// wie beim Fahrzeug.
		FHitResult MoveHit;
		AddActorWorldOffset(Wanted, /*bSweep=*/true, &MoveHit);

		if (MoveHit.bBlockingHit && MoveHit.Normal.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			// Erst versuchen HINAUFZUSTEIGEN, dann entlanggleiten.
			//
			// Ohne diesen Versuch kommt man nicht auf den Gehweg: der
			// Bordstein ist 12 cm hoch, die Kapsel schiebt sich seitlich
			// dagegen und gleitet an ihm ENTLANG. Von aussen sieht es aus,
			// als schwebe der Gehweg unerreichbar ueber dem Boden.
			//
			// Eine senkrechte Wand liefert eine waagerechte Normale; genau
			// die trifft auch auf eine Hauswand zu. Unterschieden wird
			// deshalb nicht an der Normale, sondern daran, ob oberhalb der
			// Stufe Platz ist - eine Hauswand ist dort weiterhin belegt.
			const bool bSteppedUp = TryStepUp(Wanted, MoveHit);

			if (!bSteppedUp)
			{
				const FVector Remaining = Wanted * (1.0f - MoveHit.Time);
				const FVector Slide = FVector::VectorPlaneProject(Remaining, MoveHit.Normal);
				if (!Slide.IsNearlyZero())
				{
					AddActorWorldOffset(Slide, /*bSweep=*/true);
				}
			}
		}
	}

	// Schiessen: Strg oder Enter. Flankenerkennung, damit ein gehaltener
	// Finger nicht jeden Frame feuert; die Feuerrate begrenzt zusaetzlich.
	FireCooldownSeconds = FMath::Max(0.0f, FireCooldownSeconds - DeltaSeconds);

	// Handlampe nach Sonnenstand - dieselbe Schwelle wie die Lichtautomatik
	// des Fahrzeugs, damit beide zum selben Zeitpunkt schalten.
	if (Torch)
	{
		bool bWantTorch = false;
		if (const UWorld* PawnWorld = GetWorld())
		{
			if (const UWiesbadenCitySubsystem* City = PawnWorld->GetSubsystem<UWiesbadenCitySubsystem>())
			{
				bWantTorch = UWiesbadenCarLightsComponent::ShouldUseHeadlights(
					City->GetWeatherState().SunElevationFactor());
			}
		}

		Torch->SetVisibility(bWantTorch);
		Torch->SetIntensity(bWantTorch ? TorchIntensity : 0.0f);
		Torch->SetOuterConeAngle(TorchOuterConeAngle);
		Torch->SetInnerConeAngle(TorchOuterConeAngle * 0.4f);
		Torch->SetAttenuationRadius(TorchRangeCm);

		// In Blickrichtung ausrichten, unabhaengig von der Kapseldrehung.
		if (Camera)
		{
			Torch->SetWorldRotation(Camera->GetComponentRotation());
		}
	}

	// Springen auf die Leertaste. Flanke, damit Halten nicht dauerspringt,
	// und nur vom Boden aus - kein zweiter Sprung in der Luft.
	const bool bJumpDown = PC->IsInputKeyDown(EKeys::SpaceBar)
		|| PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom);
	if (bJumpDown && !bJumpKeyHeld && !bAirborne)
	{
		VerticalSpeedCmS = JumpSpeedCmS;
		bAirborne = true;
	}
	bJumpKeyHeld = bJumpDown;

	// Angriff auf die linke Maustaste - Strg und Enter bleiben als Ersatz.
	const bool bFireDown = PC->IsInputKeyDown(EKeys::LeftMouseButton)
		|| PC->IsInputKeyDown(EKeys::LeftControl)
		|| PC->IsInputKeyDown(EKeys::Enter)
		|| PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) > 0.35f;

	// Ansicht und Waffenwahl vor dem Feuern abfragen: ein Druck auf C oder
	// eine Ziffer gilt im selben Bild schon fuer die neue Lage.
	PollWeaponKeys(PC);
	const bool bEgoDown = PC->IsInputKeyDown(EKeys::C);
	if (bEgoDown && !bEgoKeyHeld)
	{
		ToggleEgoCamera();
	}
	bEgoKeyHeld = bEgoDown;

	if (bUsesChainsaw)
	{
		// Kettensaege: EIN Hieb je Tastendruckphase, kein Dauerfeuer. Der
		// naechste beginnt erst, wenn der laufende durchgeschwungen ist.
		if (bFireDown && SwingRemaining <= 0.0f)
		{
			StartSwing();
		}

		if (SwingRemaining > 0.0f)
		{
			const float ElapsedBefore = SwingSeconds - SwingRemaining;
			SwingRemaining = FMath::Max(0.0f, SwingRemaining - DeltaSeconds);
			const float ElapsedAfter = SwingSeconds - SwingRemaining;

			// Der Treffer sitzt im Durchzug, nicht beim Tastendruck: die
			// Saege braucht die 0,37 s vom Ausholen bis zur Bahnmitte.
			if (!bMeleeHitDone
				&& ElapsedBefore < SwingHitAtSeconds
				&& ElapsedAfter >= SwingHitAtSeconds)
			{
				DoMeleeHit();
			}
		}
	}
	else if (bFireDown && FireCooldownSeconds <= 0.0f)
	{
		FireWeapon();
		FireCooldownSeconds = FireIntervalSeconds;
	}
	bFireKeyHeld = bFireDown;

	if (Weapon && !bUsesChainsaw)
	{
		Weapon->TickWeapon(DeltaSeconds);
	}

	FollowGround(DeltaSeconds);

	// Gemessenes Tempo aus der tatsaechlichen Ortsaenderung - NICHT aus der
	// Eingabe. Wer gegen eine Hauswand laeuft, steht; die Fuesse sollen dann
	// nicht weiterlaufen wie auf Glatteis.
	const FVector Location = GetActorLocation();
	float SpeedMps = 0.0f;
	if (DeltaSeconds > KINDA_SMALL_NUMBER && !PreviousLocation.IsNearlyZero())
	{
		SpeedMps = FVector::Dist2D(Location, PreviousLocation) / (DeltaSeconds * 100.0f);
	}
	PreviousLocation = Location;

	UpdateFigure(DeltaSeconds, SpeedMps);
}

void AWiesbadenFootPawn::BuildBody()
{
	// Die Spielerfigur ist "Sebbo mit Kettensaege" - ein Fotoscan.
	//
	// Erste Wahl ist die ANIMIERTE Fassung: Skelett mit neun Knochen und drei
	// Bewegungen (Tools/Blender/rig_sebbo.py). Mit ihr geht Sebbo im
	// Schrittzyklus und schwingt die Kettensaege im Nahkampf; die Pistole
	// entfaellt, weil beide Haende an der Saege sind.
	if (USkeletalMesh* Skeletal = LoadObject<USkeletalMesh>(
		nullptr, TEXT("/Game/Assets/People/SK_Sebbo.SK_Sebbo")))
	{
		// Der FBX-Importer benennt Bewegungen als
		// <Zielname><Armaturname>_<Aktionsname> - fuer Aufraeumarbeiten am
		// Namen lohnt kein eigener Editorlauf, der Pfad steht eben so da.
		auto LoadAnim = [](const TCHAR* Name) -> UAnimSequence*
		{
			const FString Primary = FString::Printf(
				TEXT("/Game/Assets/People/SK_SebboSebboRig_%s.SK_SebboSebboRig_%s"), Name, Name);
			if (UAnimSequence* Found = LoadObject<UAnimSequence>(nullptr, *Primary))
			{
				return Found;
			}
			const FString Plain = FString::Printf(
				TEXT("/Game/Assets/People/%s.%s"), Name, Name);
			return LoadObject<UAnimSequence>(nullptr, *Plain);
		};

		IdleAnim = LoadAnim(TEXT("Sebbo_Idle"));
		WalkAnim = LoadAnim(TEXT("Sebbo_Walk"));
		SwingAnim = LoadAnim(TEXT("Sebbo_Swing"));

		// Nur mit allen drei Bewegungen lohnt der Umstieg: ein Skelett, das
		// reglos in T-Haltung ueber die Strasse gleitet, waere schlechter als
		// das statische Modell.
		if (FigureMesh && IdleAnim && WalkAnim && SwingAnim)
		{
			FigureMesh->SetSkeletalMesh(Skeletal);
			FigureMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -88.0f));
			FigureMesh->PlayAnimation(IdleAnim, true);
			CurrentLoop = 1;

			if (BodyMesh) { BodyMesh->SetVisibility(false); }
			if (HeadMesh) { HeadMesh->SetVisibility(false); }

			// Die Kettensaege ist jetzt WAFFENSLOT 9 (Taste 9), nicht mehr
			// Dauerzustand: Start ist die MP aus der Waffentabelle, die Saege
			// (und ihr Zweitakter-Klang) kommt mit der Taste 9 zurueck.
			bUsesChainsaw = false;

			UE_LOG(LogWbVehicles, Log,
				TEXT("Spielerfigur: SK_Sebbo animiert (Gehen + Schwung); Startwaffe MP, Saege auf Taste 9."));
			return;
		}

		UE_LOG(LogWbVehicles, Warning,
			TEXT("Spielerfigur: SK_Sebbo ohne vollstaendige Bewegungen (Idle %d, Walk %d, Swing %d) - statisches Modell."),
			IdleAnim != nullptr, WalkAnim != nullptr, SwingAnim != nullptr);
	}

	// Rueckfall: das statische Modell (kein Skelett importiert).
	//
	// Hier stand ein Zylinder als Rumpf und eine Kugel als Kopf, dann das
	// gemeinsame Fussgaengermodell mit 900 Dreiecken. Jetzt traegt der Spieler
	// ein eigenes Gesicht: Tools/Blender/build_sebbo.py schweisst den Scan
	// zusammen, setzt ihm Beine an den GEMESSENEN Huftquerschnitt und bemalt
	// sie mit der Hosenfarbe, die im Scan selbst steht.
	//
	// Faellt der Scan aus, bleibt das Fussgaengermodell als Ersatz - eine
	// unsichtbare Spielfigur waere schlimmer als eine schlichte.
	UStaticMesh* Person = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Assets/People/SM_Sebbo.SM_Sebbo"));

	const bool bIsSebbo = Person != nullptr;
	if (!Person)
	{
		Person = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Assets/People/SM_WbPerson.SM_WbPerson"));
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Spielerfigur: SM_Sebbo nicht ladbar - es bleibt beim Fussgaengermodell."));
	}

	UMaterialInterface* Skin = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Materials/City/M_WbPedestrian.M_WbPedestrian"));

	if (BodyMesh && Person)
	{
		BodyMesh->SetStaticMesh(Person);
		BodyMesh->SetRelativeScale3D(FVector::OneVector);

		// Der Ursprung des Modells liegt zwischen den Fuessen, die Kapsel wird
		// mit ihrem MITTELPUNKT gesetzt: Die Figur muss deshalb um die halbe
		// Kapselhoehe nach unten. Ohne diesen Versatz steckte sie bis zur
		// Huefte im Asphalt - derselbe Fehler wie beim Kaefer.
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -88.0f));

		// Sebbo bringt seine eigenen drei Materialien mit (Scan, Hose,
		// Stiefel). Sie mit der Fussgaengerfarbe zu ueberschreiben, machte
		// gerade den Fotoscan zunichte, um dessentwillen er hier steht.
		if (Skin && !bIsSebbo) { BodyMesh->SetMaterial(0, Skin); }
	}
	else if (BodyMesh)
	{
		// Rueckfall auf die Grundkoerper, falls das Modell fehlt. Ohne diesen
		// Zweig waere der Spieler unsichtbar statt nur schlicht.
		UStaticMesh* Cylinder = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		if (Cylinder)
		{
			BodyMesh->SetStaticMesh(Cylinder);
			BodyMesh->SetRelativeScale3D(FVector(0.38f, 0.38f, 0.9f));
			BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -35.0f));
			if (Skin) { BodyMesh->SetMaterial(0, Skin); }
		}

		UE_LOG(LogWbVehicles, Warning,
			TEXT("Spielerfigur: kein Personenmodell ladbar - es bleibt beim Zylinder."));
	}

	// Der Kopf steckt im Modell; die separate Kugel entfaellt.
	if (HeadMesh)
	{
		HeadMesh->SetVisibility(false);
	}

	// Die Waffe baut sich selbst auf (UWiesbadenWeaponComponent). Hier stand
	// zuvor ein einzelner flacher Quader.
}

void AWiesbadenFootPawn::FireWeapon()
{
	if (!Weapon)
	{
		return;
	}

	// Aus der KAMERA zielen, nicht aus dem Lauf: Der Spieler zielt mit dem
	// Blick, und ein Schuss aus der Hueftposition traefe sichtbar daneben.
	// Die Leuchtspur startet trotzdem am Lauf - das erledigt die Waffe selbst.
	FVector Start = GetActorLocation() + FVector(0.0, 0.0, 60.0);
	FVector Direction = GetActorForwardVector();

	if (Camera)
	{
		Start = Camera->GetComponentLocation();
		Direction = Camera->GetForwardVector();
	}

	Weapon->Fire(Start, Direction);
}

void AWiesbadenFootPawn::ToggleEgoCamera()
{
	bEgoCamera = !bEgoCamera;
	ApplyCameraMode();
}

void AWiesbadenFootPawn::ApplyCameraMode()
{
	if (!CameraArm || !Camera)
	{
		return;
	}

	if (bEgoCamera)
	{
		// Erste Person: Kamera auf Augenhoehe, leicht rechts (Schulter-Feel),
		// Arm gestaucht. Der Arm folgt weiterhin der Maus (Pitch oben).
		CameraArm->TargetArmLength = EgoArmLengthCm;
		CameraArm->SetRelativeLocation(FVector(0.0f, EgoShoulderOffsetCm, 60.0f));

		// Eigene Figur ausblenden (nur fuer diesen Spieler; Schatten bleiben,
		// damit man in der Ego-Ansicht nicht sichtbar schwebt).
		if (BodyMesh) { BodyMesh->SetOwnerNoSee(true); }
		if (HeadMesh) { HeadMesh->SetOwnerNoSee(true); }
		if (FigureMesh) { FigureMesh->SetOwnerNoSee(true); }

		// Waffe an die Kamera: vorn rechts unterhalb des Blicks, leicht
		// einwaerts gedreht - die uebliche Ego-Waffenlage. Die Teile sind
		// StaticMeshComponents am eigenen Actor: OwnerNoSee versteckt sie
		// fuer den Traeger NICHT, darum bleibt die Waffe sichtbar geschaltet
		// und haengt nah genug, um im Bild zu bleiben.
		if (Weapon)
		{
			Weapon->AttachToComponent(Camera,
				FAttachmentTransformRules::KeepRelativeTransform);
			Weapon->SetRelativeLocation(FVector(22.0f, 14.0f, -16.0f));
			Weapon->SetRelativeRotation(FRotator(0.0f, -4.0f, 0.0f));
		}
	}
	else
	{
		// Schulterkamera: die bekannte Verfolgerlage zurueck.
		CameraArm->TargetArmLength = 300.0f;
		CameraArm->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));

		if (BodyMesh) { BodyMesh->SetOwnerNoSee(false); }
		if (HeadMesh) { HeadMesh->SetOwnerNoSee(false); }
		if (FigureMesh) { FigureMesh->SetOwnerNoSee(false); }

		if (Weapon)
		{
			Weapon->AttachToComponent(Capsule,
				FAttachmentTransformRules::KeepRelativeTransform);
			Weapon->SetRelativeLocation(FVector(30.0f, 26.0f, 2.0f));
			Weapon->SetRelativeRotation(FRotator::ZeroRotator);
		}
	}
}

void AWiesbadenFootPawn::PollWeaponKeys(const APlayerController* PC)
{
	if (!PC || !Weapon)
	{
		return;
	}

	// Tasten 1-9 auf die Tabelle (Pistole .. Kettensaege). Flankenerkennung
	// je Taste, damit Halten nicht springt.
	static const FKey Keys[9] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };

	for (int32 Index = 0; Index < 9; ++Index)
	{
		const bool bDown = PC->IsInputKeyDown(Keys[Index]);
		if (bDown && !WeaponKeyHeld[Index] && UWiesbadenWeaponComponent::IsValidWeaponIndex(Index))
		{
			SelectWeapon(Index);
		}
		WeaponKeyHeld[Index] = bDown;
	}
}

void AWiesbadenFootPawn::SelectWeapon(int32 Index)
{
	if (!Weapon || !UWiesbadenWeaponComponent::IsValidWeaponIndex(Index))
	{
		return;
	}
	if (Index == Weapon->WeaponIndex)
	{
		return;
	}

	Weapon->SetWeaponIndex(Index);

	// Feuerrate des Pawns an die neue Waffe.
	const FWiesbadenWeaponSpec& Spec = WiesbadenWeapons::Spec(Index);
	FireCooldownSeconds = FMath::Max(FireCooldownSeconds, Spec.ShotIntervalSeconds());

	// Die Kettensaege (Slot 9) schwingt die Figur und tuckert; jede andere
		// Waffe zeigt die Waffenkomponente und feuert Projektile. Ein laufender
		// Hieb gehoert zur Saege und wird beim Wechsel abgebrochen.
	const bool bSaw = Index == static_cast<int32>(EWiesbadenWeaponId::Kettensaege)
		&& SwingAnim != nullptr;
	bUsesChainsaw = bSaw;
	SwingRemaining = 0.0f;
	bMeleeHitDone = false;

	if (Weapon)
	{
		Weapon->SetVisibility(!bSaw, true);
	}
	if (SawAudio)
	{
		SawAudio->SetEngineRunning(bSaw);
	}

	// Waffenlage neu anwenden (Ego/Schulter bleibt erhalten).
	ApplyCameraMode();

	UE_LOG(LogWbVehicles, Log, TEXT("FootPawn: Waffe %d (%s) gewaehlt."),
		Index, Spec.DisplayName);
}

void AWiesbadenFootPawn::FollowGround(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Start = GetActorLocation() + FVector(0.0, 0.0, 200.0);
	const FVector End = Start - FVector(0.0, 0.0, 100000.0);

	// WorldStatic, nicht Visibility: Fahrbahn, Gehweg und Gelaende sind
	// statische Weltgeometrie. Die Kanaele stimmen hier zwar ueberein, aber die
	// Bodenabfrage soll denselben Kanal benutzen wie die Kollision, auf der
	// gelaufen wird.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbFootGround), true);
	Params.AddIgnoredActor(this);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return;
	}

	// Halbe Kapselhoehe PLUS ein kleiner Abstand, damit die Fuesse auf dem
	// Boden stehen und nicht darin.
	//
	// Hier stand die 90 als feste Zahl - genau die halbe Kapselhoehe. Die
	// Unterkante der Kapsel lag damit exakt auf der Flaeche, und ein
	// gesweepter Schritt meldete den BODEN als Hindernis: Die Figur liess sich
	// nicht mehr von der Stelle bewegen. Seit die Fahrbahnen eigene Kollision
	// haben, trat das auf jeder Strasse auf.
	//
	// Die Hoehe kommt jetzt aus der Kapsel selbst, nicht aus einer zweiten
	// Kopie der Zahl; der Abstand entspricht der Bodenfreiheit, die auch
	// Unreals CharacterMovement einhaelt.
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	const float DesiredZ = Hit.Location.Z + HalfHeight + FootFloorClearanceCm;
	const float CurrentZ = GetActorLocation().Z;

	// -- Sprung und Fall ------------------------------------------------------
	//
	// Solange die Figur steigt oder ueber dem Boden ist, gilt die Physik und
	// NICHT das Nachziehen an die Oberflaeche. Ohne diese Trennung zoege die
	// Bodenverfolgung die Figur im selben Bild wieder herunter, in dem der
	// Sprung sie angehoben hat - man saehe nichts als ein Zittern.
	if (bAirborne)
	{
		VerticalSpeedCmS -= GravityCmPerS2 * DeltaSeconds;
		const float NextZ = CurrentZ + VerticalSpeedCmS * DeltaSeconds;

		if (VerticalSpeedCmS <= 0.0f && NextZ <= DesiredZ)
		{
			// Aufgekommen.
			AddActorWorldOffset(FVector(0.0f, 0.0f, DesiredZ - CurrentZ), /*bSweep=*/false);
			VerticalSpeedCmS = 0.0f;
			bAirborne = false;
		}
		else
		{
			AddActorWorldOffset(FVector(0.0f, 0.0f, NextZ - CurrentZ), /*bSweep=*/true);
		}
		return;
	}

	// Ohne Boden unter den Fuessen faellt die Figur, statt in der Luft zu
	// stehen - etwa nach einem Schritt ueber eine Mauerkante.
	if (CurrentZ - DesiredZ > FallThresholdCm)
	{
		bAirborne = true;
		VerticalSpeedCmS = 0.0f;
		return;
	}

	if (!FMath::IsNearlyEqual(CurrentZ, DesiredZ, 1.0f))
	{
		const float Blend = FMath::Clamp(DeltaSeconds * 10.0f, 0.0f, 1.0f);
		AddActorWorldOffset(FVector(0.0f, 0.0f, (DesiredZ - CurrentZ) * Blend), /*bSweep=*/false);
	}
}

void AWiesbadenFootPawn::StartSwing()
{
	SwingRemaining = SwingSeconds;
	bMeleeHitDone = false;

	if (FigureMesh && SwingAnim)
	{
		// Einmalig, keine Schleife. CurrentLoop auf 0, damit UpdateFigure
		// nach dem Hieb die passende Dauerschleife NEU startet - sonst
		// bliebe die Figur im letzten Bild des Hiebs stehen.
		FigureMesh->PlayAnimation(SwingAnim, false);
		FigureMesh->SetPlayRate(SwingAnim->GetPlayLength() / FMath::Max(SwingSeconds, 0.1f));
		CurrentLoop = 0;
	}
}

bool AWiesbadenFootPawn::TryStepUp(const FVector& Wanted, const FHitResult& Blocked)
{
	UWorld* World = GetWorld();
	if (!World || Wanted.IsNearlyZero())
	{
		return false;
	}

	// Nur an aufrechten Hindernissen versuchen. Eine flache Rampe blockiert
	// nicht, und eine Decke ueber dem Kopf ist keine Stufe.
	if (FMath::Abs(Blocked.Normal.Z) > 0.5f)
	{
		return false;
	}

	const FVector Start = GetActorLocation();
	const FVector Lift(0.0f, 0.0f, MaxStepHeightCm);

	// Die Probe ist ein VOLLSTAENDIGER Weg: anheben, vorwaerts, absetzen.
	// Nur wenn alle drei Teilstuecke frei sind, ist es eine Stufe und keine
	// Wand - bei einer Hauswand scheitert schon das Vorwaertsstueck.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbFootStep), false);
	Params.AddIgnoredActor(this);

	const FCollisionShape Shape = Capsule
		? FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),
			Capsule->GetScaledCapsuleHalfHeight())
		: FCollisionShape::MakeCapsule(40.0f, 90.0f);

	FHitResult Probe;
	const FVector Raised = Start + Lift;
	if (World->SweepSingleByChannel(Probe, Start, Raised, FQuat::Identity,
		ECC_Pawn, Shape, Params))
	{
		return false;
	}

	const FVector Ahead = Raised + Wanted.GetSafeNormal() * (Wanted.Size() + StepForwardProbeCm);
	if (World->SweepSingleByChannel(Probe, Raised, Ahead, FQuat::Identity,
		ECC_Pawn, Shape, Params))
	{
		return false;
	}

	// Wieder absetzen. Findet sich unterhalb kein Boden innerhalb der
	// Stufenhoehe, war es eine Kante ins Nichts - dann NICHT hinaufsteigen,
	// sonst schwebt die Figur.
	const FVector Down = Ahead - Lift - FVector(0.0f, 0.0f, 2.0f);
	if (!World->SweepSingleByChannel(Probe, Ahead, Down, FQuat::Identity,
		ECC_Pawn, Shape, Params))
	{
		return false;
	}

	SetActorLocation(Probe.Location, /*bSweep=*/false);
	return true;
}

void AWiesbadenFootPawn::DoMeleeHit()
{
	bMeleeHitDone = true;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Kugel-Sweep quer vor der Figur - die Saege zieht von rechts nach
	// links durch, also deckt eine Kugel auf halber Reichweite die Bahn ab.
	const FVector Forward = GetActorForwardVector();
	const FVector Start = GetActorLocation() + FVector(0.0f, 0.0f, 20.0f);
	const FVector End = Start + Forward * MeleeRangeCm;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbMelee), false);
	Params.AddIgnoredActor(this);

	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(
		Hits, Start, End, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeSphere(MeleeRadiusCm), Params);

	int32 Struck = 0;
	for (const FHitResult& Hit : Hits)
	{
		AActor* Victim = Hit.GetActor();
		if (!Victim || Victim == this)
		{
			continue;
		}

		// Physikkoerper bekommen den Schwung der Saege mit - ein geparktes
		// Chaos-Fahrzeug ruckt sichtbar zur Seite.
		if (UPrimitiveComponent* Prim = Hit.GetComponent())
		{
			if (Prim->IsSimulatingPhysics())
			{
				Prim->AddImpulseAtLocation(
					Forward * 60000.0f + FVector(0, 0, 15000.0f), Hit.ImpactPoint);
			}
		}
		++Struck;
	}

	// Fussgaenger getrennt behandeln.
	//
	// Der Sweep oben findet sie GRUNDSAETZLICH nicht: sie werden als
	// Instanzen einer HierarchicalInstancedStaticMeshComponent gezeichnet,
	// die ausdruecklich keine Kollision traegt. Der Hieb schwang bis hierher
	// ins Leere - Klang und Bewegung liefen, getroffen wurde nie etwas.
	// Deshalb fragt er die Simulation direkt.
	if (UWiesbadenCitySubsystem* City = World->GetSubsystem<UWiesbadenCitySubsystem>())
	{
		// Die Kettensaege faellt nicht, sie zerteilt: getroffene Fussgaenger
		// zerplatzen, statt umzufallen und wieder aufzustehen.
		const FVector Centre = GetActorLocation() + Forward * (MeleeRangeCm * 0.5f);
		const int32 Felled = City->PedestrianSimulation.BurstNear(
			Centre, MeleeRadiusCm + MeleeRangeCm * 0.5);
		Struck += Felled;

		// Jede zerplatze Figur ist eine Tat ins Fahndungskonto.
		for (int32 HitIndex = 0; HitIndex < Felled; ++HitIndex)
		{
			City->ReportCrime(EWiesbadenCrimeEvent::PedestrianDowned);
		}
	}

	if (Struck > 0)
	{
		UE_LOG(LogWbVehicles, Log, TEXT("Saegehieb: %d getroffen."), Struck);
	}
}

void AWiesbadenFootPawn::UpdateFigure(float DeltaSeconds, float SpeedMps)
{
	if (!bUsesChainsaw || !FigureMesh)
	{
		return;
	}

	// Kettensaegen-Klang: leiser Zweitakt-Leerlauf, beim Hieb Vollgas. Das
	// Tempo faerbt leicht mit - im Laufen dreht der Motor etwas hoeher, wie
	// bei einer getragenen Saege, die mitgeschuettelt wird.
	if (SawAudio)
	{
		const bool bSwinging = SwingRemaining > 0.0f;
		const float TargetRpm = bSwinging
			? 9200.0f
			: 2600.0f + 600.0f * FMath::Clamp(SpeedMps / 2.0f, 0.0f, 1.0f);
		SawAudio->SetEngineState(TargetRpm, bSwinging ? 1.0f : 0.08f, SpeedMps * 3.6f);
	}

	// Waehrend des Hiebs laeuft Sebbo_Swing - nichts ueberschreiben.
	if (SwingRemaining > 0.0f)
	{
		return;
	}

	if (SpeedMps > 0.4f)
	{
		if (CurrentLoop != 2 && WalkAnim)
		{
			FigureMesh->PlayAnimation(WalkAnim, true);
			CurrentLoop = 2;
		}
		// Schritttakt an das Tempo koppeln: der Zyklus ist fuer 1,67 m/s
		// gebaut; beim Rennen (16 km/h) laufen die Beine entsprechend
		// schneller, statt ueber den Asphalt zu gleiten.
		FigureMesh->SetPlayRate(FMath::Clamp(SpeedMps / WalkAnimSpeedMps, 0.5f, 3.0f));
	}
	else if (CurrentLoop != 1 && IdleAnim)
	{
		FigureMesh->PlayAnimation(IdleAnim, true);
		FigureMesh->SetPlayRate(1.0f);
		CurrentLoop = 1;
	}
}
