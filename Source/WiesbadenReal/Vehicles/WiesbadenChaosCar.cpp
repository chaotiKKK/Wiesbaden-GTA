// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenChaosCar.h"

#include "WiesbadenReal.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

#include "ChaosVehicleWheel.h"
#include "Vehicles/WiesbadenChaosWheels.h"

AWiesbadenChaosCar::AWiesbadenChaosCar()
{
	PrimaryActorTick.bCanEverTick = true;

	USkeletalMeshComponent* Body = GetMesh();

	// Das Skelett-Mesh mit den vier Radknochen. Ohne es hat Chaos nichts, an
	// dem es die Raeder aufhaengen kann - das Fahrzeug stuende bewegungslos.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BeetleMesh(
		TEXT("/Game/Vehicles/Beetle/SK_VWBeetle.SK_VWBeetle"));
	if (BeetleMesh.Succeeded() && Body)
	{
		Body->SetSkeletalMesh(BeetleMesh.Object);
	}

	if (Body)
	{
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Body->SetCollisionResponseToAllChannels(ECR_Block);
	}

	UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement();
	if (!Movement)
	{
		return;
	}

	// -- Raeder --------------------------------------------------------------
	//
	// Reihenfolge und Knochennamen muessen zum Skelett passen
	// (Tools/Blender/rig_beetle.py). Ein Tippfehler hier faellt nicht als
	// Fehler auf, sondern als Fahrzeug, das auf der Stelle steht.
	Movement->WheelSetups.SetNum(4);

	Movement->WheelSetups[0].WheelClass = UWiesbadenWheelFront::StaticClass();
	Movement->WheelSetups[0].BoneName = FName(TEXT("Wheel_FL"));
	Movement->WheelSetups[0].AdditionalOffset = FVector::ZeroVector;

	Movement->WheelSetups[1].WheelClass = UWiesbadenWheelFront::StaticClass();
	Movement->WheelSetups[1].BoneName = FName(TEXT("Wheel_FR"));
	Movement->WheelSetups[1].AdditionalOffset = FVector::ZeroVector;

	Movement->WheelSetups[2].WheelClass = UWiesbadenWheelRear::StaticClass();
	Movement->WheelSetups[2].BoneName = FName(TEXT("Wheel_BL"));
	Movement->WheelSetups[2].AdditionalOffset = FVector::ZeroVector;

	Movement->WheelSetups[3].WheelClass = UWiesbadenWheelRear::StaticClass();
	Movement->WheelSetups[3].BoneName = FName(TEXT("Wheel_BR"));
	Movement->WheelSetups[3].AdditionalOffset = FVector::ZeroVector;

	// -- Motor ---------------------------------------------------------------
	//
	// Kaefer 1302 von 1969, 1,5-Liter-Boxer: 44 PS bei 4000 Umdrehungen,
	// 102 Nm bei 2600. Die Drehmomentkurve gilt als Vielfaches von MaxTorque,
	// ihr Hoechstwert liegt deshalb bei 1,0.
	Movement->EngineSetup.MaxTorque = 102.0f;
	Movement->EngineSetup.MaxRPM = 4600.0f;
	Movement->EngineSetup.EngineIdleRPM = 800.0f;
	Movement->EngineSetup.EngineBrakeEffect = 0.15f;

	if (FRichCurve* Torque = Movement->EngineSetup.TorqueCurve.GetRichCurve())
	{
		Torque->Reset();
		// Boxermotor mit langem, flachem Verlauf und deutlichem Abfall oben
		// heraus - der Kaefer dreht nicht gern hoch.
		Torque->AddKey(800.0f, 0.72f);
		Torque->AddKey(1600.0f, 0.90f);
		Torque->AddKey(2600.0f, 1.00f);
		Torque->AddKey(3400.0f, 0.95f);
		Torque->AddKey(4000.0f, 0.85f);
		Torque->AddKey(4600.0f, 0.62f);
	}

	// -- Getriebe ------------------------------------------------------------
	//
	// Vier Gaenge mit den Uebersetzungen des Originals, Achsantrieb 4,375.
	Movement->TransmissionSetup.bUseAutomaticGears = true;
	Movement->TransmissionSetup.bUseAutoReverse = true;
	Movement->TransmissionSetup.FinalRatio = 4.375f;
	Movement->TransmissionSetup.ForwardGearRatios = { 3.80f, 2.06f, 1.32f, 0.89f };
	Movement->TransmissionSetup.ReverseGearRatios = { 3.61f };
	Movement->TransmissionSetup.GearChangeTime = 0.35f;
	Movement->TransmissionSetup.TransmissionEfficiency = 0.92f;

	// -- Antrieb -------------------------------------------------------------
	//
	// HECKANTRIEB. Der Motor sitzt beim Kaefer hinten ueber der Antriebsachse;
	// zusammen mit der Gewichtsverteilung von rund 40:60 ergibt das sein
	// eigenwilliges Fahrverhalten. Frontantrieb waere hier nicht nur falsch,
	// sondern liesse den Wagen voellig anders fahren.
	Movement->DifferentialSetup.DifferentialType = EVehicleDifferential::RearWheelDrive;

	// -- Aufbau --------------------------------------------------------------
	//
	// Der Kaefer ist hoch und schmal und waelzt sich in Kurven. Der
	// Schwerpunkt liegt hinten (Motor) und tief; ohne diese Vorgabe kippt er
	// in der Physik leichter als in der Wirklichkeit.
	Movement->bEnableCenterOfMassOverride = true;
	Movement->CenterOfMassOverride = FVector(-20.0f, 0.0f, 10.0f);

	Movement->ChassisHeight = 140.0f;
	Movement->DragCoefficient = 0.48f;   // Kaefer: cW 0,48

	// Leergewicht des Kaefers.
	//
	// Zwei Anlaeufe haben hier nicht gewirkt, weil ich an der falschen Stelle
	// gedreht habe. Erst `SetMassOverrideInKg` am Skelett-Mesh im Konstruktor,
	// dann dasselbe im BeginPlay - beide Male meldete das Protokoll weiter
	// 1500 kg. Meine zweite Erklaerung ("im Konstruktor gibt es den
	// Physikkoerper noch nicht") war ebenfalls falsch.
	//
	// Die Fahrzeugkomponente hat eine EIGENE Masse und setzt damit das
	// Fahrgestell, unabhaengig vom Physik-Asset. Ihr Vorgabewert ist genau
	// jene 1500 kg. Die Engine begruendet das so:
	//
	//   "Mass to set the vehicle chassis to. It's much easier to tweak vehicle
	//    settings when the mass doesn't change due to tweaks with the physics
	//    asset."
	//
	// 680 kg zu viel sind kein Schoenheitsfehler: Beschleunigung, Bremsweg und
	// Seitenneigung haengen unmittelbar daran.
	Movement->Mass = 820.0f;

	// -- Anbauteile ----------------------------------------------------------
	VehicleCamera = CreateDefaultSubobject<UWiesbadenVehicleCameraComponent>(TEXT("VehicleCamera"));
	VehicleCamera->SetupAttachment(Body);

	// Auf Dachhoehe, wie beim bisherigen Fahrzeug. Ohne den Versatz haengt die
	// Kamera im Radkasten - der erste Probelauf zeigte genau das: ein Bild von
	// unten gegen Reifen und Bodenblech.
	VehicleCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 110.0f));

	// Fahrerauge (Ich-Perspektive) wie beim AWiesbadenCar; die eigene Karosserie
	// wird dabei fuer den Fahrer ausgeblendet (kein modellierter Innenraum).
	VehicleCamera->CockpitOffset = FVector(18.0f, -32.0f, 8.0f);
	VehicleCamera->AddCockpitHiddenMesh(GetMesh());

	Lights = CreateDefaultSubobject<UWiesbadenCarLightsComponent>(TEXT("Lights"));
	Lights->SetupAttachment(Body);

	EngineAudio = CreateDefaultSubobject<UWiesbadenCarAudioComponent>(TEXT("EngineAudio"));
	EngineAudio->SetupAttachment(Body);
}

UChaosWheeledVehicleMovementComponent* AWiesbadenChaosCar::GetChaosMovement() const
{
	return Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
}

void AWiesbadenChaosCar::BeginPlay()
{
	Super::BeginPlay();

	// Physiksimulation ausdruecklich einschalten.
	//
	// Der Zustandsbericht der Fahrprobe sagte "Physik simuliert NEIN" - bei
	// vier korrekt erzeugten Raedern, aktiver Komponente und einem
	// Physik-Asset mit genau einem Koerper. Das Fahrzeug war vollstaendig
	// eingerichtet und stand still, weil sein Koerper gar nicht simuliert
	// wurde. Die Drehzahl blieb deshalb bei 0, nicht bei den 800 des
	// Leerlaufs.
	//
	// Meine Vermutung war eine andere gewesen - Koerper an den Radknochen, die
	// die Aufhaengung blockieren. Das Physik-Asset hat genau EINEN Koerper;
	// die Vermutung war falsch, und ohne den Zustandsbericht haette ich an der
	// falschen Stelle gesucht.
	//
	// Der Aufruf gehoert hierher und nicht in den Konstruktor: Dort wird der
	// Physikzustand erst danach angelegt.
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetSimulatePhysics(true);
	}

	// Schlafen unterbinden.
	//
	// Der Wagen steht bei EXAKT 0,00 cm/s, alle vier Raeder melden "Luft" -
	// obwohl drei von ihnen nachweislich innerhalb der Abtastreichweite ueber
	// der Fahrbahn stehen (44, 27 und 43 cm bei 46,3 cm Reichweite) und beide
	// Kollisionskanaele die Strasse dort finden.
	//
	// In ChaosWheeledVehicleMovementComponent steht der Grund als erste
	// Abfrage der Radabtastung: Ist das Fahrzeug im Schlafzustand, wird gar
	// nicht getastet, sondern der letzte Wert weiterbenutzt. Ein Fahrzeug, das
	// beim Einsetzen einschlaeft, bevor es je Bodenkontakt hatte, kommt aus
	// diesem Zustand nicht mehr heraus: Ohne Abtastung keine Federkraft, ohne
	// Federkraft keine Bewegung, ohne Bewegung kein Aufwachen.
	//
	// Zwei frueher vermutete Ursachen sind widerlegt: Der Kollisionskanal ist
	// es nicht (Sicht- und Fahrzeugkanal treffen beide), und die Reichweite
	// auch nicht (drei Raeder liegen darin).
	//
	// NACHTRAG: Auch diese Erklaerung traegt nicht. Der Schlafzustand wird laut
	// Engine-Quelle nur erreicht, wenn `bAllWheelsOnGround` gilt - und genau
	// das ist hier nicht der Fall. Ausserdem weckt jede Eingabe das Fahrzeug,
	// und die Fahrprobe gibt Vollgas. Bleibt als Sicherung, kostet nichts.
	//
	// `DisableVehicleSleep` liegt uebrigens NICHT an der Komponente, sondern in
	// einer globalen Diagnosestruktur; erreichbar ist es nur ueber die
	// Konsolenvariable p.Vehicle.DisableVehicleSleep.
	if (UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement())
	{
		Movement->SetSleeping(false);
	}

	if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(
			TEXT("p.Vehicle.DisableVehicleSleep")))
	{
		Var->Set(1, ECVF_SetByCode);
	}

	// Die Zahlen einmal ins Protokoll. Ein Fahrzeug, das sich nicht bewegt,
	// hat entweder keine Raeder (Knochennamen falsch), keine Masse oder kein
	// Drehmoment - und welches davon, sieht man sonst nicht.
	if (const UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement())
	{
		UE_LOG(LogWbCore, Log,
			TEXT("Chaos-Fahrzeug: %d Raeder, %.0f Nm, max %.0f 1/min, %d Gaenge, ")
			TEXT("Achsantrieb %.3f, Masse %.0f kg."),
			Movement->WheelSetups.Num(),
			Movement->EngineSetup.MaxTorque,
			Movement->EngineSetup.MaxRPM,
			Movement->TransmissionSetup.ForwardGearRatios.Num(),
			Movement->TransmissionSetup.FinalRatio,
			Movement->Mass);
	}
	else
	{
		UE_LOG(LogWbCore, Error,
			TEXT("Chaos-Fahrzeug: keine Fahrzeugkomponente - es wird stehen bleiben."));
	}
}

void AWiesbadenChaosCar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TickSelfTest(DeltaSeconds);
	if (bSelfTestActive)
	{
		return;
	}

	// Reihenfolge: Selbsttest > externe Naht (KI/Harness/WbDrive) > Tastatur.
	// So faehrt der ChaosCar ueber DIESELBE Naht wie der kinematische Car.
	if (bExternalControlActive)
	{
		ApplyExternalControl();
	}
	else
	{
		ReadInput(DeltaSeconds);
	}
}

void AWiesbadenChaosCar::SetExternalControl(const FWiesbadenCarControl& Control)
{
	ExternalControl = Control;
	bExternalControlActive = true;
}

void AWiesbadenChaosCar::ClearExternalControl()
{
	bExternalControlActive = false;
}

void AWiesbadenChaosCar::ApplyExternalControl()
{
	UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement();
	if (!Movement)
	{
		return;
	}

	Movement->SetThrottleInput(FMath::Clamp(ExternalControl.Throttle, 0.0f, 1.0f));
	Movement->SetBrakeInput(FMath::Clamp(ExternalControl.Brake, 0.0f, 1.0f));
	Movement->SetSteeringInput(FMath::Clamp(ExternalControl.Steering, -1.0f, 1.0f));
	Movement->SetHandbrakeInput(ExternalControl.bHandbrake);

	// Rueckwaerts: expliziter Rueckwaertsgang; sonst regelt die Automatik
	// (bUseAutomaticGears/bUseAutoReverse) den Vorwaertsgang selbst.
	// Hinweis: erst nach dem PhysicsAsset-Bauch-Fix end-to-end verifizierbar
	// (siehe Spec) - der ChaosCar steht bis dahin auf dem Bauch.
	if (ExternalControl.bReverse)
	{
		Movement->SetTargetGear(-1, /*bImmediate=*/true);
	}
}

void AWiesbadenChaosCar::TickSelfTest(float DeltaSeconds)
{
	float StartAfter = 0.0f;
	if (!FParse::Value(FCommandLine::Get(), TEXT("WbCarTest="), StartAfter)
		|| StartAfter <= 0.0f)
	{
		return;
	}

	SelfTestElapsed += DeltaSeconds;
	if (SelfTestElapsed < StartAfter)
	{
		return;
	}

	UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement();
	if (!Movement)
	{
		return;
	}

	// Die Fallprobe ist ausgebaut. Ihr Ergebnis steht in AGENTS.md: Der Wagen
	// fiel 200,1 cm und kam auf derselben Hoehe zur Ruhe - die Physik ist
	// dynamisch. Damit war die Ursache nicht der Koerper, sondern der
	// Startplatz (siehe WiesbadenGameMode::EnsurePlayerCar).

	// Vollgas, geradeaus, keine Bremse.
	bSelfTestActive = true;
	Movement->SetThrottleInput(1.0f);
	Movement->SetBrakeInput(0.0f);
	Movement->SetSteeringInput(0.0f);
	Movement->SetHandbrakeInput(false);

	const float Kmh = GetSpeedKmh();
	SelfTestTopKmh = FMath::Max(SelfTestTopKmh, Kmh);

	const float SinceStart = SelfTestElapsed - StartAfter;
	if (SelfTestTo50 < 0.0f && Kmh >= 50.0f) { SelfTestTo50 = SinceStart; }
	if (SelfTestTo100 < 0.0f && Kmh >= 100.0f) { SelfTestTo100 = SinceStart; }

	const int32 Second = FMath::FloorToInt(SinceStart);
	if (Second >= SelfTestNextReport)
	{
		SelfTestNextReport = Second + 1;

		// Beim ersten Bericht den Zustand der Physik mitschreiben.
		//
		// Der erste Lauf ergab 0 km/h bei 0 Umdrehungen und Gang 0 - der Motor
		// lief also nicht einmal. Ob das an der Physiksimulation, an den
		// Raedern oder am Antriebsstrang liegt, sagt keine dieser drei Zahlen.
		if (Second == 0)
		{
			const USkeletalMeshComponent* Body = GetMesh();
			const UPhysicsAsset* Physics = Body ? Body->GetPhysicsAsset() : nullptr;

			UE_LOG(LogWbCore, Log,
				TEXT("Fahrprobe Zustand: Physik simuliert %s, Physik-Asset %s mit %d Koerpern, ")
				TEXT("Raeder erzeugt %d, Komponente aktiv %s, Bewegungsziel %s."),
				(Body && Body->IsSimulatingPhysics()) ? TEXT("JA") : TEXT("NEIN"),
				Physics ? TEXT("vorhanden") : TEXT("FEHLT"),
				Physics ? Physics->SkeletalBodySetups.Num() : 0,
				Movement->Wheels.Num(),
				Movement->IsActive() ? TEXT("ja") : TEXT("nein"),
				Movement->UpdatedComponent ? *Movement->UpdatedComponent->GetName() : TEXT("KEINES"));
		}
		UE_LOG(LogWbCore, Log,
			TEXT("Fahrprobe %3d s: %6.1f km/h, %5.0f 1/min, Gang %d, hoechste %.1f km/h."),
			Second, Kmh, GetEngineRpm(), GetCurrentGear(), SelfTestTopKmh);

		// Radkontakt und Lage des Fahrgestell-Koerpers.
		//
		// Der Motor dreht binnen einer Sekunde auf den Begrenzer, ohne dass
		// sich der Wagen bewegt - das ist das Bild eines Antriebs OHNE LAST.
		// Entweder haben die Raeder keinen Bodenkontakt, oder das Fahrgestell
		// liegt auf und traegt den Wagen.
		//
		// Der automatisch erzeugte Koerper umschliesst das GANZE Mesh, also
		// auch die Raeder - er reicht damit bis auf die Fahrbahn. Ein
		// Fahrzeug-Physikkoerper muss ueber den Raedern enden.
		if (Second <= 3 || Second == 10)
		{
			FString WheelState;
			for (int32 Index = 0; Index < Movement->Wheels.Num(); ++Index)
			{
				const UChaosVehicleWheel* Wheel = Movement->Wheels[Index];
				if (!Wheel)
				{
					continue;
				}
				WheelState += FString::Printf(TEXT("%d:%s(%.1f) "),
					Index,
					Wheel->IsInAir() ? TEXT("Luft") : TEXT("Boden"),
					Wheel->GetSuspensionOffset());
			}

			// Wo ist der Boden - JE RAD, und ohne das eigene Fahrzeug.
			//
			// Der erste Versuch startete den Strahl 100 cm ueber dem Ursprung
			// und meldete den Boden exakt 100 cm darueber: Er hat das eigene
			// Dach getroffen. Ein Strahl, der bei seinem Startpunkt auf etwas
			// trifft, misst gar nichts - und die Zahl sah trotzdem wie eine
			// Messung aus.
			const USkeletalMeshComponent* Body = GetMesh();

			FCollisionQueryParams Params(SCENE_QUERY_STAT(WbChaosCarGround), false, this);
			Params.AddIgnoredActor(this);

			FString GroundState;
			for (int32 Index = 0; Index < Movement->Wheels.Num(); ++Index)
			{
				const UChaosVehicleWheel* Wheel = Movement->Wheels[Index];
				if (!Wheel || !Body)
				{
					continue;
				}

				const FVector WheelWorld =
					Body->GetBoneLocation(Movement->WheelSetups[Index].BoneName);

				// ZWEI Strahlen, zwei Kanaele.
				//
				// Chaos tastet die Fahrbahn mit ECC_WorldDynamic ab (nachgelesen
				// in ChaosWheeledVehicleMovementComponent.cpp: "ECollisionChannel
				// SpringCollisionChannel = ECC_WorldDynamic"). Mein bisheriger
				// Strahl fragte ueber ECC_Visibility und fand die Strasse - die
				// Raeder melden trotzdem "Luft". Trifft der eine Kanal und der
				// andere nicht, ist die Antwort der Fahrbahn auf den
				// Fahrzeugkanal die Ursache, und nicht die Reichweite.
				FHitResult HitVis;
				const bool bHitVis = GetWorld()->LineTraceSingleByChannel(
					HitVis, WheelWorld, WheelWorld - FVector(0, 0, 500.0),
					ECC_Visibility, Params);

				FHitResult HitDyn;
				const bool bHitDyn = GetWorld()->LineTraceSingleByChannel(
					HitDyn, WheelWorld, WheelWorld - FVector(0, 0, 500.0),
					ECC_WorldDynamic, Params);

				GroundState += FString::Printf(TEXT("%d:Sicht%.0f/Dyn%s "),
					Index,
					bHitVis ? WheelWorld.Z - HitVis.Location.Z : -1.0f,
					bHitDyn ? *FString::Printf(TEXT("%.0f"), WheelWorld.Z - HitDyn.Location.Z)
							: TEXT("NICHTS"));
			}

			// Der KOLLISIONSKOERPER, nicht das Mesh.
			//
			// Bleibt eine Erklaerung, die zu allem passt: Der Wagen ruht auf
			// seinem Fahrgestell-Koerper, und der reicht tiefer als die Raeder.
			// Dann haengen die Raeder in der Luft, der Wagen faellt nicht, und
			// nichts bewegt sich. Der Zaehler "1 Koerper" hat mich das
			// verwerfen lassen - ein Kasten um das GANZE Mesh ist aber genau
			// ein Koerper.
			//
			// Ausserdem: GetComponentVelocity liefert bei einem Physikkoerper
			// nicht dessen Geschwindigkeit. Die 0,00 cm/s waren womoeglich ein
			// totes Instrument - hier steht jetzt der Wert aus der Physik.
			const FVector PhysVel = Body ? Body->GetPhysicsLinearVelocity() : FVector::ZeroVector;
			const FBox BodyBounds = Body ? Body->GetBodyInstance()->GetBodyBounds() : FBox(ForceInit);
			const double OriginZ = GetActorLocation().Z;

			UE_LOG(LogWbCore, Log,
				TEXT("Fahrprobe Raeder %s | Z %.1f, Physik-Geschwindigkeit %.2f cm/s, ")
				TEXT("Schwerkraft %s, Koerper %s, Physik-Asset %s, Kollisionskoerper %.1f bis %.1f ")
				TEXT("(relativ zum Ursprung), Radmitte ueber Boden: %s")
				TEXT("(Radius 34,3, Reichweite 74,3)"),
				*WheelState,
				OriginZ,
				PhysVel.Size(),
				(Body && Body->IsGravityEnabled()) ? TEXT("an") : TEXT("AUS"),
				(Body && Body->RigidBodyIsAwake()) ? TEXT("wach") : TEXT("SCHLAEFT"),
				(Body && Body->GetPhysicsAsset()) ? *Body->GetPhysicsAsset()->GetName() : TEXT("KEINES"),
				BodyBounds.Min.Z - OriginZ,
				BodyBounds.Max.Z - OriginZ,
				*GroundState);

			UE_LOG(LogWbCore, Log,
				TEXT("Fallprobe: %.1f cm gefallen seit dem Anheben (%.1f -> %.1f)."),
				DropTestStartZ - OriginZ, DropTestStartZ, OriginZ);
		}

		if (Second == 30 || Second == 60)
		{
			UE_LOG(LogWbCore, Log,
				TEXT("Fahrprobe Zwischenstand: 0-50 km/h %s, 0-100 km/h %s ")
				TEXT("(Kaefer 1302: rund 23 s auf 100, Hoechstgeschwindigkeit 130)."),
				SelfTestTo50 < 0.0f ? TEXT("nicht erreicht")
					: *FString::Printf(TEXT("%.1f s"), SelfTestTo50),
				SelfTestTo100 < 0.0f ? TEXT("nicht erreicht")
					: *FString::Printf(TEXT("%.1f s"), SelfTestTo100));
		}
	}
}

void AWiesbadenChaosCar::CycleCameraMode()
{
	if (VehicleCamera)
	{
		VehicleCamera->CycleCameraMode();
	}
}

bool AWiesbadenChaosCar::RestsOnWheels(double ChassisBottomZcm, double WheelContactZcm, double MarginCm)
{
	// Auf den Raedern, wenn die Chassis-Unterkante NICHT tiefer sitzt als der
	// Radaufstandspunkt (abzueglich Toleranz). Sitzt sie tiefer, kommt das
	// Chassis zuerst auf und der Wagen haengt auf dem Bauch.
	return ChassisBottomZcm >= WheelContactZcm - MarginCm;
}

bool AWiesbadenChaosCar::IsKeyDown(const FKey& Key) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	return PC && PC->IsInputKeyDown(Key);
}

float AWiesbadenChaosCar::GetAnalogAxis(const FKey& Key) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	return PC ? PC->GetInputAnalogKeyState(Key) : 0.0f;
}

void AWiesbadenChaosCar::ReadInput(float DeltaSeconds)
{
	UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement();
	if (!Movement)
	{
		return;
	}

	// Belegung wie beim bisherigen Fahrzeug:
	//
	//   W / Pfeil hoch      Gas
	//   S / Pfeil runter    Bremse, im Stillstand Rueckwaerts
	//   A D / Pfeile        Lenken
	//   Leertaste           Handbremse
	//   Rechter Trigger     Gas (analog)
	//   Linker Trigger      Bremse (analog)
	//   Linker Stick X      Lenken (analog)
	//
	// Die Trigger sind echte Analogachsen und erlauben Teilgas - genau das,
	// was ein Reifenmodell braucht, um Schlupf ueberhaupt zeigen zu koennen.
	const float TriggerThrottle = GetAnalogAxis(EKeys::Gamepad_RightTriggerAxis);
	const float TriggerBrake = GetAnalogAxis(EKeys::Gamepad_LeftTriggerAxis);
	const float StickSteer = GetAnalogAxis(EKeys::Gamepad_LeftX);

	const bool bForwardHeld = IsKeyDown(EKeys::W) || IsKeyDown(EKeys::Up);
	const bool bBackwardHeld = IsKeyDown(EKeys::S) || IsKeyDown(EKeys::Down);
	const bool bRightHeld = IsKeyDown(EKeys::D) || IsKeyDown(EKeys::Right);
	const bool bLeftHeld = IsKeyDown(EKeys::A) || IsKeyDown(EKeys::Left);

	const float Throttle = FMath::Max(bForwardHeld ? 1.0f : 0.0f, TriggerThrottle);
	const float Brake = FMath::Max(bBackwardHeld ? 1.0f : 0.0f, TriggerBrake);

	float Steer = 0.0f;
	if (bRightHeld) { Steer += 1.0f; }
	if (bLeftHeld) { Steer -= 1.0f; }
	if (FMath::Abs(StickSteer) > FMath::Abs(Steer))
	{
		Steer = StickSteer;
	}

	Movement->SetThrottleInput(FMath::Clamp(Throttle, 0.0f, 1.0f));
	Movement->SetBrakeInput(FMath::Clamp(Brake, 0.0f, 1.0f));
	Movement->SetSteeringInput(FMath::Clamp(Steer, -1.0f, 1.0f));
	Movement->SetHandbrakeInput(IsKeyDown(EKeys::SpaceBar) || IsKeyDown(EKeys::Gamepad_FaceButton_Bottom));
}

float AWiesbadenChaosCar::GetSpeedKmh() const
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement();
	// GetForwardSpeed liefert Zentimeter je Sekunde.
	return Movement ? Movement->GetForwardSpeed() * 0.036f : 0.0f;
}

float AWiesbadenChaosCar::GetEngineRpm() const
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement();
	return Movement ? Movement->GetEngineRotationSpeed() : 0.0f;
}

float AWiesbadenChaosCar::GetEngineIdleRpm() const
{
	// Skala des HUD-Drehzahlbands aus dem Chaos-Antrieb (EngineSetup).
	const UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement();
	return Movement ? Movement->EngineSetup.EngineIdleRPM : 0.0f;
}

float AWiesbadenChaosCar::GetEngineMaxRpm() const
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement();
	return Movement ? Movement->EngineSetup.MaxRPM : 1.0f;
}

EWiesbadenVehicleCameraMode AWiesbadenChaosCar::GetCameraMode() const
{
	return VehicleCamera ? VehicleCamera->GetCameraMode() : EWiesbadenVehicleCameraMode::Follow;
}

int32 AWiesbadenChaosCar::GetCurrentGear() const
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetChaosMovement();
	return Movement ? Movement->GetCurrentGear() : 0;
}
