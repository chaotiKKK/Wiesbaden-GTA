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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "World/WiesbadenCitySubsystem.h"
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
		case EHqMaterial::Glass:   return TEXT("/Game/Materials/City/M_WbLmSlate.M_WbLmSlate");
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

	TArray<FHqPart> Teile;
	SebboHq::BuildShell(Dimensions, Teile);
	SebboHq::BuildVerticalCore(Dimensions, Teile);
	const FSebboHqArrivalLayout ArrivalLayout = SebboHq::BuildArrivalFacilities(Dimensions);
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
		Komponente->SetCollisionEnabled(Teil.Material == EHqMaterial::Marking
			? ECollisionEnabled::NoCollision
			: ECollisionEnabled::QueryAndPhysics);
		Komponente->RegisterComponent();
		Komponente->SetWorldLocation(BaseWorld + BaseYaw.RotateVector(Teil.CenterCm));
		Komponente->SetWorldRotation(BaseYaw);
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

	UE_LOG(LogWbSebboHq, Log,
		TEXT("Sebbo-Hauptsitz gebaut bei (%.0f, %.0f, %.0f): %d Bauteile, %d Geschosse, ")
		TEXT("%.0f m hoch, Landeplatz auf %.0f m."),
		BaseWorld.X, BaseWorld.Y, BaseWorld.Z, Parts.Num(), Dimensions.FloorCount,
		SebboHq::GetRoofHeightCm(Dimensions) / 100.0,
		SebboHq::GetHelipadHeightCm(Dimensions) / 100.0);
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

namespace
{
	/**
	 * Ein Schritt mit der Kapsel der Spielfigur - dieselbe Regel wie
	 * AWiesbadenFootPawn::TryStep: anheben, vorwaerts, absetzen. Nur wenn alle
	 * drei Teilstuecke frei sind, war es eine Stufe und keine Wand.
	 */
	bool KapselSchritt(const UWorld* World, const AActor* /*Selbst*/,
		const FVector& Von, const FVector& Richtung, double Weite,
		double MaxStufeCm, FVector& OutNach, FString& OutGrund)
	{
		OutNach = Von;
		if (!World)
		{
			OutGrund = TEXT("keine Welt");
			return false;
		}

		// KEIN AddIgnoredActor(Selbst)! Der Turm IST das, wogegen hier
		// getastet wird. Aus der Bodensuche uebernommen, wo das Ignorieren
		// richtig ist, hat es die Sonde blind gemacht: sie traf nur noch das
		// Landscape und meldete ueberall "nichts unter den Fuessen".
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WbTreppenProbe), false);
		const FCollisionShape Kapsel = FCollisionShape::MakeCapsule(40.0f, 90.0f);

		// bStartPenetrating NICHT als Wand werten.
		//
		// Die Kapsel steht mit ihrer Unterkante auf der Trittflaeche. Ein
		// Sweep, der beruehrend beginnt, meldet genau dort einen Treffer -
		// die erste Fassung der Sonde las das als "kein Kopfraum" und kam
		// keinen einzigen Schritt weit, obwohl die Treppe frei war.
		const auto Versperrt = [](const FHitResult& H)
		{
			return H.bBlockingHit && !H.bStartPenetrating;
		};

		FHitResult Treffer;
		const FVector Hoch = Von + FVector(0.0, 0.0, MaxStufeCm);
		if (World->SweepSingleByChannel(Treffer, Von, Hoch, FQuat::Identity,
			ECC_Pawn, Kapsel, Params) && Versperrt(Treffer))
		{
			OutGrund = FString::Printf(TEXT("kein Kopfraum (%s)"),
				*GetNameSafe(Treffer.GetActor()));
			return false;
		}

		const FVector Vor = Hoch + Richtung.GetSafeNormal() * Weite;
		if (World->SweepSingleByChannel(Treffer, Hoch, Vor, FQuat::Identity,
			ECC_Pawn, Kapsel, Params) && Versperrt(Treffer))
		{
			// MIT Namen: "eine Wand quer im Weg" sagt nicht, ob die Fassade,
			// das Gelaende oder ein Nachbarhaus im Weg stand - und genau das
			// ist die Frage, sobald die Sonde ausserhalb des Turms laeuft.
			OutGrund = FString::Printf(TEXT("eine Wand quer im Weg (%s)"),
				*GetNameSafe(Treffer.GetComponent()));
			return false;
		}

		// Absetzen: bis zu einer vollen Stufe nach unten suchen.
		const FVector Tief = Vor - FVector(0.0, 0.0, MaxStufeCm * 2.0);
		if (World->SweepSingleByChannel(Treffer, Vor, Tief, FQuat::Identity,
			ECC_Pawn, Kapsel, Params))
		{
			// EIN SCHRITT, DER IN DER GEOMETRIE ENDET, IST KEINER.
			//
			// GEMESSEN am 21.09.2026: der Fussweg meldete "Portal begehbar",
			// alle 14 Schritte, und endete 3,47 m UEBER dem Portalvolumen
			// (Volumenkoordinaten Z 347 bei halber Hoehe 90). Vor liegt eine
			// volle Stufe HOEHER als der Ausgangspunkt; steckte die Kapsel
			// dort, wurde sie bisher trotzdem dorthin gesetzt und meldete
			// Erfolg. Ueber 14 Schritte ratscht das 5,6 m nach oben - die
			// Sonde kletterte durch das Gebaeude und gab das als begangenen
			// Weg aus.
			//
			// Geprueft wird der Sweep-Start, NICHT eine Ueberlappung der
			// Endlage: eine Kapsel, die auf dem Landscape aufsetzt, meldet
			// dort regelmaessig Ueberlappung, und die erste Fassung dieser
			// Pruefung liess deshalb keinen einzigen Schritt mehr zu.
			if (Treffer.bStartPenetrating)
			{
				OutGrund = FString::Printf(TEXT("steckt in der Geometrie (%s)"),
					*GetNameSafe(Treffer.GetComponent()));
				return false;
			}
			OutNach = Treffer.Location;
			return true;
		}
		OutGrund = TEXT("nichts unter den Fuessen");
		// Nichts unter den Fuessen - das waere ein Loch, kein Schritt.
		return false;
	}
}

void AWiesbadenSebboHq::ProbeStaircase() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Die Masse des Treppenhauses noch einmal hier auszurechnen waere eine
	// zweite Wahrheit. Sie stehen darum in denselben Groessen wie im Bauteil:
	// Aussen = CoreCm/2, Wand 25, daraus Innen und die Quer-Teilung.
	const double Aussen = Dimensions.CoreCm * 0.5;
	const double Innen = Aussen - 25.0;
	const double Trennung = 12.5;
	const double TrennY = (-Innen + -Trennung) * 0.5;
	const double LaufY = (-Innen + 20.0 + TrennY - 5.0) * 0.5;   // Mitte des Laufs
	const double PodestY = (TrennY + -Trennung) * 0.5;           // Mitte des Podests
	const double LaufX0 = -Innen + 40.0;
	const double LaufX1 = Innen - 40.0;
	const double Kapselmitte = 90.0;
	const double MaxStufe = 40.0;      // wie AWiesbadenFootPawn::MaxStepHeightCm

	const FRotator Drehung(0.0, HeadingDegrees, 0.0);
	const FVector Fuss = BuiltBase;   // NICHT GetActorLocation(), siehe Header
	const auto NachWelt = [&](const FVector& Oertlich)
	{
		return Fuss + Drehung.RotateVector(Oertlich);
	};

	// WO FAENGT DER WEG AN? Nicht zwingend im Erdgeschoss.
	//
	// Der Turm setzt sich auf den Bodenpunkt seiner MITTE. Am Hang liegt das
	// Gelaende an der Treppenhausecke hoeher und schneidet durch die unteren
	// Stufen - die Sonde lief dort gegen das Landscape. Gemessen wird das
	// darum, statt es zu uebergehen: Bodenhoehe an der Treppe suchen und auf
	// der ersten Stufe darueber beginnen.
	double GelaendeUeberFuss = 0.0;
	{
		const FVector Oben = NachWelt(FVector(LaufX0, LaufY, 20000.0));
		FHitResult Boden;
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbTreppenProbeBoden), true);
		P.AddIgnoredActor(this);
		if (World->LineTraceSingleByChannel(Boden, Oben,
			Oben - FVector(0.0, 0.0, 40000.0), ECC_WorldStatic, P))
		{
			GelaendeUeberFuss = Boden.Location.Z - Fuss.Z;
		}
	}
	// Stufenlage GENAU wie im Bauteil (SebboHqShape::BuildVerticalCore).
	//
	// Eine eigene, aehnliche Rechnung reicht nicht: mit LaufX0 als Nullpunkt
	// und 16 gleichen Teilen lag die Sonde eine halbe Stufe daneben und stand
	// in der Luft ("nichts unter den Fuessen"). Die Stufen beginnen bei
	// -Innen + 20 und sind (Innen*2 - 40)/16 tief.
	const int32 Stufenzahl = 16;
	const double Stufenhoehe = Dimensions.FloorHeightCm / Stufenzahl;
	const double Auftritt = ((Innen * 2.0) - 40.0) / Stufenzahl;
	const auto StufeMitteX = [&](int32 k) { return -Innen + 20.0 + (k + 0.5) * Auftritt; };
	const auto StufeObenZ = [&](int32 k) { return 20.0 + (k + 1) * Stufenhoehe; };

	int32 ErsteStufe = 0;
	while (StufeObenZ(ErsteStufe) < GelaendeUeberFuss + 10.0 && ErsteStufe < Stufenzahl - 2)
	{
		++ErsteStufe;
	}
	// Zwei Zentimeter Luft: eine Kapsel, die die Flaeche genau beruehrt,
	// meldet beim Sweep sofort einen Treffer.
	const double StartHoehe = StufeObenZ(ErsteStufe) + 2.0;
	const double StartX = StufeMitteX(ErsteStufe);

	FVector Jetzt = NachWelt(FVector(StartX, LaufY, StartHoehe + Kapselmitte));
	const double StartZ = Jetzt.Z;

	UE_LOG(LogWbSebboHq, Log,
		TEXT("Treppenprobe: Gelaende an der Treppe %.0f cm ueber dem Fusspunkt, ")
		TEXT("Start auf Stufe %d (%.0f cm)."),
		GelaendeUeberFuss, ErsteStufe, StartHoehe);

	const FVector VorwaertsX = Drehung.RotateVector(FVector(1.0, 0.0, 0.0));
	const FVector QuerY = Drehung.RotateVector(FVector(0.0, 1.0, 0.0));

	int32 ErreichtesGeschoss = 0;
	int32 Schritte = 0;
	int32 Gescheitert = 0;
	FString Woran;

	// Je Geschoss: den Lauf hinauf, quer aufs Podest, ueber das Podest zurueck,
	// quer auf den naechsten Lauf. Genau der Weg, den ein Mensch geht.
	for (int32 Geschoss = 0; Geschoss < Dimensions.FloorCount; ++Geschoss)
	{
		bool bGeschossGeschafft = true;

		// 1) Den Lauf hinauf (in +X), Schrittweite 40 cm.
		const int32 SchritteLauf = FMath::CeilToInt((Innen - 20.0 - StartX) / 40.0);
		for (int32 i = 0; i < (Geschoss == 0 ? SchritteLauf
			: FMath::CeilToInt((LaufX1 - LaufX0) / 40.0)); ++i)
		{
			FVector Nach;
			FString Grund;
			if (!KapselSchritt(World, this, Jetzt, VorwaertsX, 40.0, MaxStufe, Nach, Grund))
			{
				bGeschossGeschafft = false;
				Woran = FString::Printf(
					TEXT("Lauf in Geschoss %d, Schritt %d von %d, Hoehe %.0f cm - %s"),
					Geschoss, i, SchritteLauf, Jetzt.Z - Fuss.Z, *Grund);
				break;
			}
			Jetzt = Nach;
			++Schritte;
		}
		if (!bGeschossGeschafft) { ++Gescheitert; break; }

		// Im obersten Geschoss endet der Weg auf dem Dachaufbau - kein
		// weiterer Lauf mehr.
		if (Geschoss + 1 >= Dimensions.FloorCount)
		{
			ErreichtesGeschoss = Geschoss + 1;
			break;
		}

		// 2) Quer auf das Podest des erreichten Geschosses.
		{
			const int32 SchritteQuer = FMath::CeilToInt(FMath::Abs(PodestY - LaufY) / 40.0);
			for (int32 i = 0; i < SchritteQuer; ++i)
			{
				FVector Nach;
				FString Grund;
				if (!KapselSchritt(World, this, Jetzt, QuerY, 40.0, MaxStufe, Nach, Grund))
				{
					bGeschossGeschafft = false;
					Woran = FString::Printf(TEXT("Uebertritt aufs Podest in Geschoss %d - %s"), Geschoss + 1, *Grund);
					break;
				}
				Jetzt = Nach;
				++Schritte;
			}
		}
		if (!bGeschossGeschafft) { ++Gescheitert; break; }

		// 3) Ueber das Podest zurueck zum Anfang des naechsten Laufs.
		{
			const int32 SchritteZurueck = FMath::CeilToInt((LaufX1 - LaufX0) / 40.0);
			for (int32 i = 0; i < SchritteZurueck; ++i)
			{
				FVector Nach;
				FString Grund;
				if (!KapselSchritt(World, this, Jetzt, -VorwaertsX, 40.0, MaxStufe, Nach, Grund))
				{
					bGeschossGeschafft = false;
					Woran = FString::Printf(TEXT("Podest in Geschoss %d - %s"), Geschoss + 1, *Grund);
					break;
				}
				Jetzt = Nach;
				++Schritte;
			}
		}
		if (!bGeschossGeschafft) { ++Gescheitert; break; }

		// 4) Quer zurueck auf den naechsten Lauf.
		{
			const int32 SchritteQuer = FMath::CeilToInt(FMath::Abs(PodestY - LaufY) / 40.0);
			for (int32 i = 0; i < SchritteQuer; ++i)
			{
				FVector Nach;
				FString Grund;
				if (!KapselSchritt(World, this, Jetzt, -QuerY, 40.0, MaxStufe, Nach, Grund))
				{
					bGeschossGeschafft = false;
					Woran = FString::Printf(TEXT("Uebertritt auf den Lauf in Geschoss %d - %s"), Geschoss + 1, *Grund);
					break;
				}
				Jetzt = Nach;
				++Schritte;
			}
		}
		if (!bGeschossGeschafft) { ++Gescheitert; break; }

		ErreichtesGeschoss = Geschoss + 1;
	}

	const double Gestiegen = Jetzt.Z - StartZ;
	const double Sollhoehe = SebboHq::GetRoofHeightCm(Dimensions);

	// GESCHOSSE AUS DER HOEHE, nicht aus dem Schleifenzaehler.
	//
	// Der Zaehler zaehlte Durchlaeufe, nicht Geschosse: er meldete "3 von 15",
	// waehrend die Kapsel nachweislich 57,7 m gestiegen war. Die erreichte
	// Hoehe ist die einzige Zahl, die hier etwas aussagt - aus ihr folgt das
	// Geschoss, nicht umgekehrt.
	const double ErreichteHoehe = Jetzt.Z - Fuss.Z;
	ErreichtesGeschoss = FMath::Clamp(
		FMath::FloorToInt(ErreichteHoehe / Dimensions.FloorHeightCm), 0, Dimensions.FloorCount);
	const bool bDachErreicht = ErreichteHoehe >= Sollhoehe - Dimensions.FloorHeightCm;

	UE_LOG(LogWbSebboHq, Log,
		TEXT("Treppenprobe: %d von %d Geschossen erreicht, %d Schritte, ")
		TEXT("%.1f m gestiegen (Ziel %.1f m).%s%s"),
		ErreichtesGeschoss, Dimensions.FloorCount, Schritte,
		Gestiegen / 100.0, Sollhoehe / 100.0,
		bDachErreicht ? TEXT(" DACH ERREICHT. Ende: ") : TEXT(" GESCHEITERT an: "),
		*Woran);

	// Ergebnis auch als Datei - eine Log-Zeile geht in 200.000 anderen unter.
	const FString Pfad = FPaths::ProjectSavedDir() / TEXT("Diagnose") / TEXT("treppenprobe.json");
	const FString Inhalt = FString::Printf(
		TEXT("{\n \"geschosse_erreicht\": %d,\n \"geschosse_gesamt\": %d,\n")
		TEXT(" \"schritte\": %d,\n \"gestiegen_m\": %.2f,\n \"dachhoehe_m\": %.2f,\n")
		TEXT(" \"gelaende_ueber_fusspunkt_cm\": %.0f,\n \"erreichte_hoehe_m\": %.2f,\n")
		TEXT(" \"dach_erreicht\": %s,\n \"ende\": \"%s\"\n}\n"),
		ErreichtesGeschoss, Dimensions.FloorCount, Schritte,
		Gestiegen / 100.0, Sollhoehe / 100.0, GelaendeUeberFuss, ErreichteHoehe / 100.0,
		bDachErreicht ? TEXT("true") : TEXT("false"), *Woran);
	FFileHelper::SaveStringToFile(Inhalt, *Pfad);
}

namespace
{
	/** Ergebnis eines abgetasteten Ankunftswegs. */
	struct FWbAnkunftsweg
	{
		bool bFrei = false;
		double WegCm = 0.0;
		FString Woran;
	};

	/** Liegt der Weltpunkt im Zielvolumen? Genau die Frage, die der Overlap stellt. */
	bool ImZielvolumen(const UBoxComponent* Volumen, const FVector& Punkt)
	{
		if (!Volumen)
		{
			return false;
		}
		const FVector Oertlich =
			Volumen->GetComponentTransform().InverseTransformPosition(Punkt);
		const FVector Halb = Volumen->GetScaledBoxExtent();
		return FMath::Abs(Oertlich.X) <= Halb.X
			&& FMath::Abs(Oertlich.Y) <= Halb.Y
			&& FMath::Abs(Oertlich.Z) <= Halb.Z;
	}

	/** Ueberschneiden sich zwei Zielvolumen? Dann setzte eine Ankunft zwei Zustaende. */
	bool VolumenUeberschneiden(const UBoxComponent* A, const UBoxComponent* B)
	{
		if (!A || !B)
		{
			return false;
		}
		return A->Bounds.GetBox().Intersect(B->Bounds.GetBox());
	}
}

void AWiesbadenSebboHq::ProbeArrival() const
{
	const UWorld* World = GetWorld();
	if (!World || !bBuilt)
	{
		return;
	}

	// DIESELBE Form wie der Bau, nicht eine zweite Rechnung daneben.
	const FSebboHqArrivalLayout Layout = SebboHq::BuildArrivalFacilities(Dimensions);
	const double Half = Dimensions.FootprintCm * 0.5 + FMath::Max(0.0, Dimensions.PodiumOversizeCm);
	const double BodenZ = Dimensions.SlabCm + 15.0;     // Oberkante des privaten Bodens
	const double Kapselmitte = 90.0;
	const double MaxStufe = 40.0;                       // wie AWiesbadenFootPawn

	const FRotator Drehung(0.0, HeadingDegrees, 0.0);
	const FVector Fuss = BuiltBase;
	const auto NachWelt = [&](const FVector& Oertlich)
	{
		return Fuss + Drehung.RotateVector(Oertlich);
	};

	// Strassenhoehe VOR dem Haus. Der Turm sitzt auf dem Bodenpunkt seiner
	// Mitte; die Platter Strasse liegt nicht zwingend auf derselben Hoehe.
	//
	// NUR 4 m ueber dem Fusspunkt ansetzen, nicht 200. Von ganz oben trifft
	// das Lot das erste Beste - im ersten Lauf ein Dach 5,7 m UEBER dem
	// Fusspunkt, woraus ein Bordstein von -510 cm wurde. Gesucht ist der
	// Belag, auf dem ein Wagen steht, also das, was unter Wagenhoehe liegt.
	FString BelagGarage;
	FString BelagPortal;
	const auto StrasseUeberFuss = [&](double Y, double& OutZ, FString& OutBelag)
	{
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbAnkunftProbeBoden), true);
		P.AddIgnoredActor(this);   // hier IST das Ignorieren richtig: gesucht ist der Belag

		// Von 4 m ueber dem Fusspunkt, und wenn dort nichts liegt, noch einmal
		// von 40 m. Das Gelaende steigt zur Garagenseite an: die enge Sonde
		// startete UNTER dem Belag und meldete "nichts gefunden", die weite
		// allein traf zuerst ein Nachbardach.
		FHitResult Boden;
		bool bGetroffen = false;
		for (const double StartHoehe : { 400.0, 4000.0 })
		{
			const FVector Oben = NachWelt(FVector(Half + 800.0, Y, StartHoehe));
			if (World->LineTraceSingleByChannel(Boden, Oben,
				Oben - FVector(0.0, 0.0, StartHoehe + 6000.0), ECC_WorldStatic, P))
			{
				bGetroffen = true;
				break;
			}
		}
		if (!bGetroffen)
		{
			OutBelag = TEXT("nichts gefunden");
			return false;
		}
		OutZ = Boden.Location.Z - Fuss.Z;
		OutBelag = GetNameSafe(Boden.GetActor());
		return true;
	};

	// Die Bodenhoehen ZUERST - beide Wege setzen darauf auf.
	double StrasseGarageZ = 0.0;
	double StrassePortalZ = 0.0;
	const double GarageY = Layout.GarageTarget.CenterCm.Y;
	const double PortalY = Layout.PedestrianTarget.CenterCm.Y;
	const bool bStrasseGarage = StrasseUeberFuss(GarageY, StrasseGarageZ, BelagGarage);
	const bool bStrassePortal = StrasseUeberFuss(PortalY, StrassePortalZ, BelagPortal);
	const double StufeGarageCm = BodenZ - StrasseGarageZ;
	const double StufePortalCm = BodenZ - StrassePortalZ;

	// --- 1) Auto: durch die Garagenoeffnung -----------------------------------
	//
	// Ein Quader in Fahrzeuggroesse. KEIN AddIgnoredActor: der Turm ist genau
	// das, wogegen getastet wird - eine Oeffnung, die nur gemalt ist, faellt
	// hier auf.
	double AutoTrefferWeltZCm = 0.0;
	double AutoQuaderUnterkanteCm = 0.0;
	double AutoBelagZCm = 0.0;
	FString AutoBelagName = TEXT("-");
	FWbAnkunftsweg Wagenweg;
	{
		// 5 cm Luft ueber dem hoeheren Belag: ein Quader, der die Bodenplatte
		// genau beruehrt, meldete deren 15-cm-Lippe als versperrte Einfahrt.
		const double WagenZ = FMath::Max(StrasseGarageZ, BodenZ) + 80.0;
		const FVector Start = NachWelt(FVector(Half + 700.0, GarageY, WagenZ));
		const FVector Ziel = NachWelt(FVector(Layout.GarageTarget.CenterCm.X, GarageY, WagenZ));
		const FCollisionShape Wagen = FCollisionShape::MakeBox(FVector(225.0, 100.0, 75.0));
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbAnkunftProbeAuto), false);
		FHitResult Treffer;
		// ECC_WorldStatic, nicht ECC_Vehicle: dieses Projekt richtet den
		// Fahrzeugkanal nirgends ein: die gebaute Geometrie antwortet dort
		// nicht, und die Sonde meldete jede Wand als freie Durchfahrt.
		//
		// Der STARTPUNKT wird zuerst eigens geprueft. Ein Sweep, der bereits
		// steckend beginnt, meldet bStartPenetrating - das als "nicht
		// versperrt" zu lesen war die Regel aus der Treppensonde, wo die
		// Kapsel die Stufe beruehrt. Hier stand der Quader im Hang, und die
		// Sonde meldete dafuer "Durchfahrt frei".
		const bool bStartSteckt = World->OverlapBlockingTestByChannel(Start,
			Drehung.Quaternion(), ECC_WorldStatic, Wagen, P);
		if (bStartSteckt)
		{
			Wagenweg.bFrei = false;
			Wagenweg.WegCm = 0.0;
			Wagenweg.Woran = TEXT("Startpunkt steckt im Gelaende - keine Zufahrt auf dieser Hoehe");
		}
		else
		{
			const bool bGetroffen = World->SweepSingleByChannel(Treffer, Start, Ziel,
				Drehung.Quaternion(), ECC_WorldStatic, Wagen, P);
			Wagenweg.bFrei = !(bGetroffen && Treffer.bBlockingHit);
			Wagenweg.WegCm = Wagenweg.bFrei ? (Ziel - Start).Size() : Treffer.Distance;
			Wagenweg.Woran = Wagenweg.bFrei ? TEXT("Durchfahrt frei")
				: FString::Printf(TEXT("versperrt durch %s"), *GetNameSafe(Treffer.GetComponent()));

			// WO stoesst er an, und was liegt dort? Ohne diese Zahlen ist
			// "versperrt" nicht von "die Sonde faehrt zu tief" zu trennen.
			if (!Wagenweg.bFrei)
			{
				AutoTrefferWeltZCm = Treffer.Location.Z;
				AutoQuaderUnterkanteCm = Start.Z - 75.0;
				FHitResult Darunter;
				const FVector Lot(Treffer.Location.X, Treffer.Location.Y, Treffer.Location.Z + 3000.0);
				if (World->LineTraceSingleByChannel(Darunter, Lot,
					Lot - FVector(0.0, 0.0, 6000.0), ECC_WorldStatic, P))
				{
					AutoBelagZCm = Darunter.Location.Z;
					AutoBelagName = GetNameSafe(Darunter.GetComponent());
				}
			}
		}
	}

	// --- 2) Fuss: durch das Portal --------------------------------------------
	FWbAnkunftsweg Fussweg;
	FVector FussEnde = FVector::ZeroVector;
	{
		// AUF DER STRASSE starten, nicht auf dem privaten Boden. Der erste
		// Lauf setzte die Kapsel auf Portalhoehe an, waehrend der Gehweg
		// 1,66 m tiefer lag - sie meldete im ersten Schritt "nichts unter
		// den Fuessen" und das sah aus wie ein verbautes Portal.
		const double StartZ = (bStrassePortal ? StrassePortalZ : BodenZ) + 2.0;
		FVector Jetzt = NachWelt(FVector(Half + 400.0, PortalY, StartZ + Kapselmitte));
		const FVector Start = Jetzt;
		const FVector NachInnen = Drehung.RotateVector(FVector(-1.0, 0.0, 0.0));
		const double ZielX = Layout.PedestrianTarget.CenterCm.X;
		const int32 SchritteSoll = FMath::CeilToInt((Half + 400.0 - ZielX) / 40.0);
		Fussweg.bFrei = true;
		for (int32 i = 0; i < SchritteSoll; ++i)
		{
			FVector Nach;
			FString Grund;
			if (!KapselSchritt(World, this, Jetzt, NachInnen, 40.0, MaxStufe, Nach, Grund))
			{
				Fussweg.bFrei = false;
				Fussweg.Woran = FString::Printf(TEXT("Schritt %d von %d - %s"),
					i, SchritteSoll, *Grund);
				break;
			}
			Jetzt = Nach;
		}
		FussEnde = Jetzt;
		Fussweg.WegCm = (Jetzt - Start).Size2D();
		if (Fussweg.bFrei)
		{
			Fussweg.Woran = TEXT("Portal begehbar");
		}
	}

	// --- 3) Helikopter: Lot auf den Landeplatz --------------------------------
	//
	// Zwei Fragen, nicht eine: liegt unter dem Ziel wirklich das Pad, und ist
	// der Luftraum darueber frei? Ein Lot allein beantwortet nur die erste.
	FWbAnkunftsweg Heliweg;
	double PadHoeheCm = 0.0;
	{
		const double PadSoll = SebboHq::GetHelipadHeightCm(Dimensions);
		const FVector Mitte = Layout.HelicopterTarget.CenterCm;
		const FVector Oben = NachWelt(FVector(Mitte.X, Mitte.Y, PadSoll + 15000.0));
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbAnkunftProbeHeli), false);

		FHitResult Lot;
		const bool bPad = World->LineTraceSingleByChannel(Lot, Oben,
			NachWelt(FVector(Mitte.X, Mitte.Y, -1000.0)), ECC_WorldStatic, P);
		PadHoeheCm = bPad ? (Lot.Location.Z - Fuss.Z) : -1.0;

		// Anflug mit Rotorradius statt mit einem Strich.
		FHitResult Anflug;
		const FCollisionShape Rotor =
			FCollisionShape::MakeSphere(static_cast<float>(Dimensions.HelipadDiameterCm * 0.35));
		// Der Anflug endet, wo die Rotorebene ueber dem Platz schwebt - eine
		// Kugel, die bis 4 m ueber die Flaeche faehrt, ragt mit ihrem unteren
		// Rand hinein und meldete den Landeplatz selbst als Hindernis.
		const double SchwebeZ = PadSoll + Rotor.GetSphereRadius() + 100.0;
		const bool bLuftraum = !World->SweepSingleByChannel(Anflug, Oben,
			NachWelt(FVector(Mitte.X, Mitte.Y, SchwebeZ)),
			FQuat::Identity, ECC_WorldStatic, Rotor, P);

		const bool bPadTrifft = bPad && FMath::Abs(PadHoeheCm - PadSoll) <= 40.0;
		Heliweg.bFrei = bPadTrifft && bLuftraum;
		Heliweg.WegCm = PadHoeheCm;
		Heliweg.Woran = !bPad ? FString(TEXT("kein Landeplatz unter dem Ziel"))
			: !bPadTrifft ? FString::Printf(
				TEXT("Aufsetzpunkt %.0f cm statt %.0f cm"), PadHoeheCm, PadSoll)
			: !bLuftraum ? FString::Printf(TEXT("Anflug versperrt durch %s"),
				*GetNameSafe(Anflug.GetComponent()))
			: FString(TEXT("Landeplatz frei"));
	}

	// --- 3b) Das Gelaende rings um den Grundriss ------------------------------
	//
	// Zwei Punkte (Garage, Portal) sagen, DASS es nicht passt. Ein Plateau
	// braucht die ganze Verteilung: wieviel muss abgegraben, wieviel
	// aufgefuellt werden, und auf welcher Hoehe wird beides am kleinsten.
	double GelaendeMinCm = TNumericLimits<double>::Max();
	double GelaendeMaxCm = -TNumericLimits<double>::Max();
	double GelaendeSummeCm = 0.0;
	int32 GelaendePunkte = 0;
	FString GelaendeSchnitt;
	{
		constexpr int32 Schritte = 24;
		const double Ring = Half + 300.0;
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbAnkunftProbeRing), true);
		P.AddIgnoredActor(this);
		for (int32 i = 0; i < Schritte; ++i)
		{
			const double Winkel = 2.0 * PI * i / Schritte;
			const FVector Punkt(Ring * FMath::Cos(Winkel), Ring * FMath::Sin(Winkel), 0.0);
			FHitResult Boden;
			bool bGetroffen = false;
			for (const double StartHoehe : { 400.0, 4000.0 })
			{
				const FVector Oben = NachWelt(FVector(Punkt.X, Punkt.Y, StartHoehe));
				if (World->LineTraceSingleByChannel(Boden, Oben,
					Oben - FVector(0.0, 0.0, StartHoehe + 6000.0), ECC_WorldStatic, P))
				{
					bGetroffen = true;
					break;
				}
			}
			if (!bGetroffen)
			{
				continue;
			}
			const double H = Boden.Location.Z - Fuss.Z;
			// Der ganze Schnitt, nicht nur seine Spannweite: aus Min/Max
			// allein ist ein Strassenanschnitt nicht von einer gleichmaessigen
			// Hangneigung zu unterscheiden.
			GelaendeSchnitt += FString::Printf(TEXT("%s{\"grad\": %.0f, \"cm\": %.0f, \"was\": \"%s\"}"),
				GelaendePunkte > 0 ? TEXT(", ") : TEXT(""),
				FMath::RadiansToDegrees(Winkel), H, *GetNameSafe(Boden.GetActor()));
			GelaendeMinCm = FMath::Min(GelaendeMinCm, H);
			GelaendeMaxCm = FMath::Max(GelaendeMaxCm, H);
			GelaendeSummeCm += H;
			++GelaendePunkte;
		}
	}
	const double GelaendeMittelCm = GelaendePunkte > 0 ? GelaendeSummeCm / GelaendePunkte : 0.0;
	if (GelaendePunkte == 0)
	{
		GelaendeMinCm = 0.0;
		GelaendeMaxCm = 0.0;
	}

	// --- 4) Die Naht: erreicht der Weg auch das Zielvolumen? ------------------
	//
	// Getastet wurde gegen die gebaute Geometrie; den Ankunftszustand setzt
	// aber das Volumen. Beide muessen dasselbe meinen.
	const FVector GarageZielWelt = NachWelt(FVector(
		Layout.GarageTarget.CenterCm.X, GarageY, BodenZ + 75.0));
	const bool bGarageTrifftVolumen = ImZielvolumen(GarageArrivalVolume, GarageZielWelt);
	const bool bFussTrifftVolumen = ImZielvolumen(PedestrianArrivalVolume, FussEnde);

	// GENAU DIE ZAHLEN, die ImZielvolumen vergleicht - keine nachgerechneten.
	// Der Widerspruch "rechnerisch drin, gemessen draussen" laesst sich nur so
	// aufloesen: Endpunkt in Volumenkoordinaten gegen die halbe Ausdehnung.
	FVector FussImVolumen = FVector::ZeroVector;
	FVector PortalHalb = FVector::ZeroVector;
	FVector PortalWeltMitte = FVector::ZeroVector;
	if (PedestrianArrivalVolume)
	{
		FussImVolumen = PedestrianArrivalVolume->GetComponentTransform()
			.InverseTransformPosition(FussEnde);
		PortalHalb = PedestrianArrivalVolume->GetScaledBoxExtent();
		PortalWeltMitte = PedestrianArrivalVolume->GetComponentLocation();
	}
	const bool bHeliTrifftVolumen = ImZielvolumen(HelipadArrivalVolume,
		NachWelt(Layout.HelicopterTarget.CenterCm));
	const bool bZieleGetrennt =
		!VolumenUeberschneiden(GarageArrivalVolume, PedestrianArrivalVolume)
		&& !VolumenUeberschneiden(GarageArrivalVolume, HelipadArrivalVolume)
		&& !VolumenUeberschneiden(PedestrianArrivalVolume, HelipadArrivalVolume);

	UE_LOG(LogWbSebboHq, Log,
		TEXT("Ankunftsprobe: Auto %s (%s), Fuss %s (%s), Heli %s (%s). ")
		TEXT("Bordstein Garage %.0f cm, Portal %.0f cm. ")
		TEXT("Volumen getroffen: Auto %s, Fuss %s, Heli %s; getrennt: %s."),
		Wagenweg.bFrei ? TEXT("FREI") : TEXT("BLOCKIERT"), *Wagenweg.Woran,
		Fussweg.bFrei ? TEXT("FREI") : TEXT("BLOCKIERT"), *Fussweg.Woran,
		Heliweg.bFrei ? TEXT("FREI") : TEXT("BLOCKIERT"), *Heliweg.Woran,
		StufeGarageCm, StufePortalCm,
		bGarageTrifftVolumen ? TEXT("ja") : TEXT("nein"),
		bFussTrifftVolumen ? TEXT("ja") : TEXT("nein"),
		bHeliTrifftVolumen ? TEXT("ja") : TEXT("nein"),
		bZieleGetrennt ? TEXT("ja") : TEXT("nein"));

	const FString Pfad = FPaths::ProjectSavedDir() / TEXT("Diagnose") / TEXT("ankunftsprobe.json");
	const auto JaNein = [](bool b) { return b ? TEXT("true") : TEXT("false"); };
	const FString Inhalt = FString::Printf(
		TEXT("{\n \"auto\": { \"frei\": %s, \"weg_cm\": %.0f, \"woran\": \"%s\" },\n")
		TEXT(" \"fuss\": { \"frei\": %s, \"weg_cm\": %.0f, \"woran\": \"%s\" },\n")
		TEXT(" \"heli\": { \"frei\": %s, \"aufsetzhoehe_cm\": %.0f, \"woran\": \"%s\" },\n")
		TEXT(" \"bordstein_garage_cm\": %.0f,\n \"bordstein_portal_cm\": %.0f,\n")
		TEXT(" \"belag_garage\": \"%s\",\n \"belag_portal\": \"%s\",\n")
		TEXT(" \"strasse_gefunden\": { \"garage\": %s, \"portal\": %s },\n")
		TEXT(" \"volumen_getroffen\": { \"auto\": %s, \"fuss\": %s, \"heli\": %s },\n")
		TEXT(" \"volumen_getrennt\": %s,\n")
		TEXT(" \"fuss_ende_welt\": [%.0f, %.0f, %.0f],\n")
		TEXT(" \"portal_volumen\": {\"mitte\": [%.0f, %.0f, %.0f], \"halb\": [%.0f, %.0f, %.0f]},\n")
		TEXT(" \"fuss_in_volumenkoordinaten\": [%.0f, %.0f, %.0f],\n")
		TEXT(" \"auto_treffer\": {\"z_cm\": %.0f, \"quader_unterkante_cm\": %.0f, ")
		TEXT("\"belag_z_cm\": %.0f, \"belag\": \"%s\"},\n")
		TEXT(" \"gelaende_ring\": { \"punkte\": %d, \"min_cm\": %.0f, \"max_cm\": %.0f, \"mittel_cm\": %.0f },\n")
		TEXT(" \"gelaende_schnitt\": [%s]\n}\n"),
		JaNein(Wagenweg.bFrei), Wagenweg.WegCm, *Wagenweg.Woran,
		JaNein(Fussweg.bFrei), Fussweg.WegCm, *Fussweg.Woran,
		JaNein(Heliweg.bFrei), Heliweg.WegCm, *Heliweg.Woran,
		StufeGarageCm, StufePortalCm, *BelagGarage, *BelagPortal,
		JaNein(bStrasseGarage), JaNein(bStrassePortal),
		JaNein(bGarageTrifftVolumen), JaNein(bFussTrifftVolumen), JaNein(bHeliTrifftVolumen),
		JaNein(bZieleGetrennt),
		FussEnde.X, FussEnde.Y, FussEnde.Z,
		PortalWeltMitte.X, PortalWeltMitte.Y, PortalWeltMitte.Z,
		PortalHalb.X, PortalHalb.Y, PortalHalb.Z,
		FussImVolumen.X, FussImVolumen.Y, FussImVolumen.Z,
		AutoTrefferWeltZCm, AutoQuaderUnterkanteCm, AutoBelagZCm, *AutoBelagName,
		GelaendePunkte, GelaendeMinCm, GelaendeMaxCm, GelaendeMittelCm, *GelaendeSchnitt);
	FFileHelper::SaveStringToFile(Inhalt, *Pfad);
}
