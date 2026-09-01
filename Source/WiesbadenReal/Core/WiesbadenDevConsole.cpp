// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Core/WiesbadenDevConsole.h"

#include "Core/WiesbadenDevActions.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "World/WiesbadenCitySubsystem.h"

APawn* UWiesbadenDevConsole::LocalPawn() const
{
	if (const UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			return PC->GetPawn();
		}
	}
	return nullptr;
}

void UWiesbadenDevConsole::WbTeleport(int32 Ziel)
{
	if (APawn* Pawn = LocalPawn())
	{
		const EWiesbadenDevTeleport Target =
			static_cast<EWiesbadenDevTeleport>(FMath::Clamp(Ziel, 0, 2));
		Pawn->SetActorLocation(FWiesbadenDevActions::TeleportSpawnCm(Target),
			/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UWiesbadenDevConsole::WbResetVehicle()
{
	if (APawn* Pawn = LocalPawn())
	{
		const FTransform Auf = FWiesbadenDevActions::UprightTransform(Pawn->GetActorTransform());
		Pawn->SetActorLocationAndRotation(Auf.GetLocation(), Auf.Rotator(),
			/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UWiesbadenDevConsole::WbTraffic(int32 An)
{
	if (const UWorld* World = GetWorld())
	{
		if (UWiesbadenCitySubsystem* City = World->GetSubsystem<UWiesbadenCitySubsystem>())
		{
			City->TrafficSimulation.Settings.TrafficDensity = (An != 0) ? 0.5f : 0.0f;
		}
	}
}
