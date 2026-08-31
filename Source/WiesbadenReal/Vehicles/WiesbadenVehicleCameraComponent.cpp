// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenVehicleCameraComponent.h"

#include "WiesbadenReal.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"

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
	SpringArm->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
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
	SpringArm->SetRelativeRotation(FRotator(FollowPitchOffset, 0.0f, 0.0f));
	SpringArm->RegisterComponent();

	ThirdPersonCamera = NewObject<UCameraComponent>(Owner, TEXT("VehicleThirdPersonCamera"));
	ThirdPersonCamera->AttachToComponent(SpringArm, FAttachmentTransformRules::KeepRelativeTransform, USpringArmComponent::SocketName);
	ThirdPersonCamera->bUsePawnControlRotation = false;
	ThirdPersonCamera->SetActive(true);
	ThirdPersonCamera->RegisterComponent();

	CockpitSocket = NewObject<USceneComponent>(Owner, TEXT("VehicleCockpitSocket"));
	CockpitSocket->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	CockpitSocket->SetRelativeLocation(CockpitOffset);
	CockpitSocket->RegisterComponent();

	CockpitCamera = NewObject<UCameraComponent>(Owner, TEXT("VehicleCockpitCamera"));
	CockpitCamera->AttachToComponent(CockpitSocket, FAttachmentTransformRules::KeepRelativeTransform);
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

	if (CameraMode == EWiesbadenVehicleCameraMode::Follow)
	{
		OrbitOffset = FRotator::ZeroRotator;
	}

	UE_LOG(LogWbVehicles, Log, TEXT("Fahrzeug-Kameramodus: %d"), static_cast<int32>(CameraMode));
}

void UWiesbadenVehicleCameraComponent::HandleInput(float DeltaTime)
{
	// Gesperrte Follow-Kamera: Umschaltung und Orbit-Eingaben komplett ignorieren.
	if (bLockFollowMode)
	{
		bCameraToggleHeld = false;
		return;
	}

	APlayerController* PC = GetPlayerController();

	// Umschaltung (Flanke auf die konfigurierte Taste).
	const bool bPressed = PC && PC->IsInputKeyDown(ToggleKey);
	if (bPressed && !bCameraToggleHeld)
	{
		CycleCameraMode();
	}
	bCameraToggleHeld = bPressed;

	// Orbit schwenken (nur im Orbit-Modus).
	if (CameraMode == EWiesbadenVehicleCameraMode::Orbit && PC)
	{
		// MAUS zuerst: Umschauen gehoert auf die Maus, nicht auf Pfeiltasten.
		// Ohne sie musste man die Kamera Grad fuer Grad ertasten.
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		PC->GetInputMouseDelta(MouseX, MouseY);
		OrbitOffset.Yaw += MouseX * MouseSensitivity;
		OrbitOffset.Pitch += MouseY * MouseSensitivity;

		// Pfeiltasten bleiben als Ersatz erhalten.
		const float Turn = OrbitTurnRate * DeltaTime;
		if (PC->IsInputKeyDown(EKeys::Left))  { OrbitOffset.Yaw -= Turn; }
		if (PC->IsInputKeyDown(EKeys::Right)) { OrbitOffset.Yaw += Turn; }
		if (PC->IsInputKeyDown(EKeys::Up))    { OrbitOffset.Pitch += Turn; }
		if (PC->IsInputKeyDown(EKeys::Down))  { OrbitOffset.Pitch -= Turn; }
		OrbitOffset.Pitch = FMath::Clamp(OrbitOffset.Pitch, -80.0f, 80.0f);
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

	FRotator Desired = FRotator(FollowPitchOffset, 0.0f, 0.0f);
	if (CameraMode == EWiesbadenVehicleCameraMode::Orbit)
	{
		Desired = FRotator(OrbitOffset.Pitch, OrbitOffset.Yaw, 0.0f);
	}

	const FRotator Current = SpringArm->GetRelativeRotation();
	const float Response = FMath::Clamp(CameraResponse, 0.01f, 100.0f);
	const FRotator Next(
		FMath::FInterpTo(Current.Pitch, Desired.Pitch, DeltaTime, Response),
		FMath::FInterpTo(Current.Yaw, Desired.Yaw, DeltaTime, Response),
		FMath::FInterpTo(Current.Roll, Desired.Roll, DeltaTime, Response));
	SpringArm->SetRelativeRotation(Next);
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
