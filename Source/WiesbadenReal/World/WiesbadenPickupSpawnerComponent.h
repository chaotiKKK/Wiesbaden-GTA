// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GIS/WiesbadenPickupSpots.h"
#include "World/WiesbadenPickup.h"

#include "WiesbadenPickupSpawnerComponent.generated.h"

class AWiesbadenPickup;
class UStaticMesh;

/**
 * Spawnt Pickup-Actors aus dem Platzierungs-Layout der Pipeline und bindet
 * die WIRKUNG an Fahrzeug und Fuss-Pawn.
 *
 * Trennung der Verantwortlichkeiten:
 *  - GIS/WiesbadenPickupSpots.h  (datenrein) entscheidet WO ein Pickup steht;
 *  - World/WiesbadenPickup.h     (Actor) erkennt nur das Einsammeln und
 *    kennt keine Gesundheits-/Fahrzeug-Systeme;
 *  - DIESE Komponente verbindet beides: sie erzeugt die Actors und reagiert
 *    auf OnCollected mit der konkreten Wirkung (Tank fuellen, Heilen).
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenPickupSpawnerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWiesbadenPickupSpawnerComponent();

	/** Spawnt Pickups fuer alle Plaetze des Layouts (vorherige werden entfernt). */
	void SpawnFromLayout(const FWiesbadenPickupSpotLayout& Layout);

	/** Entfernt alle gespawnten Pickups. */
	void ClearPickups();

	/** Zahl der aktuell lebenden Pickups. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Pickups")
	int32 GetPickupCount() const;

protected:
	virtual void BeginPlay() override;

	/** Erzeugt einen Pickup-Actor an der Position und konfiguriert ihn. */
	AWiesbadenPickup* SpawnPickup(const FWiesbadenPickupSpot& Spot);

	/** Wirkungsbinding (OnCollected); UFUNCTION wegen AddDynamic. */
	UFUNCTION()
	void HandlePickupCollected(AWiesbadenPickup* Pickup, AActor* Collector);

private:
	/** Wandelt die Daten-Art in die Actor-Art um. */
	static EWiesbadenPickupKind MapKind(EWiesbadenPickupSpotKind Kind);

	/** Schwache Referenzen - zerstoerte Pickups (einsammelbar) fallen raus. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AWiesbadenPickup>> LivePickups;

	/** Sichtbares Mesh der Pickups (Engine-Kugel, farbig eingefaerbt). */
	UPROPERTY(Transient)
	UStaticMesh* PickupMesh = nullptr;

	/** Farbe der Treibstoff-Pickups (Kanister-Gelb). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Pickups")
	FLinearColor FuelColor = FLinearColor(0.92f, 0.58f, 0.06f);

	/** Farbe der Gesundheits-Pickups (Apotheken-Rot). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Pickups")
	FLinearColor HealthColor = FLinearColor(0.82f, 0.10f, 0.12f);

	/** Schwebehoehe des Pickups ueber dem Boden (cm) - macht es sichtbar. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Pickups", meta = (ClampMin = "0.0"))
	float HoverHeightCm = 120.0f;

	/** Treibstoffmenge je Pickup (Liter). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Pickups", meta = (ClampMin = "1"))
	int32 FuelAmountLiters = 20;

	/** Heilung je Gesundheit-Pickup (Punkte). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Pickups", meta = (ClampMin = "1"))
	int32 HealthAmount = 50;
};
