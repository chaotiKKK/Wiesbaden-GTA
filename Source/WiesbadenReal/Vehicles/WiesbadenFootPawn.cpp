// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenFootPawn.h"

#include "Core/WiesbadenInputMap.h"       // Belegungstabelle (Tastatur + XBox)
#include "Vehicles/WiesbadenHelicopter.h"   // ApplyStickShaping: eine Kennlinie fuer alle Sticks
#include "Weapons/WiesbadenWeaponComponent.h"
#include "Weapons/WiesbadenWeaponSpec.h"

#include "WiesbadenReal.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Vehicles/WiesbadenSebboFigureComponent.h"
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
	Capsule->InitCapsuleSize(40.0f, StandingHalfHeightCm);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	SetRootComponent(Capsule);

	// Verfolgerkamera wie beim Fahrzeug - so bleibt der Wechsel zwischen zu
	// Fuss und am Steuer optisch ruhig.
	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(Capsule);
	CameraArm->TargetArmLength = ShoulderArmLengthCm;
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
	FigureMesh = CreateDefaultSubobject<UWiesbadenSebboFigureComponent>(TEXT("FigureMesh"));
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

	// Grund-FOV merken: der Zielmodus teilt es durch den Zoomfaktor, und wer
	// den Wert hier liest, muss ihn nicht zur Kameraeinstellung doppeln.
	if (Camera)
	{
		BaseCameraFOV = Camera->FieldOfView;
	}

	// Erst hier, nicht im Konstruktor: die Materialien liegen als Assets vor
	// und sind zur Konstruktionszeit des CDO noch nicht sicher ladbar.
	BuildBody();

	// Dasselbe gilt fuer die Waffe: Modell aus Grundkoerpern, Muendungslicht
	// und prozeduraler Schussklang brauchen eine laufende Welt.
	if (Weapon)
	{
		Weapon->SetupWeapon();
	}

	// Waffenlage und Zielzustand einmal sauber setzen - dann gilt die
	// Armlaenge aus den Eigenschaften, nicht nur der Konstruktionswert.
	ApplyCameraMode();
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

	// Ducken, solange X (oder der rechte Stick) gehalten wird. Aufstehen nur mit
	// Platz darueber: unter einer niedrigen Decke bleibt die Figur geduckt,
	// bis sie hervorkommt.
	// Ducken: X (halten) oder R3 am Gamepad - aus der Belegungstabelle,
	// nicht als verstreute Einzelabfrage.
	const bool bCrouchDown =
		WiesbadenInputMap::IsActionDown(PC, EWiesbadenInputAction::DuckenHalten);
	if (bCrouchDown && !bCrouched && !bAirborne)
	{
		SetCrouched(true);
	}
	else if (!bCrouchDown && bCrouched && HasRoomToStand())
	{
		SetCrouched(false);
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
		// Rennen: Umschalt oder L3 (Vorbild RDR2: Sprint auf dem linken Stick).
		const bool bSprint =
			WiesbadenInputMap::IsActionDown(PC, EWiesbadenInputAction::RennenHalten);
		const float SpeedCmPerS = (bCrouched ? CrouchSpeedKmh : bSprint ? SprintSpeedKmh : WalkSpeedKmh)
			* KmhToCmPerS;
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
	const bool bJumpDown =
		WiesbadenInputMap::IsActionDown(PC, EWiesbadenInputAction::Springen);
	if (bJumpDown && !bJumpKeyHeld && !bAirborne && !bCrouched)
	{
		VerticalSpeedCmS = JumpSpeedCmS;
		bAirborne = true;
	}
	bJumpKeyHeld = bJumpDown;

	// Feuern: linke Maustaste, Strg/Enter als Ersatz, RT am Gamepad
	// (RDR2-Layout: rechter Trigger schiesst).
	const bool bFireDown =
		WiesbadenInputMap::IsActionDown(PC, EWiesbadenInputAction::Feuern);

	// Ansicht und Waffenwahl vor dem Feuern abfragen: ein Druck auf C oder
	// eine Ziffer gilt im selben Bild schon fuer die neue Lage.
	PollWeaponKeys(PC);
	PollAimAndWheel(PC);
	const bool bEgoDown =
		WiesbadenInputMap::IsActionDown(PC, EWiesbadenInputAction::AnsichtWechseln);
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
	LastGroundCheckLocation = GetActorLocation();

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
	// Die Spielerfigur: Sebbo als geriggtes Tripo-Modell. Clips und Clip-Wahl
	// besitzt die Figur (UWiesbadenSebboFigureComponent).
	if (FigureMesh && FigureMesh->SetupFigure(2.0f * JumpSpeedCmS / FMath::Max(GravityCmPerS2, 1.0f)))
	{
		FigureMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -88.0f));

		// Nahkampf (Taste 9) ist jetzt ein Tritt: das Modell traegt keine
		// Kettensaege mehr. Hiebdauer = Laenge des Tritts, Treffer beim
		// hoechsten Bein (0,67 von 1,42 s in der Quelle).
		if (const float Kick = FigureMesh->MoveLength(EWbSebboMove::Kick); Kick > 0.0f)
		{
			SwingSeconds = Kick;
			SwingHitAtSeconds = 0.47f * Kick;
		}

		if (BodyMesh) { BodyMesh->SetVisibility(false); }
		if (HeadMesh) { HeadMesh->SetVisibility(false); }
		bUsesChainsaw = false;
		return;
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
		CameraArm->TargetArmLength = ShoulderArmLengthCm;
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

	// Zielzustand zuletzt: Zoom, Armlaenge und Streuung gelten fuer beide
	// Lagen (Schulter und Ego).
	ApplyAimState();
}

void AWiesbadenFootPawn::PollWeaponKeys(const APlayerController* PC)
{
	if (!PC || !Weapon)
	{
		return;
	}

	// Ziffern 1-8 auf die acht Waffen des Auftrags, in dessen Reihenfolge
	// (Pistole, Gewehr, MG, Laserpistole, Lichtschwert, Raketenwerfer,
	// Granatwerfer, Plasmacutter). Die vier Nebenwaffen (MP, Schrotflinte,
	// Scharfschuetze, Kettensaege) haben keine eigene Ziffer mehr - sie
	// liegen auf dem Mausrad, das durch die ganze Tabelle blaettern kann.
	// Flankenerkennung je Taste, damit Halten nicht springt.
	static const FKey Keys[8] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
		EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight };
	static const int32 CoreWeapons[8] = {
		static_cast<int32>(EWiesbadenWeaponId::Pistole),
		static_cast<int32>(EWiesbadenWeaponId::Gewehr),
		static_cast<int32>(EWiesbadenWeaponId::Maschinengewehr),
		static_cast<int32>(EWiesbadenWeaponId::Laserpistole),
		static_cast<int32>(EWiesbadenWeaponId::Lichtschwert),
		static_cast<int32>(EWiesbadenWeaponId::Raketenwerfer),
		static_cast<int32>(EWiesbadenWeaponId::Granatwerfer),
		static_cast<int32>(EWiesbadenWeaponId::Plasmacutter) };

	for (int32 Index = 0; Index < 8; ++Index)
	{
		const bool bDown = PC->IsInputKeyDown(Keys[Index]);
		if (bDown && !WeaponKeyHeld[Index]
			&& UWiesbadenWeaponComponent::IsValidWeaponIndex(CoreWeapons[Index]))
		{
			SelectWeapon(CoreWeapons[Index]);
		}
		WeaponKeyHeld[Index] = bDown;
	}
}

void AWiesbadenFootPawn::PollAimAndWheel(const APlayerController* PC)
{
	if (!PC)
	{
		return;
	}

	// Zielen (ADS): rechte Maustaste ODER linker Trigger am Gamepad
	// (RDR2-Layout: LT zielt). Kamera zoomt heran, Streuung halbiert sich;
	// Loslassen stellt beides sofort wieder her.
	const bool bAimDown =
		WiesbadenInputMap::IsActionDown(PC, EWiesbadenInputAction::Zielen);
	if (bAimDown != bAiming)
	{
		bAiming = bAimDown;
		if (!bAiming)
		{
			AdsZoomLevel = 1.0f;
		}
		ApplyAimState();
	}

	// Waffenwechsel an den Schultertasten (Flanke je Taste): der Gamepad-Weg
	// fuer das, was am PC das Mausrad tut.
	const bool bVorHeld =
		WiesbadenInputMap::IsActionDown(PC, EWiesbadenInputAction::WaffeVor);
	if (bVorHeld && !bWaffeVorHeld && Weapon)
	{
		SelectWeapon(WiesbadenWeapons::NextWeaponIndex(Weapon->WeaponIndex, +1));
	}
	bWaffeVorHeld = bVorHeld;

	const bool bZurueckHeld =
		WiesbadenInputMap::IsActionDown(PC, EWiesbadenInputAction::WaffeZurueck);
	if (bZurueckHeld && !bWaffeZurueckHeld && Weapon)
	{
		SelectWeapon(WiesbadenWeapons::NextWeaponIndex(Weapon->WeaponIndex, -1));
	}
	bWaffeZurueckHeld = bZurueckHeld;

	// "Mausrad": die Achse meldet ein Delta je Bild, das D-Pad liefert
	// Klicks als Flanken. Erst ab einem ganzen Klick handeln, damit ein
	// langsames Scrollen nicht mehrere Stufen springt.
	WheelAccumulator += PC->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
	const bool bPadUp = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Up);
	const bool bPadDown = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Down);
	if (bPadUp && !bPadUpHeld)
	{
		WheelAccumulator += 1.0f;
	}
	if (bPadDown && !bPadDownHeld)
	{
		WheelAccumulator -= 1.0f;
	}
	bPadUpHeld = bPadUp;
	bPadDownHeld = bPadDown;

	if (FMath::Abs(WheelAccumulator) < 1.0f)
	{
		return;
	}
	const int32 Clicks = FMath::TruncToInt(WheelAccumulator);
	WheelAccumulator -= static_cast<float>(Clicks);

	// Aufteilung des Klicks - dieselbe reine Funktion wie am PC, im Test
	// geprueft (WiesbadenReal.Input.MausradRoute).
	bool bCuts = false;
	if (Weapon)
	{
		bCuts = WiesbadenWeapons::Spec(Weapon->WeaponIndex).bCuts;
	}

	switch (WiesbadenInputMap::RouteMausrad(bCuts, bAiming))
	{
	case WiesbadenInputMap::EMausradRoute::Schnittebene:
		// Plasma-Trennen: die Schnittebene dreht in Rasten (Spec), weder
		// Zoom noch Waffenwechsel. Das Dead-Space-Prinzip.
		if (Weapon)
		{
			const FWiesbadenWeaponSpec& CutSpec =
				WiesbadenWeapons::Spec(Weapon->WeaponIndex);
			Weapon->RotateCutPlane(Clicks * CutSpec.CutAngleStepDeg);
		}
		break;

	case WiesbadenInputMap::EMausradRoute::Zoom:
	{
		// Zielmodus: das Rad zoomt. Die Obergrenze gehoert zur Waffe
		// (Scharfschuetze 3.5, Plasmacutter 1.4), nicht zum Pawn.
		const float MaxZoom = Weapon
			? WiesbadenWeapons::Spec(Weapon->WeaponIndex).AdsZoomMax
			: 2.0f;
		AdsZoomLevel = WiesbadenInputMap::ZoomStufe(
			AdsZoomLevel, Clicks, AdsZoomStep, MaxZoom);
		ApplyAimState();
		break;
	}

	case WiesbadenInputMap::EMausradRoute::Waffenwechsel:
	default:
		// Sonst blaettern: durch die ganze Tabelle, ueber beide Raender.
		if (Weapon)
		{
			SelectWeapon(WiesbadenWeapons::NextWeaponIndex(Weapon->WeaponIndex, Clicks));
		}
		break;
	}
}

void AWiesbadenFootPawn::ApplyAimState()
{
	// Zoom = FOV teilen: 2.0 halbiert den Bildausschnitt. Der Grundwert
	// stammt aus der Kamera selbst (gemerkt beim Start), damit niemand zwei
	// FOV-Zahlen synchron halten muss.
	if (Camera)
	{
		Camera->SetFieldOfView(BaseCameraFOV / FMath::Max(AdsZoomLevel, 1.0f));
	}

	// Im Zielmodus rueckt die Kamera dichter an die Schulter.
	if (CameraArm)
	{
		const float BaseArm = bEgoCamera ? EgoArmLengthCm : ShoulderArmLengthCm;
		CameraArm->TargetArmLength = bAiming ? BaseArm * AdsArmLengthScale : BaseArm;
	}

	// Zielen macht praezise: halbe Streuung.
	if (Weapon)
	{
		Weapon->SpreadScale = bAiming ? 0.5f : 1.0f;
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

	// Zoom bleibt gueltig, aber nie ueber die Grenze der neuen Waffe.
	AdsZoomLevel = FMath::Min(AdsZoomLevel, FMath::Max(Spec.AdsZoomMax, 1.0f));

	// Die Kettensaege (Slot 9) schwingt die Figur und tuckert; jede andere
		// Waffe zeigt die Waffenkomponente und feuert Projektile. Ein laufender
		// Hieb gehoert zur Saege und wird beim Wechsel abgebrochen.
	const bool bSaw = Index == static_cast<int32>(EWiesbadenWeaponId::Kettensaege)
		&& FigureMesh && FigureMesh->HasMove(EWbSebboMove::Kick);
	bUsesChainsaw = bSaw;
	SwingRemaining = 0.0f;
	if (FigureMesh)
	{
		FigureMesh->CancelOneShot(EWbSebboMove::Kick);
	}
	bMeleeHitDone = false;

	if (Weapon)
	{
		Weapon->SetVisibility(!bSaw, true);
	}
	// Slot 9 ist ein Tritt (A_Sebbo_Kick), keine Saege mehr: der
	// Zweitakter-Synthesizer bleibt still.
	if (SawAudio)
	{
		SawAudio->SetEngineRunning(false);
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

	// WorldStatic, nicht Visibility: Fahrbahn, Gehweg und Gelaende sind
	// statische Weltgeometrie. Die Kanaele stimmen hier zwar ueberein, aber die
	// Bodenabfrage soll denselben Kanal benutzen wie die Kollision, auf der
	// gelaufen wird.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbFootGround), true);
	Params.AddIgnoredActor(this);

	// Boden = erste Flaeche unter dem SCHEITEL der Kapsel. Was ueber dem Kopf
	// liegt, ist Decke: mit einem Start 2 m ueber der Mitte setzte die Abfrage
	// die geduckte Figur unter einer niedrigen Platte OBEN auf die Platte.
	//
	// Ausnahme: gerade VERSETZT (Aussteigen, -WbGoto, Bahn/Bus). Dann kann die
	// Figur im Gelaende stecken - am 54-%-Hang der Emser Strasse bis ueber den
	// Kopf -, und nur die alte Reichweite von 2 m ueber der Mitte findet den
	// Boden wieder. Gehen versetzt nie so weit (Sweep); ein Sprung von mehr als
	// 1,5 m in einem Bild ist ein Versetzen.
	const FVector Center = GetActorLocation();
	if (FVector::Dist(Center, LastGroundCheckLocation) > 150.0)
	{
		TeleportGraceSeconds = 0.5f;
	}
	const float Reach = TeleportGraceSeconds > 0.0f ? 200.0f
		: (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f);
	TeleportGraceSeconds = FMath::Max(0.0f, TeleportGraceSeconds - DeltaSeconds);
	const FVector Start = Center + FVector(0.0, 0.0, Reach);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0, 0.0, 100000.0), ECC_WorldStatic, Params))
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

void AWiesbadenFootPawn::SetRiding(bool bInRiding)
{
	// Bahn und Bus setzen den Fahrgast mit der STEHENDEN Kapsel ein (Boden +
	// 90 cm) und verfolgen waehrend der Fahrt keinen Boden - geduckt schwebte
	// er 20 cm ueber dem Wagenboden und stuende nach dem Aussteigen mit der
	// kurzen Kapsel da. Der Wagen hat Kopfhoehe, also aufrichten.
	if (bInRiding)
	{
		SetCrouched(false);
	}
	bRiding = bInRiding;
}

void AWiesbadenFootPawn::SetCrouched(bool bInCrouched)
{
	if (!Capsule || bInCrouched == bCrouched)
	{
		return;
	}
	bCrouched = bInCrouched;
	const float HalfHeight = bCrouched ? CrouchHalfHeightCm : StandingHalfHeightCm;
	const float Delta = Capsule->GetUnscaledCapsuleHalfHeight() - HalfHeight;
	Capsule->SetCapsuleHalfHeight(HalfHeight);
	// Fuesse bleiben, wo sie sind: der Kapselmittelpunkt wandert um die
	// Differenz, die Figuren sitzen wieder mit den Sohlen auf der Kapselunterseite.
	AddActorWorldOffset(FVector(0.0f, 0.0f, -Delta), /*bSweep=*/false);
	const FVector Feet(0.0f, 0.0f, -(HalfHeight - 2.0f));
	if (FigureMesh) { FigureMesh->SetRelativeLocation(Feet); }
	if (BodyMesh) { BodyMesh->SetRelativeLocation(Feet); }
}

bool AWiesbadenFootPawn::HasRoomToStand() const
{
	const UWorld* World = GetWorld();
	if (!World || !Capsule)
	{
		return true;
	}
	// Die geduckte Kapsel um die Differenz nach oben schieben - trifft sie
	// etwas, stoesst der Kopf an. Etwas schmaler als die Kapsel, damit ein
	// Hang unter den Fuessen nicht als Decke zaehlt.
	const float Rise = StandingHalfHeightCm - Capsule->GetUnscaledCapsuleHalfHeight();
	const FVector Start = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbAufstehen), false, this);
	return !World->SweepTestByChannel(Start, Start + FVector(0.0f, 0.0f, Rise), FQuat::Identity,
		Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Capsule->GetUnscaledCapsuleRadius() - 5.0f, Capsule->GetUnscaledCapsuleHalfHeight()),
		Params, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
}

void AWiesbadenFootPawn::StartSwing()
{
	SwingRemaining = SwingSeconds;
	bMeleeHitDone = false;

	if (FigureMesh)
	{
		FigureMesh->PlayOneShot(EWbSebboMove::Kick, SwingSeconds);
	}
	UE_LOG(LogWbVehicles, Log, TEXT("Tritt (Taste 9): A_Sebbo_Kick %.2f s, Saegenklang %s."),
		SwingSeconds, SawAudio && SawAudio->IsEngineRunning() ? TEXT("AN") : TEXT("aus"));
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
	//
	// Ausnahme: die KANTE einer Stufe. Trifft die runde Kapselunterseite die
	// Vorderkante, ist die Kontaktnormale schraeg (im Sebbo-Treppenhaus Z 0,63
	// bei 15 cm ueber den Fuessen) - die Figur rutschte ab und hing am Podest,
	// je nach Bildtakt in einem anderen Geschoss. Beruehrt sie zwischen 10 cm
	// und Stufenhoehe ueber den Fuessen, ist es eine Stufe; Rampen beruehren
	// tiefer (unter 6 cm bis 30 Grad), Decken hoeher.
	const float FeetZ = GetActorLocation().Z - (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f);
	const float ContactCm = Blocked.ImpactPoint.Z - FeetZ;
	const bool bStepEdge = ContactCm > 10.0f && ContactCm <= MaxStepHeightCm;
	if (FMath::Abs(Blocked.Normal.Z) > 0.5f && !bStepEdge)
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

	// Eine Beruehrung am START ist kein Hindernis: an der Wand des
	// Sebbo-Treppenhauses steht die Kapsel buendig (Radius 40 = Abstand zur
	// Wand), und jeder Sweep meldete "steckt schon" - die Figur kam nicht vom
	// Podest auf den naechsten Lauf. Dieselbe Regel wie die Treppensonde
	// (KapselSchritt in WiesbadenSebboHqSonden.cpp).
	const auto Blocks = [](const FHitResult& H) { return H.bBlockingHit && !H.bStartPenetrating; };

	FHitResult Probe;
	const FVector Raised = Start + Lift;
	if (World->SweepSingleByChannel(Probe, Start, Raised, FQuat::Identity,
		ECC_Pawn, Shape, Params) && Blocks(Probe))
	{
		return false;
	}

	const FVector Ahead = Raised + Wanted.GetSafeNormal() * (Wanted.Size() + StepForwardProbeCm);
	if (World->SweepSingleByChannel(Probe, Raised, Ahead, FQuat::Identity,
		ECC_Pawn, Shape, Params) && Blocks(Probe))
	{
		return false;
	}

	// Wieder absetzen. Findet sich unterhalb kein Boden innerhalb der
	// Stufenhoehe, war es eine Kante ins Nichts - dann NICHT hinaufsteigen,
	// sonst schwebt die Figur.
	const FVector Down = Ahead - Lift - FVector(0.0f, 0.0f, 2.0f);
	if (!World->SweepSingleByChannel(Probe, Ahead, Down, FQuat::Identity,
		ECC_Pawn, Shape, Params) || Probe.bStartPenetrating)
	{
		return false;   // kein Boden - oder die Figur staende IN der Geometrie
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
		if (Felled > 0)
		{
			City->PlayPedestrianBurstSound(Centre);
		}

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
	if (FigureMesh)
	{
		FWbFigureInput Input;
		Input.SpeedMps = SpeedMps;
		Input.YawDeg = GetActorRotation().Yaw;
		Input.bAirborne = bAirborne;
		Input.bRiding = bRiding;
		Input.bCrouching = bCrouched;
		Input.HealthPoints = HealthPoints;
		FigureMesh->Animate(DeltaSeconds, Input);
	}
}
