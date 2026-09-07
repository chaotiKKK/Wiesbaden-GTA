// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WiesbadenLandmarks.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UGeoCoordinateConverter;

/**
 * Platziert markante Wiesbaden-Wahrzeichen als prozedurale Primitiv-Bauwerke an
 * ihren echten OSM-Koordinaten - fuer Wiedererkennbarkeit der Karte
 * (Referenz: Wiesbaden-Rundgang- und Neroberg-Videos).
 *
 * Aktuell: Marktkirche (roter Backstein, hoher Turm + Spitzen) und die
 * Russisch-Orthodoxe Kirche am Neroberg (weiss, goldene Zwiebelkuppeln).
 *
 * Bauweise wie Nerobergbahn/Pfeiler: Engine-Primitive (Cube/Cone/Cylinder/
 * Sphere) als StaticMeshComponents, an Geo->Welt umgerechnet, Hoehe per Trace.
 * Laeuft zur Laufzeit - kein Re-Bake.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenLandmarks : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenLandmarks();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class ELandmarkKind : uint8 { Marktkirche, RussischeKirche };

	struct FLandmarkSpec
	{
		ELandmarkKind Kind;
		double Lat;
		double Lon;
		double HeadingDeg;   // Ausrichtung der Laengsachse
		bool bBuilt = false;
	};

	TArray<FLandmarkSpec> Specs;
	bool bAllBuilt = false;

	UPROPERTY(Transient)
	USceneComponent* Root = nullptr;

	UPROPERTY(Transient)
	TArray<UStaticMeshComponent*> Parts;

	// Engine-Primitive + Materialien (in BeginPlay geladen).
	UPROPERTY(Transient) UStaticMesh* CubeMesh = nullptr;
	UPROPERTY(Transient) UStaticMesh* ConeMesh = nullptr;
	UPROPERTY(Transient) UStaticMesh* CylinderMesh = nullptr;
	UPROPERTY(Transient) UStaticMesh* SphereMesh = nullptr;
	UPROPERTY(Transient) UMaterialInterface* BrickMat = nullptr;
	UPROPERTY(Transient) UMaterialInterface* SlateMat = nullptr;
	UPROPERTY(Transient) UMaterialInterface* GoldMat = nullptr;
	UPROPERTY(Transient) UMaterialInterface* WhiteMat = nullptr;

	UPROPERTY(Transient)
	UGeoCoordinateConverter* Converter = nullptr;

	/** Ein Bauteil anlegen: Mesh, Weltlage (relativ zur Basis + Heading), Materialslot. */
	void AddPart(UStaticMesh* Mesh, const FVector& BaseWorld, const FRotator& BaseYaw,
		const FVector& LocalCm, const FRotator& LocalRot, const FVector& SizeCm,
		UMaterialInterface* Material);

	/** Quader mit Kantenlaengen (cm), Unterkante auf LocalZ. */
	void AddBox(const FVector& BaseWorld, const FRotator& BaseYaw,
		double LX, double LY, double LZ, double WidthX, double DepthY, double HeightZ,
		UMaterialInterface* Material);
	/** Kegel-Spitze: Basisradius, Hoehe, Basis auf LocalZ. */
	void AddSpire(const FVector& BaseWorld, const FRotator& BaseYaw,
		double LX, double LY, double BaseZ, double RadiusCm, double HeightCm,
		UMaterialInterface* Material);
	/** Zwiebelkuppel auf Trommel: goldene Kuppel + Spitze auf LocalZ. */
	void AddOnionDome(const FVector& BaseWorld, const FRotator& BaseYaw,
		double LX, double LY, double BaseZ, double RadiusCm, UMaterialInterface* DomeMat);

	void BuildMarktkirche(const FVector& BaseWorld, const FRotator& Yaw);
	void BuildRussianChurch(const FVector& BaseWorld, const FRotator& Yaw);

	bool ResolveGround(const FVector& WorldXY, double& OutZ) const;
};
