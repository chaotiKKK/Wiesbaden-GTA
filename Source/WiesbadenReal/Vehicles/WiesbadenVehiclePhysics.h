// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Vehicles/WiesbadenPowertrainSpec.h"

#include "WiesbadenVehiclePhysics.generated.h"

/**
 * Eingaben der Fahrzeug-Physik (ein Tick).
 *
 * Konvention:
 *  - Throttle : 0..1 (Gaspedal).
 *  - Brake    : 0..1 (Bremspedal, wirkt in Bewegungsrichtung).
 *  - Steering : -1..1 (+1 = rechts lenken, kinematisches Bicycle-Modell).
 *  - Handbrake: true = zusaetzliche Bremskraft (vereinfacht, hinten wirksam).
 *  - bReverseRequested: Umschalten auf Rueckwaertsgang (nur im Stand).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenVehiclePhysicsInput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	float Throttle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	float Brake = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	float Steering = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	bool bHandbrake = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	bool bReverseRequested = false;
};

/**
 * Ergebnis der Fahrzeug-Physik (ein Tick).
 *
 * Laengsdynamik (Beschleunigung, Geschwindigkeit, Gang, Drehzahl) und
 * Querdynamik (YawRate ueber das kinematische Bicycle-Modell). Alle Groessen
 * sind Skalare im Fahrzeug-Lokalkoordinatensystem (+X = vorwaerts).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenVehiclePhysicsOutput
{
	GENERATED_BODY()

	/** Geschwindigkeit entlang der Fahrzeug-X-Achse (m/s, negativ = rueckwaerts). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	float ForwardSpeedMetersPerS = 0.0f;

	/** Absolutgeschwindigkeit in km/h (fuer HUD/Tacho). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	float SpeedKmh = 0.0f;

	/** Aktuelle Motordrehzahl (U/min). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	float EngineRpm = 0.0f;

	/** Aktueller Gang (1..N, 0 = neutral/Rueckwaerts wird als -1 gemeldet). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	int32 Gear = 1;

	/** Gierrate um die Fahrzeug-Z-Achse (rad/s, + = rechts). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	float YawRateRadPerS = 0.0f;

	/** Resultierende Laengsbeschleunigung (m/s^2, + = vorwaerts). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	float ForwardAccelerationMetersPerS2 = 0.0f;

	/** Aktueller Lenkeinschlag -1..1 (Zustand, nicht die rohe Eingabe). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	float SteerAngleNorm = 0.0f;

	/**
	 * QUERgeschwindigkeit im Fahrzeug-Lokalsystem (m/s, +Y = rechts).
	 *
	 * Das ist der Kern des "nicht auf Schienen"-Gefuehls: der Wagen bewegt sich
	 * nicht mehr exakt in Blickrichtung, sondern kann quer rutschen (Schlupf,
	 * Drift). Das Fahrzeug addiert diese Komponente zur Laengsbewegung.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	float LateralVelocityMetersPerS = 0.0f;

	/** Karosserie-Schwimmwinkel in Grad (atan(Vy/Vx)) - fuer HUD/Diagnose. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	float SlipAngleDeg = 0.0f;
};

/**
 * Fahrzeug-Physik-Modul fuer Bodenfahrzeuge (PKW-Prototyp).
 *
 * Modelliert die Laengsdynamik mit:
 *  - Motor      : Drehmomentkurve ueber der Drehzahl (T_max aus der Leistung,
 *                 Abfall zum Leerlauf und zur Drehzahlgrenze).
 *  - Getriebe   : automatische Schaltung ueber Drehzahlschwellen, Uebersetzung
 *                 aus Gangverhaeltnis * Achsantrieb, Drehzahl aus Radumfang.
 *  - Antrieb    : F = T * i_total / Radradius, begrenzt durch das
 *                 Traktionslimit (mu * Gewicht) - kein Radschlupf-Modell.
 *  - Widerstand : Rollreibung + Luftwiderstand (0.5 * rho * CdA * v^2) + Bremse.
 *  - Reverse    : Rueckwaertsgang nur im (fast) Stillstand, begrenzte Max-Speed.
 * Die Querdynamik nutzt das kinematische Bicycle-Modell (yaw = v/L * tan(delta)),
 * begrenzt durch das Seitenkraftlimit (|yaw| <= mu * g / v).
 *
 * Das Modul ist bewusst rein (kein Welt-/Actor-Zugriff) und deterministisch -
 * dadurch ohne Level/Actor in Automation-Tests pruefbar. Die Integration
 * (Geschwindigkeit -> Position, YawRate -> Ausrichtung, Bodenkontakt)
 * uebernimmt das Fahrzeug.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenVehiclePhysics
{
	GENERATED_BODY()

	// -- Grunddaten -------------------------------------------------------
	/** Erdbeschleunigung (m/s^2) - fuer Gewicht und Traktionslimit. */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "0.1"))
	float GravityMetersPerS2 = 9.81f;

	// -- Antriebsstrang (geteilte Quelle der Wahrheit) --------------------
	/** Motor, Getriebe, Achsantrieb und Masse - identisch zum Chaos-Wagen. */
	UPROPERTY(EditAnywhere, Category = "Vehicle")
	FWiesbadenPowertrainSpec Powertrain = FWiesbadenPowertrainSpec::Kaefer1302();

	// -- Getriebe ---------------------------------------------------------
	/** Hochschalten oberhalb dieser Drehzahl (U/min). */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "1000.0"))
	float ShiftUpRpm = 4200.0f;

	/** Runterschalten unterhalb dieser Drehzahl (U/min). */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "100.0"))
	float ShiftDownRpm = 1800.0f;

	/** Radradius (m). */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "0.1"))
	float WheelRadiusM = 0.343f;

	/** Maximalgeschwindigkeit im Rueckwaertsgang (m/s). */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "0.1"))
	float ReverseMaxSpeedMetersPerS = 8.0f;

	// -- Laengswiderstand -------------------------------------------------
	/** Luftwiderstand: Cd * Stirnflaeche (m^2). F_air = 0.5 * rho * CdA * v^2. */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "0.0"))
	// 1.05 statt 0.62: mit der echten Drehmomentkurve (102 Nm statt der frueher
	// aus 32 kW abgeleiteten ~74 Nm) triebe der Wagen sonst auf ~150 km/h. Der
	// hoehere CdA bringt die Spitze zurueck auf die ~130 km/h des Kaefer 1302.
	float DragCoeffAreaM2 = 1.05f;

	/** Rollwiderstandsbeiwert (dimensionslos). */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "0.0"))
	float RollCoeff = 0.012f;

	// -- Treibstoff -------------------------------------------------------
	/** Tankgroesse in Litern. */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Treibstoff", meta = (ClampMin = "1.0"))
	float TankCapacityLiters = 42.0f;

	/**
	 * Aktueller Tankinhalt in Litern (Zustand).
	 *
	 * 42 l entsprechen dem Tank eines Kaefer 1300. Bei Verbrauch im
	 * zweistelligen Literbereich auf 100 km reicht der Tank fuer die
	 * halbe Karte - die Tankstellen-Pickups machen ihn zur Ressource.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Treibstoff")
	float FuelLiters = 42.0f;

	/** Grundverbrauch laufender Motor im Leerlauf (Liter je Stunde). */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Treibstoff", meta = (ClampMin = "0.0"))
	float IdleConsumptionLitersPerHour = 1.5f;

	/** Verbrauch je mechanischer Arbeit (Liter je Kilowattstunde). */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Treibstoff", meta = (ClampMin = "0.0"))
	float ConsumptionLitersPerKWh = 0.35f;

	/** True, solange Treibstoff da ist; ein leerer Motor liefert keine Kraft. */
	bool HasFuel() const { return FuelLiters > 0.0f; }

	/** Tankfuellstand 0..1 (fuer HUD). */
	float GetFuelFraction() const { return TankCapacityLiters > 0.0f ? FMath::Clamp(FuelLiters / TankCapacityLiters, 0.0f, 1.0f) : 0.0f; }

	/**
	 * Tankt nach.
	 * @return false, wenn der Tank bereits voll war (das Pickup bleibt dann liegen).
	 */
	bool Refuel(float Liters)
	{
		if (FuelLiters >= TankCapacityLiters - 0.01f)
		{
			return false;
		}
		FuelLiters = FMath::Min(FuelLiters + FMath::Max(Liters, 0.0f), TankCapacityLiters);
		return true;
	}

	/** Bremskraft bei vollem Bremspedal (N). */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "0.0"))
	//
	// 14.000 N bei 820 kg waeren 17 m/s^2, also 1,7 g. Das ist physikalisch
	// unmoeglich - mehr als die Reifen uebertragen koennen. Ein Reifen auf
	// trockenem Asphalt schafft rund 0,9 g, ein Kaefer von 1969 mit
	// Trommelbremsen rundum eher 0,7 g. 5.600 N entsprechen genau dem und
	// ergeben einen Bremsweg von rund 14 m aus 50 km/h.
	//
	// Mit dem alten Wert stand das Fahrzeug schlagartig - das war einer der
	// Gruende, aus denen sich die Fahrphysik unrealistisch anfuehlte.
	float BrakeForceN = 5600.0f;

	// -- Querdynamik ------------------------------------------------------
	/** Maximaler Lenkeinschlag der Vorderraeder (Grad). */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "1.0"))
	float MaxSteerAngleDeg = 35.0f;

	/**
	 * Geschwindigkeit, bei der der nutzbare Lenkeinschlag auf die Haelfte faellt (m/s).
	 *
	 * Ohne diese Abnahme kommandiert ein voll durchgedruecktes A oder D auch
	 * bei Tempo 100 den vollen Anschlag von 35 Grad. Die Seitenkraftgrenze
	 * fing das zwar ab, aber erst als harte Klemmung - das Fahrzeug schlug
	 * dabei in die Begrenzung, statt weich zu reagieren.
	 *
	 * 12 m/s (43 km/h) heisst: bei Schrittgeschwindigkeit voller Einschlag zum
	 * Rangieren, bei 50 km/h noch 16 Grad, bei 120 km/h rund 9 Grad.
	 */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "1.0"))
	float SteerFalloffSpeedMetersPerS = 12.0f;

	/**
	 * Geschwindigkeit, mit der sich der Lenkeinschlag aufbaut (Anteil je Sekunde).
	 *
	 * Die Simulation gab den Lenkbefehl bisher unveraendert an das Giermodell
	 * weiter: Ein Tastendruck bedeutete Volleinschlag im selben Bild. Ein
	 * Fahrzeug, das seine Richtung in einer Sechzigstelsekunde aendert, fuehlt
	 * sich nach nichts an, was auf Raedern steht - das war der groesste Anteil
	 * an der unrealistischen Fahrphysik.
	 *
	 * 2,5 je Sekunde heisst: von Anschlag zu Anschlag in 0,8 s. Ein Kaefer von
	 * 1969 hat 2,6 Lenkradumdrehungen ohne Servounterstuetzung - schneller
	 * bekommt man das Rad nicht herum.
	 */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "0.1"))
	float SteerRatePerSecond = 2.5f;

	/**
	 * Geschwindigkeit, mit der die Lenkung ohne Eingabe zurueckstellt.
	 *
	 * Groesser als der Aufbau: Die Vorspur richtet die Raeder von selbst
	 * gerade, das geht schneller als das Einlenken gegen die Reifenaufstands-
	 * kraefte.
	 */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "0.1"))
	float SteerReturnRatePerSecond = 4.0f;

	/**
	 * Motorbremsmoment bei Nenndrehzahl in Nm (Schubbetrieb, Gas geschlossen).
	 *
	 * Ohne diesen Anteil rollt das Fahrzeug beim Gaswegnehmen nur gegen Roll-
	 * und Luftwiderstand aus - es fuehlt sich an wie im Leerlauf, obwohl ein
	 * Gang eingelegt ist. Ein luftgekuehlter Boxer mit 1,5 l bremst im Schub
	 * mit rund 30 Nm.
	 */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "0.0"))
	float EngineBrakeTorqueNm = 30.0f;

	/** Radstand (m) - kinematisches Bicycle-Modell. */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "0.5"))
	float WheelbaseM = 2.7f;

	/** Reibbeiwert Reifen/Strasse - begrenzt Antriebs- UND Querkraft. */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "0.1"))
	//
	// 0,75 statt 0,9: Ein Kaefer von 1969 faehrt auf Diagonalreifen, die
	// deutlich weniger Seitenfuehrung aufbauen als moderne Guerteilreifen.
	float MuTraction = 0.75f;

	// -- Dynamisches Einspurmodell (Querschlupf/Drift) --------------------
	//
	// Das kinematische Bicycle-Modell laesst den Wagen exakt in Blickrichtung
	// fahren - kein Schlupf, kein Drift, kein Unter-/Uebersteuern: "auf
	// Schienen". Das dynamische Einspurmodell rechnet stattdessen die
	// Reifen-Seitenkraefte aus den Schraeglaufwinkeln und laesst den Wagen quer
	// rutschen. Ab LowSpeedBlend aktiv (bei v->0 ist das Modell singulaer).

	/** Schraeglaufsteifigkeit Vorderachse (N je rad Schraeglaufwinkel). */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "1000.0"))
	float CorneringStiffnessFrontNPerRad = 30000.0f;

	/**
	 * Schraeglaufsteifigkeit Hinterachse. Bewusst HOEHER als vorn: der Kaefer
	 * ist hecklastig (Motor hinten) und neigt zum Uebersteuern; eine steifere
	 * Hinterachse haelt ihn ueber den ganzen Geschwindigkeitsbereich stabil
	 * (kritische Geschwindigkeit ueber der Hoechstgeschwindigkeit), laesst aber
	 * unter Last/hartem Einlenken das Heck gutmuetig kommen.
	 */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "1000.0"))
	float CorneringStiffnessRearNPerRad = 36000.0f;

	/** Giertraegheitsmoment um die Hochachse (kg*m^2). ~ m*a*b fuer einen PKW. */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "1.0"))
	float YawInertiaKgM2 = 1150.0f;

	/** Gewichtsanteil auf der Vorderachse (Kaefer hecklastig: ~0,42). */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float FrontWeightFraction = 0.42f;

	/** Unterhalb dieser Geschwindigkeit kinematisch lenken (m/s). */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "0.5"))
	float LowSpeedBlendMetersPerS = 3.0f;

	// -- Zustand ----------------------------------------------------------
	/** Aktuelle Geschwindigkeit entlang der Fahrzeug-X-Achse (m/s). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	float SpeedMetersPerS = 0.0f;

	/** Aktueller Gang (1..N; -1 = Rueckwaerts). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	int32 Gear = 1;

	/** Aktuelle Motordrehzahl (U/min). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	float EngineRpm = 850.0f;

	/**
	 * Aktueller Lenkeinschlag, normiert -1..1.
	 *
	 * Zustand, nicht Eingabe: Er folgt dem Lenkbefehl mit begrenzter
	 * Geschwindigkeit (SteerRatePerSecond) und stellt ohne Eingabe zurueck.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Zustand")
	float SteerAngleNorm = 0.0f;

	/** Quergeschwindigkeit im Lokalsystem (m/s, +Y = rechts) - Schlupf-Zustand. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Zustand")
	float LateralVelocityMetersPerS = 0.0f;

	/** Gierrate als integrierter Zustand (rad/s, + = rechts). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Zustand")
	float YawRateRadPerS = 0.0f;

	/**
	 * Treibt die Laengs-/Querdynamik einen Schritt weiter.
	 * @param Out Ergebnisfelder des Ticks (Speed, Drehzahl, Gang, YawRate, a).
	 */
	/**
	 * Nutzbarer Lenkeinschlag in Grad bei der aktuellen Geschwindigkeit.
	 *
	 * Oeffentlich, weil datenrein pruefbar: der Test
	 * Vehicles.Physics.SteeringFalloff haelt die Kennlinie fest.
	 */
	static float ComputeUsableSteerAngleDeg(
		float MaxSteerAngleDeg, float SpeedMetersPerS, float FalloffSpeedMetersPerS);

	/**
	 * Naechster Lenkeinschlag aus dem aktuellen und dem Lenkbefehl.
	 *
	 * Datenrein und statisch, damit die Kennlinie ohne Fahrzeug pruefbar ist.
	 * Zurueckstellen (Betrag wird kleiner) laeuft mit ReturnRate, Einlenken
	 * mit Rate.
	 */
	static float AdvanceSteerAngle(
		float CurrentNorm, float TargetNorm, float Rate, float ReturnRate, float DeltaSeconds);

	/**
	 * Verbleibende Querbeschleunigung in m/s^2 nach dem Reibungskreis.
	 *
	 * Ein Reifen hat EIN Kraftbudget. Wer 0,7 g bremst, hat fuer die Kurve
	 * nicht mehr die vollen 0,75 g uebrig, sondern nur noch die Wurzel aus dem
	 * Rest. Ohne diese Kopplung liess sich mit voller Bremsung genauso scharf
	 * einlenken wie ohne - der haeufigste Grund, aus dem sich ein Fahrmodell
	 * wie auf Schienen anfuehlt.
	 */
	static float ComputeAvailableLateralAccel(
		float MuTraction, float GravityMetersPerS2, float LongitudinalAccelMetersPerS2);

	void Tick(const FWiesbadenVehiclePhysicsInput& Input, float DeltaSeconds, FWiesbadenVehiclePhysicsOutput& Out);

	/** Setzt das Fahrzeug in den Ruhezustand zurueck (Stand, 1. Gang, voller Tank). */
	void Reset();

private:
	float GetDriveForce(float Throttle) const;
	float GetTotalGearRatio() const;
	float RpmFromSpeed(float Speed) const;
	float MotorTorqueAt(float Rpm) const;
	void ShiftGear(const FWiesbadenVehiclePhysicsInput& Input, float DeltaSeconds);
	float ComputeYawRate(float SteeringInput, float LongitudinalAccelMetersPerS2 = 0.0f) const;


};
