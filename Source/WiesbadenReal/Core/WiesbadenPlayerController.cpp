// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Core/WiesbadenPlayerController.h"

#include "WiesbadenReal.h"
#include "Core/WiesbadenDevActions.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "World/WiesbadenCitySubsystem.h"

void AWiesbadenPlayerController::WbTeleport(int32 Ziel)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbTeleport %d erkannt, aber kein besessener Pawn."), Ziel);
		return;
	}
	const EWiesbadenDevTeleport Target =
		static_cast<EWiesbadenDevTeleport>(FMath::Clamp(Ziel, 0, 2));
	const FVector Von = ControlledPawn->GetActorLocation();
	ControlledPawn->SetActorLocation(FWiesbadenDevActions::TeleportSpawnCm(Target),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	const FVector Nach = ControlledPawn->GetActorLocation();
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbTeleport %d ausgefuehrt: von (%.0f,%.0f,%.0f) nach (%.0f,%.0f,%.0f), Distanz %.0f cm."),
		Ziel, Von.X, Von.Y, Von.Z, Nach.X, Nach.Y, Nach.Z, FVector::Dist(Von, Nach));
}

void AWiesbadenPlayerController::WbResetVehicle()
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbResetVehicle erkannt, aber kein besessener Pawn."));
		return;
	}
	const FRotator Vorher = ControlledPawn->GetActorRotation();
	const FTransform Auf = FWiesbadenDevActions::UprightTransform(ControlledPawn->GetActorTransform());
	ControlledPawn->SetActorLocationAndRotation(Auf.GetLocation(), Auf.Rotator(),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	const FRotator Nachher = ControlledPawn->GetActorRotation();
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbResetVehicle ausgefuehrt: Nick/Roll vorher (%.1f/%.1f) -> nachher (%.1f/%.1f), Yaw %.1f bleibt."),
		Vorher.Pitch, Vorher.Roll, Nachher.Pitch, Nachher.Roll, Nachher.Yaw);
}

void AWiesbadenPlayerController::WbTraffic(int32 An)
{
	const UWorld* World = GetWorld();
	UWiesbadenCitySubsystem* City = World ? World->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	if (!City)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbTraffic %d erkannt, aber kein City-Subsystem."), An);
		return;
	}
	const float Vorher = City->TrafficSimulation.Settings.TrafficDensity;
	City->TrafficSimulation.Settings.TrafficDensity = (An != 0) ? 0.5f : 0.0f;
	const float Nachher = City->TrafficSimulation.Settings.TrafficDensity;
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbTraffic %d ausgefuehrt: Dichte %.2f -> %.2f."),
		An, Vorher, Nachher);
}
