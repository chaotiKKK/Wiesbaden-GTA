// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenNerobergbahn.h"

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
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/WiesbadenFootPawn.h"

namespace
{
	// Streckenpunkte aus OpenStreetMap (way 39223618/39223619 und Nachbarn),
	// Tal -> Berg, bereits entlang der Fahrtrichtung sortiert. Zwei Gleise,
	// wie beim Vorbild rund 4 m auseinander.
	struct FLatLon { double Lat; double Lon; };

	const FLatLon TRACK_A[] = {
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

	const FLatLon TRACK_B[] = {
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

	// Wagenboden ueber Schienenoberkante.
	constexpr float CarFloorCm = 45.0f;
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
}

USceneComponent* AWiesbadenNerobergbahn::BuildCar(const TCHAR* Name)
{
	USceneComponent* CarRoot = CreateDefaultSubobject<USceneComponent>(Name);
	CarRoot->SetupAttachment(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!CubeFinder.Succeeded())
	{
		return CarRoot;
	}

	struct FPart { const TCHAR* Suffix; FVector Pos; FVector Scale; FLinearColor Colour; };
	// Nerobergbahn-Wagen: blauer Kasten, cremefarbenes Dach, dunkle
	// Fensterbaender. Masse nach Vorbild rund 5,2 x 2,2 x 2,6 m.
	const FPart Parts[] = {
		{ TEXT("_Kasten"), FVector(0, 0, 90),  FVector(5.2f, 2.2f, 1.3f),
		  FLinearColor(0.02f, 0.10f, 0.30f) },
		{ TEXT("_Fenster"), FVector(0, 0, 185), FVector(4.8f, 2.24f, 0.7f),
		  FLinearColor(0.03f, 0.04f, 0.05f) },
		{ TEXT("_Dach"), FVector(0, 0, 235),  FVector(5.4f, 2.4f, 0.24f),
		  FLinearColor(0.75f, 0.72f, 0.62f) },
		{ TEXT("_Rahmen"), FVector(0, 0, 20), FVector(5.4f, 2.3f, 0.3f),
		  FLinearColor(0.05f, 0.05f, 0.05f) },
	};

	for (const FPart& Part : Parts)
	{
		UStaticMeshComponent* Piece = CreateDefaultSubobject<UStaticMeshComponent>(
			*(FString(Name) + Part.Suffix));
		Piece->SetupAttachment(CarRoot);
		Piece->SetStaticMesh(CubeFinder.Object);
		Piece->SetRelativeLocation(Part.Pos);
		Piece->SetRelativeScale3D(Part.Scale);
		// Kein Kollisionskoerper: der Fahrgast wird ANGEHAENGT, nicht
		// physikalisch mitgeschoben - ein kollidierender Wagen wuerde den
		// einsteigenden Spieler wegdruecken.
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		if (MatFinder.Succeeded())
		{
			// Dynamische Instanz erst in BeginPlay - im Konstruktor gibt es
			// dafuer keinen sicheren Aussenwelt-Zustand. Farbe kommt unten.
			Piece->SetMaterial(0, MatFinder.Object);
			Piece->ComponentTags.Add(*FString::Printf(TEXT("Farbe:%s"),
				*Part.Colour.ToString()));
		}
	}

	return CarRoot;
}

void AWiesbadenNerobergbahn::BeginPlay()
{
	Super::BeginPlay();

	// Wagenfarben: die Farbmarken aus dem Konstruktor in dynamische
	// Materialinstanzen uebersetzen.
	for (USceneComponent* Car : { CarA, CarB })
	{
		if (!Car) { continue; }
		// Nicht "Children" nennen: AActor hat einen gleichnamigen Member,
		// und der Uebersetzer behandelt das Ausblenden als Fehler.
		TArray<USceneComponent*> CarParts;
		Car->GetChildrenComponents(false, CarParts);
		for (USceneComponent* Child : CarParts)
		{
			UStaticMeshComponent* Piece = Cast<UStaticMeshComponent>(Child);
			if (!Piece) { continue; }
			for (const FName& Tag : Piece->ComponentTags)
			{
				const FString TagString = Tag.ToString();
				if (!TagString.StartsWith(TEXT("Farbe:"))) { continue; }
				FLinearColor Colour;
				if (Colour.InitFromString(TagString.Mid(6)))
				{
					if (UMaterialInstanceDynamic* Mid =
						Piece->CreateAndSetMaterialInstanceDynamic(0))
					{
						Mid->SetVectorParameterValue(TEXT("Color"), Colour);
					}
				}
			}
		}
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

	auto Build = [&](const FLatLon* Data, int32 Count, FTrack& Out)
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

		// Rueckfallhoehe: lineare Rampe ueber die 83 m des Vorbilds.
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
			// Hier standen 20.000 cm, also 200 m. Das klang grosszuegig - die
			// Talstation liegt bei 130 m - war aber der Grund, warum die
			// Bahn im Berg steckte: die Bergstation liegt bei 245 m, der
			// Trace begann dort also UNTERHALB der Gelaendeoberflaeche. Ein
			// Trace, der in der Geometrie startet, meldet diese Flaeche nicht;
			// er faellt durch und trifft irgendetwas Tieferes. Genau so kam
			// der gemessene Bereich 88 bis 173 m zustande statt 130 bis 240 m,
			// und der obere Teil der Trasse verschwand im Hang.
			//
			// 100.000 cm sind 1.000 m - deutlich ueber der Hohen Wurzel
			// (618 m), dem hoechsten Punkt im Stadtgebiet.
			constexpr double TraceTopCm = 100000.0;
			constexpr double TraceBottomCm = -20000.0;

			const FVector Start(Point.Position.X, Point.Position.Y, TraceTopCm);
			const FVector End(Point.Position.X, Point.Position.Y, TraceBottomCm);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(WbBahnHoehe), true);
			Params.AddIgnoredActor(this);

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

		// Die Bahn faehrt am SEIL, nicht durch Schlagloecher: die
		// Gelaendehoehen werden monoton geglaettet, damit die Trasse eine
		// stetige Steigung hat wie eine echte Standseilbahntrasse.
		for (FTrack* Track : { &TrackA, &TrackB })
		{
			for (int32 i = 1; i < Track->Points.Num(); ++i)
			{
				Track->Points[i].Position.Z = FMath::Max(
					Track->Points[i].Position.Z,
					Track->Points[i - 1].Position.Z);
			}
		}

		BuildTrackMeshes();

		// Hoehenbereich MELDEN.
		//
		// "Die Bahn ist nicht da" hat bisher keine pruefbare Zahl gehabt: das
		// Protokoll meldete Gleislaengen, und die entstehen aus den
		// Wegpunkten - auch dann, wenn die Trasse anschliessend unter dem
		// Gelaende liegt. Der Hoehenbereich sagt es sofort: die Talstation
		// liegt bei rund 130 m, die Bergstation bei rund 240 m. Steht hier
		// etwas nahe null, wurden die Hoehen vor dem Streaming abgefragt und
		// die Trasse steckt im Berg.
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

		// Und sofort sagen, wenn das Ergebnis nicht stimmen KANN.
		//
		// Die echte Bahn ueberwindet 83 m zwischen 130 m und 245 m. Weicht
		// das deutlich ab, ist die Trasse falsch abgetastet - und der Fehler
		// soll im Protokoll stehen, nicht erst im Spiel auffallen, wo er sich
		// nur als "die Bahn fehlt komplett" zeigt.
		constexpr double ExpectedTopM = 245.0;
		constexpr double ExpectedBottomM = 130.0;
		constexpr double ToleranceM = 25.0;
		if (FMath::Abs(MaxZ / 100.0 - ExpectedTopM) > ToleranceM
			|| FMath::Abs(MinZ / 100.0 - ExpectedBottomM) > ToleranceM)
		{
			UE_LOG(LogWbStreaming, Warning,
				TEXT("Nerobergbahn: Hoehen unplausibel - erwartet %.0f bis %.0f m ")
				TEXT("(+/- %.0f m), gemessen %.0f bis %.0f m. Die Trasse steckt ")
				TEXT("vermutlich im Gelaende."),
				ExpectedBottomM, ExpectedTopM, ToleranceM,
				MinZ / 100.0, MaxZ / 100.0);
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
			{ 130.0f, 0.0f, 6.0f,  FLinearColor(0.10f, 0.09f, 0.085f) },  // Bett
			{ 6.0f, -50.0f, 16.0f, FLinearColor(0.35f, 0.34f, 0.32f) },   // Schiene links
			{ 6.0f, +50.0f, 16.0f, FLinearColor(0.35f, 0.34f, 0.32f) },   // Schiene rechts
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
				const FVector Centre = P + Right * Ribbon.Offset + FVector(0, 0, Ribbon.Lift);

				Vertices.Add(Centre - Right * Ribbon.HalfWidth);
				Vertices.Add(Centre + Right * Ribbon.HalfWidth);
				Normals.Add(FVector::UpVector);
				Normals.Add(FVector::UpVector);
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

			TrackMesh->CreateMeshSection_LinearColor(
				Section++, Vertices, Triangles, Normals, UV0, Colours,
				TArray<FProcMeshTangent>(), /*bCreateCollision=*/false);
		}
	}
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
	PlaceCar(CarB, TrackB, TrackB.TotalLength - CablePosition
		* (TrackB.TotalLength / FMath::Max(TrackA.TotalLength, 1.0)));

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
	if (Passenger)
	{
		USceneComponent* Car = PassengerCar == 0 ? CarA : CarB;
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
		UE_LOG(LogWbStreaming, Log, TEXT("Nerobergbahn: Fahrgast ausgestiegen."));
		Passenger = nullptr;
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

		Passenger = Pawn;
		PassengerCar = i;
		Foot->SetRiding(true);
		Pawn->AttachToComponent(Cars[i], FAttachmentTransformRules::KeepWorldTransform);
		// In die Wagenmitte, Boden auf Bodenhoehe des Wagens.
		Pawn->SetActorLocation(Cars[i]->GetComponentLocation() + FVector(0, 0, 150.0f));
		UE_LOG(LogWbStreaming, Log, TEXT("Nerobergbahn: Fahrgast eingestiegen (Wagen %s)."),
			i == 0 ? TEXT("A") : TEXT("B"));
		return;
	}
}
