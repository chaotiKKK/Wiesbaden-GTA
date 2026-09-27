// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GIS/GeoCoordinateConverter.h"
#include "WiesbadenPlatterParking.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/** Stellplatz in Metern relativ zur Hausnummer Platter Strasse 144.
 * X = Ost, Y = Nord; Heading ist in Unreal-Weltkoordinaten. */
struct FWiesbadenPlatterSpace
{
	FVector2D EastNorthM = FVector2D::ZeroVector;
	float HeadingDeg = 0.0f;
	float LengthM = 5.0f;
	float WidthM = 2.5f;
};

/**
 * Garagenhof vor Platter Strasse 144: Vorplatz zwischen Platter Strasse und
 * Garagenzeile (OSM 182281233) mit vier Toren, zwoelf Stellplaetzen entlang
 * der Gehweg-Hinterkante und einer Zufahrt mit abgesenktem Bordstein.
 *
 * LAGE aus dem amtlichen Luftbild (HVBG DOP20, Saved/Diagnose/
 * platter144_dop_overlay.png): der asphaltierte Hof liegt NORDOESTLICH von
 * Nr. 144, direkt an der Platter Strasse - nicht suedwestlich von Nr. 146, wo
 * die erste Fassung ihn in den Baumbestand gelegt hatte.
 *
 * Der Hof ist in einem Strassen-Rahmen (U entlang der Platter Strasse,
 * N quer von der Fahrbahnachse weg) beschrieben. Wie weit die Gehweg-
 * Hinterkante von der Achse liegt, liest der Actor zur Laufzeit aus dem
 * gebackenen Strassennetz (halbe Fahrbahn + Gehweg) - so trifft der Hof den
 * Gehweg, den der Bake wirklich gebaut hat. Baut erst nach dem Streaming.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenPlatterParking : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenPlatterParking();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Standardabstand Fahrbahnachse -> Gehweg-Hinterkante (m), falls das
	 * gebackene Netz nicht lesbar ist: 7 m Fahrbahn / 2 + 2,5 m Gehweg. */
	static constexpr double DefaultEdgeOffsetM = 6.0;

	/** Zwoelf Stellplaetze, Nase zum Hof (rueckwaerts eingeparkt). */
	static TArray<FWiesbadenPlatterSpace> BuildSpaces(double EdgeOffsetM = DefaultEdgeOffsetM);
	static FWiesbadenPlatterSpace PlayerStartSpace(double EdgeOffsetM = DefaultEdgeOffsetM);
	/** Strassen-Rahmen (U entlang, N von der Achse weg) -> Ost/Nord-Meter. */
	static FVector2D FrameToEastNorth(double U, double N);
	/** Garagenfront (OSM 182281233) im Strassen-Rahmen: N und U-Bereich. */
	static void GetGarageFront(double& OutN, double& OutU0, double& OutU1);
	/** Kann der GameMode nach dem blockierenden Streaming vor dem Auto-Spawn
	 * aufrufen; derselbe Actor versucht es spaeter im Tick erneut. */
	bool EnsureBuilt();
	bool GetPlayerStart(FVector& OutLocation, FRotator& OutRotation) const;

private:
	struct FMeshData
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colours;
	};
	FVector WorldXY(const FVector2D& EastNorthM) const;
	bool GroundZ(const FVector& XY, double& OutZ) const;
	bool TerrainZ(const FVector& XY, double& OutZ) const;
	/** Hoechster Gelaendepunkt im Umkreis - das Landscape darf zwischen zwei
	 * Belagsstuetzpunkten nicht durchstossen. */
	bool TerrainMaxAround(double U, double N, double RadiusM, double& OutZ) const;
	/** Abstand Achse -> Gehweg-Hinterkante und Bordhoehe aus dem gebackenen Netz. */
	void ReadRoadProfile();
	/** Sind Garagenwand und Fahrbahndecke schon gestreamt? Vorher trifft jeder
	 * Strahl nur das Landscape - Tore landeten in der Wand, die Rampe unter dem
	 * Gehweg. Liefert eine Beschreibung des Fehlenden. */
	bool IsSurroundingReady(FString& OutMissing) const;
	/** Wandpunkt der Garagenfront an Laengsposition U (Welt-XY), falls getroffen. */
	bool FindGarageWall(double U, double FloorZ, FVector& OutWallXY) const;
	/** Hofflaeche: Ausgleichsgerade vorn (Gehwegoberkante) und hinten
	 * (Gelaende an den Garagen); dazwischen linear quer zur Strasse. */
	void FitCourtPlane();
	double CourtZ(double U, double N) const;
	/** Achsabstand der gebackenen Fahrbahn vom Rahmen an Laengsposition U (m). */
	double AxisN(double U) const;
	double KerbN(double U) const { return AxisN(U) + CarriagewayHalfM; }
	double EdgeN(double U) const { return KerbN(U) + SidewalkM; }
	/** Streifen U0..U1; quer von N0(U) bis N1(U) - so folgt er gekruemmten Kanten. */
	void AddQuad(FMeshData& Mesh, double U0, double U1,
		TFunctionRef<double(double)> N0, TFunctionRef<double(double)> N1,
		double GridMetres, TFunctionRef<double(double, double)> SurfaceZ);
	void AddLine(FMeshData& Mesh, double UA, double NA, double UB, double NB,
		double WidthCm);
	void AddGarageFront();
	/** Zufahrtsrampe als eigene Mesh-Sektion ueber Bordstein und Gehweg. */
	void AddEntryRamp();
	/** Hofbelag (Sektion 0) und Markierungen aus den aktuellen Kantenprofilen. */
	void BuildCourt();
	/** Tore + Rampe, sobald die Umgebung gestreamt ist (oder nach 30 s). */
	void TryFinish();
	void AddGaragePart(const FVector& WorldXY, float YawDeg, const FVector& SizeCm,
		double BaseZ, UMaterialInterface* Material);

	UPROPERTY(Transient) USceneComponent* Root = nullptr;
	UPROPERTY(Transient) UProceduralMeshComponent* Pavement = nullptr;
	UPROPERTY(Transient) UProceduralMeshComponent* Markings = nullptr;
	UPROPERTY(Transient) UGeoCoordinateConverter* Converter = nullptr;
	UPROPERTY(Transient) UStaticMesh* Cube = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> GarageParts;
	bool bBuilt = false;
	/** Tore und Zufahrtsrampe gesetzt (brauchen gestreamte Wand/Fahrbahn). */
	bool bFinished = false;
	double BuiltSeconds = -1.0;
	double NextWaitLogSeconds = 0.0;
	double EdgeOffsetM = DefaultEdgeOffsetM;
	double CarriagewayHalfM = 3.5;
	double SidewalkM = 2.5;
	/** (U, N) der Fahrbahnachse vor dem Hof, nach U sortiert. */
	TArray<FVector2D> AxisSamples;
	/** Hoehenprofile der Hofkanten laengs der Strasse: (U m, Z cm), nach U sortiert. */
	TArray<FVector2D> FrontProfile;
	TArray<FVector2D> BackProfile;
	double CourtBackN = 18.0;
	static double SampleProfile(const TArray<FVector2D>& Profile, double U, double Fallback);
	double FrontZ(double U) const { return SampleProfile(FrontProfile, U, AnchorWorld.Z); }
	double BackZ(double U) const { return SampleProfile(BackProfile, U, AnchorWorld.Z); }
	FVector AnchorWorld = FVector::ZeroVector;
	FVector PlayerStartWorld = FVector::ZeroVector;
	FRotator PlayerStartRotation = FRotator::ZeroRotator;
};
