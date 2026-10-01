// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenVehicleCameraComponent.h"

#include "WiesbadenReal.h"

#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "World/WiesbadenVisualTuning.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UWiesbadenVehicleCameraComponent::UWiesbadenVehicleCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UWiesbadenVehicleCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bRigCreated)
	{
		CreateCameraRig();
		bRigCreated = true;
	}
}

void UWiesbadenVehicleCameraComponent::CreateCameraRig()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogWbVehicles, Warning, TEXT("Fahrzeug-Kamera ohne Owner - Rig wird nicht erzeugt."));
		return;
	}

	// SpringArm: Kind der Fahrzeug-Wurzel -> dreht mit dem Fahrzeug mit.
	SpringArm = NewObject<USpringArmComponent>(Owner, TEXT("VehicleSpringArm"));
	SpringArm->AttachToComponent(CameraAnchor ? CameraAnchor : this, FAttachmentTransformRules::KeepRelativeTransform);
	SpringArm->TargetArmLength = FollowArmLength;
	SpringArm->bUsePawnControlRotation = false;
	// Ruhiger Horizont: Nicken und Rollen des Rumpfs bleiben draussen, nur
	// die Blickrichtung (Gieren) folgt. Fuer Bodenfahrzeuge unveraendert.
	SpringArm->bInheritPitch = !bLevelHorizon;
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = !bLevelHorizon;
	SpringArm->bDoCollisionTest = true;
	// Drehglaettung machen wir selbst in UpdateBoom (versionssicher);
	// Positions-Nachlauf darf die Engine uebernehmen.
	SpringArm->bEnableCameraLag = PositionLagSpeed > 0.0f;
	SpringArm->CameraLagSpeed = FMath::Max(PositionLagSpeed, 0.01f);
	SpringArm->CameraLagMaxDistance = 350.0f;
	SpringArm->bEnableCameraRotationLag = false;
	SpringArm->SetRelativeLocation(CameraOffset);
	SpringArm->SetRelativeRotation(FRotator(FollowPitchOffset, 0.0f, 0.0f));
	SpringArm->RegisterComponent();

	ThirdPersonCamera = NewObject<UCameraComponent>(Owner, TEXT("VehicleThirdPersonCamera"));
	ThirdPersonCamera->AttachToComponent(SpringArm, FAttachmentTransformRules::KeepRelativeTransform, USpringArmComponent::SocketName);
	// Explizites Bildfeld statt Engine-Default 90 (Fischauge-Weitwinkel) -
	// Werte in World/WiesbadenVisualTuning.h.
	ThirdPersonCamera->SetFieldOfView(WiesbadenVisualTuning::FollowFieldOfView);
	ThirdPersonCamera->bUsePawnControlRotation = false;
	ThirdPersonCamera->SetActive(true);
	ThirdPersonCamera->RegisterComponent();

	CockpitSocket = NewObject<USceneComponent>(Owner, TEXT("VehicleCockpitSocket"));
	CockpitSocket->AttachToComponent(CameraAnchor ? CameraAnchor : this, FAttachmentTransformRules::KeepRelativeTransform);
	CockpitSocket->SetRelativeLocation(CockpitOffset);
	// Eigene Blickrichtung (z. B. Tiefblick auf die Instrumente im Bahnwagen).
	CockpitSocket->SetRelativeRotation(FRotator(CockpitPitch, CockpitYaw, 0.0f));
	CockpitSocket->RegisterComponent();

	CockpitCamera = NewObject<UCameraComponent>(Owner, TEXT("VehicleCockpitCamera"));
	CockpitCamera->AttachToComponent(CockpitSocket, FAttachmentTransformRules::KeepRelativeTransform);
	CockpitCamera->SetFieldOfView(WiesbadenVisualTuning::CockpitFieldOfView);
	CockpitCamera->bUsePawnControlRotation = false;
	CockpitCamera->SetActive(false);
	CockpitCamera->RegisterComponent();

	ApplyCameraMode();

	UE_LOG(LogWbVehicles, Log, TEXT("Fahrzeug-Kamera-Rig fuer %s erzeugt."), *Owner->GetName());
}

void UWiesbadenVehicleCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	HandleInput(DeltaTime);
	UpdateBoom(DeltaTime);
}

void UWiesbadenVehicleCameraComponent::CycleCameraMode()
{
	// Gesperrte Follow-Kamera: Modus bleibt hinter dem Fahrzeug, keine Weiterschaltung.
	if (bLockFollowMode)
	{
		CameraMode = EWiesbadenVehicleCameraMode::Follow;
		return;
	}

	switch (CameraMode)
	{
	case EWiesbadenVehicleCameraMode::Follow:
		CameraMode = EWiesbadenVehicleCameraMode::Orbit;
		break;
	case EWiesbadenVehicleCameraMode::Orbit:
		CameraMode = EWiesbadenVehicleCameraMode::Cockpit;
		break;
	case EWiesbadenVehicleCameraMode::Cockpit:
	default:
		CameraMode = EWiesbadenVehicleCameraMode::Follow;
		break;
	}

	ApplyCameraMode();
}

void UWiesbadenVehicleCameraComponent::ApplyCameraMode()
{
	// Gesperrte Follow-Kamera: immer dritte Person direkt hinter dem Fahrzeug.
	if (bLockFollowMode)
	{
		CameraMode = EWiesbadenVehicleCameraMode::Follow;
	}

	const bool bCockpit = (CameraMode == EWiesbadenVehicleCameraMode::Cockpit);
	if (ThirdPersonCamera)
	{
		ThirdPersonCamera->SetActive(!bCockpit);
	}
	if (CockpitCamera)
	{
		CockpitCamera->SetActive(bCockpit);
	}

	// Eigene Aussenhaut in der Cockpit-Ansicht fuer den Fahrer ausblenden -
	// sonst blickt man in der Ich-Perspektive auf die Rueckseiten der
	// geschlossenen Karosserie. Nur die Sicht des Besitzers, nicht die anderer.
	for (UPrimitiveComponent* Mesh : CockpitHiddenMeshes)
	{
		if (Mesh)
		{
			Mesh->SetOwnerNoSee(bCockpit);
		}
	}

	if (CameraMode == EWiesbadenVehicleCameraMode::Follow)
	{
		OrbitOffset = FRotator::ZeroRotator;
	}

	UE_LOG(LogWbVehicles, Log, TEXT("Fahrzeug-Kameramodus: %d"), static_cast<int32>(CameraMode));
}

void UWiesbadenVehicleCameraComponent::AddCockpitHiddenMesh(UPrimitiveComponent* Mesh)
{
	if (Mesh)
	{
		CockpitHiddenMeshes.AddUnique(Mesh);
		// Sofort auf den aktuellen Modus bringen (Registrierung passiert im
		// Konstruktor/BeginPlay, ApplyCameraMode kann schon gelaufen sein).
		Mesh->SetOwnerNoSee(CameraMode == EWiesbadenVehicleCameraMode::Cockpit);
	}
}

void UWiesbadenVehicleCameraComponent::RemoveCockpitHiddenMesh(UPrimitiveComponent* Mesh)
{
	if (!Mesh) { return; }
	CockpitHiddenMeshes.Remove(Mesh);
	Mesh->SetOwnerNoSee(false);
}

void UWiesbadenVehicleCameraComponent::HandleInput(float DeltaTime)
{
	LastLookInputMagnitude = 0.0f;
#if !UE_BUILD_SHIPPING
	// Dev-Sichtprobe: -WbCamMode=0/1/2 erzwingt Follow/Orbit/Cockpit EINMAL, damit
	// sich die Innen-/Aussenansicht headless per Screenshot belegen laesst.
	if (!bDevModeApplied)
	{
		bDevModeApplied = true;
		int32 Forced = -1;
		if (FParse::Value(FCommandLine::Get(), TEXT("WbCamMode="), Forced) && Forced >= 0)
		{
			CameraMode = static_cast<EWiesbadenVehicleCameraMode>(FMath::Clamp(Forced, 0, 2));
			ApplyCameraMode();
			return;
		}
	}
#endif

	// Gesperrte Follow-Kamera: Umschaltung und Orbit-Eingaben komplett ignorieren.
	if (bLockFollowMode)
	{
		bCameraToggleHeld = false;
		return;
	}

	APlayerController* PC = GetPlayerController();

	// Umschaltung (Flanke auf die konfigurierte Taste).
	const bool bPressed = PC && (PC->IsInputKeyDown(ToggleKey)
		|| (PadToggleKey.IsValid() && PC->IsInputKeyDown(PadToggleKey)));
	if (bPressed && !bCameraToggleHeld)
	{
		CycleCameraMode();
	}
	bCameraToggleHeld = bPressed;

	if (!PC)
	{
		return;
	}

	// Cockpit: fester Blick nach vorn, kein Umsehen-Offset.
	if (CameraMode == EWiesbadenVehicleCameraMode::Cockpit)
	{
		return;
	}

	// Umsehen greift jetzt in Follow UND Orbit. Vorher nur Orbit - deshalb
	// schien die Kamera "kaputt": im Standard-Follow bewirkte Maus/Stick nichts,
	// obwohl die Steuerungshilfe "Maus - Umsehen" verspricht.
	float LookYaw = 0.0f;
	float LookPitch = 0.0f;
	bool bLooked = false;

	// Maus (Frame-Delta, framerate-unabhaengig).
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	if (MouseX != 0.0f || MouseY != 0.0f)
	{
		LookYaw += MouseX * MouseSensitivity;
		LookPitch += MouseY * MouseSensitivity;
		LastLookInputMagnitude = FMath::Max(LastLookInputMagnitude,
			FMath::Max(FMath::Abs(MouseX), FMath::Abs(MouseY)));
		bLooked = true;
	}

	// Gamepad rechter Stick (analog, mit kleiner Totzone gegen Drift).
	const float StickX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	const float StickY = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
	const float Wheel = PC->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
	if (FMath::Abs(Wheel) > KINDA_SMALL_NUMBER && SpringArm)
	{
		SpringArm->TargetArmLength = FMath::Clamp(
			SpringArm->TargetArmLength - Wheel * ZoomStep,
			ZoomMinArmLength, ZoomMaxArmLength);
	}
	if (FMath::Abs(StickX) > 0.15f || FMath::Abs(StickY) > 0.15f)
	{
		LookYaw += StickX * GamepadLookRate * DeltaTime;
		LookPitch += StickY * GamepadLookRate * DeltaTime;
		LastLookInputMagnitude = FMath::Max(LastLookInputMagnitude,
			FMath::Max(FMath::Abs(StickX), FMath::Abs(StickY)));
		bLooked = true;
	}

	// Pfeiltasten: nur im Orbit-Modus, weil sie im Fahrzeug lenken/gasgeben.
	if (CameraMode == EWiesbadenVehicleCameraMode::Orbit)
	{
		const float Turn = OrbitTurnRate * DeltaTime;
		if (PC->IsInputKeyDown(EKeys::Left))  { LookYaw -= Turn; bLooked = true; }
		if (PC->IsInputKeyDown(EKeys::Right)) { LookYaw += Turn; bLooked = true; }
		if (PC->IsInputKeyDown(EKeys::Up))    { LookPitch += Turn; bLooked = true; }
		if (PC->IsInputKeyDown(EKeys::Down))  { LookPitch -= Turn; bLooked = true; }
	}

	OrbitOffset.Yaw = FRotator::NormalizeAxis(OrbitOffset.Yaw + LookYaw);
	OrbitOffset.Pitch = FMath::Clamp(OrbitOffset.Pitch + LookPitch, -80.0f, 80.0f);

	// Follow-Freilook: ohne Eingabe sanft hinter das Fahrzeug zuruecklaufen.
	// Im Orbit-Modus bleibt der Blick stehen, wo man ihn geparkt hat.
	if (CameraMode == EWiesbadenVehicleCameraMode::Follow && !bLooked && FollowRecenterSpeed > 0.0f)
	{
		OrbitOffset.Yaw = FMath::FInterpTo(OrbitOffset.Yaw, 0.0f, DeltaTime, FollowRecenterSpeed);
		OrbitOffset.Pitch = FMath::FInterpTo(OrbitOffset.Pitch, 0.0f, DeltaTime, FollowRecenterSpeed);
	}
}

void UWiesbadenVehicleCameraComponent::UpdateBoom(float DeltaTime)
{
	if (!SpringArm)
	{
		return;
	}

	// Cockpit-Ansicht nutzt die Cockpit-Kamera; der Boom wird ignoriert.
	if (CameraMode == EWiesbadenVehicleCameraMode::Cockpit)
	{
		return;
	}

	// Follow: Basis-Neigung plus Freilook-Offset (Yaw/Pitch aus Maus/Stick).
	// Orbit: reiner Umsehen-Offset.
	FRotator Desired = FRotator(FollowPitchOffset + OrbitOffset.Pitch, OrbitOffset.Yaw, 0.0f);
	if (CameraMode == EWiesbadenVehicleCameraMode::Orbit)
	{
		Desired = FRotator(OrbitOffset.Pitch, OrbitOffset.Yaw, 0.0f);
	}

	const FRotator Current = SpringArm->GetRelativeRotation();
	const float Response = FMath::Clamp(CameraResponse, 0.01f, 100.0f);
	// Nur der Yaw braucht den kuerzesten Weg: Pitch ist auf +-80 begrenzt und
	// Roll ist hier immer 0 - dort gibt es keinen Vorzeichenbruch.
	const FRotator Next(
		FMath::FInterpTo(Current.Pitch, Desired.Pitch, DeltaTime, Response),
		SmoothYaw(Current.Yaw, Desired.Yaw, DeltaTime, Response),
		FMath::FInterpTo(Current.Roll, Desired.Roll, DeltaTime, Response));
	SpringArm->SetRelativeRotation(Next);
}

float UWiesbadenVehicleCameraComponent::SmoothYaw(float CurrentYaw, float DesiredYaw, float DeltaTime, float Response)
{
	const float Delta = FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredYaw);
	const float Schritt = FMath::FInterpTo(0.0f, Delta, DeltaTime, Response);
	return FRotator::NormalizeAxis(CurrentYaw + Schritt);
}

void UWiesbadenVehicleCameraComponent::SetCameraAnchor(USceneComponent* Anchor)
{
	CameraAnchor = Anchor;
}

void UWiesbadenVehicleCameraComponent::ActivateExternalView(
	APlayerController* Controller, USceneComponent* Anchor, AActor* RestoreTarget)
{
	ExternalController = Controller;
	ExternalRestoreTarget = RestoreTarget;
	CameraAnchor = Anchor;
	bExternalViewActive = Controller != nullptr && Anchor != nullptr;
	if (bExternalViewActive)
	{
		if (!bRigCreated)
		{
			CreateCameraRig();
			bRigCreated = true;
		}
		if (ExternalController.IsValid())
		{
			ExternalController->SetViewTarget(GetOwner());
		}
	}
}

void UWiesbadenVehicleCameraComponent::DeactivateExternalView()
{
	if (ExternalController.IsValid() && ExternalRestoreTarget.IsValid())
	{
		ExternalController->SetViewTarget(ExternalRestoreTarget.Get());
	}
	bExternalViewActive = false;
	ExternalController.Reset();
	ExternalRestoreTarget.Reset();
	CameraAnchor = nullptr;
}

APlayerController* UWiesbadenVehicleCameraComponent::GetPlayerController() const
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			return PC;
		}
	}

	UWorld* World = GetWorld();
	return World ? World->GetFirstPlayerController() : nullptr;
}
