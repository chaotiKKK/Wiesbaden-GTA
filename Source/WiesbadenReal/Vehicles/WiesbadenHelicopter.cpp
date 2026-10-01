// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenHelicopter.h"
#include "Core/WiesbadenInputMap.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#include "WiesbadenReal.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"          // TActorIterator: den Sebbotower finden
#include "World/WiesbadenSebboHq.h"   // Landeplatz des Turms (eine Wahrheit)
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
	// Cockpit-Innenraum und Kanone kommen aus der Blender-Pipeline
	// (Tools/Blender/build_ka52_cockpit.py -> Tools/import_ka52_cockpit.cmd).
	// Beide Assets tragen erzwungene SM_-Namen, damit der Pfad hier nicht
	// vom FBX-Meshnamen abhaengt.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeliCockpitMesh(
		TEXT("/Game/Vehicles/Ka52/SM_Ka52Cockpit.SM_Ka52Cockpit"));

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

	// Drehpunkt der Rotor-Meshes im Modellraum (cm), gegen die
	// Rotorstangenachse (0, 0) gerechnet.
	//
	// GEMESSEN, nicht angenommen (Tools/ka52_rotorachse.py,
	// Saved/Diagnose/ka52/rotorachse.txt): Der Drehpunkt eines
	// Dreiblattrotors ist der SCHWERPUNKT seiner Vertexmenge, weil drei
	// gleiche Blaetter im 120-Grad-Abstand die Menge 3-fach-symmetrisch
	// machen. Bestaetigt durch zwei unabhaengige Wege und eine Kontrolle:
	//   - der Schwerpunkt der aeusseren 30 % (Blattspitzen) faellt auf
	//     4,4 bzw. 9,2 cm;
	//   - die Punktmenge um 120 Grad gedreht passt mit Median 4,1 mm
	//     (oben) bzw. 6,7 mm (unten) auf sich selbst zurueck. An der
	//     Kontrollstelle (Bounding-Box-Mitte) sind es 375 mm - Faktor 92
	//     bzw. 56. Ein Verfahren, das ueberall "gut" meldet, prueft nichts.
	//   - unabhaengige Gegenprobe am Rumpf: die Punkte liegen auf der
	//     Mittelsenkrechten (2,2 bzw. 7,4 cm daneben) bei 41,4 % bzw.
	//     41,1 % der Rumpflaenge ab der Nase. Das ist die Lage eines
	//     Hauptmastes; ein Bogenmast waere irgendwo sonst.
	//
	// Bis hierher stand im Kommentar "die Rotor-Achse liegt in Mesh-XY
	// exakt bei (0, 0)". Fuer den oberen Rotor ist das mit 2,6 cm
	// brauchbar, fuer den unteren mit 5,4 cm nicht. Die frueher notierten
	// "6,66 cm / 0,83 cm" stammten aus der Mitte der Bounding Box - die
	// liegt 1,80 m daneben und war als Achse nie brauchbar.
	const FVector RotorDrehpunktOben = GetRotorDrehpunktCm(false);
	const FVector RotorDrehpunktUnten = GetRotorDrehpunktCm(true);

	// Gierdrehung des Modells.
	//
	// ACHTUNG, RICHTUNG: +90, nicht -90. Das importierte FBX wurde mit echten
	// Vertexdaten vermessen (Tools/ka52_fbxlage.py, Saved/Diagnose/ka52/
	// fbxlage.txt): der Heckfinner (am Ende nur 66 cm breit, Oberkante
	// 295 cm) und das weisse Strobe liegen bei Modell-+Y, die NASE (189 cm
	// breit, Oberkante nur 160 cm) bei Modell--Y. Der Actor faehrt aber nach
	// Welt-+X. Mit -90 zeigte damit das Heck nach vorn: der Hubschrauber
	// flog rueckwaerts. Beweisbild: Saved/Diagnose/ka52/flugrichtung.png
	// (Nase bei X = -7,1 m mit -90, bei +7,1 m mit +90).
	//
	// Nebenbei stimmt dann auch die Seite: Modell-+X (Steuerbord, rotes
	// Licht) wandert nach Welt-+Y, und +Y ist im Actor-Rechtssystem die
	// rechte Seite. Rot rechts, gruen links - so, wie es sein muss.
	const FRotator ModelYaw(0.0f, 90.0f, 0.0f);


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

	// Cockpit-Innenraum. Das Mesh bringt seine Modellkoordinaten mit (Kabine
	// Y -500..-231, Z 71..220) und sitzt darum ohne Versatz am Rumpf - es
	// haengt an FuselageMesh und erbt dessen ModelYaw, Massstab und Lage.
	CockpitMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CockpitMesh"));
	CockpitMesh->SetupAttachment(FuselageMesh);
	CockpitMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CockpitMesh->SetGenerateOverlapEvents(false);
	if (HeliCockpitMesh.Succeeded())
	{
		CockpitMesh->SetStaticMesh(HeliCockpitMesh.Object);

		// Zweiseitig schalten, sonst sieht der Pilot in die leere Kabine.
		//
		// Die Huellle ist geschlossen (0 nicht-gepaarte Kanten) und NICHT
		// einheitlich gewickelt: vom Pilotenauge aus zeigen 166 der 342
		// Flaechen vom Auge weg, 130 ihm zu (gemessen am FBX, Blender). Mit
		// einseitigen Materialien wird der Rest verworfen - sichtbar bleibt
		// allein die Innenflaeche der gegenueberliegenden Wand, im Bild also
		// ein flaches graues Band statt Tafel, Sitzen und Rahmen.
		//
		// Der Schalter sitzt am UMaterial, nicht an der Instanz. Die vier
		// Kabinenmaterialien teilen sich ihr Elternmaterial
		// (FBXLegacyPhongSurfaceMaterial) mit den uebrigen FBX-Teilen des
		// Hubschraubers; die Umstellung gilt daher fuer alle. Von aussen ist
		// das gleichwertig, es kostet nur mehr Overdraw.
		for (int32 Slot = 0; Slot < CockpitMesh->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Stoff = CockpitMesh->GetMaterial(Slot);
			UMaterial* Basis = Stoff ? Stoff->GetMaterial() : nullptr;
			if (Basis && !Basis->TwoSided)
			{
				Basis->TwoSided = true;
				// Shader neu bauen: TwoSided steckt in der Variante. Ohne
				// diesen Aufruf bleibt die alte, einseitige Variante aktiv
				// und das Bild zeigt weiterhin nur das graue Band. (Der
				// direkte Weg CacheResourceShadersForRendering ist in
				// UMaterial privat; PostEditChangeProperty ist der oeffentliche
				// und macht genau das.)
#if WITH_EDITOR
				Basis->PostEditChange();
#endif
			}
		}
	}
	else
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Ka52-Cockpitmesh fehlt - im Cockpit steht man im leeren Rumpf."));
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
		// Die Korrektur liegt in ComputeRotorMountOffset - dort steht, wie der
		// gemessene Drehpunkt des Mesh auf die Rotorstangenachse (0, 0) im
		// Modellraum zu legen ist. Zwei Zeilen, keine Sonderbehandlung fuer einen
		// der beiden Rotoren; der Hub-Node selbst bleibt unangetastet.
		MainRotorBlade->SetRelativeLocation(
			ComputeRotorMountOffset(RotorDrehpunktOben, ModelYaw, UpperRotorHeightCm));
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
		LowerRotorBlade->SetRelativeLocation(
			ComputeRotorMountOffset(RotorDrehpunktUnten, ModelYaw, LowerRotorHeightCm));
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
	// Das Blur-Netz ist bewusst ~10 % groesser als der echte Blattkreis (15,6 /
	// 16,0 m): das Material blendet die Opazitaet schon INNERHALB des Netzrands
	// auf 0 (runde Kante), sodass die facettierte Zylinderkante nie sichtbar wird.
	const FVector BlurScaleUpper(17.2f, 17.2f, 0.02f);
	const FVector BlurScaleLower(17.6f, 17.6f, 0.02f);
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
	VehicleCamera->PadToggleKey = EKeys::Gamepad_RightThumbstick;

	// Ruhiger Horizont und Positions-Nachlauf: der Rumpf neigt sich IM Bild,
	// nicht das Bild mit ihm, und die Kamera federt Beschleunigungen weich
	// ab. Vorher uebertrug sich jeder zyklische Ausschlag ungefiltert auf
	// die Kamera - das wirkte hektisch und machte das Zielen von Blicken
	// unnoetig schwer.
	VehicleCamera->bLevelHorizon = true;
	VehicleCamera->PositionLagSpeed = 9.0f;
	VehicleCamera->FollowArmLength = 1500.0f;
	VehicleCamera->FollowPitchOffset = -10.0f;

	// Pilotensitz vorn im Rumpf, Blick nach vorn.
	//
	// Der Augpunkt sitzt an der GEBAUTEN Kabine, nicht an einer geratenen
	// Zahl. Messung am 26.09.2026, zwei unabhaengige Quellen:
	//   1. Tools/Blender/build_ka52_cockpit.py, Zeile 409: die Pilotenvorschau
	//      wurde von (34, -392, 206) gerendert - das ist der Pilotenplatz.
	//   2. Das importierte Sitzkissen liegt bei X +9..+59, Y -371..-421 cm.
	// Beide decken sich. Mit ModelYaw +90 (Modell--Y -> Actor-+X) wird daraus
	// Actor (395, 34, 206) - Aughoehe 133 cm ueber dem Kabinenboden (71),
	// Kiste 71..220.
	//
	// WICHTIG - in welchem Raum steht CockpitOffset? Die Fahrzeugkamera
	// haengt am SceneRoot, der Cockpit-Socket an ihr, der Rumpf ebenfalls am
	// SceneRoot ohne Z-Versatz: der Versatz wird also im ACTORraum addiert.
	// Deshalb stehen hier die GEDREHTEN Werte (395, 34), nicht die
	// Modellwerte (34, -395). Die Kamerabasis (0, 0, 130) wird genau einmal
	// abgezogen.
	//
	// Am 26.09.2026 lag an dieser Stelle eine Fehldiagnose: man verglich den
	// Actor-Augpunkt (395, 34, 204) mit der MESH-lokalen Kabinenbox
	// (X -81..81) und schloss daraus "314 cm davor, in freier Luft". Beide
	// Angaben stehen in verschiedenen Rahmen. Richtig ist der Vergleich in
	// einem Rahmen - genau den macht jetzt Ka52GeraetTest ("Augenhoehe ueber
	// Kabinenboden"), inklusive Asset-Box.
	VehicleCamera->CockpitOffset = FVector(395.0f, 34.0f, 74.0f);
	VehicleCamera->CockpitPitch = -7.0f;
	VehicleCamera->AddCockpitHiddenMesh(FuselageMesh);
	VehicleCamera->AddCockpitHiddenMesh(TailBoomMesh);
	VehicleCamera->AddCockpitHiddenMesh(TailFinMesh);

	// -- Geraet: Lichtbastel und Bordgeschuetz ------------------------------
	//
	// Beide bekommen dieselbe Modelldrehung und denselben Massstab wie
	// Rumpf und Rotoren. Das ist der Grund, warum es SetModelTransform
	// gibt: Licht, Waffe und Rumpf teilen sich damit eine Zahl statt je
	// einer eigenen. Als die Leuchten ueber ihre eigene Kopie der Drehung
	// verfuegten, saessen sie an der falschen Seite.
	LightRig = CreateDefaultSubobject<UWiesbadenHeliLightRig>(TEXT("Lichtbastel"));
	LightRig->SetupAttachment(SceneRoot);
	LightRig->SetModelTransform(ModelScale, ModelYaw);

	Gun = CreateDefaultSubobject<UWiesbadenHeliGunComponent>(TEXT("Bordgeschoetz"));
	Gun->SetupAttachment(SceneRoot);
	Gun->SetModelTransform(ModelScale, ModelYaw);

	// Flugsound: echte Ka-52-Aufnahmen aus Tools/make_ka52_audio.py
	// (Rotorblatt-Ticken, TV3-117, Fahrtwind). Die prozedurale Synthese in
	// der Komponente bleibt Rueckfall fuer den Fall, dass die Assets fehlen.
	HelicopterAudio = CreateDefaultSubobject<UWiesbadenHelicopterAudioComponent>(TEXT("HelicopterAudio"));
	HelicopterAudio->SetupAttachment(SceneRoot);
	HelicopterAudio->SetRelativeLocation(FVector(0.0f, 0.0f, 130.0f));
	{
		static ConstructorHelpers::FObjectFinder<USoundWave> Ka52RotorSound(
			TEXT("/Game/Audio/Ka52/S_Ka52_Rotor.S_Ka52_Rotor"));
		static ConstructorHelpers::FObjectFinder<USoundWave> Ka52EngineSound(
			TEXT("/Game/Audio/Ka52/S_Ka52_Engine.S_Ka52_Engine"));
		static ConstructorHelpers::FObjectFinder<USoundWave> Ka52WindSound(
			TEXT("/Game/Audio/Ka52/S_Ka52_Wind.S_Ka52_Wind"));
		if (Ka52RotorSound.Succeeded())
		{
			HelicopterAudio->RotorSound = Ka52RotorSound.Object;
		}
		if (Ka52EngineSound.Succeeded())
		{
			HelicopterAudio->EngineSound = Ka52EngineSound.Object;
		}
		if (Ka52WindSound.Succeeded())
		{
			HelicopterAudio->WindSound = Ka52WindSound.Object;
		}
	}

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

	if (bDestroyed)
	{
		UpdateCrash(DeltaSeconds);
		if (Gun) { Gun->SetTriggerHeld(false); }
		UpdateRotors(DeltaSeconds);
		UpdateVisualEffects(DeltaSeconds);
		UpdateAudio(DeltaSeconds);
		return;
	}

	// Ohne Pilot wird nicht geflogen.
	//
	// Der abgestellte Helikopter durchlief bisher dieselbe Flugsimulation wie
	// der geflogene. Mit laufendem Triebwerk und dem Ruhewert von 50 %
	// Kollektiv erzeugte der Rotor Auftrieb, und er stieg unbemannt davon -
	// gemessen 126 m ueber Grund. Die Spawn-Meldung stimmte jedes Mal, zu
	// finden war er trotzdem nie.
	if (!Controller)
	{
		if (Gun) { Gun->SetTriggerHeld(false); }
		ParkOnGround();
		UpdateRotors(DeltaSeconds);
		UpdateVisualEffects(DeltaSeconds);
		UpdateAudio(DeltaSeconds);
		return;
	}

	// Konsolen-Schalter fuer die Fahrzeugkamera: wb.HeliKamera (CVar, 0 Folge,
	// 1 Orbit, 2 Cockpit). Einmalig uebernehmen und danach zuruecksetzen, damit
	// er die Taste C nicht dauerhaft ueberstimmt. Taste C allein genuegt nicht:
	// sie erreicht das Spiel nicht zuverlaessig - in einem Lauf blieb der Modus
	// auf 0 und das Bild war trotzdem als Cockpit beschriftet.
	if (VehicleCamera)
	{
		// Die CVar wird namentlich geholt, nicht verlinkt: sie gehoert dem
		// PlayerController (dort liegt der Befehl), und ein Include des
		// IConsoleManagers in einem Fahrzeug-Header waere nur fuer diese
		// eine Zahl zu viel.
		if (IConsoleVariable* Konst =
			IConsoleManager::Get().FindConsoleVariable(TEXT("wb.HeliKamera")))
		{
			const int32 Gewuenscht = Konst->GetInt();
			if (Gewuenscht >= 0
				&& Gewuenscht != static_cast<int32>(VehicleCamera->GetCameraMode()))
			{
				VehicleCamera->SetCameraMode(
					static_cast<EWiesbadenVehicleCameraMode>(Gewuenscht));
				// Zuruecksetzen, damit die Taste C spaeter wieder gilt.
				Konst->Set(-1, ECVF_SetByCode);
				UE_LOG(LogWbVehicles, Log,
					TEXT("Ka52: Kameramodus %d aus der Konsole gesetzt."), Gewuenscht);
			}
		}
	}

	ReadInput(DeltaSeconds);
	ApplyFlightPhysics(DeltaSeconds);
	UpdateRotors(DeltaSeconds);
	UpdateVisualEffects(DeltaSeconds);
	UpdateAudio(DeltaSeconds);
	// Geraete lesen hier ihre Tasten. AM 26.09.2026 stand ReadDeviceInput
	// an dieser Stelle nicht: die Funktion war definiert, aber ohne jeden
	// Aufrufer. Damit hatte das Bordgeschoetz keinen lebenden Abzug - im
	// Spiel liess sich die Kanone ueberhaupt nicht ausloesen, und ein
	// Bildbeleg fuer das Muendungsfeuer war damit unmoeglich. Gemessen:
	// 39 s gehaltener Abzug, 0 Schuesse, kein "Munition leer".
	ReadDeviceInput(DeltaSeconds);
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
	SetLessonDryFireActive(false);
	ReleaseFireForRespawn();

	UE_LOG(LogWbVehicles, Log, TEXT("Helikopter %s wurde freigegeben."), *GetName());
}

void AWiesbadenHelicopter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Nur beim Actor-Ende wird die lokale Sperre geloescht; bei Respawn
	// und Aussteigen muss weiterhin eine physische Loslass-Eingabe folgen.
	if (Gun) { Gun->SetTriggerHeld(false); }
	bLessonDryFireActive = false;
	bDryFireReleasePending = false;
	bTriggerHeld = false;

	UE_LOG(LogWbVehicles, Log,
		TEXT("Helikopter %s EndPlay (Grund %d) - Abzug und Flugstunden-Feuersperre zurueckgesetzt."),
		*GetName(), static_cast<int32>(EndPlayReason));

	Super::EndPlay(EndPlayReason);
}

void AWiesbadenHelicopter::CycleCameraMode()
{
	if (VehicleCamera)
	{
		VehicleCamera->CycleCameraMode();
	}
	// Warum ist in der Cockpit-Ansicht keine Kabine zu sehen? Der Augpunkt
	// liegt nachweislich in der Kabine (Ka52GeraetTest), und sie wird nicht
	// ausgeblendet - also ist entweder das Mesh gar nicht auf dem Bildschirm
	// oder es steht nicht dort, wo die Kabine steht. Genau das sagt der
	// letzte Renderzeitpunkt: 0 heisst "nie gezeichnet".
	if (CockpitMesh && VehicleCamera)
	{
		UE_LOG(LogWbVehicles, Log,
			TEXT("Kabine: Sichtbar=%d OwnerNoSee=%d versteckt=%d Grenzen %s")
			TEXT(" zuletztGezeichnet=%d"),
			CockpitMesh->IsVisible() ? 1 : 0,
			CockpitMesh->bOwnerNoSee ? 1 : 0,
			CockpitMesh->bHiddenInGame ? 1 : 0,
			*CockpitMesh->Bounds.ToString(),
			CockpitMesh->GetLastRenderTimeOnScreen() > 0.0 ? 1 : 0);
	}
}

// Gleiche Diagnose, aber am Ort, an dem die Taste C wirklich ankommt: die
// Fahrzeugkamera schaltet selbst um, AWiesbadenHelicopter::CycleCameraMode
// wird dabei nie gerufen. Einmal je Moduswechsel genuegt.
void AWiesbadenHelicopter::MeldeKabine()
{
	if (!CockpitMesh || GetCameraMode() == KameraModusMerker)
	{
		return;
	}
	KameraModusMerker = GetCameraMode();
	// Auch der Rumpf mitmelden: im Cockpit-Modus wird er ausgeblendet, und das
	// ist eine eigene Aussage, die ein Bild allein nicht belegt. "Rumpf
	// ausgeblendet" wird sonst nur behauptet.
	const int32 RumpfSichtbar = FuselageMesh && FuselageMesh->IsVisible() ? 1 : 0;
	const int32 RumpfOwnerNoSee = FuselageMesh && FuselageMesh->bOwnerNoSee ? 1 : 0;
	UE_LOG(LogWbVehicles, Log,
		TEXT("Kabine bei Kameramodus %d: Sichtbar=%d OwnerNoSee=%d versteckt=%d")
		TEXT(" Grenzen %s zuletztGezeichnet=%d | Rumpf: Sichtbar=%d OwnerNoSee=%d"),
		static_cast<int32>(KameraModusMerker),
		CockpitMesh->IsVisible() ? 1 : 0,
		CockpitMesh->bOwnerNoSee ? 1 : 0,
		CockpitMesh->bHiddenInGame ? 1 : 0,
		*CockpitMesh->Bounds.ToString(),
		CockpitMesh->GetLastRenderTimeOnScreen() > 0.0 ? 1 : 0,
		RumpfSichtbar, RumpfOwnerNoSee);
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

FWiesbadenHeliMastSample AWiesbadenHelicopter::SampleRotorMast() const
{
	FWiesbadenHeliMastSample Sample;
	Sample.MainRotorRpm = RotorPhysics.MainRotorRpm;

	if (!MainRotorHub || !LowerRotorHub)
	{
		// Wuerfel-Rueckfall ohne Rotor-Naben: nichts zu messen.
		return Sample;
	}

	const FVector ActorUp = GetActorUpVector();
	const FVector ActorLoc = GetActorLocation();
	const FVector MainHub = MainRotorHub->GetComponentLocation();
	const FVector LowerHub = LowerRotorHub->GetComponentLocation();

	// Seitenabstand eines Punktes zu einer Achse (Aufpunkt + Richtung).
	auto LateralCm = [](const FVector& Point, const FVector& AxisPoint, const FVector& AxisDir) -> float
	{
		const FVector Delta = Point - AxisPoint;
		return static_cast<float>((Delta - FVector::DotProduct(Delta, AxisDir) * AxisDir).Size());
	};

	// Winkel zweier Richtungen in Grad (0 = gleichgerichtet, 90 = abgeknickt).
	auto AngleDeg = [](const FVector& A, const FVector& B) -> float
	{
		const double Cos = FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal());
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cos, -1.0, 1.0))));
	};

	Sample.MainHubOffsetCm = LateralCm(MainHub, ActorLoc, ActorUp);
	Sample.LowerHubOffsetCm = LateralCm(LowerHub, ActorLoc, ActorUp);
	Sample.MastTiltDeg = AngleDeg(MainHub - LowerHub, ActorUp);

	Sample.MainAzimuthDeg = FRotator::ClampAxis(MainRotorHub->GetRelativeRotation().Yaw);
	Sample.LowerAzimuthDeg = FRotator::ClampAxis(LowerRotorHub->GetRelativeRotation().Yaw);

	// Die Blaetter haengen in der Nabe; ihr Drehpunkt (Komponenten-Ursprung) muss
	// auf der Nabenachse liegen. Gemessen wird der DREHPUNKT, nicht der
	// Bounding-Box-Mittelpunkt - der wandert bei einem drehenden Blattstern mit
	// der Drehlage um bis zu 0,25 * Radius und wuerde eine Kreisbahn vortaeuschen.
	// Geometrie-Mitte des Blattsterns, im RUMPF-Frame und als MOMENTAUFNAHME.
	//
	// Gemessen wird der WELT-Mittelpunkt der Komponenten-Bounds (die Engine zieht
	// das AABB jeden Bild neu auf): er wandert bei einem drehenden Stern mit der
	// Drehlage um die echte Sternmitte. Der MITTELWERT ueber eine volle Drehung ist
	// darum die Sternmitte - und weil hier der RUMPF-Frame steht (der nicht mit dem
	// Rotor dreht), mittelt der Aufrufer die Momentaufnahmen einfach. Die Aufloesung
	// liegt bei etwa (0,25 * Rotorradius) / Anzahl Bilder, also bei ~5 cm.
	//
	// FALLE: der Anker der MESH-Bounding-Box (Mesh->GetBoundingBox().GetCenter())
	// taugt NICHT. Er ist ein starrer Punkt der Nabe und misst nur die Asymmetrie
	// des Sterns im eigenen AABB - beim Ka-52-Rotor 1,8 m, ohne dass irgendetwas
	// schief sitzt.
	auto BladeCentreInBodyCm = [this](const UStaticMeshComponent* Comp) -> FVector
	{
		if (!Comp)
		{
			return FVector::ZeroVector;
		}
		return GetActorTransform().InverseTransformPosition(Comp->Bounds.Origin);
	};

	if (MainRotorBlade)
	{
		const FVector HubUp = MainRotorHub->GetUpVector();
		Sample.MainBladeOffsetCm = LateralCm(MainRotorBlade->GetComponentLocation(), MainHub, HubUp);
		Sample.MainSpinTiltDeg = AngleDeg(MainRotorBlade->GetUpVector(), HubUp);
		Sample.MainBladeCentreInBodyCm = BladeCentreInBodyCm(MainRotorBlade);
		// Der Component-Ursprung traegt die Achsenkorrektur, nicht den Drehpunkt.
		// Der Drehpunkt des Mesh muss also durch die WELT-Transformation des
		// Components geschickt werden - mit der relativen Drehung allein
		// gemessen oszilliert der Ausdruck, weil die Nabe mitdreht und ihren
		// Versatz mitdreht: es kam 2 x Korrektur heraus (5,2 statt 0,0 cm),
		// weil der Versatz in Weltlage steckt, die Drehung aber nicht.
		Sample.MainAxisResidualCm = LateralCm(
			MainRotorBlade->GetComponentTransform().TransformPosition(
				GetRotorDrehpunktCm(false)),
			MainHub, HubUp);
	}
	if (LowerRotorBlade)
	{
		const FVector HubUp = LowerRotorHub->GetUpVector();
		Sample.LowerBladeOffsetCm = LateralCm(LowerRotorBlade->GetComponentLocation(), LowerHub, HubUp);
		Sample.LowerSpinTiltDeg = AngleDeg(LowerRotorBlade->GetUpVector(), HubUp);
		Sample.LowerBladeCentreInBodyCm = BladeCentreInBodyCm(LowerRotorBlade);
		Sample.LowerAxisResidualCm = LateralCm(
			LowerRotorBlade->GetComponentTransform().TransformPosition(
				GetRotorDrehpunktCm(true)),
			LowerHub, HubUp);
	}

	return Sample;
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
	const APlayerController* PC = GetHeliController();
	const float TargetCollective = ApplyStickShaping(
		WiesbadenInputMap::HelicopterAxis(PC, EWiesbadenHeliAction::Collective),
		StickDeadzone, StickExpo);
	float TargetPitch = ApplyStickShaping(
		WiesbadenInputMap::HelicopterAxis(PC, EWiesbadenHeliAction::Pitch),
		StickDeadzone, StickExpo);
	float TargetRoll = ApplyStickShaping(
		WiesbadenInputMap::HelicopterAxis(PC, EWiesbadenHeliAction::Roll),
		StickDeadzone, StickExpo);
	const float TargetYaw = ApplyStickShaping(
		WiesbadenInputMap::HelicopterAxis(PC, EWiesbadenHeliAction::Yaw),
		StickDeadzone, StickExpo);

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

	// Triebwerk an/aus (G oder X am Gamepad). Y bleibt ausschliesslich
	// dem Aussteigen vorbehalten; LB/RB steuern jetzt die Gierachse.
	const bool bEnginePressed = WiesbadenInputMap::IsHelicopterActionDown(
		GetHeliController(), EWiesbadenHeliAction::Engine);
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
	FRotator Oben;
	FRotator Unten;
	ComputeCoaxialRotorRotation(RotorPhysics.MainRotorRpm, DeltaSeconds, Oben, Unten);

	if (MainRotorHub)
	{
		MainRotorHub->AddLocalRotation(Oben);
	}
	if (LowerRotorHub)
	{
		LowerRotorHub->AddLocalRotation(Unten);
	}
	if (TailRotorHub)
	{
		// Heckrotor: U/min -> deg/s: * 360 / 60 = * 6.
		TailRotorHub->AddLocalRotation(
			FRotator(RotorPhysics.TailRotorRpm * 6.0f * DeltaSeconds, 0.0f, 0.0f));
	}
}

FVector AWiesbadenHelicopter::ComputeRotorMountOffset(
	const FVector& MeshDrehpunktCm, const FRotator& ModelYaw, float HubHeightCm)
{
	// DIE EINE STELLE, an der die Rotoren auf die Rotorstangenachse gelegt
	// werden. Beide Scheiben gehen durch dieselbe Funktion; es gibt keine
	// Sonderbehandlung fuer eine der beiden und keinen zweiten Weg, die
	// Achse zu erreichen.
	//
	// Rechnung: Ein Component bildet seine Weltlage als
	//   Nabe + Versatz + Gier(MeshPunkt) ab
	// Der Versatz muss also gerade das aufheben, was die Gier aus dem
	// Drehpunkt des Mesh macht. Deshalb wird der Drehpunkt mit derselben
	// Gier zurueckgedreht und negiert:
	//   Versatz.xy = -(Gier * Drehpunkt).xy
	// Mit ModelYaw +90 ist das (x, y) -> (-y, x) des Modellraums.
	//
	// Z bleibt der Hub-Hoehe-Versatz: der Mesh-Ursprung liegt auf der
	// Kufenebene, die Geometrie muss am gebackenen Platz bleiben und
	// trotzdem um die Nabe kreisen.
	const FVector Gedreht = ModelYaw.RotateVector(MeshDrehpunktCm);
	return FVector(-Gedreht.X, -Gedreht.Y, -HubHeightCm);
}

FVector AWiesbadenHelicopter::GetRotorDrehpunktCm(bool bUnten)
{
	// DIE ZAHLEN DES MODELLS, an einer Stelle.
	//
	// Sie stehen in GetRotorDrehpunktCm, damit Constructor und
	// Automationstest dieselbe Quelle lesen - zwei Abschriften desselben
	// Messwerts sind zwei Werte, sobald nur einer davon gepflegt wird.
	//
	// Gemessen an der Importquelle, nicht am Asset: das Asset traegt Nanite,
	// seine klassischen LOD-Buffer haben 773 Dreiecke statt 1,9 Millionen,
	// und die Achse dieses Ersatzdatensatzes liegt 3 bis 5 cm daneben
	// (gemessen im Test, Saved/Logs/wb_test_ka52.log). Wer den Wert vom
	// Asset nimmt, korrigiert gegen einen Fehler.
	//
	// Beide Scheiben liegen nicht auf (0, 0): die untere 5,4 cm in X. Ohne
	// Korrektur dreht sie sichtbar um etwas anderes als die obere - daher
	// ComputeRotorMountOffset, und deshalb diese Zahlen.
	return bUnten ? FVector(4.92f, -2.06f, 0.0f) : FVector(-0.19f, 2.59f, 0.0f);
}

void AWiesbadenHelicopter::ComputeCoaxialRotorRotation(
	float MainRotorRpm, float DeltaSeconds, FRotator& OutUpper, FRotator& OutLower)
{
	// Der Ka-52 hat EINEN Mast mit zwei gegenlaeufigen Rotoren. Gefordert
	// ist nicht "zwei Rotoren", sondern "synchron schnell und gegeneinander":
	// gleicher Betrag, entgegengesetztes Vorzeichen, gleiche Achse. Beide
	// Werte entstehen in DIESER Rechnung, damit sie nicht auseinanderlaufen
	// koennen - zwei unabhaengige Ausdruecke wuerden irgendwann getrennt
	// gepflegt.
	//
	// U/min -> deg/s: * 360 / 60 = * 6.
	const float MainDegPerSec = FMath::Max(MainRotorRpm, 0.0f) * 6.0f;
	const float Schritt = MainDegPerSec * DeltaSeconds;
	OutUpper = FRotator(0.0f, Schritt, 0.0f);
	OutLower = FRotator(0.0f, -Schritt, 0.0f);
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
	// abgestellten Heli beurteilen lassen (im echten Spiel nie gesetzt). Ohne Wert
	// = 450 U/min (volle Blur-Scheibe); mit Wert (-WbHeliSpin=220) eine feste
	// Drehzahl, um den Uebergang Blaetter -> Scheibe zu pruefen.
	static float SpinDemoRpm = 0.0f;
	static bool bSpinDemoInit = false;
	if (!bSpinDemoInit)
	{
		bSpinDemoInit = true;
		if (!FParse::Value(FCommandLine::Get(), TEXT("WbHeliSpin="), SpinDemoRpm)
			&& FParse::Param(FCommandLine::Get(), TEXT("WbHeliSpin")))
		{
			SpinDemoRpm = 450.0f;
		}
	}
	const bool bSpinDemo = SpinDemoRpm > 0.0f;

	// --- Rotor-Blur: Scheiben blenden mit der Drehzahl ein ---
	const float Rpm = bSpinDemo ? SpinDemoRpm : RotorPhysics.MainRotorRpm;
	const bool bEngineForVfx = bSpinDemo ? true : bEngineRunning;
	// Unter ~120 U/min sieht man die Blaetter, ab ~360 die volle Scheibe.
	const float BlurAlpha = FMath::Clamp((Rpm - 120.0f) / 240.0f, 0.0f, 1.0f);
	if (RotorBlurMID)
	{
		RotorBlurMID->SetScalarParameterValue(TEXT("Opacity"), BlurAlpha * 0.70f);
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

// ===========================================================================
// Geraet, Schaden, Absturz und Wiederaufsetzen
// ===========================================================================

/**
 * Konsolenschalter wb.HeliFeuer: 1 = Abzug halten, 0 = loslassen.
 *
 * Der haelt den Abzug dauerhaft - im Gegensatz zu einer synthetischen
 * Maustaste, die das Spiel nicht annimmt (am 26.09.2026 vier Wege gefahren,
 * null Abzugsflanken im Log). Ohne diesen Schalter war das Muendungsfeuer
 * nicht als Bild zu belegen. Die CVar gehoert dem PlayerController (dort
 * liegt der Befehl WbHeliFeuer), deshalb wird sie namentlich geholt.
 */
static bool IsFireSwitchHeld()
{
	const IConsoleVariable* Feuer =
		IConsoleManager::Get().FindConsoleVariable(TEXT("wb.HeliFeuer"));
	return Feuer != nullptr && Feuer->GetInt() == 1;
}

void AWiesbadenHelicopter::ReadDeviceInput(float DeltaSeconds)
{
	(void)DeltaSeconds;

	if (bDestroyed || !LightRig || !Gun)
	{
		return;
	}

	// -- Bordgeschuetz -------------------------------------------------------
	// Linke Maustaste oder A am Gamepad; RT bleibt ausschliesslich dem Kollektiv.
	// Im gefuehrten Unterricht verhindert der Trockenmodus jeden Schuss.
	const bool bFeuern = WiesbadenInputMap::IsHelicopterActionDown(
		GetHeliController(), EWiesbadenHeliAction::Fire) || IsFireSwitchHeld();
	MeldeKabine();

	if (bFeuern != bTriggerHeld)
	{
		// Flanke loggen. Das Geschuetz selbst schweigt im Normalfall
		// komplett, und genau daran ist am 26.09.2026 ein Bildbeleg
		// gescheitert: 39 s LMB gehalten, kein Schuss, kein Logzeichen -
		// unabhaengig davon, ob der Abzug das Spiel ueberhaupt erreicht.
		UE_LOG(LogWbVehicles, Log, TEXT("Ka52-Abzug: %s"),
			bFeuern ? TEXT("gedrueckt") : TEXT("losgelassen"));
	}
	// Bleibt der Abzug beim Unterrichtsende gehalten, muss er erst losgelassen
	// werden, bevor scharfe Schuesse wieder moeglich sind.
	if (!bFeuern)
	{
		bDryFireReleasePending = false;
	}
	else if (bLessonDryFireActive)
	{
		bDryFireReleasePending = true;
	}
	const bool bAllowLiveFire = AllowsLiveFire(bLessonDryFireActive, bDryFireReleasePending);
	Gun->SetTriggerHeld(bFeuern && bAllowLiveFire);
	bTriggerHeld = bFeuern;

	// -- Suchscheinwerfer ----------------------------------------------------
	// L oder Gamepad D-Pad hoch. Flanke, nicht Pegel: ein gehaltener Schalter
	// wuerde im Frame mehrfach umschalten.
	const bool bLicht = WiesbadenInputMap::IsHelicopterActionDown(
		GetHeliController(), EWiesbadenHeliAction::Searchlight);
	if (bLicht && !bSearchlightToggleHeld)
	{
		SetSearchlights(!LightRig->AreSearchlightsOn());
	}
	bSearchlightToggleHeld = bLicht;

	// -- Landlicht -----------------------------------------------------------
	const bool bLande = WiesbadenInputMap::IsHelicopterActionDown(
		GetHeliController(), EWiesbadenHeliAction::LandingLight);
	if (bLande && !bLandingLightToggleHeld)
	{
		SetLandingLight(!LightRig->IsLandingLightOn());
	}
	bLandingLightToggleHeld = bLande;

	// -- Zielen -------------------------------------------------------------
	// Das Geschuetz folgt dem Blick, nicht einer Taste. Aim(0,0) stellte den
	// Turm in Ruhelage nach vorn: man konnte den Horizont drehen und der
	// Schuss ging trotzdem immer geradeaus - auf Zieldistanzen von mehreren
	// Kilometern ist das der Unterschied zwischen Treffer und Kartoffeln.
	//
	// Den Nachlauf macht der Turm selbst (TurmFolgt = 3,2/s); hier wird nur
	// das Ziel gestellt, nicht die Gier geschrieben.
	FRotator Blick = GetControlRotation();
	if (FMath::Abs(Blick.Yaw) > 89.0f)
	{
		// Fast senkrechter Blick: die Steuerrotation kippt dabei in eine
		// sinnlose Gier, die dem Geschuetz eine 90-Grad-Schwenkung aufzwingt.
		Blick.Yaw = GetActorRotation().Yaw;
	}
	Gun->AimAt(GetActorLocation() + Blick.Vector() * ZielDistanzCm);

	// Der Suchscheinwerfer zeigt auf dieselbe Stelle. Bei Nacht ist er das
	// einzige, was einem sagt, wohin der Schuss geht - er sitzt darum am
	// Zielpunkt des Geschuetzes und nicht an einer eigenen Taste.
	LightRig->SetSearchlightTarget(
		Gun->GetMuzzleLocation() + Gun->GetAimRotation().Vector() * LichtDistanzCm);
}

void AWiesbadenHelicopter::SetSearchlights(bool bOn)
{
	if (LightRig)
	{
		LightRig->SetSearchlights(bOn);
	}
}

void AWiesbadenHelicopter::SetLandingLight(bool bOn)
{
	if (LightRig)
	{
		LightRig->SetLandingLight(bOn);
	}
}

float AWiesbadenHelicopter::GetHealthFraction() const
{
	return MaxHealth > 0.0f ? FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f) : 0.0f;
}

float AWiesbadenHelicopter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	(void)DamageEvent;
	(void)EventInstigator;
	(void)DamageCauser;

	if (bDestroyed || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	// Nicht unter Null. TakeDamage liefert den tatsaechlich angerichteten
	// Schaden zurueck - der Rueckgabewert ist damit die ehrliche Groesse und
	// kein zweiter Ort, an dem der Zustand steht.
	const float Vorher = Health;
	Health = FMath::Clamp(Health - DamageAmount, 0.0f, MaxHealth);
	const float Angewendet = Vorher - Health;

	UE_LOG(LogWbVehicles, Log, TEXT("Ka52 getroffen: %.0f Schaden, noch %.0f von %.0f."),
		Angewendet, Health, MaxHealth);

	if (Health <= 0.0f)
	{
		bDestroyed = true;
		bEngineRunning = false;
		RespawnCountdown = FMath::Max(RespawnDelay, KINDA_SMALL_NUMBER);
		// Der Rumpf faellt zur getroffenen Seite, nicht in eine Zufalls-
		// richtung: der Taumel ist die Story des Absturzes.
		CrashYawRate = FMath::DegreesToRadians(CrashTumbleDegPerSec);
		CrashRollRate = FMath::DegreesToRadians(CrashTumbleDegPerSec * 0.6f);
		if (LightRig)
		{
			LightRig->SetAllLightsEnabled(false);
		}
		ReleaseFireForRespawn();
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Ka52 zerstoert - Wiederaufsetzen auf dem Landeplatz in %.0f s."),
			RespawnDelay);
	}
	return Angewendet;
}

void AWiesbadenHelicopter::UpdateCrash(float DeltaSeconds)
{
	// Triebwerk aus, Rotoren stehen, Steuerung weg - alles Weitere ist
	// Schauwerk. Der Rumpf sinkt und taumelt, damit man den Absturz von
	// aussen sieht, statt dass die Maschine einfach in der Luft stehen
	// bleibt (dieselbe Fehlerklasse wie bei ParkOnGround).
	Velocity = FVector(0.0f, 0.0f, -CrashSinkCmPerSec);
	AngularVelocity = FVector(0.0f, CrashRollRate, CrashYawRate);
	AddActorWorldOffset(Velocity * DeltaSeconds, false);
	AddActorWorldRotation(
		FRotator(0.0f, AngularVelocity.Z, AngularVelocity.Y)
		* FMath::RadiansToDegrees(DeltaSeconds), false);

	// Auf dem Boden endet das Taumeln: der Rumpf bleibt liegen, bis der
	// Respawn ihn holt.
	FHitResult Boden;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbHeliAbsturz), false, this);
	const FVector Von = GetActorLocation();
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(
		Boden, Von, Von - FVector(0.0f, 0.0f, 4000.0f), ECC_WorldStatic, Params)
		&& Boden.bBlockingHit)
	{
		const float Abstand = FVector::Dist(Von, Boden.ImpactPoint);
		if (Abstand < 260.0f)
		{
			// Knapp ueber der Aufsetzflaeche halten: der Ursprung des
			// Ka-52 liegt in der Rumpfmitte, nicht an der Kufe.
			const FVector Aufsetzpunkt = Boden.ImpactPoint + FVector(0.0f, 0.0f, 60.0f);
			SetActorLocation(Aufsetzpunkt, false, nullptr, ETeleportType::TeleportPhysics);
			AngularVelocity = FVector::ZeroVector;
			Velocity = FVector::ZeroVector;
		}
	}

	if (RespawnCountdown > 0.0f)
	{
		RespawnCountdown -= DeltaSeconds;
		if (RespawnCountdown <= 0.0f)
		{
			RespawnCountdown = -1.0f;
			RespawnOnTowerHelipad();
		}
	}
}

bool AWiesbadenHelicopter::RespawnOnTowerHelipad()
{
	if (!PlaceOnTowerHelipad())
	{
		return false;
	}

	bDestroyed = false;
	Health = MaxHealth;
	RespawnCountdown = -1.0f;
	CrashYawRate = 0.0f;
	CrashRollRate = 0.0f;
	Velocity = FVector::ZeroVector;
	AngularVelocity = FVector::ZeroVector;
	CollectiveInput = 0.0f;
	CyclicPitchInput = 0.0f;
	CyclicRollInput = 0.0f;
	YawInput = 0.0f;
	bGrounded = true;
	bGroundCacheValid = false;
	bEngineRunning = true;

	if (LightRig)
	{
		LightRig->SetAllLightsEnabled(true);
		LightRig->SetSearchlights(false);
		LightRig->SetLandingLight(false);
	}
	if (Gun)
	{
		Gun->Reload();
	}
	// Nach Wiederaufsetzen erst eine echte Loslass-Eingabe abwarten.
	// Ein bestehender Lektions-Trockenmodus bleibt erhalten.
	ReleaseFireForRespawn();

	UE_LOG(LogWbVehicles, Log,
		TEXT("Ka52 wiederaufgesetzt auf dem Landeplatz (%.0f, %.0f, %.0f)."),
		GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
	return true;
}

bool AWiesbadenHelicopter::PlaceOnTowerHelipad()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Der Landeplatz gehoert zum Sebbotower, und seine Weltlage rechnet der
	// Tower selbst (AWiesbadenSebboHq::GetHelipadWorldLocation) - der Heli
	// fragt ihn nur. Eine eigene Rechnung (Turmfuss + Versatz) waere die
	// zweite Wahrheit, und die beiden wuerden auseinanderlaufen. Aus genau
	// diesem Grund darf der Hubschrauber auch NICHT GetActorLocation() des
	// Towers fragen: der Actor steht im Ursprung, seine Bauteile tragen die
	// Weltlage.
	AWiesbadenSebboHq* Turm = nullptr;
	for (TActorIterator<AWiesbadenSebboHq> It(World); It; ++It)
	{
		Turm = *It;
		break;
	}
	if (!Turm)
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Ka52-Reset: kein Sebbotower in der Ebene - bleibe, wo ich bin."));
		return false;
	}

	// 120 cm ueber der Aufsetzflaeche: die Kufen stehen nicht auf dem
	// Hubschrauberursprung, und 0 cm hiesse "im Dach".
	const FVector Platz = Turm->GetHelipadWorldLocation() + FVector(0.0f, 0.0f, 120.0f);

	// Mit dem Heck zum Ankunftsweg: die Nase zeigt damit vom Turm weg, und
	// der Hubschrauber kann nach dem Aufsetzen direkt ausrollen.
	const FRotator Lage(0.0f, Turm->GetActorRotation().Yaw, 0.0f);
	SetActorLocationAndRotation(Platz, Lage, false, nullptr, ETeleportType::TeleportPhysics);
	bGroundCacheValid = false;
	return true;
}

bool AWiesbadenHelicopter::AimAtWorldTarget(float Xcm, float Ycm,
	float HoeheUeberBodenCm, float DistanzMeter)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Bodenspur am Ziel: HoeheUeberBodenCm bezieht sich auf den Boden, nicht
	// auf Z=0. Wiesbaden liegt auf Huegeln - ein festes Z landet je nach
	// Stadtgegend im Erdreich oder in der Luft.
	float BodenZ = 0.0f;
	FHitResult Boden;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbHeliZiel), false, this);
	if (World->LineTraceSingleByChannel(Boden,
		FVector(Xcm, Ycm, 200000.0f), FVector(Xcm, Ycm, -50000.0f),
		ECC_WorldStatic, Params))
	{
		BodenZ = Boden.ImpactPoint.Z;
	}

	const FVector Ziel(Xcm, Ycm, BodenZ + HoeheUeberBodenCm);

	// Schwebeposition SuedLICH des Ziels: +Y ist in Unreal suedlich (die Achse
	// ist linkshaendig, siehe UGeoCoordinateConverter). Dadurch zeigt die Nase
	// nach Norden - auf das Ziel zu, und die Kamera dahinter sieht die Muendung
	// vor dem Bauwerk, nicht den Rumpf davor.
	const float Abstand = FMath::Max(500.0f, DistanzMeter * 100.0f);
	const FVector Platz(Xcm, Ycm + Abstand, BodenZ + HoeheUeberBodenCm + 600.0f);
	const FRotator Blick = (Ziel - Platz).Rotation();

	SetActorLocationAndRotation(Platz,
		FRotator(0.0f, Blick.Yaw, 0.0f), false, nullptr,
		ETeleportType::TeleportPhysics);
	bGroundCacheValid = false;
	bGrounded = false;
	bEngineRunning = true;
	Velocity = FVector::ZeroVector;
	AngularVelocity = FVector::ZeroVector;

	// Das Geschuetz folgt der Control-Rotation, nicht der Actorlage - ohne
	// diese Zeile zielt die Kanone weiter geradeaus, waehrend der Rumpf auf
	// das Ziel zeigt (am 26.09.2026 genau so beobachtet).
	// Nicht "Controller" nennen: der Klassenname hat ein solches Member, und
	// C4458 (Verdeckung) ist in diesem Projekt ein Fehler.
	if (AController* Steuermann = GetController())
	{
		Steuermann->SetControlRotation(Blick);
	}

	UE_LOG(LogWbVehicles, Log,
		TEXT("Ka52-Ziel: schwebe bei (%.0f, %.0f, %.0f), peile (%.0f, %.0f, %.0f) an, Boden %.0f cm."),
		Platz.X, Platz.Y, Platz.Z, Ziel.X, Ziel.Y, Ziel.Z, BodenZ);
	return true;
}
