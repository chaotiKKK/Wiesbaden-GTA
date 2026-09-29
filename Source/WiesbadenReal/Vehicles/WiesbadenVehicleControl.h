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

/**
 * Momentaufnahme der Mastachse (Rotor-Montage) eines Hubschraubers.
 *
 * Warum es diese Messung gibt: die Rotoren eines Koaxial-Hubschraubers sitzen
 * auf EINER Stange. Faellt beim Bau ein Versatz an - Blatt-Drehpunkt neben der
 * Nabe oder Nabe neben dem Rumpfursprung -, laufen die Blaetter nicht um die
 * Stange, sondern um einen Punkt daneben: im Bild eine Kreisbahn.
 *
 * Gemessen werden nur DREHPUNKTE (Komponenten-Ursprünge), nie Bounding-Boxen:
 * der Mittelpunkt der Bounding-Box eines drehenden Blattsterns wandert mit der
 * Drehlage um bis zu 0,25 * Radius und wuerde eine Kreisbahn vortaeuschen.
 *
 * Alle Abstaende sind SEITLICH gemessen (senkrecht zur jeweiligen Achse) - die
 * Hoehe AUF der Stange ist gewollt und darf die Pruefung nicht faerben.
 */
struct FWiesbadenHeliMastSample
{
	/** Hauptrotor-Drehzahl aus der Physik (U/min). */
	float MainRotorRpm = 0.0f;

	/** Drehlage des oberen/unteren Rotorsterns relativ zum Rumpf, 0..360 Grad. */
	float MainAzimuthDeg = 0.0f;
	float LowerAzimuthDeg = 0.0f;

	/** Seitenabstand Naben-Drehpunkt -> Mastachse durch den Rumpfursprung (cm). */
	float MainHubOffsetCm = 0.0f;
	float LowerHubOffsetCm = 0.0f;

	/**
	 * Angewandte Achsenkorrektur, also der Seitenabstand des Blatt-Component-
	 * Ursprugs von der Nabe (cm).
	 *
	 * WICHTIG, die Zahl ist KEIN Fehler und kein Kreisbahn-Detektor mehr: der
	 * Component-Ursprung traegt seit dem 26.09.2026 die Korrektur, mit der der
	 * gemessene Drehpunkt des Mesh auf die Rotorstangenachse gelegt wird
	 * (AWiesbadenHelicopter::GetRotorDrehpunktCm, ComputeRotorMountOffset).
	 * Beim Ka-52 sind das 2,6 cm (oben) und 5,3 cm (unten) - genau die Werte,
	 * die man braucht, damit die Scheibe auf der Stange laeuft. Vorher stand
	 * hier "Blatt-Drehpunkte X cm ab Nabe", und das sah nach dem gerade
	 * behobenen Fehler aus. Die Wirkung steht in MainAxisResidualCm.
	 */
	float MainBladeOffsetCm = 0.0f;
	float LowerBladeOffsetCm = 0.0f;

	/**
	 * Seitenabstand des GEDREHTEN Mesh-Drehpunkts von der Rotorstangenachse
	 * (cm). Das ist die eigentliche Forderung und muss 0 sein - so, wie
	 * "Naben 0,0 cm ab Mastachse" fuer die Nabe.
	 */
	float MainAxisResidualCm = 0.0f;
	float LowerAxisResidualCm = 0.0f;

	/**
	 * Blattstern-Mitte im RUMPF-Frame (cm): x/y quer zur Mastachse, z auf der Stange.
	 *
	 * Der Drehpunkt kann auf der Stange sitzen, waehrend das Netz daneben
	 * gezeichnet ist - dann laeuft der Stern trotzdem um einen Punkt neben der
	 * Stange. Dieser Wert misst die GEOMETRIE und ist darum der Kreisbahn-Detektor.
	 *
	 * ACHTUNG, Momentaufnahme: der WELT-Mittelpunkt der Blatt-Bounds wandert bei
	 * einem drehenden Stern mit der Drehlage um bis zu 0,25 * Rotorradius um die
	 * echte Mitte (ein Stern ist nicht drehinvariant). Erst der MITTELWERT ueber
	 * eine volle Drehung ist die Sternmitte - und weil hier der RUMPF-Frame steht
	 * (nicht der mitdrehende Naben- oder Welt-Frame), mittelt die Messstelle die
	 * Momentaufnahmen einfach. Darum liefert der Heli nur den Wert, die Statistik
	 * macht der Aufrufer (Harness/Tests). Aufloesung ~ (0,25 * Radius) / Bilder,
	 * in der Praxis ~5 cm - ein Versatz ab ~10 cm ist sicher erkennbar.
	 *
	 * NICHT verwenden: den Anker der Mesh-Bounding-Box. Der ist ein starrer Punkt
	 * der Nabe und misst nur die AABB-Asymmetrie des Sterns (hier 1,8 m), ohne
	 * dass irgendetwas schief sitzt.
	 */
	FVector MainBladeCentreInBodyCm = FVector::ZeroVector;
	FVector LowerBladeCentreInBodyCm = FVector::ZeroVector;

	/** Winkel der Naben-Stange gegen die Rumpf-Hochachse (Grad). */
	float MastTiltDeg = 0.0f;

	/** Winkel der Blatt-Drehachse gegen die Naben-Hochachse (Grad). */
	float MainSpinTiltDeg = 0.0f;
	float LowerSpinTiltDeg = 0.0f;
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

	// -- Traktions-Kontrollleuchten (HUD) ---------------------------------
	// Reine MODELL-Ausgaben der Fahrphysik (durchdrehende Antriebsraeder /
	// blockierende Raeder), vom HUD nur KONSUMIERT - kein Verhalten haengt
	// daran. Standard false, damit nur das Fahrzeug, das die Flags fuehrt
	// (der Kaefer mit dem eigenen Einspurmodell), sie ueberschreiben muss;
	// der ChaosCar hat keine solchen Flags und bleibt bei false.

	/** Drehen die Antriebsraeder gerade durch (Radspin)? */
	virtual bool IsWheelSpinning() const { return false; }

	/** Blockieren die Raeder gerade (Bremse ueber der Haftgrenze)? */
	virtual bool IsWheelLocked() const { return false; }

	/** Ist die Handbremse gezogen (Taste oder externe Steuerung)? Fuer die
	 *  Parcours-Wertung (Handbremswende); ChaosCar meldet nie eine. */
	virtual bool IsHandbrakeApplied() const { return false; }
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

	/** Aktuelle Hauptrotor-Drehzahl (U/min). */
	virtual float GetMainRotorRpm() const = 0;

	/** Momentaufnahme der Mastachse/Rotormontage (Welt) - fuer Harness, KI und Werkzeuge. */
	virtual FWiesbadenHeliMastSample SampleRotorMast() const = 0;
};
