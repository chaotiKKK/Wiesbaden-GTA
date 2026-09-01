// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenVehicleTestHarness.h"

#include "WiesbadenReal.h"
#include "Vehicles/WiesbadenHelicopter.h"

UWiesbadenVehicleTestHarness::UWiesbadenVehicleTestHarness()
{
	PrimaryComponentTick.bCanEverTick = true;
}

AWiesbadenHelicopter* UWiesbadenVehicleTestHarness::Heli() const
{
	return Cast<AWiesbadenHelicopter>(GetOwner());
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

void UWiesbadenVehicleTestHarness::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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
