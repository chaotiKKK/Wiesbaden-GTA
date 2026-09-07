// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenCar.h"

#include "Vehicles/WiesbadenHelicopter.h"   // ApplyStickShaping: eine Kennlinie fuer alle Sticks

#include "WiesbadenReal.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "World/WiesbadenCitySubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr double MetersToCm = 100.0;
}

AWiesbadenCar::AWiesbadenCar()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// Beim Platzieren im Level sofort vom lokalen Spieler uebernehmen.
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	AutoPossessAI = EAutoPossessAI::Disabled;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// Masse des VW Kaefer 1969: 4,08 m lang, 1,55 m breit, 1,50 m hoch.
	// Die Box sitzt so hoch, dass ihre Unterkante auf Radaufstandshoehe liegt.
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	CollisionBox->SetupAttachment(SceneRoot);
	CollisionBox->SetBoxExtent(FVector(204.0f, 78.0f, 75.0f));
	CollisionBox->SetRelativeLocation(FVector(0.0f, 0.0f, 75.0f));
	CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionBox->SetCollisionResponseToAllChannels(ECR_Block);

	// Karosserie: VW Kaefer 1969 als Platzhaltermodell. Faellt auf den
	// Engine-Basis-Cube zurueck, solange das Asset nicht importiert ist -
	// ohne diesen Rueckfall waere das Fahrzeug im Level unsichtbar und der
	// Fehler schwer zu erkennen.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeetleMesh(
		TEXT("/Game/Vehicles/Beetle/SM_VWBeetle1969_Body.SM_VWBeetle1969_Body"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeetleWheelMesh(
		TEXT("/Game/Vehicles/Beetle/SM_VWBeetle_Wheel.SM_VWBeetle_Wheel"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));

	UStaticMesh* Cube = CubeMesh.Object;
	UStaticMesh* Beetle = BeetleMesh.Succeeded() ? BeetleMesh.Object : nullptr;
	UStaticMesh* BeetleWheel = BeetleWheelMesh.Succeeded() ? BeetleWheelMesh.Object : nullptr;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(SceneRoot);

	if (Beetle)
	{
		// Das Modell ist massstaeblich (4,15 m lang) und hat seinen Ursprung
		// auf Radaufstandshoehe - es wird weder skaliert noch versetzt.
		BodyMesh->SetStaticMesh(Beetle);
		BodyMesh->SetRelativeLocation(FVector::ZeroVector);
		BodyMesh->SetRelativeScale3D(FVector::OneVector);
	}
	else if (Cube)
	{
		// Ersatzquader in PKW-Groesse: 440 x 180 x 60 cm, Sitzhoehe ~75 cm.
		BodyMesh->SetStaticMesh(Cube);
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 75.0f));
		BodyMesh->SetRelativeScale3D(FVector(4.4f, 1.8f, 0.6f));
	}

	// Radpositionen, am Modell vermessen (Reifenmitten, cm im Fahrzeug-
	// Lokalsystem: +X vorwaerts, +Y rechts, Z = Radradius ueber der Strasse).
	//
	// Radstand 2,42 m und Spurweite 1,31 m entsprechen dem realen Kaefer
	// (2,40 m / 1,30 m). Alle vier liegen auf Z = 34,3 cm, dem Radhalbmesser -
	// das Fahrzeug steht damit auf den Reifen, nicht auf der Karosserie.
	// Das Rad-Mesh ist massstaeblich und wird nicht skaliert; der
	// Ersatzquader dagegen muss auf Radgroesse gebracht werden.
	const FVector WheelScale = BeetleWheel
		? FVector::OneVector
		: FVector(0.8f, 0.35f, 0.8f);

	const FVector WheelPositions[4] = {
		FVector(129.9f, -65.6f, 34.3f),  // vorne links
		FVector(130.5f,  65.3f, 34.3f),  // vorne rechts
		FVector(-112.2f, -65.6f, 34.3f), // hinten links
		FVector(-111.6f,  65.3f, 34.3f), // hinten rechts
	};

	FrontLeftWheel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontLeftWheel"));
	FrontRightWheel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontRightWheel"));
	RearLeftWheel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RearLeftWheel"));
	RearRightWheel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RearRightWheel"));

	UStaticMeshComponent* Wheels[4] = {
		FrontLeftWheel, FrontRightWheel, RearLeftWheel, RearRightWheel
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UStaticMeshComponent* Wheel = Wheels[Index];
		Wheel->SetupAttachment(SceneRoot);
		Wheel->SetRelativeLocation(WheelPositions[Index]);
		Wheel->SetRelativeScale3D(WheelScale);

		if (BeetleWheel)
		{
			// Das Rad wurde aus dem Quellmodell herausgetrennt (dort nach
			// Material gruppiert, nicht nach Bauteil) und auf seinen eigenen
			// Mittelpunkt zentriert. Es dreht dadurch um die eigene Achse.
			Wheel->SetStaticMesh(BeetleWheel);

			// Die Raeder tragen keine Kollision: die Fahrzeugphysik arbeitet
			// mit der Kollisionskugel am Rumpf und Raycasts nach unten.
			// Vier zusaetzliche Kollisionskoerper waeren reine Last.
			Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		else if (Cube)
		{
			Wheel->SetStaticMesh(Cube);
		}
	}

	// Fahrwerkswerte an das Kaefer-Modell angleichen. Die Physik leitet aus
	// dem Radhalbmesser die Uebersetzung ab und UpdateWheels() die
	// Drehgeschwindigkeit - weicht der Wert vom Mesh ab, drehen sich die
	// Raeder sichtbar schneller oder langsamer als das Fahrzeug faehrt
	// ("Eisstockschieben").
	if (BeetleWheel)
	{
		VehiclePhysics.WheelRadiusM = 0.343f;   // am Reifen vermessen (0,686 m Durchmesser)
		VehiclePhysics.WheelbaseM = 2.42f;      // Radstand aus den Radmitten
	}

	// Kamera: generische Fahrzeug-Kamera-Komponente (erzeugt ihr Rig in BeginPlay).
	VehicleCamera = CreateDefaultSubobject<UWiesbadenVehicleCameraComponent>(TEXT("VehicleCamera"));
	VehicleCamera->SetupAttachment(SceneRoot);
	VehicleCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 110.0f));

	// Fahrerauge: leicht vor der Mitte, links (Linkslenker), Augenhoehe ~118 cm
	// ueber dem Boden. Der Versatz ist relativ zur Kamera-Komponente (0,0,110),
	// die Augenhoehe ergibt sich also aus 110 + 8. Ohne diese Anpassung sass die
	// Cockpit-Kamera durch den Standardversatz 2,5 m ueber dem Kaefer.
	VehicleCamera->CockpitOffset = FVector(18.0f, -32.0f, 8.0f);
	VehicleCamera->AddCockpitHiddenMesh(BodyMesh);

	// Lichtanlage und Motorklang. Beide bauen ihre Unterobjekte erst in
	// BeginPlay auf - im Konstruktor gibt es weder eine Welt noch ein
	// Audiogeraet, an das sie sich haengen koennten.
	Lights = CreateDefaultSubobject<UWiesbadenCarLightsComponent>(TEXT("Lights"));
	Lights->SetupAttachment(SceneRoot);

	EngineAudio = CreateDefaultSubobject<UWiesbadenCarAudioComponent>(TEXT("EngineAudio"));
	EngineAudio->SetupAttachment(SceneRoot);
	// Der Kaefer hat den Motor hinten - der Klang kommt von dort.
	EngineAudio->SetRelativeLocation(FVector(-160.0f, 0.0f, 50.0f));

	if (!Cube)
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Basis-Cube (/Engine/BasicShapes/Cube) nicht gefunden - Platzhalter-Meshes bleiben unsichtbar."));
	}
}

void AWiesbadenCar::BeginPlay()
{
	Super::BeginPlay();

	// Herbie-Lackierung - NUR fuer das Spielerauto.
	//
	// Die Instanzen MI_VWBeetleHerbie_<Kachel> (Tools/import_herbie.py)
	// tragen Rennstreifen und die Startnummer 53, gebacken auf dieselbe
	// UV-Abwicklung. Der Verkehr behaelt die Serienlackierung; er bekommt
	// seine Materialien aus dem Mesh selbst und nicht von diesem Pawn.
	//
	// Zuordnung ueber den KACHELNAMEN im Schlitz, nicht ueber den Index:
	// die Reihenfolge der Schlitze haengt am FBX-Import und aendert sich
	// beim naechsten Reimport stillschweigend.
	if (BodyMesh && BodyMesh->GetStaticMesh())
	{
		static const TCHAR* Tiles[] = { TEXT("1001"), TEXT("1002"), TEXT("1003"), TEXT("1004") };
		// GetMaterialSlotNames lebt an der KOMPONENTE (UMeshComponent), nicht
		// am UStaticMesh - dort gibt es die Funktion gar nicht.
		const TArray<FName> SlotNames = BodyMesh->GetMaterialSlotNames();
		int32 Swapped = 0;
		for (const FName& SlotName : SlotNames)
		{
			const FString Name = SlotName.ToString();
			for (const TCHAR* Tile : Tiles)
			{
				if (!Name.Contains(Tile))
				{
					continue;
				}
				const FString Path = FString::Printf(
					TEXT("/Game/Vehicles/Beetle/MI_VWBeetleHerbie_%s.MI_VWBeetleHerbie_%s"),
					Tile, Tile);
				if (UMaterialInterface* Herbie = LoadObject<UMaterialInterface>(nullptr, *Path))
				{
					const int32 Index = BodyMesh->GetMaterialIndex(SlotName);
					if (Index != INDEX_NONE)
					{
						BodyMesh->SetMaterial(Index, Herbie);
						++Swapped;
					}
				}
				break;
			}
		}
		UE_LOG(LogWbVehicles, Log,
			TEXT("Spielerauto: Herbie-Lackierung auf %d von %d Schlitzen."),
			Swapped, SlotNames.Num());
	}
}

void AWiesbadenCar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ReadInput(DeltaSeconds);
	ReadLightInput();
	ApplyVehiclePhysics(DeltaSeconds);
	UpdateWheels(DeltaSeconds);
}

void AWiesbadenCar::ReadLightInput()
{
	// Flankenauswertung: reagiert auf das Druecken, nicht auf das Halten.
	// Ohne diese Unterscheidung wuerde der Blinker mit der Framerate flackern.
	auto Edge = [this](const FKey& Key, bool& bHeld) -> bool
	{
		const bool bDown = IsKeyDown(Key);
		const bool bPressed = bDown && !bHeld;
		bHeld = bDown;
		return bPressed;
	};

	if (!Lights)
	{
		return;
	}

	// Gamepad-Belegung der Bedienelemente (Xbox).
	//
	// Bis hierher war am Gamepad NUR Fahren belegt - Gas, Bremse, Lenken,
	// Handbremse, Rueckwaertsgang. Licht, Blinker, Warnblinker, Hupe und
	// Aussteigen gab es ausschliesslich auf der Tastatur, was das Gamepad
	// zum halben Eingabegeraet machte.
	//
	//   A               Hupe
	//   B               Handbremse
	//   X               Rueckwaertsgang
	//   Y               Aussteigen (im GameMode)
	//   LB / RB         Blinker links / rechts
	//   Steuerkreuz hoch    Licht durchschalten
	//   Steuerkreuz runter  Warnblinkanlage
	//   Rechter Stick druecken  Lichthupe
	if (Edge(EKeys::L, bHeadlightKeyHeld) || Edge(EKeys::Gamepad_DPad_Up, bHeadlightPadHeld))
	{
		Lights->CycleHeadlights();
	}

	// Lichthupe: KEINE Flanke - sie gilt, solange gedrueckt wird.
	Lights->SetHeadlightFlash(
		IsKeyDown(EKeys::X) || IsKeyDown(EKeys::Gamepad_RightThumbstick));

	// Hupe: ebenfalls gehalten, nicht geschaltet.
	if (EngineAudio)
	{
		EngineAudio->SetHorn(
			IsKeyDown(EKeys::B) || IsKeyDown(EKeys::Gamepad_FaceButton_Bottom));
	}

	// Lichtautomatik nach Sonnenstand.
	//
	// Die Tageszeit laeuft mit einer Stunde je 2,5 Realminuten. Nach rund einer
	// halben Stunde Spielzeit ist es Nacht - und die Stadt hat keine
	// Strassenbeleuchtung. Ohne Automatik faehrt man dann mit ausgeschalteten
	// Scheinwerfern durch voellige Schwaerze, ohne zu wissen, dass Licht auf L
	// liegt. Der erste Druck auf L uebergibt die Kontrolle dauerhaft.
	if (const UWorld* CarWorld = GetWorld())
	{
		if (const UWiesbadenCitySubsystem* City = CarWorld->GetSubsystem<UWiesbadenCitySubsystem>())
		{
			const FWiesbadenWeatherState WeatherState = City->GetWeatherState();
			Lights->SetAutomaticHeadlights(
				UWiesbadenCarLightsComponent::ShouldUseHeadlights(WeatherState.SunElevationFactor));
		}
	}
	if (Edge(EKeys::Q, bIndicatorLeftKeyHeld)
		|| Edge(EKeys::Gamepad_LeftShoulder, bIndicatorLeftPadHeld))
	{
		Lights->ToggleIndicatorLeft();
	}
	if (Edge(EKeys::E, bIndicatorRightKeyHeld)
		|| Edge(EKeys::Gamepad_RightShoulder, bIndicatorRightPadHeld))
	{
		Lights->ToggleIndicatorRight();
	}
	if (Edge(EKeys::H, bHazardKeyHeld)
		|| Edge(EKeys::Gamepad_DPad_Down, bHazardPadHeld))
	{
		Lights->ToggleHazardLights();
	}
}

void AWiesbadenCar::UpdateLightsAndAudio(const FWiesbadenVehiclePhysicsOutput& Output)
{
	if (Lights)
	{
		// Bremslicht: sobald das Bremspedal spuerbar betaetigt ist. Die
		// Schwelle verhindert, dass das geglaettete Eingabesignal beim
		// Loslassen noch nachleuchtet.
		Lights->SetBraking(BrakeInput > 0.15f);

		// Rueckfahrlicht: nur wenn tatsaechlich rueckwaerts gefahren wird,
		// nicht schon beim Einlegen des Ganges.
		Lights->SetReversing(Output.ForwardSpeedMetersPerS < -0.3f);
	}

	if (EngineAudio)
	{
		EngineAudio->SetEngineState(Output.EngineRpm, ThrottleInput, Output.SpeedKmh);
	}
}

void AWiesbadenCar::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		PC->SetViewTarget(this);
	}

	UE_LOG(LogWbVehicles, Log, TEXT("Fahrzeug %s wurde vom Spieler uebernommen."), *GetName());
}

void AWiesbadenCar::UnPossessed()
{
	Super::UnPossessed();

	UE_LOG(LogWbVehicles, Log, TEXT("Fahrzeug %s wurde freigegeben."), *GetName());
}

void AWiesbadenCar::CycleCameraMode()
{
	if (VehicleCamera)
	{
		VehicleCamera->CycleCameraMode();
	}
}

EWiesbadenVehicleCameraMode AWiesbadenCar::GetCameraMode() const
{
	return VehicleCamera ? VehicleCamera->GetCameraMode() : EWiesbadenVehicleCameraMode::Follow;
}

float AWiesbadenCar::GetSpeedKmh() const
{
	return FMath::Abs(VehiclePhysics.SpeedMetersPerS) * 3.6f;
}

int32 AWiesbadenCar::GetGear() const
{
	return VehiclePhysics.Gear;
}

float AWiesbadenCar::GetEngineRpm() const
{
	return VehiclePhysics.EngineRpm;
}

float AWiesbadenCar::GetAnalogAxis(const FKey& Key)
{
	const APlayerController* PC = GetCarController();
	return PC ? PC->GetInputAnalogKeyState(Key) : 0.0f;
}

void AWiesbadenCar::ReadInput(float DeltaSeconds)
{
	const float Response = FMath::Clamp(ControlResponse, 0.1f, 100.0f);

	// Externe Steuerung (KI/Test/Replay) hat Vorrang und umgeht die Tastenabfrage:
	// fertige Werte werden nur geglaettet wie eine echte Eingabe, dann uebernimmt
	// die normale Fahrphysik. So bleibt diese Klasse frei von Test-/Treiberlogik.
	if (bExternalControlActive)
	{
		bReverseRequested = ExternalControl.bReverse;
		ThrottleInput = FMath::FInterpTo(ThrottleInput, FMath::Clamp(ExternalControl.Throttle, 0.0f, 1.0f), DeltaSeconds, Response);
		BrakeInput = FMath::FInterpTo(BrakeInput, FMath::Clamp(ExternalControl.Brake, 0.0f, 1.0f), DeltaSeconds, Response);
		SteeringInput = FMath::FInterpTo(SteeringInput, FMath::Clamp(ExternalControl.Steering, -1.0f, 1.0f), DeltaSeconds, Response);
		return;
	}

	// Zieleingaben aus gepollten Tasten (zero-config, keine Input-Assets noetig).
	// Pfeiltasten laufen ueberall parallel zu WASD mit.
	//
	// Gamepad-Belegung (uebliche Fahrzeugbelegung):
	//
	//   Rechter Trigger   Gas
	//   Linker Trigger    Bremse / Rueckwaerts
	//   Linker Stick X    Lenken
	//   A                 Handbremse
	//   X                 Rueckwaertsgang umschalten
	//
	// Die Trigger sind echte Analogachsen - anders als die Tastatur erlauben
	// sie Teilgas und dosiertes Bremsen. Genau daran fehlte es bisher.
	const bool bForwardHeld = IsKeyDown(EKeys::W) || IsKeyDown(EKeys::Up);
	const bool bBackwardHeld = IsKeyDown(EKeys::S) || IsKeyDown(EKeys::Down);
	const bool bRightHeld = IsKeyDown(EKeys::D) || IsKeyDown(EKeys::Right);
	const bool bLeftHeld = IsKeyDown(EKeys::A) || IsKeyDown(EKeys::Left);

	// Automatischer Rueckwaertsgang.
	//
	// Bisher liess sich rueckwaerts NUR ueber eine eigene Taste (R) fahren -
	// wer vor eine Wand fuhr, blieb dort stehen, weil Bremsen im Stillstand
	// nichts weiter tut. Ueblich und erwartbar ist: gebremst wird bis zum
	// Stillstand, wer dann weiter drueckt, faehrt zurueck. R bleibt als
	// ausdrueckliche Umschaltung erhalten.
	const float SpeedAbsMetersPerS = FMath::Abs(VehiclePhysics.SpeedMetersPerS);
	constexpr float StandstillMetersPerS = 0.4f;

	if (!bReverseRequested && bBackwardHeld && SpeedAbsMetersPerS < StandstillMetersPerS)
	{
		bReverseRequested = true;
	}
	else if (bReverseRequested && bForwardHeld && SpeedAbsMetersPerS < StandstillMetersPerS)
	{
		bReverseRequested = false;
	}

	// Im Rueckwaertsgang tauschen Gas und Bremse die Tasten: Zurueck faehrt
	// weiter zurueck, Vorwaerts bremst. Ohne diesen Tausch wuerde die
	// Rueckwaertsfahrt ausgerechnet mit der Taste eingeleitet, die sie
	// anschliessend abwuergt.
	float TargetThrottle = (bReverseRequested ? bBackwardHeld : bForwardHeld) ? 1.0f : 0.0f;
	float TargetBrake = (bReverseRequested ? bForwardHeld : bBackwardHeld) ? 1.0f : 0.0f;

	float TargetSteering = 0.0f;
	if (bRightHeld) { TargetSteering += 1.0f; }
	if (bLeftHeld) { TargetSteering -= 1.0f; }

	// Gamepad: Trigger analog, Stick analog. Der groessere Betrag gewinnt, damit
	// eine ruhende Eingabequelle die andere nie stoert.
	const float GamepadThrottle = GetAnalogAxis(EKeys::Gamepad_RightTriggerAxis);
	const float GamepadBrake = GetAnalogAxis(EKeys::Gamepad_LeftTriggerAxis);
	const float GamepadSteering = AWiesbadenHelicopter::ApplyStickShaping(
		GetAnalogAxis(EKeys::Gamepad_LeftX), GamepadDeadzone, GamepadSteerExpo);

	// Im Rueckwaertsgang tauschen auch die Trigger die Rollen - sonst waere die
	// Bedienung je nach Eingabegeraet verschieden.
	const float MappedThrottle = bReverseRequested ? GamepadBrake : GamepadThrottle;
	const float MappedBrake = bReverseRequested ? GamepadThrottle : GamepadBrake;

	if (MappedThrottle > TargetThrottle) { TargetThrottle = MappedThrottle; }
	if (MappedBrake > TargetBrake) { TargetBrake = MappedBrake; }
	if (FMath::Abs(GamepadSteering) > FMath::Abs(TargetSteering)) { TargetSteering = GamepadSteering; }

	// Rueckwaertsgang auch am Gamepad automatisch: Bremse im Stand haelt sonst
	// nur das Fahrzeug fest, statt zurueckzusetzen.
	if (!bReverseRequested && GamepadBrake > 0.5f && SpeedAbsMetersPerS < StandstillMetersPerS)
	{
		bReverseRequested = true;
	}

	ThrottleInput = FMath::FInterpTo(ThrottleInput, TargetThrottle, DeltaSeconds, Response);
	BrakeInput = FMath::FInterpTo(BrakeInput, TargetBrake, DeltaSeconds, Response);
	SteeringInput = FMath::FInterpTo(SteeringInput, TargetSteering, DeltaSeconds, Response);

	// Rueckwaertsgang (Flanke auf R) - nur im Stand aktiviert die Physik ihn.
	const bool bReversePressed = IsKeyDown(EKeys::R) || IsKeyDown(EKeys::Gamepad_FaceButton_Left);
	if (bReversePressed && !bReverseToggleHeld)
	{
		bReverseRequested = !bReverseRequested;
		UE_LOG(LogWbVehicles, Log, TEXT("Rueckwaertsgang %s."), bReverseRequested ? TEXT("an") : TEXT("aus"));
	}
	bReverseToggleHeld = bReversePressed;
}

float AWiesbadenCar::AdvanceFallSpeedCmS(float CurrentCmS, float GravityCmS2, float Dt)
{
	// Freier Fall, gedeckelt auf eine plausible Endgeschwindigkeit.
	const float Next = CurrentCmS + FMath::Max(GravityCmS2, 0.0f) * FMath::Max(Dt, 0.0f);
	return FMath::Min(Next, 20000.0f);
}

void AWiesbadenCar::ApplyVehiclePhysics(float DeltaSeconds)
{
	FWiesbadenVehiclePhysicsInput Input;
	Input.Throttle = ThrottleInput;
	Input.Brake = BrakeInput;
	Input.Steering = SteeringInput;
	// Handbremse auf B, nicht mehr auf A: A ist jetzt die Hupe, und die
	// braucht man oefter und schneller als die Handbremse. Bei externer
	// Steuerung kommt die Handbremse aus dem Steuerwert, nicht von der Taste.
	Input.bHandbrake = bExternalControlActive
		? ExternalControl.bHandbrake
		: (IsKeyDown(EKeys::SpaceBar) || IsKeyDown(EKeys::Gamepad_FaceButton_Right));
	Input.bReverseRequested = bReverseRequested;

	FWiesbadenVehiclePhysicsOutput Output;
	VehiclePhysics.Tick(Input, DeltaSeconds, Output);

	// Licht und Klang direkt aus dem Physikergebnis speisen, damit Bremslicht
	// und Motordrehzahl im selben Frame stimmen wie die Bewegung.
	UpdateLightsAndAudio(Output);

	// Geschwindigkeit (m/s) -> Weltbewegung (cm/s) entlang der Fahrzeug-X-Achse.
	const FVector Forward = GetActorForwardVector();
	const double SpeedCmPerS = Output.ForwardSpeedMetersPerS * MetersToCm;

	// QUERbewegung (Schlupf/Drift) aus dem dynamischen Einspurmodell: der Wagen
	// faehrt nicht mehr exakt in Blickrichtung, sondern hat eine Querkomponente
	// entlang der rechten Fahrzeugachse - das ist der Kern des "nicht auf
	// Schienen"-Gefuehls (Untersteuern, Heck kommt, Drift).
	const FVector Right = GetActorRightVector();
	const double LateralCmPerS = Output.LateralVelocityMetersPerS * MetersToCm;

	// -- Ueberflug: Gebaeude voraus? ----------------------------------------
	//
	// Der Wagen kracht nicht in Haeuser, er fliegt darueber - rund zehn Meter
	// ueber dem Dach, und dahinter gleitet er wieder herab. Erkannt wird ein
	// Gebaeude an zwei Merkmalen: eine STEILE Flaeche voraus (Hauswand,
	// Normale fast waagerecht) und eine Oberkante DEUTLICH ueber dem Wagen.
	// Das zweite Merkmal haelt Bordsteine heraus: deren Wand ist genauso
	// steil, ihre Oberkante liegt aber auf Strassenhoehe - ohne die Pruefung
	// wuerde jeder Bordstein den Wagen zehn Meter in die Luft schicken.
	//
	// STANDARD AUS (bEnableBuildingFlyOver): der Ueberflug warf den Wagen bei
	// normaler Fahrt an jeder Hauswand hoch ueber die Daecher (trudelnder
	// Absturz = "Kaefer fliegt ueber die Haeuser"). Ist er aus, wird nie ein
	// Ueberflug ausgeloest; der Wagen bleibt am Boden und schiebt an Waenden
	// entlang. Der Ueberflug bleibt als bewusst schaltbares Feature erhalten.
	if (UWorld* ProbeWorld = bEnableBuildingFlyOver ? GetWorld() : nullptr)
	{
		const FVector ForwardFlat =
			FVector::VectorPlaneProject(Forward, FVector::UpVector).GetSafeNormal();
		const float ProbeLength = FMath::Max(
			1500.0f, FMath::Abs(static_cast<float>(SpeedCmPerS)) * 1.8f);
		const FVector ProbeStart = GetActorLocation() + FVector(0.0f, 0.0f, 60.0f);
		const FVector ProbeEnd = ProbeStart + ForwardFlat * ProbeLength;

		FCollisionQueryParams ProbeParams(SCENE_QUERY_STAT(WbCarWallProbe), true);
		ProbeParams.AddIgnoredActor(this);

		FHitResult WallHit;
		const bool bWallAhead = SpeedCmPerS > 50.0
			&& ProbeWorld->LineTraceSingleByChannel(
				WallHit, ProbeStart, ProbeEnd, ECC_WorldStatic, ProbeParams)
			&& FMath::Abs(WallHit.ImpactNormal.Z) < 0.35f;

		bWallAheadThisFrame = false;
		if (bWallAhead)
		{
			// Oberkante des Gebaeudes: knapp HINTER der Wand von hoch oben
			// nach unten tasten. Der Versatz von 1,5 m stellt sicher, dass
			// der Strahl das Dach trifft und nicht die Wandkante.
			const FVector TopStart = WallHit.ImpactPoint
				+ ForwardFlat * 150.0f + FVector(0.0f, 0.0f, 8000.0f);
			FHitResult TopHit;
			if (ProbeWorld->LineTraceSingleByChannel(
				TopHit, TopStart, TopStart - FVector(0.0f, 0.0f, 9000.0f),
				ECC_WorldStatic, ProbeParams))
			{
				const float ObstacleTopZ = TopHit.Location.Z;
				if (ObstacleTopZ > GetActorLocation().Z + FlyOverMinObstacleCm)
				{
					if (!bFlyingOverBuilding)
					{
						bFlyingOverBuilding = true;
						FlyOverStreetZ = GetActorLocation().Z;
						UE_LOG(LogWbVehicles, Log,
							TEXT("Ueberflug: Gebaeudekante %.1f m voraus, Dach %.1f m ueber der Strasse."),
							WallHit.Distance / 100.0f,
							(ObstacleTopZ - FlyOverStreetZ) / 100.0f);
					}
					FlyOverTargetZ = ObstacleTopZ + FlyOverClearanceCm;
					bWallAheadThisFrame = true;
				}
			}
		}
	}

	// Bewegung mit Sweep. Blockiert etwas, wird der Rest der Strecke an der
	// Wand ENTLANG geschoben statt verworfen - sonst steht das Fahrzeug bei
	// jeder Beruehrung schlagartig, was sich wie ein Aufprall gegen Beton
	// anfuehlt, auch beim Streifen.
	//
	// Im Ueberflug faellt der Sweep weg: Der Wagen steigt zwar rechtzeitig,
	// aber wer erst dicht vor der Wand Gas gibt, wuerde vom Sweep gestoppt,
	// bevor die Steigrate ihn ueber die Kante traegt.
	FHitResult MoveHit;
	const FVector Wanted =
		(Forward * SpeedCmPerS + Right * LateralCmPerS) * DeltaSeconds;

	// Kollision AUSDRUECKLICH gegen die Kollisionsbox pruefen.
	//
	// Hier stand `AddActorWorldOffset(Wanted, bSweep, &MoveHit)`. Der Sweep
	// prueft die WURZELKOMPONENTE - und die ist beim Kaefer ein nackter
	// USceneComponent ohne jede Form (SceneRoot, siehe Konstruktor). Er
	// testete also nichts und meldete nie einen Treffer: das Fahrzeug fuhr
	// durch Haeuser hindurch, waehrend das Protokoll brav
	// "Gebaeude-Kollision wirksam: 96 Koerper aktiv" meldete. Die Koerper
	// waren da, nur hat sie niemand abgefragt.
	//
	// Beim Fussgaenger faellt derselbe Aufruf nicht auf, weil dort die
	// KAPSEL die Wurzel ist - dort funktioniert er.
	bool bBlocked = !bFlyingOverBuilding && SweepVehicle(Wanted, MoveHit);

	// Step-up: niedrige Stufen (Bordsteine bis StepUpMaxCm) NICHT als Wand
	// behandeln - sonst bleibt der Wagen am Bordstein haengen. Die Oberkante der
	// Blockade kurz DAHINTER abtasten: der Abwaertsstrahl startet nur wenig ueber
	// der maximalen Stufenhoehe, sodass eine echte Hauswand (Balken bis zum Dach)
	// den Strahl beim Start durchdringt -> Treffer weit oben -> KEIN Step-up,
	// waehrend ein flacher Bordstein knapp darunter getroffen wird -> ueberfahren.
	if (bBlocked)
	{
		const FVector WantedDirFlat = FVector(Wanted.X, Wanted.Y, 0.0f).GetSafeNormal();
		if (UWorld* StepWorld = GetWorld(); StepWorld && !WantedDirFlat.IsNearlyZero())
		{
			const float CarBaseZ = GetActorLocation().Z - GroundClearanceCm;
			const FVector Behind = MoveHit.ImpactPoint + WantedDirFlat * 45.0f;
			const FVector StepStart(Behind.X, Behind.Y, CarBaseZ + StepUpMaxCm + 10.0f);
			FHitResult StepHit;
			FCollisionQueryParams StepParams(SCENE_QUERY_STAT(WbCarStepUp), true);
			StepParams.AddIgnoredActor(this);
			if (StepWorld->LineTraceSingleByChannel(StepHit, StepStart,
					StepStart - FVector(0.0f, 0.0f, StepUpMaxCm + 410.0f),
					ECC_WorldStatic, StepParams)
				&& (StepHit.ImpactPoint.Z - CarBaseZ) <= StepUpMaxCm + 1.0f)
			{
				// Flache Stufe: vollen Zug zulassen, die Bodenverfolgung hebt sanft an.
				bBlocked = false;
			}
		}
	}

	if (bBlocked)
	{
		// Bis kurz vor den Treffer fahren, den Rest uebernimmt das Gleiten.
		AddActorWorldOffset(Wanted * FMath::Max(0.0f, MoveHit.Time - 0.01f), false);
	}
	else
	{
		AddActorWorldOffset(Wanted, false);
	}

	if (bBlocked && MoveHit.Normal.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		// Reststrecke auf die Wandebene projizieren.
		const FVector Remaining = Wanted * (1.0f - MoveHit.Time);
		const FVector Slide = FVector::VectorPlaneProject(Remaining, MoveHit.Normal);
		if (!Slide.IsNearlyZero())
		{
			// Auch das Entlanggleiten geprueft - sonst schiebt es den Wagen
			// in der Ecke zweier Waende durch die zweite hindurch.
			FHitResult SlideHit;
			if (SweepVehicle(Slide, SlideHit))
			{
				AddActorWorldOffset(Slide * FMath::Max(0.0f, SlideHit.Time - 0.01f), false);
			}
			else
			{
				AddActorWorldOffset(Slide, false);
			}
		}

		// Anprall kostet Tempo, streifen kaum: der Verlust richtet sich danach,
		// wie frontal die Wand getroffen wurde.
		const float Frontal = FMath::Abs(FVector::DotProduct(Forward, MoveHit.Normal));
		VehiclePhysics.SpeedMetersPerS *= FMath::Lerp(0.98f, 0.25f, Frontal);
	}

	// Fussgaenger ueberfahren.
	//
	// Sie tragen keine Kollision (Instanzen einer gemeinsamen Komponente),
	// werden vom Sweep oben also grundsaetzlich nicht getroffen. Die
	// Simulation wird deshalb direkt gefragt - dieselbe Loesung wie beim
	// Saegehieb der Spielfigur.
	//
	// Erst ab Schrittgeschwindigkeit: wer im Stau vorwaertsrollt, soll nicht
	// nebenbei den Gehweg raeumen.
	if (FMath::Abs(VehiclePhysics.SpeedMetersPerS) > 2.0)
	{
		if (UWiesbadenCitySubsystem* City = GetWorld()
			? GetWorld()->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr)
		{
			const FVector Front = GetActorLocation() + Forward * 180.0;
			const int32 Hit = City->PedestrianSimulation.BurstNear(Front, 110.0);
			if (Hit > 0)
			{
				UE_LOG(LogWbVehicles, Log, TEXT("Ueberfahren: %d Fussgaenger."), Hit);
			}
		}
	}

	// Gierrate -> Ausrichtung (Drehung um die Hochachse der Fahrbahn).
	if (!FMath::IsNearlyZero(Output.YawRateRadPerS, 1e-4))
	{
		const float YawDegrees = FMath::RadiansToDegrees(Output.YawRateRadPerS) * DeltaSeconds;
		AddActorWorldRotation(FRotator(0.0f, YawDegrees, 0.0f));
	}

	// Bodenkontakt: Hoehe UND Neigung.
	//
	// Hier wurde zuvor nur die Hoehe nachgezogen. Das Fahrzeug blieb dadurch an
	// jeder Steigung exakt waagerecht, waehrend die Fahrbahn unter ihm kippte -
	// und weil GetActorForwardVector() damit immer horizontal war, fuhr der
	// Wagen waagerecht in jeden Anstieg hinein und wurde anschliessend von der
	// Hoehenkorrektur ruckartig nach oben gesetzt. Genau dieses Stossen war als
	// "Fahrphysik" zu spueren. Wird die Karosserie an die Flaechennormale
	// gelegt, zeigt die Vorwaertsachse die Steigung hinauf und die Bewegung
	// folgt der Strasse, statt gegen sie zu arbeiten.
	if (UWorld* CarWorld = GetWorld())
	{
		FHitResult Hit;
		// Start ueber dem Wagen: begaenne der Trace in seinem Mittelpunkt,
		// koennte er innerhalb eines Kollisionskoerpers starten und nichts
		// treffen.
		const FVector Start = GetActorLocation() + FVector(0.0f, 0.0f, 200.0f);
		const FVector End = Start - FVector(0.0f, 0.0f, 100000.0f);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(WbCarGround), true);
		Params.AddIgnoredActor(this);

		if (CarWorld->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			// Boden gefunden: der Wagen faellt nicht, Fallgeschwindigkeit zurueck.
			FallSpeedCmS = 0.0f;

			// Neigung: Hochachse auf die Flaechennormale, Vorwaertsachse so nah
			// wie moeglich an der bisherigen Fahrtrichtung. Sehr steile oder
			// senkrechte Treffer (Bordsteinkante, Hauswand) werden ignoriert -
			// sie wuerden den Wagen auf die Seite legen.
			const FVector GroundNormal = Hit.ImpactNormal;
			const FVector ForwardFlat =
				FVector::VectorPlaneProject(GetActorForwardVector(), FVector::UpVector).GetSafeNormal();

			if (GroundNormal.Z > 0.5f && !ForwardFlat.IsNearlyZero())
			{
				const FRotator Target = FRotationMatrix::MakeFromZX(GroundNormal, ForwardFlat).Rotator();
				SetActorRotation(FMath::RInterpTo(
					GetActorRotation(), Target, DeltaSeconds, GroundAlignResponse));
			}

			float DesiredZ = Hit.Location.Z + GroundClearanceCm;
			const float CurrentZ = GetActorLocation().Z;

			if (bFlyingOverBuilding)
			{
				// Drei Phasen, unterschieden an zwei Merkmalen:
				//
				//   ANFLUG    Wand voraus, unter dem Wagen noch Strasse
				//             -> auf Kantenhoehe + 10 m steigen
				//   UEBERFLUG Abwaertsstrahl trifft das DACH (deutlich ueber
				//             der Strasse des Abhebens)
				//             -> Dachhoehe + 10 m halten
				//   ABSTIEG   keine Wand mehr, unter dem Wagen wieder Strasse
				//             -> herabgleiten, Flug endet am Boden
				//
				// Die erste Fassung liess FlyOverTargetZ auch im Abstieg
				// gelten - der Wagen waere fuer immer auf Ueberflughoehe
				// weitergefahren.
				const bool bOverRoof = Hit.Location.Z > FlyOverStreetZ + 300.0f;
				if (bOverRoof)
				{
					DesiredZ = FMath::Max(DesiredZ, Hit.Location.Z + FlyOverClearanceCm);
				}
				else if (bWallAheadThisFrame)
				{
					DesiredZ = FMath::Max(DesiredZ, FlyOverTargetZ);
				}
				else if (CurrentZ <= DesiredZ + 50.0f)
				{
					bFlyingOverBuilding = false;
					UE_LOG(LogWbVehicles, Log, TEXT("Ueberflug beendet - wieder auf der Strasse."));
				}

				// Raten begrenzen: steigen zuegig, sinken sanft - ein
				// Zehn-Meter-Sturz mit Federungstempo saehe aus wie ein
				// Absturz, nicht wie ein Gleitflug.
				const float MaxUp = FlyOverClimbRateCmPerS * DeltaSeconds;
				const float MaxDown = FlyOverDescendRateCmPerS * DeltaSeconds;
				const float Step = FMath::Clamp(DesiredZ - CurrentZ, -MaxDown, MaxUp);
				if (!FMath::IsNearlyZero(Step, 0.5f))
				{
					AddActorWorldOffset(FVector(0.0f, 0.0f, Step), false);
				}
			}
			else if (!FMath::IsNearlyEqual(CurrentZ, DesiredZ, 1.0f))
			{
				const float Blend = FMath::Clamp(DeltaSeconds * SuspensionResponse, 0.0f, 1.0f);
				AddActorWorldOffset(FVector(0.0f, 0.0f, (DesiredZ - CurrentZ) * Blend), true);
			}
		}
		else
		{
			// Kein Boden getroffen = ungeladene Zelle oder echtes Loch. Der Wagen
			// faellt, statt in der Luft zu schweben - so misst die Durchfall-
			// Regression einen ECHTEN Karosserie-Sturz statt einer Fehl-Null.
			FallSpeedCmS = AdvanceFallSpeedCmS(FallSpeedCmS, FallGravityCmS2, DeltaSeconds);
			AddActorWorldOffset(FVector(0.0f, 0.0f, -FallSpeedCmS * DeltaSeconds), false);
		}
	}
}

void AWiesbadenCar::UpdateWheels(float DeltaSeconds)
{
	// Radumdrehung: zurueckgelegte Strecke / Radumfang. Die Raeder drehen um
	// ihre Querachse (lokale Y-Achse = Pitch-Komponente des FRotator).
	//
	// Strecke/Umfang ist bereits die Zahl der Umdrehungen - eine dimensionslose
	// Groesse. Sie muss nur mit 360 multipliziert werden, um Grad zu erhalten.
	// Hier stand zuvor zusaetzlich RadiansToDegrees(), was den Wert mit 57,3
	// multiplizierte: die Raeder drehten sich 57-fach zu schnell. Unbemerkt
	// blieb das, weil die Rad-Meshes bis jetzt ausgeblendet waren.
	//
	// Vorzeichenbehaftet, damit sich die Raeder beim Rueckwaertsfahren auch
	// rueckwaerts drehen.
	const double DistanceCm = VehiclePhysics.SpeedMetersPerS * MetersToCm * DeltaSeconds;
	const double WheelCircumferenceCm = 2.0 * PI * VehiclePhysics.WheelRadiusM * MetersToCm;

	WheelRotationPitch += static_cast<float>(
		(DistanceCm / FMath::Max(WheelCircumferenceCm, 1.0)) * 360.0);
	WheelRotationPitch = FMath::Fmod(WheelRotationPitch, 360.0f);

	// Lenkeinschlag der Vorderraeder (visuell, aus dem Physik-Modul).
	const float SteerDegrees = SteeringInput * VehiclePhysics.MaxSteerAngleDeg;

	FrontLeftWheel->SetRelativeRotation(FRotator(WheelRotationPitch, SteerDegrees, 0.0f));
	FrontRightWheel->SetRelativeRotation(FRotator(WheelRotationPitch, SteerDegrees, 0.0f));
	RearLeftWheel->SetRelativeRotation(FRotator(WheelRotationPitch, 0.0f, 0.0f));
	RearRightWheel->SetRelativeRotation(FRotator(WheelRotationPitch, 0.0f, 0.0f));
}

bool AWiesbadenCar::SweepVehicle(const FVector& Delta, FHitResult& OutHit) const
{
	OutHit = FHitResult();

	UWorld* CarWorld = GetWorld();
	if (!CarWorld || !CollisionBox || Delta.IsNearlyZero())
	{
		return false;
	}

	// Die Box dort abtasten, wo sie tatsaechlich sitzt - nicht am
	// Actor-Ursprung. Sie liegt 75 cm hoeher als die Wurzel; ein Sweep am
	// Ursprung liefe halb im Boden und traefe die Fahrbahn statt der Wand.
	const FVector Start = CollisionBox->GetComponentLocation();
	const FQuat Rotation = CollisionBox->GetComponentQuat();
	const FCollisionShape Shape = FCollisionShape::MakeBox(CollisionBox->GetScaledBoxExtent());

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbCarSweep), false);
	Params.AddIgnoredActor(this);

	return CarWorld->SweepSingleByChannel(
		OutHit, Start, Start + Delta, Rotation, ECC_WorldStatic, Shape, Params);
}

APlayerController* AWiesbadenCar::GetCarController()
{
	return Cast<APlayerController>(GetController());
}

bool AWiesbadenCar::IsKeyDown(const FKey& Key)
{
	const APlayerController* PC = GetCarController();
	return PC && PC->IsInputKeyDown(Key);
}
