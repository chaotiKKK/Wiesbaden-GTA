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
	 * Instanzenpools der vier Gangphasen.
	 *
	 * Instanzen teilen sich EIN Mesh und lassen sich deshalb nicht einzeln per
	 * Skelett animieren - das ist der Preis dafuer, dass Dutzende Fussgaenger
	 * fast nichts kosten. Statt dessen vier Standbilder des Schrittzyklus, je
	 * eines als eigener Pool. Jede Figur landet in dem Pool, der zu ihrer
	 * Schrittphase passt, und wandert beim Weitergehen weiter.
	 *
	 * Das ist Stop-Motion, kein weicher Uebergang. Bei Fussgaengern in einigen
	 * Metern Entfernung ist der Unterschied nicht auszumachen.
	 */
	UPROPERTY(Transient)
	TArray<UInstancedStaticMeshComponent*> PoseInstances;

	/** Zahl der Gangphasen - muss zu den importierten Meshes passen. */
	static constexpr int32 WalkPoseCount = 4;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fussgaenger")
	UInstancedStaticMeshComponent* Instances = nullptr;

	/** True, sobald Mesh und Material einmal gesetzt wurden. */
	bool bMeshReady = false;
};
