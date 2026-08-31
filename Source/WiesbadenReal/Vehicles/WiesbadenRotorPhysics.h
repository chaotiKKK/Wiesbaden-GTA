// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WiesbadenRotorPhysics.generated.h"

/**
 * Eingaben der Rotor-Physik (ein Tick).
 *
 * Konvention fuer die Zyklik-/Pedal-Eingaenge (-1..1):
 *  - Collective  : 0..1 (0 = kein Auftrieb, 0.5 = Schwebeflug, 1 = Voll).
 *  - CyclicPitch : +1 = Nase runter / vorwaerts.
 *  - CyclicRoll  : +1 = rechts rollen.
 *  - YawPedal    : +1 = rechts gieren.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenRotorPhysicsInput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotor")
	float Collective = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotor")
	float CyclicPitch = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotor")
	float CyclicRoll = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotor")
	float YawPedal = 0.0f;

	/** Triebwerk laeuft; sonst arbeitet der Rotor nur ueber Autorotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotor")
	bool bEngineRunning = true;
};

/**
 * Ergebnis der Rotor-Physik (ein Tick).
 *
 * Konvention: Lokalkoordinatensystem des Fahrzeugs (+X vor, +Y rechts, +Z oben).
 * Kraft in Newton (N), Drehmoment in Newtonmeter (N*m).
 * Drehmoment-Achsen: X = Roll, Y = Pitch, Z = Yaw.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenRotorPhysicsOutput
{
	GENERATED_BODY()

	/** Aerodynamische Gesamtkraft im Fahrzeug-Lokalkoordinatensystem (N). */
	UPROPERTY(BlueprintReadOnly, Category = "Rotor")
	FVector Force = FVector::ZeroVector;

	/** Aerodynamisches Gesamtdrehmoment (Roll/Pitch/Yaw als X/Y/Z) in N*m. */
	UPROPERTY(BlueprintReadOnly, Category = "Rotor")
	FVector Torque = FVector::ZeroVector;
};

/**
 * Rotor-Physik-Modul fuer Drehfluegler (Hubschrauber, Gyrocopter, ...).
 *
 * Modelliert ein quasi-statisches Hauptrotor-System mit:
 *  - Governor  : haelt die Rotordrehzahl nahe der Soll-Drehzahl (begrenzt
 *                durch die Wellenleistung).
 *  - Lift      : F_L = C_L * omega^2 * CollectivePitch (omega = Rotordrehzahl).
 *  - Collective: Blattanstellwinkel zwischen Min-/MaxCollectivePitchDeg.
 *  - Zyklik    : Neigung der Rotorscheibe -> horizontale Kraft + Pitch/Roll-Moment.
 *  - Heckrotor : Gegendrehmoment-Kompensation + Yaw-Kontrolle (Pedal).
 *  - Autorotation: bei Triebwerksausfall und Sinkflug treibt der aufsteigende
 *                  Luftstrom den Rotor an und erhaelt so Drehzahl und Auftrieb.
 *
 * Das Modul ist bewusst rein (kein Welt-/Actor-Zugriff) und deterministisch -
 * dadurch ohne Level/Actor in Automation-Tests pruefbar. Die Integration
 * (Kraft -> Beschleunigung, Drehmoment -> Winkelbeschleunigung) uebernimmt das
 * Fahrzeug.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenRotorPhysics
{
	GENERATED_BODY()

	// -- Grunddaten -------------------------------------------------------
	/** Fahrzeugmasse (fuer die Gewichtskraft/Normalisierung). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "1.0"))
	//
	// Ka-52 "Alligator": Leergewicht 7,7 t, normales Startgewicht 10,4 t.
	// Hier stand 1.000 kg - die Groessenordnung eines Ultraleicht-Hubschraubers.
	// Alle abhaengigen Beiwerte (Auftrieb, Widerstand, Traegheit, Leistung,
	// Steuermomente) sind mit derselben Aenderung mitskaliert; einzeln
	// verstellt waere die Maschine unfliegbar.
	float MassKg = 9800.0f;

	/** Erdbeschleunigung in m/s^2 (nur fuer die Gewichtskraft). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.1"))
	float GravityMetersPerS2 = 9.81f;

	// -- Hauptrotor -------------------------------------------------------
	/** Hauptrotor-Radius in Metern (informativ/Referenz). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.1"))
	float MainRotorRadiusM = 7.25f;   // Ka-52: 14,5 m Rotordurchmesser

	/** Gebuendelter Auftriebsbeiwert: Lift (N) = Faktor * omega^2 * Pitch(rad). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	//
	// Aus dem Schwebeflug zurueckgerechnet: Gewichtskraft = Faktor * omega^2 *
	// Blattwinkel * 2 (Koaxialpaar). Bei 9.800 kg, 350 U/min und rund 4 Grad
	// Blattwinkel ergibt das 512. Der alte Wert 45 gehoerte zu 1.000 kg und
	// 420 U/min.
	float MainRotorLiftFactor = 512.0f;

	/** Gebuendelter Profilwiderstand: Drag (N*m) = Faktor * omega^2 * (1 + Collective). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	float MainRotorDragFactor = 13.7f;   // mit dem Auftriebsbeiwert mitskaliert

	/** Traegheitsmoment des Rotors um die Drehachse (kg*m^2). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.01"))
	//
	// Skaliert mit Masse und Radius^2 (11,4 * 1,74). Ein grosser Rotor haelt
	// seine Drehzahl laenger - das traegt die Autorotation.
	float MainRotorInertia = 1790.0f;

	/** Soll-Drehzahl des Hauptrotors (U/min). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	float TargetMainRotorRpm = 350.0f;   // Ka-50/52-Rotordrehzahl

	/** Governor-Verstaerkung (N*m pro rad/s Drehzahlabweichung). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	float GovernorGain = 12000.0f;   // mit dem Traegheitsmoment mitskaliert

	/** Verfuegbare Wellenleistung des Triebwerks (W). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	float EnginePowerWatts = 3600000.0f;   // 2 x VK-2500, je 1.800 kW

	/** Min. Blattanstellwinkel (rad) bei Collective = 0. */
	UPROPERTY(EditAnywhere, Category = "Rotor")
	float MinCollectivePitchDeg = 2.0f;

	/** Max. Blattanstellwinkel (rad) bei Collective = 1. */
	UPROPERTY(EditAnywhere, Category = "Rotor")
	float MaxCollectivePitchDeg = 14.0f;

	/**
	 * Blattanstellwinkel, bei dem der Auftrieb genau das Gewicht traegt.
	 *
	 * Wird aus Masse, Auftriebsbeiwert und Soll-Drehzahl BERECHNET, nicht
	 * eingestellt - so bleibt der Schwebepunkt richtig, wenn einer dieser Werte
	 * geaendert wird.
	 */
	float ComputeHoverPitchDeg() const;

	// -- Zyklik -----------------------------------------------------------
	/** Max. Neigung der Rotorscheibe durch Zyklik (deg). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	//
	// 6 Grad waren zu zaghaft fuer eine Maschine, die auf Flugschauen Loopings
	// und Rueckwaertsflug zeigt. 10 Grad Scheibenneigung entsprechen dem, was
	// ein Koaxialrotor ohne Blattueberschlag hergibt.
	float CyclicMaxTiltDeg = 10.0f;

	/** Pitch-Momentenautoritaet bei Vollausschlag und Schwebelast (N*m). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	float CyclicPitchMomentAuthority = 42000.0f;   // mit dem Nickmoment mitskaliert

	/** Roll-Momentenautoritaet bei Vollausschlag und Schwebelast (N*m). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	float CyclicRollMomentAuthority = 14400.0f;   // mit dem Rollmoment mitskaliert

	// -- Heckrotor --------------------------------------------------------
	/** Heckrotor-Schubbeiwert: Schub (N) = Faktor * omega_tail^2 * Pedal. */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	float TailRotorThrustCoefficient = 0.003f;

	/** Abstand Heckrotor -> Schwerpunkt (m). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.1"))
	float TailBoomLengthM = 5.0f;

	/** Uebersetzung: omega_tail = omega_main * Faktor. */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.1"))
	float TailRotorGearRatio = 5.0f;

	/** Skalierung der Reaktionsmoment-Kompensation (1 = neutral ausbalanciert). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	float MainRotorTorqueReactionScale = 1.0f;

	// -- Koaxial-Rotoren & Fluggeschwindigkeit ---------------------------
	/**
	 * Koaxialer, gegenlaeufiger Doppelhauptrotor (Ka-52-Stil): verdoppelt den
	 * Auftrieb je Rotorflaeche, kompensiert das Reaktionsmoment gegenseitig
	 * (kein Heckrotor noetig) und steuert Yaw ueber differentielle
	 * Blattverstellung (Pedal -> direktes Yaw-Moment).
	 */
	UPROPERTY(EditAnywhere, Category = "Rotor")
	bool bCoaxialRotors = false;

	/** Yaw-Moment aus dem Pedal bei Koaxial-Rotoren (N*m pro normalisierter Last). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	float CoaxialYawAuthority = 95000.0f;   // mit dem Giermoment mitskaliert

	/**
	 * Hoechstgeschwindigkeit im Vorwaertsflug (m/s). Ab RetreatingBladeStallStartFrac
	 * davon bricht der Auftrieb der ruecklaufenden Blaetter ein (Retreating
	 * Blade Stall) und sinkt bis vmax auf ~45 %% - begrenzt die Geschwindigkeit
	 * realistisch statt mit einem harten Clamp.
	 */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "1.0"))
	float MaxForwardSpeedMetersPerS = 83.0f;   // Ka-52: 300 km/h Hoechstgeschwindigkeit

	/** Vorwaertsgeschwindigkeit (Anteil von MaxForwardSpeedMetersPerS), ab der der Auftrieb abfaellt. */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.1", ClampMax = "0.99"))
	float RetreatingBladeStallStartFrac = 0.75f;

	// -- Autorotation & Daempfung ----------------------------------------
	/** Autorotations-Gewinn: Antriebsdrehmoment (N*m) pro m/s Sinkgeschwindigkeit. */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	//
	// Mit dem Blattwiderstand mitskaliert (Faktor 11,4). Beim Umstellen auf
	// Ka-52-Werte war dieser Beiwert zuerst UEBERSEHEN worden: Der Widerstand
	// wuchs um das Elffache, das antreibende Moment nicht - die Autorotation
	// hielt danach nur noch 124 statt 350 U/min. Ein Triebwerksausfall waere
	// damit nicht mehr beherrschbar gewesen.
	float AutorotationGain = 3880.0f;

	/** Aerodynamische Winkeldaempfung (N*m pro rad/s), stabilisiert alle Achsen. */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "0.0"))
	float RotorAngularDamping = 26000.0f;   // mit dem Traegheitsmoment mitskaliert

	// -- Zustand ----------------------------------------------------------
	/** Aktuelle Hauptrotor-Drehzahl (U/min). */
	UPROPERTY(BlueprintReadOnly, Category = "Rotor")
	float MainRotorRpm = 0.0f;

	/** Aktuelle Heckrotor-Drehzahl (U/min). */
	UPROPERTY(BlueprintReadOnly, Category = "Rotor")
	float TailRotorRpm = 0.0f;

	/** Aktuelle Triebwerksdrehzahl (U/min); 0 bei abgeschaltetem Triebwerk (Autorotation). */
	UPROPERTY(BlueprintReadOnly, Category = "Rotor")
	float EngineRpm = 0.0f;

	/** Uebersetzung Triebwerk -> Hauptrotor (EngineRpm = MainRotorRpm * Faktor). */
	UPROPERTY(EditAnywhere, Category = "Rotor", meta = (ClampMin = "1.0"))
	float EngineToMainRotorRatio = 8.0f;

	/** Setzt den Rotorzustand zurueck (Drehzahl 0). */
	void Reset();

	/**
	 * Berechnet Kraft und Drehmoment fuer einen Tick und schreibt die
	 * Rotor-Drehzahl fort.
	 *
	 * @param In                              Steuereingaben.
	 * @param DeltaTime                       Zeitschritt in Sekunden.
	 * @param LocalLinearVelocityCmPerS       Fahrzeuggeschwindigkeit lokal (cm/s), fuer Autorotation.
	 * @param LocalAngularVelocityRadPerS     Fahrzeug-Winkelgeschwindigkeit lokal (rad/s), fuer Daempfung.
	 * @param Out                             Ergebnis (Kraft N, Drehmoment N*m).
	 */
	void Tick(const FWiesbadenRotorPhysicsInput& In, float DeltaTime,
		const FVector& LocalLinearVelocityCmPerS, const FVector& LocalAngularVelocityRadPerS,
		FWiesbadenRotorPhysicsOutput& Out);
};
