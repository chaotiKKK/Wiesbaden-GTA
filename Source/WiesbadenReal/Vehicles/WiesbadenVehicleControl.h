// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "WiesbadenVehicleControl.generated.h"

/**
 * Fertige Steuerwerte fuer die externe Fahrzeugsteuerung (KI/Test/Replay).
 *
 * Analog zu FWiesbadenHeliControl: ein Treiber schiebt fertige Eingaben ein,
 * das Fahrzeug wendet sie ueber dieselbe Fahrphysik an wie eine Tastatureingabe.
 * So bleibt die Fahrzeugklasse frei von Test-/Treiber-Code, und die Fahrphysik
 * laesst sich ohne Tastatur nachweisen (Rauchtest) oder von einer KI fahren.
 *
 * Bewusst in einem NEUTRALEN Header (nicht in WiesbadenCar.h): so kann auch der
 * ChaosCar die Naht nutzen, ohne WiesbadenCar.h (und dessen kinematisches
 * FWiesbadenVehiclePhysics) einzuziehen. Das bleibt das gemeinsame
 * Steuer-Vokabular beider Fahrzeugarten.
 */
struct FWiesbadenCarControl
{
	float Throttle = 0.0f;   // 0..1 (Gas)
	float Brake = 0.0f;      // 0..1 (Bremse)
	float Steering = 0.0f;   // -1..1 (rechts = +)
	bool bHandbrake = false;
	bool bReverse = false;
};

UINTERFACE(MinimalAPI)
class UWiesbadenVehicleControl : public UInterface
{
	GENERATED_BODY()
};

/**
 * Gemeinsame Steuernaht beider Fahrzeuge (AWiesbadenCar kinematisch,
 * AWiesbadenChaosCar Chaos-Physik).
 *
 * Warum ein Interface und kein gemeinsamer Basistyp: der ChaosCar MUSS von
 * AWheeledVehiclePawn erben (Chaos), der Car ist ein reiner APawn - ein
 * gemeinsamer Pawn-Basistyp ist unmoeglich. Harness, WbDrive und die HUD-Basis
 * casten deshalb auf DIESES Interface (Dependency Inversion) statt auf eine
 * konkrete Klasse, und erreichen damit BEIDE Fahrzeuge ueber denselben Pfad.
 *
 * Bewusst SCHLANK: nur Steuerung + die zwei Readouts (Tempo, Gang), die
 * Harness/Tests/HUD-Basis brauchen. Reiche fahrzeugspezifische Anzeigen
 * (Drehzahlband, Kontrollleuchten, Cockpit) bleiben vorerst Car-spezifisch.
 */
class IWiesbadenVehicleControl
{
	GENERATED_BODY()

public:
	/** Externe Steuerung setzen: umgeht die Tastenabfrage und speist die Werte
	 *  ueber die normale Fahrphysik ein. */
	virtual void SetExternalControl(const FWiesbadenCarControl& Control) = 0;

	/** Externe Steuerung abschalten - Tastatur/Gamepad uebernimmt wieder. */
	virtual void ClearExternalControl() = 0;

	/** True, solange eine externe Steuerung aktiv ist. */
	virtual bool IsExternalControlActive() const = 0;

	/** Absolutgeschwindigkeit in km/h (Tacho/Diagnose). */
	virtual float GetSpeedKmh() const = 0;

	/** Aktueller Gang (1..N; 0 = Leerlauf, negativ = Rueckwaerts). */
	virtual int32 GetGear() const = 0;
};
