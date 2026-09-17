// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenHelicopter.h"

#include "WiesbadenReal.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "InputCoreTypes.h"
#include "UObject/ConstructorHelpers.h"

AWiesbadenHelicopter::AWiesbadenHelicopter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// Beim Platzieren im Level sofort vom lokalen Spieler uebernehmen.
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	AutoPossessAI = EAutoPossessAI::Disabled;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->SetupAttachment(SceneRoot);
	CollisionSphere->InitSphereRadius(160.0f);
	CollisionSphere->SetRelativeLocation(FVector(0.0f, 0.0f, 130.0f));
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);

	// Platzhalter-Mesh: der Engine-Basis-Cube (100x100x100 cm), je Bauteil skaliert.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));
	UStaticMesh* Cube = CubeMesh.Object;

	// Echte Modelle. Vorher bestand der Helikopter aus Engine-Wuerfeln - ein
	// schwarzer Kasten mit Brettern als Rotor.
	//
	// Die Zahlen unten sind an den Assets GEMESSEN (Bounds im Editor), nicht
	// geschaetzt:
	//   Rumpf        48,9 x 100,7 x 17,1 cm, Mittelpunkt (0,7 | -9,9 | 8,6)
	//   Rotor oben   77,8 x  87,0 x  7,0 cm, Mittelpunkt (-6,7 | 26,6 | 0)
	//   Rotor unten  89,0 x  88,0 x 10,0 cm, Mittelpunkt (0,4 | 11,8 | 0)
	//
	// Daraus folgt die Ausrichtung:
	//
	// Die Laengsachse des Rumpfes liegt auf Y, Unreals Vorwaertsachse ist X -
	// daher die Gierdrehung um -90 Grad, die Modell-+Y auf Welt-+X abbildet.
	// Welches Y-Ende die Nase ist, wurde an den Vertices ausgezaehlt: Die
	// Y-Spanne laeuft von -60,0 bis +40,6 cm, der Pivot sitzt also nicht mittig,
	// sondern dort, wo beim Ka-52 der Rotormast steht. Im aeusseren Fuenftel
	// des langen Endes (-Y) misst der Querschnitt 21,4 x 32,9 cm, am kurzen
	// Ende (+Y) nur 13,1 x 28,5 cm - hinten sitzen also Leitwerk und
	// Hoehenflosse, vorn die schmale Kanzel. Nase = +Y.
	//
	// Lage der Rotor-Drehachse im Modell.
	//
	// Weder der Ursprung noch der Bounding-Box-Mittelpunkt taugen dafuer:
	//  - Der Ursprung ist der gemeinsame Szenen-Nullpunkt des Exports, nicht
	//    die Nabe. Rotoren, die um ihn kreisen, wandern sichtbar aus.
	//  - Die Bounding Box ist bei einem GEPARKTEN Rotor irrefuehrend, weil die
	//    Blaetter ungleich stehen; ihr Mittelpunkt liegt irgendwo dazwischen.
	//
	// Neubau 2026-09-17 (Import /Game/Vehicles/Ka52, Blender-Pipeline 23-25):
	// Das Modell liegt bereits im WISSENDEN Massstab (x15) und mit EINGEBAKENER
	// Weltlage: Die Rotor-Achse liegt in Mesh-XY exakt bei (0, 0), die Geometrie
	// in Gebaeude-z (Boden 0, oberer Hub 330..501, unterer 222..376, Rumpf
	// 0..295). Pivots und Median-Offsets des alten Assets entfallen - die Nabe
	// sitzt per Definition auf der Achse (gemessen: Bohrung 2 cm off-axis,
	// Blatt-Gaps exakt 120 Grad, Round-Trip ueber GLB UND FBX verifiziert).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeliBodyMesh(
		TEXT("/Game/Vehicles/Ka52/Fuselage.Fuselage"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeliRotorUpperMesh(
		TEXT("/Game/Vehicles/Ka52/Rotor_Upper.Rotor_Upper"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeliRotorLowerMesh(
		TEXT("/Game/Vehicles/Ka52/Rotor_Lower.Rotor_Lower"));

	UStaticMesh* HeliBody = HeliBodyMesh.Succeeded() ? HeliBodyMesh.Object : nullptr;
	UStaticMesh* HeliRotorUpper = HeliRotorUpperMesh.Succeeded() ? HeliRotorUpperMesh.Object : nullptr;
	UStaticMesh* HeliRotorLower = HeliRotorLowerMesh.Succeeded() ? HeliRotorLowerMesh.Object : nullptr;

	// Importiertes Modell gebunden? Dann bleiben seine eigenen PBR-Materialien
	// stehen (siehe HasImportedModel) - nur der Wuerfel-Rueckfall wird lackiert.
	bImportedModel = (HeliBody != nullptr);

	// Massstab: Das Modell ist ein Kamov Ka-52 - Koaxialrotor, keine
	// Heckrotor. Genau die Bauart, die RotorPhysics bereits abbildet
	// (bCoaxialRotors, siehe BeginPlay).
	//
	// Der Massstab folgt aus drei UNABHAENGIGEN Massen des echten Ka-52, die
	// gegen die gemessenen Bounds gerechnet gut zusammenpassen:
	//   Rumpflaenge  14,2 m / 1,007 m = 14,1
	//   Stummelfluegel-Spannweite 7,3 m / 0,489 m = 14,9
	//   Rotordurchmesser 14,5 m / 0,88 m = 16,5
	// Die ersten beiden stuetzen sich gegenseitig; der dritte faellt hoeher
	// aus, weil die Blaetter im Modell etwas kuerzer geraten sind. Gewaehlt
	// Der Neubau ist already-scaled: Rumpflaenge 14,06 m, Rotorscheiben
	// 15,6 m (oben) / 16,0 m (unten) - direkt die echten Ka-52-Masse.
	constexpr float ModelScale = 1.0f;

	// Hoehenlage: Der Modell-Ursprung liegt an der Kufenebene (Bounds z 0..295).
	// Rotor-Naben auf den gemessenen Hub-Positionen des Exports (Pivot z 495 /
	// 376,5 cm) - die Blattspitzen erreichen damit die Bauhoehe von 5,0 m.
	constexpr float FuselageHeightCm = 0.0f;
	constexpr float UpperRotorHeightCm = 495.0f;
	constexpr float LowerRotorHeightCm = 376.5f;

	// Gierdrehung, die Modell-+Y auf Welt-+X legt.
	const FRotator ModelYaw(0.0f, -90.0f, 0.0f);


	// Rumpf.
	FuselageMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FuselageMesh"));
	FuselageMesh->SetupAttachment(SceneRoot);

	if (HeliBody)
	{
		FuselageMesh->SetStaticMesh(HeliBody);
		FuselageMesh->SetRelativeRotation(ModelYaw);
		FuselageMesh->SetRelativeScale3D(FVector(ModelScale));
		FuselageMesh->SetRelativeLocation(FVector(0.0f, 0.0f, FuselageHeightCm));
	}
	else if (Cube)
	{
		// Rueckfall: Wuerfel 320 x 120 x 70 cm.
		FuselageMesh->SetStaticMesh(Cube);
		FuselageMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 130.0f));
		FuselageMesh->SetRelativeScale3D(FVector(3.2f, 1.2f, 0.7f));
	}

	// Heckausleger und Flosse gehoeren beim echten Modell zum Rumpf und werden
	// nur im Wuerfel-Rueckfall als eigene Bauteile gebraucht.
	TailBoomMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TailBoomMesh"));
	TailBoomMesh->SetupAttachment(SceneRoot);
	TailBoomMesh->SetRelativeLocation(FVector(-240.0f, 0.0f, 150.0f));
	TailBoomMesh->SetRelativeScale3D(FVector(1.6f, 0.16f, 0.16f));

	TailFinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TailFinMesh"));
	TailFinMesh->SetupAttachment(SceneRoot);
	TailFinMesh->SetRelativeLocation(FVector(-300.0f, 0.0f, 180.0f));
	TailFinMesh->SetRelativeScale3D(FVector(0.2f, 0.04f, 0.5f));

	if (HeliBody)
	{
		TailBoomMesh->SetVisibility(false);
		TailFinMesh->SetVisibility(false);
	}
	else if (Cube)
	{
		TailBoomMesh->SetStaticMesh(Cube);
		TailFinMesh->SetStaticMesh(Cube);
	}

	// Hauptrotor: Nabe auf dem Mast (leicht vor dem Schwerpunkt, ueber dem Rumpf).
	MainRotorHub = CreateDefaultSubobject<USceneComponent>(TEXT("MainRotorHub"));
	MainRotorHub->SetupAttachment(SceneRoot);
	// Die Nabe sitzt beim Neubau in Mesh-XY auf (0, 0); der Hub-Node traegt nur
	// noch die Hub-Hoehe, das Mesh wird um sie nach unten versetzt, damit die
	// Geometrie an ihrem gebakten Platz bleibt und trotzdem um den Hub kreist.
	const FVector MastOffset = FVector::ZeroVector;

	MainRotorHub->SetRelativeLocation(
		FVector(MastOffset.X, MastOffset.Y, UpperRotorHeightCm));

	MainRotorBlade = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MainRotorBlade"));
	MainRotorBlade->SetupAttachment(MainRotorHub);
	if (HeliRotorUpper)
	{
		MainRotorBlade->SetStaticMesh(HeliRotorUpper);
		MainRotorBlade->SetRelativeScale3D(FVector(ModelScale));
		MainRotorBlade->SetRelativeRotation(ModelYaw);
		// Hub-Hoehe aus der Geometrie heraus: der Mesh-Ursprung liegt am Boden
		// auf der Achse, also hebt der reine z-Versatz die Geometrie nicht -
		// sie bleibt am gebakten Ort und dreht sich um die Hub-Achse.
		MainRotorBlade->SetRelativeLocation(FVector(0.0f, 0.0f, -UpperRotorHeightCm));
	}
	else if (Cube)
	{
		MainRotorBlade->SetStaticMesh(Cube);
		MainRotorBlade->SetRelativeScale3D(FVector(7.0f, 0.2f, 0.05f));
	}

	// Unterer Hauptrotor des Koaxial-Paars: knapp unter dem oberen, dreht
	// gegenlaeufig (Ka-52-Stil). Kaempferisch kompakt, kein sichtbarer Mast.
	LowerRotorHub = CreateDefaultSubobject<USceneComponent>(TEXT("LowerRotorHub"));
	LowerRotorHub->SetupAttachment(SceneRoot);
	LowerRotorHub->SetRelativeLocation(
		FVector(MastOffset.X, MastOffset.Y, LowerRotorHeightCm));
	// (siehe oben: MastOffset ist beim Neubau (0,0,0) - die Achse sitzt zentriert)

	LowerRotorBlade = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LowerRotorBlade"));
	LowerRotorBlade->SetupAttachment(LowerRotorHub);
	if (HeliRotorLower)
	{
		LowerRotorBlade->SetStaticMesh(HeliRotorLower);
		LowerRotorBlade->SetRelativeScale3D(FVector(ModelScale));
		LowerRotorBlade->SetRelativeRotation(ModelYaw);
		LowerRotorBlade->SetRelativeLocation(FVector(0.0f, 0.0f, -LowerRotorHeightCm));
	}
	else if (Cube)
	{
		LowerRotorBlade->SetStaticMesh(Cube);
		LowerRotorBlade->SetRelativeScale3D(FVector(7.0f, 0.2f, 0.05f));
	}

	// Heckrotor: Nabe am Ende des Heckauslegers, dreht um die Y-Achse.
	TailRotorHub = CreateDefaultSubobject<USceneComponent>(TEXT("TailRotorHub"));
	TailRotorHub->SetupAttachment(SceneRoot);
	TailRotorHub->SetRelativeLocation(FVector(-320.0f, 0.0f, 150.0f));

	TailRotorBlade = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TailRotorBlade"));
	TailRotorBlade->SetupAttachment(TailRotorHub);
	TailRotorBlade->SetRelativeScale3D(FVector(0.7f, 0.05f, 0.15f));

	if (HeliBody)
	{
		// Ein Ka-52 HAT keinen Heckrotor - das gegenlaeufige Rotorpaar hebt
		// das Reaktionsmoment auf, gesteuert wird die Gierachse ueber
		// differentielle Blattverstellung. Die Physik bildet das bereits so ab
		// (bCoaxialRotors: Force.Y = 0, kein Heckrotorschub); sichtbar war der
		// Heckrotor trotzdem, weil er aus der Wuerfel-Notloesung stammte.
		TailRotorBlade->SetVisibility(false);
	}
	else if (Cube)
	{
		TailRotorBlade->SetStaticMesh(Cube);
	}

	// --- Sicht-FX: Rotor-Blur-Scheiben + Downwash-Staub ----------------------
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlurMat(
		TEXT("/Game/Materials/City/M_WbRotorBlur.M_WbRotorBlur"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DustMat(
		TEXT("/Game/Materials/City/M_WbDownwash.M_WbDownwash"));
	UStaticMesh* Disc = CylMesh.Succeeded() ? CylMesh.Object : nullptr;

	// Rotorscheiben des Neubaus: 15,6 m (oben) / 16,0 m (unten) Blattkreis ->
	// Zylinder (Durchmesser 100 cm) entsprechend skalieren, flach (2 cm). Sitzt
	// an der jeweiligen Nabe und blendet mit der Drehzahl ein (Opacity per MID),
	// waehrend die soliden Blaetter ausblenden.
	const FVector BlurScaleUpper(15.6f, 15.6f, 0.02f);
	const FVector BlurScaleLower(16.0f, 16.0f, 0.02f);
	UpperRotorBlur = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("UpperRotorBlur"));
	UpperRotorBlur->SetupAttachment(MainRotorHub);
	UpperRotorBlur->SetRelativeScale3D(BlurScaleUpper);
	UpperRotorBlur->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UpperRotorBlur->SetCastShadow(false);
	if (Disc) { UpperRotorBlur->SetStaticMesh(Disc); }
	if (Disc && BlurMat.Succeeded()) { UpperRotorBlur->SetMaterial(0, BlurMat.Object); }

	LowerRotorBlur = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LowerRotorBlur"));
	LowerRotorBlur->SetupAttachment(LowerRotorHub);
	LowerRotorBlur->SetRelativeScale3D(BlurScaleLower);
	LowerRotorBlur->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LowerRotorBlur->SetCastShadow(false);
	if (Disc) { LowerRotorBlur->SetStaticMesh(Disc); }
	if (Disc && BlurMat.Succeeded()) { LowerRotorBlur->SetMaterial(0, BlurMat.Object); }

	// Downwash-Staub: flache Scheibe, zur Laufzeit per Trace auf den Boden
	// gesetzt (Weltkoordinaten, damit sie beim Neigen des Rumpfs flach bleibt).
	GroundDust = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundDust"));
	GroundDust->SetupAttachment(SceneRoot);
	GroundDust->SetRelativeScale3D(FVector(15.0f, 15.0f, 0.02f));
	GroundDust->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GroundDust->SetCastShadow(false);
	GroundDust->SetAbsolute(true, true, false);
	if (Disc) { GroundDust->SetStaticMesh(Disc); }
	if (Disc && DustMat.Succeeded()) { GroundDust->SetMaterial(0, DustMat.Object); }

	// Kamera: generische Fahrzeug-Kamera-Komponente (erzeugt ihr Rig in BeginPlay).
	// Umsehen ist jetzt auch im Heli erlaubt (Freilook im Follow-Modus per
	// Maus/Rechtsstick, Umschalten auf Orbit/Cockpit per C). Frueher war die
	// Kamera hier fest verriegelt (bLockFollowMode) - man konnte sich im Flug
	// nicht umsehen; der ruhige Horizont (unten) bleibt davon unberuehrt.
	VehicleCamera = CreateDefaultSubobject<UWiesbadenVehicleCameraComponent>(TEXT("VehicleCamera"));
	VehicleCamera->SetupAttachment(SceneRoot);
	VehicleCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 130.0f));
	VehicleCamera->bLockFollowMode = false;

	// Ruhiger Horizont und Positions-Nachlauf: der Rumpf neigt sich IM Bild,
	// nicht das Bild mit ihm, und die Kamera federt Beschleunigungen weich
	// ab. Vorher uebertrug sich jeder zyklische Ausschlag ungefiltert auf
	// die Kamera - das wirkte hektisch und machte das Zielen von Blicken
	// unnoetig schwer.
	VehicleCamera->bLevelHorizon = true;
	VehicleCamera->PositionLagSpeed = 9.0f;
	VehicleCamera->FollowArmLength = 1500.0f;
	VehicleCamera->FollowPitchOffset = -10.0f;

	// Pilotensitz vorn im Rumpf, Blick nach vorn. Versatz relativ zur
	// Kamera-Komponente (0,0,130) -> Augpunkt ~ (120, 0, 165). Rumpf und
	// Heck werden in der Cockpit-Ansicht ausgeblendet (kein Innenraum
	// modelliert); die Rotoren ueber dem Kopf bleiben sichtbar.
	VehicleCamera->CockpitOffset = FVector(120.0f, 0.0f, 35.0f);
	VehicleCamera->AddCockpitHiddenMesh(FuselageMesh);
	VehicleCamera->AddCockpitHiddenMesh(TailBoomMesh);
	VehicleCamera->AddCockpitHiddenMesh(TailFinMesh);

	// Flugsound: prozeduraler Rotor-/Motor-Klang (Asset-Slots liegen bereit).
	HelicopterAudio = CreateDefaultSubobject<UWiesbadenHelicopterAudioComponent>(TEXT("HelicopterAudio"));
	HelicopterAudio->SetupAttachment(SceneRoot);
	HelicopterAudio->SetRelativeLocation(FVector(0.0f, 0.0f, 130.0f));

	// Kampfhelikopter-Konfiguration (Ka-52-Stil, Referenz: Mi-35/28/Ka-52/Mi-8):
	// koaxiale, gegenlaeufige Rotoren (flink, kein Heckrotor-Moment) und ein
	// schwerer, tiefer Rotorschlag (Blade Slap) statt zivilem Surren.
	RotorPhysics.bCoaxialRotors = true;
	// Gier-Autoritaet: der Heli drehte sich praktisch NICHT (Beschwerde "Heli
	// "Heli dreht nicht": Die eigentliche Ursache lag NICHT bei dieser
	// Autoritaet, sondern in ApplyFlightPhysics - die Ratendaempfung setzte die
	// Gierrate per FInterpTo mit InterpSpeed 0 JEDES Bild auf null, sobald Gieren
	// kommandiert war (FInterpTo gibt bei Speed<=0 sofort das Ziel 0 zurueck).
	// Das Giermoment war also immer da (gemessen 360.000 N*m, 9 rad/s^2), die
	// Drehrate wurde nur sofort wieder genullt. Nach dem Fix genuegt eine
	// moderate Autoritaet: 16000 gibt am Boden ~30 Grad/s, im Steigflug bis
	// ~80 Grad/s - flink, aber steuerbar. 60000 liess den Rumpf mit ueber
	// 400 Grad/s durchdrehen.
	RotorPhysics.CoaxialYawAuthority = 16000.0f;
	RotorPhysics.MaxForwardSpeedMetersPerS = 85.0f;
	// Weniger kippelig/ueberschiessend: geringere Zyklik-Autoritaet + mehr
	// Drehdaempfung -> die Lage baut sich ruhiger auf und schwingt nicht ueber.
	RotorPhysics.CyclicPitchMomentAuthority = 1150.0f;
	RotorPhysics.CyclicRollMomentAuthority = 1150.0f;
	RotorPhysics.RotorAngularDamping = 2200.0f;

	// Flachere Kollektiv-Kennlinie fuer moderate Steig-/Sinkraten.
	//
	// Der Schwebepitch stellt sich zur Laufzeit selbst auf ~4 Grad ein
	// (ComputeHoverPitchDeg, lift = weight). Die Modul-Defaults 2..14 Grad
	// liessen den Rotor bei vollem Hebel das ~4-fache Gewicht erzeugen (~3 g,
	// >20 m/s Steigen). Mit 3..6 Grad liegt der volle Ausschlag nur noch knapp
	// ueber/unter dem Schwebepunkt: rund +10 / -8 m/s - kontrollierbar, und der
	// Hebel wirkt wie ein getrimmter Kollektiv (Mitte haelt die Hoehe).
	RotorPhysics.MaxCollectivePitchDeg = 6.0f;
	RotorPhysics.MinCollectivePitchDeg = 3.0f;

	// Weniger schrill/nervig: flacherer Blattschlag + tiefere Filter-Grundfrequenz
	// (dumpfer, weniger Hoehen). Master-Lautstaerke ist im Audio-Component gesenkt.
	HelicopterAudio->BladeCount = 3;
	HelicopterAudio->BladeSlapDepth = 0.35f;
	HelicopterAudio->RotorCutoffBaseHz = 85.0f;

	if (!Cube)
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Basis-Cube (/Engine/BasicShapes/Cube) nicht gefunden - Platzhalter-Meshes bleiben unsichtbar."));
	}
}

void AWiesbadenHelicopter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Boden-Cache pro Frame invalidieren; ApplyGroundConstraint fuellt ihn im
	// Flugpfad neu. Bleibt er ungueltig (geparkt), tracen die Leser selbst.
	bGroundCacheValid = false;

	// Ohne Pilot wird nicht geflogen.
	//
	// Der abgestellte Helikopter durchlief bisher dieselbe Flugsimulation wie
	// der geflogene. Mit laufendem Triebwerk und dem Ruhewert von 50 %
	// Kollektiv erzeugte der Rotor Auftrieb, und er stieg unbemannt davon -
	// gemessen 126 m ueber Grund. Die Spawn-Meldung stimmte jedes Mal, zu
	// finden war er trotzdem nie.
	if (!Controller)
	{
		ParkOnGround();
		UpdateRotors(DeltaSeconds);
		UpdateVisualEffects(DeltaSeconds);
		UpdateAudio(DeltaSeconds);
		return;
	}

	ReadInput(DeltaSeconds);
	ApplyFlightPhysics(DeltaSeconds);
	UpdateRotors(DeltaSeconds);
	UpdateVisualEffects(DeltaSeconds);
	UpdateAudio(DeltaSeconds);
}

void AWiesbadenHelicopter::ParkOnGround()
{
	bEngineRunning = false;
	Velocity = FVector::ZeroVector;
	AngularVelocity = FVector::ZeroVector;

	CollectiveInput = 0.0f;
	CyclicPitchInput = 0.0f;
	CyclicRollInput = 0.0f;
	YawInput = 0.0f;

	// Waagerecht ausrichten - ein geparkter Helikopter steht nicht schraeg.
	const FRotator Current = GetActorRotation();
	SetActorRotation(FRotator(0.0f, Current.Yaw, 0.0f));

	UWorld* HeliWorld = GetWorld();
	if (!HeliWorld)
	{
		return;
	}

	// Auf den Boden setzen. Der Trace startet ueber dem Rumpf, damit er nicht
	// innerhalb eines Kollisionskoerpers beginnt.
	FHitResult Hit;
	const FVector Start = GetActorLocation() + FVector(0.0f, 0.0f, 500.0f);
	const FVector End = Start - FVector(0.0f, 0.0f, 100000.0f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbHeliPark), true);
	Params.AddIgnoredActor(this);

	if (!HeliWorld->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		// Boden noch nicht gestreamt: stehen bleiben, nicht fallen.
		return;
	}

	const FVector Location = GetActorLocation();
	SetActorLocation(
		FVector(Location.X, Location.Y, Hit.Location.Z + MinGroundClearanceCm),
		/*bSweep=*/false);
}

void AWiesbadenHelicopter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Pilot-Controller einmalig cachen (der Typ ist hier bereits geprueft) -
	// ReadInput fragt ihn pro Frame ~17x ab, sonst je ein Cast.
	CachedPlayerController = Cast<APlayerController>(NewController);
	if (CachedPlayerController)
	{
		CachedPlayerController->SetViewTarget(this);
	}

	// Triebwerk laeuft nur mit Pilot an Bord.
	bEngineRunning = true;

	UE_LOG(LogWbVehicles, Log, TEXT("Helikopter %s wurde vom Spieler uebernommen."), *GetName());
}

void AWiesbadenHelicopter::UnPossessed()
{
	Super::UnPossessed();

	bEngineRunning = false;
	CachedPlayerController = nullptr;

	UE_LOG(LogWbVehicles, Log, TEXT("Helikopter %s wurde freigegeben."), *GetName());
}

void AWiesbadenHelicopter::CycleCameraMode()
{
	if (VehicleCamera)
	{
		VehicleCamera->CycleCameraMode();
	}
}

EWiesbadenVehicleCameraMode AWiesbadenHelicopter::GetCameraMode() const
{
	return VehicleCamera ? VehicleCamera->GetCameraMode() : EWiesbadenVehicleCameraMode::Follow;
}

double AWiesbadenHelicopter::GetUpperRotorDiameterCm() const
{
	if (!MainRotorBlade || !MainRotorBlade->GetStaticMesh())
	{
		return 0.0;
	}
	const FVector Size = MainRotorBlade->GetStaticMesh()->GetBoundingBox().GetSize();
	return FMath::Max(Size.X, Size.Y) * MainRotorBlade->GetRelativeScale3D().X;
}

double AWiesbadenHelicopter::GetNoseToTailCm() const
{
	if (!FuselageMesh || !FuselageMesh->GetStaticMesh())
	{
		return 0.0;
	}
	// Im Mesh liegt die Laengsachse auf Y (14,06 m), quer dazu die 8,7 m
	// Stummelfluegel - max(X, Y) trifft die Laengsachse und bleibt auch fuer
	// den Wuerfel-Rueckfall richtig.
	const FVector Size = FuselageMesh->GetStaticMesh()->GetBoundingBox().GetSize();
	return FMath::Max(Size.X, Size.Y) * FuselageMesh->GetRelativeScale3D().X;
}

float AWiesbadenHelicopter::GetMainRotorRpm() const
{
	return RotorPhysics.MainRotorRpm;
}

float AWiesbadenHelicopter::GetAirspeedKmh() const
{
	// Nur die waagerechte Komponente: das Steigen zaehlt der Variometer, nicht
	// der Fahrtmesser.
	return FVector(Velocity.X, Velocity.Y, 0.0f).Size() * 0.036f;
}

float AWiesbadenHelicopter::GetVerticalSpeedMs() const
{
	return Velocity.Z * 0.01f;
}

float AWiesbadenHelicopter::GetAltitudeMeters() const
{
	const FVector Location = GetActorLocation();
	// Gecachten Bodenwert dieses Frames nutzen (aus ApplyGroundConstraint),
	// sonst selbst tracen (geparkt / Aufruf ausserhalb des Flug-Ticks).
	if (bGroundCacheValid)
	{
		return (Location.Z - CachedGroundZ) * 0.01f;
	}
	if (const UWorld* HeliWorld = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WbHeliAltitude), false, this);
		Params.AddIgnoredActor(this);
		if (HeliWorld->LineTraceSingleByChannel(
				Hit, Location, Location - FVector(0.0f, 0.0f, 100000.0f),
				ECC_WorldStatic, Params))
		{
			return (Location.Z - Hit.Location.Z) * 0.01f;
		}
	}
	// Kein Bodentreffer (ueber Wasser, ausserhalb Kollision): Welthoehe.
	return Location.Z * 0.01f;
}

float AWiesbadenHelicopter::GetHeadingDegrees() const
{
	float Yaw = FMath::Fmod(GetActorRotation().Yaw, 360.0f);
	if (Yaw < 0.0f)
	{
		Yaw += 360.0f;
	}
	return Yaw;
}

float AWiesbadenHelicopter::GetCollective() const
{
	// Gleiche Abbildung wie in UpdateRotors: Hebel -1..1 -> Blattstellung 0..1.
	return FMath::Clamp(0.5f + 0.5f * CollectiveInput, 0.0f, 1.0f);
}

float AWiesbadenHelicopter::GetYawRateDegPerSec() const
{
	return FMath::RadiansToDegrees(AngularVelocity.Z);
}

float AWiesbadenHelicopter::ApplyStickShaping(float RawAxis, float Deadzone, float Expo)
{
	const float Clamped = FMath::Clamp(RawAxis, -1.0f, 1.0f);
	const float Magnitude = FMath::Abs(Clamped);
	const float Dead = FMath::Clamp(Deadzone, 0.0f, 0.9f);

	if (Magnitude <= Dead)
	{
		return 0.0f;
	}

	// Nach der Totzone auf den vollen Bereich strecken - sonst waere der
	// Vollausschlag nicht mehr erreichbar und der Stick fuehlte sich kurz an.
	const float Rescaled = (Magnitude - Dead) / (1.0f - Dead);

	// Expo: Mischung aus linear und kubisch. Kleine Ausschlaege werden fein
	// aufgeloest, der Vollausschlag bleibt bei 1. Ohne das ist ein
	// Hubschrauber kaum auf der Stelle zu halten.
	const float ExpoAmount = FMath::Clamp(Expo, 0.0f, 1.0f);
	const float Shaped = FMath::Lerp(Rescaled, Rescaled * Rescaled * Rescaled, ExpoAmount);

	return Shaped * FMath::Sign(Clamped);
}

float AWiesbadenHelicopter::AdvanceControlAxis(
	float Current, float Target, float RiseRate, float ReturnRate, float DeltaSeconds)
{
	Target = FMath::Clamp(Target, -1.0f, 1.0f);

	// Ruecklauf ist die Bewegung zur Mitte. Die Pruefung muss ausdruecklich
	// ausschliessen, dass die Achse schon mittig steht: FMath::Sign(0) ist 0
	// und weicht damit von jedem Ziel ab - genau dieser Fehler liess bei der
	// Fahrzeuglenkung das Einlenken aus der Mitte mit der schnelleren
	// Ruecklaufrate laufen.
	const bool bReturning = Current != 0.0f
		&& (FMath::Abs(Target) < FMath::Abs(Current) || Target * Current < 0.0f);

	const float Rate = FMath::Max(bReturning ? ReturnRate : RiseRate, 0.01f);
	const float Step = Rate * FMath::Max(DeltaSeconds, 0.0f);

	return FMath::Clamp(FMath::FInterpConstantTo(Current, Target, 1.0f, Step), -1.0f, 1.0f);
}

float AWiesbadenHelicopter::ComputeAutoLevel(
	float AttitudeDegrees, float CommandedInput, float Strength, float MaxAuthority)
{
	// Wirksamkeit sinkt mit der Eingabe, verschwindet aber NICHT.
	//
	// Vorher galt Freedom = 1 - |Eingabe|: bei Vollausschlag blieb null
	// Stabilisierung. Genau dann braucht man sie aber - der Hubschrauber
	// kippte beim Steuern weg und war "kaum zu navigieren". Ein Rest von
	// 30 Prozent haelt die Lage beherrschbar, ohne den bewusst schraegen
	// Flug zu verhindern.
	constexpr float MinFreedom = 0.30f;
	const float Freedom = FMath::Max(
		MinFreedom, 1.0f - FMath::Clamp(FMath::Abs(CommandedInput), 0.0f, 1.0f));

	const float Correction = -AttitudeDegrees * FMath::Max(Strength, 0.0f);
	const float Authority = FMath::Clamp(MaxAuthority, 0.0f, 1.0f);

	return FMath::Clamp(Correction, -Authority, Authority) * Freedom;
}

float AWiesbadenHelicopter::GetAnalogAxis(const FKey& Key)
{
	const APlayerController* PC = GetHeliController();
	return PC ? PC->GetInputAnalogKeyState(Key) : 0.0f;
}

void AWiesbadenHelicopter::ReadInput(float DeltaSeconds)
{
	// Tastatur UND Gamepad, analog wo moeglich.
	//
	// Hier wurden zuvor nur Tasten gepollt: jede Eingabe war +1, -1 oder 0.
	// Ein Hubschrauber laesst sich so nicht dosieren - das Kollektiv sprang
	// zwischen Steigen und Sinken, ohne Zwischenwerte. Der Gamepad-Stick
	// liefert eine echte Analogachse, die Tastatur wird als Vollausschlag
	// behandelt und ueber die Anstiegsrate weich gemacht.
	//
	// Belegung (uebliche Hubschrauber-Belegung):
	//
	//   Tastatur                    Gamepad
	//   W / S    Nicken             Linker Stick hoch/runter
	//   A / D    Rollen             Linker Stick links/rechts
	//   Q / E    Gieren             Rechter Stick links/rechts
	//   Leer     Kollektiv hoch     Rechter Trigger
	//   Strg     Kollektiv runter   Linker Trigger
	//   G        Triebwerk          Y

	// -- Kollektiv --------------------------------------------------------
	float TargetCollective = 0.0f;
	if (IsKeyDown(EKeys::SpaceBar) || IsKeyDown(EKeys::LeftShift))
	{
		TargetCollective += 1.0f;
	}
	if (IsKeyDown(EKeys::LeftControl))
	{
		TargetCollective -= 1.0f;
	}

	// Trigger sind einseitige Achsen von 0 bis 1; die Differenz ergibt eine
	// beidseitige Achse mit feiner Aufloesung in beide Richtungen.
	const float TriggerUp = GetAnalogAxis(EKeys::Gamepad_RightTriggerAxis);
	const float TriggerDown = GetAnalogAxis(EKeys::Gamepad_LeftTriggerAxis);
	const float TriggerCollective = ApplyStickShaping(TriggerUp - TriggerDown, StickDeadzone, StickExpo);
	if (FMath::Abs(TriggerCollective) > FMath::Abs(TargetCollective))
	{
		TargetCollective = TriggerCollective;
	}

	// -- Zyklisch (Nicken/Rollen) ------------------------------------------
	float TargetPitch = 0.0f;
	if (IsKeyDown(EKeys::W)) { TargetPitch += 1.0f; }
	if (IsKeyDown(EKeys::S)) { TargetPitch -= 1.0f; }

	float TargetRoll = 0.0f;
	if (IsKeyDown(EKeys::D)) { TargetRoll += 1.0f; }
	if (IsKeyDown(EKeys::A)) { TargetRoll -= 1.0f; }

	const float StickPitch = ApplyStickShaping(
		GetAnalogAxis(EKeys::Gamepad_LeftY), StickDeadzone, StickExpo);
	const float StickRoll = ApplyStickShaping(
		GetAnalogAxis(EKeys::Gamepad_LeftX), StickDeadzone, StickExpo);

	// Der groessere Betrag gewinnt - so stoert eine ruhende Eingabequelle die
	// andere nicht, und beide bleiben jederzeit benutzbar.
	if (FMath::Abs(StickPitch) > FMath::Abs(TargetPitch)) { TargetPitch = StickPitch; }
	if (FMath::Abs(StickRoll) > FMath::Abs(TargetRoll)) { TargetRoll = StickRoll; }

	// -- Gieren ------------------------------------------------------------
	float TargetYaw = 0.0f;
	if (IsKeyDown(EKeys::E)) { TargetYaw += 1.0f; }
	if (IsKeyDown(EKeys::Q)) { TargetYaw -= 1.0f; }

	const float StickYaw = ApplyStickShaping(
		GetAnalogAxis(EKeys::Gamepad_RightX), StickDeadzone, StickExpo);
	if (FMath::Abs(StickYaw) > FMath::Abs(TargetYaw)) { TargetYaw = StickYaw; }

	// -- Selbststabilisierung ----------------------------------------------
	//
	// Ohne sie bleibt die Lage stehen, sobald man loslaesst: einmal schraeg,
	// immer schraeg, bis man von Hand gegensteuert. Das ist der Hauptgrund,
	// aus dem sich eine Hubschraubersteuerung unbeherrschbar anfuehlt.
	const FRotator Attitude = GetActorRotation();
	TargetPitch += ComputeAutoLevel(Attitude.Pitch, TargetPitch, AutoLevelStrength, AutoLevelMaxAuthority);
	TargetRoll += ComputeAutoLevel(Attitude.Roll, TargetRoll, AutoLevelStrength, AutoLevelMaxAuthority);

	TargetPitch = FMath::Clamp(TargetPitch, -1.0f, 1.0f);
	TargetRoll = FMath::Clamp(TargetRoll, -1.0f, 1.0f);

	// -- Nachfuehren mit getrennten Raten je Achse -------------------------
	//
	// Zuvor lag EINE exponentielle Glaettung ueber allen vier Achsen. Ein
	// Hubschrauber ist darauf aber verschieden traege: Das Kollektiv haengt an
	// der Blattverstellung und braucht am laengsten, das Gierpedal spricht am
	// schnellsten an.
	CollectiveInput = AdvanceControlAxis(
		CollectiveInput, TargetCollective, CollectiveRiseRate, CollectiveReturnRate, DeltaSeconds);
	CyclicPitchInput = AdvanceControlAxis(
		CyclicPitchInput, TargetPitch, CyclicRiseRate, CyclicReturnRate, DeltaSeconds);
	CyclicRollInput = AdvanceControlAxis(
		CyclicRollInput, TargetRoll, CyclicRiseRate, CyclicReturnRate, DeltaSeconds);
	YawInput = AdvanceControlAxis(
		YawInput, TargetYaw, YawRiseRate, YawReturnRate, DeltaSeconds);

	// Triebwerk an/aus (Flanke auf G oder Y am Gamepad) - erlaubt Autorotationstests.
	const bool bEnginePressed = IsKeyDown(EKeys::G) || IsKeyDown(EKeys::Gamepad_FaceButton_Top);
	if (bEnginePressed && !bEngineToggleHeld)
	{
		bEngineRunning = !bEngineRunning;
		UE_LOG(LogWbVehicles, Log, TEXT("Triebwerk %s."), bEngineRunning ? TEXT("an") : TEXT("aus"));
	}
	bEngineToggleHeld = bEnginePressed;

	// Externe Steuerung (KI/Zwischensequenz/Test-Harness): ueberschreibt die
	// geglaetteten Steuerwerte, wirkt aber ueber die echte Rotorphysik. Die
	// Choreografie (Gierprobe, Flugprofil) liegt bewusst NICHT hier, sondern in
	// UWiesbadenVehicleTestHarness - die Flugsimulation bleibt frei davon.
	if (bExternalControlActive)
	{
		bEngineRunning = ExternalControl.bEngine;
		CollectiveInput = FMath::Clamp(ExternalControl.Collective, -1.0f, 1.0f);
		CyclicPitchInput = FMath::Clamp(ExternalControl.Pitch, -1.0f, 1.0f);
		CyclicRollInput = FMath::Clamp(ExternalControl.Roll, -1.0f, 1.0f);
		YawInput = FMath::Clamp(ExternalControl.Yaw, -1.0f, 1.0f);
	}
}

void AWiesbadenHelicopter::ApplyFlightPhysics(float DeltaSeconds)
{
	// Eingaben aus den geglaetteten Steuerwerten.
	FWiesbadenRotorPhysicsInput RotorInput;
	RotorInput.Collective = FMath::Clamp(0.5f + 0.5f * CollectiveInput, 0.0f, 1.0f);
	RotorInput.CyclicPitch = CyclicPitchInput;
	RotorInput.CyclicRoll = CyclicRollInput;
	RotorInput.YawPedal = YawInput;
	RotorInput.bEngineRunning = bEngineRunning;

	// Lokale Geschwindigkeiten fuer das Modul (Autorotation + Daempfung).
	const FVector LocalVelocity = GetActorTransform().InverseTransformVectorNoScale(Velocity);

	FWiesbadenRotorPhysicsOutput RotorOut;
	RotorPhysics.Tick(RotorInput, DeltaSeconds, LocalVelocity, AngularVelocity, RotorOut);

	// Kraft (N) -> Beschleunigung (cm/s^2): 1 N/kg = 100 cm/s^2.
	const float InvMass = 1.0f / FMath::Max(RotorPhysics.MassKg, 1.0f);
	const FVector WorldAccel = GetActorTransform().TransformVectorNoScale(RotorOut.Force * (InvMass * 100.0f));

	// Schwerkraft + Rumpf-Luftwiderstand (Fahrzeug-Ebene).
	Velocity += WorldAccel * DeltaSeconds;
	Velocity += FVector(0.0f, 0.0f, -GravityCmPerS2) * DeltaSeconds;
	Velocity -= Velocity * LinearDrag * DeltaSeconds;

	// Schwebehilfe: bei neutralem Kollektiv die Vertikalgeschwindigkeit
	// abklingen lassen. Der Faktor blendet mit dem Hebelausschlag aus -
	// wer bewusst steigt oder sinkt, bekommt keine Gegenwehr.
	if (bEngineRunning && HoverAssistStrength > 0.0f)
	{
		const float Neutral = 1.0f - FMath::Clamp(FMath::Abs(CollectiveInput) * 4.0f, 0.0f, 1.0f);
		if (Neutral > 0.0f)
		{
			Velocity.Z = FMath::FInterpTo(
				Velocity.Z, 0.0f, DeltaSeconds, HoverAssistStrength * Neutral);
		}
	}

	// Driftdaempfung: das Gegenstueck zur Schwebehilfe fuer die WAAGERECHTE.
	//
	// Die Schwebehilfe oben haelt die Hoehe, aber nichts hielt die Position.
	// Wer zum Beschleunigen nach vorn kippt, rutscht anschliessend weiter -
	// der Luftwiderstand allein bremst kaum. Zum Anhalten musste man exakt
	// gegensteuern und den Ausschlag im richtigen Moment zuruecknehmen; das
	// gelingt am Stick praktisch nie, und genau daran lag "kann kaum
	// navigieren".
	//
	// Wie die Schwebehilfe eine DAEMPFUNG, keine Sollwertregelung: sie zieht
	// die Geschwindigkeit gegen null, nicht die Position auf einen Punkt.
	// Damit arbeitet sie nie gegen den Piloten.
	if (bEngineRunning && DriftAssistStrength > 0.0f)
	{
		const float Cyclic = FMath::Max(
			FMath::Abs(CyclicPitchInput), FMath::Abs(CyclicRollInput));
		const float Neutral = 1.0f - FMath::Clamp(Cyclic * 4.0f, 0.0f, 1.0f);
		if (Neutral > 0.0f)
		{
			Velocity.X = FMath::FInterpTo(
				Velocity.X, 0.0f, DeltaSeconds, DriftAssistStrength * Neutral);
			Velocity.Y = FMath::FInterpTo(
				Velocity.Y, 0.0f, DeltaSeconds, DriftAssistStrength * Neutral);
		}
	}

	// Hoechstgeschwindigkeit begrenzen.
	const float SpeedSq = Velocity.SizeSquared();
	const float MaxSq = MaxSpeedCmPerS * MaxSpeedCmPerS;
	if (SpeedSq > MaxSq)
	{
		Velocity *= MaxSpeedCmPerS / FMath::Sqrt(SpeedSq);
	}

	// Weiche Boden-Kollision.
	ApplyGroundConstraint(DeltaSeconds);

	// Position integrieren.
	AddActorWorldOffset(Velocity * DeltaSeconds, true);

	// Drehmoment (N*m) -> Winkelbeschleunigung (rad/s^2) pro Achse.
	const float InvRoll = 1.0f / FMath::Max(MomentOfInertiaKgM2.X, 0.01f);
	const float InvPitch = 1.0f / FMath::Max(MomentOfInertiaKgM2.Y, 0.01f);
	const float InvYaw = 1.0f / FMath::Max(MomentOfInertiaKgM2.Z, 0.01f);
	const FVector AngAccel(
		RotorOut.Torque.X * InvRoll,
		RotorOut.Torque.Y * InvPitch,
		RotorOut.Torque.Z * InvYaw);
	AngularVelocity += AngAccel * DeltaSeconds;

	// Ratendaempfung: ohne Knueppelausschlag klingt die Drehbewegung ab.
	// Je Achse mit dem eigenen Kommando ausgeblendet, damit ein bewusster
	// Ausschlag nie gegen die Hilfe arbeitet.
	if (bEngineRunning && RateAssistStrength > 0.0f)
	{
		const float RollNeutral = 1.0f - FMath::Clamp(FMath::Abs(CyclicRollInput) * 2.0f, 0.0f, 1.0f);
		const float PitchNeutral = 1.0f - FMath::Clamp(FMath::Abs(CyclicPitchInput) * 2.0f, 0.0f, 1.0f);
		const float YawNeutral = 1.0f - FMath::Clamp(FMath::Abs(YawInput) * 2.0f, 0.0f, 1.0f);

		// NUR daempfen, wenn die Achse nicht (nahezu) voll kommandiert ist.
		//
		// FMath::FInterpTo gibt bei InterpSpeed <= 0 SOFORT das Ziel zurueck -
		// hier also 0. Bei vollem Ausschlag ist der Neutral-Faktor 0, die
		// Daempfung wurde damit zu einem harten Nullsetzen der Drehrate: Wer
		// Gieren kommandierte, dem wurde die Gierrate JEDES Bild auf null
		// gerissen - der Hubschrauber drehte trotz vollem Giermoment nicht.
		// Deshalb die Daempfung ueberspringen, sobald der Faktor verschwindet.
		if (RollNeutral > KINDA_SMALL_NUMBER)
		{
			AngularVelocity.X = FMath::FInterpTo(
				AngularVelocity.X, 0.0f, DeltaSeconds, RateAssistStrength * RollNeutral);
		}
		if (PitchNeutral > KINDA_SMALL_NUMBER)
		{
			AngularVelocity.Y = FMath::FInterpTo(
				AngularVelocity.Y, 0.0f, DeltaSeconds, RateAssistStrength * PitchNeutral);
		}
		if (YawNeutral > KINDA_SMALL_NUMBER)
		{
			AngularVelocity.Z = FMath::FInterpTo(
				AngularVelocity.Z, 0.0f, DeltaSeconds, RateAssistStrength * 0.6f * YawNeutral);
		}
	}

	// Rotation anwenden: FRotator(Pitch, Yaw, Roll) aus (Roll, Pitch, Yaw)-Achsen.
	const FRotator DeltaRot(
		FMath::RadiansToDegrees(AngularVelocity.Y) * DeltaSeconds, // Pitch
		FMath::RadiansToDegrees(AngularVelocity.Z) * DeltaSeconds, // Yaw
		FMath::RadiansToDegrees(AngularVelocity.X) * DeltaSeconds);// Roll
	AddActorLocalRotation(DeltaRot);
}

void AWiesbadenHelicopter::ApplyGroundConstraint(float DeltaSeconds)
{
	// Einfacher Down-Raycast; verhindert Durchsinken, ohne Physik-Simulation.
	if (!GetWorld())
	{
		return;
	}

	FHitResult Hit;
	const FVector Start = GetActorLocation();
	const FVector End = Start - FVector(0.0f, 0.0f, 100000.0f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbHeliGround), true);
	Params.AddIgnoredActor(this);

	bGrounded = false;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		// Boden fuer den visuellen Pfad cachen (Hoehenanzeige + Downwash-Staub
		// lesen diesen Wert und tracen nicht selbst nochmal).
		CachedGroundZ = Hit.Location.Z;
		bGroundCacheValid = true;

		const float Altitude = Hit.Distance;
		if (Altitude < MinGroundClearanceCm)
		{
			// Unterhalb der Mindestflughoehe: anheben und Sinken daempfen.
			AddActorWorldOffset(FVector(0.0f, 0.0f, MinGroundClearanceCm - Altitude), true);
			if (Velocity.Z < 0.0f)
			{
				Velocity.Z = FMath::Max(0.0f, Velocity.Z * 0.25f);
			}
			bGrounded = true;
		}
		return;
	}

	// KEIN Treffer nach unten. Das hiess hier bisher stillschweigend "freier
	// Himmel" - und genau daran ist der Helikopter jedes Mal verschwunden:
	//
	// Er wird in Frame 0 abgesetzt, bevor World Partition Gelaende und Strassen
	// gestreamt hat. Der Abwaertstrace fand nichts, die Schwerkraft lief
	// trotzdem weiter, und als der Boden Sekunden spaeter da war, lag der
	// Helikopter bereits darunter und fiel mit hoher Geschwindigkeit. Von dort
	// trifft ein Abwaertstrace erst recht nichts mehr - er war endgueltig weg.
	// Im Log stand jedes Mal "Helikopter abgesetzt", im Spiel war er nie zu
	// finden.
	//
	// Kein Treffer bedeutet daher: Boden UNBEKANNT, nicht Boden ABWESEND.
	FHitResult UpHit;
	if (GetWorld()->LineTraceSingleByChannel(
		UpHit, Start, Start + FVector(0.0f, 0.0f, 100000.0f), ECC_WorldStatic, Params))
	{
		// Wir stecken unter der Welt - zurueck auf die Oberflaeche setzen.
		SetActorLocation(UpHit.Location + FVector(0.0f, 0.0f, MinGroundClearanceCm), false);
		Velocity.Z = 0.0f;
		bGrounded = true;
		return;
	}

	// Weder ueber noch unter uns Geometrie: der Untergrund ist noch nicht
	// geladen. Solange nicht sinken, sonst faellt der Helikopter waehrend des
	// Streamings aus der Welt.
	Velocity.Z = FMath::Max(Velocity.Z, 0.0f);
}

void AWiesbadenHelicopter::UpdateRotors(float DeltaSeconds)
{
	// Rotor-Naben visuell drehen; die Drehzahl kommt aus dem Physik-Modul.
	// U/min -> deg/s: * 360 / 60 = * 6.
	const float MainDegPerSec = RotorPhysics.MainRotorRpm * 6.0f;
	const float TailDegPerSec = RotorPhysics.TailRotorRpm * 6.0f;

	if (MainRotorHub)
	{
		MainRotorHub->AddLocalRotation(FRotator(0.0f, MainDegPerSec * DeltaSeconds, 0.0f));
	}
	if (LowerRotorHub)
	{
		// Gegenlaeufiger unterer Rotor des Koaxial-Paars.
		LowerRotorHub->AddLocalRotation(FRotator(0.0f, -MainDegPerSec * DeltaSeconds, 0.0f));
	}
	if (TailRotorHub)
	{
		TailRotorHub->AddLocalRotation(FRotator(TailDegPerSec * DeltaSeconds, 0.0f, 0.0f));
	}
}

void AWiesbadenHelicopter::UpdateVisualEffects(float DeltaSeconds)
{
	// MIDs beim ersten Aufruf anlegen (beide Blur-Scheiben teilen sich eine).
	if (!RotorBlurMID && UpperRotorBlur && UpperRotorBlur->GetMaterial(0))
	{
		RotorBlurMID = UpperRotorBlur->CreateDynamicMaterialInstance(0);
		if (RotorBlurMID && LowerRotorBlur)
		{
			LowerRotorBlur->SetMaterial(0, RotorBlurMID);
		}
	}
	if (!DownwashMID && GroundDust && GroundDust->GetMaterial(0))
	{
		DownwashMID = GroundDust->CreateDynamicMaterialInstance(0);
	}

	// Entwickler-Vorschau: -WbHeliSpin dreht den Rotor fuer Screenshots hoch und
	// setzt das Triebwerk auf laufend, damit sich Rotor-Blur und Downwash auch am
	// abgestellten Heli beurteilen lassen (im echten Spiel nie gesetzt).
	static const bool bSpinDemo = FParse::Param(FCommandLine::Get(), TEXT("WbHeliSpin"));

	// --- Rotor-Blur: Scheiben blenden mit der Drehzahl ein ---
	const float Rpm = bSpinDemo ? 450.0f : RotorPhysics.MainRotorRpm;
	const bool bEngineForVfx = bSpinDemo ? true : bEngineRunning;
	// Unter ~120 U/min sieht man die Blaetter, ab ~360 die volle Scheibe.
	const float BlurAlpha = FMath::Clamp((Rpm - 120.0f) / 240.0f, 0.0f, 1.0f);
	if (RotorBlurMID)
	{
		RotorBlurMID->SetScalarParameterValue(TEXT("Opacity"), BlurAlpha * 0.33f);
	}
	// Solide Blaetter oberhalb 75 % Blur ausblenden - dann traegt die Scheibe das Bild.
	const bool bBladesVisible = (BlurAlpha < 0.75f);
	if (MainRotorBlade && MainRotorBlade->IsVisible() != bBladesVisible)
	{
		MainRotorBlade->SetVisibility(bBladesVisible);
	}
	if (LowerRotorBlade && LowerRotorBlade->IsVisible() != bBladesVisible)
	{
		LowerRotorBlade->SetVisibility(bBladesVisible);
	}

	// --- Downwash-Staub am Boden ---
	float DustAlpha = 0.0f;
	if (bEngineForVfx && Rpm > 200.0f)
	{
		const float HeightM = GetAltitudeMeters();
		// Nur bis ~12 m ueber Grund, staerker je naeher und je hoeher der Collective.
		const float Proximity = FMath::Clamp(1.0f - HeightM / 12.0f, 0.0f, 1.0f);
		const float Collective = FMath::Clamp(0.5f + 0.5f * CollectiveInput, 0.0f, 1.0f);
		DustAlpha = Proximity * (0.35f + 0.65f * Collective);
	}
	if (GroundDust)
	{
		const bool bDust = DustAlpha > 0.01f;
		if (GroundDust->IsVisible() != bDust)
		{
			GroundDust->SetVisibility(bDust);
		}
		if (bDust)
		{
			// Boden direkt unter dem Heli - aus dem Frame-Cache (ApplyGround-
			// Constraint hat diesen Frame bereits getract); nur zur Not selbst.
			const FVector HeliLoc = GetActorLocation();
			FVector GroundLoc = HeliLoc - FVector(0.0f, 0.0f, 800.0f);
			if (bGroundCacheValid)
			{
				GroundLoc = FVector(HeliLoc.X, HeliLoc.Y, CachedGroundZ + 8.0f);
			}
			else if (UWorld* World = GetWorld())
			{
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(WbHeliDownwash), true);
				Params.AddIgnoredActor(this);
				if (World->LineTraceSingleByChannel(
						Hit, HeliLoc, HeliLoc - FVector(0.0f, 0.0f, 100000.0f), ECC_WorldStatic, Params)
					&& !Hit.bStartPenetrating)
				{
					GroundLoc = Hit.Location + FVector(0.0f, 0.0f, 8.0f);
				}
			}
			// Leichtes Pulsieren des Durchmessers (Downwash "atmet").
			DustPhase += DeltaSeconds * 2.2f;
			const float Pulse = 15.0f + 2.5f * FMath::Sin(DustPhase);
			GroundDust->SetWorldLocation(GroundLoc);
			GroundDust->SetWorldRotation(FRotator::ZeroRotator);
			GroundDust->SetWorldScale3D(FVector(Pulse, Pulse, 0.02f));
			if (DownwashMID)
			{
				DownwashMID->SetScalarParameterValue(TEXT("Opacity"), DustAlpha * 0.5f);
			}
		}
	}
}

void AWiesbadenHelicopter::UpdateAudio(float DeltaSeconds)
{
	if (!HelicopterAudio)
	{
		return;
	}

	// Collective-Wert des Physik-Moduls (0..1) als Blattlast weitergeben.
	const float Collective = FMath::Clamp(0.5f + 0.5f * CollectiveInput, 0.0f, 1.0f);
	HelicopterAudio->SetRotorState(RotorPhysics.MainRotorRpm, Collective);
	HelicopterAudio->SetEngineState(RotorPhysics.EngineRpm, bEngineRunning);
	HelicopterAudio->SetForwardSpeed(Velocity.Size() / 100.0f);
}

APlayerController* AWiesbadenHelicopter::GetHeliController()
{
	// Gecachten Pilot-Controller nutzen; nur wenn keiner da ist (z.B. KI-Besitz
	// vor PossessedBy), einmalig casten.
	if (CachedPlayerController)
	{
		return CachedPlayerController;
	}
	return Cast<APlayerController>(GetController());
}

bool AWiesbadenHelicopter::IsKeyDown(const FKey& Key)
{
	const APlayerController* PC = GetHeliController();
	return PC && PC->IsInputKeyDown(Key);
}
