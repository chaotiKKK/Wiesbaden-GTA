// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WiesbadenParkFeatures.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UGeoCoordinateConverter;

/**
 * Formale Wiesbadener Parkanlagen als prozedurale Primitiv-Bauwerke an ihren
 * echten Koordinaten (Referenz: Wiesbaden-Rundgang - lange Wasserbecken,
 * Springbrunnen, Formhecken).
 *
 * Aktuell: Bowling Green am Kurhaus (zwei lange Becken + Fontaenen) und die
 * Reisinger-Anlagen vor dem Hauptbahnhof (zwei Becken). Bauweise wie
 * AWiesbadenLandmarks / Nerobergbahn: Engine-Primitive, Geo->Welt, Hoehe per
 * Trace, zur Laufzeit - kein Re-Bake.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenParkFeatures : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenParkFeatures();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EParkKind : uint8 { BowlingGreen, Reisinger };

	struct FParkSpec
	{
		EParkKind Kind;
		double Lat;
		double Lon;
		double HeadingDeg;
		bool bBuilt = false;
	};

	TArray<FParkSpec> Specs;
	bool bAllBuilt = false;

	UPROPERTY(Transient)
	USceneComponent* Root = nullptr;

	UPROPERTY(Transient)
	TArray<UStaticMeshComponent*> Parts;

	UPROPERTY(Transient) UStaticMesh* CubeMesh = nullptr;
	UPROPERTY(Transient) UStaticMesh* CylinderMesh = nullptr;
	UPROPERTY(Transient) UMaterialInterface* WaterMat = nullptr;
	UPROPERTY(Transient) UMaterialInterface* StoneMat = nullptr;
	UPROPERTY(Transient) UMaterialInterface* GravelMat = nullptr;
	UPROPERTY(Transient) UMaterialInterface* HedgeMat = nullptr;

	UPROPERTY(Transient)
	UGeoCoordinateConverter* Converter = nullptr;

	void AddPart(UStaticMesh* Mesh, const FVector& BaseWorld, const FRotator& BaseYaw,
		const FVector& LocalCm, const FVector& SizeCm, UMaterialInterface* Material);

	/** Quader mit Kantenlaengen (cm), Unterkante auf BaseZ. */
	void AddBox(const FVector& BaseWorld, const FRotator& BaseYaw,
		double LX, double LY, double BaseZ, double WidthX, double DepthY, double HeightZ,
		UMaterialInterface* Material);
	/** Zylinder (Radius/Hoehe in cm), Unterkante auf BaseZ. */
	void AddCyl(const FVector& BaseWorld, const FRotator& BaseYaw,
		double LX, double LY, double BaseZ, double RadiusCm, double HeightCm,
		UMaterialInterface* Material);

	/** Langes Wasserbecken mit Steinrand (Mittelpunkt LX/LY, halbe Kanten). */
	void AddBasin(const FVector& BaseWorld, const FRotator& BaseYaw,
		double LX, double LY, double HalfWidthCm, double HalfLenCm);
	/** Rundes Brunnenbecken mit Fontaene. */
	void AddFountain(const FVector& BaseWorld, const FRotator& BaseYaw,
		double LX, double LY, double RadiusCm);

	void BuildBowlingGreen(const FVector& BaseWorld, const FRotator& Yaw);
	void BuildReisinger(const FVector& BaseWorld, const FRotator& Yaw);

	bool ResolveGround(const FVector& WorldXY, double& OutZ) const;
};
