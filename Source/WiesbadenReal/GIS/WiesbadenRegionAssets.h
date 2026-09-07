// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/IHeightSampler.h"
#include "GIS/WiesbadenRegion.h"
#include "GIS/RoadNetworkTypes.h"
#include "GIS/WiesbadenRoadClearance.h"

#include "WiesbadenRegionAssets.generated.h"

/** Art eines regionen-abhaengigen Assets. */
UENUM(BlueprintType)
enum class ERegionAssetCategory : uint8
{
	/** Baum (nur in Gruen-Regionen). */
	Tree,
	/** Ufer-Objekt: Laterne, Bank, Poller (am Rand von Wasser-Regionen). */
	Waterfront,
	/** Industrie-Objekt: Container, Fass, Halle (nur Industrie/Gewerbe). */
	Industrial,
	MAX UMETA(Hidden)
};

/** Ein platziertes regionen-abhaengiges Asset (Weltkoordinaten, cm). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FPlacedRegionAsset
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	ERegionAssetCategory Category = ERegionAssetCategory::Tree;

	/** Position in Weltkoordinaten (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	FVector Location = FVector::ZeroVector;

	/** Zufaellige Drehung um die Hochachse (deterministisch). */
	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	float YawDegrees = 0.0f;

	/** Skalierung (deterministische Variation). */
	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	float Scale = 1.0f;

	/** Name der Region, aus der das Asset stammt. */
	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	FString RegionName;
};

/** Vollstaendiges Ergebnis des Regionen-Asset-Passes. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRegionAssetLayout
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	TArray<FPlacedRegionAsset> Assets;

	void Reset() { Assets.Reset(); }

	FString GetStatisticsString() const
	{
		int32 Trees = 0, Waterfront = 0, Industrial = 0;
		for (const FPlacedRegionAsset& Asset : Assets)
		{
			switch (Asset.Category)
			{
			case ERegionAssetCategory::Tree: ++Trees; break;
			case ERegionAssetCategory::Waterfront: ++Waterfront; break;
			case ERegionAssetCategory::Industrial: ++Industrial; break;
			default: break;
			}
		}
		return FString::Printf(TEXT("%d Baeume, %d Ufer-Objekte, %d Industrie-Objekte"),
			Trees, Waterfront, Industrial);
	}
};

/** Parameter des Regionen-Asset-Passes. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRegionAssetSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RegionAssets")
	bool bPlaceTrees = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RegionAssets")
	bool bPlaceWaterfront = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RegionAssets")
	bool bPlaceIndustrial = true;

	/** Rasterabstand der Baeume (cm). Dichter = ueppiger, naeher an der
	 *  baumgesaeumten Referenz (echtes Wiesbaden). 900 war zu licht. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RegionAssets", meta = (ClampMin = "100.0"))
	double TreeSpacingCm = 500.0;

	/** Rasterabstand der Ufer-Objekte entlang des Ufers (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RegionAssets", meta = (ClampMin = "50.0"))
	double WaterfrontSpacingCm = 400.0;

	/** Rasterabstand der Industrie-Objekte (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RegionAssets", meta = (ClampMin = "100.0"))
	double IndustrialSpacingCm = 1200.0;
};

/** Diagnose des Regionen-Asset-Passes. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRegionAssetReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	int32 RegionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	int32 AssetCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	int32 TreeCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	int32 WaterfrontCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	int32 IndustrialCount = 0;

	/**
	 * Objekte, die auf einer Fahrbahn gelandet waeren und weggelassen wurden.
	 *
	 * Steht bewusst im Bericht und nicht nur im Protokoll: Ist die Zahl 0,
	 * obwohl ein Strassennetz uebergeben wurde, arbeitet die Freihaltung
	 * nicht - und das saehe man im Spiel erst, wenn ein Auto durch einen
	 * Baum faehrt.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RegionAssets")
	int32 SkippedOnRoadCount = 0;

	FString ToString() const;
};

/**
 * Regionen-Asset-Pass (WorldClaw-Schritt 3 - "Objekte logisch platzieren"):
 * platziert deterministisch Assets passend zur Region - Baeume nur in Gruen-
 * Regionen, Ufer-Objekte am Rand von Wasser-Regionen, Industrie-Objekte nur in
 * Industrie-/Gewerbe-Regionen. Kein Baum in der Wueste, kein Container im Park.
 *
 * Rein datenrein: erzeugt Platzierungsdaten (Position/Rotation/Skala), kein
 * Rendering. Der Spawner (RegionAssetSpawnerComponent) konsumiert das Layout
 * als HISM/ISM.
 */
UCLASS(BlueprintType)
class WIESBADENREAL_API UWiesbadenRegionAssetGenerator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Fuehrt den Asset-Pass aus.
	 *
	 * @param Regions       Regionen-Karte (aus dem Regions-Pass der Pipeline).
	 * @param HeightSampler Terrainhoehen (darf nullptr sein -> Z=0).
	 * @param Settings      Parameter.
	 * @param OutLayout     Ergebnis (Platzierungen).
	 */
	FRegionAssetReport Generate(
		const TArray<FWiesbadenRegion>& Regions,
		const IHeightSampler* HeightSampler,
		const FRegionAssetSettings& Settings,
		FRegionAssetLayout& OutLayout);

	/**
	 * Wie Generate, haelt aber die Fahrbahnen frei.
	 *
	 * Ohne das Netz landen Baeume mitten auf der Strasse: die Streuung
	 * kennt nur das Regionspolygon, und Landnutzungsflaechen ueberlappen
	 * Strassen regelmaessig. Sichtbar wird es nicht als Kollision - die
	 * Baum-Instanzen tragen gar keine -, sondern als Verkehr, der durch
	 * Staemme faehrt.
	 */
	FRegionAssetReport GenerateClearOfRoads(
		const TArray<FWiesbadenRegion>& Regions,
		const IHeightSampler* HeightSampler,
		const FRegionAssetSettings& Settings,
		const FRoadNetwork& Network,
		FRegionAssetLayout& OutLayout);

	/** Asset-Kategorien, die eine Region liefert (leer = keine Assets). */
	static void GetCategoriesForRegion(ECityRegionType Type, TArray<ERegionAssetCategory>& OutCategories);

	/** Deterministischer String-Hash (FNV-1a) - Seed fuer die Streuung. */
	static uint32 HashString(const FString& Text);

private:
	/**
	 * Platziert eine Kategorie in einer Region (Gitter + Jitter im Polygon).
	 *
	 * @param Clearance Darf nullptr sein - dann wird nichts freigehalten.
	 * @param OutSkipped Zaehler fuer weggelassene Punkte auf Fahrbahnen.
	 */
	void ScatterRegion(const FWiesbadenRegion& Region, ERegionAssetCategory Category,
		double SpacingCm, double JitterCm, const IHeightSampler* HeightSampler,
		const FWiesbadenRoadClearance* Clearance, int32& OutSkipped,
		FRegionAssetLayout& OutLayout);

	/** Gemeinsamer Kern von Generate und GenerateClearOfRoads. */
	FRegionAssetReport GenerateInternal(
		const TArray<FWiesbadenRegion>& Regions,
		const IHeightSampler* HeightSampler,
		const FRegionAssetSettings& Settings,
		const FWiesbadenRoadClearance* Clearance,
		FRegionAssetLayout& OutLayout);

	/** Rasterabstand einer Kategorie (cm). */
	static double GetSpacingCm(ERegionAssetCategory Category, const FRegionAssetSettings& Settings);

	/** Jitter-Amplitude einer Kategorie (cm). */
	static double GetJitterCm(ERegionAssetCategory Category);

	/** True, wenn die Kategorie in den Settings aktiv ist. */
	static bool IsCategoryEnabled(ERegionAssetCategory Category, const FRegionAssetSettings& Settings);
};
