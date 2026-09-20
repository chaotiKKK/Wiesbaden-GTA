// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/RoadFurnitureGenerator.h"

#include "StreetFurnitureShapes.generated.h"

/**
 * Aus welchem Engine-Basismesh ein Moebelteil besteht.
 *
 * Beide Meshes sind 100 cm gross und um den Ursprung zentriert
 * (/Engine/BasicShapes/Cube bzw. /Engine/BasicShapes/Cylinder) - die
 * Skalierung in FFurniturePart::Transform ist darum schlicht Groesse/100.
 */
UENUM(BlueprintType)
enum class EFurnitureMeshKind : uint8
{
	Box      UMETA(DisplayName = "Kasten"),
	Cylinder UMETA(DisplayName = "Zylinder"),
	MAX      UMETA(Hidden)
};

/** Werkstoff eines Moebelteils - bestimmt, welchen Draw-Call es teilt. */
UENUM(BlueprintType)
enum class EFurnitureMaterialKind : uint8
{
	/** Verzinkt/lackiert: Poller, Wangen, Beine, Kappen. */
	Metal  UMETA(DisplayName = "Metall"),
	/** Sitzlatten und Tischplatten. */
	Wood   UMETA(DisplayName = "Holz"),
	/** Farbig: gelber Briefkasten, roter Hydrant, Automatenfront. */
	Signal UMETA(DisplayName = "Signalfarbe"),
	MAX    UMETA(Hidden)
};

/**
 * Ein einzelnes Teil eines Strassenmoebels, fertig in Weltkoordinaten.
 *
 * Der Spawner macht daraus eine Instanz im passenden HISM - eine Instanz je
 * Teil, und alle Teile mit gleichem Mesh UND gleichem Werkstoff teilen sich
 * einen Draw-Call.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FFurniturePart
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	EFurnitureMeshKind Mesh = EFurnitureMeshKind::Box;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	EFurnitureMaterialKind Material = EFurnitureMaterialKind::Metal;

	UPROPERTY(BlueprintReadOnly, Category = "Furniture")
	FTransform Transform;
};

/**
 * Die Masse der Strassenmoebel in Zentimetern.
 *
 * Bewusst als Struktur mit UPROPERTYs und nicht als Konstanten im Code: die
 * Werte sind Gestaltung, keine Logik. Sie stehen im Spawner als eine
 * bearbeitbare Eigenschaft, damit "die Baenke sind zu klein" eine Einstellung
 * ist und keine Codeaenderung. Die Vorgaben sind die ueblichen deutschen
 * Strassenmoebel-Masse.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FStreetFurnitureDimensions
{
	GENERATED_BODY()

	// -- Bank ----------------------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Bank", meta = (ClampMin = "10.0"))
	double BenchLengthCm = 180.0;
	UPROPERTY(EditAnywhere, Category = "Bank", meta = (ClampMin = "10.0"))
	double BenchDepthCm = 45.0;
	UPROPERTY(EditAnywhere, Category = "Bank", meta = (ClampMin = "10.0"))
	double BenchSeatHeightCm = 45.0;
	UPROPERTY(EditAnywhere, Category = "Bank", meta = (ClampMin = "0.0"))
	double BenchBackHeightCm = 40.0;

	// -- Poller --------------------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Poller", meta = (ClampMin = "5.0"))
	double BollardHeightCm = 90.0;
	UPROPERTY(EditAnywhere, Category = "Poller", meta = (ClampMin = "1.0"))
	double BollardRadiusCm = 9.0;

	// -- Abfallkorb ----------------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Abfallkorb", meta = (ClampMin = "5.0"))
	double BasketHeightCm = 50.0;
	UPROPERTY(EditAnywhere, Category = "Abfallkorb", meta = (ClampMin = "1.0"))
	double BasketRadiusCm = 20.0;
	/** Hoehe der Oberkante ueber dem Boden - darunter steht das Standrohr. */
	UPROPERTY(EditAnywhere, Category = "Abfallkorb", meta = (ClampMin = "10.0"))
	double BasketTopHeightCm = 95.0;

	// -- Hydrant -------------------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Hydrant", meta = (ClampMin = "5.0"))
	double HydrantHeightCm = 80.0;
	UPROPERTY(EditAnywhere, Category = "Hydrant", meta = (ClampMin = "1.0"))
	double HydrantRadiusCm = 11.0;

	// -- Briefkasten ---------------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Briefkasten", meta = (ClampMin = "5.0"))
	double PostBoxWidthCm = 40.0;
	UPROPERTY(EditAnywhere, Category = "Briefkasten", meta = (ClampMin = "5.0"))
	double PostBoxDepthCm = 30.0;
	UPROPERTY(EditAnywhere, Category = "Briefkasten", meta = (ClampMin = "5.0"))
	double PostBoxHeightCm = 55.0;
	/** Unterkante des Kastens ueber dem Boden (Standrohr). */
	UPROPERTY(EditAnywhere, Category = "Briefkasten", meta = (ClampMin = "0.0"))
	double PostBoxStandHeightCm = 75.0;

	// -- Automat -------------------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Automat", meta = (ClampMin = "10.0"))
	double VendingWidthCm = 80.0;
	UPROPERTY(EditAnywhere, Category = "Automat", meta = (ClampMin = "10.0"))
	double VendingDepthCm = 40.0;
	UPROPERTY(EditAnywhere, Category = "Automat", meta = (ClampMin = "10.0"))
	double VendingHeightCm = 170.0;

	// -- Recycling-Container -------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Recycling", meta = (ClampMin = "10.0"))
	double RecyclingWidthCm = 120.0;
	UPROPERTY(EditAnywhere, Category = "Recycling", meta = (ClampMin = "10.0"))
	double RecyclingDepthCm = 120.0;
	UPROPERTY(EditAnywhere, Category = "Recycling", meta = (ClampMin = "10.0"))
	double RecyclingHeightCm = 140.0;

	// -- Picknick-Tisch ------------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Picknick", meta = (ClampMin = "10.0"))
	double PicnicLengthCm = 200.0;
	UPROPERTY(EditAnywhere, Category = "Picknick", meta = (ClampMin = "10.0"))
	double PicnicTableWidthCm = 80.0;
	UPROPERTY(EditAnywhere, Category = "Picknick", meta = (ClampMin = "10.0"))
	double PicnicTableHeightCm = 75.0;
	UPROPERTY(EditAnywhere, Category = "Picknick", meta = (ClampMin = "10.0"))
	double PicnicSeatHeightCm = 45.0;

	/** Staerke der Latten, Platten und Bleche (cm). */
	UPROPERTY(EditAnywhere, Category = "Allgemein", meta = (ClampMin = "0.5"))
	double PlankThicknessCm = 5.0;
};

/**
 * Baut die Teile eines Strassenmoebels - datenrein, ohne Engine-Komponenten.
 *
 * WARUM EIGENE EINHEIT: der Ausstattungs-Spawner ist bereits fuer Schilder,
 * Leitpfosten, Markierungen und Laternen zustaendig. Die Frage "woraus besteht
 * eine Bank" hat damit nichts zu tun und laesst sich hier ohne Welt, ohne
 * Komponente und ohne Mesh pruefen - der Spawner macht aus den Teilen nur noch
 * Instanzen.
 */
namespace WiesbadenStreetFurniture
{
	/**
	 * Zerlegt ein platziertes Moebel in seine Teile (Weltkoordinaten).
	 *
	 * @param Instance   Ergebnis des Bake-Passes (Art, Ort, Drehung, Variante).
	 * @param Dimensions Masse.
	 * @param OutParts   Teile werden ANGEHAENGT (Aufrufer sammelt ueber alle
	 *                   Moebel und reserviert einmal).
	 */
	WIESBADENREAL_API void BuildParts(
		const FFurnitureInstance& Instance,
		const FStreetFurnitureDimensions& Dimensions,
		TArray<FFurniturePart>& OutParts);

	/** Teilezahl einer Art/Variante - fuer Reserve und Pruefungen. */
	WIESBADENREAL_API int32 GetPartCount(EStreetFurnitureKind Kind, int32 Variant);
}
