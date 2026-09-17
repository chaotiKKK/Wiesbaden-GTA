// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenBusRoute.h"

#include "World/WiesbadenRailTransport.h"
#include "World/WiesbadenCitySubsystem.h"
#include "GIS/WiesbadenTrafficLights.h"
#include "GIS/GeoCoordinateConverter.h"
#include "Vehicles/WiesbadenFootPawn.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
#include "GameFramework/PlayerController.h"
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
	RideAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("RideAnchor"));
	RideAnchor->SetupAttachment(Root);
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
	CitySubsystem = GetWorld() ? GetWorld()->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
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
	SlotWorldPos.Init(FVector::ZeroVector, Buses.Num());
	SlotState.Init(0, Buses.Num());
	SlotRunKey.Init(-1, Buses.Num());

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
	if (SlotState.IsValidIndex(k)) { SlotState[k] = 0; }
}

void AWiesbadenBusRoute::BuildGates()
{
	Gates.Reset();
	if (!CitySubsystem || WorldPath.Num() < 2) { return; }
	const TArray<FWiesbadenTrafficLight>& Lights = CitySubsystem->TrafficLightSystem.Lights;
	if (Lights.Num() == 0) { return; }   // Ampelsystem noch nicht initialisiert -> spaeter erneut
	const double MatchSq = (double)RedGateMatchCm * (double)RedGateMatchCm;
	for (int32 li = 0; li < Lights.Num(); ++li)
	{
		const FVector L = Lights[li].Location;
		double Best = TNumericLimits<double>::Max();
		int32 BestIdx = INDEX_NONE;
		for (int32 i = 0; i < WorldPath.Num(); ++i)
		{
			const double D = FVector2D::DistSquared(FVector2D(WorldPath[i].X, WorldPath[i].Y), FVector2D(L.X, L.Y));
			if (D < Best) { Best = D; BestIdx = i; }
		}
		if (BestIdx != INDEX_NONE && Best <= MatchSq)
		{
			FBusGate G; G.ArcCm = ArcCm[BestIdx]; G.LightIndex = li;
			Gates.Add(G);
		}
	}
	Gates.Sort([](const FBusGate& A, const FBusGate& B) { return A.ArcCm < B.ArcCm; });
	bGatesBuilt = true;
	UE_LOG(LogWbBus, Log, TEXT("Bus: %d Ampeln auf der Linie 6 als Halte-Gates erkannt (von %d im Netz)."),
		Gates.Num(), Lights.Num());
}

bool AWiesbadenBusRoute::RedGateAhead(double InArcCm, const FVector& Dir, bool bForward, double& OutStopArcCm) const
{
	if (!CitySubsystem || Gates.Num() == 0) { return false; }
	// naechstes Gate in Fahrtrichtung innerhalb des Prueffensters.
	int32 BestGate = INDEX_NONE;
	double BestDelta = (double)RedApproachCm;
	for (int32 gi = 0; gi < Gates.Num(); ++gi)
	{
		const double Delta = bForward ? (Gates[gi].ArcCm - InArcCm) : (InArcCm - Gates[gi].ArcCm);
		if (Delta > 0.0 && Delta < BestDelta) { BestDelta = Delta; BestGate = gi; }
	}
	if (BestGate == INDEX_NONE) { return false; }
	// Anfahrts-Achse (0/1) aus der Peilung - dieselbe Regel wie ComputeGroupIndex.
	const double BearingDeg = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	const int32 Axis = (BearingDeg >= 0.0 && BearingDeg < 180.0) ? 0 : 1;
	const ESignalAspect A = CitySubsystem->TrafficLightSystem.GetGroupAspect(Gates[BestGate].LightIndex, Axis);
	if (A == ESignalAspect::Green) { return false; }
	OutStopArcCm = bForward ? (Gates[BestGate].ArcCm - RedStopMarginCm)
	                        : (Gates[BestGate].ArcCm + RedStopMarginCm);
	return true;
}

WiesbadenBusLine::FBusState AWiesbadenBusRoute::ComputeHeldState(int64 RunKey, double RawElapsed, float DeltaSeconds, bool& bOutFinished)
{
	const double SpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;
	const double Held = HoldByRun.FindRef(RunKey);
	const double Eff = RawElapsed - Held;
	bOutFinished = (Eff >= CycleSeconds);
	WiesbadenBusLine::FBusState St = WiesbadenBusLine::EvaluateRoundTrip(
		Eff, Route, SpeedCmS, StopDwellSeconds, TerminusDwellSeconds);
	if (!bStopAtRed || Gates.Num() == 0 || St.bDwelling || bOutFinished) { return St; }
	FVector Pos, Tangent;
	if (!WiesbadenRailTransport::SamplePolyline(WorldPath, ArcCm, St.ArcLengthCm, Pos, Tangent)) { return St; }
	FVector Dir = Tangent.GetSafeNormal();
	if (!St.bForward) { Dir = -Dir; }
	double StopArc = 0.0;
	if (RedGateAhead(St.ArcLengthCm, Dir, St.bForward, StopArc))
	{
		const bool bPast = St.bForward ? (St.ArcLengthCm >= StopArc) : (St.ArcLengthCm <= StopArc);
		if (bPast)
		{
			St.ArcLengthCm = StopArc;                            // an der Haltelinie klemmen
			HoldByRun.Add(RunKey, Held + (double)DeltaSeconds);  // Zeit verlieren -> bei Gruen fluessig weiter
		}
	}
	return St;
}

void AWiesbadenBusRoute::PlaceBusAt(int32 k, const WiesbadenBusLine::FBusState& St, bool bLogThisTick)
{
	UStaticMeshComponent* Bus = Buses.IsValidIndex(k) ? Buses[k] : nullptr;
	if (!Bus) { return; }
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
	// Haltebucht: an der Halte weiter nach rechts ausscheren (weich ein/aus).
	const double Bay = WiesbadenBusLine::BayFactor(St.ArcLengthCm, St.bDwelling, Route.StopArcCm, BayZoneCm);
	const double SideOffsetCm = LaneOffsetCm + Bay * BayDepthCm;
	const double FinalX = Pos.X + RightDir.X * SideOffsetCm;
	const double FinalY = Pos.Y + RightDir.Y * SideOffsetCm;
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
	if (SlotWorldPos.IsValidIndex(k)) { SlotWorldPos[k] = FVector(FinalX, FinalY, BusZ); }
	if (SlotState.IsValidIndex(k)) { SlotState[k] = St.bDwelling ? 2 : 1; }

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

	// Ampeln als Halte-Gates sammeln, sobald das (nur auf gebackenen Karten
	// initialisierte) Ampelsystem bereitsteht; nur in den ersten Sekunden versuchen.
	if (bStopAtRed && !bGatesBuilt && WorldTime < 20.0) { BuildGates(); }

	const double SpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;

	if (Schedule.DepartureSeconds.Num() >= 2)
	{
		// FAHRPLAN-MODUS: jeder Kurs faehrt zur echten ESWE-Abfahrtsminute ab
		// Nordfriedhof; Dichte schwankt mit dem Takt. Rotlicht-Halt je Kurs (Held).
		const double ServiceSeconds = (double)ServiceStartHour * 3600.0 + WorldTime;
		const double Window = CycleSeconds + 2400.0;   // Spielraum fuer rotlicht-verspaetete Kurse
		TArray<WiesbadenBusLine::FBusRun> Runs;
		WiesbadenBusLine::ActiveRuns(ServiceSeconds, Schedule, Window, Runs);
		for (int32 k = 0; k < N; ++k) { HideBusSlot(k); }
		TArray<bool> Used; Used.Init(false, N);
		TSet<int64> ActiveKeys;
		int32 Driving = 0;
		// Mitfahren: den Slot des Fahrgast-Busses reservieren, damit die Slotvergabe
		// ihn nicht wegtauscht/versteckt.
		const bool bRiding = RideSession.IsRiding() && Buses.IsValidIndex(RiddenSlot);
		bool bRiddenSeen = false;
		if (bRiding) { Used[RiddenSlot] = true; }
		for (const WiesbadenBusLine::FBusRun& R : Runs)
		{
			bool bFinished = false;
			const WiesbadenBusLine::FBusState St = ComputeHeldState(R.Index, R.Elapsed, DeltaSeconds, bFinished);
			if (bFinished) { continue; }   // Rundfahrt (evtl. verspaetet) beendet
			ActiveKeys.Add(R.Index);
			int32 Slot;
			if (bRiding && R.Index == RiddenRunKey)
			{
				Slot = RiddenSlot;      // gepinnt: der Fahrgast bleibt an diesem Bus
				bRiddenSeen = true;
			}
			else
			{
				// Stabile Slot-Zuordnung ueber den fortlaufenden Kurs-Index; bei Kollision
				// (Pool < gleichzeitige Kurse) den naechsten freien Slot.
				Slot = (int32)(((R.Index % N) + N) % N);
				if (Used[Slot])
				{
					Slot = INDEX_NONE;
					for (int32 j = 0; j < N; ++j) { if (!Used[j]) { Slot = j; break; } }
					if (Slot == INDEX_NONE) { continue; }
				}
			}
			Used[Slot] = true;
			if (SlotRunKey.IsValidIndex(Slot)) { SlotRunKey[Slot] = R.Index; }
			PlaceBusAt(Slot, St, bLogThisTick);
			++Driving;
		}
		// Fahrgast-Kurs nicht mehr aktiv (Rundfahrt beendet) -> automatisch absetzen.
		if (bRiding && !bRiddenSeen) { ToggleBoarding(); }
		// Haltezeiten nicht mehr aktiver Kurse aufraeumen (kein Leck).
		for (auto It = HoldByRun.CreateIterator(); It; ++It)
		{
			if (!ActiveKeys.Contains(It.Key())) { It.RemoveCurrent(); }
		}
		if (bLogThisTick)
		{
			const int32 Hour = ((int32)(ServiceSeconds / 3600.0)) % 24;
			const int32 Min = ((int32)(ServiceSeconds / 60.0)) % 60;
			UE_LOG(LogWbBus, Log, TEXT("Fahrplan: Dienstzeit %02d:%02d, %d Busse unterwegs (%d Ampel-Gates)."),
				Hour, Min, Driving, Gates.Num());
		}
	}
	else
	{
		// Rueckfall ohne Fahrplan: gleichverteilte Busse (ohne Rotlicht-Halt).
		for (int32 k = 0; k < N; ++k)
		{
			const double Offset = (CycleSeconds * k) / FMath::Max(N, 1);
			const WiesbadenBusLine::FBusState St = WiesbadenBusLine::EvaluateRoundTrip(
				WorldTime + Offset, Route, SpeedCmS, StopDwellSeconds, TerminusDwellSeconds);
			if (SlotRunKey.IsValidIndex(k)) { SlotRunKey[k] = k; }
			PlaceBusAt(k, St, bLogThisTick);
		}
	}

	// Mitfahren: der unskalierte Anker folgt der Pose des Fahrgast-Busses (Fahrgast +
	// Kamera haengen am Anker); danach die Einstiegstaste abfragen.
	if (RideSession.IsRiding() && RideAnchor && Buses.IsValidIndex(RiddenSlot) && Buses[RiddenSlot])
	{
		RideAnchor->SetWorldLocationAndRotation(
			Buses[RiddenSlot]->GetComponentLocation(), Buses[RiddenSlot]->GetComponentQuat());
	}
	UpdateRiding();
}

void AWiesbadenBusRoute::UpdateRiding()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC) { return; }
	const bool bDown = PC->IsInputKeyDown(EKeys::E);
	if (bDown && !bBoardKeyHeld) { ToggleBoarding(); }
	bBoardKeyHeld = bDown;
}

void AWiesbadenBusRoute::CreatePassengerCamera()
{
	if (PassengerCamera || !RideSession.GetPassenger() || !RideAnchor) { return; }
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC) { return; }
	PassengerCamera = NewObject<UWiesbadenVehicleCameraComponent>(this, TEXT("BusPassengerCamera"));
	PassengerCamera->SetupAttachment(RideAnchor);   // unskalierter Anker -> Offsets stimmen
	PassengerCamera->CameraOffset = FVector(0.0f, 0.0f, 220.0f);
	PassengerCamera->FollowArmLength = 750.0f;
	PassengerCamera->ZoomMinArmLength = 120.0f;
	PassengerCamera->ZoomMaxArmLength = 1600.0f;
	PassengerCamera->bLevelHorizon = true;
	// Innenraum vorn: Blick des Fahrgasts nach vorne durch den ~18 m langen Bus.
	PassengerCamera->CockpitOffset = FVector(620.0f, -55.0f, 210.0f);
	PassengerCamera->RegisterComponent();
	PassengerCamera->ActivateExternalView(PC, RideAnchor, RideSession.GetPassenger());
	PassengerCamera->SetCameraMode(EWiesbadenVehicleCameraMode::Cockpit);
}

void AWiesbadenBusRoute::DestroyPassengerCamera()
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

void AWiesbadenBusRoute::ToggleBoarding()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !RideAnchor) { return; }

	// Aussteigen: neben dem Bus absetzen und die Fusssteuerung freigeben.
	if (RideSession.IsRiding())
	{
		APawn* Passenger = RideSession.GetPassenger();
		if (!RideSession.BeginExiting() || !Passenger) { return; }
		Passenger->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		const FVector Side = RideAnchor->GetRightVector() * 320.0f + FVector(0, 0, 60.0f);
		Passenger->SetActorLocation(RideAnchor->GetComponentLocation() + Side);
		if (AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(Passenger)) { Foot->SetRiding(false); }
		DestroyPassengerCamera();
		RideSession.CompleteExit();
		RiddenSlot = INDEX_NONE;
		RiddenRunKey = -1;
		UE_LOG(LogWbBus, Log, TEXT("Bus: Fahrgast ausgestiegen."));
		return;
	}

	// Einsteigen: nur der Spieler ZU FUSS, nur nahe an einem an der Halte STEHENDEN Bus.
	AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(Pawn);
	if (!Foot) { return; }
	const FVector P = Pawn->GetActorLocation();
	int32 Best = INDEX_NONE;
	double BestD = (double)BoardRangeCm;
	for (int32 k = 0; k < SlotState.Num(); ++k)
	{
		if (SlotState[k] != 2 || !SlotWorldPos.IsValidIndex(k)) { continue; }   // nur haltende Busse
		const double D = FVector::Dist(P, SlotWorldPos[k]);
		if (D < BestD) { BestD = D; Best = k; }
	}
	UStaticMeshComponent* Car = (Best != INDEX_NONE && Buses.IsValidIndex(Best)) ? Buses[Best] : nullptr;
	if (!Car) { return; }
	if (!RideSession.BeginBoarding(Pawn, Best)) { return; }
	RideAnchor->SetWorldLocationAndRotation(Car->GetComponentLocation(), Car->GetComponentQuat());
	Foot->SetRiding(true);
	Pawn->AttachToComponent(RideAnchor, FAttachmentTransformRules::KeepWorldTransform);
	if (!RideSession.ConfirmRiding())
	{
		Pawn->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Foot->SetRiding(false);
		RideSession.Reset();
		return;
	}
	RiddenSlot = Best;
	RiddenRunKey = SlotRunKey.IsValidIndex(Best) ? SlotRunKey[Best] : -1;
	CreatePassengerCamera();
	Pawn->SetActorLocation(Car->GetComponentLocation() + FVector(0, 0, 160.0f));
	UE_LOG(LogWbBus, Log, TEXT("Bus: Fahrgast eingestiegen (Slot %d, Kurs %lld)."), Best, (long long)RiddenRunKey);
}