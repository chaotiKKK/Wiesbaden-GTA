// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/SebboHqElevator.h"
#include "World/SebboHqShape.h"

#include "WiesbadenSebboHqElevator.generated.h"

class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

/** Fahrbare Kabine und gekoppelte Schachttueren im SebboTower. */
UCLASS()
class WIESBADENREAL_API AWiesbadenSebboHqElevator : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenSebboHqElevator();
	virtual void Tick(float DeltaSeconds) override;

	/** Nach dem Spawn vom Tower aufrufen; Actor-Transform ist dessen Fusspunkt. */
	void Initialize(const FSebboHqDimensions& Dimensions);

	UFUNCTION(BlueprintCallable, Category = "Sebbo HQ|Aufzug")
	bool RequestFloor(int32 Floor);

	UFUNCTION(BlueprintPure, Category = "Sebbo HQ|Aufzug")
	int32 GetCurrentFloor() const { return Lift.CurrentFloor; }

	UFUNCTION(BlueprintPure, Category = "Sebbo HQ|Aufzug")
	int32 GetTargetFloor() const { return Lift.TargetFloor; }

private:
	UStaticMeshComponent* AddBox(FName Name, USceneComponent* Parent,
		const FVector& LocalCenter, const FVector& SizeCm, UMaterialInterface* Material,
		bool bBlocking = true);
	/** Zylinder (Durchmesser x Hoehe), optional gedreht - fuer Knoepfe. */
	UStaticMeshComponent* AddCylinder(FName Name, USceneComponent* Parent,
		const FVector& LocalCenter, const FVector& SizeCm, UMaterialInterface* Material,
		const FRotator& Rotation = FRotator::ZeroRotator);
	void UpdateDoorPositions();
	void HandlePlayerInput();
	bool IsPlayerInCab(const FVector& LocalPlayer) const;

	UPROPERTY(Transient)
	USceneComponent* Root = nullptr;

	UPROPERTY(Transient)
	USceneComponent* CabRoot = nullptr;

	UPROPERTY(Transient)
	TArray<UStaticMeshComponent*> LandingLeft;

	UPROPERTY(Transient)
	TArray<UStaticMeshComponent*> LandingRight;

	UPROPERTY(Transient)
	UStaticMeshComponent* CabDoorLeft = nullptr;

	UPROPERTY(Transient)
	UStaticMeshComponent* CabDoorRight = nullptr;

	UPROPERTY(Transient)
	UStaticMesh* CubeMesh = nullptr;

	UPROPERTY(Transient)
	UStaticMesh* CylinderMesh = nullptr;

	FSebboHqElevator Lift;
	FSebboHqDimensions Dims;
	bool bInitialized = false;
	bool bProbeComplete = false;
};
