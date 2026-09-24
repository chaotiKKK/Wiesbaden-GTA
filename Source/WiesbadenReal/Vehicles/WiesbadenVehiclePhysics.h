// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Vehicles/WiesbadenPowertrainSpec.h"
#include "Vehicles/WiesbadenFuelTank.h"

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

	/**
	 * Griffigkeit des UNTERGRUNDS, 0..1 (1 = trockener Asphalt, kleiner = nass /
	 * Kopfsteinpflaster / Schotter).
	 *
	 * Skaliert das effektive mu und wirkt damit ueber DIESELBE Kopplung wie die
	 * Reifenhaftung: Traktion, Anfahr-Radspin und das grip-abgeleitete Brems-
	 * blockieren setzen auf griffarmem Belag frueher/staerker ein. Das Fahrzeug
	 * liefert den Wert (aktuell ein Dev-Override, spaeter aus dem Strassenbelag);
	 * die Physik bleibt rein.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float SurfaceGripScale = 1.0f;
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

	/**
	 * True, solange die ANTRIEBSraeder durchdrehen (Anfahr-Radspin).
	 *
	 * Die geforderte Antriebslaengskraft ueberschreitet die Haftreibung der
	 * (dynamisch belasteten) Hinterachse; der Grip faellt auf Gleitreibung. Das
	 * ist der sichtbare Traktionsverlust beim harten Anfahren.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	bool bWheelSpin = false;

	/**
	 * True, solange die Raeder beim Bremsen blockieren (Bremsschlupf).
	 *
	 * Die geforderte Bremskraft ueberschreitet die Haftreibung; die uebertragene
	 * Kraft pulst dann zwischen Gleit- und Haftreibung (Threshold-/ABS-Anmutung)
	 * und die Seitenfuehrung bricht ueber den Reibungskreis weg (kein Lenken mit
	 * blockierten Raedern).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle")
	bool bWheelLock = false;
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

	// -- Treibstoff (eigenes Modul, eigener Besitzer) ---------------------
	/** Tank als Gameplay-Ressource - Fuellstand/Verbrauch/Nachtanken. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Treibstoff")
	FWiesbadenFuelTank Fuel;

	// Duenne Weiterreicher an den Tank - halten die oeffentliche Physik-API
	// stabil (HUD/Pickups fragen weiter das Fahrzeug, nicht den Tank direkt).
	bool HasFuel() const { return Fuel.HasFuel(); }
	float GetFuelFraction() const { return Fuel.GetFuelFraction(); }
	bool Refuel(float Liters) { return Fuel.Refuel(Liters); }

	/** Bremskraft bei vollem Bremspedal (N). */
	UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (ClampMin = "0.0"))
	//
	// Das ist die BremsANFORDERUNG bei vollem Pedal, NICHT die am Reifen
	// wirksame Kraft. 5.600 N bei 820 kg sind rund 0,7 g - die reale Verzoegerung
	// eines Kaefer von 1969 mit Trommelbremsen, Bremsweg ~14 m aus 50 km/h.
	//
	// Das Blockieren haengt NICHT an diesem Wert: der Tick prueft die Anforderung
	// gegen den REIBUNGSKREIS-REDUZIERTEN Laengs-Grip (mu*Gewicht abzueglich der
	// quer verbrauchten Haftung). Auf der Geraden steht der volle Grip (mu*g >
	// 0,7 g), das Pedal blockiert dort NICHT; beim Bremsen in der Kurve oder auf
	// griffarmem Belag faellt der verfuegbare Grip unter die Anforderung und die
	// Raeder blockieren - grip-abgeleitet, robust gegen Aenderungen von Masse/mu.
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

	/**
	 * Schwerpunkthoehe ueber Grund (m) - Hebel der Laengs-Radlastverlagerung.
	 *
	 * Bremsen und Beschleunigen kippen Last zwischen den Achsen: die
	 * uebertragene Last ist m * a_x * h / L. Ein Kaefer 1302 hat einen tiefen,
	 * hecklastigen Schwerpunkt bei rund 0,45 m. Ohne diesen Hebel blieben die
	 * Achslasten statisch und die Kurvenbalance reagierte NICHT auf die Pedale -
	 * genau das fehlte fuer ein glaubwuerdiges Fahrgefuehl.
	 */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "0.1"))
	float CgHeightM = 0.45f;

	/**
	 * Verhaeltnis Gleit- zu Haftreibung (kinetic/static, 0..1).
	 *
	 * Ein rutschender Reifen (durchdrehend oder blockiert) uebertraegt WENIGER
	 * als ein haftender - genau darum kostet Radspin Vortrieb und ein blockiertes
	 * Rad bremst schlechter als ein rollendes an der Haftgrenze. ~0,72 ist ein
	 * ueblicher Wert fuer Reifen auf Asphalt (Haft 0,75 -> Gleit 0,54).
	 */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MuKineticFraction = 0.72f;

	/**
	 * Pulsfrequenz der Blockier-/ABS-Anmutung beim Bremsen (Hz).
	 *
	 * Ueber der Haftgrenze wechselt das Rad zwischen blockiert und wieder
	 * greifend; die uebertragene Bremskraft pulst mit dieser Frequenz zwischen
	 * Gleit- und Haftreibung. 12 Hz entspricht dem Rubbeln einer
	 * Schwellwertbremsung / einfacher ABS-Regelung.
	 */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physik", meta = (ClampMin = "1.0"))
	float BrakeAbsPulseHz = 12.0f;

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
	 * Laengsbeschleunigung des VORTICKS (m/s^2) - Radlast der Antriebsachse.
	 *
	 * Die Traktionsgrenze der Hinterachse haengt an ihrer dynamischen Last, die
	 * wiederum von der Laengsbeschleunigung kommt. Weil die Antriebskraft die
	 * Beschleunigung erst erzeugt, waere das im selben Tick zirkulaer - deshalb
	 * die Last aus dem letzten Tick.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Zustand")
	float LastLongAccelMetersPerS2 = 0.0f;

	/** Phase der Bremsschlupf-Pulsung (rad) - Zustand der ABS-Anmutung. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Zustand")
	float BrakeAbsPhaseRad = 0.0f;

	/** Hysterese-Zustand Antriebsschlupf (Rad dreht durch). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Zustand")
	bool bDriveSlipState = false;

	/** Hysterese-Zustand Bremsschlupf (Rad blockiert). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Zustand")
	bool bBrakeLockState = false;

	/** Belags-Griffigkeit dieses Ticks (0..1, 1 = trocken) - aus dem Input. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Zustand")
	float SurfaceGripScale = 1.0f;

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

	/**
	 * Dynamischer Vorderachs-Lastanteil (0..1) nach Laengs-Radlastverlagerung.
	 *
	 * Beim Bremsen (a_x < 0) kippt Last nach VORN (Anteil steigt), beim
	 * Beschleunigen nach HINTEN (Anteil faellt). Uebertragener Anteil =
	 * a_x * h / (g * L). Das macht die Kurvenbalance pedalabhaengig: geladene
	 * Vorderachse beisst beim Einlenken/Trail-Braking, entlastete Hinterachse
	 * kommt (Lastwechsel-Uebersteuern des Heckmotor-Kaefers); unter Gas ist es
	 * umgekehrt (stabil, leichtes Untersteuern).
	 *
	 * Oeffentlich und datenrein, damit die Kennlinie ohne Fahrzeug pruefbar ist
	 * (Test Vehicles.Physics.LoadTransfer). Auf [0,08 .. 0,92] geklemmt, damit
	 * keine Achse rechnerisch voellig entlastet (ein 4-Rad-Fahrzeug hebt beim
	 * Bremsen/Gasgeben keine Achse ganz ab).
	 */
	static float ComputeDynamicFrontLoadFraction(
		float StaticFrontFraction, float LongitudinalAccelMetersPerS2,
		float GravityMetersPerS2, float CgHeightM, float WheelbaseM);

	/**
	 * Uebertragene Laengskraft eines Reifens mit Haft-/Gleitreibung + Hysterese.
	 *
	 * Solange die Anforderung unter der Haftreibung bleibt, wird sie voll
	 * uebertragen. UEberschreitet sie die Haftreibung, RUTSCHT der Reifen (Rad
	 * dreht durch bzw. blockiert): der Grip faellt auf die (kleinere) Gleit-
	 * reibung und bleibt dort, bis die Anforderung wieder unter die Gleitreibung
	 * faellt (Hysterese gegen Flattern am Grenzwert). @param bSlipping wird als
	 * Zustand hinein- und herausgereicht. Datenrein pruefbar
	 * (Test Vehicles.Physics.LongitudinalSlip).
	 */
	static float ComputeTransmittedLongitudinalForce(
		float DemandN, float StaticGripN, float KineticGripN, bool& bSlipping);

	/**
	 * Uebertragbare Bremskraft bei blockierendem Rad - Threshold-/ABS-Anmutung.
	 *
	 * Pulst zwischen Gleit- und Haftreibung (das Rad wechselt zwischen blockiert
	 * und wieder greifend), erreicht NIE mehr als die Haftreibung ("begrenzt")
	 * und liegt im Mittel bei (Static+Kinetic)/2. Datenrein pruefbar.
	 */
	static float ComputeAbsBrakeCapN(float StaticGripN, float KineticGripN, float PhaseRad);

	void Tick(const FWiesbadenVehiclePhysicsInput& Input, float DeltaSeconds, FWiesbadenVehiclePhysicsOutput& Out);

	/** Setzt das Fahrzeug in den Ruhezustand zurueck (Stand, 1. Gang, voller Tank). */
	void Reset();

private:
	/**
	 * Laengsdynamik EINES Ticks: Schalten, Antrieb mit Radschlupf, Widerstaende,
	 * Bremse mit Blockieren, Integration von Geschwindigkeit/Drehzahl, Verbrauch.
	 * Fuellt die Laengs-Ausgaben und liefert die Laengsbeschleunigung, die die
	 * Querdynamik fuer Reibungskreis und Radlastverlagerung braucht.
	 */
	float TickLongitudinal(const FWiesbadenVehiclePhysicsInput& Input, float DeltaSeconds, FWiesbadenVehiclePhysicsOutput& Out);

	/**
	 * Querdynamik EINES Ticks: Lenkeinschlag nachfuehren, dann dynamisches
	 * Einspurmodell (bzw. kinematisch bei geringem Tempo). Braucht die
	 * Laengsbeschleunigung aus TickLongitudinal.
	 */
	void TickLateral(const FWiesbadenVehiclePhysicsInput& Input, float DeltaSeconds, float LongitudinalAccelMetersPerS2, FWiesbadenVehiclePhysicsOutput& Out);

	/** Haft-/Gleitreibungs-Kraft einer Achse aus ihrer Radlast (eine Politik, EIN Ort). */
	/** Effektiver Reibbeiwert = Reifenhaftung * Belags-Griffigkeit. */
	float EffectiveMuTraction() const { return MuTraction * SurfaceGripScale; }
	float StaticGripN(float LoadN) const { return EffectiveMuTraction() * LoadN; }
	float KineticGripN(float LoadN) const { return EffectiveMuTraction() * MuKineticFraction * LoadN; }

	/** Rohe Antriebs-Laengskraft am Rad aus Motormoment*Uebersetzung/Radius (vor Grip). */
	float GetWheelForceDemand(float Throttle) const;
	float GetTotalGearRatio() const;
	float RpmFromSpeed(float Speed) const;
	float MotorTorqueAt(float Rpm) const;
	void ShiftGear(const FWiesbadenVehiclePhysicsInput& Input, float DeltaSeconds);
	float ComputeYawRate(float SteeringInput, float LongitudinalAccelMetersPerS2 = 0.0f) const;


};
