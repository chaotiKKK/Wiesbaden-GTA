// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenSebboHq.h"

#include "GIS/GeoCoordinateConverter.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "LandscapeProxy.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "CollisionQueryParams.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "World/WiesbadenCitySubsystem.h"
#include "World/WiesbadenSebboHqElevator.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "GIS/RoadNetworkGenerator.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbSebboHq, Log, All);

namespace
{
	/** Die Zufahrt wird mit dem World-Builder gebacken. Der Tower muss deshalb
	 * dieselbe Kartenreferenz benutzen, auch wenn die Map den Standard-Origin
	 * bewusst ueberschreibt. */
	bool InitializeTowerConverter(UGeoCoordinateConverter& Converter, UWorld& World)
	{
		for (TActorIterator<AWiesbadenWorldBuilder> It(&World); It; ++It)
		{
			return It->bUseWiesbadenOrigin
				? Converter.InitializeWithWiesbadenOrigin()
				: Converter.Initialize(It->CustomOrigin);
		}

		return Converter.InitializeWithWiesbadenOrigin();
	}
}

namespace
{
	/** Materialpfade je Werkstoff - dieselben Stadt-Materialien wie die Wahrzeichen. */
	const TCHAR* MaterialPath(EHqMaterial Material)
	{
		switch (Material)
		{
		case EHqMaterial::Glass:   return TEXT("/Game/Materials/City/M_WbFacade_Glas.M_WbFacade_Glas");
		case EHqMaterial::Metal:   return TEXT("/Game/Materials/City/M_WbLmSlate.M_WbLmSlate");
		case EHqMaterial::Marking: return TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite");
		default:                   return TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite");
		}
	}
}

AWiesbadenSebboHq::AWiesbadenSebboHq()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AWiesbadenSebboHq::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	Materials.SetNum(static_cast<int32>(EHqMaterial::MAX));
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		Materials[i] = LoadObject<UMaterialInterface>(
			nullptr, MaterialPath(static_cast<EHqMaterial>(i)));
	}

	UGeoCoordinateConverter* Own = NewObject<UGeoCoordinateConverter>(this);
	UWorld* World = GetWorld();
	if (!World || !InitializeTowerConverter(*Own, *World))
	{
		UE_LOG(LogWbSebboHq, Error, TEXT("Tower-Georeferenzierung konnte nicht initialisiert werden."));
		return;
	}
	Converter = Own;
}

bool AWiesbadenSebboHq::ResolveGround(const FVector& WorldXY, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbSebboHqGround), true);
	Params.AddIgnoredActor(this);
	const FVector Start(WorldXY.X, WorldXY.Y, 100000.0);
	const FVector End(WorldXY.X, WorldXY.Y, -20000.0);

	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByChannel(Hits, Start, End, ECC_WorldStatic, Params))
	{
		return false;
	}

	// DAS GELAENDE, nicht das Erste unter dem Himmel.
	//
	// GEMESSEN am 21.09.2026 auf Alkis17: der Turm baute auf 10251 cm,
	// waehrend sein eigenes Bauplateau auf 10048 cm liegt - 2,03 m zu hoch.
	// Der Einzeltaster nahm den obersten Treffer, und ueber dem Grundstueck
	// liegt Stadtgeometrie: Wolkenbruch quert es, und Nachbardaecher reichen
	// bis 16296 cm. Der Turm stellte sich damit auf eine Fahrbahn statt auf
	// den Boden, den der Bake eigens fuer ihn eingeebnet hat.
	//
	// Das Plateau IST das Landscape. Darum wird die ganze Saeule gelesen und
	// der Landscape-Treffer gewaehlt.
	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bStartPenetrating && Cast<ALandscapeProxy>(Hit.GetActor()))
		{
			OutZ = Hit.Location.Z;
			return true;
		}
	}

	// Kein Landscape in der Saeule (ungebackene Karte, Probelevel): dann der
	// oberste brauchbare Treffer wie bisher. NICHT aufgeben - ein Turm, der
	// gar nicht baut, waere schlechter als einer, der zu hoch steht.
	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bStartPenetrating)
		{
			OutZ = Hit.Location.Z;
			return true;
		}
	}
	return false;
}

void AWiesbadenSebboHq::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBuilt)
	{
		// Treppenprobe NACH dem Bauen, nicht im selben Bild (siehe
		// SecondsSinceBuild): die Kollisionskoerper brauchen einen Takt.
		if (!bProbed && SecondsSinceBuild >= 0.0f)
		{
			SecondsSinceBuild += DeltaSeconds;
			if (SecondsSinceBuild >= 2.0f)
			{
				bProbed = true;
				if (FParse::Param(FCommandLine::Get(), TEXT("WbTreppenProbe")))
				{
					ProbeStaircase();
				}
				if (FParse::Param(FCommandLine::Get(), TEXT("WbAnkunftProbe")))
				{
					ProbeArrival();
				}
				SetActorTickEnabled(false);
			}
		}
		return;
	}
	if (!Converter)
	{
		return;
	}

	FGeoCoordinate Coord;
	Coord.Latitude = PlotLatitude;
	Coord.Longitude = PlotLongitude;
	Coord.Height = 0.0;
	const FVector Ground = Converter->GeoToUnrealGround(Coord);

	// Der Taster sagt nur, DASS die Zelle da ist - die Hoehe kommt aus der
	// Zufahrt. Beides zu trennen ist noetig: das Plateau waere sofort
	// bekannt (das Strassennetz liegt serialisiert im WorldBuilder), aber ein
	// Turm, der vor dem Streaming baut, laesst die Ankunftssonde in eine
	// leere Welt tasten.
	double BodenZ = 0.0;
	if (!ResolveGround(Ground, BodenZ))
	{
		return;     // Zelle noch nicht gestreamt - naechster Tick
	}

	double Z = BodenZ;
	if (!ResolvePlateau(Z))
	{
		Z = BodenZ;
	}

	Build(FVector(Ground.X, Ground.Y, Z), FRotator(0.0, HeadingDegrees, 0.0));
	bBuilt = true;

	// -WbTreppenProbe steigt einmal die Treppe hoch, -WbAnkunftProbe tastet die
	// drei Ankunftswege ab. Beides sind Messwerkzeuge - ohne einen der beiden
	// Schalter tickt der Actor gar nicht weiter.
	const bool bSondeGewuenscht =
		FParse::Param(FCommandLine::Get(), TEXT("WbTreppenProbe"))
		|| FParse::Param(FCommandLine::Get(), TEXT("WbAnkunftProbe"));
	if (bSondeGewuenscht)
	{
		SecondsSinceBuild = 0.0f;
	}
	else
	{
		SetActorTickEnabled(false);
	}
}

bool AWiesbadenSebboHq::ResolvePlateau(double& OutZ) const
{
	const UWorld* World = GetWorld();
	if (!World || !Converter)
	{
		return false;
	}

	FGeoCoordinate Coord;
	Coord.Latitude = PlotLatitude;
	Coord.Longitude = PlotLongitude;
	Coord.Height = 0.0;
	const FVector GrundXY = Converter->GeoToUnrealGround(Coord);
	const FRotator Drehung(0.0, HeadingDegrees, 0.0);
	const FSebboHqArrivalLayout Layout = SebboHq::BuildArrivalFacilities(Dimensions);

	// DIESELBEN Ankerpunkte, die ConfigureSebboHqPad dem Bake uebergibt. Nur
	// ihr XY zaehlt - die Hoehe kommt aus der Fahrbahn.
	FRoadAccessOverride Zugang;
	Zugang.bEnabled = true;
	Zugang.GarageEntranceWorldCm = GrundXY + Drehung.RotateVector(Layout.GarageTarget.CenterCm);
	Zugang.PedestrianEntranceWorldCm = GrundXY + Drehung.RotateVector(Layout.PedestrianTarget.CenterCm);
	Zugang.SearchRadiusCm = 5000.0;
	Zugang.PreferredStreetName = SebboHqSite::AccessRoadName;

	for (TActorIterator<AWiesbadenWorldBuilder> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->RoadNetwork.Segments.Num() == 0)
		{
			continue;
		}
		const FResolvedRoadAccess Zugeloest =
			URoadNetworkGenerator::ResolveRoadAccess(It->RoadNetwork, Zugang);
		if (!Zugeloest.Garage.IsValid())
		{
			continue;
		}
		OutZ = Zugeloest.Garage.RoadPointCm.Z - SebboHq::GetAccessFloorCm(Dimensions);
		return true;
	}
	return false;
}

void AWiesbadenSebboHq::Build(const FVector& BaseWorld, const FRotator& BaseYaw)
{
	BuiltBase = BaseWorld;

	// ANSCHLUSS AN DIE PLATTER STRASSE MESSEN.
	//
	// Die Fahrbahn faellt an den Oeffnungen entlang rund 6 Prozent; ein Deck
	// auf fester Hoehe endet dort mit einer Kante bis 15 cm. Darum tastet der
	// Actor die Oberflaeche an der Deckenkante ab und reicht sie dem Builder
	// je Y-Spalte - das Deck laeuft dann stufenlos auf Strassenniveau zu
	// ("ebenerdig mit der Platter Strasse"). Ohne Treffer bleibt alles auf
	// dem nominellen Boden.
	const double FloorZ = SebboHq::GetAccessFloorCm(Dimensions);
	const double HalfLocal = Dimensions.FootprintCm * 0.5
		+ FMath::Max(0.0, Dimensions.PodiumOversizeCm);
	SebboHq::FSebboHqAnschluss Anschluss;
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WbSebboAnschluss), true);
		const auto Taste = [this, &BaseWorld, &BaseYaw, &Params, FloorZ](
			double XLocal, double YLocal) -> double
		{
			const FVector Oben = BaseWorld + BaseYaw.RotateVector(FVector(XLocal, YLocal, 0.0))
				+ FVector(0.0, 0.0, 800.0);
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByChannel(Hit, Oben,
				Oben - FVector(0.0, 0.0, 1600.0), ECC_WorldStatic, Params))
			{
				// Baum, Mauer oder Absperrung neben der Deckenkante wuerde das
				// Deck sonst unnoetig hochziehen - nur plausible Strassenhoehen
				// durchlassen.
				return FMath::Clamp(Hit.ImpactPoint.Z - BaseWorld.Z,
					FloorZ - 120.0, FloorZ + 60.0);
			}
			return FloorZ;
		};

		const FSebboHqArrivalLayout Ziele = SebboHq::BuildArrivalFacilities(Dimensions);
		const auto SpaltenAus = [&Taste](TArray<double>& Reihe, double XLocal,
			const FHqArrivalTarget& Ziel, double RandZuschlag)
		{
			const double Rand = Ziel.ExtentCm.Y + RandZuschlag;
			for (int32 s = 0; s < 3; ++s)
			{
				const double YLocal = Ziel.CenterCm.Y - Rand + 2.0 * Rand * (s + 0.5) / 3.0;
				Reihe.Add(Taste(XLocal, YLocal));
			}
		};
		SpaltenAus(Anschluss.GarageZCm, HalfLocal + SebboHq::GarageBridgeLengthCm,
			Ziele.GarageTarget, 30.0);
		SpaltenAus(Anschluss.PortalZCm, HalfLocal + SebboHq::PedestrianBridgeLengthCm,
			Ziele.PedestrianTarget, 10.0);

		UE_LOG(LogWbSebboHq, Log,
			TEXT("Sebbo-Zufahrt: Anschluss Garage %.0f/%.0f/%.0f cm, Portal %.0f/%.0f/%.0f cm ")
			TEXT("(Boden %.0f cm)."),
			Anschluss.GarageZCm[0], Anschluss.GarageZCm[1], Anschluss.GarageZCm[2],
			Anschluss.PortalZCm[0], Anschluss.PortalZCm[1], Anschluss.PortalZCm[2], FloorZ);
	}

	TArray<FHqPart> Teile;
	SebboHq::BuildShell(Dimensions, Teile);
	SebboHq::BuildVerticalCore(Dimensions, Teile);
	const FSebboHqArrivalLayout ArrivalLayout =
		SebboHq::BuildArrivalFacilities(Dimensions, &Anschluss);
	Teile.Append(ArrivalLayout.Parts);

	for (const FHqPart& Teil : Teile)
	{
		UStaticMesh* Mesh = Teil.Primitive == EHqPrimitive::Cylinder ? CylinderMesh : CubeMesh;
		if (!Mesh)
		{
			continue;
		}
		UStaticMeshComponent* Komponente = NewObject<UStaticMeshComponent>(this);
		Komponente->SetStaticMesh(Mesh);
		Komponente->SetupAttachment(Root);
		// MIT Kollision, anders als die Wahrzeichen: auf diesem Dach soll der
		// Helikopter aufsetzen und der Spieler herumlaufen koennen.
		//
		// AUSSER Markierungen: die sind Farbe. Als 6 cm hohe Quader mit
		// Kollision war der Haltstreifen vor der Garage eine Schwelle quer in
		// der Einfahrt - die Laufzeit-Sonde blieb mit dem Fahrzeugquader
		// daran haengen, noch bevor sie die Oeffnung erreicht hatte.
		Komponente->SetCollisionEnabled(Teil.Material == EHqMaterial::Marking || !Teil.bCollision
			? ECollisionEnabled::NoCollision
			: ECollisionEnabled::QueryAndPhysics);
		Komponente->RegisterComponent();
		Komponente->SetWorldLocation(BaseWorld + BaseYaw.RotateVector(Teil.CenterCm));
		// Teilweise gedrehte Teile (Handlauf): erst das Teil in sich drehen,
		// dann den Turm in die Welt stellen. FRotator besitzt kein operator*
		// fuer Komposition - Quaternionen: Q1 * Q2 wendet Q2 zuerst an.
		Komponente->SetWorldRotation(
			FRotator(BaseYaw.Quaternion() * Teil.Rotation.Quaternion()));
		// Engine-Cube und -Cylinder sind 100 cm gross und um den Ursprung
		// zentriert - die Skalierung ist darum schlicht Groesse/100.
		Komponente->SetWorldScale3D(Teil.SizeCm / 100.0);
		if (Materials.IsValidIndex(static_cast<int32>(Teil.Material)))
		{
			if (UMaterialInterface* Material = Materials[static_cast<int32>(Teil.Material)])
			{
				Komponente->SetMaterial(0, Material);
			}
		}
		Parts.Add(Komponente);
	}

	// DACHAUFBAUTEN: Werbe-Logo, Antennen und Satellitenschuessel sind
	// BLENDER-Assets (Tools/Blender/make_sebbo_dach.py, Import ueber
	// Tools/import_sebbo_dach.py) und keine Primitive. Ohne die importierten
	// Meshes (frischer Checkout) faellt der Turm nicht aus - die Silhouette
	// fehlt dann nur, mit Warnung im Log.
	TArray<SebboHq::FSebboHqDachProp> DachProps;
	SebboHq::BuildDachaufbauten(Dimensions, DachProps);
	for (const SebboHq::FSebboHqDachProp& Prop : DachProps)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Prop.MeshPfad);
		if (!Mesh)
		{
			UE_LOG(LogWbSebboHq, Warning, TEXT("Dach-Asset fehlt: %s"), *Prop.MeshPfad);
			continue;
		}
		UStaticMeshComponent* Komponente = NewObject<UStaticMeshComponent>(this);
		Komponente->SetStaticMesh(Mesh);
		Komponente->SetupAttachment(Root);
		// MIT Kollision wie die Huellflaechen: auf dem Dach laeuft der Spieler
		// herum, Mast und Schuessel sollen nicht passierbar sein.
		Komponente->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Komponente->RegisterComponent();
		Komponente->SetWorldLocation(BaseWorld + BaseYaw.RotateVector(Prop.PosCm));
		Komponente->SetWorldRotation(BaseYaw + FRotator(0.0, Prop.YawDeg, 0.0));
		Parts.Add(Komponente);
	}

	CreateArrivalVolume(GarageArrivalVolume, TEXT("GarageArrival"),
		ArrivalLayout.GarageTarget, BaseWorld, BaseYaw);
	CreateArrivalVolume(PedestrianArrivalVolume, TEXT("PedestrianArrival"),
		ArrivalLayout.PedestrianTarget, BaseWorld, BaseYaw);
	CreateArrivalVolume(HelipadArrivalVolume, TEXT("HelipadArrival"),
		ArrivalLayout.HelicopterTarget, BaseWorld, BaseYaw);

	FHqArrivalTarget GarageYieldTarget = ArrivalLayout.GarageTarget;
	GarageYieldTarget.CenterCm.X += GarageYieldTarget.ExtentCm.X - 60.0;
	GarageYieldTarget.ExtentCm.X = 60.0;
	CreateArrivalVolume(GarageYieldVolume, TEXT("GarageYield"),
		GarageYieldTarget, BaseWorld, BaseYaw);

	CreateGuidanceLight(GarageGuidanceLight, TEXT("GarageGuidance"),
		GarageYieldTarget.CenterCm + FVector(0.0, 0.0, 180.0), BaseWorld, BaseYaw,
		FLinearColor(1.0f, 0.65f, 0.05f), 3500.0f, 1800.0f);
	CreateGuidanceLight(PortalGuidanceLight, TEXT("PortalGuidance"),
		ArrivalLayout.PedestrianTarget.CenterCm + FVector(0.0, 0.0, 200.0), BaseWorld, BaseYaw,
		FLinearColor(0.65f, 0.85f, 1.0f), 2500.0f, 1400.0f);
	CreateGuidanceLight(HelipadGuidanceLight, TEXT("HelipadGuidance"),
		ArrivalLayout.HelicopterTarget.CenterCm + FVector(0.0, 0.0, 150.0), BaseWorld, BaseYaw,
		FLinearColor(0.25f, 1.0f, 0.5f), 6000.0f, 2600.0f);

	FActorSpawnParameters LiftSpawn;
	LiftSpawn.Owner = this;
	Elevator = GetWorld()->SpawnActor<AWiesbadenSebboHqElevator>(BaseWorld, BaseYaw, LiftSpawn);
	if (Elevator)
	{
		Elevator->Initialize(Dimensions);
	}

	UE_LOG(LogWbSebboHq, Log,
		TEXT("Sebbo-Hauptsitz gebaut bei (%.0f, %.0f, %.0f): %d Bauteile, %d Geschosse, ")
		TEXT("%.0f m hoch, Landeplatz auf %.0f m."),
		BaseWorld.X, BaseWorld.Y, BaseWorld.Z, Parts.Num(), Dimensions.FloorCount,
		SebboHq::GetRoofHeightCm(Dimensions) / 100.0,
		SebboHq::GetHelipadHeightCm(Dimensions) / 100.0);
}

FVector AWiesbadenSebboHq::GetHelipadWorldLocation() const
{
	// DERSELBE Ausdruck, mit dem oben das Ankunftsvolumen und das Lande-
	// licht gesetzt werden: Fusspunkt des Turms plus der Gierdrehung
	// folgende lokale Lage des Landeplatzes. Ein eigener Ausdruck hier waere
	// die dritte Kopie derselben Zahl.
	const FSebboHqArrivalLayout Layout = SebboHq::BuildArrivalFacilities(Dimensions);
	const FRotator Drehung(0.0, HeadingDegrees, 0.0);
	return BuiltBase + Drehung.RotateVector(Layout.HelicopterTarget.CenterCm);
}

void AWiesbadenSebboHq::CreateArrivalVolume(UBoxComponent*& OutVolume, FName Name,
	const FHqArrivalTarget& Target, const FVector& BaseWorld, const FRotator& BaseYaw)
{
	UBoxComponent* Volume = NewObject<UBoxComponent>(this, Name);
	Volume->SetupAttachment(Root);
	Volume->SetBoxExtent(Target.ExtentCm);
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionResponseToAllChannels(ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
	Volume->OnComponentBeginOverlap.AddDynamic(this, &AWiesbadenSebboHq::OnArrivalTargetOverlap);
	Volume->RegisterComponent();
	Volume->SetWorldLocation(BaseWorld + BaseYaw.RotateVector(Target.CenterCm));
	Volume->SetWorldRotation(BaseYaw);
	OutVolume = Volume;
}

void AWiesbadenSebboHq::CreateGuidanceLight(UPointLightComponent*& OutLight, FName Name,
	const FVector& LocalPosition, const FVector& BaseWorld, const FRotator& BaseYaw,
	const FLinearColor& Color, float Intensity, float Radius)
{
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this, Name);
	Light->SetupAttachment(Root);
	Light->SetLightColor(Color);
	Light->SetIntensity(Intensity);
	Light->SetAttenuationRadius(Radius);
	Light->SetCastShadows(false);
	Light->SetVolumetricScatteringIntensity(2.0f);
	Light->RegisterComponent();
	Light->SetWorldLocation(BaseWorld + BaseYaw.RotateVector(LocalPosition));
	OutLight = Light;
}

bool AWiesbadenSebboHq::HasCrossTraffic(const FVector& WorldPosition) const
{
	const UWorld* World = GetWorld();
	const UWiesbadenCitySubsystem* City = World ? World->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	if (!City)
	{
		return false;
	}

	constexpr double ConflictRadiusCm = 1800.0;
	for (const FTrafficVehicle& Vehicle : City->GetTrafficVehicles())
	{
		const FVector& VehiclePosition = Vehicle.bBodyInitialized ? Vehicle.BodyLocation : Vehicle.Location;
		if (FVector::DistSquared2D(WorldPosition, VehiclePosition) <= FMath::Square(ConflictRadiusCm))
		{
			return true;
		}
	}
	return false;
}

void AWiesbadenSebboHq::OnArrivalTargetOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* /*OtherComponent*/, int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController || OtherActor != PlayerController->GetPawn())
	{
		return;
	}
	if (OverlappedComponent == GarageYieldVolume)
	{
		const bool bCrossTraffic = HasCrossTraffic(GarageYieldVolume->GetComponentLocation());
		if (GarageGuidanceLight)
		{
			GarageGuidanceLight->SetLightColor(bCrossTraffic ? FLinearColor::Red : FLinearColor::Green);
		}
		UE_LOG(LogWbSebboHq, Log, TEXT("Tower-Zufahrt: %s auf der Platter Strasse."),
			bCrossTraffic ? TEXT("warte auf Querverkehr") : TEXT("Fahrbahn frei"));
		return;
	}

	if (OverlappedComponent == GarageArrivalVolume)
	{
		ArrivalTarget = ESebboHqArrivalTarget::Garage;
	}
	else if (OverlappedComponent == PedestrianArrivalVolume)
	{
		ArrivalTarget = ESebboHqArrivalTarget::Pedestrian;
	}
	else if (OverlappedComponent == HelipadArrivalVolume)
	{
		ArrivalTarget = ESebboHqArrivalTarget::Helipad;
	}
	else
	{
		return;
	}

	UE_LOG(LogWbSebboHq, Log, TEXT("Tower-Ankunft erreicht: %s"),
		*UEnum::GetValueAsString(ArrivalTarget));
}
