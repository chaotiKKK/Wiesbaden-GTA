// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenHelicopterAutopilot.h"

#include "WiesbadenReal.h"
#include "Vehicles/WiesbadenHelicopter.h"

namespace
{
	// Engine-Positionen sind in cm; der Regler rechnet in m.
	constexpr float CmToM = 0.01f;
}

UWiesbadenHelicopterAutopilot::UWiesbadenHelicopterAutopilot()
{
	PrimaryComponentTick.bCanEverTick = true;
}

AWiesbadenHelicopter* UWiesbadenHelicopterAutopilot::Heli() const
{
	return Cast<AWiesbadenHelicopter>(GetOwner());
}

void UWiesbadenHelicopterAutopilot::FlyTo(const FVector& WorldTarget)
{
	Target = WorldTarget;
	Mode = EWiesbadenAutopilotMode::Goto;
	ElapsedInMode = 0.0f;
	LastLogSecond = -1;
	bArrivedLogged = false;
	IntegralErrorXY = FVector::ZeroVector;
}

void UWiesbadenHelicopterAutopilot::HoldPosition()
{
	if (const AWiesbadenHelicopter* H = Heli())
	{
		Target = H->GetActorLocation();
	}
	Mode = EWiesbadenAutopilotMode::Hold;
	ElapsedInMode = 0.0f;
	LastLogSecond = -1;
	bArrivedLogged = false;
	IntegralErrorXY = FVector::ZeroVector;
}

void UWiesbadenHelicopterAutopilot::Disengage()
{
	Mode = EWiesbadenAutopilotMode::Off;
	IntegralErrorXY = FVector::ZeroVector;
	if (AWiesbadenHelicopter* H = Heli())
	{
		H->ClearExternalControl();
	}
}

void UWiesbadenHelicopterAutopilot::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Mode == EWiesbadenAutopilotMode::Off)
	{
		return;
	}
	AWiesbadenHelicopter* H = Heli();
	if (!H)
	{
		Mode = EWiesbadenAutopilotMode::Off;
		return;
	}
	ElapsedInMode += DeltaTime;

	// Ist- und Fehlergroessen in Metern / m/s. ACHTUNG: die ECHTE Geschwindigkeit
	// kommt aus der internen Integration des Helis - AActor::GetVelocity() ist bei
	// diesem kinematischen Pawn 0 und wuerde jede Daempfung wirkungslos machen.
	const FVector Pos = H->GetActorLocation();
	const FVector Vel = H->GetVelocityMetersPerSecond();
	const FVector ErrorCm = Target - Pos;
	const FVector ErrorXY(ErrorCm.X, ErrorCm.Y, 0.0f);
	const float DistXYm = ErrorXY.Size() * CmToM;
	const float ErrorZm = ErrorCm.Z * CmToM;
	const FVector VelXY(Vel.X, Vel.Y, 0.0f);

	FWiesbadenHeliControl ControlCmd;
	ControlCmd.bEngine = true;

	// -- Lage (zuerst, fuer die Auftriebs-Vorsteuerung): Ziel-Horizontalgeschwindig-
	//    keit zum Wegpunkt (gekappt), daraus ueber den Geschwindigkeitsfehler
	//    Nick/Roll im Rumpf-Frame. Nahe am Ziel geht die Ziel-Geschwindigkeit gegen
	//    0 -> die Restgeschwindigkeit wird weggebremst = Position halten. Ein
	//    einheitliches Gesetz fuer Anflug UND Schweben, ohne Fallunterscheidung.
	// Zielgeschwindigkeit als Bremsprofil mit drei Zonen - so rastet der Heli sanft
	// ein, statt am Mindesttempo-Boden um den Zielpunkt zu kreisen:
	//  - Ankunftsradius (<= Arrive): 0 -> reine Geschwindigkeitsdaempfung = halten.
	//  - Settle-Band (Arrive..Settle): proportional zum Abstand, OHNE Boden -> laeuft
	//    stetig gegen 0; kein Mindesttempo-Kick mehr (das war der ~15-m-Grenzzyklus).
	//  - Transit (> Settle): Mindesttempo ueber der Rotor-Totzone, gekappt.
	float DesiredSpeed;
	if (DistXYm <= ArriveRadiusMeters)
	{
		DesiredSpeed = 0.0f;
	}
	else
	{
		DesiredSpeed = FMath::Min(ApproachGain * DistXYm, MaxApproachSpeed);
		if (DistXYm > SettleRadiusMeters)
		{
			DesiredSpeed = FMath::Max(DesiredSpeed, MinApproachSpeed);
		}
	}
	const FVector DesiredVelXY = (DistXYm > KINDA_SMALL_NUMBER)
		? ErrorXY.GetSafeNormal() * DesiredSpeed
		: FVector::ZeroVector;
	const FVector VelErrorXY = DesiredVelXY - VelXY;

	FVector Forward = H->GetActorForwardVector(); Forward.Z = 0.0f; Forward = Forward.GetSafeNormal();
	FVector Right = H->GetActorRightVector(); Right.Z = 0.0f; Right = Right.GetSafeNormal();
	const float ForwardVelError = FVector::DotProduct(VelErrorXY, Forward);
	const float RightVelError = FVector::DotProduct(VelErrorXY, Right);

	// Integralterm der Lage: NUR im Settle-Band aufsummieren (sonst Wind-up beim
	// langen Transit), Betrag deckeln (Anti-Windup). Er hebt das Kommando ueber die
	// Rotor-Totzone und schliesst den stationaeren Restversatz, den das reine
	// P-Kommando (zu klein) stehen liesse.
	if (DistXYm <= SettleRadiusMeters)
	{
		// Bedingte Integration: nur aufladen, solange der Heli nahezu steht (die
		// Totzone haelt ihn) - sobald er sich bewegt, nicht weiter aufladen, sonst
		// schoebe der Integrator ueber das Ziel (Schwingung). Plus sanftes Auslaufen.
		if (VelXY.Size() < IntegralFreezeSpeed)
		{
			IntegralErrorXY += FVector(ErrorCm.X, ErrorCm.Y, 0.0f) * CmToM * DeltaTime;
		}
		IntegralErrorXY *= FMath::Max(0.0f, 1.0f - DeltaTime / IntegralLeakTau);
		IntegralErrorXY = IntegralErrorXY.GetClampedToMaxSize(TiltIntegralMaxMeterSeconds);
	}
	else
	{
		IntegralErrorXY = FVector::ZeroVector;
	}
	const float ForwardIntegral = FVector::DotProduct(IntegralErrorXY, Forward);
	const float RightIntegral = FVector::DotProduct(IntegralErrorXY, Right);

	// Nase runter (+Pitch) beschleunigt vorwaerts; +Roll nach rechts. P auf dem
	// Geschwindigkeitsfehler + I auf dem Positionsfehler.
	ControlCmd.Pitch = FMath::Clamp(TiltGain * ForwardVelError + TiltIntegralGain * ForwardIntegral, -MaxTilt, MaxTilt);
	ControlCmd.Roll = FMath::Clamp(TiltGain * RightVelError + TiltIntegralGain * RightIntegral, -MaxTilt, MaxTilt);

	// -- Hoehe: Rueckfuehrung (Hoehenfehler -> Ziel-Steigrate -> Kollektiv) PLUS
	//    Vorsteuerung gegen den Auftriebsverlust durch die Rotorneigung. Ohne die
	//    Vorsteuerung sackt der Heli im Schnellflug ab, obwohl das Kollektiv schon
	//    voll steht.
	const float DesiredVz = FMath::Clamp(ClimbGain * ErrorZm, -MaxClimbRate, MaxClimbRate);
	const float CollectiveFeedback = CollectiveGain * (DesiredVz - Vel.Z);
	const float TiltFeedforward =
		TiltLiftCompensation * (FMath::Abs(ControlCmd.Pitch) + FMath::Abs(ControlCmd.Roll));
	ControlCmd.Collective = FMath::Clamp(CollectiveFeedback + TiltFeedforward, -1.0f, 1.0f);

	// -- Gieren: Nase zum Ziel drehen, solange es weit weg ist (nahe dran wuerde
	//    es nur zappeln; das Nick/Roll-Gesetz fliegt ohnehin in jede Richtung). -
	if (DistXYm > FaceTargetMinDistanceMeters)
	{
		const float CurrentHeadingDeg = FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
		const float TargetHeadingDeg = FMath::RadiansToDegrees(FMath::Atan2(ErrorXY.Y, ErrorXY.X));
		const float HeadingErrorDeg = FMath::FindDeltaAngleDegrees(CurrentHeadingDeg, TargetHeadingDeg);
		ControlCmd.Yaw = FMath::Clamp(YawGain * HeadingErrorDeg, -1.0f, 1.0f);
	}

	H->SetExternalControl(ControlCmd);

	// -- Fortschritts-Log (Sekundentakt) + einmaliges "erreicht". --------------
	const float Dist3Dm = ErrorCm.Size() * CmToM;
	const int32 Second = FMath::FloorToInt(ElapsedInMode);
	if (Second != LastLogSecond)
	{
		LastLogSecond = Second;
		// Ehrliches Modus-Label: Goto benutzt EIN Gesetz fuer Anflug+Schweben, der
		// Modus bleibt technisch "Goto" auch nach Ankunft. Ohne Zusatz sieht der
		// Leser dauerhaft "Anflug", obwohl der Heli laengst haelt - deshalb nach
		// dem Wegpunkt "(angekommen, haelt)" anhaengen.
		const TCHAR* const ModusText = (Mode == EWiesbadenAutopilotMode::Goto)
			? (bArrivedLogged ? TEXT("Anflug (angekommen, haelt)") : TEXT("Anflug"))
			: TEXT("Halten");
		UE_LOG(LogWbVehicles, Log,
			TEXT("WbDev Autopilot t=%.0f: Abstand %.0f m (horiz %.0f m, Hoehe %+.0f m), Tempo %.0f km/h, Modus %s."),
			ElapsedInMode, Dist3Dm, DistXYm, ErrorZm, H->GetAirspeedKmh(),
			ModusText);
	}

	// Ankunft EINMAL je Ziel melden (bArrivedLogged wird nur von FlyTo/HoldPosition
	// zurueckgesetzt). Kein Zuruecksetzen bei kleinem Abtreiben am Radius-Rand,
	// sonst wiederholt sich "Wegpunkt erreicht" beim Zappeln um die Grenze.
	const bool bArrived = (DistXYm <= ArriveRadiusMeters)
		&& (FMath::Abs(ErrorZm) <= ArriveAltToleranceMeters);
	if (bArrived && !bArrivedLogged)
	{
		bArrivedLogged = true;
		UE_LOG(LogWbVehicles, Log,
			TEXT("WbDev Autopilot: Wegpunkt erreicht nach %.1f s (Abstand %.1f m) - halte Position."),
			ElapsedInMode, Dist3Dm);
	}
}
