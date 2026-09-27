// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GIS/BuildingGenerator.h"
#include "BuildingCollisionSpawnerComponent.generated.h"

class UBoxComponent;
struct FRoadNetwork;

/**
 * Haelt Kollisionskoerper fuer die Gebaeude in Spielernaehe.
 *
 * Warum ueberhaupt: Die Stadt-Meshes werden mit bCreateCollision = false
 * gebacken - Dreieckskollision fuer 5.825 Gebaeude-Abschnitte waere in
 * Rechenzeit wie Speicher unbezahlbar. Der Spieler fuhr dadurch mitten durch
 * die Haeuser. `AWiesbadenCar` bewegt sich per AddActorWorldOffset MIT Sweep,
 * findet also durchaus Kollision - es war schlicht keine da.
 *
 * Loesung wie beim Verkehr: ein fester Pool von Box-Koerpern, der dem Spieler
 * folgt. Jedes `FGeneratedBuilding` traegt bereits seine `Bounds` (achsparallel,
 * ueberlebt das Backen als UPROPERTY am WorldBuilder) - daraus entsteht der
 * Koerper direkt, ohne Geometrie zu kochen.
 *
 * Bewusste Naeherung: Die Box ist achsparallel. Bei einem gedrehten Gebaeude
 * deckt sie etwas mehr ab als der Grundriss; das Fahrzeug haelt dann wenige
 * Dezimeter vor der Wand statt an ihr. Das ist dem Hindurchfahren klar
 * vorzuziehen und kostet nichts.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UBuildingCollisionSpawnerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UBuildingCollisionSpawnerComponent();

	/** Marke an den Kollisionskoerpern - unterscheidet sie vom Verkehr. */
	static const FName BuildingBodyTag;

	/** Uebernimmt die Gebaeudeliste (aus dem gebackenen WorldBuilder). */
	void SetBuildings(const TArray<FGeneratedBuilding>& InBuildings);

	/**
	 * Kuerzt die Grundriss-Kaesten an Fahrspuren (nach SetBuildings aufrufen).
	 *
	 * Der Kasten ist das minimale gedrehte Rechteck um den GANZEN Grundriss. Bei
	 * einer Ueberbauung (LuisenForum ueber der Schwalbacher Strasse) oder einem
	 * L-foermigen Haus an der Ecke deckt er die Strasse mit ab - eine unsichtbare
	 * Wand auf allen Spuren, an der Spielerauto und Busse haengen blieben. Durch
	 * ein echtes Gebaeude fuehrt aber keine Fahrbahn: laufen Spuren (Strassen-
	 * klassen bis living_street, auf Hoehe des Gebaeudefusses) durch den Kasten,
	 * wird er an der Strasse abgeschnitten; bliebe zu wenig uebrig, bekommt das
	 * Gebaeude keinen Kasten (die Strasse geht vor).
	 */
	void ClipAgainstRoads(const FRoadNetwork& Network);

	/** Ergebnis von ClipBoxAgainstPoints. */
	enum class EBoxClip : uint8 { Untouched, Clipped, Removed };

	/**
	 * Datenrein: Kasten (Mitte, Halbmasse, Gier) so kuerzen, dass kein Punkt
	 * naeher als MarginCm darin liegt. Geschnitten wird an der Seite, die am
	 * meisten Flaeche uebrig laesst; bleibt weniger als MinKeepFraction,
	 * lautet das Ergebnis Removed (Kasten unveraendert).
	 */
	static EBoxClip ClipBoxAgainstPoints(FVector2D& Center, FVector2D& Extent, float YawDegrees,
		const TArray<FVector2D>& Points, double MarginCm, double MinKeepFraction);

	/** Gekuerzte bzw. entfernte Kaesten beim letzten ClipAgainstRoads. */
	int32 GetClippedCount() const { return ClippedCount; }
	int32 GetRemovedCount() const { return RemovedCount; }

	/** Setzt die Koerper auf die Gebaeude um den Beobachter um. */
	void UpdateAround(const FVector& Observer);

	/** Anzahl aktuell belegter Koerper - Kennzahl fuer die Bilanz. */
	int32 GetActiveBodyCount() const { return ActiveBodyCount; }

	/**
	 * Waehlt die naechstgelegenen Gebaeude im Radius.
	 *
	 * Datenrein und statisch, damit ohne Welt testbar. Gemessen wird
	 * HORIZONTAL: ein Gebaeude den Hang hinauf ist fuer den Fahrer genauso
	 * nah wie eines auf gleicher Hoehe.
	 */
	static void SelectNearestBuildings(
		const TArray<FGeneratedBuilding>& Buildings,
		const FVector& Center,
		double RadiusCm,
		int32 MaxCount,
		TArray<int32>& OutIndices);

	/** Zahl der Kollisionskoerper im Pool. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Gebaeude", meta = (ClampMin = "1"))
	int32 BodyCount = 96;

	/** Radius, in dem Gebaeude Kollision bekommen, in Metern. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Gebaeude", meta = (ClampMin = "10"))
	double CollisionRadiusMeters = 150.0;

private:
	/** Legt den Pool an (einmalig). */
	void EnsurePool();

	UPROPERTY(Transient)
	TArray<UBoxComponent*> Bodies;

	UPROPERTY(Transient)
	TArray<FGeneratedBuilding> Buildings;

	int32 ActiveBodyCount = 0;
	int32 ClippedCount = 0;
	int32 RemovedCount = 0;
};
