// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenBusRoute.h"

#include "World/WiesbadenRailTransport.h"
#include "GIS/GeoCoordinateConverter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbBus, Log, All);

AWiesbadenBusRoute::AWiesbadenBusRoute()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AWiesbadenBusRoute::LoadLine()
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data/Raw/Bus") / LineFile;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogWbBus, Warning, TEXT("Bus: Liniendatei nicht lesbar: %s"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
	{
		UE_LOG(LogWbBus, Warning, TEXT("Bus: kein gueltiges JSON: %s"), *Path);
		return;
	}
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
	UE_LOG(LogWbBus, Log, TEXT("Bus %s: %d Wegpunkte, %d Halte."), *LineFile, GeoPath.Num(), GeoStops.Num());
}

void AWiesbadenBusRoute::LoadSchedule()
{
	Schedule.DepartureSeconds.Reset();
	Schedule.DaySeconds = 86400.0;
	if (ScheduleFile.IsEmpty()) { return; }
	const FString Path = FPaths::ProjectDir() / TEXT("Data/Raw/Bus") / ScheduleFile;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogWbBus, Warning, TEXT("Bus: Fahrplandatei nicht lesbar (nutze gleichverteilt): %s"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
	{
		UE_LOG(LogWbBus, Warning, TEXT("Bus: Fahrplan kein gueltiges JSON: %s"), *Path);
		return;
	}
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
	UE_LOG(LogWbBus, Log, TEXT("Bus-Fahrplan %s: %d Abfahrten/Tag, Start %.1f Uhr."),
		*ScheduleFile, Schedule.DepartureSeconds.Num(), ServiceStartHour);
}

void AWiesbadenBusRoute::BuildWorldPath()
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
	if (Route.StopArcCm.Num() < 2)
	{
		Route.StopArcCm.Reset();
		Route.StopArcCm.Add(0.0);
		Route.StopArcCm.Add(Route.TotalLengthCm);
	}
}

bool AWiesbadenBusRoute::ResolveGround(double X, double Y, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbBusGround), true);
	Params.AddIgnoredActor(this);
	if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, 1000000.0), FVector(X, Y, -200000.0),
		ECC_WorldStatic, Params) && !Hit.bStartPenetrating)
	{
		OutZ = Hit.Location.Z;
		return true;
	}
	return false;
}

void AWiesbadenBusRoute::BeginPlay()
{
	Super::BeginPlay();
	LoadLine();
	LoadSchedule();
	Converter = NewObject<UGeoCoordinateConverter>(this);
	Converter->InitializeWithWiesbadenOrigin();
	BuildWorldPath();
	BusMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Vehicles/Bus/SM_Bus.SM_Bus"));
	if (BusMesh)
	{
		const FBox LocalBox = BusMesh->GetBoundingBox();
		const FVector Size = LocalBox.GetSize();
		const double Longest = FMath::Max3(Size.X, Size.Y, Size.Z);
		MeshScale = (Longest > 1.0) ? (BusLengthCm / Longest) : 1.0;
		// Der glTF-Pivot liegt in der Mesh-MITTE (convert_bus.py: origin BOUNDS),
		// nicht an der Unterkante. Ohne Korrektur haengt der halbe Bus unter der
		// Fahrbahn. Unterste Ecke der skalierten+ausgerichteten Box bestimmen und
		// den Bus spaeter genau um diesen Betrag anheben -> Raeder auf der Strasse.
		double MinZ = TNumericLimits<double>::Max();
		double MaxZ = -TNumericLimits<double>::Max();
		for (int32 ci = 0; ci < 8; ++ci)
		{
			const FVector Corner(
				(ci & 1) ? LocalBox.Max.X : LocalBox.Min.X,
				(ci & 2) ? LocalBox.Max.Y : LocalBox.Min.Y,
				(ci & 4) ? LocalBox.Max.Z : LocalBox.Min.Z);
			MinZ = FMath::Min(MinZ, MeshOrient.RotateVector(Corner * MeshScale).Z);
			MaxZ = FMath::Max(MaxZ, MeshOrient.RotateVector(Corner * MeshScale).Z);
		}
		MeshBottomCm = -MinZ;
		MeshTopCm = MaxZ;
		UE_LOG(LogWbBus, Log, TEXT("Bus-Box lokal Min.Z=%.2f Max.Z=%.2f, Scale %.3f -> Unterkante %.0f cm, Oberkante %.0f cm ueber Pivot, Hoehe %.0f cm."),
			LocalBox.Min.Z, LocalBox.Max.Z, MeshScale, MeshBottomCm, MeshTopCm, MeshBottomCm + MeshTopCm);
	}
	// Zielanzeige-Schild: einfaches Engine-Quad + die beiden Unlit-Materialien.
	SignMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	SignMatMainz = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Vehicles/Bus/Ziel/M_WbBusZiel_Mainz.M_WbBusZiel_Mainz"));
	SignMatNord = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Vehicles/Bus/Ziel/M_WbBusZiel_Nordfriedhof.M_WbBusZiel_Nordfriedhof"));
	UE_LOG(LogWbBus, Log, TEXT("Bus-Zielschild-Assets: PlaneMesh=%d MatMainz=%d MatNord=%d."),
		SignMesh ? 1 : 0, SignMatMainz ? 1 : 0, SignMatNord ? 1 : 0);

	const double SpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;
	CycleSeconds = WiesbadenBusLine::RoundTripSeconds(Route, SpeedCmS, StopDwellSeconds, TerminusDwellSeconds);
	int32 N = FMath::Max(NumBuses, 1);
	{ int32 BusOv = 0; if (FParse::Value(FCommandLine::Get(), TEXT("WbBusCount="), BusOv) && BusOv > 0) { N = BusOv; } }
	for (int32 k = 0; k < N; ++k)
	{
		UStaticMeshComponent* Bus = NewObject<UStaticMeshComponent>(this);
		Bus->SetStaticMesh(BusMesh);
		Bus->SetupAttachment(Root);
		Bus->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Bus->RegisterComponent();
		Bus->SetWorldScale3D(FVector(MeshScale));
		Bus->SetVisibility(false);
		Buses.Add(Bus);

		UStaticMeshComponent* Sign = nullptr;
		if (SignMesh)
		{
			Sign = NewObject<UStaticMeshComponent>(this);
			Sign->SetStaticMesh(SignMesh);
			Sign->SetupAttachment(Root);
			Sign->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Sign->SetCastShadow(false);
			Sign->RegisterComponent();
			Sign->SetVisibility(false);
		}
		Signs.Add(Sign);
		SignForward.Add(-1);
	}
	bReady = (WorldPath.Num() >= 2 && Route.StopArcCm.Num() >= 2 && BusMesh != nullptr && CycleSeconds > 0.0);
	UE_LOG(LogWbBus, Log, TEXT("Bus-Linie bereit=%d: %d Busse, Route %.0f m, Zyklus %.0f s, MeshScale %.3f, Unterkante %.0f cm."),
		bReady ? 1 : 0, Buses.Num(), Route.TotalLengthCm / 100.0, CycleSeconds, MeshScale, MeshBottomCm);

	bLogDiag = FParse::Param(FCommandLine::Get(), TEXT("WbBusLog"));
	if (bLogDiag)
	{
		for (int32 i = 0; i < Route.StopArcCm.Num(); ++i)
		{
			FVector Pos, Tangent;
			if (WiesbadenRailTransport::SamplePolyline(WorldPath, ArcCm, Route.StopArcCm[i], Pos, Tangent))
			{
				UE_LOG(LogWbBus, Log, TEXT("Bus-Halte %2d: Bogen %.0f m -> Welt X=%.0f Y=%.0f"),
					i, Route.StopArcCm[i] / 100.0, Pos.X, Pos.Y);
			}
		}
	}
}

void AWiesbadenBusRoute::HideBusSlot(int32 k)
{
	if (Buses.IsValidIndex(k) && Buses[k]) { Buses[k]->SetVisibility(false); }
	if (Signs.IsValidIndex(k) && Signs[k]) { Signs[k]->SetVisibility(false); }
}

void AWiesbadenBusRoute::PlaceBusRun(int32 k, double ElapsedSeconds, bool bLogThisTick)
{
	UStaticMeshComponent* Bus = Buses.IsValidIndex(k) ? Buses[k] : nullptr;
	if (!Bus) { return; }
	const double SpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;
	const WiesbadenBusLine::FBusState St = WiesbadenBusLine::EvaluateRoundTrip(
		ElapsedSeconds, Route, SpeedCmS, StopDwellSeconds, TerminusDwellSeconds);
	FVector Pos, Tangent;
	if (!WiesbadenRailTransport::SamplePolyline(WorldPath, ArcCm, St.ArcLengthCm, Pos, Tangent))
	{
		HideBusSlot(k);
		return;
	}
	// Fahrtrichtung (fuer die Rueckfahrt gespiegelt) bestimmt die rechte Seite.
	FVector Dir = Tangent.GetSafeNormal();
	if (!St.bForward) { Dir = -Dir; }
	// Rechtsverkehr: jeder Bus faehrt LaneOffsetCm rechts seiner Fahrtrichtung,
	// so begegnen sich Gegenrichtungs-Busse nebeneinander statt durcheinander.
	// ACHTUNG Achsen: die Geo->Welt-Projektion bildet Ost~+X, SUED~+Y ab (Nord=-Y),
	// also gegenueber ENU gespiegelt. Die rechte Hand der Fahrtrichtung ist damit
	// (-Dir.Y, Dir.X) (nicht (Dir.Y,-Dir.X) - das waere Linksverkehr). Boden an der
	// versetzten Position tasten (Strassenquerneigung/Streaming).
	const FVector RightDir = FVector(-Dir.Y, Dir.X, 0.0).GetSafeNormal();
	const double FinalX = Pos.X + RightDir.X * LaneOffsetCm;
	const double FinalY = Pos.Y + RightDir.Y * LaneOffsetCm;
	double GroundZ = 0.0;
	if (!ResolveGround(FinalX, FinalY, GroundZ))
	{
		HideBusSlot(k);
		if (bLogThisTick)
		{
			UE_LOG(LogWbBus, Log, TEXT("Bus %2d: X=%.0f Y=%.0f Bogen %.0f m %s - Boden nicht gestreamt (unsichtbar)"),
				k, FinalX, FinalY, St.ArcLengthCm / 100.0, St.bDwelling ? TEXT("VERWEILT") : TEXT("faehrt"));
		}
		return;
	}
	const double BusZ = GroundZ + MeshBottomCm + BusLiftCm;
	const FQuat Q = Dir.Rotation().Quaternion() * MeshOrient.Quaternion();
	Bus->SetWorldLocationAndRotation(FVector(FinalX, FinalY, BusZ), Q);
	Bus->SetVisibility(true);

	// Zielanzeige vorn: Quad blickt in Fahrtrichtung, Material je Richtung
	// (hin -> Mainz-Gonsenheim, zurueck -> Nordfriedhof).
	if (Signs.IsValidIndex(k) && Signs[k])
	{
		UStaticMeshComponent* Sign = Signs[k];
		const double SignZ = GroundZ + SignZAboveGroundCm;
		const FVector SignPos(
			FinalX + Dir.X * (BusLengthCm * SignFrontFrac),
			FinalY + Dir.Y * (BusLengthCm * SignFrontFrac),
			SignZ);
		const FRotator SignRot = FRotationMatrix::MakeFromZY(-Dir, FVector::UpVector).Rotator();
		Sign->SetWorldLocationAndRotation(SignPos, SignRot);
		Sign->SetWorldScale3D(FVector(SignWidthCm / 100.0, SignHeightCm / 100.0, 1.0));
		const int8 WantFwd = St.bForward ? 1 : 0;
		if (SignForward.IsValidIndex(k) && SignForward[k] != WantFwd)
		{
			UMaterialInterface* M = St.bForward ? SignMatMainz : SignMatNord;
			if (M) { Sign->SetMaterial(0, M); }
			SignForward[k] = WantFwd;
		}
		Sign->SetVisibility(true);
	}

	if (bLogThisTick)
	{
		UE_LOG(LogWbBus, Log, TEXT("Bus %2d: X=%.0f Y=%.0f Z=%.0f Bogen %.0f m %s SICHTBAR"),
			k, FinalX, FinalY, BusZ, St.ArcLengthCm / 100.0,
			St.bDwelling ? TEXT("VERWEILT") : TEXT("faehrt"));
	}
}

void AWiesbadenBusRoute::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bReady) { return; }
	const double WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const int32 N = Buses.Num();
	if (N <= 0) { return; }
	bool bLogThisTick = false;
	if (bLogDiag)
	{
		LogAccum += DeltaSeconds;
		if (LogAccum >= 2.0) { LogAccum = 0.0; bLogThisTick = true; }
	}

	if (Schedule.DepartureSeconds.Num() >= 2)
	{
		// FAHRPLAN-MODUS: jeder Kurs faehrt zur echten ESWE-Abfahrtsminute ab
		// Nordfriedhof; Dichte schwankt mit dem Takt (HVZ dicht, Rand duenn).
		const double ServiceSeconds = (double)ServiceStartHour * 3600.0 + WorldTime;
		TArray<WiesbadenBusLine::FBusRun> Runs;
		WiesbadenBusLine::ActiveRuns(ServiceSeconds, Schedule, CycleSeconds, Runs);
		for (int32 k = 0; k < N; ++k) { HideBusSlot(k); }
		// Stabile Slot-Zuordnung ueber den fortlaufenden Kurs-Index; bei Kollision
		// (Pool < gleichzeitige Kurse) den naechsten freien Slot.
		TArray<bool> Used; Used.Init(false, N);
		for (const WiesbadenBusLine::FBusRun& R : Runs)
		{
			int32 Slot = (int32)(((R.Index % N) + N) % N);
			if (Used[Slot])
			{
				Slot = INDEX_NONE;
				for (int32 j = 0; j < N; ++j) { if (!Used[j]) { Slot = j; break; } }
				if (Slot == INDEX_NONE) { continue; }
			}
			Used[Slot] = true;
			PlaceBusRun(Slot, R.Elapsed, bLogThisTick);
		}
		if (bLogThisTick)
		{
			const int32 Hour = ((int32)(ServiceSeconds / 3600.0)) % 24;
			const int32 Min = ((int32)(ServiceSeconds / 60.0)) % 60;
			UE_LOG(LogWbBus, Log, TEXT("Fahrplan: Dienstzeit %02d:%02d, %d Busse unterwegs."), Hour, Min, Runs.Num());
		}
	}
	else
	{
		// Rueckfall ohne Fahrplan: gleichverteilte Busse (wie zuvor).
		for (int32 k = 0; k < N; ++k)
		{
			const double Offset = (CycleSeconds * k) / FMath::Max(N, 1);
			PlaceBusRun(k, WorldTime + Offset, bLogThisTick);
		}
	}
}