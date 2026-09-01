// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"

#include "Vehicles/WiesbadenHelicopterAudioComponent.h"
#include "Vehicles/WiesbadenRotorPhysics.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"

#include "WiesbadenHelicopter.generated.h"

class APlayerController;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Geglaettete Steuerwerte fuer die externe Steuerung des Hubschraubers.
 *
 * Ein Treiber (KI, Zwischensequenz, Replay, Test-Harness) reicht hierueber die
 * fertigen Steuerwerte (-1..1) ein; der Hubschrauber wendet sie ueber die echte
 * Rotorphysik an. So bleibt die Flugsimulation frei von Treiber-/Test-Code.
 */
struct FWiesbadenHeliControl
{
	float Collective = 0.0f;   // -1..1 (steigen/sinken)
	float Pitch = 0.0f;        // -1..1 (Nase runter = +)
	float Roll = 0.0f;         // -1..1 (rechts = +)
	float Yaw = 0.0f;          // -1..1 (rechts = +)
	bool bEngine = true;
};

/**
 * Fliegbarer Helikopter-Pawn mit Platzhalter-Geometrie (Engine-Basis-Shapes).
 *
 * Die Rotor-Physik (Lift, Collective, Zyklik, Heckrotor, Autorotation) kommt
 * aus dem reinen Modul FWiesbadenRotorPhysics; der Heli integriert nur noch
 * Kraft -> Beschleunigung und Drehmoment -> Winkelbeschleunigung. Die Kamera
 * (Follow/Orbit/Cockpit) kommt aus UWiesbadenVehicleCameraComponent.
 *
 * Steuerung (zero-config, Tasten werden gepollt, keine Input-Assets noetig):
 *  - W/S = Pitch (Nase runter/hoch), A/D = Roll, Q/E = Yaw (Heckrotor)
 *  - Space/Shift = Collective hoch (steigen), Ctrl = Collective runter (sinken)
 *  - G = Triebwerk an/aus (Autorotation testbar)
 *
 * Kamera: Umsehen per Maus/Gamepad-Rechtsstick (Freilook im Follow-Modus),
 * Umschalten Follow -> Orbit -> Cockpit per C. Der Horizont bleibt ruhig
 * (bLevelHorizon), der Rumpf neigt sich im Bild statt das Bild mitzukippen.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenHelicopter : public APawn
{
	GENERATED_BODY()

public:
	AWiesbadenHelicopter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	/** Umschalten der Kamera (Forward an die Fahrzeug-Kamera-Komponente). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli")
	void CycleCameraMode();

	/** Aktueller Kameramodus (Forward an die Fahrzeug-Kamera-Komponente). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli")
	EWiesbadenVehicleCameraMode GetCameraMode() const;

	/** Aktuelle Hauptrotor-Drehzahl (U/min). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli")
	float GetMainRotorRpm() const;

	// -- Cockpit-Instrumente ----------------------------------------------
	// Telemetrie fuer die Cockpit-Anzeige (WiesbadenVehicleHUD). Bewusst als
	// einfache Abfragen aus dem bereits gefuehrten Flugzustand.

	/** Waagerechte Fluggeschwindigkeit in km/h. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	float GetAirspeedKmh() const;

	/** Steig-/Sinkrate in m/s (positiv = steigen). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	float GetVerticalSpeedMs() const;

	/** Hoehe ueber Grund in Metern (Strahl nach unten; Fallback: Welthoehe). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	float GetAltitudeMeters() const;

	/** Steuerkurs 0..360 Grad (aus dem Gier-Winkel des Rumpfes). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	float GetHeadingDegrees() const;

	/** Kollektiv-Blattverstellung, 0..1 (Hebelstellung). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	float GetCollective() const;

	/** Triebwerk laeuft? */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	bool IsEngineRunning() const { return bEngineRunning; }

	/** Momentane Gierrate in Grad/s (Telemetrie fuer KI/Test/Anzeige). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	float GetYawRateDegPerSec() const;

	// -- Externe Steuerung ------------------------------------------------
	// Sauberer Eingang, ueber den ein anderer Treiber (KI, Zwischensequenz,
	// Replay, Test-Harness) den Hubschrauber steuert - ueber die ECHTE
	// Rotorphysik, nicht per direkter Transformation. Solange aktiv,
	// ueberschreibt er Tastatur/Gamepad. So bleibt die Flugsimulation frei von
	// Test-/Skript-Code (die Choreografie liegt in UWiesbadenVehicleTestHarness).

	/** Geglaettete Steuerwerte setzen (aktiviert die externe Steuerung). */
	void SetExternalControl(const FWiesbadenHeliControl& Control)
	{
		ExternalControl = Control;
		bExternalControlActive = true;
	}

	/** Externe Steuerung abschalten - der Rumpf hoert wieder auf Tastatur/Gamepad. */
	void ClearExternalControl() { bExternalControlActive = false; }

	/** Triebwerk laeuft; sonst arbeitet der Rotor nur ueber Autorotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Heli|Physik")
	bool bEngineRunning = false;

	// -- Fahrzeug-Integration (Kraft/Drehmoment -> Bewegung) --------------
	/** Traegheitsmoment des Rumpfes (Roll, Pitch, Yaw) in kg*m^2. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.01"))
	//
	// Traegheitsmomente eines 10-Tonners statt eines Ultraleichten. Roll um die
	// Laengsachse ist am kleinsten, Nicken um die Querachse am groessten - der
	// Rumpf ist 16 m lang und nur 2 m breit.
	FVector MomentOfInertiaKgM2 = FVector(12000.0f, 45000.0f, 40000.0f);

	/** Glattung der Steuereingaenge (hoeher = direkter). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.01"))
	float ControlResponse = 6.0f;

	// -- Steuergefuehl ----------------------------------------------------
	//
	// Die Steuerung las bisher nur Tasten: jede Eingabe war +1, -1 oder 0, und
	// eine einzige exponentielle Glaettung lag darueber. Fliegen per An/Aus,
	// mit gleicher Reaktion fuer Kollektiv, Nick, Roll und Gier - obwohl ein
	// Hubschrauber auf diesen Achsen voellig verschieden traege ist. Dazu kam,
	// dass die Lage ohne Eingabe stehen blieb: einmal schraeg, immer schraeg.

	/**
	 * Totzone der Analogsticks (Anteil des Vollausschlags).
	 *
	 * Ohne Totzone driftet der Hubschrauber, weil kein Stick exakt mittig
	 * ruht. 0,15 ist der uebliche Wert fuer Xbox-Sticks.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float StickDeadzone = 0.15f;

	/**
	 * Kruemmung der Stick-Kennlinie (0 = linear, 1 = stark).
	 *
	 * Mit Expo sind kleine Ausschlaege fein aufgeloest und der volle Ausschlag
	 * bleibt erreichbar - genau das, was Schweben ueberhaupt erst moeglich
	 * macht. Linear ist ein Hubschrauber kaum auf der Stelle zu halten.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StickExpo = 0.6f;

	/** Aufbau des zyklischen Ausschlags (Anteil je Sekunde). */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float CyclicRiseRate = 2.2f;

	/** Ruecklauf des zyklischen Ausschlags zur Mitte. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float CyclicReturnRate = 3.5f;

	/** Aufbau des Kollektivs - traeger, es haengt an der Blattverstellung. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float CollectiveRiseRate = 1.4f;

	/** Ruecklauf des Kollektivs. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float CollectiveReturnRate = 2.0f;

	/** Aufbau des Gierpedals - die schnellste Achse. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float YawRiseRate = 3.0f;

	/** Ruecklauf des Gierpedals. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float YawReturnRate = 5.0f;

	/**
	 * Staerke der Selbststabilisierung (Grad Ausschlag je Grad Schraeglage).
	 *
	 * Ohne sie bleibt die Lage stehen, sobald man loslaesst - der Hubschrauber
	 * kippt weiter in die zuletzt kommandierte Richtung, bis man gegensteuert.
	 * Das ist der Hauptgrund, aus dem sich eine Hubschraubersteuerung
	 * unbeherrschbar anfuehlt.
	 *
	 * Wirkt NUR, solange auf der Achse nichts kommandiert wird - wer bewusst
	 * schraeg fliegt, wird nicht gegen sich selbst arbeiten muessen.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0"))
	float AutoLevelStrength = 0.045f;

	/** Groesster Ausschlag, den die Selbststabilisierung allein erzeugt. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AutoLevelMaxAuthority = 0.55f;

	/**
	 * Schwebehilfe: Daempfung der Vertikalgeschwindigkeit bei neutralem
	 * Kollektiv (1/s). Mit losgelassenem Hebel strebt der Hubschrauber
	 * gegen Schweben, statt jede Stoerung als Dauersinken fortzuschreiben -
	 * so arbeitet auch das Stabilisierungssystem des echten Ka-52.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0"))
	float HoverAssistStrength = 1.1f;

	/**
	 * Driftdaempfung: Abbau der waagerechten Geschwindigkeit bei mittigem
	 * zyklischen Stick (1/s).
	 *
	 * Das Gegenstueck zur Schwebehilfe. Ohne sie haelt der Hubschrauber zwar
	 * die Hoehe, rutscht aber nach jedem Kippen weiter, bis man exakt
	 * gegensteuert - der Grund fuer "kann kaum navigieren".
	 *
	 * Bewusst schwaecher als die Schwebehilfe: ein Hubschrauber soll sich
	 * vorwaerts noch traege anfuehlen, nur nicht unkontrollierbar.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0"))
	float DriftAssistStrength = 0.8f;

	/**
	 * Ratendaempfung um Quer- und Laengsachse ohne Knueppelausschlag (1/s).
	 * Loslassen laesst die Drehbewegung abklingen, statt sie als
	 * Restdrehung weiterlaufen zu lassen.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0"))
	float RateAssistStrength = 2.0f;

	/** Luftwiderstand des Rumpfes (pro Sekunde). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.0"))
	float LinearDrag = 0.35f;

	/** Hoechstgeschwindigkeit (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.0"))
	float MaxSpeedCmPerS = 8300.0f;   // 300 km/h, Ka-52-Hoechstgeschwindigkeit

	/** Schwerkraft (UE-Default 980 cm/s^2). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.0"))
	float GravityCmPerS2 = 980.0f;

	/** Minimaler Abstand zum Boden (weiche Boden-Kollision per Raycast). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.0"))
	float MinGroundClearanceCm = 40.0f;

	/** Rotor-Physik-Modul (Lift, Collective, Zyklik, Heckrotor, Autorotation). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Rotor")
	FWiesbadenRotorPhysics RotorPhysics;

	/**
	 * Totzone anwenden und die Expo-Kennlinie auflegen.
	 *
	 * Datenrein und statisch, damit die Kennlinie ohne Welt pruefbar ist.
	 * Nach der Totzone wird auf den vollen Bereich gestreckt - sonst waere der
	 * Vollausschlag nicht mehr erreichbar.
	 */
	static float ApplyStickShaping(float RawAxis, float Deadzone, float Expo);

	/**
	 * Achse mit getrennten Raten fuer Aufbau und Ruecklauf nachfuehren.
	 *
	 * Ruecklauf heisst: Bewegung zur Mitte. Die Pruefung muss ausschliessen,
	 * dass die Achse bereits mittig steht - FMath::Sign(0) ist 0 und weicht
	 * damit von JEDEM Ziel ab. Genau dieser Fehler hat bei der Lenkung des
	 * Fahrzeugs dazu gefuehrt, dass aus der Mitte heraus mit der schnelleren
	 * Ruecklaufrate eingelenkt wurde.
	 */
	static float AdvanceControlAxis(
		float Current, float Target, float RiseRate, float ReturnRate, float DeltaSeconds);

	/**
	 * Zusaetzlicher Ausschlag, der den Hubschrauber aufrichtet.
	 *
	 * Wirkt nur, solange auf der Achse nichts kommandiert wird: Der Rueckgabe-
	 * wert wird mit (1 - |Eingabe|) gewichtet, damit bewusstes Schraegfliegen
	 * nicht gegen die Stabilisierung ankaempfen muss.
	 */
	static float ComputeAutoLevel(
		float AttitudeDegrees, float CommandedInput, float Strength, float MaxAuthority);


protected:
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	USceneComponent* SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	USphereComponent* CollisionSphere = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* FuselageMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* TailBoomMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* TailFinMesh = nullptr;

	/** Rotor-Nabe des Hauptrotors (Mast ueber dem Schwerpunkt). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	USceneComponent* MainRotorHub = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* MainRotorBlade = nullptr;

	/** Unterer Hauptrotor des Koaxial-Paars (gegenlaeufig, Ka-52-Stil). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	USceneComponent* LowerRotorHub = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* LowerRotorBlade = nullptr;

	/** Rotor-Nabe des Heckrotors (Ende des Heckauslegers). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	USceneComponent* TailRotorHub = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* TailRotorBlade = nullptr;

	/** Generische Fahrzeug-Kamera (Follow/Orbit/Cockpit). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UWiesbadenVehicleCameraComponent* VehicleCamera = nullptr;

	/** Flugsound (Rotor-/Motor-Assets oder prozeduraler Fallback). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UWiesbadenHelicopterAudioComponent* HelicopterAudio = nullptr;

private:
	void ReadInput(float DeltaSeconds);
	void ApplyFlightPhysics(float DeltaSeconds);
	void ApplyGroundConstraint(float DeltaSeconds);

	/**
	 * Haelt den Helikopter am Boden, solange niemand ihn fliegt.
	 *
	 * Ohne diesen Zustand lief die volle Flugsimulation auch fuer den
	 * abgestellten Helikopter weiter - bei laufendem Triebwerk und 50 %
	 * Kollektiv stieg er von selbst davon. Gemessen stand er nach acht
	 * Sekunden 126 m ueber Grund und war vom Startplatz aus nicht mehr zu
	 * sehen.
	 */
	void ParkOnGround();
	void UpdateRotors(float DeltaSeconds);
	void UpdateAudio(float DeltaSeconds);

	bool IsKeyDown(const FKey& Key);

	/** Analogwert einer Achse (Gamepad-Stick oder Trigger), 0 ohne Controller. */
	float GetAnalogAxis(const FKey& Key);
	APlayerController* GetHeliController();

	// Geglaettete Steuereingaenge (-1..1).
	float CollectiveInput = 0.0f;
	float CyclicPitchInput = 0.0f;
	float CyclicRollInput = 0.0f;
	float YawInput = 0.0f;

	// Lokale Winkelgeschwindigkeit (Roll, Pitch, Yaw) in rad/s.
	FVector AngularVelocity = FVector::ZeroVector;

	// Weltgeschwindigkeit in cm/s.
	FVector Velocity = FVector::ZeroVector;

	bool bEngineToggleHeld = false;
	bool bGrounded = false;

	/** Externe Steuerung (KI/Zwischensequenz/Test), siehe SetExternalControl. */
	FWiesbadenHeliControl ExternalControl;
	bool bExternalControlActive = false;
};
