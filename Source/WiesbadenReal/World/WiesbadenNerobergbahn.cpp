// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenNerobergbahn.h"
#include "World/WiesbadenRailTransport.h"

#include "WiesbadenReal.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
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
	constexpr double TrackBedStartCm = 850.0;
	constexpr double RailClearanceCm = 35.0;
	constexpr double MaxRailGrade = 0.30;

	// Hebung des Wagen-Ursprungs ueber den Gleispunkt. Der Ursprung von
	// SM_WbNbWagen liegt auf der Schienenkontaktlinie in Wagenmitte, der
	// Unterrahmen reicht als Keil nach unten - ein kleiner Wert setzt ihn
	// buendig auf die Schiene.
	constexpr float CarFloorCm = 12.0f;
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

	// Material der Trasse.
	//
	// Die Abschnitte unterscheiden sich NUR ueber Vertexfarben:
	// Schotterbett dunkel, Schienen hell. Hier stand lange
	// BasicShapeMaterial - und der Kommentar daneben behauptete, es zeige
	// die Vertexfarben. Das tut es nicht. Die Trasse war genau das graue
	// Band den Berg hinauf, vor dem der Kommentar warnte.
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> TrackMat(
			TEXT("/Game/Materials/City/M_WbVertexFarbe.M_WbVertexFarbe"));
		if (TrackMat.Succeeded())
		{
			// Sechs Abschnitte: je Gleis Bett, linke und rechte Schiene.
			for (int32 Section = 0; Section < 6; ++Section)
			{
				TrackMesh->SetMaterial(Section, TrackMat.Object);
			}
		}
	}

	CarA = BuildCar(TEXT("WagenA"));
	CarB = BuildCar(TEXT("WagenB"));

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

	Talstation = MakeStructure(TEXT("Talstation"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbTalstation.SM_WbNbTalstation"));
	Bergstation = MakeStructure(TEXT("Bergstation"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbBergstation.SM_WbNbBergstation"));
	Viadukt = MakeStructure(TEXT("Viadukt"),
		TEXT("/Game/Nerobergbahn/Meshes/SM_WbNbViadukt.SM_WbNbViadukt"));
}

USceneComponent* AWiesbadenNerobergbahn::BuildCar(const TCHAR* Name)
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
	return Car;
}

void AWiesbadenNerobergbahn::BeginPlay()
{
	Super::BeginPlay();

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
	for (const FTrack* Track : { &TrackA, &TrackB })
	{
		// Drei Baender je Gleis: Schotterbett (2,6 m) und zwei Schienen
		// (Meterspur: Schienenmitten 50 cm neben der Achse).
		struct FRibbon { float HalfWidth; float Offset; float Lift; FLinearColor Colour; };
		const FRibbon Ribbons[] = {
			{ 130.0f, 0.0f, 4.0f,  FLinearColor(0.26f, 0.245f, 0.22f) },  // Schotterbett - realistischer Kies (mittelgrau, leicht warm - Bruchstein statt hellbeige)
			{ 5.0f, -50.0f, 14.0f, FLinearColor(0.40f, 0.41f, 0.44f) },   // Schiene links - gedaempfter Stahl (heller als Bett/Schwelle, aber nicht grellweiss)
			{ 5.0f, +50.0f, 14.0f, FLinearColor(0.40f, 0.41f, 0.44f) },   // Schiene rechts
		};

		for (const FRibbon& Ribbon : Ribbons)
		{
			TArray<FVector> Vertices;
			TArray<int32> Triangles;
			TArray<FVector> Normals;
			TArray<FVector2D> UV0;
			TArray<FLinearColor> Colours;

			for (int32 i = 0; i < Track->Points.Num(); ++i)
			{
				const FVector& P = Track->Points[i].Position;
				const float BedLift = Track->Points[i].ArcLength >= TrackBedStartCm ? 6.0f : 0.0f;
				FVector Tangent = FVector::ForwardVector;
				if (i + 1 < Track->Points.Num())
				{
					Tangent = (Track->Points[i + 1].Position - P).GetSafeNormal();
				}
				else if (i > 0)
				{
					Tangent = (P - Track->Points[i - 1].Position).GetSafeNormal();
				}
				const FVector Right = FVector::CrossProduct(Tangent, FVector::UpVector).GetSafeNormal();
				const FVector Centre = P + Right * Ribbon.Offset + FVector(0, 0, Ribbon.Lift + BedLift);

				Vertices.Add(Centre - Right * Ribbon.HalfWidth);
				Vertices.Add(Centre + Right * Ribbon.HalfWidth);
				const float V = static_cast<float>(Track->Points[i].ArcLength / 100.0);
				UV0.Add(FVector2D(0.0f, V));
				UV0.Add(FVector2D(1.0f, V));
				Colours.Add(Ribbon.Colour);
				Colours.Add(Ribbon.Colour);

				if (i > 0)
				{
					const int32 Base = (i - 1) * 2;
					Triangles.Append({ Base, Base + 2, Base + 1,
									   Base + 1, Base + 2, Base + 3 });
				}
			}

			// Normalen aus der Geometrie ableiten (wie bei der Boeschung): fixe
			// UpVector-Normalen auf dem steilen Gleiskoerper spiegelte der
			// zweiseitige Shader auf der Sichtseite nach unten -> dunkler Streifen.
			TArray<FProcMeshTangent> Tang;
			UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UV0, Normals, Tang);
			TrackMesh->CreateMeshSection_LinearColor(
				Section++, Vertices, Triangles, Normals, UV0, Colours,
				Tang, /*bCreateCollision=*/false);
		}
	}

	// -- Schwellen: dunkle Querbalken auf dem Schotter, ~alle 65 cm -----------
	// Erst dadurch liest sich das Band als GLEIS statt als schwarze Strasse.
	{
		const FLinearColor SleeperColour(0.16f, 0.115f, 0.08f);   // creosot-dunkles Holz
		constexpr double SleeperSpacingCm = 65.0;
		constexpr float SleeperHalfLenCm = 13.0f;    // Balken 26 cm laengs
		constexpr float SleeperHalfWidthCm = 88.0f;  // Schwelle ~1,76 m quer (schmaler als das Bett)
		constexpr float SleeperLift = 8.0f;
		for (const FTrack* Track : { &TrackA, &TrackB })
		{
			TArray<FVector> V; TArray<int32> Tri; TArray<FVector> N;
			TArray<FVector2D> UV; TArray<FLinearColor> C;
			double NextArc = 0.0;
			for (int32 i = 0; i < Track->Points.Num(); ++i)
			{
				if (Track->Points[i].ArcLength < NextArc)
				{
					continue;
				}
				NextArc = Track->Points[i].ArcLength + SleeperSpacingCm;
				const FVector& P = Track->Points[i].Position;
				FVector Tangent = FVector::ForwardVector;
				if (i + 1 < Track->Points.Num())
				{
					Tangent = (Track->Points[i + 1].Position - P).GetSafeNormal();
				}
				else if (i > 0)
				{
					Tangent = (P - Track->Points[i - 1].Position).GetSafeNormal();
				}
				const FVector Right = FVector::CrossProduct(Tangent, FVector::UpVector).GetSafeNormal();
				const float BedLift = Track->Points[i].ArcLength >= TrackBedStartCm ? 6.0f : 0.0f;
				const FVector Base = P + FVector(0, 0, SleeperLift + BedLift);
				const FVector Along = Tangent * SleeperHalfLenCm;
				const FVector Across = Right * SleeperHalfWidthCm;
				// Reihenfolge wie beim Bett-Band: [links0, rechts0, links1, rechts1].
				const int32 B = V.Num();
				V.Add(Base - Across - Along);
				V.Add(Base + Across - Along);
				V.Add(Base - Across + Along);
				V.Add(Base + Across + Along);
				// Normalen unten aus der Geometrie (CalculateTangentsForMesh).
				for (int32 k = 0; k < 4; ++k)
				{
					UV.Add(FVector2D(0.0f, 0.0f));
					C.Add(SleeperColour);
				}
				Tri.Append({ B, B + 2, B + 1, B + 1, B + 2, B + 3 });
			}
			if (V.Num() > 0)
			{
				TArray<FProcMeshTangent> Tang;
				UKismetProceduralMeshLibrary::CalculateTangentsForMesh(V, Tri, UV, N, Tang);
				TrackMesh->CreateMeshSection_LinearColor(
					Section++, V, Tri, N, UV, C, Tang,
					/*bCreateCollision=*/false);
			}
		}
	}

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

					const double WallHeight = FMath::Max(0.0, BedTopZ - WallBottomZ);
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
					if (bBuildRailing && Height > RailMinHeight && (Step % RailNodeEvery == 0))
					{
						const FVector Crown(EdgeTop.X, EdgeTop.Y, BedTopZ);
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
					else if (Height <= RailMinHeight)
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

	// Tal- und Bergstation an den Streckenenden, seitlich neben dem Gleis,
	// damit das Perronvordach ueber die Trasse reicht.
	Place(Talstation, 0.0, 650.0, -150.0, GroundDrop);
	Place(Bergstation, TrackA.TotalLength, 700.0, 150.0, GroundDrop);

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

		// Nur GIEREN. Die echten Wagen sind Stufenwagen: der Boden bleibt
		// waagerecht, waehrend die Trasse unter ihnen steigt - ein
		// mitkippender Kasten saehe nach Achterbahn aus, nicht nach 1888.
		const FRotator Yaw = FRotationMatrix::MakeFromX(
			FVector(Tangent.X, Tangent.Y, 0.0f).GetSafeNormal()).Rotator();
		Car->SetWorldRotation(Yaw);
	};

	// Gegenlauf am Seil: Wagen B steht immer spiegelbildlich zu A.
	PlaceCar(CarA, TrackA, CablePosition);
	PlaceCar(CarB, TrackB, WiesbadenRailTransport::OpposingCablePosition(
		CablePosition, TrackB.TotalLength, true));

	// -- Mitfahren -----------------------------------------------------------
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	const bool bBoardDown = PC->IsInputKeyDown(EKeys::E);
	if (bBoardDown && !bBoardKeyHeld)
	{
		ToggleBoarding();
	}
	bBoardKeyHeld = bBoardDown;
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
		// In die Wagenmitte, Boden auf Bodenhoehe des Wagens.
		Pawn->SetActorLocation(Cars[i]->GetComponentLocation() + FVector(0, 0, 150.0f));
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
	PassengerCamera->CameraOffset = FVector(0.0f, 0.0f, 155.0f);
	PassengerCamera->FollowArmLength = 260.0f;
	PassengerCamera->ZoomMinArmLength = 80.0f;
	PassengerCamera->ZoomMaxArmLength = 700.0f;
	// Horizont waagerecht halten. Ohne das erbte die Kamera die STEILE Neigung
	// des Wagens (die Bahn faehrt ~30 % Steigung): der Verfolgerarm zeigte
	// steil abwaerts in den massiven Trassen-Balken und das Bild wurde SCHWARZ.
	// Waagerecht steht die Kamera ueber dem Balken und zeigt den Panoramablick
	// ueber die Stadt statt ins Innere der Trasse.
	PassengerCamera->bLevelHorizon = true;
	// Innenansicht (C schaltet Follow -> Orbit -> Cockpit): First-Person im Wagen,
	// Blick nach vorn die Trasse hinauf. Der Wagen bleibt sichtbar (man sitzt
	// darin); die Cockpit-Kamera erbt die Wagenneigung, zeigt also den Hang hinauf.
	PassengerCamera->CockpitOffset = FVector(40.0f, 0.0f, 175.0f);
	PassengerCamera->RegisterComponent();
	PassengerCamera->ActivateExternalView(PC, Car, RideSession.GetPassenger());
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
