// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenDennoShop.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "World/WiesbadenCityChunk.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "CollisionQueryParams.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbDennoShop, Log, All);

namespace
{
	// Schwerpunkt des OSM-Umrisses 175418681 (Sedanplatz 5).
	constexpr double RefLongitude = 8.22958757;
	constexpr double RefLatitude = 50.08294914;
	// Strassenfront in Ost/Nord-Metern um den Schwerpunkt: Sued- und Nordecke.
	const FVector2D FrontSouthEN(-6.22, -7.39);
	const FVector2D FrontNorthEN(-7.61, 7.30);


	const TCHAR* FacadePrefix = TEXT("M_WbFacade_");
}

AWiesbadenDennoShop::AWiesbadenDennoShop()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("DennoShopRoot"));
	RootComponent = Root;
}

FVector AWiesbadenDennoShop::CutHalfExtentCm()
{
	return FVector(ShopHalfWidthCm, CutHalfDepthCm, (CutTopCm - CutBottomCm) * 0.5);
}

EDennoShopBuild AWiesbadenDennoShop::DecideBuild(bool bWallFound, double WaitedSeconds)
{
	if (bWallFound)
	{
		return EDennoShopBuild::Build;
	}
	return WaitedSeconds < WallWaitSeconds ? EDennoShopBuild::Wait : EDennoShopBuild::GiveUp;
}

bool AWiesbadenDennoShop::IsPlausibleWall(const FVector& Hit, const FVector& OsmFrontMid,
	const FVector& Outward)
{
	const FVector2D D(Hit.X - OsmFrontMid.X, Hit.Y - OsmFrontMid.Y);
	const FVector2D Out = FVector2D(Outward.X, Outward.Y).GetSafeNormal();
	return FMath::Abs(FVector2D::DotProduct(D, Out)) <= WallToleranceCm;
}

bool AWiesbadenDennoShop::IsInsideCut(const FVector& Point, const FVector& Centre,
	const FVector2D& AxisU, const FVector& HalfExtent)
{
	// Spiegelbild des HLSL in create_facade_cut_materials.py.
	const FVector D = Point - Centre;
	const FVector2D U = AxisU.GetSafeNormal();
	const double Lu = D.X * U.X + D.Y * U.Y;
	const double Ln = D.X * -U.Y + D.Y * U.X;
	return FMath::Abs(Lu) < HalfExtent.X && FMath::Abs(Ln) < HalfExtent.Y
		&& FMath::Abs(D.Z) < HalfExtent.Z;
}

FDennoIdlePose AWiesbadenDennoShop::ComputeDennoIdle(double Seconds)
{
	auto Wave = [Seconds](double Period, double Phase)
	{
		return FMath::Sin(UE_DOUBLE_TWO_PI * Seconds / Period + Phase);
	};
	FDennoIdlePose Pose;
	// Atem: 0 = ausgeatmet (Grundstellung), 1 = eingeatmet.
	const double Breath = 0.5 - 0.5 * FMath::Cos(UE_DOUBLE_TWO_PI * Seconds / BreathPeriodSeconds);
	Pose.Scale = FVector(1.0 + BreathWidth * Breath, 1.0 + BreathWidth * Breath, 1.0 + BreathRise * Breath);
	// Gewicht verlagern und umschauen: Perioden ohne gemeinsamen Takt, damit
	// sich die Bewegung nicht sichtbar alle paar Sekunden wiederholt.
	Pose.Rotation.Roll = SwayRollDeg * Wave(7.3, 0.0);
	Pose.Rotation.Pitch = SwayPitchDeg * Wave(9.7, 1.3);
	Pose.Rotation.Yaw = LookAroundDeg * (0.65 * Wave(19.0, 0.0) + 0.35 * Wave(31.0, 2.0));
	return Pose;
}

void AWiesbadenDennoShop::BeginPlay()
{
	Super::BeginPlay();
	Converter = NewObject<UGeoCoordinateConverter>(this);
	bool bGeoreferenced = false;
	for (TActorIterator<AWiesbadenWorldBuilder> It(GetWorld()); It; ++It)
	{
		bGeoreferenced = It->bUseWiesbadenOrigin
			? Converter->InitializeWithWiesbadenOrigin()
			: Converter->Initialize(It->CustomOrigin);
		break;
	}
	if (!bGeoreferenced)
	{
		Converter->InitializeWithWiesbadenOrigin();
	}
	TryBuild();
}

void AWiesbadenDennoShop::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		TryBuild();
		return;
	}
	// Die Chunk-Zelle kann spaeter gestreamt oder neu geladen werden - dann
	// traegt sie wieder die opaken Fassaden. Darum im Sekundentakt nachsehen.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now >= NextPatchSeconds)
	{
		NextPatchSeconds = Now + 1.0;
		PatchFacades();
	}
	// Denno bewegt sich nur, wenn jemand hinsieht - sonst kostet sie nichts.
	if (DennoFigure && DennoFigure->WasRecentlyRendered(0.25f))
	{
		const FDennoIdlePose Pose = ComputeDennoIdle(Now);
		DennoFigure->SetRelativeRotation(
			FRotator(Pose.Rotation.Pitch, DennoYawDeg + Pose.Rotation.Yaw, Pose.Rotation.Roll));
		DennoFigure->SetRelativeScale3D(Pose.Scale);
	}
}

FVector AWiesbadenDennoShop::WorldXY(const FVector2D& EastNorthM) const
{
	const FGeoCoordinate Coord(
		RefLongitude + EastNorthM.X / UGeoCoordinateConverter::MetersPerDegreeLongitude(RefLatitude),
		RefLatitude + EastNorthM.Y / UGeoCoordinateConverter::MetersPerDegreeLatitude(RefLatitude),
		0.0);
	return Converter->GeoToUnrealGround(Coord);
}

bool AWiesbadenDennoShop::TryBuild()
{
	UWorld* World = GetWorld();
	if (!World || !Converter)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (FirstAttemptSeconds < 0.0)
	{
		FirstAttemptSeconds = Now;
	}

	const FVector2D MidEN = (FrontSouthEN + FrontNorthEN) * 0.5;
	const FVector2D AlongEN = (FrontNorthEN - FrontSouthEN).GetSafeNormal();
	const FVector2D OutEN(-AlongEN.Y, AlongEN.X);   // nach Westen, zum Platz
	const FVector Mid = WorldXY(MidEN);
	const FVector AxisU = (WorldXY(MidEN + AlongEN) - Mid).GetSafeNormal2D();
	const FVector Outward = (WorldXY(MidEN + OutEN) - Mid).GetSafeNormal2D();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbDennoShop), true);
	Params.AddIgnoredActor(this);

	// Boden vor der Front (Gehweg): erster Treffer von oben.
	const FVector Walk = Mid + Outward * 150.0;
	FHitResult Ground;
	if (!World->LineTraceSingleByChannel(Ground, FVector(Walk.X, Walk.Y, 100000.0),
		FVector(Walk.X, Walk.Y, -20000.0), ECC_WorldStatic, Params))
	{
		return false;   // Zelle noch nicht gestreamt
	}
	const double FloorZ = Ground.ImpactPoint.Z;

	// Die gebackene Wand selbst: waagerechter Strahl von der Strasse ins Haus.
	FVector Wall;
	FHitResult WallHit;
	const bool bWall = World->LineTraceSingleByChannel(WallHit,
		FVector(Mid.X, Mid.Y, FloorZ + 150.0) + Outward * 600.0,
		FVector(Mid.X, Mid.Y, FloorZ + 150.0) - Outward * 300.0, ECC_WorldStatic, Params)
		&& !Cast<ALandscapeProxy>(WallHit.GetActor())
		&& IsPlausibleWall(WallHit.ImpactPoint, Mid, Outward);
	switch (DecideBuild(bWall, Now - FirstAttemptSeconds))
	{
	case EDennoShopBuild::Wait:
		return false;   // Haus noch nicht gestreamt - weiter warten
	case EDennoShopBuild::GiveUp:
		// Ohne Hauswand (andere Karte, Haus fehlt, anderer Bau) KEIN Laden -
		// frueher entstand er dann auf der OSM-Linie und schwebte im Leeren.
		UE_LOG(LogWbDennoShop, Warning,
			TEXT("Dennos Laden: keine Hauswand von Sedanplatz 5 nach %.0f s - der Laden entsteht nicht."),
			WallWaitSeconds);
		SetActorTickEnabled(false);
		return false;
	case EDennoShopBuild::Build:
		break;
	}
	Wall = FVector(WallHit.ImpactPoint.X, WallHit.ImpactPoint.Y, 0.0);

	const float Yaw = static_cast<float>(AxisU.Rotation().Yaw);

	// Das Gelaende steigt unter dem Haus an: im ersten Bild stach hinten im
	// Friseur das Gras durch den Ladenboden. Hoechsten Gelaendepunkt unter dem
	// Raum messen und den Laden darueber heben - vorn schliesst eine Stufe die
	// Luecke zum Gehweg (wie bei den Gruenderzeit-Laeden am Platz ueblich).
	// Der Strahl beginnt IM Haus unter dem Dach, sonst endet er am Dach.
	const FVector Inward(-Outward.X, -Outward.Y, 0.0);
	double TerrainTop = FloorZ;
	for (double U = -640.0; U <= 640.0; U += 80.0)
	{
		for (double N = 40.0; N <= 640.0; N += 60.0)
		{
			const FVector P = FVector(Wall.X, Wall.Y, FloorZ) + AxisU * U + Inward * N;
			// Direkt am Landscape abfragen: ein Strahl von oben endet am Dach,
			// einer aus dem Haus heraus steckt in dessen Kollision und findet
			// das Gelaende nie (erster Versuch: 3 cm statt der sichtbaren Wiese).
			for (TActorIterator<ALandscapeProxy> Land(World); Land; ++Land)
			{
				const TOptional<float> H = Land->GetHeightAtLocation(P);
				if (H.IsSet())
				{
					TerrainTop = FMath::Max(TerrainTop, static_cast<double>(H.GetValue()));
				}
			}
		}
	}
	const double LiftCm = FMath::Max(0.0, TerrainTop - FloorZ + 3.0);
	const double ShopFloorZ = FloorZ + LiftCm;
	SetActorLocationAndRotation(FVector(Wall.X, Wall.Y, ShopFloorZ), FRotator(0.0f, Yaw, 0.0f));
	if (LiftCm > 1.0)
	{
		// Stufe ueber die ganze Front: vom Gehweg bis zur Ladenschwelle.
		if (UStaticMeshComponent* Step = AddPart(TEXT("DennoShopStep"),
			TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.0, -20.0, -LiftCm * 0.5), 0.0f))
		{
			Step->SetRelativeScale3D(FVector(14.1, 0.60, LiftCm / 100.0));
			if (UMaterialInterface* Stone = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/Materials/City/M_WbLmSlate.M_WbLmSlate")))
			{
				Step->SetMaterial(0, Stone);
			}
		}
	}

	AddPart(TEXT("DennoShopShell"), TEXT("/Game/Buildings/DennoShop/Meshes/SM_DennoShop_Shell.SM_DennoShop_Shell"), FVector::ZeroVector, 0.0f);
	AddPart(TEXT("DennoShopCafe"), TEXT("/Game/Buildings/DennoShop/Meshes/SM_DennoShop_Cafe.SM_DennoShop_Cafe"), FVector::ZeroVector, 0.0f);
	AddPart(TEXT("DennoShopSalon"), TEXT("/Game/Buildings/DennoShop/Meshes/SM_DennoShop_Salon.SM_DennoShop_Salon"), FVector::ZeroVector, 0.0f);
	AddPart(TEXT("DennoShopGlass"), TEXT("/Game/Buildings/DennoShop/Meshes/SM_DennoShop_Glass.SM_DennoShop_Glass"), FVector::ZeroVector, 0.0f);
	DennoFigure = AddPart(TEXT("Denno"), TEXT("/Game/Buildings/DennoShop/Meshes/SM_Denno.SM_Denno"),
		FVector(DennoXCm, DennoYCm, 0.0), DennoYawDeg);

	// Warmes Ladenlicht: je Laden zwei Leuchten unter der Decke. Ohne sie
	// waere der Raum hinter der Scheibe ein schwarzes Loch - die Stadt hat
	// kein Lumen, das Tageslicht faellt nicht in den Kasten.
	const FVector LightSpots[] = {
		{ 220.0, 220.0, 270.0 }, { 520.0, 430.0, 270.0 },
		{ -220.0, 220.0, 270.0 }, { -520.0, 430.0, 270.0 } };
	for (const FVector& Spot : LightSpots)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
		Light->SetupAttachment(Root);
		Light->SetRelativeLocation(Spot);
		Light->SetIntensity(Spot.X > 0 ? 2600.0f : 3000.0f);
		Light->SetAttenuationRadius(650.0f);
		Light->SetLightColor(Spot.X > 0 ? FLinearColor(1.0f, 0.80f, 0.58f) : FLinearColor(1.0f, 0.93f, 0.84f));
		// MIT Schatten: ohne sie schien das Ladenlicht durch die Decke auf die
		// Hausfront ueber dem Schild (heller Fleck im ersten Bild).
		Light->SetCastShadows(true);
		Light->RegisterComponent();
		Lights.Add(Light);
	}

	const FVector Half = CutHalfExtentCm();
	CutCentre = FVector(Wall.X, Wall.Y, ShopFloorZ + (CutTopCm + CutBottomCm) * 0.5);
	CutAxisU = FVector2D(AxisU.X, AxisU.Y);
	bBuilt = true;
	// Ab jetzt jedes Bild (Dennos Bewegung); die Fassaden-Nachkontrolle bleibt
	// ueber NextPatchSeconds im Sekundentakt.
	SetActorTickInterval(0.0f);
	const int32 Patched = PatchFacades();
	UE_LOG(LogWbDennoShop, Log,
		TEXT("Dennos Laden Sedanplatz 5: Front bei (%.0f, %.0f, %.0f), Ladenboden %.0f cm ueber dem Gehweg, Gier %.1f; %d Chunk-Komponente(n) ausgeschnitten (Kasten %.0f x %.0f x %.0f cm)."),
		Wall.X, Wall.Y, FloorZ, LiftCm, Yaw, Patched, Half.X * 2.0, Half.Y * 2.0, Half.Z * 2.0);
	return true;
}

UStaticMeshComponent* AWiesbadenDennoShop::AddPart(const TCHAR* Name, const TCHAR* MeshPath,
	const FVector& LocalCm, float LocalYaw)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
	if (!Mesh)
	{
		UE_LOG(LogWbDennoShop, Warning, TEXT("Dennos Laden: Mesh fehlt: %s (Tools/import_denno_shop.py)."), MeshPath);
		return nullptr;
	}
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, Name);
	Part->SetupAttachment(Root);
	Part->SetStaticMesh(Mesh);
	Part->SetRelativeLocation(LocalCm);
	Part->SetRelativeRotation(FRotator(0.0f, LocalYaw, 0.0f));
	// Keine Kollision: die gebackene Hauskollision bleibt ohnehin bestehen,
	// der Laden ist ein Blick durchs Schaufenster.
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->RegisterComponent();
	Parts.Add(Part);
	return Part;
}

int32 AWiesbadenDennoShop::PatchFacades()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}
	const FVector Half = CutHalfExtentCm();
	const FBox CutBox = FBox::BuildAABB(CutCentre, FVector(Half.X, Half.X, Half.Z));
	int32 Before = PatchedFacades.Num();
	for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
	{
		TInlineComponentArray<UMeshComponent*> Meshes(*It);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (!Mesh || PatchedFacades.Contains(Mesh) || !Mesh->Bounds.GetBox().Intersect(CutBox))
			{
				continue;
			}
			PatchComponent(Mesh);
		}
	}
	// Ersetzte (entladene) Komponenten vergessen.
	for (auto It = PatchedFacades.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
	return PatchedFacades.Num() - Before;
}

void AWiesbadenDennoShop::PatchComponent(UMeshComponent* Mesh)
{
	bool bAny = false;
	const FVector Half = CutHalfExtentCm();
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		UMaterialInterface* Current = Mesh->GetMaterial(Slot);
		if (!Current)
		{
			continue;
		}
		// Bei einer dynamischen Instanz (Fensterlicht) zaehlt die Vorlage.
		UMaterialInterface* Base = Current;
		if (UMaterialInstanceDynamic* Dyn = Cast<UMaterialInstanceDynamic>(Current))
		{
			Base = Dyn->Parent ? Dyn->Parent.Get() : Current;
		}
		const FString BaseName = Base->GetName();
		if (!BaseName.StartsWith(FacadePrefix) || BaseName.EndsWith(TEXT("_Cut")))
		{
			continue;
		}
		const FString CutPath = FString::Printf(TEXT("/Game/Materials/City/Cut/%s_Cut.%s_Cut"), *BaseName, *BaseName);
		UMaterialInterface* CutParent = LoadObject<UMaterialInterface>(nullptr, *CutPath);
		if (!CutParent)
		{
			UE_LOG(LogWbDennoShop, Warning, TEXT("Dennos Laden: %s fehlt (Tools/create_facade_cut_materials.py)."), *CutPath);
			continue;
		}
		// Name nach der Vorlage: ApplyWindowLight leitet die Nutzungsart aus dem
		// Materialnamen ab - ein generischer MID-Name verfaelschte das Fensterlicht.
		UMaterialInstanceDynamic* Cut = UMaterialInstanceDynamic::Create(CutParent, Mesh,
			MakeUniqueObjectName(Mesh, UMaterialInstanceDynamic::StaticClass(), FName(*BaseName)));
		// Das Fensterlicht (AWiesbadenCityChunk::ApplyWindowLight) bleibt erhalten.
		float WindowLight = 0.0f;
		if (Current->GetScalarParameterValue(FName(TEXT("FensterlichtStaerke")), WindowLight))
		{
			Cut->SetScalarParameterValue(TEXT("FensterlichtStaerke"), WindowLight);
		}
		Cut->SetVectorParameterValue(TEXT("ShopCenter"), FLinearColor(CutCentre.X, CutCentre.Y, CutCentre.Z, 0.0f));
		Cut->SetVectorParameterValue(TEXT("ShopAxisU"), FLinearColor(CutAxisU.X, CutAxisU.Y, 0.0f, 0.0f));
		Cut->SetVectorParameterValue(TEXT("ShopHalf"), FLinearColor(Half.X, Half.Y, Half.Z, 0.0f));
		Mesh->SetMaterial(Slot, Cut);
		bAny = true;
	}
	if (bAny)
	{
		PatchedFacades.Add(Mesh);
		UE_LOG(LogWbDennoShop, Log, TEXT("Dennos Laden: Fassade von %s ausgeschnitten."),
			*Mesh->GetOwner()->GetName());
	}
}
