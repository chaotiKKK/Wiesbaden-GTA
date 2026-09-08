// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "InputCoreTypes.h"

#include "WiesbadenVehicleCameraComponent.generated.h"

class AActor;
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

	/** Kameramodus direkt setzen (fuer Dev-Befehle/Skripte). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Kamera")
	void SetCameraMode(EWiesbadenVehicleCameraMode Mode) { CameraMode = Mode; ApplyCameraMode(); }

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

	/**
	 * Schwenkgeschwindigkeit des Gamepad-Rechtssticks (deg/s).
	 *
	 * Umsehen mit dem rechten Stick war bisher gar nicht verdrahtet - am
	 * Gamepad liess sich die Kamera ueberhaupt nicht drehen.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera", meta = (ClampMin = "0.0"))
	float GamepadLookRate = 130.0f;

	/**
	 * Wie schnell die Follow-Kamera ohne Eingabe hinter das Fahrzeug
	 * zurueckschwenkt (Freilook). 0 = bleibt stehen, wo man losgelassen hat.
	 *
	 * Umsehen greift jetzt AUCH im Follow-Modus (Maus/Rechtsstick), nicht erst
	 * nach Umschalten auf Orbit - so wie es die Steuerungshilfe verspricht
	 * ("Maus - Umsehen"). Ohne Eingabe kehrt der Blick sanft nach hinten zurueck.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera", meta = (ClampMin = "0.0"))
	float FollowRecenterSpeed = 3.0f;

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

	/** Hoehenversatz des Kameraankers, z. B. Augenhoehe im Bahnwagen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera")
	FVector CameraOffset = FVector::ZeroVector;

	/** Zoomgrenzen und Schrittweite des Mausrads. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera", meta = (ClampMin = "50.0"))
	float ZoomMinArmLength = 80.0f;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera", meta = (ClampMin = "80.0"))
	float ZoomMaxArmLength = 1200.0f;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera", meta = (ClampMin = "1.0"))
	float ZoomStep = 45.0f;

	/** Position des Cockpit-Sockets relativ zur Kamera-Komponente (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Kamera")
	FVector CockpitOffset = FVector(95.0f, 0.0f, 140.0f);

	/**
	 * Meshes, die in der Cockpit-Ansicht fuer den Fahrer unsichtbar werden.
	 *
	 * Die Fahrzeuge haben keinen modellierten Innenraum - saesse die Kamera in
	 * der geschlossenen Karosserie, blickte man auf die schwarzen Rueckseiten
	 * der Aussenhaut. Statt einen Innenraum zu modellieren, wird die eigene
	 * Huelle fuer den Fahrer ausgeblendet (bOwnerNoSee, nur seine Sicht): freier
	 * Blick nach vorn, das Cockpit liefert die Instrumententafel im HUD.
	 * Umgeschaltet in ApplyCameraMode, nicht je Bild.
	 */
	UPROPERTY(Transient)
	TArray<UPrimitiveComponent*> CockpitHiddenMeshes;

	/** Ein Mesh registrieren, das in der Cockpit-Ansicht verborgen wird. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Kamera")
	void AddCockpitHiddenMesh(UPrimitiveComponent* Mesh);

	/** Kamera-Rig an einen bewegten Unterpunkt, z. B. einen Bahnwagen, haengen. */
	void SetCameraAnchor(USceneComponent* Anchor);

	/** Aktiviert das Rig fuer einen nicht besessenen Rideable-Actor. */
	void ActivateExternalView(APlayerController* Controller, USceneComponent* Anchor, AActor* RestoreTarget);
	void DeactivateExternalView();

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

	UPROPERTY(Transient)
	USceneComponent* CameraAnchor = nullptr;

	TWeakObjectPtr<APlayerController> ExternalController;
	TWeakObjectPtr<AActor> ExternalRestoreTarget;
	bool bExternalViewActive = false;
	FRotator OrbitOffset = FRotator::ZeroRotator;
	bool bCameraToggleHeld = false;
	bool bRigCreated = false;
	/** Einmalige Anwendung des Dev-Schalters -WbCamMode (Sichtprobe). */
	bool bDevModeApplied = false;
};
