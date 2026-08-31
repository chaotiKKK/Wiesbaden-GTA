// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WiesbadenRegion.generated.h"

class UGeoCoordinateConverter;
struct FOSMDataSet;

/**
 * Regionstyp einer Stadt-Region, abgeleitet aus OSM-Flaechen-Tags
 * (WorldClaw-Schritt 3: Welt in logische Regionen unterteilen - ein
 * Fischerdorf landet am See, nicht in der Wueste).
 */
UENUM(BlueprintType)
enum class ECityRegionType : uint8
{
	Other,
	Water,
	Green,
	Residential,
	Commercial,
	Industrial
};

/**
 * Eine Stadt-Region: zusammenhaengende OSM-Flaeche (landuse/natural/leisure/
 * waterway) mit Typ, Name und Geometrie in Weltkoordinaten (cm).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenRegion
{
	GENERATED_BODY()

	/** Name aus name=* oder Typ-Fallback (z. B. "Wasser"). */
	UPROPERTY(BlueprintReadOnly, Category = "Regions")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Regions")
	ECityRegionType Type = ECityRegionType::Other;

	/** Umschliessende Box in Weltkoordinaten (cm) - schneller Vorab-Check. */
	UPROPERTY(BlueprintReadOnly, Category = "Regions")
	FBox2D Bounds = FBox2D(ForceInit);

	/** Exakter Umring in Weltkoordinaten (cm) fuer Punkt-in-Polygon-Tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Regions")
	TArray<FVector2D> Polygon;

	UPROPERTY(BlueprintReadOnly, Category = "Regions")
	double AreaSqm = 0.0;
};

/** Diagnose des Regionen-Passes (inkl. Gebaeude-Zuordnung). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRegionGenerationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Regions")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Regions")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Regions")
	int32 RegionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Regions")
	int32 WaterRegionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Regions")
	int32 GreenRegionCount = 0;

	FString ToString() const;
};

/**
 * Erzeugt Stadt-Regionen aus OSM-Flaechen (datenrein, deterministisch,
 * testbar).
 *
 * VERFAHREN (WorldClaw-Schritt 3 - "Welt in Regionen unterteilen"):
 * Geschlossene OSM-Ways mit landuse/natural/leisure/waterway-Tags werden in
 * Weltkoordinaten projiziert und klassifiziert (Wasser/Gruen/Wohnen/Gewerbe/
 * Industrie); zu kleine Flecken werden verworfen. Die Zuordnung der Gebaeude
 * zu ihren Regionen (RegionType/RegionName am FGeneratedBuilding) - inkl.
 * Wasser-Filter (kein Haus im See) und regionalem Hoehen-/Fassaden-Feintuning
 * - erfolgt beim Bauen im UBuildingGenerator ueber die RegionMap
 * (FBuildingGenerationSettings), damit regionale Entscheidungen VOR der
 * Mesh-Erzeugung getroffen werden (paperkonform, arXiv 2608.05248, Abschnitt 2.3).
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenRegionGenerator : public UObject
{
	GENERATED_BODY()

public:
	/** Minimale Regionenflaeche in m^2; kleinere Flecken werden verworfen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regions", meta = (ClampMin = "1.0"))
	double MinRegionAreaSqm = 500.0;

	/** Klassifiziert OSM-Tags eines Ways in einen Regionstyp (Other = irrelevant). */
	static ECityRegionType ClassifyTags(const TMap<FName, FString>& Tags);

	/**
	 * Erzeugt Regionen aus geschlossenen OSM-Flaechen (deterministisch:
	 * sortierte Way-Ids). Offene Ringe, unvollstaendige Node-Referenzen und
	 * Flecken unter MinRegionAreaSqm werden uebersprungen.
	 */
	FRegionGenerationReport Generate(
		const FOSMDataSet& DataSet,
		const UGeoCoordinateConverter* Converter,
		TArray<FWiesbadenRegion>& OutRegions);

	/**
	 * Regionstyp an einem Punkt (Weltkoordinaten). Prioritaet:
	 * Wasser > Gruen > Residential > Commercial > Industrial, sonst Other.
	 * Nur Regionen, deren Bounds den Punkt enthalten, werden exakt per
	 * Polygon getestet.
	 */
	static ECityRegionType ClassifyPoint(const TArray<FWiesbadenRegion>& Regions, const FVector2D& Point);

	/**
	 * Region an einem Punkt nach der Prioritaetskette (oder nullptr).
	 * Liefert auch den Namen - z. B. fuer den BuildingGenerator, der
	 * Gebaeude direkt beim Bauen ihrer Region zuordnet.
	 */
	static const FWiesbadenRegion* FindRegionAt(
		const TArray<FWiesbadenRegion>& Regions,
		const FVector2D& Point);

	/**
	 * Zaehlt Stadt-Regionen je Typ (datenrein, deterministisch) - fuer die
	 * Zusammenfassungs-Logs der Build-Pipeline (Editor- und Laufzeit-Pfad).
	 * Other wird bewusst nicht gezaehlt (kein echter Regionstyp).
	 */
	static void GetRegionTypeCounts(
		const TArray<FWiesbadenRegion>& Regions,
		int32& OutWater, int32& OutGreen, int32& OutResidential,
		int32& OutCommercial, int32& OutIndustrial);

private:
	/** Anzeigename-Fallback je Typ (ASCII). */
	static FString GetTypeName(ECityRegionType Type);
};
