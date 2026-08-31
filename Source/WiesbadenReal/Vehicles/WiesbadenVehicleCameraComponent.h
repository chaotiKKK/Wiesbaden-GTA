// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "InputCoreTypes.h"

#include "WiesbadenVehicleCameraComponent.generated.h"

class APlayerController;
class UCameraComponent;
class USpringArmComponent;

/** Kameramodi der generischen Fahrzeug-Kamera. */
UENUM(BlueprintType)
enum class EWiesbadenVehicleCameraMode : uint8
{
	/** Third-Person-Follow: Kamera haengt am Fahrzeug und dreht mit ihm. */
	Follow  UMETA(DisplayName = "Follow"),
	/** Frei um das Fahrzeug schwenkbare Orbit-Kamera (Pfeiltasten). */
	Orbit   UMETA(DisplayName = "Orbit"),
	/** Cockpit-/First-Person-Ansicht (an einem Cockpit-Socket). */
	Cockpit UMETA(DisplayName = "Cockpit")
};

/**
 * Generische, wiederverwendbare Fahrzeug-Kamera.
 *
 * Wird an ein beliebiges Fahrzeug/einen Pawn gehaengt und erzeugt selbst:
 *  - USpringArmComponent als Kind der Fahrzeug-Wurzel -> dreht bei jeder Kurve
 *    automatisch mit (loest das "Kamera dreht nicht mit"-Problem).
 *  - Third-Person-Kamera am Boom-Ende (Follow/Orbit).
 *  - Cockpit-Socket + Cockpit-Kamera (First Person).
 *
 * Umschaltung per Taste: Follow -> Orbit -> Cockpit -> Follow ...
 * Die Orbit-Kamera wird mit den Pfeiltasten geschwenkt.
 *
 * Voraussetzung: Der Owner sollte ein APawn sein, der von einem
 * APlayerController besessen wird (Eingabe wird ueber den Controller gepollt).
 * Ist der Owner kein Pawn, faellt die Eingabe auf den ersten Spieler-Controller
 * der Welt zurueck.
 *
 * Das Kamera-Rig wird erst in BeginPlay erzeugt (nicht im CDO), damit keine
 * Laufzeit-Komponenten im Klassenvorlagen-Objekt entstehen.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenVehicleCameraComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWiesbadenVehicleCameraComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Umschalten der Kamera (Follow -> Orbit -> Cockpit). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Kamera")
	void CycleCameraMode();

	/** Aktuellen Kameramodus anwenden (aktiviert die passende Kamera). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Kamera")
	void ApplyCameraMode();

	/** Aktueller Kameramodus. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Kamera")
	EWiesbadenVehicleCameraMode GetCameraMode() const { return CameraMode; }

	/** Taste zum Umschalten der Kamera. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera")
	FKey ToggleKey = EKeys::C;

	/**
	 * Wenn true, bleibt die Kamera fest im Follow-Modus hinter dem Fahrzeug:
	 * Umschalten (ToggleKey) und Orbit-Schwenk (Pfeiltasten) werden ignoriert
	 * und der Modus wird bei jedem ApplyCameraMode auf Follow zurueckgesetzt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Kamera")
	bool bLockFollowMode = false;

	/** Abstand der Follow-Kamera zum Fahrzeug (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera", meta = (ClampMin = "50.0"))
	float FollowArmLength = 900.0f;

	/** Neigung der Follow-Kamera (negativ = leicht von oben). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera")
	float FollowPitchOffset = -14.0f;

	/** Schwenkgeschwindigkeit der Orbit-Kamera (deg/s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera", meta = (ClampMin = "0.0"))
	float OrbitTurnRate = 70.0f;

	/**
	 * Mausempfindlichkeit beim Umschauen.
	 *
	 * Umschauen gehoert auf die Maus. Vorher liess sich die Kamera nur ueber
	 * die Pfeiltasten Grad fuer Grad drehen, was sich zaeh anfuehlte.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera", meta = (ClampMin = "0.05"))
	float MouseSensitivity = 2.2f;

	/** Glattung der Kamera-Bewegung. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera", meta = (ClampMin = "0.01"))
	float CameraResponse = 4.0f;

	/**
	 * Horizont ruhig halten (fuer den Hubschrauber).
	 *
	 * Der Ausleger erbt sonst Nicken UND Rollen 1:1 vom Rumpf - bei jedem
	 * Steuerausschlag kippte die ganze Welt mit. Mit ruhigem Horizont
	 * neigt sich der Hubschrauber IM Bild, nicht das Bild selbst; so machen
	 * es praktisch alle Flugspiele in der Verfolgeransicht.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera")
	bool bLevelHorizon = false;

	/** Positions-Nachlauf des Auslegers (0 = aus). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera", meta = (ClampMin = "0.0"))
	float PositionLagSpeed = 0.0f;

	/** Position des Cockpit-Sockets relativ zum Fahrzeug (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera")
	FVector CockpitOffset = FVector(95.0f, 0.0f, 140.0f);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Kamera")
	USpringArmComponent* SpringArm = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Kamera")
	UCameraComponent* ThirdPersonCamera = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Kamera")
	USceneComponent* CockpitSocket = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Kamera")
	UCameraComponent* CockpitCamera = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wiesbaden|Kamera")
	EWiesbadenVehicleCameraMode CameraMode = EWiesbadenVehicleCameraMode::Follow;

private:
	void CreateCameraRig();
	void HandleInput(float DeltaTime);
	void UpdateBoom(float DeltaTime);

	APlayerController* GetPlayerController() const;

	FRotator OrbitOffset = FRotator::ZeroRotator;
	bool bCameraToggleHeld = false;
	bool bRigCreated = false;
};
