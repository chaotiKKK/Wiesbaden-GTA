// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenVehicleTestHarness.h"

#include "WiesbadenReal.h"
#include "GameFramework/Actor.h"
#include "Vehicles/WiesbadenVehicleControl.h"
#include "Vehicles/WiesbadenHelicopter.h"

UWiesbadenVehicleTestHarness::UWiesbadenVehicleTestHarness()
{
	PrimaryComponentTick.bCanEverTick = true;
}

AWiesbadenHelicopter* UWiesbadenVehicleTestHarness::Heli() const
{
	return Cast<AWiesbadenHelicopter>(GetOwner());
}

IWiesbadenVehicleControl* UWiesbadenVehicleTestHarness::VehicleControl() const
{
	return Cast<IWiesbadenVehicleControl>(GetOwner());
}

void UWiesbadenVehicleTestHarness::StartYawProbe(float Seconds)
{
	YawDuration = FMath::Max(Seconds, 0.1f);
	YawElapsed = 0.0f;
	YawLastSecond = -1;
}

void UWiesbadenVehicleTestHarness::StartFlightProfile(float Seconds)
{
	FlyDuration = FMath::Max(Seconds, 0.1f);
	FlyElapsed = 0.0f;
	FlyLastSecond = -1;
}

void UWiesbadenVehicleTestHarness::StartDriveProfile(float Seconds)
{
	DriveDuration = FMath::Max(Seconds, 0.1f);
	DriveElapsed = 0.0f;
	DriveLastSecond = -1;
	// Startkurs merken: die Kursaenderung wird wrap-sicher dagegen gemessen.
	// Kurs kommt aus der Actor-Ebene (GetOwner), Steuerung aus der Naht.
	DriveStartYaw = GetOwner() ? GetOwner()->GetActorRotation().Yaw : 0.0f;
}

void UWiesbadenVehicleTestHarness::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TickHeliProfiles(DeltaTime);
	TickDriveProfile(DeltaTime);
}

void UWiesbadenVehicleTestHarness::TickHeliProfiles(float DeltaTime)
{
	const bool bYaw = (YawDuration > 0.0f && YawElapsed < YawDuration);
	const bool bFly = (FlyDuration > 0.0f && FlyElapsed < FlyDuration);
	if (!bYaw && !bFly)
	{
		return;
	}
	AWiesbadenHelicopter* H = Heli();
	if (!H)
	{
		YawDuration = FlyDuration = 0.0f;
		return;
	}

	// Beide Profile in EINEN Steuerbefehl zusammenfuehren (verschiedene Achsen).
	FWiesbadenHeliControl Control;
	Control.bEngine = true;

	if (bFly)
	{
		// Flugprofil in vier Phasen:
		//   Steigen (0-28 %): Kollektiv hoch  -> Hoehe steigt, Vario positiv
		//   Schweben (28-50 %): Hebel neutral -> Hoehe halten, Vario ~0
		//   Marsch  (50-75 %): Nase runter    -> Fahrt steigt
		//   Sinken  (75-100 %): Kollektiv runter -> Hoehe faellt, Vario negativ
		FlyElapsed += DeltaTime;
		const float Frac = FlyElapsed / FlyDuration;
		if (Frac < 0.28f)      { Control.Collective = 0.6f;  Control.Pitch = 0.0f; }
		else if (Frac < 0.50f) { Control.Collective = 0.0f;  Control.Pitch = 0.0f; }
		else if (Frac < 0.75f) { Control.Collective = 0.0f;  Control.Pitch = 0.6f; }
		else                   { Control.Collective = -0.6f; Control.Pitch = 0.2f; }

		const int32 Second = FMath::CeilToInt(FlyElapsed);
		if (Second != FlyLastSecond)
		{
			FlyLastSecond = Second;
			UE_LOG(LogWbVehicles, Log,
				TEXT("WbDev Flug t=%.0f: Hoehe %.0f m, Vario %+.1f m/s, Fahrt %.0f km/h."),
				FlyElapsed, H->GetAltitudeMeters(), H->GetVerticalSpeedMs(), H->GetAirspeedKmh());
		}
		if (FlyElapsed >= FlyDuration)
		{
			UE_LOG(LogWbVehicles, Log, TEXT("WbDev HeliFly fertig."));
		}
	}

	if (bYaw)
	{
		// Gemaessigtes Gierpedal; ohne Flugprofil etwas Auftrieb, damit der Rumpf
		// frei ueber Grund giert (mit Flugprofil bestimmt dessen Kollektiv den Auftrieb).
		YawElapsed += DeltaTime;
		Control.Yaw = 0.45f;
		if (!bFly)
		{
			Control.Collective = FMath::Max(Control.Collective, 0.55f);
		}

		const int32 Second = FMath::CeilToInt(YawElapsed);
		if (Second != YawLastSecond)
		{
			YawLastSecond = Second;
			UE_LOG(LogWbVehicles, Log,
				TEXT("WbDev Gierprobe t=%.0f: Kurs %.0f Grad (Gierrate %.1f Grad/s)."),
				YawElapsed, H->GetHeadingDegrees(), H->GetYawRateDegPerSec());
		}
		if (YawElapsed >= YawDuration)
		{
			UE_LOG(LogWbVehicles, Log, TEXT("WbDev HeliYaw fertig."));
		}
	}

	H->SetExternalControl(Control);

	// Beide fertig -> externe Steuerung abschalten, Tastatur uebernimmt wieder.
	if (YawElapsed >= YawDuration && FlyElapsed >= FlyDuration)
	{
		H->ClearExternalControl();
	}
}

void UWiesbadenVehicleTestHarness::TickDriveProfile(float DeltaTime)
{
	if (!(DriveDuration > 0.0f && DriveElapsed < DriveDuration))
	{
		return;
	}
	// Fahrzeug ueber die Steuernaht (Interface) UND der traegende Actor fuer den
	// Kurs. So treibt derselbe Pfad Kaefer wie ChaosCar.
	IWiesbadenVehicleControl* Ctrl = VehicleControl();
	AActor* Owner = GetOwner();
	if (!Ctrl || !Owner)
	{
		DriveDuration = 0.0f;
		return;
	}

	// Fahrprofil in zwei Phasen ueber die echte Fahrphysik:
	//   Beschleunigen (0-45 %): Vollgas geradeaus -> Tempo steigt
	//   Lenken (45-100 %): Vollgas + Lenk-Sweep rechts, dann links -> Kurs aendert sich
	// So weist der Rauchtest BEIDES nach - Laengsdynamik und Lenkung - ohne Tastatur.
	DriveElapsed += DeltaTime;
	const float Frac = DriveElapsed / DriveDuration;

	FWiesbadenCarControl Control;
	Control.Throttle = 1.0f;
	if (Frac < 0.45f)      { Control.Steering = 0.0f; }
	else if (Frac < 0.72f) { Control.Steering = 0.6f; }
	else                   { Control.Steering = -0.6f; }
	Ctrl->SetExternalControl(Control);

	const int32 Second = FMath::CeilToInt(DriveElapsed);
	if (Second != DriveLastSecond)
	{
		DriveLastSecond = Second;
		// Kursaenderung wrap-sicher gegen den Startkurs (FindDeltaAngle: -180..180).
		const float HeadingDelta = FMath::FindDeltaAngleDegrees(DriveStartYaw, Owner->GetActorRotation().Yaw);
		UE_LOG(LogWbVehicles, Log,
			TEXT("WbDev Fahrt t=%.0f: Tempo %.0f km/h, Kursaenderung %+.0f Grad, Gang %d."),
			DriveElapsed, Ctrl->GetSpeedKmh(), HeadingDelta, Ctrl->GetGear());
	}
	if (DriveElapsed >= DriveDuration)
	{
		UE_LOG(LogWbVehicles, Log, TEXT("WbDev Fahren fertig."));
		Ctrl->ClearExternalControl();
	}
}
