// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenVehicleTestHarness.h"

#include "WiesbadenReal.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
#include "Vehicles/WiesbadenVehicleControl.h"

UWiesbadenVehicleTestHarness::UWiesbadenVehicleTestHarness()
{
	PrimaryComponentTick.bCanEverTick = true;
}

IWiesbadenHeliControl* UWiesbadenVehicleTestHarness::HeliControl() const
{
	return Cast<IWiesbadenHeliControl>(GetOwner());
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
	bDriveReverse = FParse::Param(FCommandLine::Get(), TEXT("WbDriveReverse"));
	bDriveCornerBrake = FParse::Param(FCommandLine::Get(), TEXT("WbDriveKurvenbremsung"));
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
	IWiesbadenHeliControl* H = HeliControl();
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

		// Rotor-/Mast-/Kamera-Telemetrie je Bild einsammeln; die Ausgabe folgt
		// unten im Sekundentakt direkt hinter der Flugzeile.
		AccumulateHeliTelemetry(DeltaTime, H->SampleRotorMast());

		const int32 Second = FMath::CeilToInt(FlyElapsed);
		if (Second != FlyLastSecond)
		{
			FlyLastSecond = Second;
			UE_LOG(LogWbVehicles, Log,
				TEXT("WbDev Flug t=%.0f: Hoehe %.0f m, Vario %+.1f m/s, Fahrt %.0f km/h."),
				FlyElapsed, H->GetAltitudeMeters(), H->GetVerticalSpeedMs(), H->GetSpeedKmh());
			LogHeliTelemetry(*H, FlyElapsed);
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

void UWiesbadenVehicleTestHarness::AccumulateHeliTelemetry(float DeltaTime, const FWiesbadenHeliMastSample& Sample)
{
	TelMaxMainHubOffsetCm = FMath::Max(TelMaxMainHubOffsetCm, Sample.MainHubOffsetCm);
	TelMaxLowerHubOffsetCm = FMath::Max(TelMaxLowerHubOffsetCm, Sample.LowerHubOffsetCm);
	TelMaxMainBladeOffsetCm = FMath::Max(TelMaxMainBladeOffsetCm, Sample.MainBladeOffsetCm);
	TelMaxLowerBladeOffsetCm = FMath::Max(TelMaxLowerBladeOffsetCm, Sample.LowerBladeOffsetCm);
	TelMaxMainAxisResidualCm = FMath::Max(TelMaxMainAxisResidualCm, Sample.MainAxisResidualCm);
	TelMaxLowerAxisResidualCm = FMath::Max(TelMaxLowerAxisResidualCm, Sample.LowerAxisResidualCm);
	TelMaxMastTiltDeg = FMath::Max(TelMaxMastTiltDeg, Sample.MastTiltDeg);
	TelMaxMainSpinTiltDeg = FMath::Max(TelMaxMainSpinTiltDeg, Sample.MainSpinTiltDeg);
	TelMaxLowerSpinTiltDeg = FMath::Max(TelMaxLowerSpinTiltDeg, Sample.LowerSpinTiltDeg);

	// Drehrate aus den Drehlagen. Nennrate ist RPM * 6 (350 U/min -> 2100 Grad/s),
	// bei 60 FPS also ~35 Grad je Bild. Ein Bild mit Riesenschritt ist ein
	// Streaming-Hitch und wird VERWORFEN, statt die Rate zu verfaelschen -
	// sonst kaeme bei 2100 Grad/s ein Vorzeichen- oder Betragsfehler heraus.
	if (bTelHasPrevAzimuth)
	{
		const float StepMain = FMath::FindDeltaAngleDegrees(TelPrevMainAzimuth, Sample.MainAzimuthDeg);
		const float StepLower = FMath::FindDeltaAngleDegrees(TelPrevLowerAzimuth, Sample.LowerAzimuthDeg);
		const float MaxStep = FMath::Max(FMath::Abs(StepMain), FMath::Abs(StepLower));
		if (MaxStep < 120.0f)
		{
			TelMainRateSum += StepMain;
			TelLowerRateSum += StepLower;
			TelRateDt += DeltaTime;
			TelRateFrames++;

			// Blattstern-Mitte nur mitzaehlen, wenn sich der Stern WIRKLICH dreht:
			// bei stehendem Rotor ist der Bounding-Box-Anker ein einzelner
			// Drehwinkel und laege bis 0,25 * Rotorradius neben der Mitte - das
			// waere ein Fehlalarm, kein Geometriefehler.
			if (MaxStep > 2.0f)
			{
				TelMainBladeCentreSum += Sample.MainBladeCentreInBodyCm;
				TelLowerBladeCentreSum += Sample.LowerBladeCentreInBodyCm;
				TelBladeCentreSamples++;
			}
		}
		else
		{
			TelRateSkipped++;
		}
	}
	TelPrevMainAzimuth = Sample.MainAzimuthDeg;
	TelPrevLowerAzimuth = Sample.LowerAzimuthDeg;
	bTelHasPrevAzimuth = true;
}

static const TCHAR* WbCameraModeName(EWiesbadenVehicleCameraMode Mode)
{
	switch (Mode)
	{
	case EWiesbadenVehicleCameraMode::Orbit:   return TEXT("Orbit");
	case EWiesbadenVehicleCameraMode::Cockpit: return TEXT("Cockpit");
	default:                                   return TEXT("Follow");
	}
}

void UWiesbadenVehicleTestHarness::LogHeliTelemetry(const IWiesbadenHeliControl& Heli, float ElapsedSeconds)
{
	const AActor* Owner = GetOwner();
	const FVector OwnerLoc = Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
	const FRotator OwnerRot = Owner ? Owner->GetActorRotation() : FRotator::ZeroRotator;

	// Soll-Drehung aus der Physik-Drehzahl: U/min * 360/60 = * 6.
	const float ExpectedRate = Heli.GetMainRotorRpm() * 6.0f;
	const float MainRate = (TelRateDt > 0.0f) ? TelMainRateSum / TelRateDt : 0.0f;
	const float LowerRate = (TelRateDt > 0.0f) ? TelLowerRateSum / TelRateDt : 0.0f;

	// Queranteil der mittleren Blattstern-Mitte (z = Lage auf der Stange, gewollt):
	// 0 cm heisst, der Stern laeuft um die Mastachse; ein Versatz waere die
	// Kreisbahn. Ohne gedrehte Bilder (stehender Rotor) ist der Wert NICHT
	// messbar und wird als -1 ausgewiesen.
	const FVector MainCentre = (TelBladeCentreSamples > 0)
		? TelMainBladeCentreSum / TelBladeCentreSamples : FVector::ZeroVector;
	const FVector LowerCentre = (TelBladeCentreSamples > 0)
		? TelLowerBladeCentreSum / TelBladeCentreSamples : FVector::ZeroVector;
	const float BladeCentreCm = (TelBladeCentreSamples > 0)
		? static_cast<float>(FVector2D(MainCentre.X, MainCentre.Y).Size()) : -1.0f;
	const float LowerBladeCentreCm = (TelBladeCentreSamples > 0)
		? static_cast<float>(FVector2D(LowerCentre.X, LowerCentre.Y).Size()) : -1.0f;

	UE_LOG(LogWbVehicles, Log,
		TEXT("WbDev Mast t=%.0f: RPM %.0f (Soll-Drehung %+.0f Grad/s) | gemessen oben %+.0f / unten %+.0f Grad/s ")
		TEXT("aus %d Bildern (%d verworfen) | Naben %.1f/%.1f cm ab Mastachse, ")
		TEXT("Achsenkorrektur %.1f/%.1f cm, Drehpunkt der Scheibe %.1f/%.1f cm neben der Stange, ")
		TEXT("Stange %.2f Grad, Blattachsen %.2f/%.2f Grad | Blattstern-Mitte quer %.1f/%.1f cm (Mittel aus %d Bildern)"),
		ElapsedSeconds, Heli.GetMainRotorRpm(), ExpectedRate, MainRate, LowerRate,
		TelRateFrames, TelRateSkipped,
		TelMaxMainHubOffsetCm, TelMaxLowerHubOffsetCm, TelMaxMainBladeOffsetCm, TelMaxLowerBladeOffsetCm,
		TelMaxMainAxisResidualCm, TelMaxLowerAxisResidualCm,
		TelMaxMastTiltDeg, TelMaxMainSpinTiltDeg, TelMaxLowerSpinTiltDeg,
		BladeCentreCm, LowerBladeCentreCm, TelBladeCentreSamples);

	// Kamera: Modus aus dem (familien-generischen) Kamera-Rig, Blicklage aus dem
	// PlayerCameraManager - das ist die Kamera, die TATSAECHLICH gerendert wird.
	const UWiesbadenVehicleCameraComponent* Camera = Owner
		? Owner->FindComponentByClass<UWiesbadenVehicleCameraComponent>() : nullptr;
	const EWiesbadenVehicleCameraMode Mode = Camera
		? Camera->GetCameraMode() : EWiesbadenVehicleCameraMode::Follow;

	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	const APlayerController* PC = (Owner && Owner->GetWorld())
		? Owner->GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		CamLoc = PC->PlayerCameraManager->GetCameraLocation();
		CamRot = PC->PlayerCameraManager->GetCameraRotation();
	}

	UE_LOG(LogWbVehicles, Log,
		TEXT("WbDev Kamera t=%.0f: %s, Abstand %.0f cm, Blick Nick %+.1f / Roll %+.1f Grad, Rumpf Nick %+.1f / Roll %+.1f Grad, ")
		TEXT("Gierfehler Blick-Rumpf %+.1f Grad"),
		ElapsedSeconds, WbCameraModeName(Mode), (CamLoc - OwnerLoc).Size(),
		CamRot.Pitch, CamRot.Roll, OwnerRot.Pitch, OwnerRot.Roll,
		FMath::FindDeltaAngleDegrees(OwnerRot.Yaw, CamRot.Yaw));

	// Sekunde abgeschlossen: Summen/Maxima fuer die naechste Zeile zuruecksetzen.
	// Die Drehlage (TelPrev*, bTelHasPrevAzimuth) bleibt - sie ist ein Zustand.
	TelMainRateSum = 0.0f;
	TelLowerRateSum = 0.0f;
	TelRateDt = 0.0f;
	TelRateFrames = 0;
	TelRateSkipped = 0;
	TelMaxMainHubOffsetCm = 0.0f;
	TelMaxLowerHubOffsetCm = 0.0f;
	TelMaxMainBladeOffsetCm = 0.0f;
	TelMaxMainAxisResidualCm = 0.0f;
	TelMaxLowerAxisResidualCm = 0.0f;
	TelMaxLowerBladeOffsetCm = 0.0f;
	TelMainBladeCentreSum = FVector::ZeroVector;
	TelLowerBladeCentreSum = FVector::ZeroVector;
	TelBladeCentreSamples = 0;
	TelMaxMastTiltDeg = 0.0f;
	TelMaxMainSpinTiltDeg = 0.0f;
	TelMaxLowerSpinTiltDeg = 0.0f;
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

	// Fahrprofil in vier Phasen ueber die echte Fahrphysik:
	//   Beschleunigen (0-40 %): Vollgas geradeaus -> Tempo (und Anfahr-Radspin)
	//   Lenken re (40-60 %):    Vollgas + Lenk-Sweep rechts
	//   Lenken li (60-80 %):    Vollgas + Lenk-Sweep links
	//   Bremsen (80-100 %):     Gas weg, voll bremsen -> Blockieren
	//                           (-WbDriveKurvenbremsung: Linkslenkung bleibt -
	//                           Vollbremsung mitten in der Kurve)
	// So weist der Rauchtest Laengsdynamik UND Lenkung nach (Max ueber den Lauf);
	// die Bremsphase macht Radspin/Blockieren fuer die Reifen-Effekte (Quietschen
	// + Bremsspuren) im Fahrlauf sicht- und hoerbar.
	DriveElapsed += DeltaTime;
	const float Frac = DriveElapsed / DriveDuration;

	FWiesbadenCarControl Control;
	Control.bReverse = bDriveReverse;
	if (Frac < 0.40f)      { Control.Throttle = 1.0f; Control.Steering = 0.0f; }
	else if (Frac < 0.60f) { Control.Throttle = 1.0f; Control.Steering = 0.6f; }
	else if (Frac < 0.80f) { Control.Throttle = 1.0f; Control.Steering = -0.6f; }
	else                   { Control.Throttle = 0.0f; Control.Brake = 1.0f; Control.Steering = bDriveCornerBrake ? -0.6f : 0.0f; }
	Ctrl->SetExternalControl(Control);

	const int32 Second = FMath::CeilToInt(DriveElapsed);
	if (Second != DriveLastSecond)
	{
		DriveLastSecond = Second;
		// Kursaenderung wrap-sicher gegen den Startkurs (FindDeltaAngle: -180..180).
		const float HeadingDelta = FMath::FindDeltaAngleDegrees(DriveStartYaw, Owner->GetActorRotation().Yaw);
		// Drehzahl mitloggen: beim Anfahr-Radspin flart sie ueber die aus dem Tempo
		// abgeleitete Basis (Antriebsschlupf-Drehzahlflare) - im Log als hohe U/min
		// bei noch niedrigem Tempo sichtbar.
		UE_LOG(LogWbVehicles, Log,
			TEXT("WbDev Fahrt t=%.0f: Tempo %.0f km/h, Drehzahl %.0f U/min, Kursaenderung %+.0f Grad, Gang %d."),
			DriveElapsed, Ctrl->GetSpeedKmh(), Ctrl->GetEngineRpm(), HeadingDelta, Ctrl->GetGear());
	}
	if (DriveElapsed >= DriveDuration)
	{
		UE_LOG(LogWbVehicles, Log, TEXT("WbDev Fahren fertig."));
		Ctrl->ClearExternalControl();
	}
}
