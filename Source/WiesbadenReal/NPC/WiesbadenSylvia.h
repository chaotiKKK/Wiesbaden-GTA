// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GIS/GeoCoordinateConverter.h"

#include "WiesbadenSylvia.generated.h"

class UGeoCoordinateConverter;
class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInterface;
class UPoseableMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UWidgetComponent;

/**
 * Kleine, selbstpositionierende Szene vor Platter Strasse 144.
 *
 * Der Actor ist absichtlich transient: die Karte bleibt unveraendert, und die
 * Szene wartet selbst auf die World-Partition-Zelle um die Zieladresse.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenSylvia : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenSylvia();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Kanonische Laufzeitpfade, die auch vom Asset-Regressionstest genutzt werden. */
	static const TCHAR* GetSylviaMeshPath();
	static FString GetSylviaMeshPartPath(int32 PartIndex);
	static int32 GetFigurePartCount();
	static const TCHAR* GetWoodShavingsSystemPath();
	static const TCHAR* GetThoughtBubbleText();
	static FGeoCoordinate GetPlatterStrasse144Coordinate();

private:
	UPROPERTY(VisibleAnywhere, Category = "Sylvia")
	TObjectPtr<USceneComponent> Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Sylvia")
	TArray<TObjectPtr<UPoseableMeshComponent>> FigureParts;

	UPROPERTY(VisibleAnywhere, Category = "Sylvia|Werkbank")
	TObjectPtr<UStaticMeshComponent> Workbench = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Sylvia|Werkbank")
	TObjectPtr<UStaticMeshComponent> WoodBlock = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Sylvia|Gedankenblase")
	TObjectPtr<UWidgetComponent> ThoughtBubble = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Sylvia|Assets")
	TObjectPtr<UStaticMesh> PrimitiveCube = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Sylvia|Assets")
	TObjectPtr<UMaterialInterface> WoodMaterial = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Sylvia|Assets")
	TObjectPtr<UMaterialInterface> DarkWoodMaterial = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Sylvia|Assets")
	TObjectPtr<UNiagaraSystem> WoodShavingsSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> WoodShavingsFX = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UGeoCoordinateConverter> Converter = nullptr;

	bool bSceneBuilt = false;
	bool bReportedMissingAsset = false;
	float PlaningPhase = 0.0f;

	bool TryBuildAtPlatterStrasse144();
	bool ResolveGround(const FVector& WorldXY, double& OutZ) const;
	void ConfigureComponents();
	void UpdatePlaningPose(float DeltaSeconds);
	void ApplyBoneRotation(FName BoneName, const FRotator& Rotation);
};
