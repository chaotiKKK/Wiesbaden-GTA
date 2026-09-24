// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenCarLightsComponent.h"

#include "WiesbadenReal.h"

#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UWiesbadenCarLightsComponent::UWiesbadenCarLightsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

// -- Datenreine Hilfsfunktionen ---------------------------------------------

bool UWiesbadenCarLightsComponent::ComputeBlinkOn(float TimeSeconds, float FrequencyHz)
{
	if (FrequencyHz <= 0.0f)
	{
		return false;
	}

	// Negative Zeiten treten auf, wenn ein Aufrufer die Blinkzeit relativ zu
	// einem spaeteren Startzeitpunkt bildet. FMath::Fmod behaelt dabei das
	// Vorzeichen bei, was die Phase spiegeln wuerde - daher der Betrag.
	const float Period = 1.0f / FrequencyHz;
	const float Phase = FMath::Fmod(FMath::Abs(TimeSeconds), Period);

	return Phase < (Period * 0.5f);
}

EWiesbadenHeadlightMode UWiesbadenCarLightsComponent::GetNextHeadlightMode(EWiesbadenHeadlightMode Current)
{
	switch (Current)
	{
	case EWiesbadenHeadlightMode::Off:		return EWiesbadenHeadlightMode::Parking;
	case EWiesbadenHeadlightMode::Parking:	return EWiesbadenHeadlightMode::LowBeam;
	case EWiesbadenHeadlightMode::LowBeam:	return EWiesbadenHeadlightMode::HighBeam;
	case EWiesbadenHeadlightMode::HighBeam:	return EWiesbadenHeadlightMode::Off;
	default:								return EWiesbadenHeadlightMode::Off;
	}
}

EWiesbadenIndicatorMode UWiesbadenCarLightsComponent::ApplyIndicatorToggle(
	EWiesbadenIndicatorMode Current, EWiesbadenIndicatorMode Requested)
{
	if (Requested == EWiesbadenIndicatorMode::Off)
	{
		return EWiesbadenIndicatorMode::Off;
	}

	// Dieselbe Betaetigung erneut schaltet aus (Hebel rastet zurueck).
	if (Current == Requested)
	{
		return EWiesbadenIndicatorMode::Off;
	}

	// Warnblinkanlage hat Vorrang und ersetzt einen Einzelblinker; umgekehrt
	// darf ein Einzelblinker die eingeschaltete Warnblinkanlage nicht
	// stillschweigend abschalten - das entspricht dem Verhalten im Fahrzeug.
	if (Current == EWiesbadenIndicatorMode::Hazard && Requested != EWiesbadenIndicatorMode::Hazard)
	{
		return EWiesbadenIndicatorMode::Hazard;
	}

	return Requested;
}

bool UWiesbadenCarLightsComponent::IndicatorAffectsLeft(EWiesbadenIndicatorMode Mode)
{
	return Mode == EWiesbadenIndicatorMode::Left || Mode == EWiesbadenIndicatorMode::Hazard;
}

bool UWiesbadenCarLightsComponent::IndicatorAffectsRight(EWiesbadenIndicatorMode Mode)
{
	return Mode == EWiesbadenIndicatorMode::Right || Mode == EWiesbadenIndicatorMode::Hazard;
}

// -- Aufbau -----------------------------------------------------------------

USpotLightComponent* UWiesbadenCarLightsComponent::MakeSpotLight(
	const TCHAR* Name, const FVector& Offset, const FRotator& Rotation)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	USpotLightComponent* Light = NewObject<USpotLightComponent>(Owner, Name);
	if (!Light)
	{
		return nullptr;
	}

	Light->SetupAttachment(this);
	Light->RegisterComponent();
	Light->SetRelativeLocation(Offset);
	Light->SetRelativeRotation(Rotation);
	Light->SetLightColor(FLinearColor(HeadlightColor));
	Light->SetAttenuationRadius(6000.0f);
	Light->SetIntensity(0.0f);
	Light->SetVisibility(false);

	// Bewegliche Lichter: das Fahrzeug faehrt, statische Schatten waeren falsch.
	Light->SetMobility(EComponentMobility::Movable);

	// Schatten kosten bei vielen Lichtquellen erheblich; nur die Scheinwerfer
	// werfen welche, weil dort der Gewinn sichtbar ist.
	Light->SetCastShadows(true);

	return Light;
}

UPointLightComponent* UWiesbadenCarLightsComponent::MakePointLight(
	const TCHAR* Name, const FVector& Offset, const FColor& Color)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner, Name);
	if (!Light)
	{
		return nullptr;
	}

	Light->SetupAttachment(this);
	Light->RegisterComponent();
	Light->SetRelativeLocation(Offset);
	Light->SetLightColor(FLinearColor(Color));
	// Alle ueber MakePointLight erzeugten Leuchten sind SIGNALleuchten
	// (Stand-, Schluss-, Brems-, Blink-, Rueckfahrlicht). Die Scheinwerfer
	// entstehen getrennt als Spotlights. Eine Signalleuchte soll gesehen
	// werden, nicht die Umgebung ausleuchten - mit den urspruenglichen 450 cm
	// faerbte das Bremslicht nachts die Fahrbahn ringsum rot.
	Light->SetAttenuationRadius(SignalLightRadiusCm);
	Light->SetIntensity(0.0f);
	Light->SetVisibility(false);
	Light->SetMobility(EComponentMobility::Movable);

	// Signalleuchten ohne Schattenwurf: sie sollen leuchten, nicht ausleuchten.
	Light->SetCastShadows(false);

	return Light;
}

UMaterialInstanceDynamic* UWiesbadenCarLightsComponent::MakeLens(
	const TCHAR* Name, const FVector& Offset, const FVector& Scale,
	const FLinearColor& Color)
{
	AActor* Owner = GetOwner();
	if (!Owner) { return nullptr; }
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Vehicles/Beetle/Restored/M_WbBeetleLamp.M_WbBeetleLamp"));
	if (!Sphere || !Material) { return nullptr; }
	UStaticMeshComponent* Lens = NewObject<UStaticMeshComponent>(Owner, Name);
	Lens->SetupAttachment(this);
	Lens->SetStaticMesh(Sphere);
	Lens->SetRelativeLocation(Offset);
	Lens->SetRelativeScale3D(Scale);
	Lens->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lens->SetCastShadow(false);
	Owner->AddInstanceComponent(Lens);
	Lens->RegisterComponent();
	UMaterialInstanceDynamic* Dynamic = Lens->CreateDynamicMaterialInstance(0, Material);
	if (Dynamic)
	{
		Dynamic->SetVectorParameterValue(TEXT("LensColor"), Color);
		Dynamic->SetScalarParameterValue(TEXT("Glow"), 0.0f);
	}
	return Dynamic;
}

void UWiesbadenCarLightsComponent::CreateLights()
{
	// Y-Vorzeichen spiegelt die Einbaulage auf die jeweilige Fahrzeugseite.
	auto Mirror = [](const FVector& V, double Side)
	{
		return FVector(V.X, V.Y * Side, V.Z);
	};

	// Scheinwerfer: leicht nach aussen und nach unten gerichtet.
	const FRotator LeftAim(-LowBeamDownwardPitch, -4.0, 0.0);
	const FRotator RightAim(-LowBeamDownwardPitch, 4.0, 0.0);

	if (USpotLightComponent* L = MakeSpotLight(TEXT("HeadlightLeft"), Mirror(HeadlightOffset, -1.0), LeftAim))
	{
		Headlights.Add(L);
	}
	if (USpotLightComponent* R = MakeSpotLight(TEXT("HeadlightRight"), Mirror(HeadlightOffset, 1.0), RightAim))
	{
		Headlights.Add(R);
	}
	// Ein weicher Rand und ein enger Kern geben dem Abblendlicht eine
	// erkennbare Fahrbahnverteilung. Fernlicht kommt als dritter, langer Kegel.
	for (const double Side : {-1.0, 1.0})
	{
		const bool bLeft = Side < 0.0;
		if (USpotLightComponent* Focus = MakeSpotLight(
			bLeft ? TEXT("BeamFocusLeft") : TEXT("BeamFocusRight"),
			Mirror(HeadlightOffset, Side), FRotator(-3.0f, bLeft ? -2.0f : 2.0f, 0.0f)))
		{
			Focus->SetAttenuationRadius(7200.0f);
			Focus->SetCastShadows(false);
			FocusedBeams.Add(Focus);
		}
		if (USpotLightComponent* Distance = MakeSpotLight(
			bLeft ? TEXT("HighBeamLeft") : TEXT("HighBeamRight"),
			Mirror(HeadlightOffset, Side), FRotator(-1.5f, 0.0f, 0.0f)))
		{
			Distance->SetAttenuationRadius(9000.0f);
			Distance->SetCastShadows(false);
			HighBeams.Add(Distance);
		}
	}

	if (UPointLightComponent* L = MakePointLight(TEXT("ParkingLeft"), Mirror(HeadlightOffset, -1.0), HeadlightColor))
	{
		ParkingLights.Add(L);
	}
	if (UPointLightComponent* R = MakePointLight(TEXT("ParkingRight"), Mirror(HeadlightOffset, 1.0), HeadlightColor))
	{
		ParkingLights.Add(R);
	}

	if (UPointLightComponent* L = MakePointLight(TEXT("TailLeft"), Mirror(TailLightOffset, -1.0), TailLightColor))
	{
		TailLights.Add(L);
	}
	if (UPointLightComponent* R = MakePointLight(TEXT("TailRight"), Mirror(TailLightOffset, 1.0), TailLightColor))
	{
		TailLights.Add(R);
	}

	if (UPointLightComponent* F = MakePointLight(TEXT("IndicatorFrontLeft"), Mirror(FrontIndicatorOffset, -1.0), IndicatorColor))
	{
		LeftIndicators.Add(F);
	}
	if (UPointLightComponent* B = MakePointLight(TEXT("IndicatorRearLeft"), Mirror(RearIndicatorOffset, -1.0), IndicatorColor))
	{
		LeftIndicators.Add(B);
	}
	if (UPointLightComponent* F = MakePointLight(TEXT("IndicatorFrontRight"), Mirror(FrontIndicatorOffset, 1.0), IndicatorColor))
	{
		RightIndicators.Add(F);
	}
	if (UPointLightComponent* B = MakePointLight(TEXT("IndicatorRearRight"), Mirror(RearIndicatorOffset, 1.0), IndicatorColor))
	{
		RightIndicators.Add(B);
	}

	ReverseLight = MakePointLight(TEXT("ReverseLight"), ReverseLightOffset, FColor(240, 240, 255));

	for (const double Side : {-1.0, 1.0})
	{
		const bool bLeft = Side < 0.0;
		if (UMaterialInstanceDynamic* Lens = MakeLens(
			bLeft ? TEXT("HeadLensLeft") : TEXT("HeadLensRight"),
			Mirror(HeadlightOffset + FVector(13.0, 0.0, 0.0), Side),
			FVector(.035, .28, .28), FLinearColor(HeadlightColor)))
		{
			HeadlightLenses.Add(Lens);
		}
		if (UMaterialInstanceDynamic* Lens = MakeLens(
			bLeft ? TEXT("TailLensLeft") : TEXT("TailLensRight"),
			Mirror(TailLightOffset - FVector(9.0, 0.0, 0.0), Side),
			FVector(.04, .22, .24), FLinearColor(TailLightColor)))
		{
			TailLenses.Add(Lens);
		}
		const FVector FrontBlink = Mirror(FrontIndicatorOffset, Side);
		const FVector RearBlink = Mirror(RearIndicatorOffset - FVector(1.0, 0.0, 0.0), Side);
		TArray<UMaterialInstanceDynamic*>& Lenses = bLeft
			? LeftIndicatorLenses : RightIndicatorLenses;
		if (UMaterialInstanceDynamic* Lens = MakeLens(
			bLeft ? TEXT("FrontBlinkLensLeft") : TEXT("FrontBlinkLensRight"),
			FrontBlink, FVector(.19, .105, .095), FLinearColor(IndicatorColor)))
		{
			Lenses.Add(Lens);
		}
		if (UMaterialInstanceDynamic* Lens = MakeLens(
			bLeft ? TEXT("RearBlinkLensLeft") : TEXT("RearBlinkLensRight"),
			RearBlink, FVector(.065, .15, .075), FLinearColor(IndicatorColor)))
		{
			Lenses.Add(Lens);
		}
	}

	UE_LOG(LogWbVehicles, Log,
		TEXT("Lichtanlage aufgebaut: %d Scheinwerfer (+ %d Kern/%d Fern), %d Standlichter, %d Rueckleuchten, %d/%d Blinker, %d Leuchtlinsen."),
		Headlights.Num(), FocusedBeams.Num(), HighBeams.Num(),
		ParkingLights.Num(), TailLights.Num(),
		LeftIndicators.Num(), RightIndicators.Num(),
		HeadlightLenses.Num() + TailLenses.Num() + LeftIndicatorLenses.Num() + RightIndicatorLenses.Num());
}

void UWiesbadenCarLightsComponent::BeginPlay()
{
	Super::BeginPlay();
	CreateLights();
	ApplyHeadlightState();
	ApplyIndicatorState();
	ApplyRearLightState();
}

// -- Bedienung --------------------------------------------------------------

bool UWiesbadenCarLightsComponent::ShouldUseHeadlights(float SunElevationFactor)
{
	return SunElevationFactor < AutoHeadlightSunThreshold;
}

void UWiesbadenCarLightsComponent::SetAutomaticHeadlights(bool bWantHeadlights)
{
	if (!bAutomaticHeadlights)
	{
		return;
	}

	// Nur zwischen AUS und ABBLENDLICHT schalten. Standlicht und Fernlicht sind
	// bewusste Entscheidungen des Fahrers und werden nicht angetastet.
	if (bWantHeadlights && HeadlightMode == EWiesbadenHeadlightMode::Off)
	{
		SetHeadlightMode(EWiesbadenHeadlightMode::LowBeam);
	}
	else if (!bWantHeadlights && HeadlightMode == EWiesbadenHeadlightMode::LowBeam)
	{
		SetHeadlightMode(EWiesbadenHeadlightMode::Off);
	}
}

void UWiesbadenCarLightsComponent::CycleHeadlights()
{
	// Der Fahrer uebernimmt: ab jetzt schaltet die Automatik nicht mehr mit.
	bAutomaticHeadlights = false;

	SetHeadlightMode(GetNextHeadlightMode(HeadlightMode));
}

void UWiesbadenCarLightsComponent::SetHeadlightFlash(bool bPressed)
{
	// Lichthupe: solange gedrueckt, brennt das Fernlicht - danach steht
	// wieder der Modus von vorher.
	//
	// Der vorherige Modus wird beim ERSTEN Druck gemerkt, nicht in jedem
	// Bild: sonst merkte sich der zweite Frame das Fernlicht als
	// "Zustand davor" und die Lichthupe liesse sich nie mehr abschalten.
	if (bPressed == bFlashing)
	{
		return;
	}

	bFlashing = bPressed;

	if (bPressed)
	{
		ModeBeforeFlash = HeadlightMode;
		SetHeadlightMode(EWiesbadenHeadlightMode::HighBeam);
	}
	else
	{
		SetHeadlightMode(ModeBeforeFlash);
	}
}

void UWiesbadenCarLightsComponent::SetHeadlightMode(EWiesbadenHeadlightMode NewMode)
{
	if (NewMode == EWiesbadenHeadlightMode::MAX)
	{
		return;
	}

	HeadlightMode = NewMode;
	ApplyHeadlightState();

	UE_LOG(LogWbVehicles, Verbose, TEXT("Fahrlicht: %d"), static_cast<int32>(HeadlightMode));
}

void UWiesbadenCarLightsComponent::ToggleIndicatorLeft()
{
	IndicatorMode = ApplyIndicatorToggle(IndicatorMode, EWiesbadenIndicatorMode::Left);
	BlinkTime = 0.0f;
	ApplyIndicatorState();
}

void UWiesbadenCarLightsComponent::ToggleIndicatorRight()
{
	IndicatorMode = ApplyIndicatorToggle(IndicatorMode, EWiesbadenIndicatorMode::Right);
	BlinkTime = 0.0f;
	ApplyIndicatorState();
}

void UWiesbadenCarLightsComponent::ToggleHazardLights()
{
	IndicatorMode = (IndicatorMode == EWiesbadenIndicatorMode::Hazard)
		? EWiesbadenIndicatorMode::Off
		: EWiesbadenIndicatorMode::Hazard;

	BlinkTime = 0.0f;
	ApplyIndicatorState();
}

void UWiesbadenCarLightsComponent::SetBraking(bool bInBraking)
{
	if (bBraking != bInBraking)
	{
		bBraking = bInBraking;
		ApplyRearLightState();
	}
}

void UWiesbadenCarLightsComponent::SetReversing(bool bInReversing)
{
	if (bReversing != bInReversing)
	{
		bReversing = bInReversing;
		ApplyRearLightState();
	}
}

// -- Zustandsanwendung ------------------------------------------------------

void UWiesbadenCarLightsComponent::ApplyHeadlightState()
{
	const bool bSpotOn = (HeadlightMode == EWiesbadenHeadlightMode::LowBeam)
		|| (HeadlightMode == EWiesbadenHeadlightMode::HighBeam);

	const bool bHigh = (HeadlightMode == EWiesbadenHeadlightMode::HighBeam);

	for (USpotLightComponent* Light : Headlights)
	{
		if (!Light)
		{
			continue;
		}

		Light->SetVisibility(bSpotOn);
		Light->SetIntensity(bHigh ? HighBeamIntensity * .18f : LowBeamIntensity * .55f);
		Light->SetOuterConeAngle(LowBeamOuterConeAngle);
		Light->SetInnerConeAngle(LowBeamOuterConeAngle * .40f);

		// Fernlicht steht waagerechter, Abblendlicht bleibt abgesenkt.
		FRotator Aim = Light->GetRelativeRotation();
		Aim.Pitch = bHigh ? -1.5f : -LowBeamDownwardPitch;
		Light->SetRelativeRotation(Aim);
	}
	for (USpotLightComponent* Light : FocusedBeams)
	{
		if (!Light) { continue; }
		Light->SetVisibility(bSpotOn);
		Light->SetIntensity(bHigh ? HighBeamIntensity * .24f : LowBeamIntensity * .72f);
		Light->SetOuterConeAngle(bHigh ? 19.0f : 23.0f);
		Light->SetInnerConeAngle(bHigh ? 9.0f : 11.0f);
		FRotator Aim = Light->GetRelativeRotation();
		Aim.Pitch = bHigh ? -1.5f : -4.0f;
		Light->SetRelativeRotation(Aim);
	}
	for (USpotLightComponent* Light : HighBeams)
	{
		if (!Light) { continue; }
		Light->SetVisibility(bHigh);
		Light->SetIntensity(bHigh ? HighBeamIntensity * .60f : 0.0f);
		Light->SetOuterConeAngle(HighBeamOuterConeAngle);
		Light->SetInnerConeAngle(HighBeamOuterConeAngle * .38f);
	}

	// Standlicht brennt bei jeder Stufe ausser "Aus" mit.
	const bool bParkingOn = (HeadlightMode != EWiesbadenHeadlightMode::Off);
	for (UPointLightComponent* Light : ParkingLights)
	{
		if (Light)
		{
			Light->SetVisibility(bParkingOn);
			Light->SetIntensity(ParkingIntensity);
		}
	}
	const float HeadGlow = bHigh ? 5.0f
		: (HeadlightMode == EWiesbadenHeadlightMode::LowBeam ? 2.8f
		: (bParkingOn ? .55f : 0.0f));
	for (UMaterialInstanceDynamic* Lens : HeadlightLenses)
	{
		if (Lens) { Lens->SetScalarParameterValue(TEXT("Glow"), HeadGlow); }
	}

	// Die Rueckleuchten haengen ebenfalls am Fahrlicht (Schlusslicht).
	ApplyRearLightState();
}

void UWiesbadenCarLightsComponent::ApplyRearLightState()
{
	// Schlusslicht bei eingeschaltetem Licht, Bremslicht deutlich heller.
	const bool bTailOn = (HeadlightMode != EWiesbadenHeadlightMode::Off) || bBraking;
	const float TailIntensity = bBraking ? BrakeLightIntensity : TailLightIntensity;

	for (UPointLightComponent* Light : TailLights)
	{
		if (Light)
		{
			Light->SetVisibility(bTailOn);
			Light->SetIntensity(bTailOn ? TailIntensity : 0.0f);
		}
	}
	for (UMaterialInstanceDynamic* Lens : TailLenses)
	{
		if (Lens)
		{
			Lens->SetScalarParameterValue(TEXT("Glow"),
				bBraking ? 5.0f : (bTailOn ? .75f : 0.0f));
		}
	}

	if (ReverseLight)
	{
		ReverseLight->SetVisibility(bReversing);
		// Rueckfahrlicht: weiss, aber ebenfalls eine Signalleuchte und keine
		// Arbeitsbeleuchtung.
		ReverseLight->SetIntensity(bReversing ? BrakeLightIntensity : 0.0f);
	}
}

void UWiesbadenCarLightsComponent::ApplyIndicatorState()
{
	const bool bLeft = IndicatorAffectsLeft(IndicatorMode) && bBlinkOn;
	const bool bRight = IndicatorAffectsRight(IndicatorMode) && bBlinkOn;

	for (UPointLightComponent* Light : LeftIndicators)
	{
		if (Light)
		{
			Light->SetVisibility(bLeft);
			Light->SetIntensity(bLeft ? BrakeLightIntensity : 0.0f);
		}
	}

	for (UPointLightComponent* Light : RightIndicators)
	{
		if (Light)
		{
			Light->SetVisibility(bRight);
			Light->SetIntensity(bRight ? BrakeLightIntensity : 0.0f);
		}
	}
	for (UMaterialInstanceDynamic* Lens : LeftIndicatorLenses)
	{
		if (Lens) { Lens->SetScalarParameterValue(TEXT("Glow"), bLeft ? 3.4f : 0.0f); }
	}
	for (UMaterialInstanceDynamic* Lens : RightIndicatorLenses)
	{
		if (Lens) { Lens->SetScalarParameterValue(TEXT("Glow"), bRight ? 3.4f : 0.0f); }
	}
}

void UWiesbadenCarLightsComponent::TickComponent(
	float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);

	if (IndicatorMode == EWiesbadenIndicatorMode::Off)
	{
		if (bBlinkOn)
		{
			bBlinkOn = false;
			ApplyIndicatorState();
		}
		return;
	}

	BlinkTime += DeltaSeconds;

	const bool bNewBlink = ComputeBlinkOn(BlinkTime, BlinkFrequencyHz);
	if (bNewBlink != bBlinkOn)
	{
		bBlinkOn = bNewBlink;
		ApplyIndicatorState();
	}
}
