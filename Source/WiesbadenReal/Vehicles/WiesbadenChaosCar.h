// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "InputCoreTypes.h"

#include "Vehicles/WiesbadenCarAudioComponent.h"
#include "Vehicles/WiesbadenCarLightsComponent.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"

#include "WiesbadenChaosCar.generated.h"

class UChaosWheeledVehicleMovementComponent;

/**
 * VW Kaefer 1969 auf Unreals echter Fahrzeugphysik.
 *
 * Bisher fuhr das Spielerfahrzeug (AWiesbadenCar) auf einem selbst
 * geschriebenen Modell: FWiesbadenVehiclePhysics rechnet Gierrate, Lenkwinkel
 * und Laengsbeschleunigung aus, der Pawn wird per SetActorLocation versetzt.
 * Das ist berechenbar und stabil, bildet aber nichts ab, was zwischen Reifen
 * und Fahrbahn passiert - kein Schlupf, keine Lastwechsel, keine Federung, und
 * keine Kollision, die das Fahrzeug wirklich aus der Bahn bringt.
 *
 * Chaos Vehicles rechnet je Rad ein Reifenmodell mit Schlupfkurve, verteilt
 * das Motordrehmoment ueber Getriebe und Differential und laesst den Wagen auf
 * vier Federn stehen. Voraussetzung dafuer ist ein SKELETT-Mesh mit einem
 * Knochen je Rad (SK_VWBeetle, erzeugt von Tools/Blender/rig_beetle.py).
 *
 * Diese Klasse steht NEBEN AWiesbadenCar, sie ersetzt es nicht. Grund: HUD,
 * Spielmodus, Waffe und mehrere Tests haengen an AWiesbadenCar; ein Austausch
 * in einem Zug waere ein Umbau mit vielen Beruehrungspunkten und ohne
 * Rueckweg, falls die Physik sich nicht bewaehrt. Umgeschaltet wird mit
 * -WbChaosCar.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenChaosCar : public AWheeledVehiclePawn
{
	GENERATED_BODY()

public:
	AWiesbadenChaosCar();

	virtual void Tick(float DeltaSeconds) override;

	/** Geschwindigkeit in km/h - fuer HUD und Diagnose. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug")
	float GetSpeedKmh() const;

	/** Motordrehzahl in 1/min. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug")
	float GetEngineRpm() const;

	/** Eingelegter Gang; 0 = Leerlauf, negativ = Rueckwaerts. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug")
	int32 GetCurrentGear() const;

	UWiesbadenCarLightsComponent* GetLights() const { return Lights; }

	/** Kameraart weiterschalten (Verfolgung, Umkreis, Cockpit). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug")
	void CycleCameraMode();

	/**
	 * Steht das Chassis auf den Raedern statt auf dem Bauch?
	 *
	 * Datenreine Regressionspruefung fuer die Kaefer-Kollision: Das Chassis-Mesh
	 * reicht (Z in [0,154] cm) unter die Reifenachsen (Radius 34.3 cm), sodass
	 * die Chassis-Kollision zuerst aufsetzt und der Wagen auf dem Bauch haengt.
	 * "Auf den Raedern" heisst: die Unterkante der Chassis-Kollision liegt nicht
	 * unter dem tiefsten Radaufstandspunkt (minus einer kleinen Toleranz).
	 *
	 * Keine Welt, kein Pawn - damit unter Automation belegbar, dass ein Fix am
	 * PhysicsAsset wirkt (VehicleRestTest).
	 */
	static bool RestsOnWheels(double ChassisBottomZcm, double WheelContactZcm, double MarginCm);

protected:
	virtual void BeginPlay() override;

private:
	/**
	 * Tasten und Gamepad abfragen und an die Fahrzeugkomponente geben.
	 *
	 * ABGEFRAGT, nicht ueber Eingabezuordnungen gebunden. Der erste Entwurf
	 * benutzte BindAxis("Throttle") und haette nie ausgeloest: Das Projekt hat
	 * in DefaultInput.ini ueberhaupt keine Achsen- oder Aktionszuordnungen -
	 * AWiesbadenCar fragt die Tasten direkt ab. Ein Fahrzeug, das sich nicht
	 * bewegt, weil eine Zuordnung fehlt, die es nie gab, waere eine lange
	 * Fehlersuche gewesen.
	 *
	 * Belegung wie beim bisherigen Fahrzeug, damit sich die Steuerung nicht
	 * je nach Fahrzeugart aendert.
	 */
	void ReadInput(float DeltaSeconds);

	/** Taste gedrueckt? Ueber den Spielercontroller, ohne Zuordnung. */
	bool IsKeyDown(const FKey& Key) const;

	/** Analogwert einer Gamepad-Achse; 0, wenn kein Gamepad da ist. */
	float GetAnalogAxis(const FKey& Key) const;

	/**
	 * Selbstpruefung der Fahrphysik: -WbCarTest.
	 *
	 * Vollgas aus dem Stand, danach Sekunde fuer Sekunde die Geschwindigkeit
	 * ins Protokoll. Ohne das laesst sich "echte Fahrphysik" nicht belegen -
	 * ein Fahrzeug, das steht, sieht auf dem Bild genauso aus wie eines, das
	 * gerade langsam anfaehrt.
	 *
	 * Pruefbare Zielwerte des Kaefer 1302 von 1969:
	 *   0 bis 100 km/h    rund 23 Sekunden
	 *   Hoechstgeschwindigkeit  rund 130 km/h
	 *
	 * Weichen die Messwerte stark ab, stimmt etwas an Drehmoment, Getriebe,
	 * Masse oder Reifengriff nicht - und zwar nachweisbar, nicht gefuehlt.
	 */
	void TickSelfTest(float DeltaSeconds);

	/** Laufzeit der Selbstpruefung in Sekunden. */
	float SelfTestElapsed = 0.0f;

	/** Naechste volle Sekunde, zu der protokolliert wird. */
	int32 SelfTestNextReport = 0;

	/** Die Fallprobe wurde durchgefuehrt (einmalig beim Start der Fahrprobe). */
	bool bDropTestDone = false;

	/** Hoehe unmittelbar nach dem Anheben - Bezugspunkt der Fallstrecke. */
	double DropTestStartZ = 0.0;

	/** Hoechste bisher erreichte Geschwindigkeit der Selbstpruefung. */
	float SelfTestTopKmh = 0.0f;

	/** Zeit bis 50 und bis 100 km/h; negativ, solange nicht erreicht. */
	float SelfTestTo50 = -1.0f;
	float SelfTestTo100 = -1.0f;

	/** Vollgas statt Tastenabfrage, solange die Selbstpruefung laeuft. */
	bool bSelfTestActive = false;

	UChaosWheeledVehicleMovementComponent* GetChaosMovement() const;

	/** Kamera wie beim bisherigen Fahrzeug (Verfolgung, Umkreis, Cockpit). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UWiesbadenVehicleCameraComponent* VehicleCamera = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UWiesbadenCarLightsComponent* Lights = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug")
	UWiesbadenCarAudioComponent* EngineAudio = nullptr;
};
