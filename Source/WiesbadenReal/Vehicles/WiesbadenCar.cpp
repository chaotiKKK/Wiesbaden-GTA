// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenCar.h"

#include "Vehicles/WiesbadenHelicopter.h"   // ApplyStickShaping: eine Kennlinie fuer alle Sticks

#include "WiesbadenReal.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "World/WiesbadenCitySubsystem.h"
#include "World/TrafficVehicleSpawnerComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr double MetersToCm = 100.0;
}

FBeetleAssembly AWiesbadenCar::ChooseBeetleAssembly(
	bool bBodyMeshAvailable, bool bWheelMeshAvailable, bool bHerbieMeshAvailable)
{
	FBeetleAssembly Choice;
	if (bBodyMeshAvailable && bWheelMeshAvailable)
	{
		// Robuster Weg: radlose Karosserie + 4 vermessene Einzelraeder -> immer
		// vier Raeder (das Herbie-Voll-Mesh vermisst ein Hinterrad).
		Choice.Body = EBeetleBodyMesh::SeparateWheelBody;
		Choice.bSeparateWheels = true;
	}
	else if (bHerbieMeshAvailable)
	{
		// Notfall: Herbie mit seinen eigenen (unvollstaendigen) Raedern.
		Choice.Body = EBeetleBodyMesh::HerbieFull;
		Choice.bSeparateWheels = false;
	}
	else
	{
		Choice.Body = EBeetleBodyMesh::Cube;
		Choice.bSeparateWheels = false;
	}
	return Choice;
}

FRotator AWiesbadenCar::WheelVisualRotation(float ForwardRollDegrees,
	float SteeringDegrees, bool bLeftSide)
{
	// Das einzige Rad-Mesh stammt vom rechten Vorderrad; dort zeigt die Felge
	// nach +Y. Links dreht eine halbe Gierumdrehung die Felge nach aussen.
	// UE-Pitch dreht bei positivem Wert den unteren Reifenpunkt nach +X.
	// Beim Vorwaertsrollen muss dieser Punkt nach -X laufen. Die um 180 Grad
	// gedrehte linke Seite braucht dafuer das entgegengesetzte Pitch-Vorzeichen.
	return FRotator(bLeftSide ? ForwardRollDegrees : -ForwardRollDegrees,
		SteeringDegrees + (bLeftSide ? 180.0f : 0.0f), 0.0f);
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

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(SceneRoot);
	VisualRoot->SetRelativeLocation(FVector(0.0f, 0.0f, -GroundClearanceCm));

	// Gefederter Aufbau (Karosserie + Leuchten); die Raeder bleiben am VisualRoot.
	SprungRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SprungRoot"));
	SprungRoot->SetupAttachment(VisualRoot);
	CollisionBox->SetBoxExtent(FVector(204.0f, 78.0f, 75.0f));
	CollisionBox->SetRelativeLocation(FVector(0.0f, 0.0f, 75.0f));
	CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionBox->SetCollisionResponseToAllChannels(ECR_Block);

	// Geschlossene, neu aufgebaute Karosserie mit Lack, Scheiben und Leuchten.
	// Der gescannte Body hat Luecken und verblichene UV-Inseln; er bleibt nur
	// als Asset-Rueckfall erhalten. Die vier separaten Reifen drehen weiter.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RestoredMesh(
		TEXT("/Game/Vehicles/Beetle/Restored/restored_beetle_body/StaticMeshes/SM_VWBeetle1969_Restored.SM_VWBeetle1969_Restored"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HerbieMesh(
		TEXT("/Game/Vehicles/Beetle/SM_Herbie.SM_Herbie"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeetleMesh(
		TEXT("/Game/Vehicles/Beetle/SM_VWBeetle1969_Body.SM_VWBeetle1969_Body"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeetleWheelMesh(
		TEXT("/Game/Vehicles/Beetle/SM_VWBeetle_Wheel.SM_VWBeetle_Wheel"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));

	// Herbie-Lackierung fuer die radlose Karosserie: die vier Material-Instanzen
	// wurden von make_herbie.py auf DIESE Karosserie gebacken (UVs passen).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HerbieMat0(
		TEXT("/Game/Vehicles/Beetle/MI_VWBeetleHerbie_1001.MI_VWBeetleHerbie_1001"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HerbieMat1(
		TEXT("/Game/Vehicles/Beetle/MI_VWBeetleHerbie_1002.MI_VWBeetleHerbie_1002"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HerbieMat2(
		TEXT("/Game/Vehicles/Beetle/MI_VWBeetleHerbie_1003.MI_VWBeetleHerbie_1003"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HerbieMat3(
		TEXT("/Game/Vehicles/Beetle/MI_VWBeetleHerbie_1004.MI_VWBeetleHerbie_1004"));
	UMaterialInterface* HerbieMats[4] = {
		HerbieMat0.Succeeded() ? HerbieMat0.Object : nullptr,
		HerbieMat1.Succeeded() ? HerbieMat1.Object : nullptr,
		HerbieMat2.Succeeded() ? HerbieMat2.Object : nullptr,
		HerbieMat3.Succeeded() ? HerbieMat3.Object : nullptr,
	};

	UStaticMesh* Cube = CubeMesh.Object;
	UStaticMesh* Herbie = HerbieMesh.Succeeded() ? HerbieMesh.Object : nullptr;
	UStaticMesh* Beetle = RestoredMesh.Succeeded() ? RestoredMesh.Object
		: (BeetleMesh.Succeeded() ? BeetleMesh.Object : nullptr);
	UStaticMesh* BeetleWheel = BeetleWheelMesh.Succeeded() ? BeetleWheelMesh.Object : nullptr;

	// Karosserie + Rad-Darstellung aus den verfuegbaren Meshes waehlen (rein/
	// getestet, s. ChooseBeetleAssembly). Das Herbie-Voll-Mesh vermisst ein
	// Hinterrad, daher bevorzugt die radlose Karosserie + 4 Einzelraeder.
	// bBodyIncludesWheels steuert unten das Ausblenden der separaten Raeder.
	const FBeetleAssembly Assembly = ChooseBeetleAssembly(
		Beetle != nullptr, BeetleWheel != nullptr, Herbie != nullptr);
	const bool bBodyIncludesWheels = !Assembly.bSeparateWheels;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(SprungRoot);

	switch (Assembly.Body)
	{
	case EBeetleBodyMesh::SeparateWheelBody:
		// Radlose Karosserie, massstaeblich (4,15 m), Ursprung auf Radaufstands-
		// hoehe - weder skaliert noch versetzt. Die 4 Einzelraeder kommen unten.
		BodyMesh->SetStaticMesh(Beetle);
		BodyMesh->SetRelativeLocation(FVector::ZeroVector);
		BodyMesh->SetRelativeScale3D(FVector::OneVector);
		// Die neue Karosserie besitzt eigene Materialslots fuer den intakten
		// Lack. Die alten vier UV-Kacheln passen nur auf den Scan-Rueckfall.
		if (!RestoredMesh.Succeeded())
		{
			for (int32 Slot = 0; Slot < 4; ++Slot)
			{
				if (HerbieMats[Slot])
				{
					BodyMesh->SetMaterial(Slot, HerbieMats[Slot]);
				}
			}
		}
		break;

	case EBeetleBodyMesh::HerbieFull:
		// Notfall: Herbie-Voll-Mesh. Laengsachse im Import auf +Y, Front auf -Y ->
		// +90 Grad legt die Laengsachse auf +X UND die Front nach vorne; auf
		// Kaefer-Maszstab skalieren (0.838 -> 4,15 m). Bringt eigene Raeder mit.
		BodyMesh->SetStaticMesh(Herbie);
		BodyMesh->SetRelativeLocation(FVector::ZeroVector);
		BodyMesh->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
		BodyMesh->SetRelativeScale3D(FVector(0.838f));
		break;

	default: // EBeetleBodyMesh::Cube
		// Ersatzquader in PKW-Groesse: 440 x 180 x 60 cm, Sitzhoehe ~75 cm.
		BodyMesh->SetStaticMesh(Cube);
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 75.0f));
		BodyMesh->SetRelativeScale3D(FVector(4.4f, 1.8f, 0.6f));
		break;
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
		WheelBasePositions[Index] = WheelPositions[Index];
		Wheel->SetupAttachment(VisualRoot);
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

	// Bringt der Body die Raeder selbst mit (Herbie), die separaten Rad-Meshes
	// ausblenden. Die Fahrphysik nutzt Raycasts, nicht die Rad-Meshes -
	// Ausblenden aendert nur die Optik (die Herbie-Raeder drehen dann nicht mit;
	// das ist ein bewusster Kompromiss fuer den intakten Body).
	if (bBodyIncludesWheels)
	{
		for (UStaticMeshComponent* Wheel : { FrontLeftWheel, FrontRightWheel, RearLeftWheel, RearRightWheel })
		{
			if (Wheel)
			{
				Wheel->SetVisibility(false);
				Wheel->SetHiddenInGame(true);
			}
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
	Lights->SetupAttachment(SprungRoot);

	EngineAudio = CreateDefaultSubobject<UWiesbadenCarAudioComponent>(TEXT("EngineAudio"));
	EngineAudio->SetupAttachment(SceneRoot);
	// Der Kaefer hat den Motor hinten - der Klang kommt von dort.
	EngineAudio->SetRelativeLocation(FVector(-160.0f, 0.0f, 50.0f));

	TireEffects = CreateDefaultSubobject<UWiesbadenTireEffectsComponent>(TEXT("TireEffects"));
	TireEffects->SetupAttachment(SceneRoot);

	if (!Cube)
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Basis-Cube (/Engine/BasicShapes/Cube) nicht gefunden - Platzhalter-Meshes bleiben unsichtbar."));
	}
}

float AWiesbadenCar::ComputeSurfaceGripScale(float RainIntensity)
{
	// Nasser Asphalt haelt deutlich weniger als trockener: bis 35 % Gripverlust
	// bei vollem Niederschlag, linear mit der Naesse. Trocken -> 1,0.
	const float WetGripLoss = 0.35f;
	const float Rain = FMath::Clamp(RainIntensity, 0.0f, 1.0f);
	return FMath::Clamp(1.0f - WetGripLoss * Rain, 0.1f, 1.0f);
}

void AWiesbadenCar::BeginPlay()
{
	Super::BeginPlay();

	// Pro Instanz geaenderte Bodenfreiheit nachziehen (Konstruktor kennt nur
	// den Vorgabewert): die sichtbaren Teile stehen immer auf der Fahrbahn.
	if (VisualRoot)
	{
		VisualRoot->SetRelativeLocation(FVector(0.0f, 0.0f, -GroundClearanceCm));
	}

	// Dev-Override der Belags-Griffigkeit: -WbSurfaceGrip=0.5 erzwingt einen festen
	// Wert (Test/Debug, Vorrang vor dem Wetter). Ohne das Flag kommt der Grip zur
	// Laufzeit aus der Wetter-Naesse (ComputeSurfaceGripScale).
	float GripArg = 1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WbSurfaceGrip="), GripArg))
	{
		SurfaceGripOverride = FMath::Clamp(GripArg, 0.1f, 1.0f);
		bSurfaceGripOverridden = true;
		UE_LOG(LogWbVehicles, Log, TEXT("WbDev: Belags-Griffigkeit fest auf %.2f (Override)."), SurfaceGripOverride);
	}

	bDriveTelemetry = FParse::Param(FCommandLine::Get(), TEXT("WbFahrTelemetrie"));

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
				UWiesbadenCarLightsComponent::ShouldUseHeadlights(WeatherState.SunElevationFactor()));
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

	// Traktions-Flags fuer die HUD-Kontrollleuchte spiegeln - reine Anzeige,
	// keine Wirkung auf die Fahrt (das HUD liest sie ueber die Steuernaht).
	bLastWheelSpin = Output.bWheelSpin;
	bLastWheelLock = Output.bWheelLock;

	// Reifen-Effekte: den Schlupf-Zustand nur KONSUMIEREN (keine Physikaenderung).
	// Welche Raeder Gummi lassen: beim Blockieren alle vier, beim Radspin die
	// angetriebenen (hinten), beim Drift ebenfalls das kommende Heck.
	if (TireEffects)
	{
		TArray<FVector> Marks;
		const float WheelRadiusCm = VehiclePhysics.WheelRadiusM * 100.0f;
		auto Contact = [WheelRadiusCm](const UStaticMeshComponent* Wheel) -> FVector
		{
			return Wheel->GetComponentLocation() - FVector(0.0f, 0.0f, WheelRadiusCm);
		};
		if (Output.bWheelLock && FrontLeftWheel && FrontRightWheel && RearLeftWheel && RearRightWheel)
		{
			Marks = { Contact(FrontLeftWheel), Contact(FrontRightWheel),
				Contact(RearLeftWheel), Contact(RearRightWheel) };
		}
		else if ((Output.bWheelSpin || FMath::Abs(Output.SlipAngleDeg) > 8.0f)
			&& RearLeftWheel && RearRightWheel)
		{
			Marks = { Contact(RearLeftWheel), Contact(RearRightWheel) };
		}

		const FVector TravelDir = GetActorForwardVector() * FMath::Sign(Output.ForwardSpeedMetersPerS);
		TireEffects->UpdateTireEffects(
			Output.bWheelSpin, Output.bWheelLock, Output.SlipAngleDeg, Output.SpeedKmh, Marks, TravelDir);
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
	// Angezeigte Drehzahl = Basis + Radspin-Flare: beim Durchdrehen dreht der
	// Motor hoch, waehrend die Fahrt kaum zunimmt (siehe WheelSpinFlare). Die
	// interne EngineRpm (Schalten/Drehmoment) bleibt davon unberuehrt.
	return FMath::Clamp(
		VehiclePhysics.EngineRpm + VehiclePhysics.WheelSpinFlare,
		VehiclePhysics.Powertrain.IdleRpm * 0.5f,
		VehiclePhysics.Powertrain.MaxRpm * 1.15f);
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

void AWiesbadenCar::ComputeBodyTilt(
	float LongAccelMs2, float LatAccelMs2,
	float PitchPerMs2, float RollPerMs2,
	float MaxPitchDeg, float MaxRollDeg,
	float Response, float Dt,
	float& InOutPitchDeg, float& InOutRollDeg)
{
	// Ziel-Neigung aus den Beschleunigungen, an den Anschlag geklemmt.
	float TargetPitch = 0.0f;
	float TargetRoll = 0.0f;
	ComputeBodyTiltTarget(LongAccelMs2, LatAccelMs2, PitchPerMs2, RollPerMs2,
		MaxPitchDeg, MaxRollDeg, TargetPitch, TargetRoll);

	// Exponentielle Glaettung, rahmenratenunabhaengig: die Federung braucht
	// einen Moment, bis die Karosserie steht - ein sofortiger Sprung saehe
	// nach Ruck statt nach Masse aus.
	const float Alpha = 1.0f - FMath::Exp(-FMath::Max(Response, 0.0f) * FMath::Max(Dt, 0.0f));
	InOutPitchDeg = FMath::Lerp(InOutPitchDeg, TargetPitch, Alpha);
	InOutRollDeg = FMath::Lerp(InOutRollDeg, TargetRoll, Alpha);
}

void AWiesbadenCar::ComputeBodyTiltTarget(
	float LongAccelMs2, float LatAccelMs2,
	float PitchPerMs2, float RollPerMs2,
	float MaxPitchDeg, float MaxRollDeg,
	float& OutPitchDeg, float& OutRollDeg)
{
	OutPitchDeg = FMath::Clamp(LongAccelMs2 * PitchPerMs2, -MaxPitchDeg, MaxPitchDeg);
	OutRollDeg = FMath::Clamp(LatAccelMs2 * RollPerMs2, -MaxRollDeg, MaxRollDeg);
}

void AWiesbadenCar::AdvanceBodySpring(float Target, float ExternalAccel,
	float NaturalHz, float DampingRatio, float Limit, float Dt,
	float& InOutValue, float& InOutRate)
{
	const float Omega = 2.0f * PI * FMath::Max(NaturalHz, 0.05f);
	const float Zeta = FMath::Max(DampingRatio, 0.0f);
	// Lange Bilder (Hitch, Laden) nicht als einen grossen Schritt rechnen: die
	// Feder wuerde dabei Energie gewinnen und aufschwingen.
	float Remaining = FMath::Clamp(Dt, 0.0f, 0.25f);
	while (Remaining > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Remaining, 1.0f / 240.0f);
		const float Accel = Omega * Omega * (Target - InOutValue)
			- 2.0f * Zeta * Omega * InOutRate + ExternalAccel;
		InOutRate += Accel * Step;       // semi-implizit: erst Geschwindigkeit,
		InOutValue += InOutRate * Step;  // dann Lage - stabil fuer Federn
		if (Limit > 0.0f && FMath::Abs(InOutValue) > Limit)
		{
			// Anschlag: der Aufbau steht, die Bewegung nach aussen ist weg.
			InOutValue = FMath::Sign(InOutValue) * Limit;
			if (InOutRate * InOutValue > 0.0f)
			{
				InOutRate = 0.0f;
			}
		}
		Remaining -= Step;
	}
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

	// Belags-Griffigkeit aus der WELT: der Dev-Override hat Vorrang (Messfahrten),
	// sonst kommt der Wert aus der Wetter-Naesse des City-Subsystems - bei Regen
	// sinkt der Grip, ganz ohne Kommandozeile. Nur ABFRAGEN, nicht rechnen: die
	// Physik bleibt unveraendert.
	if (bSurfaceGripOverridden)
	{
		Input.SurfaceGripScale = SurfaceGripOverride;
	}
	else if (const UWorld* CarWorld = GetWorld())
	{
		float Rain = 0.0f;
		if (const UWiesbadenCitySubsystem* City = CarWorld->GetSubsystem<UWiesbadenCitySubsystem>())
		{
			Rain = City->GetWeatherState().Intensity.Rain;
		}
		Input.SurfaceGripScale = ComputeSurfaceGripScale(Rain);
	}

	FWiesbadenVehiclePhysicsOutput Output;
	VehiclePhysics.Tick(Input, DeltaSeconds, Output);

	// Licht und Klang direkt aus dem Physikergebnis speisen, damit Bremslicht
	// und Motordrehzahl im selben Frame stimmen wie die Bewegung.
	UpdateLightsAndAudio(Output);

	// Gewichtsverlagerung: die Karosserie nickt und wankt aus den
	// Beschleunigungen. Querbeschleunigung = v * Gierrate (Zentripetalanteil).
	// Rein visuell an der BodyMesh - die Raeder haengen an SceneRoot und
	// bleiben am Boden, die Actor-Kollision bleibt unberuehrt.
	const float LateralAccelMs2 = Output.ForwardSpeedMetersPerS * Output.YawRateRadPerS;

	// FEDER-MASSE statt Glaettung: Ziel aus den Beschleunigungen wie bisher, aber
	// die Karosserie schwingt darueber hinaus und kommt nach dem Anhalten zurueck.
	// Dazu die Traegheit gegen die Wurzel: faehrt der Wagen eine Kante hoch oder
	// kippt die Bodenebene, bleibt der Aufbau zurueck und federt nach.
	float TargetPitch = 0.0f;
	float TargetRoll = 0.0f;
	ComputeBodyTiltTarget(
		Output.ForwardAccelerationMetersPerS2, LateralAccelMs2,
		BodyPitchPerMeterPerS2, BodyRollPerMeterPerS2,
		BodyMaxPitchDeg, BodyMaxRollDeg, TargetPitch, TargetRoll);
	AdvanceBodySpring(TargetPitch, -RootPitchAccel, BodySpringHz, BodyDampingRatio,
		BodyMaxPitchDeg * 1.5f, DeltaSeconds, BodyPitchDeg, BodyPitchRate);
	AdvanceBodySpring(TargetRoll, -RootRollAccel, BodySpringHz, BodyDampingRatio,
		BodyMaxRollDeg * 1.5f, DeltaSeconds, BodyRollDeg, BodyRollRate);
	AdvanceBodySpring(0.0f, -RootAccelZ, BodySpringHz, BodyDampingRatio,
		BodyHeaveMaxCm, DeltaSeconds, BodyHeaveCm, BodyHeaveRate);
	if (SprungRoot)
	{
		// Neigung und Hub im FAHRZEUG-Rahmen auf den gefederten Traeger - Mesh und
		// Leuchten behalten darunter ihre Eigenorientierung.
		SprungRoot->SetRelativeLocationAndRotation(
			FVector(0.0f, 0.0f, BodyHeaveCm), FRotator(BodyPitchDeg, 0.0f, BodyRollDeg));
	}

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

		// ZUSAMMENSTOSS MIT EINEM VERKEHRSAUTO: kein Anprall an eine Wand,
		// sondern ein Stoss nach Impulserhaltung - das Verkehrsauto wird
		// verschoben und gedreht (sein Fahrer bremst und faehrt danach
		// weiter), das Spielerauto verliert genau den abgegebenen Impuls.
		bool bTrafficImpact = false;
		if (const AActor* HitActor = MoveHit.GetActor())
		{
			const UTrafficVehicleSpawnerComponent* Traffic =
				HitActor->FindComponentByClass<UTrafficVehicleSpawnerComponent>();
			const int32 TrafficId = Traffic ? Traffic->FindVehicleIdForProxy(MoveHit.GetComponent()) : INDEX_NONE;
			UWiesbadenCitySubsystem* City = (TrafficId != INDEX_NONE && GetWorld())
				? GetWorld()->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
			if (City)
			{
				const FVector PlayerVelocity = Forward * SpeedCmPerS + Right * LateralCmPerS;
				FVector PlayerDeltaV;
				if (City->TrafficSimulation.ApplyPlayerImpact(TrafficId, MoveHit.ImpactPoint,
					-MoveHit.ImpactNormal, PlayerVelocity, VehiclePhysics.Powertrain.MassKg, PlayerDeltaV))
				{
					VehiclePhysics.SpeedMetersPerS += static_cast<float>(FVector::DotProduct(PlayerDeltaV, Forward) / MetersToCm);
					VehiclePhysics.LateralVelocityMetersPerS += static_cast<float>(FVector::DotProduct(PlayerDeltaV, Right) / MetersToCm);
					bTrafficImpact = true;
					UE_LOG(LogWbVehicles, Log,
						TEXT("Zusammenstoss mit Verkehrsauto #%d bei %.0f km/h: Spielerauto %+.0f km/h."),
						TrafficId, PlayerVelocity.Size() * 0.036, FVector::DotProduct(PlayerDeltaV, Forward) * 0.036);
				}
			}
		}

		// Anprall kostet Tempo, streifen kaum: der Verlust richtet sich danach,
		// wie frontal die Wand getroffen wurde.
		if (!bTrafficImpact)
		{
			const float Frontal = FMath::Abs(FVector::DotProduct(Forward, MoveHit.Normal));
			VehiclePhysics.SpeedMetersPerS *= FMath::Lerp(0.98f, 0.25f, Frontal);
		}
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
				City->PlayPedestrianBurstSound(Front);
				// Jedes Ueberfahren ist eine Tat ins Fahndungskonto - sonst
				// wuerde die Polizei nur auf Schuesse reagieren, nicht auf
				// den drastischsten Fall.
				for (int32 HitIndex = 0; HitIndex < Hit; ++HitIndex)
				{
					City->ReportCrime(EWiesbadenCrimeEvent::PedestrianDowned);
				}
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

		// Boden unter jedem Rad - fuer die Bodenebene und den Federweg je Rad.
		TraceWheelGround();

		if (CarWorld->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			// Boden gefunden: der Wagen faellt nicht, Fallgeschwindigkeit zurueck.
			FallSpeedCmS = 0.0f;

			// Neigung: Hochachse auf die Flaechennormale, Vorwaertsachse so nah
			// wie moeglich an der bisherigen Fahrtrichtung. Sehr steile oder
			// senkrechte Treffer (Bordsteinkante, Hauswand) werden ignoriert -
			// sie wuerden den Wagen auf die Seite legen.
			FVector GroundNormal = Hit.ImpactNormal;
			float GroundZ = static_cast<float>(Hit.Location.Z);
			const FVector ForwardFlat =
				FVector::VectorPlaneProject(GetActorForwardVector(), FVector::UpVector).GetSafeNormal();

			// BODENEBENE AUS DEN VIER RADPUNKTEN statt eines Strahls in der Mitte:
			// ein Bordstein unter einem Rad hebt diese Ecke, eine Welle zwischen
			// den Achsen kippt den Wagen. Vorher sah der Mittelstrahl nur, was
			// genau unter dem Wagenzentrum lag - die Raeder schwebten oder sanken
			// an Kanten bis 15 cm (gemessen 29.09.2026). Radpunkte weit weg vom
			// Mittelstrahl (Mauerkrone, Graben) gelten als nicht getroffen.
			if (!bFlyingOverBuilding && !ForwardFlat.IsNearlyZero())
			{
				float H[4];
				int32 Hits = 0;
				for (int32 Index = 0; Index < 4; ++Index)
				{
					const bool bUsable = bWheelGroundHit[Index]
						&& FMath::Abs(WheelGroundZ[Index] - GroundZ) <= 45.0f;
					H[Index] = bUsable ? WheelGroundZ[Index] : GroundZ;
					Hits += bUsable ? 1 : 0;
				}
				if (Hits >= 3)
				{
					const FVector* W = WheelBasePositions;
					const float Dx = 0.5f * ((W[0].X + W[1].X) - (W[2].X + W[3].X));
					const float Dy = 0.5f * ((W[1].Y + W[3].Y) - (W[0].Y + W[2].Y));
					const float SlopeX = 0.5f * ((H[0] + H[1]) - (H[2] + H[3])) / FMath::Max(Dx, 1.0f);
					const float SlopeY = 0.5f * ((H[1] + H[3]) - (H[0] + H[2])) / FMath::Max(Dy, 1.0f);
					const FVector RightFlat = FVector::CrossProduct(FVector::UpVector, ForwardFlat);
					GroundNormal = (FVector::UpVector - SlopeX * ForwardFlat - SlopeY * RightFlat).GetSafeNormal();
					// Hoehe der Ebene am Fahrzeugursprung (die Radmitte liegt vorn).
					const float CenterX = 0.25f * (W[0].X + W[1].X + W[2].X + W[3].X);
					GroundZ = 0.25f * (H[0] + H[1] + H[2] + H[3]) - SlopeX * CenterX;
				}
			}

			if (GroundNormal.Z > 0.5f && !ForwardFlat.IsNearlyZero())
			{
				const FRotator Target = FRotationMatrix::MakeFromZX(GroundNormal, ForwardFlat).Rotator();
				SetActorRotation(FMath::RInterpTo(
					GetActorRotation(), Target, DeltaSeconds, GroundAlignResponse));
			}

			float DesiredZ = GroundZ + GroundClearanceCm;
			const float CurrentZ = GetActorLocation().Z;
			TelemetryHeaveCm = CurrentZ - DesiredZ;

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

	UpdateWheelTravel(DeltaSeconds);
	UpdateRootMotion(DeltaSeconds);

	// Fahrtelemetrie: eine Zeile je 0,1 s Spielzeit. Querbeschleunigung wie beim
	// Wanken als v * Gierrate; Nicken/Wanken sind die sichtbare Karosserie-
	// Neigung, Hub der Abstand der Wurzel zu ihrer Sollhoehe. "gleit" ist der
	// innere Zustand "Raeder gleiten" (blockiert, keine Seitenfuehrung) - anders
	// als "block", das auch meldet, wenn die Bremse nur an der Haftgrenze regelt.
	if (bDriveTelemetry && GetWorld())
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now >= DriveTelemetryNextTime)
		{
			DriveTelemetryNextTime = Now + 0.1;
			// Boden = mittlere Bodenhoehe unter den Raedern (Kanten-Erkennung),
			// spalt = groesster Abstand Reifen/Boden, fz = Karosseriehub.
			float GroundSum = 0.0f;
			int32 GroundHits = 0;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				if (bWheelGroundHit[Index]) { GroundSum += WheelGroundZ[Index]; ++GroundHits; }
			}
			UE_LOG(LogWbVehicles, Log,
				TEXT("WbFahrt t=%.2f v=%.2f ax=%.2f ay=%.2f gier=%.2f schwimm=%.2f lenk=%.3f gas=%.2f bremse=%.2f nick=%.2f wank=%.2f hub=%.2f spin=%d block=%d gleit=%d gang=%d boden=%.1f spalt=%.2f fz=%.2f"),
				Now, Output.SpeedKmh, Output.ForwardAccelerationMetersPerS2, LateralAccelMs2,
				FMath::RadiansToDegrees(Output.YawRateRadPerS), Output.SlipAngleDeg,
				Output.SteerAngleNorm, ThrottleInput, BrakeInput, BodyPitchDeg, BodyRollDeg,
				TelemetryHeaveCm, Output.bWheelSpin ? 1 : 0, Output.bWheelLock ? 1 : 0,
				VehiclePhysics.bBrakeLockState ? 1 : 0, Output.Gear,
				GroundHits > 0 ? GroundSum / GroundHits : 0.0f, ComputeMaxWheelGapCm(), BodyHeaveCm);
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

	FrontLeftWheel->SetRelativeRotation(WheelVisualRotation(WheelRotationPitch, SteerDegrees, true));
	FrontRightWheel->SetRelativeRotation(WheelVisualRotation(WheelRotationPitch, SteerDegrees, false));
	RearLeftWheel->SetRelativeRotation(WheelVisualRotation(WheelRotationPitch, 0.0f, true));
	RearRightWheel->SetRelativeRotation(WheelVisualRotation(WheelRotationPitch, 0.0f, false));
}

void AWiesbadenCar::TraceWheelGround()
{
	UWorld* CarWorld = GetWorld();
	if (!CarWorld)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbCarWheelGround), true);
	Params.AddIgnoredActor(this);
	const FTransform& Transform = GetActorTransform();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		// Senkrecht unter dem Radaufstandspunkt, vom Rad aus gesehen 1,5 m hoch
		// beginnend (nicht im Boden starten) und 4 m tief suchend.
		const FVector Contact = Transform.TransformPosition(
			FVector(WheelBasePositions[Index].X, WheelBasePositions[Index].Y, 0.0f));
		FHitResult Hit;
		const bool bHit = CarWorld->LineTraceSingleByChannel(Hit,
			Contact + FVector(0.0f, 0.0f, 150.0f), Contact - FVector(0.0f, 0.0f, 400.0f),
			ECC_WorldStatic, Params);
		// Steile Treffer (Hauswand, Bordsteinflanke) sind kein Aufstand.
		bWheelGroundHit[Index] = bHit && Hit.ImpactNormal.Z > 0.5f;
		WheelGroundZ[Index] = bHit ? static_cast<float>(Hit.ImpactPoint.Z) : 0.0f;
	}
}

void AWiesbadenCar::UpdateWheelTravel(float DeltaSeconds)
{
	// Die Raeder sind UNGEFEDERT: jedes folgt seinem eigenen Boden innerhalb des
	// Federwegs, die Karosserie (SprungRoot) bleibt davon unberuehrt. So steht an
	// einer Kante jedes Rad auf dem Boden, statt mit dem Wagen zu schweben.
	UStaticMeshComponent* Wheels[4] = { FrontLeftWheel, FrontRightWheel, RearLeftWheel, RearRightWheel };
	const FTransform& Transform = GetActorTransform();
	const float RadiusCm = VehiclePhysics.WheelRadiusM * MetersToCm;
	const float UpZ = FMath::Max(static_cast<float>(GetActorUpVector().Z), 0.3f);
	const float Alpha = 1.0f - FMath::Exp(-40.0f * FMath::Max(DeltaSeconds, 0.0f));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (!Wheels[Index])
		{
			continue;
		}
		float Target = 0.0f;
		if (bWheelGroundHit[Index])
		{
			// Radmitte in Ruhelage (VisualRoot liegt GroundClearanceCm unter der Wurzel).
			const FVector Rest = Transform.TransformPosition(FVector(WheelBasePositions[Index].X,
				WheelBasePositions[Index].Y, WheelBasePositions[Index].Z - GroundClearanceCm));
			Target = FMath::Clamp((WheelGroundZ[Index] + RadiusCm - static_cast<float>(Rest.Z)) / UpZ,
				-WheelTravelMaxCm, WheelTravelMaxCm);
		}
		WheelTravelCm[Index] = FMath::Lerp(WheelTravelCm[Index], Target, Alpha);
		Wheels[Index]->SetRelativeLocation(WheelBasePositions[Index] + FVector(0.0f, 0.0f, WheelTravelCm[Index]));
	}
}

void AWiesbadenCar::UpdateRootMotion(float DeltaSeconds)
{
	if (DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const float Z = static_cast<float>(GetActorLocation().Z);
	const FRotator Rot = GetActorRotation();
	// Erstes Bild oder Teleport (-WbGoto, Aussteigen, Fall): keine Traegheit
	// daraus ableiten - ein Meter Sprung waere sonst ein Karosserie-Schlag.
	if (!bRootMotionValid || FMath::Abs(Z - LastRootZ) > 150.0f)
	{
		bRootMotionValid = true;
		LastRootZ = Z;
		LastRootPitch = Rot.Pitch;
		LastRootRoll = Rot.Roll;
		LastRootVelZ = LastRootPitchRate = LastRootRollRate = 0.0f;
		RootAccelZ = RootPitchAccel = RootRollAccel = 0.0f;
		return;
	}
	const float VelZ = (Z - LastRootZ) / DeltaSeconds;
	const float PitchRate = FMath::FindDeltaAngleDegrees(LastRootPitch, Rot.Pitch) / DeltaSeconds;
	const float RollRate = FMath::FindDeltaAngleDegrees(LastRootRoll, Rot.Roll) / DeltaSeconds;
	// Gedeckelt: eine Kante ist ein kurzer Stoss, kein Crash - und die zweite
	// Ableitung einzelner Bilder rauscht.
	RootAccelZ = FMath::Clamp((VelZ - LastRootVelZ) / DeltaSeconds, -2000.0f, 2000.0f);
	RootPitchAccel = FMath::Clamp((PitchRate - LastRootPitchRate) / DeltaSeconds, -500.0f, 500.0f);
	RootRollAccel = FMath::Clamp((RollRate - LastRootRollRate) / DeltaSeconds, -500.0f, 500.0f);
	LastRootZ = Z;
	LastRootVelZ = VelZ;
	LastRootPitch = Rot.Pitch;
	LastRootPitchRate = PitchRate;
	LastRootRoll = Rot.Roll;
	LastRootRollRate = RollRate;
}

float AWiesbadenCar::ComputeMaxWheelGapCm() const
{
	const UStaticMeshComponent* Wheels[4] = { FrontLeftWheel, FrontRightWheel, RearLeftWheel, RearRightWheel };
	const float RadiusCm = VehiclePhysics.WheelRadiusM * MetersToCm;
	float MaxGap = 0.0f;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (bWheelGroundHit[Index] && Wheels[Index])
		{
			const float Bottom = static_cast<float>(Wheels[Index]->GetComponentLocation().Z) - RadiusCm;
			MaxGap = FMath::Max(MaxGap, FMath::Abs(Bottom - WheelGroundZ[Index]));
		}
	}
	return MaxGap;
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
