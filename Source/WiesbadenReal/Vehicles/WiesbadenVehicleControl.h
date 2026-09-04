// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "WiesbadenVehicleControl.generated.h"

class UWiesbadenCarLightsComponent;
enum class EWiesbadenVehicleCameraMode : uint8;

/**
 * Fertige Steuerwerte fuer die externe FAHRZEUG-Steuerung (KI/Test/Replay).
 *
 * Ein Treiber schiebt fertige Eingaben ein, das Fahrzeug wendet sie ueber
 * dieselbe Fahrphysik an wie eine Tastatureingabe. So bleibt die Fahrzeugklasse
 * frei von Test-/Treiber-Code, und die Fahrphysik laesst sich ohne Tastatur
 * nachweisen (Rauchtest) oder von einer KI fahren.
 */
struct FWiesbadenCarControl
{
	float Throttle = 0.0f;   // 0..1 (Gas)
	float Brake = 0.0f;      // 0..1 (Bremse)
	float Steering = 0.0f;   // -1..1 (rechts = +)
	bool bHandbrake = false;
	bool bReverse = false;
};

/**
 * Fertige Steuerwerte fuer die externe HELIKOPTER-Steuerung (KI/Test/Replay).
 *
 * Dasselbe Prinzip wie FWiesbadenCarControl, nur die Achsen eines Hubschraubers.
 * Bewusst hier im NEUTRALEN Steuernaht-Header (nicht in WiesbadenHelicopter.h),
 * damit Autopilot, Harness und die Interface-Familie ihn nutzen koennen, ohne
 * die schwere Heli-Klasse (FWiesbadenRotorPhysics) einzuziehen.
 */
struct FWiesbadenHeliControl
{
	float Collective = 0.0f;   // -1..1 (steigen/sinken)
	float Pitch = 0.0f;        // -1..1 (Nase runter = +)
	float Roll = 0.0f;         // -1..1 (rechts = +)
	float Yaw = 0.0f;          // -1..1 (rechts = +)
	bool bEngine = true;
};

// ---------------------------------------------------------------------------
// Interface-FAMILIE der externen Steuernaht.
//
// Wurzel IWiesbadenExternalControl: der gemeinsame Lebenszyklus (aktivieren via
// typspezifischem SetExternalControl, abschalten, abfragen) PLUS der eine
// gemeinsame Readout (Tempo). Auto UND Helikopter implementieren sie - deshalb
// haben Autopilot und Harness EINEN Zugriffspfad (Cast<IWiesbadenExternalControl>)
// fuer "steht das Ding unter externer Steuerung / gib sie frei / wie schnell".
//
// Zwei typisierte Zweige, weil die Steuer-Nutzlast (Car vs Heli) verschieden ist
// und kein gemeinsamer Pawn-Basistyp moeglich ist (ChaosCar erbt zwingend von
// AWheeledVehiclePawn). Die Zweige ERBEN die Wurzel (U- und I-Klasse), sodass ein
// Cast auf die Wurzel ueber IsChildOf auch bei den Ableitungen greift.
// ---------------------------------------------------------------------------

UINTERFACE(MinimalAPI)
class UWiesbadenExternalControl : public UInterface
{
	GENERATED_BODY()
};

/** Gemeinsame Wurzel: externer-Steuerung-Lebenszyklus + gemeinsamer Readout. */
class IWiesbadenExternalControl
{
	GENERATED_BODY()

public:
	/** Externe Steuerung abschalten - Tastatur/Gamepad uebernimmt wieder. */
	virtual void ClearExternalControl() = 0;

	/** True, solange eine externe Steuerung aktiv ist. */
	virtual bool IsExternalControlActive() const = 0;

	/** Fahrtgeschwindigkeit in km/h (Auto: Tacho; Heli: Fahrt/Airspeed). */
	virtual float GetSpeedKmh() const = 0;
};

UINTERFACE(MinimalAPI)
class UWiesbadenVehicleControl : public UWiesbadenExternalControl
{
	GENERATED_BODY()
};

/**
 * Fahrzeug-Zweig (AWiesbadenCar kinematisch, AWiesbadenChaosCar Chaos-Physik).
 * Erbt den gemeinsamen Lebenszyklus, ergaenzt die Auto-Nutzlast + den Gang.
 */
class IWiesbadenVehicleControl : public IWiesbadenExternalControl
{
	GENERATED_BODY()

public:
	/** Externe Fahr-Steuerung setzen (aktiviert die externe Steuerung). */
	virtual void SetExternalControl(const FWiesbadenCarControl& Control) = 0;

	/** Aktueller Gang (1..N; 0 = Leerlauf, negativ = Rueckwaerts). */
	virtual int32 GetGear() const = 0;

	// -- Volle Instrumententafel (HUD) ------------------------------------
	// Damit das reiche Fahrzeug-HUD (Drehzahlband, Kontrollleuchten, Cockpit)
	// fuer JEDES Fahrzeug der Familie funktioniert - nicht nur den Kaefer.

	/** Aktuelle Motordrehzahl (U/min). */
	virtual float GetEngineRpm() const = 0;

	/** Leerlauf- bzw. Hoechstdrehzahl (U/min) - Skala des Drehzahlbands. */
	virtual float GetEngineIdleRpm() const = 0;
	virtual float GetEngineMaxRpm() const = 0;

	/** Lichtanlage fuer die HUD-Kontrollleuchten. */
	virtual UWiesbadenCarLightsComponent* GetLights() const = 0;

	/** Kameramodus (Follow/Orbit/Cockpit) - fuer die Cockpit-Instrumententafel. */
	virtual EWiesbadenVehicleCameraMode GetCameraMode() const = 0;
};

UINTERFACE(MinimalAPI)
class UWiesbadenHeliControl : public UWiesbadenExternalControl
{
	GENERATED_BODY()
};

/**
 * Helikopter-Zweig (AWiesbadenHelicopter). Erbt den gemeinsamen Lebenszyklus,
 * ergaenzt die Heli-Nutzlast + die Flug-Telemetrie, die Autopilot und Harness
 * brauchen - damit beide den Heli AUSSCHLIESSLICH ueber die Familie ansprechen
 * (kein Cast mehr auf die konkrete Klasse).
 */
class IWiesbadenHeliControl : public IWiesbadenExternalControl
{
	GENERATED_BODY()

public:
	/** Externe Flug-Steuerung setzen (aktiviert die externe Steuerung). */
	virtual void SetExternalControl(const FWiesbadenHeliControl& Control) = 0;

	/** Weltgeschwindigkeit in m/s aus der internen Integration (Regelung/KI). */
	virtual FVector GetVelocityMetersPerSecond() const = 0;

	/** Hoehe ueber Grund in Metern. */
	virtual float GetAltitudeMeters() const = 0;

	/** Steig-/Sinkrate in m/s (positiv = steigen). */
	virtual float GetVerticalSpeedMs() const = 0;

	/** Steuerkurs 0..360 Grad. */
	virtual float GetHeadingDegrees() const = 0;

	/** Momentane Gierrate in Grad/s. */
	virtual float GetYawRateDegPerSec() const = 0;
};
