// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Vehicles/WiesbadenVehicleControl.h"
#include "WiesbadenPoliceSubsystem.generated.h"
class AWiesbadenPoliceCar;
class AWiesbadenWorldBuilder;
class APawn;
class AWiesbadenHelicopter;
class AAIController;
struct FRoadNetwork;

namespace WiesbadenPolice
{
	WIESBADENREAL_API int32 PatrolCount(int32 Level);
	WIESBADENREAL_API FWiesbadenCarControl DriveControl(const FVector& Car, const FVector& Forward,
		const FVector& Target, float SpeedKmh, float LimitKmh);
}
UCLASS()
class WIESBADENREAL_API UWiesbadenPoliceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaSeconds) override;
	virtual void Deinitialize() override;
	virtual TStatId GetStatId() const override;
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei") bool IsPlayerSeen() const { return bPlayerSeen; }
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei") float GetArrestProgress() const { return ArrestSeconds / 5.0f; }
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei") bool WasRecentlyArrested() const { return ArrestNoticeSeconds > 0; }
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei") int32 GetPatrolCount() const { return Patrols.Num(); }
private:
	struct FPatrol
	{
		TWeakObjectPtr<AWiesbadenPoliceCar> Car;
		int32 LaneId = INDEX_NONE;
		TArray<FVector> Route;
		int32 Waypoint = 0;
	};
	void PrepareNetwork(AWiesbadenWorldBuilder* Builder);
	bool SpawnPatrol(const FVector& PlayerLocation, const FVector& ViewForward);
	bool HasSight(AActor* Observer, APawn* Player) const;
	TArray<FPatrol> Patrols;
	TWeakObjectPtr<AWiesbadenWorldBuilder> NetworkOwner;
	TMap<int32, TArray<int32>> Successors;
	TMap<FIntPoint, TArray<int32>> SpawnCells;
	int32 IndexedLanes = INDEX_NONE;
	float SpawnCooldown = 0;
	float ArrestSeconds = 0;
	float ArrestNoticeSeconds = 0;
	float RamCooldown = 0;
	bool bPlayerSeen = false;
};
