// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"

#include "Vehicles/WiesbadenCarAudioComponent.h"
#include "Vehicles/WiesbadenCarLightsComponent.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
#include "Vehicles/WiesbadenVehicleControl.h"
#include "Vehicles/WiesbadenVehiclePhysics.h"

#include "WiesbadenCar.generated.h"

class APlayerController;
class USceneComponent;
class UBoxComponent;
class UStaticMeshComponent;

// FWiesbadenCarControl liegt jetzt im neutralen Header WiesbadenVehicleControl.h
// (gemeinsame Steuernaht beider Fahrzeuge), von hier aus mit-inkludiert.

/** Welche Kaefer-Karosserie gezeichnet wird. */
enum class EBeetleBodyMesh : uint8
{
	SeparateWheelBody, // radlose Karosserie (SM_VWBeetle1969_Body) + 4 Einzelraeder
	HerbieFull,        // Herbie-Voll-Mesh mit eingebackenen Raedern (Notfall)
	Cube,              // Engine-Ersatzquader
};

/** Ergebnis der Kaefer-Mesh-Auswahl: Karosserie + ob die 4 Einzelraeder sichtbar sind. */
struct FBeetleAssembly
{
	EBeetleBodyMesh Body = EBeetleBodyMesh::Cube;
	bool bSeparateWheels = false;
};

/**
 * Fahrbarer PKW-Pawn mit Platzhalter-Geometrie (Engine-Basis-Shapes).
 *
 * Die Laengs-/Querdynamik (Motor, Automatik-Getriebe, Radkraefte, Lenkung)
 * kommt aus dem reinen Modul FWiesbadenVehiclePhysics; der Pawn integriert nur
 * Geschwindigkeit -> Position und Gierrate -> Ausrichtung plus Bodenkontakt.
 * Die Kamera (Follow/Orbit/Cockpit) kommt aus UWiesbadenVehicleCameraComponent.
 *
 * Steuerung (zero-config, Tasten werden gepollt, keine Input-Assets noetig):
 *  - W/S = Gas/Bremse, A/D = Lenken, Space = Handbremse
 *  - R = Rueckwaertsgang (Flanke), C = Kamera umschalten, Pfeiltasten = Orbit
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenCar : public APawn, public IWiesbadenVehicleControl
{
	GENERATED_BODY()

public:
	AWiesbadenCar();

	/**
	 * Waehlt Kaefer-Karosserie + Rad-Darstellung aus den verfuegbaren Meshes.
	 * Bevorzugt die radlose Karosserie + 4 Einzelraeder, weil das Herbie-Voll-Mesh
	 * ein Hinterrad vermissen laesst; Herbie nur als Notfall, sonst der Ersatzquader.
	 * Rein/statisch, ohne Welt testbar (Test Vehicles.BeetleAssembly).
	 */
	static FBeetleAssembly ChooseBeetleAssembly(
		bool bBodyMeshAvailable, bool bWheelMeshAvailable, bool bHerbieMeshAvailable);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	/** Umschalten der Kamera (Forward an die Fahrzeug-Kamera-Komponente). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug")
	void CycleCameraMode();

	/** Aktueller Kameramodus (Forward an die Fahrzeug-Kamera-Komponente). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Fahrzeug")
	virtual EWiesbadenVehicleCameraMode GetCameraMode() const override;

	/** Absolutgeschwindigkeit in km/h (Tacho). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Fahrzeug")
	virtual float GetSpeedKmh() const override;

	/** Aktueller Gang (1..N; -1 = Rueckwaerts). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Fahrzeug")
	virtual int32 GetGear() const override;

	/** Aktuelle Motordrehzahl (U/min). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Fahrzeug")
	virtual float GetEngineRpm() const override;

	/** Leerlauf-/Hoechstdrehzahl (U/min) fuer die HUD-Drehzahlband-Skala. */
	virtual float GetEngineIdleRpm() const override { return VehiclePhysics.Powertrain.IdleRpm; }
	virtual float GetEngineMaxRpm() const override { return VehiclePhysics.Powertrain.MaxRpm; }

	/**
	 * Externe Steuerung setzen (KI/Zwischensequenz/Test): umgeht die Tastenabfrage
	 * und speist Gas/Bremse/Lenkung ueber die normale Fahrphysik. ReadInput wendet
	 * sie nur an - keine Test-/Treiberlogik in der Fahrzeugklasse.
	 */
	virtual void SetExternalControl(const FWiesbadenCarControl& Control) override
	{
		ExternalControl = Control;
		bExternalControlActive = true;
	}

	/** Externe Steuerung abschalten - die Tastatur/das Gamepad uebernimmt wieder. */
	virtual void ClearExternalControl() override { bExternalControlActive = false; }

	/** True, solange die externe Steuerung aktiv ist (Interface-Naht). */
	virtual bool IsExternalControlActive() const override { return bExternalControlActive; }

	/** Lichtanlage des Fahrzeugs - fuer die HUD-Kontrollleuchten. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Fahrzeug|Licht")
	virtual UWiesbadenCarLightsComponent* GetLights() const override { return Lights; }

	/** Fahrzeug-Physik-Modul (Motor, Getriebe, Radkraefte, Lenkung). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Physik")
	FWiesbadenVehiclePhysics VehiclePhysics;

	/** Minimaler Abstand des Fahrzeugs zum Boden (weiche Federung per Raycast). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Physik", meta = (ClampMin = "0.0"))
	float GroundClearanceCm = 35.0f;

	/**
	 * Stufen bis zu dieser Hoehe (cm) werden UEBERFAHREN statt als Wand behandelt.
	 *
	 * Die vorgekochte Fahrbahn-Kollision traegt 12-cm-Bordsteine als senkrechte
	 * Kanten; der Bewegungs-Sweep blockierte daran und der Wagen blieb am
	 * Bordstein haengen. Ist die Oberkante einer Blockade nur so hoch, wird der
	 * volle Zug zugelassen und die Bodenverfolgung hebt den Wagen sanft hinauf.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Physik", meta = (ClampMin = "0.0"))
	float StepUpMaxCm = 18.0f;

	/** Fallbeschleunigung, wenn kein Boden gefunden wird (cm/s^2). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Physik", meta = (ClampMin = "0.0"))
	float FallGravityCmS2 = 981.0f;

	/**
	 * Naechste Fallgeschwindigkeit (cm/s), wenn unter dem Wagen kein Boden liegt.
	 *
	 * Datenrein und statisch, damit das Fallverhalten ueber einer ungeladenen
	 * Zelle ohne Welt pruefbar ist (Durchfall-Regression).
	 */
	static float AdvanceFallSpeedCmS(float CurrentCmS, float GravityCmS2, float Dt);

	/** Glattung der Steuereingaenge (hoeher = direkter). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Physik", meta = (ClampMin = "0.1"))
	float ControlResponse = 6.0f;

	/** Totzone der Gamepad-Sticks (Anteil des Vollausschlags). */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Steuerung", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float GamepadDeadzone = 0.15f;

	/**
	 * Kruemmung der Lenk-Kennlinie am Stick (0 = linear, 1 = stark).
	 *
	 * Am Fahrzeug schwaecher als am Hubschrauber: Lenken soll direkt bleiben,
	 * aber kleine Korrekturen bei hoher Geschwindigkeit brauchen Feingefuehl.
	 */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Steuerung", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GamepadSteerExpo = 0.35f;

	/**
	 * Wie schnell sich das Fahrzeug an die Neigung des Untergrunds anlegt
	 * (hoeher = straffer). Ohne diese Ausrichtung blieb der Wagen an jeder
	 * Steigung exakt waagerecht stehen, waehrend der Boden unter ihm kippte.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Physik", meta = (ClampMin = "0.1"))
	float GroundAlignResponse = 7.0f;

	/** Wie schnell die Federung der Bodenhoehe folgt (hoeher = haerter). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Physik", meta = (ClampMin = "0.1"))
	float SuspensionResponse = 16.0f;

	/** Flughoehe ueber dem Gebaeude beim Ueberflug, in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Ueberflug", meta = (ClampMin = "100.0"))
	float FlyOverClearanceCm = 1000.0f;

	/** Steigrate beim Aufstieg in cm/s. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Ueberflug", meta = (ClampMin = "100.0"))
	float FlyOverClimbRateCmPerS = 2600.0f;

	/** Sinkrate nach dem Gebaeude in cm/s. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Ueberflug", meta = (ClampMin = "100.0"))
	float FlyOverDescendRateCmPerS = 1400.0f;

	/** Mindesthoehe eines Hindernisses ueber dem Wagen, damit geflogen wird (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Ueberflug", meta = (ClampMin = "0.0"))
	float FlyOverMinObstacleCm = 250.0f;

	/**
	 * Gebaeude-UEBERFLUG erlauben. STANDARD AUS.
	 *
	 * Der Ueberflug warf den Wagen bei normaler Fahrt an jeder Hauswand (und bei
	 * gestreiften Kanten) hoch ueber die Daecher, wo er trudelnd wieder herabfiel
	 * - im Spiel als "Kaefer fliegt ueber die Haeuser" sichtbar. Bei normaler
	 * Fahrt bleibt der Wagen jetzt am Boden und schiebt an Waenden entlang; der
	 * Ueberflug ist nur noch ueber diesen expliziten Schalter aktivierbar.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Ueberflug")
	bool bEnableBuildingFlyOver = false;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	USceneComponent* SceneRoot = nullptr;

	/**
	 * Kollisionskoerper in echten Fahrzeugmassen.
	 *
	 * Hier stand zuvor eine Kugel mit 220 cm Radius - 4,4 m Durchmesser fuer
	 * einen Kaefer, der 4,08 m lang und 1,55 m breit ist. Das Fahrzeug stiess
	 * damit rund anderthalb Meter vor jeder Wand an, passte durch keine Luecke,
	 * die es optisch haette passieren muessen, und rollte an Kanten auf der
	 * Kugelrundung auf.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UBoxComponent* CollisionBox = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UStaticMeshComponent* BodyMesh = nullptr;

	/** Vorderraeder (lenken um die Z-Achse, drehen um die Y-Achse = Pitch). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UStaticMeshComponent* FrontLeftWheel = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UStaticMeshComponent* FrontRightWheel = nullptr;

	/** Hinterraeder (drehen nur um die X-Achse). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UStaticMeshComponent* RearLeftWheel = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UStaticMeshComponent* RearRightWheel = nullptr;

	/** Generische Fahrzeug-Kamera (Follow/Orbit/Cockpit). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UWiesbadenVehicleCameraComponent* VehicleCamera = nullptr;

	/** Lichtanlage: Fahrlicht, Bremslicht, Rueckfahrlicht, Blinker. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UWiesbadenCarLightsComponent* Lights = nullptr;

	/** Motorklang, synthetisiert aus Drehzahl und Last. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UWiesbadenCarAudioComponent* EngineAudio = nullptr;

private:
	void ReadInput(float DeltaSeconds);
	void ApplyVehiclePhysics(float DeltaSeconds);
	void UpdateWheels(float DeltaSeconds);

	/** Tastenflanken fuer Fahrlicht, Blinker und Warnblinkanlage auswerten. */
	void ReadLightInput();

	/** Licht- und Klangzustand aus dem Ergebnis der Fahrphysik nachfuehren. */
	void UpdateLightsAndAudio(const FWiesbadenVehiclePhysicsOutput& Output);

	/**
	 * Prueft die Kollisionsbox entlang einer Bewegung.
	 *
	 * Nicht ueber AddActorWorldOffset(bSweep=true): das prueft die
	 * WURZELKOMPONENTE, und die ist hier ein formloser USceneComponent. Der
	 * Sweep traf deshalb nie etwas, und das Fahrzeug fuhr durch Haeuser.
	 *
	 * @return True, wenn etwas im Weg ist (OutHit gefuellt).
	 */
	bool SweepVehicle(const FVector& Delta, FHitResult& OutHit) const;

	bool IsKeyDown(const FKey& Key);

	/** Analogwert einer Achse (Gamepad-Stick oder Trigger), 0 ohne Controller. */
	float GetAnalogAxis(const FKey& Key);
	APlayerController* GetCarController();

	// Geglaettete Steuereingaenge.
	float ThrottleInput = 0.0f;
	float BrakeInput = 0.0f;
	float SteeringInput = 0.0f;

	bool bReverseRequested = false;
	bool bReverseToggleHeld = false;

	/** Externe Steuerung (KI/Test), siehe SetExternalControl. */
	FWiesbadenCarControl ExternalControl;
	bool bExternalControlActive = false;

	/** Akkumulierte Rad-Drehung um die Querachse (Grad, auf 360 normalisiert). */
	float WheelRotationPitch = 0.0f;

	/** Aktuelle Fallgeschwindigkeit (cm/s), wenn kein Boden unter dem Wagen liegt. */
	float FallSpeedCmS = 0.0f;

	/** True, solange der Wagen ueber ein Gebaeude hinwegfliegt. */
	bool bFlyingOverBuilding = false;

	/** Strassenhoehe beim Abheben - unterscheidet Dach von Strasse. */
	float FlyOverStreetZ = 0.0f;

	/** Zielhoehe des laufenden Ueberflugs (Weltkoordinate). */
	float FlyOverTargetZ = 0.0f;

	/** In DIESEM Bild eine Hauswand voraus erkannt (Anflugphase). */
	bool bWallAheadThisFrame = false;

	// Halte-Flanken der Licht- und Blinkertasten. Gleiche Technik wie beim
	// Rueckwaertsgang: die Eingabe wird gepollt statt ueber Events gebunden,
	// damit das Fahrzeug ohne Input-Assets funktioniert.
	bool bHeadlightKeyHeld = false;
	bool bIndicatorLeftKeyHeld = false;
	bool bIndicatorRightKeyHeld = false;
	bool bHazardKeyHeld = false;

	// Dieselben Flanken fuer das Gamepad. Eigene Variablen, nicht die
	// obigen mitbenutzt: sonst loeschte ein Tastendruck die Flanke des
	// Gamepads und umgekehrt - beide Geraete sollen unabhaengig gehen.
	bool bHeadlightPadHeld = false;
	bool bIndicatorLeftPadHeld = false;
	bool bIndicatorRightPadHeld = false;
	bool bHazardPadHeld = false;
};
