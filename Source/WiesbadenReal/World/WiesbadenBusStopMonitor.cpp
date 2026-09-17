// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenBusStopMonitor.h"

#include "World/WiesbadenRailTransport.h"
#include "GIS/GeoCoordinateConverter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbMonitor, Log, All);

AWiesbadenBusStopMonitor::AWiesbadenBusStopMonitor()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AWiesbadenBusStopMonitor::LoadLine()
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data/Raw/Bus") / LineFile;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path)) { return; }
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) { return; }
	auto ReadPairs = [](const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FVector2D>& Out)
	{
		if (!Arr) { return; }
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
			if (V.IsValid() && V->TryGetArray(P) && P && P->Num() >= 2)
			{
				Out.Add(FVector2D((*P)[0]->AsNumber(), (*P)[1]->AsNumber()));
			}
		}
	};
	const TArray<TSharedPtr<FJsonValue>>* PathArr = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* StopArr = nullptr;
	Obj->TryGetArrayField(TEXT("path"), PathArr);
	Obj->TryGetArrayField(TEXT("stops"), StopArr);
	ReadPairs(PathArr, GeoPath);
	ReadPairs(StopArr, GeoStops);
}

void AWiesbadenBusStopMonitor::LoadSchedule()
{
	Schedule.DepartureSeconds.Reset();
	Schedule.DaySeconds = 86400.0;
	const FString Path = FPaths::ProjectDir() / TEXT("Data/Raw/Bus") / ScheduleFile;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path)) { return; }
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) { return; }
	double DayS = 86400.0;
	Obj->TryGetNumberField(TEXT("day_seconds"), DayS);
	Schedule.DaySeconds = (DayS > 0.0) ? DayS : 86400.0;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Obj->TryGetArrayField(TEXT("weekday"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString HHMM;
			if (V.IsValid() && V->TryGetString(HHMM))
			{
				int32 Colon = INDEX_NONE;
				if (HHMM.FindChar(TEXT(':'), Colon))
				{
					const int32 H = FCString::Atoi(*HHMM.Left(Colon));
					const int32 Mn = FCString::Atoi(*HHMM.Mid(Colon + 1));
					Schedule.DepartureSeconds.Add((double)H * 3600.0 + (double)Mn * 60.0);
				}
			}
		}
	}
	Schedule.DepartureSeconds.Sort();
}

void AWiesbadenBusStopMonitor::BuildWorldPath()
{
	WorldPath.Reset(); ArcCm.Reset(); Route.StopArcCm.Reset();
	if (!Converter || GeoPath.Num() < 2) { return; }
	for (const FVector2D& G : GeoPath)
	{
		FGeoCoordinate C; C.Latitude = G.X; C.Longitude = G.Y; C.Height = 0.0;
		const FVector Wld = Converter->GeoToUnrealGround(C);
		WorldPath.Add(FVector(Wld.X, Wld.Y, 0.0));
	}
	ArcCm.Add(0.0);
	for (int32 i = 1; i < WorldPath.Num(); ++i)
	{
		ArcCm.Add(ArcCm.Last() + FVector2D::Distance(
			FVector2D(WorldPath[i - 1].X, WorldPath[i - 1].Y),
			FVector2D(WorldPath[i].X, WorldPath[i].Y)));
	}
	Route.TotalLengthCm = ArcCm.Last();
	for (const FVector2D& G : GeoStops)
	{
		FGeoCoordinate C; C.Latitude = G.X; C.Longitude = G.Y; C.Height = 0.0;
		const FVector Wld = Converter->GeoToUnrealGround(C);
		const FVector2D S(Wld.X, Wld.Y);
		int32 Best = 0; double BestD = TNumericLimits<double>::Max();
		for (int32 i = 0; i < WorldPath.Num(); ++i)
		{
			const double D = FVector2D::DistSquared(FVector2D(WorldPath[i].X, WorldPath[i].Y), S);
			if (D < BestD) { BestD = D; Best = i; }
		}
		Route.StopArcCm.Add(ArcCm[Best]);
	}
	Route.StopArcCm.Sort();
}

bool AWiesbadenBusStopMonitor::ResolveGround(double X, double Y, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbMonitorGround), true);
	Params.AddIgnoredActor(this);
	if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, 1000000.0), FVector(X, Y, -200000.0),
		ECC_WorldStatic, Params) && !Hit.bStartPenetrating)
	{
		OutZ = Hit.Location.Z;
		return true;
	}
	return false;
}

void AWiesbadenBusStopMonitor::BeginPlay()
{
	Super::BeginPlay();
	LoadLine();
	LoadSchedule();
	Converter = NewObject<UGeoCoordinateConverter>(this);
	Converter->InitializeWithWiesbadenOrigin();
	BuildWorldPath();

	PoleMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	PanelMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	PoleMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Props/DFI/M_WbDfiPole.M_WbDfiPole"));
	PanelMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Props/DFI/M_WbDfiPanel.M_WbDfiPanel"));

	bReady = (WorldPath.Num() >= 2 && Route.StopArcCm.Num() >= 2 && PoleMesh && PanelMesh);
	if (bReady) { BuildMonitors(); }
	UE_LOG(LogWbMonitor, Log, TEXT("Abfahrtsmonitor bereit=%d: %d Halten, %d Abfahrten/Tag."),
		bReady ? 1 : 0, Monitors.Num(), Schedule.DepartureSeconds.Num());
}

void AWiesbadenBusStopMonitor::BuildMonitors()
{
	const double SpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;
	// Die fuenf gewuenschten Halten (Index in line6.json + Anzeigename).
	const int32 Indices[] = { 0, 1, 4, 6, 10 };
	const TCHAR* Names[] = { TEXT("Nordfriedhof"), TEXT("Wolkenbruch"), TEXT("Adlerstrasse"),
		TEXT("Platz der Deutschen Einheit"), TEXT("Hauptbahnhof") };
	const double PoleH = 225.0, PoleR = 7.0, PW = 250.0, PH = 140.0, PT = 12.0, PanelZ = 220.0;
	// Basismasse der Engine-Formen abtasten -> korrekte Skalierung unabhaengig
	// von deren Groesse (Cube 100, Zylinder abweichend).
	const FVector PoleSize = PoleMesh->GetBoundingBox().GetSize();
	const FVector PanelSize = PanelMesh->GetBoundingBox().GetSize();

	for (int32 c = 0; c < 5; ++c)
	{
		const int32 Idx = Indices[c];
		if (!Route.StopArcCm.IsValidIndex(Idx)) { continue; }
		FVector Pos, Tangent;
		if (!WiesbadenRailTransport::SamplePolyline(WorldPath, ArcCm, Route.StopArcCm[Idx], Pos, Tangent)) { continue; }
		const FVector Dir = Tangent.GetSafeNormal();
		const FVector RightDir = FVector(-Dir.Y, Dir.X, 0.0).GetSafeNormal();   // Bordsteinseite (wie Bus)
		const FVector MXY = Pos + RightDir * SidewalkOffsetCm;                  // Saeule auf dem Gehweg
		double GZ = Pos.Z;
		ResolveGround(MXY.X, MXY.Y, GZ);
		const FVector Fwd = -RightDir;   // Panel/Text blicken zur Strasse (zu den Wartenden)
		const FRotator FaceRot = FRotationMatrix::MakeFromXZ(Fwd, FVector::UpVector).Rotator();

		// Mast
		if (UStaticMeshComponent* Pole = NewObject<UStaticMeshComponent>(this))
		{
			Pole->SetStaticMesh(PoleMesh);
			Pole->SetupAttachment(Root);
			Pole->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Pole->RegisterComponent();
			if (PoleMat) { Pole->SetMaterial(0, PoleMat); }
			Pole->SetWorldScale3D(FVector((PoleR * 2.0) / FMath::Max(PoleSize.X, 1.0),
				(PoleR * 2.0) / FMath::Max(PoleSize.Y, 1.0), PoleH / FMath::Max(PoleSize.Z, 1.0)));
			Pole->SetWorldLocation(FVector(MXY.X, MXY.Y, GZ + PoleH * 0.5));
			Parts.Add(Pole);
		}
		// Panel (dunkel), blickt zur Strasse
		if (UStaticMeshComponent* Panel = NewObject<UStaticMeshComponent>(this))
		{
			Panel->SetStaticMesh(PanelMesh);
			Panel->SetupAttachment(Root);
			Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Panel->RegisterComponent();
			if (PanelMat) { Panel->SetMaterial(0, PanelMat); }
			Panel->SetWorldScale3D(FVector(PT / FMath::Max(PanelSize.X, 1.0),
				PW / FMath::Max(PanelSize.Y, 1.0), PH / FMath::Max(PanelSize.Z, 1.0)));   // X=Dicke(Fwd), Y=Breite, Z=Hoehe
			Panel->SetWorldRotation(FaceRot);
			Panel->SetWorldLocation(FVector(MXY.X, MXY.Y, GZ + PanelZ) + Fwd * (PoleR + PT * 0.5));
			Parts.Add(Panel);
		}
		// Text (bernsteingelb), oben-links auf der Panel-Front
		UTextRenderComponent* Txt = NewObject<UTextRenderComponent>(this);
		Txt->SetupAttachment(Root);
		Txt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Txt->RegisterComponent();
		Txt->SetTextRenderColor(FColor(255, 178, 20));
		Txt->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
		Txt->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextTop);
		Txt->SetWorldSize(8.0f);
		Txt->SetWorldRotation(FaceRot);
		const FVector Up(0.0, 0.0, 1.0);
		// Panel-Front, oben mittig (zentriert -> unabhaengig von der Dir-Richtung).
		const FVector Front = FVector(MXY.X, MXY.Y, GZ + PanelZ) + Fwd * (PoleR + PT + 1.5);
		Txt->SetWorldLocation(Front + Up * (PH * 0.5 - 12.0));
		Texts.Add(Txt);

		FMonitor M;
		M.StopIndex = Idx;
		M.Name = Names[c];
		M.OffsetSeconds = WiesbadenBusLine::SecondsToStop(Route, SpeedCmS, StopDwellSeconds, Idx);
		M.Text = Txt;
		Monitors.Add(M);
	}
}

void AWiesbadenBusStopMonitor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bReady || Monitors.Num() == 0) { return; }
	UpdateAccum += DeltaSeconds;
	if (UpdateAccum < 1.0) { return; }   // Sekunden-Takt reicht
	UpdateAccum = 0.0;

	const double WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double ServiceSeconds = (double)ServiceStartHour * 3600.0 + WorldTime;

	for (const FMonitor& M : Monitors)
	{
		if (!M.Text) { continue; }
		TArray<double> Until;
		WiesbadenBusLine::NextDepartures(ServiceSeconds, Schedule, M.OffsetSeconds, DisplayRows, Until);
		FString S = M.Name + TEXT("\n");
		if (Until.Num() == 0)
		{
			S += TEXT("kein Verkehr");
		}
		for (const double U : Until)
		{
			const int32 Min = (int32)FMath::FloorToDouble(U / 60.0);
			if (Min <= 0) { S += FString::Printf(TEXT("6  %s   sofort\n"), *Destination); }
			else { S += FString::Printf(TEXT("6  %s   %d min\n"), *Destination, Min); }
		}
		M.Text->SetText(FText::FromString(S));
	}
}
