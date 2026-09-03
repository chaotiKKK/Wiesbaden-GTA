// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Core/WiesbadenPlayerController.h"

#include "WiesbadenReal.h"
#include "Core/WiesbadenDevActions.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenHelicopter.h"
#include "Vehicles/WiesbadenHelicopterAutopilot.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
#include "Vehicles/WiesbadenVehicleTestHarness.h"
#include "World/WiesbadenCitySubsystem.h"

void AWiesbadenPlayerController::WbTeleport(int32 Ziel)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbTeleport %d erkannt, aber kein besessener Pawn."), Ziel);
		return;
	}
	const int32 ZielClamped = FMath::Clamp(Ziel, 0, 2);
	if (Ziel != ZielClamped)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbTeleport %d ausserhalb 0-2 - auf Ziel %d begrenzt."), Ziel, ZielClamped);
	}
	const EWiesbadenDevTeleport Target = static_cast<EWiesbadenDevTeleport>(ZielClamped);
	const FVector Von = ControlledPawn->GetActorLocation();
	ControlledPawn->SetActorLocation(FWiesbadenDevActions::TeleportSpawnCm(Target),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	const FVector Nach = ControlledPawn->GetActorLocation();
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbTeleport %d ausgefuehrt: von (%.0f,%.0f,%.0f) nach (%.0f,%.0f,%.0f), Distanz %.0f cm."),
		ZielClamped, Von.X, Von.Y, Von.Z, Nach.X, Nach.Y, Nach.Z, FVector::Dist(Von, Nach));
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

void AWiesbadenPlayerController::WbHealth()
{
	const UWorld* World = GetWorld();
	const UWiesbadenCitySubsystem* City = World ? World->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	if (!City)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHealth erkannt, aber kein City-Subsystem."));
		return;
	}
	const FWiesbadenHealthReport Report = City->BuildHealthReport();
	const FString Json = Report.ToJson();
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Logs") / TEXT("WbHealth.json");
	FFileHelper::SaveStringToFile(Json, *Path);
	// Die Verdikte stecken im JSON ("healthy"/"warnings") - eine Zeile genuegt.
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHealth: %s"), *Json);
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHealth - Bericht nach %s geschrieben."), *Path);
}

void AWiesbadenPlayerController::WbCam(int32 Modus)
{
	APawn* ControlledPawn = GetPawn();
	UWiesbadenVehicleCameraComponent* Cam = ControlledPawn
		? ControlledPawn->FindComponentByClass<UWiesbadenVehicleCameraComponent>() : nullptr;
	if (!Cam)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbCam %d erkannt, aber kein Fahrzeug mit Kamera."), Modus);
		return;
	}
	const int32 ModusClamped = FMath::Clamp(Modus, 0, 2);
	if (Modus != ModusClamped)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbCam %d ausserhalb 0-2 - auf %d begrenzt."), Modus, ModusClamped);
	}
	Cam->SetCameraMode(static_cast<EWiesbadenVehicleCameraMode>(ModusClamped));
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbCam %d gesetzt (0=Follow,1=Orbit,2=Cockpit)."), ModusClamped);
}

void AWiesbadenPlayerController::WbHeli()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AWiesbadenHelicopter> It(World); It; ++It)
	{
		Possess(*It);
		UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeli - Helikopter %s uebernommen."), *It->GetName());
		return;
	}
	UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeli - kein Helikopter in der Welt."));
}

void AWiesbadenPlayerController::WbNudge(int32 NickGrad, int32 RollGrad)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbNudge erkannt, aber kein besessener Pawn."));
		return;
	}
	const FRotator Vorher = ControlledPawn->GetActorRotation();
	const FRotator Nachher(Vorher.Pitch + NickGrad, Vorher.Yaw, Vorher.Roll + RollGrad);
	ControlledPawn->SetActorRotation(Nachher, ETeleportType::TeleportPhysics);
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbNudge %d/%d: Nick/Roll %.0f/%.0f -> %.0f/%.0f."),
		NickGrad, RollGrad, Vorher.Pitch, Vorher.Roll, Nachher.Pitch, Nachher.Roll);
}

// Test-Harness auf dem besessenen Helikopter holen oder anlegen.
//
// Die Harness-Komponente existiert im normalen Spiel nicht - sie wird erst hier,
// beim ersten Dev-Befehl, auf dem Pawn erzeugt. So bleibt die Fahrzeugklasse frei
// von Test-Code.
static UWiesbadenVehicleTestHarness* GetOrAddHarness(AActor* Owner)
{
	UWiesbadenVehicleTestHarness* Harness =
		Owner->FindComponentByClass<UWiesbadenVehicleTestHarness>();
	if (!Harness)
	{
		Harness = NewObject<UWiesbadenVehicleTestHarness>(Owner);
		Harness->RegisterComponent();
	}
	return Harness;
}

void AWiesbadenPlayerController::WbHeliYaw(int32 Sekunden)
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliYaw erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	GetOrAddHarness(Heli)->StartYawProbe(static_cast<float>(Sekunden));
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliYaw - Gierprobe fuer %d s gestartet."), Sekunden);
}

void AWiesbadenPlayerController::WbHeliFly(int32 Sekunden)
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliFly erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	GetOrAddHarness(Heli)->StartFlightProfile(static_cast<float>(Sekunden));
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliFly - Flugprofil fuer %d s gestartet."), Sekunden);
}

void AWiesbadenPlayerController::WbDrive(int32 Sekunden)
{
	AWiesbadenCar* Car = Cast<AWiesbadenCar>(GetPawn());
	if (!Car)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbDrive erkannt, aber kein Fahrzeug besessen."));
		return;
	}
	GetOrAddHarness(Car)->StartDriveProfile(static_cast<float>(Sekunden));
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbDrive - Fahrprofil fuer %d s gestartet."), Sekunden);
}

// Autopilot-Komponente on-demand am Helikopter anlegen (wie der Test-Harness):
// im normalen Spiel existiert sie nicht, bis ein Dev-Befehl sie anfordert.
static UWiesbadenHelicopterAutopilot* GetOrAddAutopilot(AWiesbadenHelicopter* Heli)
{
	UWiesbadenHelicopterAutopilot* Autopilot =
		Heli->FindComponentByClass<UWiesbadenHelicopterAutopilot>();
	if (!Autopilot)
	{
		Autopilot = NewObject<UWiesbadenHelicopterAutopilot>(Heli);
		Autopilot->RegisterComponent();
	}
	return Autopilot;
}

void AWiesbadenPlayerController::WbHeliGoto(int32 DeltaXMeter, int32 DeltaYMeter, int32 DeltaZMeter)
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliGoto erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	const FVector Target = Heli->GetActorLocation()
		+ FVector(DeltaXMeter, DeltaYMeter, DeltaZMeter) * 100.0;
	GetOrAddAutopilot(Heli)->FlyTo(Target);
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbHeliGoto - Autopilot fliegt zu (%d, %d, %d) m relativ."),
		DeltaXMeter, DeltaYMeter, DeltaZMeter);
}

void AWiesbadenPlayerController::WbHeliHover()
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliHover erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	GetOrAddAutopilot(Heli)->HoldPosition();
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliHover - Autopilot haelt die Position."));
}

void AWiesbadenPlayerController::WbHeliOff()
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliOff erkannt, aber kein Helikopter besessen."));
		return;
	}
	// Nur einen VORHANDENEN Autopiloten abschalten - keinen erst anlegen, um ihn
	// gleich wieder zu loesen.
	if (UWiesbadenHelicopterAutopilot* Autopilot = Heli->FindComponentByClass<UWiesbadenHelicopterAutopilot>())
	{
		Autopilot->Disengage();
		UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliOff - Autopilot aus, Steuerung zurueck an Tastatur/Gamepad."));
	}
	else
	{
		UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliOff - kein Autopilot aktiv."));
	}
}
