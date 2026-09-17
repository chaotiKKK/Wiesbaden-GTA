// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenNerobergbahn.h"
#include "World/WiesbadenRailTransport.h"

#include "WiesbadenReal.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
#include "UI/WiesbadenVehicleHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/WiesbadenFootPawn.h"
#include "World/WiesbadenCityChunk.h"
#include "EngineUtils.h"

namespace
{
	// Streckenpunkte aus OpenStreetMap (way 39223618/39223619 und Nachbarn),
	// Tal -> Berg, bereits entlang der Fahrtrichtung sortiert. Zwei Gleise,
	// wie beim Vorbild rund 4 m auseinander.
	struct FBahnLatLon { double Lat; double Lon; };

	const FBahnLatLon TRACK_A[] = {
		{ 50.0947441, 8.2254544 },
		{ 50.0948482, 8.2255669 },
		{ 50.0951863, 8.2259325 },
		{ 50.0953680, 8.2261290 },
		{ 50.0954625, 8.2262312 },
		{ 50.0956604, 8.2264462 },
		{ 50.0957747, 8.2265789 },
		{ 50.0959056, 8.2267354 },
		{ 50.0961730, 8.2270651 },
		{ 50.0961917, 8.2270861 },
		{ 50.0962390, 8.2271229 },
		{ 50.0962554, 8.2271403 },
		{ 50.0963741, 8.2272873 },
		{ 50.0963885, 8.2273097 },
		{ 50.0964240, 8.2273721 },
		{ 50.0964364, 8.2273898 },
		{ 50.0977211, 8.2289736 },
		{ 50.0977483, 8.2290070 },
		{ 50.0978479, 8.2291301 },
	};

	const FBahnLatLon TRACK_B[] = {
		{ 50.0947712, 8.2254836 },
		{ 50.0948430, 8.2255784 },
		{ 50.0951811, 8.2259440 },
		{ 50.0953629, 8.2261405 },
		{ 50.0954573, 8.2262427 },
		{ 50.0956552, 8.2264576 },
		{ 50.0957693, 8.2265901 },
		{ 50.0959000, 8.2267465 },
		{ 50.0961673, 8.2270759 },
		{ 50.0961837, 8.2270988 },
		{ 50.0962179, 8.2271632 },
		{ 50.0962333, 8.2271854 },
		{ 50.0963519, 8.2273340 },
		{ 50.0963685, 8.2273506 },
		{ 50.0964173, 8.2273878 },
		{ 50.0964310, 8.2274010 },
		{ 50.0977159, 8.2289849 },
		{ 50.0977426, 8.2290179 },
		{ 50.0978423, 8.2291410 },
	};

	// Hoehenunterschied des Vorbilds - Rueckfallrampe, bis das Gelaende
	// gestreamt ist.
	constexpr double ClimbCm = 8300.0;
	constexpr double RailClearanceCm = 35.0;
	// Ab hier (Bogenlaenge, cm) liegt die Trasse aufgeschuettet; die Krone der
	// Damm-/Viaduktmaeuer liegt dort 6 cm hoeher. Betrifft nur noch die
	// Erdbauwerke - das Gleis selbst folgt dem Trassenpunkt plus RailTopCm.
	constexpr double TrackBedStartCm = 850.0;
	constexpr double MaxRailGrade = 0.30;

	// Hebung des Wagen-Ursprungs ueber den Gleispunkt. Der Ursprung von
	// SM_WbNbWagen liegt auf der Schienenkontaktlinie in Wagenmitte, der
	// Unterrahmen reicht als Keil nach unten - ein kleiner Wert setzt ihn
	// buendig auf die Schiene.
	constexpr float CarFloorCm = 12.0f;

	// Schienen-OBERKANTE ueber dem Trassenpunkt - bewusst derselbe Wert wie
	// CarFloorCm: die Gleisbauteile sind so modelliert, dass ihre Z=0-Ebene der
	// Schienenoberkante entspricht (Tools/Blender/make_nerobergbahn.py), also
	// laufen die Raeder genau auf der Schiene. Der fruehrere Bettlift von 6 cm
	// ab 8,5 m ist entfallen: breite Farbbaender verzeihen einen Hoehensprung,
	// ein Schienenstab mit 14 cm Profil nicht.
	constexpr float RailTopCm = CarFloorCm;

	// Laengen der Gleis-Wiederholteile aus Blender (cm).
	constexpr double SchienenstabCm = 600.0;   // SM_WbNbSchiene
	constexpr double TeilStabCm = 200.0;       // Zahnstange, Seilkanal, Bett
	constexpr double SchwellenAbstandCm = 65.0;
	constexpr double SchwellenLaengeCm = 24.0; // Stabmass der Schwelle in Fahrt

	// Halbe Spurweite: die Schienen liegen 50 cm neben der jeweiligen Wagenmitte
	// (1000 mm Spur, wie Wagen und Radsaetze).
	constexpr double SpurHalfCm = 50.0;

	// Ab welchem Abstand der beiden INNEREN Schienen nicht mehr von einer
	// gemeinsamen Mittelschiene die Rede sein kann. Darunter wird nur EINE
	// Schiene bei 0 gelegt (Vorbild: drei Laufschienen).
	constexpr double MittelschienenToleranzCm = 15.0;

	// Streuung der OSM-Knoten um die echte Spurweite: bis hierher wird der
	// gemessene Gleisabstand als "eine Spurweite" gelesen und darauf gerundet.
	constexpr double SpurtoleranzCm = 45.0;
}

AWiesbadenNerobergbahn::AWiesbadenNerobergbahn()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	TrackMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TrackMesh"));
	TrackMesh->SetupAttachment(Root);
	// Keine Kollision: das Bett folgt dem Gelaende, das bereits Kollision
	// hat, und UProceduralMeshComponent kocht seine Koerper SYNCHRON - fuer
	// ein reines Schmuckband waere das bezahlte Wartezeit.
	TrackMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Material der Erdbauwerke (Damm, Viaduktwaende, Gelaender).
	//
	// Die Abschnitte unterscheiden sich NUR ueber Vertexfarben:
	// Damm-Spandrel hell, Gelaender Metall, Bogenring Klinker. Hier stand
	// lange BasicShapeMaterial - und der Kommentar daneben behauptete, es zeige
	// die Vertexfarben. Das tut es nicht. Die Trasse war genau das graue
	// Band den Berg hinauf, vor dem der Kommentar warnte.
	//
	// Das Gleis selbst sind keine Vertexfarben-Baender mehr, sondern die fuenf
	// Instanzbauteile weiter unten; fuer die setzt BuildTrackMeshes() die
	// Materialien aus den importierten Meshes.
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> TrackMat(
			TEXT("/Game/Materials/City/M_WbVertexFarbe.M_WbVertexFarbe"));
		if (TrackMat.Succeeded())
		{
			// Vorbelegung; die Sektionen bekommen ihre Materialien nach dem Bau.
			for (int32 Section = 0; Section < 4; ++Section)
			{
				TrackMesh->SetMaterial(Section, TrackMat.Object);
			}
		}
	}

	CarA = BuildCar(TEXT("WagenA"), 0);
	CarB = BuildCar(TEXT("WagenB"), 1);

	// Bauwerke: modelliert in Blender (Tools/Blender/make_nerobergbahn.py),
	// importiert von Tools/import_nerobergbahn.py. Anfangs versteckt - erst
	// PlaceStructures() setzt sie auf die aufgeloeste Trasse.
	auto MakeStructure = [&](const TCHAR* Name, const TCHAR* MeshPath) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Comp = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Comp->SetupAttachment(Root);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetHiddenInGame(true);
		ConstructorHelpers::FObjectFinder<UStaticMesh> Finder(MeshPath);
		if (Finder.Succeeded())
		{
			Comp->SetStaticMesh(Finder.Object);
		}
		return Comp;
	};

	// Bahnsteighalle: EIN Mesh fuer beide Stationen - das Vorbild hat denselben
	// Bautyp an beiden Enden (NACHBAU-REFERENZ §7.1). Der Ursprung des Meshes
	// liegt auf der Schienenoberkante in Trassenmitte, der Actor setzt ihn
	// deshalb ohne Hoehenzuschlag (siehe PlaceStructures).
	Talstation = MakeStructure(TEXT("Talstation"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbBahnsteighalle.SM_WbNbBahnsteighalle"));
	Bergstation = MakeStructure(TEXT("Bergstation"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbBahnsteighalle.SM_WbNbBahnsteighalle"));
	Viadukt = MakeStructure(TEXT("Viadukt"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbViadukt.SM_WbNbViadukt"));

	// Gleisbauteile: fuenf Wiederholteile aus demselben Blender-Skript, die
	// BuildTrackInstances() beim Aufloesen der Hoehen als Instanzen legt.
	// Der Ursprung jedes Teils liegt auf der Schienenoberkante in
	// Trassenmitte - der Platzer muss sie also nur heben und quer verschieben.
	auto MakeTrackPart = [&](const TCHAR* Name, const TCHAR* MeshPath)
		-> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* Comp =
			CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Comp->SetupAttachment(Root);
		// Keine Kollision: die Trasse folgt dem Gelaende, das bereits Kollision
		// hat. Ein Instanzkoerper ueber 400 m Strecke waere reine Kochzeit.
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ConstructorHelpers::FObjectFinder<UStaticMesh> Finder(MeshPath);
		if (Finder.Succeeded())
		{
			Comp->SetStaticMesh(Finder.Object);
		}
		return Comp;
	};

	GleisSchienen = MakeTrackPart(TEXT("GleisSchienen"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbSchiene.SM_WbNbSchiene"));
	GleisZahnstangen = MakeTrackPart(TEXT("GleisZahnstangen"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbZahnstange.SM_WbNbZahnstange"));
	GleisSeilkanal = MakeTrackPart(TEXT("GleisSeilkanal"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbSeilkanal.SM_WbNbSeilkanal"));
	GleisSchwellen = MakeTrackPart(TEXT("GleisSchwellen"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbSchwelle.SM_WbNbSchwelle"));
	GleisBett = MakeTrackPart(TEXT("GleisBett"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbSchotterbett.SM_WbNbSchotterbett"));
}

namespace
{
	// -----------------------------------------------------------------------
	// Einbauwerte der beweglichen Teile. Sie MUESSEN mit den Ankern in
	// Tools/Blender/make_nerobergbahn.py uebereinstimmen (GRADE, TACHO_MITTE,
	// SCHWIMMER_UNTEN/OBEN, KURBEL_ACHSE, KURBEL_LAENGE); der Pruefer
	// check_nerobergbahn_wagen.py prueft die Mesh-Seite, hier stehen dieselben
	// Zahlen in cm.
	// -----------------------------------------------------------------------
	constexpr float CarTiltGrade = 0.195f;            // = GRADE
	constexpr float CarTachoMaxKmh = 10.0f;           // Skalenende der Scheibe
	// Bildwinkel = Winkel auf der Scheibe, gemessen von Bildrechts nach
	// Bildoben. Der gruene Sektor der Textur reicht bis 7,3 km/h, darueber rot.
	constexpr float CarZeigerWinkelNull = 225.0f;     // Skalenmarke 0
	constexpr float CarZeigerWinkelVoll = -45.0f;     // Skalenmarke 10
	const FVector CarTachoAnchor(170.0f, 6.0f, 162.0f);
	const FVector CarSchwimmerUnten(175.1f, 68.0f, 140.0f);
	const FVector CarSchwimmerOben(175.1f, 68.0f, 190.0f);
	const FVector CarKurbelachse(172.0f, -63.0f, 122.0f);
	constexpr float CarKurbelWinkelOffen = -26.0f;    // zum Bediener gezogen
	constexpr float CarKurbelWinkelZu = 2.0f;         // senkrecht

	/**
	 * Neigung des Wagen-Meshes als Quaternion. build_wagen() kippt das Mesh mit
	 * tilt_grade() um die Y-Achse (x' = x*ca - z*sa, z' = x*sa + z*ca); der
	 * Actor giert nur. Kinder des Wagen-Meshes leben deshalb im UNGEKIPPTEN
	 * Bau-System und muessen mit dieser Drehung gesetzt werden.
	 */
	FQuat CarTiltQuat()
	{
		return FQuat(FVector::YAxisVector, -FMath::Atan(CarTiltGrade));
	}

}

USceneComponent* AWiesbadenNerobergbahn::BuildCar(const TCHAR* Name, int32 Index)
{
	// Ein StaticMesh statt des frueheren Wuerfelstapels: der Wagen ist jetzt
	// das in Blender modellierte, texturierte AAA-Modell (Stufenwagen, blau/
	// Narzissengelb, cremefarbenes Dach). Materialien liegen im Mesh.
	UStaticMeshComponent* Car = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Car->SetupAttachment(Root);
	// Kein Kollisionskoerper: der Fahrgast wird ANGEHAENGT, nicht physikalisch
	// mitgeschoben - ein kollidierender Wagen wuerde den einsteigenden Spieler
	// wegdruecken.
	Car->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> WagenMesh(
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbWagen.SM_WbNbWagen"));
	if (WagenMesh.Succeeded())
	{
		Car->SetStaticMesh(WagenMesh.Object);
	}

	// Die beweglichen Teile des Fuehrerstands haengen IM Wagen. Sie liegen im
	// Bau-System des Meshes (siehe CarTiltQuat) und werden in Tick gesetzt -
	// hier nur angebunden und auf ihre Ruhelage gestellt.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ZeigerMesh(
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbTachoZeiger.SM_WbNbTachoZeiger"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SchwimmerMesh(
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbSchwimmer.SM_WbNbSchwimmer"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> KurbelMesh(
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbKurbel.SM_WbNbKurbel"));

	auto MakePart = [&](const TCHAR* Suffix, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(
			*(FString(Name) + Suffix));
		Part->SetupAttachment(Car);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Mesh)
		{
			Part->SetStaticMesh(Mesh);
		}
		return Part;
	};

	FWbCarDetail& Detail = CarDetail[Index];
	Detail.Zeiger = MakePart(TEXT("_Zeiger"),
		ZeigerMesh.Succeeded() ? ZeigerMesh.Object : nullptr);
	Detail.Schwimmer = MakePart(TEXT("_Schwimmer"),
		SchwimmerMesh.Succeeded() ? SchwimmerMesh.Object : nullptr);
	Detail.Kurbel = MakePart(TEXT("_Kurbel"),
		KurbelMesh.Succeeded() ? KurbelMesh.Object : nullptr);
	return Car;
}

void AWiesbadenNerobergbahn::BeginPlay()
{
	Super::BeginPlay();

	// Entwicklungshilfe -WbMitfahr=<Sekunden> (siehe Tick).
	if (!FParse::Value(FCommandLine::Get(), TEXT("WbMitfahr="), DevRideAfterSeconds))
	{
		DevRideAfterSeconds = -1.0f;
	}

	// Entwicklungshilfe -WbKurbel=<Sekunden> (siehe Tick).
	if (!FParse::Value(FCommandLine::Get(), TEXT("WbKurbel="), DevCrankAfterSeconds))
	{
		DevCrankAfterSeconds = -1.0f;
	}

	// Entwicklungshilfe -WbWagenlog=<Sekunden>: Zustand der beweglichen Teile
	// im angegebenen Abstand in das Log (Nadel, Schwimmer, Kurbel).
	if (!FParse::Value(FCommandLine::Get(), TEXT("WbWagenlog="), DevCarLogInterval))
	{
		DevCarLogInterval = 0.0f;
	}
	NextDevCarLogTime = 0.0f;

	// Entwicklungshilfe -WbBallast=<0..1>: Anfangs-Fuellstand beider Wagen.
	// Ohne sie startet der Wagen mit 0,75 (dreiviertel voll).
	float DevBallast = 0.75f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WbBallast="), DevBallast))
	{
		DevBallast = FMath::Clamp(DevBallast, 0.0f, 1.0f);
		CarDetail[0].Fuellstand = DevBallast;
		CarDetail[1].Fuellstand = DevBallast;
	}

	BuildTracks();
	BuildTrackMeshes();

	UE_LOG(LogWbStreaming, Log,
		TEXT("Nerobergbahn: Gleis A %.0f m, Gleis B %.0f m, Talstation bei (%.0f, %.0f)."),
		TrackA.TotalLength / 100.0, TrackB.TotalLength / 100.0,
		TrackA.Points.Num() ? TrackA.Points[0].Position.X : 0.0,
		TrackA.Points.Num() ? TrackA.Points[0].Position.Y : 0.0);
}

void AWiesbadenNerobergbahn::BuildTracks()
{
	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>(this);
	Converter->InitializeWithWiesbadenOrigin();

	auto Build = [&](const FBahnLatLon* Data, int32 Count, FTrack& Out)
	{
		Out.Points.Reset();
		FVector Previous = FVector::ZeroVector;
		double Arc = 0.0;
		for (int32 i = 0; i < Count; ++i)
		{
			FGeoCoordinate Coord;
			Coord.Latitude = Data[i].Lat;
			Coord.Longitude = Data[i].Lon;
			Coord.Height = 0.0;
			FVector World = Converter->GeoToUnrealGround(Coord);

			// Doppelpunkte an den Stationskehlen ausduennen - die OSM-Stummel
			// liegen teils unter einem Meter auseinander.
			if (Out.Points.Num() > 0 && FVector::Dist2D(World, Previous) < 300.0)
			{
				continue;
			}

			FTrackPoint Point;
			Point.Position = World;
			if (Out.Points.Num() > 0)
			{
				Arc += FVector::Dist2D(World, Previous);
			}
			Point.ArcLength = Arc;
			Out.Points.Add(Point);
			Previous = World;
		}
		Out.TotalLength = Arc;

		// Rueckfallprofil: bis zur Gelaendeabtastung eine Vorbildrampe.
		for (FTrackPoint& Point : Out.Points)
		{
			Point.Position.Z = Out.TotalLength > 0.0
				? (Point.ArcLength / Out.TotalLength) * ClimbCm
				: 0.0;
		}
	};

	Build(TRACK_A, UE_ARRAY_COUNT(TRACK_A), TrackA);
	Build(TRACK_B, UE_ARRAY_COUNT(TRACK_B), TrackB);
}

bool AWiesbadenNerobergbahn::ResolveHeights()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	bool bResolvedAny = false;
	bool bAllResolved = true;

	// Stadt-Chunks vom Hoehen-Trace AUSNEHMEN. Der Trace sucht das GELAENDE unter
	// dem Gleis; trifft er zuerst ein Gebaeude- oder Strassen-Chunk (die liegen
	// am Hang mit), setzt er die Schiene auf ein Dach - die Bahn "schwebt" dann
	// ueberm Berg und das Viadukt darauf mit. Die Landscape bleibt trefbar.
	TArray<AActor*> ChunkActors;
	for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
	{
		ChunkActors.Add(*It);
	}

	for (FTrack* Track : { &TrackA, &TrackB })
	{
		for (FTrackPoint& Point : Track->Points)
		{
			if (Point.bHeightResolved)
			{
				continue;
			}

			// Von WEIT oben nach unten.
			//
			// Hier standen einmal 20.000 cm (200 m). Das ist zu wenig: auf
			// hohem Gelaende beginnt der Trace dann UNTERHALB der Oberflaeche,
			// meldet die Startflaeche nicht, faellt durch und trifft etwas
			// Tieferes - die Trasse haenge dort im Hang. 100.000 cm (1.000 m)
			// liegen deutlich ueber der Hohen Wurzel (618 m), dem hoechsten
			// Punkt im Stadtgebiet, und damit ueber jedem moeglichen Start.
			//
			// Zur Groessenordnung: der In-Game-Hoehendatensatz der Bahn misst
			// rund 88 bis 173 m - etwa 70 m unter den echten 160 bis 243 m NN,
			// aber bei gleicher Steigung von rund 83 m (siehe die Plausibili-
			// taetspruefung weiter unten).
			constexpr double TraceTopCm = 100000.0;
			constexpr double TraceBottomCm = -20000.0;

			const FVector Start(Point.Position.X, Point.Position.Y, TraceTopCm);
			const FVector End(Point.Position.X, Point.Position.Y, TraceBottomCm);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(WbBahnHoehe), true);
			Params.AddIgnoredActor(this);
			Params.AddIgnoredActors(ChunkActors);

			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(
				Hit, Start, End, ECC_WorldStatic, Params);

			// bStartPenetrating heisst: der Trace begann in Geometrie. Die
			// gemeldete Stelle ist dann kein Bodenpunkt, sondern der
			// Startpunkt selbst - sie zu uebernehmen haenge die Trasse in
			// die Luft. Lieber unaufgeloest lassen und im naechsten Tick
			// erneut versuchen.
			if (bHit && !Hit.bStartPenetrating)
			{
				Point.Position.Z = Hit.Location.Z;
				Point.bHeightResolved = true;
				bResolvedAny = true;
			}
			else
			{
				bAllResolved = false;
			}
		}
	}

	if (bAllResolved && !bHeightsFinal)
	{
		bHeightsFinal = true;

		for (FTrack* Track : { &TrackA, &TrackB })
		{
			TArray<double> ArcLengths;
			TArray<double> TerrainHeights;
			ArcLengths.Reserve(Track->Points.Num());
			TerrainHeights.Reserve(Track->Points.Num());
			for (const FTrackPoint& Point : Track->Points)
			{
				ArcLengths.Add(Point.ArcLength);
				TerrainHeights.Add(Point.Position.Z);
			}

			TArray<FWiesbadenRailProfilePoint> Profile;
			// Beide Stationen ruhen auf dem Gelaende - kein kuenstliches
			// Anheben des oberen Endes (das hing die Bergstation in die Luft,
			// siehe StationRailEndpoints). Ob die Sehne dazwischen fahrbar ist,
			// klaert BuildConstrainedGradeProfile; scheitert sie, bleibt das
			// abgetastete Terrainprofil stehen - am Boden, nicht darueber.
			double StartZ = 0.0;
			double EndZ = 0.0;
			WiesbadenRailTransport::StationRailEndpoints(
				TerrainHeights[0], TerrainHeights.Last(), RailClearanceCm, StartZ, EndZ);
			if (WiesbadenRailTransport::BuildConstrainedGradeProfile(
				ArcLengths, TerrainHeights, StartZ, EndZ,
				RailClearanceCm, MaxRailGrade, Profile))
			{			for (int32 Index = 0; Index < Track->Points.Num(); ++Index)
				{
					Track->Points[Index].Position.Z = Profile[Index].RailZCm;
				}
			}
		}

		BuildTrackMeshes();
		PlaceStructures();
#if !UE_BUILD_SHIPPING
		if (bDebugRailway)
		{
			DrawRailwayDebug();
		}
#endif

		// Hoehenbereich MELDEN.
		//
		// "Die Bahn ist nicht da" hat bisher keine pruefbare Zahl gehabt: das
		// Protokoll meldete Gleislaengen, und die entstehen aus den
		// Wegpunkten - auch dann, wenn die Trasse anschliessend unter dem
		// Gelaende liegt. Der Hoehenbereich sagt es sofort. Steht hier etwas
		// nahe null, wurden die Hoehen vor dem Streaming abgefragt und die
		// Trasse steckt im Berg.
		double MinZ = TNumericLimits<double>::Max();
		double MaxZ = TNumericLimits<double>::Lowest();
		for (const FTrack* Track : { &TrackA, &TrackB })
		{
			for (const FTrackPoint& Point : Track->Points)
			{
				MinZ = FMath::Min(MinZ, Point.Position.Z);
				MaxZ = FMath::Max(MaxZ, Point.Position.Z);
			}
		}

		UE_LOG(LogWbStreaming, Log,
			TEXT("Nerobergbahn: Gelaendehoehen uebernommen, Trasse neu gebaut. ")
			TEXT("Hoehe %.0f bis %.0f m ueber Null, Steigung %.0f m."),
			MinZ / 100.0, MaxZ / 100.0, (MaxZ - MinZ) / 100.0);

		// NN-Datum an der Talfuss-Hoehe kalibrieren: das Gelaende-Datum liegt
		// rund 70 m unter NN, die Talstation aber real bei ~160 m ue. NN. Der
		// Versatz macht die Trasse NN-bewusst (Log/Beschilderung/Hoehenmesser),
		// ohne die Geometrie zu verschieben - die bleibt am Gelaende, sonst
		// loesten sich die Stationen von Stadt und Berg (das braeuchte einen
		// weltweiten Re-Bake). Bergstation = Talstation + gemessene Steigung.
		NNDatumOffsetMeters = RealTalstationNNMeters - (MinZ / 100.0);
		TalstationNNMeters = MinZ / 100.0 + NNDatumOffsetMeters;
		BergstationNNMeters = MaxZ / 100.0 + NNDatumOffsetMeters;
		UE_LOG(LogWbStreaming, Log,
			TEXT("Nerobergbahn: NN-Datum kalibriert (Versatz %.0f m). ")
			TEXT("Talstation %.0f m ue. NN, Bergstation %.0f m ue. NN ")
			TEXT("(Vorbild 160 / 245 m)."),
			NNDatumOffsetMeters, TalstationNNMeters, BergstationNNMeters);

		// Und sofort sagen, wenn das Ergebnis nicht stimmen KANN.
		//
		// Geprueft wird der HOEHENUNTERSCHIED, nicht die absolute Lage. Das
		// Vorbild ueberwindet 83 m (ESWE Verkehr: Talstation ~160 m NN,
		// Bergstation ~243 m NN). Der In-Game-Hoehendatensatz liegt aber rund
		// 70 m TIEFER als NN - gemessen 88 bis 173 m -, waehrend die Relief-
		// spanne stimmt: 85 m gegen 83 m. Absolute Erwartungswerte (frueher
		// 130 bis 245 m) passen deshalb WEDER zum Vorbild NOCH zum Spiel und
		// gaben bei korrekt abgetasteter Trasse Fehlalarm. Der Klettergewinn
		// ist datumsunabhaengig; zusaetzlich faengt eine lockere Untergrenze
		// den Fall ab, dass die Hoehen vor dem Streaming am Weltnullpunkt
		// abgefragt wurden.
		constexpr double ExpectedClimbM = 83.0;
		constexpr double ClimbToleranceM = 20.0;
		constexpr double MinBottomM = 20.0;
		const double ClimbM = (MaxZ - MinZ) / 100.0;
		const double BottomM = MinZ / 100.0;
		if (WiesbadenRailTransport::RailHeightsImplausible(
			BottomM, MaxZ / 100.0, ExpectedClimbM, ClimbToleranceM, MinBottomM))
		{
			UE_LOG(LogWbStreaming, Warning,
				TEXT("Nerobergbahn: Hoehen unplausibel - erwartet ~%.0f m Steigung ")
				TEXT("(+/- %.0f m) und Talfuss ueber %.0f m, gemessen %.0f m Steigung ")
				TEXT("ab %.0f m. Die Trasse steckt vermutlich im Gelaende."),
				ExpectedClimbM, ClimbToleranceM, MinBottomM, ClimbM, BottomM);
		}
	}

	return bResolvedAny;
}

void AWiesbadenNerobergbahn::BuildTrackMeshes()
{
	if (!TrackMesh)
	{
		return;
	}
	TrackMesh->ClearAllMeshSections();

	int32 Section = 0;
	// Das Gleis selbst wird nicht mehr als flaches Farbband gebaut: Bett,
	// Schienen und Schwellen sind jetzt die fuenf Blender-Bauteile am Ende
	// dieser Funktion. Was hier bleibt, sind die Erdbauwerke.

	// -- Erd-Boeschung (Damm/Einschnitt) statt Stuetzpfeiler -----------------
	//
	// Von beiden Bettkanten eine Boeschung hinab zum Gelaende. Wo die Trasse
	// ueber dem Terrain liegt, entsteht ein Damm; wo sie darunter liegt, ist die
	// Boeschung ~0 (Einschnitt). Dadurch schmiegt sich die Bahn an den Hang.
	int32 EmbStartSection = INDEX_NONE;   // erste Damm-Sektion (Spandrel: heller Sandstein)
	TArray<int32> RailSections;           // Gelaender-Sektionen (Metall)
	TArray<int32> RingSections;           // Bogenring-/Sockelband-Sektionen (roter Klinker)
	TArray<int32> FlagSections;           // Wimpel-Sektionen (Vertexfarbe orange/blau)
	if (UWorld* EmbWorld = GetWorld())
	{
		constexpr float BedHalf = 130.0f;
		constexpr float BedTopLift = 6.0f;
		// F Erst-Pass: der reale Nerobergbahn-Damm ist eine BACKSTEIN-Stuetzmauer
		// (110 m, spaeter mit 5 Rundboegen), kein gruener Erddamm. Ziegelfarbe +
		// steile (nahezu senkrechte) Wand statt 35-Grad-Boeschung; das Klinker-
		// Material wird den Damm-Sektionen unten gezielt zugewiesen.
		const FLinearColor EarthColour(0.60f, 0.34f, 0.26f);   // Klinker-Ziegel (vom Backstein-Material texturiert)
		EmbStartSection = Section;

		// Wie beim Hoehen-Trace: Stadt-Chunks ausnehmen, sonst misst die Boeschung
		// gegen ein Dach und zieht eine riesige dunkle Schuerze in die Luft.
		TArray<AActor*> EmbChunks;
		for (TActorIterator<AWiesbadenCityChunk> It(EmbWorld); It; ++It)
		{
			EmbChunks.Add(*It);
		}

		// Genau 5 Rundboegen a 12,5 m in der unteren Viadukt-Sektion (Vorbild:
		// Bruecke 110 m mit 5 Boegen je 12,5 m). Das Bogenfenster wird um den
		// Viadukt-Ankerpunkt zentriert (gleiche Formel wie die Viadukt-Struktur
		// in PlaceStructures); ausserhalb bleibt die Wand Vollmauer.
		constexpr int32 ViaductArchCount = 5;
		constexpr double ArchSpanCm = 1250.0;                       // 12,5 m Spannweite
		constexpr double PierWidthCm = 260.0;                       // Pfeilerbreite zwischen den Boegen
		constexpr double SpringHeightCm = 200.0;                    // Kaempferlinie ueber Grund
		constexpr double MinSpandrelCm = 180.0;                     // Restmauer ueberm Scheitel
		constexpr double ArchMinElevationCm = 500.0;                // nur wo die Mauer hoch genug ist
		const double ArchWindowLen = ViaductArchCount * ArchSpanCm; // 62,5 m
		const double ViaductCenterS = FMath::Min(5500.0, TrackA.TotalLength * 0.18);
		const double ArchWindowStart = FMath::Clamp(
			ViaductCenterS - 0.5 * ArchWindowLen, 0.0,
			FMath::Max(0.0, TrackA.TotalLength - ArchWindowLen));
		const double ArchWindowEnd = ArchWindowStart + ArchWindowLen;
		const double ArchHalf = 0.5 * (ArchSpanCm - PierWidthCm);

		for (const FTrack* Track : { &TrackA, &TrackB })
		{
			// BEIDE Spuren ummauern: die zwei Gleise liegen ~3-4 m auseinander
			// (parallele Trassen, nicht deckungsgleich) und bilden die beiden
			// Seiten des Viadukts. Wird nur TrackA ummauert, schwebt das Gleisbett
			// von TrackB ohne Stuetze in der Luft (die weisse ueberkragende Platte).
			for (int32 SideSign = -1; SideSign <= 1; SideSign += 2)
			{
				TArray<FVector> V; TArray<int32> Tri; TArray<FVector> N;
				TArray<FVector2D> UV; TArray<FLinearColor> C;
				// Blaues Gelaender oben auf der Mauerkrone - nur am erhoehten Damm.
				TArray<FVector> RV; TArray<int32> RTri;
				TArray<FVector2D> RUV; TArray<FLinearColor> RC;
				const FLinearColor RailMetal(0.62f, 0.64f, 0.67f);   // silbriges Metall (Viadukt-Handlauf), kein Blau
				int32 PrevWallTop = -1;   // laufender Vertex-Index der hellen Spandrel-Krone

				// Roter Ziegel-Bogenring/Sockelband als eigener Streifen unten an der
				// Wand (folgt der Bogen-Unterkante) -> zweifarbiges Viadukt.
				TArray<FVector> RingV; TArray<int32> RingTri;
				TArray<FVector2D> RingUV; TArray<FLinearColor> RingC;
				int32 PrevRingTop = -1;
				const FLinearColor BrickTint(0.55f, 0.28f, 0.20f);
				constexpr double RingBandHeightCm = 75.0;   // Hoehe des roten Bogenring-/Sockelbands

				// Orange-blaue Wimpel auf schraegen Stangen entlang des Decks (Doku).
				TArray<FVector> FlagV; TArray<int32> FlagTri;
				TArray<FVector2D> FlagUV; TArray<FLinearColor> FlagC;
				int32 RailNodeIdx = 0;
				const FLinearColor FlagOrange(0.95f, 0.45f, 0.05f);
				const FLinearColor FlagBlue(0.06f, 0.20f, 0.60f);

				// Echtes Gelaender (Pfosten + zwei Handlaeufe) als MASSIVE Quader
				// statt eines flachen Bandes -> von beiden Seiten sichtbar (ein
				// Solid-Kasten zeigt aussen aus jeder Richtung eine Flaeche). Das
				// NbBlau-Material wird zusaetzlich zweiseitig gesetzt (Absicherung).
				FVector PrevRailCrown = FVector::ZeroVector;
				bool bHavePrevRailCrown = false;
				auto AppendRailBox = [&](const FVector& C0, const FVector& C1,
										 const FVector& AxisU, const FVector& AxisV,
										 float HalfU, float HalfV)
				{
					const FVector U = AxisU * HalfU;
					const FVector Wv = AxisV * HalfV;
					const int32 B = RV.Num();
					RV.Add(C0 - U - Wv); RV.Add(C0 + U - Wv);
					RV.Add(C0 + U + Wv); RV.Add(C0 - U + Wv);
					RV.Add(C1 - U - Wv); RV.Add(C1 + U - Wv);
					RV.Add(C1 + U + Wv); RV.Add(C1 - U + Wv);
					for (int32 k = 0; k < 8; ++k)
					{
						RUV.Add(FVector2D((k >= 4) ? 1.0f : 0.0f, (k % 2) ? 1.0f : 0.0f));
						RC.Add(RailMetal);
					}
					auto Quad = [&](int32 a, int32 b, int32 c, int32 d)
					{
						RTri.Append({ B + a, B + b, B + c, B + a, B + c, B + d });
					};
					Quad(1, 5, 6, 2);  // +U-Seite
					Quad(0, 3, 7, 4);  // -U-Seite
					Quad(3, 2, 6, 7);  // Oberseite
					Quad(1, 0, 4, 5);  // Unterseite
					Quad(4, 5, 6, 7);  // Ende bei C1
					Quad(0, 3, 2, 1);  // Ende bei C0
				};

				// Die Rohpunkte stehen sehr ungleich (2 m bis >100 m Abstand).
				// Fuer runde Boegen UND eine glatte Mauer die Trasse mit festem
				// Schritt neu abtasten (Position/Tangente linear interpoliert).
				constexpr double StepCm = 60.0;
				const double Total = Track->TotalLength;
				const int32 StepCount = FMath::Max(1, FMath::CeilToInt(Total / StepCm));
				for (int32 Step = 0; Step <= StepCount; ++Step)
				{
					const double S = FMath::Min(Total, Step * StepCm);

					// Trasse bei Bogenlaenge S abtasten.
					FVector P = Track->Points[0].Position;
					FVector Tangent = FVector::ForwardVector;
					for (int32 j = 0; j + 1 < Track->Points.Num(); ++j)
					{
						const double A = Track->Points[j].ArcLength;
						const double Bl = Track->Points[j + 1].ArcLength;
						if (S <= Bl || j + 2 == Track->Points.Num())
						{
							const double Denom = FMath::Max(1.0, Bl - A);
							const double T = FMath::Clamp((S - A) / Denom, 0.0, 1.0);
							P = FMath::Lerp(Track->Points[j].Position, Track->Points[j + 1].Position, T);
							Tangent = (Track->Points[j + 1].Position - Track->Points[j].Position).GetSafeNormal();
							break;
						}
					}
					const FVector Right = FVector::CrossProduct(Tangent, FVector::UpVector).GetSafeNormal();
					const float BedLift = S >= TrackBedStartCm ? 6.0f : 0.0f;
					const double BedTopZ = P.Z + BedTopLift + BedLift;

					// Gelaende an der MAUERKANTE abtasten (nicht Gleismitte), damit
					// beide Seiten ihrem eigenen Hang folgen.
					const FVector EdgeXY(P.X + Right.X * (SideSign * BedHalf),
										 P.Y + Right.Y * (SideSign * BedHalf), 0.0);
					double TerrainZ = P.Z;
					FHitResult Hit;
					FCollisionQueryParams EmbParams(SCENE_QUERY_STAT(WbBahnDamm), true);
					EmbParams.AddIgnoredActor(this);
					EmbParams.AddIgnoredActors(EmbChunks);
					const FVector TS(EdgeXY.X, EdgeXY.Y, BedTopZ + 200.0);
					if (EmbWorld->LineTraceSingleByChannel(Hit, TS,
							TS - FVector(0, 0, 100000.0), ECC_WorldStatic, EmbParams)
						&& !Hit.bStartPenetrating)
					{
						TerrainZ = Hit.Location.Z;
					}
					const double Height = FMath::Max(0.0, BedTopZ - TerrainZ);

					// IM HALLENBEREICH keine Stuetzmauer und kein Gelaender: die
					// Bahnsteighalle steht dort selbst (Bahnsteigkoerper, Balustrade),
					// und Mauer wie Gelaender liefen mitten durch den Bahnsteig
					// (Bild _station_out/n3.jpg).
					constexpr double HalleFreiCm = 700.0;   // Halle 600 + Zuschlag
					const bool bInHalle =
						(S <= HalleFreiCm) || (S >= Total - HalleFreiCm);

					// Viadukt-Boegen: NUR im 62,5-m-Fenster (5 Boegen) und nur wo
					// die Mauer hoch genug ist. Dort folgt die Mauer-Unterkante dem
					// Halbkreis-Intrados (Durchblick), an den Pfeilern zum Boden.
					double WallBottomZ = TerrainZ;
					if (S >= ArchWindowStart && S < ArchWindowEnd && Height > ArchMinElevationCm)
					{
						const double Local = FMath::Fmod(S - ArchWindowStart, ArchSpanCm);
						const double Dx = Local - 0.5 * ArchSpanCm;
						if (FMath::Abs(Dx) < ArchHalf)
						{
							const double SpringZ = TerrainZ + SpringHeightCm;
							const double Intrados = SpringZ
								+ FMath::Sqrt(FMath::Max(0.0, ArchHalf * ArchHalf - Dx * Dx));
							WallBottomZ = FMath::Clamp(Intrados, TerrainZ, BedTopZ - MinSpandrelCm);
						}
					}

					const double WallHeight = bInHalle
						? 0.0 : FMath::Max(0.0, BedTopZ - WallBottomZ);
					const double Run = WallHeight * 0.12;   // leichter Anzug der Backsteinmauer

					const FVector EdgeTop(EdgeXY.X, EdgeXY.Y, BedTopZ);
					const FVector EdgeBot(P.X + Right.X * (SideSign * (BedHalf + Run)),
										  P.Y + Right.Y * (SideSign * (BedHalf + Run)), WallBottomZ);
					// Zweifarbig: unten ein rotes Klinker-Band der Hoehe
					// RingBandHeightCm (folgt der Bogen-Unterkante -> Bogenring bzw.
					// Sockelband), darueber heller Sandstein-Spandrel. RingTop liegt
					// auf der Wandflaeche (inkl. Anzug) zwischen EdgeTop und EdgeBot.
					const double RingTopZ = FMath::Min(WallBottomZ + RingBandHeightCm, BedTopZ);
					const double RingFrac = (WallHeight > 1.0)
						? (BedTopZ - RingTopZ) / WallHeight : 0.0;
					const FVector RingTop = FMath::Lerp(EdgeTop, EdgeBot, static_cast<float>(RingFrac));
					const float Uarc = static_cast<float>(S / 300.0);

					// Heller Sandstein-Spandrel: EdgeTop -> RingTop.
					const int32 LTop = V.Add(EdgeTop);
					const int32 LBot = V.Add(RingTop);
					UV.Add(FVector2D(Uarc, 0.0f));
					UV.Add(FVector2D(Uarc, static_cast<float>((BedTopZ - RingTopZ) / 300.0)));
					C.Add(EarthColour); C.Add(EarthColour);
					if (PrevWallTop >= 0)
					{
						const int32 B = PrevWallTop;
						if (SideSign < 0)
						{
							Tri.Append({ B, LTop, B + 1, B + 1, LTop, LBot });
						}
						else
						{
							Tri.Append({ B, B + 1, LTop, LTop, B + 1, LBot });
						}
					}
					PrevWallTop = LTop;

					// Rotes Klinker-Bogenring-/Sockelband: RingTop -> EdgeBot.
					const int32 RTopIdx = RingV.Add(RingTop);
					const int32 RBotIdx = RingV.Add(EdgeBot);
					RingUV.Add(FVector2D(Uarc, 0.0f));
					RingUV.Add(FVector2D(Uarc, static_cast<float>((RingTopZ - WallBottomZ) / 300.0)));
					RingC.Add(BrickTint); RingC.Add(BrickTint);
					if (PrevRingTop >= 0)
					{
						const int32 B = PrevRingTop;
						if (SideSign < 0)
						{
							RingTri.Append({ B, RTopIdx, B + 1, B + 1, RTopIdx, RBotIdx });
						}
						else
						{
							RingTri.Append({ B, B + 1, RTopIdx, RTopIdx, B + 1, RBotIdx });
						}
					}
					PrevRingTop = RTopIdx;

					// Echtes Gelaender nur wo die Bahn spuerbar ueberm Gelaende
					// liegt (Damm/Viadukt). Alle ~240 cm ein Knoten: ein Pfosten
					// (vertikaler Quader) plus zwei Handlaeufe (oben/mitte) als
					// horizontale Quader zum vorigen Knoten.
					constexpr double RailMinHeight = 200.0;   // ab ~2 m Mauer
					constexpr double RailTopZ = 120.0;         // oberer Handlauf
					constexpr int32 RailNodeEvery = 4;         // 4 * 60 cm = 240 cm
					// Gelaender-Neuaufbau, ERSTER SCHRITT: nur der obere Handlauf als
					// DUENNER Metallholm (silbrig, kein blaues Band - so wie am realen
					// Viadukt laut Doku). Pfosten + zweiter Holm folgen, sobald der
					// Handlauf visuell bestaetigt ist (kontrollierter Aufbau, nachdem
					// die blinde Kasten-Geometrie zuvor riesige Platten erzeugt hatte).
					constexpr bool bBuildRailing = true;
					const FVector Up = FVector::UpVector;
					if (bBuildRailing && !bInHalle && Height > RailMinHeight
						&& (Step % RailNodeEvery == 0))
					{
						// Das Gelaender steht auf der AUSSENKANTE der Mauerkrone, nicht
						// auf der Bettkante: BedHalf (1,30 m) ist genau die halbe
						// Wagenbreite - dort liefen Handlauf und Wimpel durch die
						// Fenster des vorbeifahrenden Wagens. Im Vorbild liegt der
						// Handlauf auf der Krone der Stuetzmauer, also aussen.
						constexpr double RailOutCm = 45.0;
						const FVector Crown(EdgeTop.X + Right.X * (SideSign * RailOutCm),
							EdgeTop.Y + Right.Y * (SideSign * RailOutCm), BedTopZ);
						if (bHavePrevRailCrown)
						{
							// Duenner Handlauf (~6 x 4 cm) zum vorigen Knoten.
							AppendRailBox(PrevRailCrown + Up * RailTopZ, Crown + Up * RailTopZ,
										  Right, Up, 3.0f, 2.0f);
						}

						// Jeder 2. Knoten (~4,8 m): schraege Metall-Fahnenstange nach
						// aussen + orange-blauer Wimpel (wie in der Doku).
						if (RailNodeIdx % 2 == 0)
						{
							const FVector PoleBase = Crown + Up * 12.0;
							const FVector PoleTip = Crown + Up * 145.0 + Right * (SideSign * 80.0);
							AppendRailBox(PoleBase, PoleTip, Right, Tangent, 2.5f, 2.5f);
							const FVector AlongPole = (PoleTip - PoleBase).GetSafeNormal();
							const FVector Fly = Tangent;   // Wimpel weht laengs des Decks
							const FVector FA = PoleTip;
							const FVector FB = PoleTip - AlongPole * 38.0;
							const FVector FC = FA + Fly * 55.0 - AlongPole * 6.0;
							const FVector FD = FB + Fly * 42.0;
							const int32 F = FlagV.Num();
							FlagV.Add(FA); FlagV.Add(FB); FlagV.Add(FD); FlagV.Add(FC);
							FlagUV.Add(FVector2D(0.0f, 0.0f)); FlagUV.Add(FVector2D(0.0f, 1.0f));
							FlagUV.Add(FVector2D(1.0f, 1.0f)); FlagUV.Add(FVector2D(1.0f, 0.0f));
							FlagC.Add(FlagOrange); FlagC.Add(FlagBlue);
							FlagC.Add(FlagBlue); FlagC.Add(FlagOrange);
							FlagTri.Append({ F, F + 1, F + 2, F, F + 2, F + 3 });
						}
						++RailNodeIdx;

						PrevRailCrown = Crown;
						bHavePrevRailCrown = true;
					}
					else if (Height <= RailMinHeight || bInHalle)
					{
						bHavePrevRailCrown = false;   // Luecke -> Handlauf nicht ueberbruecken
					}
				}
				// Normalen aus der Geometrie ableiten statt fix UpVector: Auf der
				// steilen Boeschung ist die sichtbare Seite die Windungs-Rueckseite;
				// der zweiseitige Shader spiegelt dort die Normale, aus UpVector wurde
				// so eine nach UNTEN zeigende Schattier-Normale -> N*L <= 0 -> schwarz.
				// CalculateTangentsForMesh liefert windungs-konsistente Normalen (+
				// Tangenten), die der Shader je Seite korrekt spiegelt -> beleuchtet.
				TArray<FProcMeshTangent> Tang;
				UKismetProceduralMeshLibrary::CalculateTangentsForMesh(V, Tri, UV, N, Tang);
				TrackMesh->CreateMeshSection_LinearColor(
					Section++, V, Tri, N, UV, C, Tang,
					/*bCreateCollision=*/false);

				// Roter Klinker-Bogenring/Sockelband als eigene Sektion.
				if (RingV.Num() >= 4 && RingTri.Num() >= 3)
				{
					TArray<FVector> RingN; TArray<FProcMeshTangent> RingTang;
					UKismetProceduralMeshLibrary::CalculateTangentsForMesh(RingV, RingTri, RingUV, RingN, RingTang);
					RingSections.Add(Section);
					TrackMesh->CreateMeshSection_LinearColor(
						Section++, RingV, RingTri, RingN, RingUV, RingC, RingTang,
						/*bCreateCollision=*/false);
				}

				// Gelaender-Sektion (Metall) getrennt.
				if (RV.Num() >= 4 && RTri.Num() >= 3)
				{
					TArray<FVector> RN; TArray<FProcMeshTangent> RTang;
					UKismetProceduralMeshLibrary::CalculateTangentsForMesh(RV, RTri, RUV, RN, RTang);
					RailSections.Add(Section);
					TrackMesh->CreateMeshSection_LinearColor(
						Section++, RV, RTri, RN, RUV, RC, RTang,
						/*bCreateCollision=*/false);
				}

				// Wimpel-Sektion (orange/blau, Vertexfarbe) getrennt.
				if (FlagV.Num() >= 3 && FlagTri.Num() >= 3)
				{
					TArray<FVector> FlagN; TArray<FProcMeshTangent> FlagTang;
					UKismetProceduralMeshLibrary::CalculateTangentsForMesh(FlagV, FlagTri, FlagUV, FlagN, FlagTang);
					FlagSections.Add(Section);
					TrackMesh->CreateMeshSection_LinearColor(
						Section++, FlagV, FlagTri, FlagN, FlagUV, FlagC, FlagTang,
						/*bCreateCollision=*/false);
				}
			}
		}
	}

	// Materialien (zweifarbiges Viadukt wie im Original):
	//   Bett/Schienen        -> Vertexfarbe (dunkler Schotter)
	//   Spandrel/Pfeiler-Wand -> heller Sandstein
	//   Bogenring/Sockelband  -> roter Klinker
	//   Gelaender             -> Metall
	UMaterialInterface* TrackMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Materials/City/M_WbVertexFarbe.M_WbVertexFarbe"));
	UMaterialInterface* SandsteinMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Materials/City/MI_WbFacade_Sandstein.MI_WbFacade_Sandstein"));
	UMaterialInterface* KlinkerMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Materials/City/MI_WbFacade_Klinker.MI_WbFacade_Klinker"));
	UMaterialInterface* RailMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Nerobergbahn/Materials/MI_Nb_NbMetall.MI_Nb_NbMetall"));
	for (int32 S = 0; S < Section; ++S)
	{
		UMaterialInterface* Mat = TrackMat;
		if (FlagSections.Contains(S))
		{
			Mat = TrackMat;   // Wimpel: Vertexfarbe (orange/blau)
		}
		else if (RailSections.Contains(S) && RailMat)
		{
			Mat = RailMat;   // Metall-Gelaender
		}
		else if (RingSections.Contains(S) && KlinkerMat)
		{
			Mat = KlinkerMat;   // roter Bogenring/Sockelband
		}
		else if (EmbStartSection != INDEX_NONE && S >= EmbStartSection && SandsteinMat)
		{
			Mat = SandsteinMat;   // heller Spandrel/Pfeiler
		}
		if (Mat)
		{
			TrackMesh->SetMaterial(S, Mat);
		}
	}

	BuildTrackInstances();
}

void AWiesbadenNerobergbahn::BuildTrackInstances()
{
	// Laeuft zweimal (BeginPlay und nach dem Aufloesen der Hoehen) - alte
	// Instanzen also zuerst wegwerfen.
	TArray<UInstancedStaticMeshComponent*> Comps = {
		GleisSchienen, GleisZahnstangen, GleisSeilkanal, GleisSchwellen, GleisBett };
	for (UInstancedStaticMeshComponent* Comp : Comps)
	{
		if (Comp)
		{
			Comp->ClearInstances();
		}
	}

	const double Total = TrackA.TotalLength;
	if (TrackA.Points.Num() < 2 || TrackB.Points.Num() < 2 || Total < 50.0)
	{
		return;
	}

	// -- Trassenmitte -------------------------------------------------------
	//
	// Die beiden Wagenmitten-Linien sind einander PUNKT FUER PUNKT zugeordnet
	// (beim Einlesen werden dieselben Indizes ausgeduennt, siehe BuildTracks),
	// aber NICHT gleich parametrisiert: die erste Linie beginnt rund 3,6 m
	// weiter talwaerts, so dass ihre Bogenlaengen bis zu 3,7 m auseinander
	// laufen. Ein Vergleich bei GLEICHER Bogenlaenge vergleicht also Punkte, die
	// bis 3,8 m auseinander liegen - die Ausweiche erschien damit 100 m zu lang.
	//
	// Die Mitte bekommt darum die Bogenlaenge der Linie A: so sitzt der
	// Querschnitt an derselben Stelle wie die Fahrt, und ein Gleisbauteil liegt
	// exakt unter dem Rad, das der Wagen an dieser Bogenlaenge hat.
	FTrack Trasse;
	TArray<double> TrasseHalf;
	{
		const int32 Paare = FMath::Min(TrackA.Points.Num(), TrackB.Points.Num());
		Trasse.Points.Reserve(Paare);
		TrasseHalf.Reserve(Paare);
		double WeitestePaarungCm = 0.0;
		for (int32 i = 0; i < Paare; ++i)
		{
			const FVector& PA = TrackA.Points[i].Position;
			const FVector& PB = TrackB.Points[i].Position;
			FVector Dir = FVector::ForwardVector;
			if (i + 1 < Paare)
			{
				Dir = TrackA.Points[i + 1].Position - PA;
			}
			else if (i > 0)
			{
				Dir = PA - TrackA.Points[i - 1].Position;
			}
			const FVector Right = FVector::CrossProduct(
				FVector(Dir.X, Dir.Y, 0.0).GetSafeNormal(),
				FVector::UpVector).GetSafeNormal();
			// Nur der QUERANTEIL zaehlt: in der Ausweiche sind die Knoten der
			// beiden Wege gegeneinander versetzt, der Abstand der Punkte ist also
			// groesser als der Gleisabstand.
			const double RohHalf = FMath::Abs((PB - PA).Dot(Right)) * 0.5;
			// Die von Hand gezeichneten OSM-Wege treffen die Spurweite im Mittel
			// auf 2 cm, an einzelnen Knoten aber bis 34 cm daneben. Fuer die
			// gemeinsame Mittelschiene muss der Abstand genau eine Spurweite sein
			// - sonst liegen zwei Schienen 30 cm nebeneinander statt einer. Darum
			// wird auf 50 cm gerundet, solange der Wert in der Naehe liegt; nur
			// die echte Ausweiche behaelt ihren gemessenen Abstand.
			const double Half = FMath::Abs(RohHalf - SpurHalfCm) <= SpurtoleranzCm
				? SpurHalfCm : RohHalf;

			FTrackPoint P;
			P.Position = (PA + PB) * 0.5;
			P.ArcLength = TrackA.Points[i].ArcLength;
			P.bHeightResolved = true;
			Trasse.Points.Add(P);
			TrasseHalf.Add(Half);
			WeitestePaarungCm = FMath::Max(WeitestePaarungCm,
				FVector::Dist(PA, PB));
		}
		Trasse.TotalLength = TrackA.TotalLength;
		if (Paare > 0 && WeitestePaarungCm > 600.0)
		{
			UE_LOG(LogWbStreaming, Warning,
				TEXT("Nerobergbahn: die beiden Gleislinien sind nicht mehr 1:1 ")
				TEXT("zugeordnet (weitestes Paar %.0f cm) - Gleisquerlage pruefen."),
				WeitestePaarungCm);
		}
	}

	// Ein Trassenquerschnitt an der Bogenlaenge S der Linie A: Mitte, Richtungen
	// und der halbe Gleisabstand (50 cm im Normalfall, bis ~2,04 m in der
	// Ausweiche) - der Abstand wird zwischen den Knoten linear nachgefuehrt.
	struct FFrame
	{
		FVector Mid = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
		FVector Up = FVector::UpVector;
		double Half = SpurHalfCm;
	};

	auto Sample = [&](double S) -> FFrame
	{
		FFrame F;
		if (Trasse.Points.Num() < 2)
		{
			return F;
		}
		const double T0 = FMath::Clamp(S, 0.0, Trasse.TotalLength);
		int32 i = 1;
		while (i < Trasse.Points.Num() - 1 && Trasse.Points[i].ArcLength < T0)
		{
			++i;
		}
		const FTrackPoint& A = Trasse.Points[i - 1];
		const FTrackPoint& B = Trasse.Points[i];
		const double SegLen = FMath::Max(B.ArcLength - A.ArcLength, 1.0);
		const double T = FMath::Clamp((T0 - A.ArcLength) / SegLen, 0.0, 1.0);
		F.Mid = FMath::Lerp(A.Position, B.Position, static_cast<float>(T));
		F.Forward = (B.Position - A.Position).GetSafeNormal();
		if (F.Forward.IsNearlyZero())
		{
			F.Forward = FVector::ForwardVector;
		}
		F.Right = FVector::CrossProduct(
			FVector(F.Forward.X, F.Forward.Y, 0.0).GetSafeNormal(),
			FVector::UpVector).GetSafeNormal();
		F.Up = FVector::CrossProduct(F.Right, F.Forward).GetSafeNormal();
		F.Half = TrasseHalf[i - 1] + (TrasseHalf[i] - TrasseHalf[i - 1]) * T;
		return F;
	};

	// Ein Bauteil: Lage in der Querschnittsmitte des Rahmens F, Querlage als
	// Verschiebung, Laengsskalierung fuer das letzte Teilstueck, Querskalierung
	// fuer Schwellen und Bett in der Ausweiche. Die Z-Achse der Instanz ist die
	// Trassen-Normale - die Instanz kippt also in der Steigung mit, ohne Roll.
	auto Emit = [&](TArray<FTransform>& Out, const FFrame& F, double Len,
		double MeshLenCm, double OffsetCm, double ScaleY)
	{
		if (MeshLenCm <= 0.0)
		{
			return;
		}
		const FVector Loc = F.Mid + F.Right * OffsetCm + FVector(0.0, 0.0, RailTopCm);
		const FQuat Rot = FRotationMatrix::MakeFromXZ(F.Forward, F.Up).ToQuat();
		Out.Add(FTransform(Rot, Loc, FVector(Len / MeshLenCm, ScaleY, 1.0)));
	};

	TArray<FTransform> Schienen, Zahnstangen, Seilkanal, Schwellen, Bett;
	int32 GemeinsameMitte = 0;
	int32 Ausweichstuecke = 0;
	double AusweichStartCm = 0.0;
	double AusweichEndeCm = 0.0;
	double WeitesterAbstandCm = 0.0;

	// -- Schienen: je Stab bis zu vier Lagen -------------------------------
	// Aussen liegen sie immer auf +-(Half + 50). Die beiden inneren bei
	// +-(Half - 50) fallen im Normalfall auf DIESELBE Lage bei 0 - dann wird
	// daraus eine einzige gemeinsame Mittelschiene (Vorbild: drei
	// Laufschienen). In der Ausweiche bleiben vier Schienen.
	for (double S = 0.0; S < Total - 1.0; S += SchienenstabCm)
	{
		const double Len = FMath::Min(SchienenstabCm, Total - S);
		const FFrame F = Sample(S + Len * 0.5);
		const double InnerGap = 2.0 * F.Half - 2.0 * SpurHalfCm;

		TArray<double, TInlineAllocator<4>> Lagen;
		if (InnerGap < MittelschienenToleranzCm)
		{
			Lagen.Add(0.0);
			++GemeinsameMitte;
		}
		else
		{
			Lagen.Add(-(F.Half - SpurHalfCm));
			Lagen.Add(+(F.Half - SpurHalfCm));
			if (Ausweichstuecke == 0)
			{
				AusweichStartCm = S;
			}
			AusweichEndeCm = S + Len;
			WeitesterAbstandCm = FMath::Max(WeitesterAbstandCm, 2.0 * F.Half);
			++Ausweichstuecke;
		}
		Lagen.Add(-(F.Half + SpurHalfCm));
		Lagen.Add(+(F.Half + SpurHalfCm));

		for (const double Lage : Lagen)
		{
			Emit(Schienen, F, Len, SchienenstabCm, Lage, 1.0);
		}
	}

	// -- Zahnstangen, Seilkanal, Schotterbett ---------------------------------
	// Die Zahnstangen liegen in den Wagenmitten - dort sitzt das Zahnrad des
	// Wagens und greift zwischen die Seitenbleche. Der Seilkanal liegt in
	// Trassenmitte unter der Mittelschiene.
	for (double S = 0.0; S < Total - 1.0; S += TeilStabCm)
	{
		const double Len = FMath::Min(TeilStabCm, Total - S);
		const FFrame F = Sample(S + Len * 0.5);

		Emit(Zahnstangen, F, Len, TeilStabCm, -F.Half, 1.0);
		Emit(Zahnstangen, F, Len, TeilStabCm, +F.Half, 1.0);
		Emit(Seilkanal, F, Len, TeilStabCm, 0.0, 1.0);

		// Bettkrone mit 10 cm Ueberstand hinter den Schwellenkoepfen.
		Emit(Bett, F, Len, TeilStabCm, 0.0, (F.Half + 80.0) / 130.0);
	}

	// -- Schwellen: eine je 65 cm, quer ueber beide Gleise --------------------
	// In der Ausweiche wird die Schwelle so lang gestreckt, dass sie die
	// aeusseren Schienen noch traegt (20 cm Ueberstand).
	int32 Schwellenzahl = 0;
	for (double S = 0.0; S < Total - 1.0; S += SchwellenAbstandCm)
	{
		const FFrame F = Sample(S);
		Emit(Schwellen, F, SchwellenLaengeCm, SchwellenLaengeCm, 0.0,
			(F.Half + 70.0) / 120.0);
		++Schwellenzahl;
	}

	// Ein Aufruf je Bauteil statt AddInstance in der Schleife, und ohne
	// Navigations-Update: die Bauteile haben keine Kollision, koennen das
	// Navmesh also nicht veraendern - ein Rebuild ueber 434 m waere reine
	// Wartezeit, und die Funktion laeuft zweimal (Start und nach den Hoehen).
	auto Bunkern = [](UInstancedStaticMeshComponent* Comp, TArray<FTransform>& Liste)
	{
		if (Comp && Liste.Num() > 0)
		{
			Comp->AddInstances(Liste, /*bShouldReturnIndices=*/false,
				/*bWorldSpace=*/true, /*bUpdateNavigation=*/false);
		}
	};
	Bunkern(GleisSchienen, Schienen);
	Bunkern(GleisZahnstangen, Zahnstangen);
	Bunkern(GleisSeilkanal, Seilkanal);
	Bunkern(GleisSchwellen, Schwellen);
	Bunkern(GleisBett, Bett);

	UE_LOG(LogWbStreaming, Log,
		TEXT("Nerobergbahn-Gleis: %d Schienenstaebe (%d Stuecke mit gemeinsamer ")
		TEXT("Mittelschiene, %d in der Ausweiche von %.0f bis %.0f m, ")
		TEXT("Gleisabstand dort bis %.2f m), %d Zahnstangen, %d Seilkanale, ")
		TEXT("%d Schwellen, %d Bettstuecke."),
		Schienen.Num(), GemeinsameMitte, Ausweichstuecke,
		AusweichStartCm / 100.0, AusweichEndeCm / 100.0,
		WeitesterAbstandCm / 100.0,
		Zahnstangen.Num(), Seilkanal.Num(), Schwellenzahl, Bett.Num());
}

void AWiesbadenNerobergbahn::PlaceStructures()
{
	if (bStructuresPlaced || TrackA.Points.Num() < 2)
	{
		return;
	}
	bStructuresPlaced = true;

	auto Heading = [](const FVector& Flat) -> FRotator
	{
		return FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X)), 0.0);
	};

	// Die aufgeloeste Gleisposition traegt Schienenhoehe (Gelaende + Abstand +
	// Bettlift). Bauwerke stehen auf dem Boden - darum diese Summe abziehen.
	const double GroundDrop = RailClearanceCm + 16.0;

	auto Place = [&](UStaticMeshComponent* Comp, double S,
		double SideCm, double AlongCm, double ZDrop)
	{
		if (!Comp)
		{
			return;
		}
		FVector Pos, Tangent;
		SampleTrack(TrackA, S, Pos, Tangent);
		const FVector Flat = FVector(Tangent.X, Tangent.Y, 0.0).GetSafeNormal();
		const FVector Right = FVector::CrossProduct(Flat, FVector::UpVector).GetSafeNormal();
		const FVector World = Pos + Flat * AlongCm + Right * SideCm - FVector(0, 0, ZDrop);
		Comp->SetWorldLocation(World);
		Comp->SetWorldRotation(Heading(Flat));
		Comp->SetHiddenInGame(false);
	};

	// -- Bahnsteighalle an beiden Streckenenden ----------------------------
	//
	// Die Halle ueberspannt das GLEISPAAR: ihre Quermitte ist die Mitte
	// zwischen den beiden Wagenmitten (TrackA/TrackB), nicht eine der beiden
	// Linien - sonst stuende sie um eine halbe Spurweite daneben und der Trog
	// traefe nur ein Gleis. Sie steht auf der Trasse selbst (Zuschlag 0): der
	// Mesh-Ursprung IST die Schienenoberkante in Trassenmitte.
	//
	// Der Wagen steht am TIEFEN Hallenende (Tal bei S=0, Berg bei S=Laenge),
	// und genau dort ist die Balustrade offen (BALUSTER_FREI im Blender-Bauer).
	// Damit das an beiden Enden gilt, wird die Berghalle um 180 Grad gedreht -
	// ihre offene Seite zeigt dann zum Bergende des Gleises.
	constexpr double HalleHalbCm = 600.0;      // halbe Hallenlaenge (12,0 m)
	auto PlaceHall = [&](UStaticMeshComponent* Comp, bool bBergseite)
	{
		if (!Comp || TrackA.Points.Num() < 2 || TrackB.Points.Num() < 2)
		{
			return;
		}
		const double SA = bBergseite ? TrackA.TotalLength - HalleHalbCm : HalleHalbCm;
		const double SB = bBergseite ? TrackB.TotalLength - HalleHalbCm : HalleHalbCm;
		FVector PosA, TangA, PosB, TangB;
		SampleTrack(TrackA, SA, PosA, TangA);
		SampleTrack(TrackB, SB, PosB, TangB);
		FVector Flat(TangA.X + TangB.X, TangA.Y + TangB.Y, 0.0);
		if (!Flat.Normalize())
		{
			Flat = FVector(1.0, 0.0, 0.0);
		}
		FRotator Yaw = Heading(Flat);
		if (bBergseite)
		{
			Yaw.Yaw += 180.0;
		}
		Comp->SetWorldLocation((PosA + PosB) * 0.5);
		Comp->SetWorldRotation(Yaw);
		Comp->SetHiddenInGame(false);
	};
	PlaceHall(Talstation, false);
	PlaceHall(Bergstation, true);

	// Die Weltkoordinaten der beiden Hallen in das Log: Posen-Dateien fuer
	// Aufnahmen der Stationen brauchen sie, und aus der Trassenlaenge allein
	// sind sie nur ueber die Bogenlaenge zu rechnen (Fehlerquelle, wenn die
	// Ausduennung der Punkte die Laenge verschiebt).
	for (const TPair<UStaticMeshComponent*, const TCHAR*>& Paar : {
		TPair<UStaticMeshComponent*, const TCHAR*>(Talstation, TEXT("Tal")),
		TPair<UStaticMeshComponent*, const TCHAR*>(Bergstation, TEXT("Berg")) })
	{
		if (Paar.Key)
		{
			const FVector L = Paar.Key->GetComponentLocation();
			UE_LOG(LogWbStreaming, Log,
				TEXT("Nerobergbahn: Halle %s auf (%.0f, %.0f, %.0f) cm, Gier %.1f Grad "
					 "(Trassenlaenge A %.0f cm, B %.0f cm)."),
				Paar.Value, L.X, L.Y, L.Z, Paar.Key->GetComponentRotation().Yaw,
				TrackA.TotalLength, TrackB.TotalLength);
		}
	}

	// Viadukt: das separate Blender-Mesh SM_WbNbViadukt (5 Boegen a 6 m, nur
	// 39 m lang) wird NICHT mehr platziert. Die prozeduralen 12,5-m-Boegen in
	// der Backstein-Stuetzmauer (BuildTrackMeshes) sind dimensionsgetreu
	// (Vorbild: 5 x 12,5 m) und folgen dem Gelaende; zwei Viadukte am selben
	// Ort wuerden sich ueberlagern. Das Mesh bleibt als Asset erhalten und
	// versteckt (MakeStructure -> SetHiddenInGame(true)), falls wir es spaeter
	// massstabskorrigiert doch bevorzugen wollen.
	if (Viadukt)
	{
		Viadukt->SetHiddenInGame(true);
	}

	// KEINE Stuetzpfeiler mehr (die wirkten wie Stelzen und trafen die Referenz
	// nicht). Stattdessen schmiegt sich die Trasse ueber eine Erd-Boeschung an
	// den Hang: BuildTrackMeshes zieht vom Schotterbett eine Boeschung zum
	// Gelaende (Damm, wo die Bahn drueberliegt; im Einschnitt verschwindet sie).
	// So laeuft die Bahn wie im Vorbild direkt am Berg, nur unten der Viadukt.

	UE_LOG(LogWbStreaming, Log,
		TEXT("Nerobergbahn: Bauwerke gesetzt (Talstation, Bergstation, Viadukt)."));
}

void AWiesbadenNerobergbahn::SampleTrack(
	const FTrack& Track, double S, FVector& OutPos, FVector& OutTangent) const
{
	if (Track.Points.Num() < 2)
	{
		OutPos = FVector::ZeroVector;
		OutTangent = FVector::ForwardVector;
		return;
	}

	S = FMath::Clamp(S, 0.0, Track.TotalLength);
	int32 i = 1;
	while (i < Track.Points.Num() - 1 && Track.Points[i].ArcLength < S)
	{
		++i;
	}
	const FTrackPoint& A = Track.Points[i - 1];
	const FTrackPoint& B = Track.Points[i];
	const double SegLen = FMath::Max(B.ArcLength - A.ArcLength, 1.0);
	const double T = FMath::Clamp((S - A.ArcLength) / SegLen, 0.0, 1.0);
	OutPos = FMath::Lerp(A.Position, B.Position, static_cast<float>(T));
	OutTangent = (B.Position - A.Position).GetSafeNormal();
}

void AWiesbadenNerobergbahn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Hoehen nachtragen, bis alles aufliegt (World Partition streamt den
	// Neroberg erst, wenn jemand hinschaut).
	if (!bHeightsFinal)
	{
		HeightRetryRemaining -= DeltaSeconds;
		if (HeightRetryRemaining <= 0.0f)
		{
			HeightRetryRemaining = 2.0f;
			ResolveHeights();
		}
	}

	// -- Seilbewegung --------------------------------------------------------
	const double SpeedCmPerS = SpeedKmh * 100000.0 / 3600.0;
	if (DwellRemaining > 0.0f)
	{
		DwellRemaining -= DeltaSeconds;
	}
	else if (Direction != 0)
	{
		CablePosition += Direction * SpeedCmPerS * DeltaSeconds;
		if (CablePosition >= TrackA.TotalLength)
		{
			CablePosition = TrackA.TotalLength;
			Direction = -1;
			DwellRemaining = DwellSeconds;
		}
		else if (CablePosition <= 0.0)
		{
			CablePosition = 0.0;
			Direction = +1;
			DwellRemaining = DwellSeconds;
		}
	}

	// -- Wagen setzen --------------------------------------------------------
	auto PlaceCar = [&](USceneComponent* Car, const FTrack& Track, double S)
	{
		if (!Car)
		{
			return;
		}
		FVector Pos, Tangent;
		SampleTrack(Track, S, Pos, Tangent);
		Car->SetWorldLocation(Pos + FVector(0, 0, CarFloorCm));

		// Nur GIEREN. Die Neigung der Trasse steckt im Mesh: SM_WbNbWagen ist
		// um die mittlere Steigung (19,5 %) gekippt, der Kasten liegt damit
		// parallel zur Strecke - im Vorbild steht der Wagen genau so auf dem
		// Gleis (Belegframes in Quellen/nerobergbahn-video/NACHBAU-REFERENZ.md,
		// §3). Wuerde der Actor zusaetzlich nicken, laege der Wagen doppelt
		// schief.
		const FRotator Yaw = FRotationMatrix::MakeFromX(
			FVector(Tangent.X, Tangent.Y, 0.0f).GetSafeNormal()).Rotator();
		Car->SetWorldRotation(Yaw);
	};

	// Gegenlauf am Seil: Wagen B steht immer spiegelbildlich zu A.
	PlaceCar(CarA, TrackA, CablePosition);
	const double PositionB = WiesbadenRailTransport::OpposingCablePosition(
		CablePosition, TrackB.TotalLength, true);
	PlaceCar(CarB, TrackB, PositionB);

	// -- Fuehrerstand: Anzeigen und Wasserballast --------------------------
	// Die Nadel folgt der FAHRT, nicht der eingestellten Geschwindigkeit: in
	// der Haltezeit steht der Wagen und der Zeiger faellt auf 0.
	const float Kmh = (DwellRemaining > 0.0f || Direction == 0) ? 0.0f : SpeedKmh;
	UpdateCarDetails(0, CablePosition, TrackA.TotalLength, Kmh, DeltaSeconds);
	UpdateCarDetails(1, PositionB, TrackB.TotalLength, Kmh, DeltaSeconds);

	// -- Mitfahren -----------------------------------------------------------
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	// Entwicklungshilfe -WbMitfahr=<Sekunden>: stellt den Spieler nach der
	// angegebenen Zeit an Wagen A und laesst ihn einsteigen. Ohne Tastendruck
	// laesst sich die Mitfahrt sonst nicht ausloesen - fuer Aufnahmen aus dem
	// Wagen heraus (zusammen mit -WbZuFuss=<Sekunden> und -WbShotWhenReady).
	if (!bDevRideDone && DevRideAfterSeconds >= 0.0f
		&& World->GetTimeSeconds() >= DevRideAfterSeconds)
	{
		bDevRideDone = true;
		APawn* DevPawn = PC->GetPawn();
		if (Cast<AWiesbadenFootPawn>(DevPawn) && !RideSession.IsRiding() && CarA)
		{
			// Erst neben den Wagen setzen: die Einstiegsreichweite wuerde einen
			// weit entfernt stehenden Spieler abweisen.
			DevPawn->SetActorLocation(
				CarA->GetComponentLocation() + FVector(0, 0, 120.0f));
			ToggleBoarding();
			UE_LOG(LogWbStreaming, Log,
				TEXT("Nerobergbahn: -WbMitfahr hat den Spieler in Wagen A gesetzt."));
		}
		else if (!Cast<AWiesbadenFootPawn>(DevPawn))
		{
			UE_LOG(LogWbStreaming, Warning,
				TEXT("Nerobergbahn: -WbMitfahr greift nur zu Fuss - vorher mit "
					 "-WbZuFuss=<Sekunden> aus dem Auto aussteigen."));
		}
	}

	// Entwicklungshilfe -WbKurbel=<Sekunden>: dreht die Kurbel des Wagens, in
	// dem der Fahrgast sitzt, nach so vielen Sekunden von selbst. Ohne sie
	// laesst sich der Schwimmer im Schauglas nicht ohne Tastendruck aufnehmen
	// (shot_mitfahrt.cmd kann keine Tasten senden).
	if (!bDevCrankDone && DevCrankAfterSeconds >= 0.0f
		&& World->GetTimeSeconds() >= DevCrankAfterSeconds)
	{
		bDevCrankDone = true;
		ToggleWaterValve(RideSession.IsRiding() ? RideSession.CarIndex : 0);
	}

	const bool bBoardDown = PC->IsInputKeyDown(EKeys::E);
	if (bBoardDown && !bBoardKeyHeld)
	{
		ToggleBoarding();
	}
	bBoardKeyHeld = bBoardDown;

	// Kurbel bedienen: im Vorbild ist sie das "Multitool" fuer Wasserschieber
	// und Bremse (TON 13:13), bedient wird sie von der offenen Buehne aus -
	// im Modell greift sie, wer mitfaehrt (Taste K).
	const bool bCrankDown = PC->IsInputKeyDown(EKeys::K);
	if (bCrankDown && !bCrankKeyHeld && RideSession.IsRiding())
	{
		ToggleWaterValve(RideSession.CarIndex);
	}
	bCrankKeyHeld = bCrankDown;
}

#if !UE_BUILD_SHIPPING
void AWiesbadenNerobergbahn::DrawRailwayDebug()
{
	if (!GetWorld() || !bDebugRailway)
	{
		return;
	}
	for (const FTrack* DebugTrack : { &TrackA, &TrackB })
	{
		for (int32 Index = 1; Index < DebugTrack->Points.Num(); ++Index)
		{
			const FVector& A = DebugTrack->Points[Index - 1].Position;
			const FVector& B = DebugTrack->Points[Index].Position;
			DrawDebugLine(GetWorld(), A, B, FColor::Green, false, 0.0f, 0, 3.0f);
			DrawDebugLine(GetWorld(), A + FVector(0, 0, -RailClearanceCm),
				A + FVector(0, 0, 120.0f), FColor::Yellow, false, 0.0f, 0, 1.0f);
		}
	}
}
#endif

bool AWiesbadenNerobergbahn::GetCarWater(int32 Index, float& OutFuellstand,
	bool& bOutSchieberOffen) const
{
	if (Index < 0 || Index > 1)
	{
		return false;
	}
	OutFuellstand = CarDetail[Index].Fuellstand;
	bOutSchieberOffen = CarDetail[Index].bSchieberOffen;
	return true;
}

void AWiesbadenNerobergbahn::UpdateCarDetails(int32 Index, double S,
	double Laenge, float Kmh, float DeltaSeconds)
{
	FWbCarDetail& Detail = CarDetail[Index];
	if (!Detail.Zeiger || !Detail.Schwimmer || !Detail.Kurbel)
	{
		return;
	}
	Detail.Bahnposition = S;
	Detail.Bahnlaenge = Laenge;

	// -- Wasserballast -----------------------------------------------------
	// Das Reservoir der Bergstation liegt nur 2-3 m ueber dem Gleis und fuellt
	// im natuerlichen Gefaelle (TON 5:46); unten laeuft das Wasser bei
	// offenem Schieber in das 200-m3-Becken ab (TON 3:18). Die Raten sind
	// [SCHAETZ]: 7 m3 in rund 50 s Abfluss bzw. 70 s Fuellung.
	if (Detail.bSchieberOffen)
	{
		Detail.Fuellstand = FMath::Max(0.0f, Detail.Fuellstand - 0.020f * DeltaSeconds);
	}
	else if (Laenge > 0.0 && S > Laenge - 300.0)
	{
		Detail.Fuellstand = FMath::Min(1.0f, Detail.Fuellstand + 0.014f * DeltaSeconds);
	}

	// -- Schwimmer im Schauglas --------------------------------------------
	const FQuat Tilt = CarTiltQuat();
	Detail.Schwimmer->SetRelativeLocation(FMath::Lerp(
		Tilt.RotateVector(CarSchwimmerUnten),
		Tilt.RotateVector(CarSchwimmerOben), Detail.Fuellstand));
	Detail.Schwimmer->SetRelativeRotation(Tilt);

	// -- Geschwindigkeitsanzeige -------------------------------------------
	// Der Zeiger laeuft auf der im Blender-Bau definierten Scheibe: Bildwinkel
	// 225 Grad bei 0 km/h bis -45 Grad bei 10 km/h, gemessen von Bildrechts
	// nach Bildoben. Die Nadel zeigt im Mesh nach "Bildoben", der Bildwinkel
	// ist deshalb Winkel - 90 Grad. Der Zeiger steht im ungekippten Bau-System:
	// lokale Z-Achse = Plattennormale (-1,0,1), lokale Y-Achse = Bildoben.
	const float T = FMath::Clamp(Kmh / CarTachoMaxKmh, 0.0f, 1.0f);
	const float Bildwinkel = FMath::Lerp(CarZeigerWinkelNull,
		CarZeigerWinkelVoll, T);
	const FVector NBuild = FVector(-1.0f, 0.0f, 1.0f).GetSafeNormal();
	const FVector UBuild = FVector(1.0f, 0.0f, 1.0f).GetSafeNormal();
	const FVector NActor = Tilt.RotateVector(NBuild);
	const FVector UActor = Tilt.RotateVector(UBuild);
	const FQuat Basis = FRotationMatrix::MakeFromZY(NActor, UActor).ToQuat();
	const FQuat Dreh(NActor, FMath::DegreesToRadians(Bildwinkel - 90.0f));
	Detail.Zeiger->SetRelativeLocation(Tilt.RotateVector(CarTachoAnchor));
	Detail.Zeiger->SetRelativeRotation(Dreh * Basis);
	Detail.Zeigerwinkel = Bildwinkel;

	// -- Kurbelarm ---------------------------------------------------------
	const float Ziel = Detail.bSchieberOffen ? CarKurbelWinkelOffen : CarKurbelWinkelZu;
	Detail.Kurbelwinkel = FMath::FInterpTo(Detail.Kurbelwinkel, Ziel,
		DeltaSeconds, 4.0f);
	Detail.Kurbel->SetRelativeLocation(Tilt.RotateVector(CarKurbelachse));
	Detail.Kurbel->SetRelativeRotation(Tilt * FQuat(FVector::YAxisVector,
		FMath::DegreesToRadians(Detail.Kurbelwinkel)));

	// -- Entwicklungsprotokoll --------------------------------------------
	// Zahlen statt Augenmass: die Nadelstellung im Bild ist nur dann eine
	// Pruefung, wenn daneben steht, was der Code gestellt haben wollte. Der
	// Ist-Winkel wird aus der TATSAECHLICHEN Drehung der Nadel zurueckgerechnet
	// (Vektor in das ungekippte Bau-System, dann gegen Bildrechts/Bildoben
	// zerlegt) - eine falsch gesetzte Basis faellt damit als Zahl auf, nicht
	// erst als schief stehende Nadel.
	if (DevCarLogInterval > 0.0f)
	{
		UWorld* LogWorld = GetWorld();
		if (LogWorld && LogWorld->GetTimeSeconds() >= NextDevCarLogTime)
		{
			NextDevCarLogTime = LogWorld->GetTimeSeconds() + DevCarLogInterval;
			// Bildrechts im Bau-System ist U x N (die Scheibe wird vom Wagen aus
			// gelesen). Gemessen wird die RICHTUNG DES BLATTS, und zwar aus der
			// relativen Drehung - die Weltrichtung enthielte den Gierwinkel des
			// Wagens und waere als Zeigerstellung unbrauchbar. Das Blatt zeigt im
			// Mesh nach +Y (make_nerobergbahn.py: "Spitze nach +Y"); welcher der
			// beiden Achsen das im UE-Mesh entspricht, entscheidet der Vergleich
			// mit dem Soll - deshalb werden beide gerechnet und der passende
			// Achsenname mitgeschrieben.
			const FVector RBuild = FVector::CrossProduct(UBuild, NBuild);
			const FQuat Rel = Detail.Zeiger->GetRelativeRotation().Quaternion();
			auto Blattwinkel = [&](const FVector& Achse)
			{
				const FVector V = Rel.RotateVector(Achse);
				return FMath::RadiansToDegrees(FMath::Atan2(V | UBuild, V | RBuild));
			};
			const float BildY = Blattwinkel(FVector::YAxisVector);
			const float BildGegenY = Blattwinkel(-FVector::YAxisVector);
			const bool bYTrifft = FMath::Abs(BildY - Bildwinkel)
				<= FMath::Abs(BildGegenY - Bildwinkel);
			const float Abweichung =
				(bYTrifft ? BildY : BildGegenY) - Bildwinkel;
			UE_LOG(LogWbStreaming, Log,
				TEXT("WbWagenlog: Wagen %s: %.1f km/h, Fuellstand %.0f %%, "
					 "Schieber %s, Kurbel %.1f Grad, Nadel Soll %.1f Grad, "
					 "Blatt %s Abweichung %+.1f Grad"),
				Index == 0 ? TEXT("A") : TEXT("B"), Kmh, 100.0f * Detail.Fuellstand,
				Detail.bSchieberOffen ? TEXT("offen") : TEXT("zu"),
				Detail.Kurbelwinkel, Bildwinkel,
				bYTrifft ? TEXT("+Y") : TEXT("-Y"), Abweichung);
		}
	}
}

void AWiesbadenNerobergbahn::ToggleWaterValve(int32 Index)
{
	if (Index < 0 || Index > 1)
	{
		return;
	}
	FWbCarDetail& Detail = CarDetail[Index];
	Detail.bSchieberOffen = !Detail.bSchieberOffen;

	UE_LOG(LogWbStreaming, Log,
		TEXT("Nerobergbahn: Kurbel gedreht (Wagen %s) - Wasserschieber %s, "
			 "Fuellstand %.0f %%."),
		Index == 0 ? TEXT("A") : TEXT("B"),
		Detail.bSchieberOffen ? TEXT("OFFEN") : TEXT("ZU"),
		100.0f * Detail.Fuellstand);

	// Kurzer Hinweis im HUD: die Bedienung ist ohne Text nicht auffindbar.
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (AWiesbadenVehicleHUD* HUD = Cast<AWiesbadenVehicleHUD>(PC->GetHUD()))
			{
				HUD->ShowTransientHint(Detail.bSchieberOffen
					? TEXT("Kurbel gedreht - Wasserschieber offen, der Wagen laeuft leer")
					: TEXT("Kurbel gedreht - Wasserschieber zu"));
			}
		}
	}
}

void AWiesbadenNerobergbahn::ToggleBoarding()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	// Aussteigen: neben dem Wagen absetzen und die Fusssteuerung freigeben.
	if (RideSession.IsRiding())
	{
		USceneComponent* Car = RideSession.CarIndex == 0 ? CarA : CarB;
		APawn* Passenger = RideSession.GetPassenger();
		if (!RideSession.BeginExiting() || !Passenger)
		{
			return;
		}
		Passenger->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		if (Car)
		{
			const FVector Side = Car->GetRightVector() * 250.0f + FVector(0, 0, 60.0f);
			Passenger->SetActorLocation(Car->GetComponentLocation() + Side);
		}
		if (AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(Passenger))
		{
			Foot->SetRiding(false);
		}
		DestroyPassengerCamera();
		UE_LOG(LogWbStreaming, Log, TEXT("Nerobergbahn: Fahrgast ausgestiegen."));
		RideSession.CompleteExit();
		return;
	}

	// Einsteigen: nur der Spieler ZU FUSS, nur nahe an einem Wagen.
	AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(Pawn);
	if (!Foot)
	{
		return;
	}

	USceneComponent* Cars[2] = { CarA, CarB };
	for (int32 i = 0; i < 2; ++i)
	{
		if (!Cars[i])
		{
			continue;
		}
		const float Dist = FVector::Dist(
			Pawn->GetActorLocation(), Cars[i]->GetComponentLocation());
		if (Dist > BoardRangeCm)
		{
			continue;
		}

		if (!RideSession.BeginBoarding(Pawn, i))
		{
			continue;
		}
		Foot->SetRiding(true);
		Pawn->AttachToComponent(Cars[i], FAttachmentTransformRules::KeepWorldTransform);
		if (!RideSession.ConfirmRiding())
		{
			Pawn->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Foot->SetRiding(false);
			RideSession.Reset();
			continue;
		}
		CreatePassengerCamera();
		// In den Mittelgang der Kabine: Wagenboden 0,85 m ueber dem Ursprung
		// plus halbe Koerperhoehe der Kapsel (90 cm) = 175 cm. Mit den alten
		// 150 cm stand der Fahrgast 25 cm IM Boden - sichtbar war das erst,
		// seit der Innenraum eingerichtet ist.
		// Der Wagen wird nur giert, der Zuschlag wirkt also in Welt-Z; die
		// Neigung des Kastens steckt im Mesh, und x = 0 ist die Wagenmitte,
		// wo der Boden exakt auf 0,85 m liegt.
		Pawn->SetActorLocation(Cars[i]->GetComponentLocation() + FVector(0, 0, 175.0f));
		UE_LOG(LogWbStreaming, Log, TEXT("Nerobergbahn: Fahrgast eingestiegen (Wagen %s)."),
			i == 0 ? TEXT("A") : TEXT("B"));
		return;
	}
}

void AWiesbadenNerobergbahn::CreatePassengerCamera()
{
	if (PassengerCamera || !RideSession.GetPassenger())
	{
		return;
	}
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	USceneComponent* Car = RideSession.CarIndex == 0 ? CarA : CarB;
	if (!PC || !Car)
	{
		return;
	}
	PassengerCamera = NewObject<UWiesbadenVehicleCameraComponent>(this, TEXT("NerobergbahnPassengerCamera"));
	PassengerCamera->SetupAttachment(Car);
	// Verfolgeransicht von AUSSEN: der Arm zeigt von der Wagenmitte nach
	// hinten. Bei 260 cm lag die Kamera noch IM Wagenkasten (der ist 5,64 m
	// lang) und blickte auf die Rueckseiten der Inneneinrichtung.
	PassengerCamera->CameraOffset = FVector(0.0f, 0.0f, 210.0f);
	PassengerCamera->FollowArmLength = 760.0f;
	PassengerCamera->ZoomMinArmLength = 120.0f;
	PassengerCamera->ZoomMaxArmLength = 1400.0f;
	// Horizont waagerecht halten. Ohne das erbte die Kamera die STEILE Neigung
	// des Wagens (die Bahn faehrt ~30 % Steigung): der Verfolgerarm zeigte
	// steil abwaerts in den massiven Trassen-Balken und das Bild wurde SCHWARZ.
	// Waagerecht steht die Kamera ueber dem Balken und zeigt den Panoramablick
	// ueber die Stadt statt ins Innere der Trasse.
	PassengerCamera->bLevelHorizon = true;
	// Innenansicht (C schaltet Follow -> Orbit -> Cockpit): First-Person in der
	// Kabine. Der Wagen bleibt sichtbar (man sitzt darin); die Cockpit-Kamera
	// erbt die Wagenneigung, zeigt also den Hang hinauf.
	//
	// Hoehen stammen aus dem Mesh: Wagenboden 0,85 m ueber dem Ursprung, ein
	// stehender Fahrgast hat die Augen bei 0,85 + 1,60 = 2,45 m. Tacho (1,62 m),
	// Schauglas und Kurbel sitzen tiefer - daher der Tiefblick von 18 Grad.
	PassengerCamera->CockpitOffset = FVector(0.0f, 0.0f, 245.0f);
	PassengerCamera->CockpitPitch = -18.0f;
	PassengerCamera->RegisterComponent();
	PassengerCamera->ActivateExternalView(PC, Car, RideSession.GetPassenger());
	// Der Innenraum ist seit dem Umbau eingerichtet - die Fahrt beginnt daher
	// IM Wagen statt hinter ihm. C schaltet weiter zu Orbit und Follow.
	PassengerCamera->SetCameraMode(EWiesbadenVehicleCameraMode::Cockpit);
}

void AWiesbadenNerobergbahn::DestroyPassengerCamera()
{
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (APawn* PassengerPawn = RideSession.GetPassenger())
		{
			PC->SetViewTarget(PassengerPawn);
		}
	}
	if (PassengerCamera)
	{
		PassengerCamera->DeactivateExternalView();
		PassengerCamera->DestroyComponent();
		PassengerCamera = nullptr;
	}
}
