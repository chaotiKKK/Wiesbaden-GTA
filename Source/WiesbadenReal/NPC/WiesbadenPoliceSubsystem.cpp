// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "NPC/WiesbadenPoliceSubsystem.h"
#include "NPC/WiesbadenPoliceCar.h"
#include "World/WiesbadenCitySubsystem.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace WiesbadenPolice
{
	int32 PatrolCount(int32 Level) { return FMath::Clamp(Level, 0, 6); }
	FWiesbadenCarControl DriveControl(const FVector& Car, const FVector& Forward,
		const FVector& Target, float SpeedKmh, float LimitKmh)
	{
		FWiesbadenCarControl C;
		const FVector Delta = Target - Car;
		if (Delta.SizeSquared2D() < FMath::Square(180.0)) { C.Brake = 1; return C; }
		const float Angle = FMath::FindDeltaAngleDegrees(Forward.Rotation().Yaw, Delta.Rotation().Yaw);
		C.Steering = FMath::Clamp(Angle / 35.0f, -1.0f, 1.0f);
		const float Desired = FMath::Max(8.0f, LimitKmh * (1.0f - .65f * FMath::Clamp(FMath::Abs(Angle) / 90.0f, 0.0f, 1.0f)));
		C.Throttle = SpeedKmh < Desired ? .7f : 0;
		C.Brake = SpeedKmh > Desired + 3 ? .65f : 0;
		return C;
	}
}
namespace
{
	FIntPoint PoliceCell(const FVector& P) { return FIntPoint(FMath::FloorToInt(P.X / 20000.0), FMath::FloorToInt(P.Y / 20000.0)); }
}
TStatId UWiesbadenPoliceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWiesbadenPoliceSubsystem, STATGROUP_Tickables);
}
void UWiesbadenPoliceSubsystem::PrepareNetwork(AWiesbadenWorldBuilder* Builder)
{
	const int32 Count = Builder ? Builder->RoadNetwork.Lanes.Num() : 0;
	if (NetworkOwner.Get() == Builder && IndexedLanes == Count) { return; }
	NetworkOwner = Builder; IndexedLanes = Count; Successors.Reset(); SpawnCells.Reset();
	if (!Builder) { return; }
	const FRoadNetwork& Network = Builder->RoadNetwork;
	// LaneSuccessors ist nicht serialisiert; aus den serialisierten Connections
	// rekonstruieren, ohne das gemeinsam genutzte Netz zu mutieren.
	for (int32 i = 0; i < Network.Connections.Num(); ++i)
	{
		const FLaneConnection& C = Network.Connections[i];
		if (!C.bRestricted) { Successors.FindOrAdd(C.FromLaneId).Add(i); }
	}
	for (int32 i = 0; i < Network.Lanes.Num(); ++i)
	{
		const FRoadLane& Lane = Network.Lanes[i];
		if (!Lane.IsValid() || Lane.bIsBikeLane || Lane.bIsBusLane) { continue; }
		const FRoadSegment* S = Network.GetSegment(Lane.SegmentId);
		if (!S || !FOSMTagParser::IsDrivable(S->HighwayType) || S->bIsTunnel) { continue; }
		SpawnCells.FindOrAdd(PoliceCell(Lane.GetStartPoint())).Add(i);
	}
}
bool UWiesbadenPoliceSubsystem::HasSight(AActor* Observer, APawn* Player) const
{
	if (!Observer || !Player || FVector::DistSquared(Observer->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(14000.0)) { return false; }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PoliceSight), true, Observer);
	FHitResult Hit;
	const FVector A = Observer->GetActorLocation() + FVector(0, 0, 120);
	const FVector B = Player->GetActorLocation() + FVector(0, 0, 70);
	return !GetWorld()->LineTraceSingleByChannel(Hit, A, B, ECC_Visibility, Params) || Hit.GetActor() == Player;
}
bool UWiesbadenPoliceSubsystem::SpawnPatrol(const FVector& PlayerLocation, const FVector& ViewForward)
{
	AWiesbadenWorldBuilder* Builder = NetworkOwner.Get();
	if (!Builder) { return false; }
	const FRoadNetwork& Network = Builder->RoadNetwork;
	const FIntPoint Cell = PoliceCell(PlayerLocation);
	for (int32 X = Cell.X - 2; X <= Cell.X + 2; ++X)
	{
		for (int32 Y = Cell.Y - 2; Y <= Cell.Y + 2; ++Y)
		{
			const TArray<int32>* Lanes = SpawnCells.Find(FIntPoint(X, Y));
			if (!Lanes) { continue; }
			for (int32 Id : *Lanes)
			{
				const FRoadLane& Lane = Network.Lanes[Id];
				FVector P = Lane.GetStartPoint() + FVector(0, 0, 38);
				const FVector Offset = P - PlayerLocation;
				if (Offset.SizeSquared2D() < FMath::Square(18000.0) || Offset.SizeSquared2D() > FMath::Square(45000.0)) { continue; }
				if (FVector::DotProduct(Offset.GetSafeNormal2D(), ViewForward.GetSafeNormal2D()) > .25) { continue; }
				bool Occupied = false;
				for (const FPatrol& Patrol : Patrols)
				{ if (Patrol.Car.IsValid() && FVector::DistSquared2D(P, Patrol.Car->GetActorLocation()) < FMath::Square(1200.0)) { Occupied = true; break; } }
				if (Occupied) { continue; }
				FHitResult Ground;
				if (!GetWorld()->LineTraceSingleByChannel(Ground, P + FVector(0, 0, 300), P - FVector(0, 0, 500), ECC_WorldStatic)) { continue; }
				if (FMath::Abs(Ground.ImpactPoint.Z - P.Z) > 100.0) { continue; } // keine ungeladene Fahrbahn oder Dach
				P.Z = Ground.ImpactPoint.Z + 38;
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
				AWiesbadenPoliceCar* Car = GetWorld()->SpawnActor<AWiesbadenPoliceCar>(P, Lane.GetEntryDirection().Rotation(), Params);
				if (!Car) { continue; }
				FPatrol Patrol; Patrol.Car = Car; Patrol.LaneId = Id; Patrol.Route = Lane.Centerline; Patrol.Waypoint = 1;
				Patrols.Add(MoveTemp(Patrol));
				UE_LOG(LogTemp, Log, TEXT("Polizei: Streife %d auf Spur %d, Abstand %.0f m."), Patrols.Num(), Id, Offset.Size2D() / 100.0);
				return true;
			}
		}
	}
	return false;
}
void UWiesbadenPoliceSubsystem::Tick(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || FParse::Param(FCommandLine::Get(), TEXT("WbNoPolice"))) { return; }
	UWiesbadenCitySubsystem* City = World->GetSubsystem<UWiesbadenCitySubsystem>();
	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (!City || !Player) { return; }
	ArrestNoticeSeconds = FMath::Max(0.0f, ArrestNoticeSeconds - DeltaSeconds);
	SpawnCooldown = FMath::Max(0.0f, SpawnCooldown - DeltaSeconds);
	RamCooldown = FMath::Max(0.0f, RamCooldown - DeltaSeconds);
	AWiesbadenWorldBuilder* Builder = NetworkOwner.Get();
	if (!Builder) { for (TActorIterator<AWiesbadenWorldBuilder> It(World); It; ++It) { Builder = *It; break; } }
	PrepareNetwork(Builder);
	Patrols.RemoveAll([](const FPatrol& P) { return !P.Car.IsValid(); });
	const FVector PlayerLocation = Player->GetActorLocation();
	const int32 Wanted = WiesbadenPolice::PatrolCount(City->GetWantedLevel());
	while (Patrols.Num() > Wanted)
	{
		if (Patrols.Last().Car.IsValid()) { Patrols.Last().Car->Destroy(); }
		Patrols.Pop();
	}
	if (Patrols.Num() < Wanted && SpawnCooldown <= 0)
	{
		const FVector Forward = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Vector() : Player->GetActorForwardVector();
		SpawnPatrol(PlayerLocation, Forward); SpawnCooldown = 4.0f;
	}
	bPlayerSeen = false;
	int32 Nearby = 0;
	for (FPatrol& Patrol : Patrols)
	{
		AWiesbadenPoliceCar* Car = Patrol.Car.Get();
		if (!Car) { continue; }
		const double Distance = FVector::Dist(Car->GetActorLocation(), PlayerLocation);
		if (Distance > 100000.0) { Car->Destroy(); continue; }
		const bool Seen = HasSight(Car, Player);
		bPlayerSeen |= Seen;
		if (Seen && Distance < 700.0) { ++Nearby; }
		if (RamCooldown <= 0 && Distance < 350.0)
		{
			const IWiesbadenVehicleControl* V = Cast<IWiesbadenVehicleControl>(Player);
			if (V && V->GetSpeedKmh() > 15)
			{ City->ReportCrime(EWiesbadenCrimeEvent::OfficerHit); RamCooldown = 3.0f; }
		}
		while (Patrol.Route.IsValidIndex(Patrol.Waypoint) && FVector::DistSquared2D(Car->GetActorLocation(), Patrol.Route[Patrol.Waypoint]) < FMath::Square(450.0))
		{ ++Patrol.Waypoint; }
		if (!Patrol.Route.IsValidIndex(Patrol.Waypoint) && Builder)
		{
			const TArray<int32>* Options = Successors.Find(Patrol.LaneId);
			const FLaneConnection* Best = nullptr; double Score = TNumericLimits<double>::Max();
			if (Options)
			{
				for (int32 i : *Options)
				{
					const FLaneConnection& C = Builder->RoadNetwork.Connections[i];
					const FRoadLane* L = Builder->RoadNetwork.GetLane(C.ToLaneId);
					if (!L || L->bIsBikeLane || L->bIsBusLane || !L->IsValid()) { continue; }
					const double D = FVector::DistSquared2D(L->GetEndPoint(), PlayerLocation);
					if (D < Score) { Score = D; Best = &C; }
				}
			}
			if (Best)
			{
				Patrol.LaneId = Best->ToLaneId; Patrol.Route = Best->ConnectionPath;
				Patrol.Route.Append(Builder->RoadNetwork.Lanes[Patrol.LaneId].Centerline); Patrol.Waypoint = 0;
			}
		}
		FVector Target = Patrol.Route.IsValidIndex(Patrol.Waypoint) ? Patrol.Route[Patrol.Waypoint] : Car->GetActorLocation();
		// Keine Beeline durch Haeuser. Nahziel nur bei freier Sicht und gleicher Hoehe.
		if (Seen && Distance < 1000.0 && FMath::Abs(PlayerLocation.Z - Car->GetActorLocation().Z) < 150)
		{ Target = PlayerLocation; }
		Car->SetExternalControl(WiesbadenPolice::DriveControl(Car->GetActorLocation(), Car->GetActorForwardVector(),
			Target, Car->GetSpeedKmh(), FMath::Min(65.0f, 35.0f + City->GetWantedLevel() * 5.0f)));
	}
	if (bPlayerSeen) { City->WantedState.SecondsSinceEvent = 0; }
	const IWiesbadenExternalControl* Vehicle = Cast<IWiesbadenExternalControl>(Player);
	const float Speed = Vehicle ? Vehicle->GetSpeedKmh() : Player->GetVelocity().Size() * .036f;
	const int32 Needed = City->GetWantedLevel() == 1 ? 1 : 2;
	ArrestSeconds = City->GetWantedLevel() > 0 && Nearby >= Needed && Speed < 3.0f
		? FMath::Min(5.0f, ArrestSeconds + DeltaSeconds) : 0.0f;
	if (ArrestSeconds >= 5.0f)
	{
		City->WantedState = {}; ArrestSeconds = 0; ArrestNoticeSeconds = 6;
		for (FPatrol& P : Patrols) { if (P.Car.IsValid()) { P.Car->Destroy(); } }
		Patrols.Reset(); bPlayerSeen = false;
		UE_LOG(LogTemp, Log, TEXT("Polizei: Festnahme nach 5 s Stillstand; Fahndung beendet."));
	}
}
void UWiesbadenPoliceSubsystem::Deinitialize()
{
	for (FPatrol& P : Patrols) { if (P.Car.IsValid()) { P.Car->Destroy(); } }
	Patrols.Reset(); Successors.Reset(); SpawnCells.Reset(); NetworkOwner.Reset();
	Super::Deinitialize();
}
