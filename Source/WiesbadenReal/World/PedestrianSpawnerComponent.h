// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GIS/WiesbadenPedestrianSimulation.h"
#include "PedestrianSpawnerComponent.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * Zeichnet die Fussgaenger der Simulation als Instanced-Meshes.
 *
 * Bewusst Instanzen statt Actors: bei bis zu einigen hundert gleichzeitigen
 * Figuren waere ein Actor je Fussgaenger reine Verschwendung - genau dieselbe
 * Entscheidung wie beim Verkehr.
 *
 * Ohne zugewiesenes Mesh greift die Komponente auf den Engine-Zylinder zurueck
 * und skaliert ihn auf Koerpermass. Das ist sichtbar ein Platzhalter und soll
 * es auch sein: eine Figur mit Schrittanimation braucht ein Skelettmesh, das
 * das Projekt nicht mitbringt.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UPedestrianSpawnerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPedestrianSpawnerComponent();

	/** Uebertraegt die aktuellen Figuren in den Instanzenpool. */
	void UpdateInstances(const TArray<FPlacedPedestrian>& Placed);

	/** Entfernt alle Instanzen. */
	void ClearInstances();

	/** Zahl der aktuell gezeichneten Figuren. */
	int32 GetVisibleCount() const;

	/**
	 * Leitet die Kleidung einer Figur DETERMINISTISCH aus ihrem Seed ab
	 * (datenrein/statisch, testbar: Vehicles.Pedestrian.Clothing): Hemdfarbe und
	 * Hosenfarbe aus festen Paletten, Hautton als 0..1. Gleicher Seed -> gleiche
	 * Kleidung; ueber viele Seeds streut es breit ueber die Paletten.
	 */
	static void ComputePedestrianColors(
		int32 Seed, FLinearColor& OutShirt, FLinearColor& OutTrouser, float& OutSkinT);

	/**
	 * Waehlt den KOERPERTYP (0 schlank, 1 breit, 2 Kind) deterministisch aus dem
	 * Seed, gewichtet. Datenrein/statisch, testbar (Vehicles.Pedestrian.BodyType):
	 * gleicher Seed -> gleicher Typ; ueber viele Seeds streut es nach den Gewichten.
	 */
	static int32 SelectPedestrianBodyType(int32 Seed);

	/**
	 * True, sobald alle Koerpertypen x Gangphasen geladen sind und animiert wird.
	 *
	 * Bei aktiver Animation liegt der GRUNDPOOL leer und alle Figuren stecken in
	 * den Pose-Pools - GetVisibleCount MUSS dann deren Summe liefern, nicht
	 * faelschlich 0 (genau dieser Zaehl-Defekt war schon einmal da).
	 */
	bool IsAnimated() const { return PoseInstances.Num() == NumBodyTypes * WalkPoseCount; }

	/** Mesh der Figur. Ohne Zuweisung wird der Engine-Zylinder verwendet. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fussgaenger")
	UStaticMesh* PedestrianMesh = nullptr;

	/** Material der Figur. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fussgaenger")
	UMaterialInterface* PedestrianMaterial = nullptr;

	/**
	 * Groesse der Figur in cm (Breite, Tiefe, Hoehe).
	 *
	 * 45 x 30 x 175 cm entspricht grob einem stehenden Menschen; der
	 * Engine-Zylinder ist 100 cm hoch und 100 cm breit und wird entsprechend
	 * skaliert.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fussgaenger")
	FVector BodySizeCm = FVector(45.0, 30.0, 175.0);

protected:
	virtual void OnRegister() override;

private:
	/** Stellt Mesh und Material am Instanzenpool sicher. */
	void EnsureMeshAndMaterial();

	/**
	 * Instanzenpools KOERPERTYP x GANGPHASE (flach: Pool = Typ*WalkPoseCount +
	 * Phase). Drei Koerpertypen (schlank, breit, Kind), je vier Standbilder des
	 * Schrittzyklus. Jede Figur landet im Pool ihres Typs UND ihrer Schrittphase
	 * und wandert beim Weitergehen durch die Phasen ihres Typs.
	 *
	 * Instanzen teilen sich EIN Mesh und lassen sich nicht einzeln per Skelett
	 * animieren - der Preis dafuer, dass Dutzende Figuren fast nichts kosten. Das
	 * ist Stop-Motion; in einigen Metern Entfernung faellt es nicht auf.
	 */
	UPROPERTY(Transient)
	TArray<UInstancedStaticMeshComponent*> PoseInstances;

	/** Zahl der Gangphasen - muss zu den importierten Meshes passen. */
	static constexpr int32 WalkPoseCount = 4;

	/** Zahl der Koerpertypen (schlank, breit, Kind). */
	static constexpr int32 NumBodyTypes = 3;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fussgaenger")
	UInstancedStaticMeshComponent* Instances = nullptr;

	/** True, sobald Mesh und Material einmal gesetzt wurden. */
	bool bMeshReady = false;
};
