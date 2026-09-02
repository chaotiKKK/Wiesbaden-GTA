// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "WiesbadenPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;

/** Art des Pickups - bestimmt, was der Sammler beim Aufnehmen erhaelt. */
UENUM(BlueprintType)
enum class EWiesbadenPickupKind : uint8
{
	Health  UMETA(DisplayName = "Gesundheit"),
	Fuel    UMETA(DisplayName = "Treibstoff"),
	Repair  UMETA(DisplayName = "Reparatur")
};

class AWiesbadenPickup;

/** Wird ausgeloest, wenn das Pickup eingesammelt wurde (Blueprint-bindbar). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWiesbadenPickupCollected, AWiesbadenPickup*, Pickup, AActor*, Collector);

/**
 * Einsammelbares Welt-Objekt (Gesundheit / Treibstoff / Reparatur).
 *
 * Ein kugelfoermiger Trigger ist die Wurzel und erkennt ueberlappende Pawns;
 * beim Kontakt loest Collect() aus, feuert OnCollected und zerstoert das Pickup
 * (optional). Das sichtbare Mesh haengt am Trigger, hat keine eigene Kollision
 * und dreht sich langsam, damit das Pickup auffaellt.
 *
 * Die eigentliche Wirkung (Heilen, Auftanken, Reparieren) liegt bewusst NICHT
 * hier, sondern beim Sammler bzw. an OnCollected gebundenen Logik - so bleibt
 * das Pickup von konkreten Health-/Fahrzeug-Systemen entkoppelt.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenPickup : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenPickup();

	/** Nimmt das Pickup auf: feuert OnCollected und zerstoert sich ggf. selbst. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Pickup")
	void Collect(AActor* Collector);

	/** Art des Pickups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Pickup")
	EWiesbadenPickupKind Kind = EWiesbadenPickupKind::Health;

	/** Menge, die der Sammler erhaelt (Health-Punkte, Liter, Reparatur-Prozent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Pickup", meta = (ClampMin = "0"))
	int32 Amount = 25;

	/** Drehgeschwindigkeit des Mesh um die Hochachse (Grad/Sekunde). 0 = keine Drehung. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Pickup")
	float SpinDegPerSec = 90.0f;

	/** Wenn true, wird der Actor nach dem Einsammeln zerstoert. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Pickup")
	bool bDestroyOnCollect = true;

	/** Nur Pawns loesen das Pickup aus (verhindert Aufnahme durch Projektile o.ae.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Pickup")
	bool bOnlyPawnsCollect = true;

	/** Broadcast beim Einsammeln - hier bindet die Wirkungslogik an. */
	UPROPERTY(BlueprintAssignable, Category = "Wiesbaden|Pickup")
	FOnWiesbadenPickupCollected OnCollected;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Overlap-Handler des Triggers (im BeginPlay gebunden; MUSS UFUNCTION sein). */
	UFUNCTION()
	void HandleTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Kugelfoermiger Aufnahme-Trigger und Wurzel des Actors. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wiesbaden|Pickup")
	USphereComponent* Trigger = nullptr;

	/** Sichtbares Mesh des Pickups (ohne eigene Kollision). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wiesbaden|Pickup")
	UStaticMeshComponent* Mesh = nullptr;

private:
	/** Verhindert Doppel-Aufnahme, falls in einem Frame mehrere Overlaps eintreffen. */
	bool bCollected = false;
};
