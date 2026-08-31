// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "WiesbadenNerotal48.generated.h"

class UProceduralMeshComponent;
class USceneComponent;

/**
 * Der Garten von Nerotal 48 - Pool, Terrasse und Palmen.
 *
 * Das HAUS selbst kommt aus den amtlichen Daten: OpenStreetMap fuehrt es als
 * way 476086889 mit sechzehn Grundrisspunkten, 21,1 x 26,5 m, Postleitzahl
 * 65193. Der Gebaeudegenerator baut es wie jedes andere. Was ihm fehlt, ist
 * alles, was NICHT im Grundriss steht: der Garten dahinter.
 *
 * Dieser Actor traegt daher nur die Aussenanlage nach. Er setzt sich selbst
 * anhand der echten Koordinaten, tastet die Gelaendehoehe ab und baut:
 *
 *   - eine Terrasse aus Sandsteinplatten am Haus,
 *   - ein Schwimmbecken mit Fliesenwand, Ueberlaufrinne und Wasserspiegel,
 *   - Palmen als eigene Geometrie (Stamm aus Ringen, Wedel als Blattflaechen).
 *
 * Die Suedseite ist kein Zufall: Ein Pool gehoert dorthin, wo die Sonne
 * hinkommt. Ueber GardenBearingDeg laesst sich die Anlage drehen, falls das
 * Grundstueck anders geschnitten ist als angenommen.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenNerotal48 : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenNerotal48();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Richtung, in der der Garten vom Haus aus liegt, in Grad.
	 *
	 * 180 = sueden. Norden ist +X, Osten +Y (Unreal-Konvention nach der
	 * Georeferenz des Projekts).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nerotal 48")
	double GardenBearingDeg = 180.0;

	/** Abstand der Terrassenkante von der Hausmitte in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nerotal 48", meta = (ClampMin = "500.0"))
	double GardenOffsetCm = 1600.0;

	/** Beckenmasse in cm (Laenge x Breite x Tiefe). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nerotal 48", meta = (ClampMin = "200.0"))
	double PoolLengthCm = 900.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nerotal 48", meta = (ClampMin = "150.0"))
	double PoolWidthCm = 400.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nerotal 48", meta = (ClampMin = "50.0"))
	double PoolDepthCm = 150.0;

	/** Anzahl der Palmen rings um die Anlage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nerotal 48", meta = (ClampMin = "0", ClampMax = "24"))
	int32 PalmCount = 6;

	/**
	 * Weltposition der Hausmitte aus den OSM-Grundrisspunkten (nach dem
	 * ersten Tick gesetzt). Fuer Tests und das Entwicklermenue.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Nerotal 48")
	FVector HouseCenter = FVector::ZeroVector;

	/** Mittelpunkt des Beckens in Weltkoordinaten. */
	UPROPERTY(BlueprintReadOnly, Category = "Nerotal 48")
	FVector PoolCenter = FVector::ZeroVector;

	/**
	 * Mittelpunkt des Grundrisses aus einer Punktliste (datenrein, testbar).
	 *
	 * Der Schwerpunkt der Eckpunkte, nicht der Flaechenschwerpunkt: Bei einem
	 * gleichmaessig umrissenen Gebaeude ist der Unterschied belanglos, und der
	 * Eckpunktmittelwert kommt ohne Flaechenformel aus.
	 */
	static FVector2D ComputeFootprintCenter(const TArray<FVector2D>& Points);

	/**
	 * Legt die Wasseroberflaeche ETWAS unter die Beckenkante (datenrein).
	 *
	 * Ein Wasserspiegel genau auf der Kante sieht falsch aus - im Becken steht
	 * das Wasser rund eine Handbreit tiefer, und genau dieser Schatten an der
	 * Innenwand macht aus einer blauen Flaeche ein gefuelltes Becken.
	 */
	static double ComputeWaterLevelCm(double CopingTopCm, double FreeboardCm);

private:
	/** Setzt den Actor auf die echten Koordinaten und tastet das Gelaende ab. */
	bool ResolvePlacement();

	/** Baut Terrasse, Becken, Wasser und Palmen. */
	void BuildGarden();

	/** Ein Quader als Netzabschnitt (Mitte, Halbmasse, Farbe). */
	void AddBox(const FVector& Center, const FVector& HalfSize, const FLinearColor& Colour,
		TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals,
		TArray<FVector2D>& UVs, TArray<FLinearColor>& Colours) const;

	/** Eine Palme an einer Stelle, mit Hoehe und Drehung. */
	void AddPalm(const FVector& Base, double HeightCm, double YawRad,
		TArray<FVector>& TrunkVerts, TArray<int32>& TrunkTris,
		TArray<FVector>& TrunkNormals, TArray<FVector2D>& TrunkUVs,
		TArray<FLinearColor>& TrunkColours,
		TArray<FVector>& FrondVerts, TArray<int32>& FrondTris,
		TArray<FVector>& FrondNormals, TArray<FVector2D>& FrondUVs,
		TArray<FLinearColor>& FrondColours) const;

	UPROPERTY() TObjectPtr<USceneComponent> Root = nullptr;

	/** Terrasse, Becken, Rand, Wasser, Palmenstaemme, Wedel. */
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> GardenMesh = nullptr;

	/** True, sobald die Gelaendehoehe steht und der Garten gebaut ist. */
	bool bBuilt = false;

	/** Wartezeit bis zum naechsten Versuch, solange das Gelaende fehlt. */
	float RetrySeconds = 0.0f;
};
