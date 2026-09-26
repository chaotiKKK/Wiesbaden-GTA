// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "WiesbadenHeliGunComponent.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class USoundBase;

/**
 * Bordgeschuetz des Hubschraubers: schwenkbares Turmgeschuetz auf der
 * Steuerbordseite, mit echter Trefferwirkung.
 *
 * WARUM EINE KOMPONENTE UND NICHT IM PAWN: Das Geschuetz hat eine eigene
 * Zustandsmaschine (Bereitschaft, Feuerfolge, Munition, Temperatur,
 * Rueckstoss), die unabhaengig vom Flugzustand laeuft. Im Pawn wuerde sie
 * zwischen Rotorphysik, Kamera und Schadenslogik verschwinden.
 *
 * BAUART: Die Ka-52 traegt rechts eine 30-mm-Kanone (2A42) auf einem
 * Pylon, links eine 9A49-Gondel. Hier wird die rechte Seite gebaut, weil
 * sie im Modell einen sichtbaren Pylon hat und der Spieler sie im Bild hat.
 * Der Turm sitzt deshalb nicht unter dem Rumpf, sondern seitlich - so
 * schiesst er auch nach vorne-hinten frei, ohne am Rumpf vorbei.
 *
 * SICHTBARKEIT DER MUENDUNG: Die Mündung sitzt am echten Rohrende
 * (GetMuzzleLocation), nicht an der Turmmitte. Sonst schiesst der
 * Hubschrauber aus dem Rumpf heraus und der Spieler sieht Mündungsfeuer
 * dort nicht, wo er feuert.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenHeliGunComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWiesbadenHeliGunComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Feuert, solange bPressed gilt.
	 *
	 * Bewusst ein gehaltenes Kommando statt eines einzelnen Schusses: eine
	 * Bordkanone feuert in Salven, und der Rueckstoss gehoert zur Bedienung.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|MG")
	void SetTriggerHeld(bool bPressed);

	/** Richtet das Geschuetz (Turm nach, Hohenrichtung -1..1). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|MG")
	void Aim(float Horizontal, float Vertical);

	/** Schwenkt das Geschuetz ohne feuern - auch wenn der Flieger keine Waffe fuehrt. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|MG")
	void AimAt(const FVector& Weltziel);

	/** Kuehlung/Munition fuellen (z. B. beim Respawn). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|MG")
	void Reload();

	/** Schadensschaden pro Schuss. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "0.0"))
	float Damage = 34.0f;

	/** Schuesse je Sekunde. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float RoundsPerMinute = 500.0f;

	/** Munition im Magazin. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "0"))
	int32 Magazine = 300;

	/** Munition je Rohr (das Geschuetz feuert abwechselnd links/rechts). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "0"))
	int32 RoundsPerBarrel = 150;

	/** Reichweite des Strahls in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "100.0"))
	float TraceRange = 6000.0f;

	/** Streukreis in Grad - ohne Streu trifft man aus 500 m einen Hubschrauber wie eine Tuer. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float ConeHalfAngleDeg = 0.65f;

	/** Temperatur je Schuss; ueber Ueberhitzung sperrt das Geschuetz. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "0.1"))
	float HeatPerShot = 0.055f;

	/** Kuehlung je Sekunde. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "0.01"))
	float CoolingPerSecond = 0.22f;

	/** Temperatur, ab der gesperrt wird. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "0.1"))
	float OverheatAt = 1.0f;

	/** Hoehe des Rueckstosses je Schuss in Grad. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float RecoilPitch = 1.5f;

	/** Wie schnell der Rueckstoss wieder zurueckgeht (1/s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG", meta = (ClampMin = "0.1"))
	float RecoilReturn = 7.0f;

	/** Schussgeraeusch (optional - fehlt es, wird nichts abgespielt). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|MG")
	USoundBase* FireSound = nullptr;

	/** Rohr samt Mesh (Geschuetzmodell). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|MG")
	UStaticMeshComponent* GetBarrelMesh() const { return Barrel; }

	/** Mündung: dort entsteht Mündungsfeuer, Rauch und Ton. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|MG")
	USceneComponent* GetMuzzlePoint() const { return MuzzlePoint; }

	/** Gierdrehung des Turms (Kind von ModelSpace). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|MG")
	USceneComponent* GetTurretYaw() const { return TurretYaw; }

	/** Modellraum des Geraets: traegt Massstab und Gierdrehung des Pawns. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|MG")
	USceneComponent* GetModelSpace() const { return ModelSpace; }

	/** Munition uebrig? */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|MG")
	int32 GetRemainingRounds() const { return RoundsLeft; }

	/** Ueberhitzt? (dann feuert es erst nach dem Abkuehlen wieder) */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|MG")
	bool IsOverheated() const { return Heat >= OverheatAt; }

	/** Schon gefeuert worden? (fuer Anzeige und Test) */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|MG")
	int32 GetShotsFired() const { return ShotsFired; }

	/** Weltort der Mündung - dort blitzt es, dort kommt der Ton her. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|MG")
	FVector GetMuzzleLocation() const;

	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|MG")
	FRotator GetAimRotation() const;

	/**
	 * Massstab und Gierdrehung des Modells uebernehmen (siehe Lichtbastel).
	 *
	 * Wichtig fuer die Treffer: Das Geschuetz sitzt in Modellkoordinaten, und
	 * ein Rohr, das im Rumpf sitzt, schiesst aus dem Rumpf heraus. Mit dem
	 * Modellraum sitzt es dort, wo die Kanonenhalterung des Modells sitzt.
	 */
	void SetModelTransform(float InScale, const FRotator& InYaw);

protected:
	/** Ein Schuss: Streuung, Treffer, Schaden, Tracer, Ton. */
	void FeuereSchuss();

	/** Knoten im Modellraum (traegt Massstab, Drehung und Sitz des Geraets). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|MG")
	USceneComponent* ModelSpace = nullptr;

	/** Gierturm (links/rechts schwenkend). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|MG")
	USceneComponent* TurretYaw = nullptr;

	/** Nickturm (hoch/runter). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|MG")
	USceneComponent* TurretPitch = nullptr;

	/** Geschuetzrohr samt Pylon und Muendungsbremse. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|MG")
	UStaticMeshComponent* Barrel = nullptr;

	/** Mündungsfeuer: kurzer, sehr heller Punkt. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|MG")
	UPointLightComponent* MuzzleFlash = nullptr;

	/** Nullstelle der Mündung (Rohrende). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|MG")
	USceneComponent* MuzzlePoint = nullptr;

	float WantedYaw = 0.0f;
	float WantedPitch = 0.0f;
	float TurretYawDeg = 0.0f;
	float TurretPitchDeg = 0.0f;
	float Recoil = 0.0f;

	float FireAccumulator = 0.0f;
	float FlashTimer = 0.0f;
	float Heat = 0.0f;
	int32 RoundsLeft = 0;
	int32 ShotsFired = 0;
	bool bTriggerHeld = false;
};
