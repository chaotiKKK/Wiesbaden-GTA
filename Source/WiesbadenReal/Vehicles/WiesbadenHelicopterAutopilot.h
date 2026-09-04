// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "WiesbadenHelicopterAutopilot.generated.h"

class IWiesbadenHeliControl;

/** Betriebsart des Autopiloten. */
UENUM(BlueprintType)
enum class EWiesbadenAutopilotMode : uint8
{
	/** Aus - der Autopilot gibt die Steuerung frei (Tastatur uebernimmt). */
	Off,
	/** Aktuelle Position/Hoehe halten (Schweben). */
	Hold,
	/** Zu einem Weltziel fliegen und dort halten. */
	Goto
};

/**
 * Einfache Helikopter-KI ueber die externe Steuerung (SetExternalControl).
 *
 * KEIN Test-Code und UNABHAENGIG vom Test-Harness: eine echte Spiel-KI, die den
 * Hubschrauber ueber die normale Rotorphysik fliegt. Sie berechnet je Bild aus
 * der Lageabweichung fertige Steuerwerte (-1..1) und schiebt sie per
 * SetExternalControl ein - der Hubschrauber selbst bleibt frei von Regel-/KI-Code.
 *
 * Regelung (kaskadierte P-Regler mit Geschwindigkeitsdaempfung, bewusst
 * konservativ fuer ruhiges Verhalten):
 *  - Hoehe:  Hoehenfehler -> Ziel-Steigrate -> Kollektiv.
 *  - Lage:   Horizontalfehler -> Ziel-Horizontalgeschwindigkeit -> Nick/Roll
 *            (im Rumpf-Frame, damit die Nase-vorn-Konvention stimmt). Nahe am
 *            Ziel geht die Ziel-Geschwindigkeit gegen 0 -> der Regler bremst die
 *            Restgeschwindigkeit weg und HAELT die Position (Schweben). Im Settle-
 *            Band kommt ein INTEGRALTERM (mit Anti-Windup) dazu, der den letzten,
 *            von der Rotor-Totzone verursachten Restversatz schliesst.
 *  - Gieren: zum Ziel ausrichten, solange es weit weg ist.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenHelicopterAutopilot : public UActorComponent
{
	GENERATED_BODY()

public:
	UWiesbadenHelicopterAutopilot();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Zu einem Weltpunkt fliegen und dort halten. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Autopilot")
	void FlyTo(const FVector& WorldTarget);

	/** Aktuelle Position/Hoehe halten (Schweben). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Autopilot")
	void HoldPosition();

	/** Autopilot abschalten - Tastatur/Gamepad uebernimmt wieder. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Autopilot")
	void Disengage();

	// -- Regel-Parameter (datengetrieben, im Details-Panel einstellbar) --------

	/** Ziel-Horizontalgeschwindigkeit je Meter Abstand (m/s pro m), gekappt.
	 *  Niedrig, damit der traege Rotor frueh genug abbremst und nicht ueberschiesst
	 *  (Bremsweg ~ v^2 / 2a, und die Neige-Bremsautoritaet ist gering). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.05"))
	float ApproachGain = 0.05f;

	/**
	 * MINIMALE Anflug-Horizontalgeschwindigkeit (m/s), solange ausserhalb des
	 * Ankunftsradius. Der Rotor hat eine Anfahr-Totzone: sehr kleine Nick-Befehle
	 * (aus sehr kleiner Ziel-Geschwindigkeit) bewegen ihn nicht -> er bliebe mit
	 * stationaerem Fehler kurz vorm Ziel stehen. Diese Untergrenze haelt den Befehl
	 * ueber der Totzone, bis er WIRKLICH im Radius ist (dort dann 0 = bremsen/halten).
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.0"))
	float MinApproachSpeed = 2.0f;

	/**
	 * Maximale Anflug-Horizontalgeschwindigkeit (m/s).
	 *
	 * Bewusst gemaessigt: schneller Vorwaertsflug kippt den Rotor stark, klaut
	 * Vertikalschub (der Heli sackt ab) und baut so viel Schwung auf, dass der
	 * Anflug ueber das Ziel hinausschiesst und es umkreist. Ruhiger = stabiler.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "1.0"))
	float MaxApproachSpeed = 5.0f;

	/** Nick/Roll je m/s Geschwindigkeitsfehler (Anteil pro m/s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.005"))
	float TiltGain = 0.12f;

	/**
	 * INTEGRAL-Anteil der Lageregelung: Nick/Roll je Meter*Sekunde aufgelaufenem
	 * Positionsfehler. NUR im Settle-Band aktiv (nahe Ziel). Loest den stationaeren
	 * Rest-Versatz: das reine P-Kommando bei kleinem Fehler liegt unter der Rotor-
	 * Anfahr-Totzone -> der Heli bliebe ~15 m neben dem Ziel stehen. Der Integrator
	 * summiert den Fehler, ueberschreitet die Totzone und schliesst die letzten
	 * Meter sanft, ohne den harten Mindesttempo-Kick (der ueberschiessen liesse).
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.0"))
	float TiltIntegralGain = 0.004f;

	/** Anti-Windup: Betrag des aufgelaufenen Positionsfehlers (m*s) wird hierauf
	 *  gedeckelt, damit der Integrator nicht ueberlaeuft und ueberschiesst. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "1.0"))
	float TiltIntegralMaxMeterSeconds = 70.0f;

	/**
	 * Anti-Windup durch bedingte Integration: der Integrator laedt NUR auf, solange
	 * der Heli langsamer als dieser Wert (m/s) ist - also von der Totzone festge-
	 * halten wird. Sobald er sich bewegt, wird nicht weiter aufgeladen, sonst
	 * schoebe der Integrator ueber das Ziel hinaus (langsame Schwingung).
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.1"))
	float IntegralFreezeSpeed = 1.5f;

	/** Zeitkonstante (s), mit der der Integrator auslaeuft - verhindert Dauer-
	 *  Wind-up und laesst ihn nach dem Ueberfahren sauber abklingen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.5"))
	float IntegralLeakTau = 8.0f;

	/** Maximaler Nick-/Roll-Ausschlag des Autopiloten (0..1). Etwas hoeher fuer mehr
	 *  Bremsautoritaet nahe am Ziel (gegen Ueberschiessen des traegen Rotors). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MaxTilt = 0.38f;

	/** Ziel-Steigrate je Meter Hoehenfehler (m/s pro m), gekappt. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.05"))
	float ClimbGain = 0.5f;

	/** Maximale Steig-/Sinkrate des Autopiloten (m/s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.5"))
	float MaxClimbRate = 7.0f;

	/** Kollektiv je m/s Steigraten-Fehler (Anteil pro m/s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.02"))
	float CollectiveGain = 0.24f;

	/**
	 * Auftriebs-Vorsteuerung: zusaetzliches Kollektiv je Einheit |Nick|+|Roll|.
	 *
	 * Kippt der Rotor fuer den Vorflug, faellt der Vertikalschub mit dem Kosinus
	 * der Neigung. Diese Vorsteuerung hebt das Kollektiv vorausschauend an, damit
	 * der Heli im Flug die Hoehe haelt statt abzusacken (reine Rueckfuehrung kaeme
	 * zu spaet - bei vollem Schub sackt er trotzdem).
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.0"))
	float TiltLiftCompensation = 0.5f;

	/** Gierausschlag je Grad Kursfehler (Anteil pro Grad). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.002"))
	float YawGain = 0.02f;

	/** Horizontaler Ankunfts-Radius (m): naeher dran gilt als "am Ziel". */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "1.0"))
	float ArriveRadiusMeters = 8.0f;

	/**
	 * SETTLE-Band-Radius (m): zwischen Ankunftsradius und hier entfaellt das
	 * Mindest-Anflugtempo (MinApproachSpeed), die Zielgeschwindigkeit laeuft
	 * stetig gegen 0. Ohne dieses Band kickt der Mindesttempo-Boden den Heli beim
	 * kleinsten Abdriften mit voller Mindestgeschwindigkeit zur Mitte zurueck ->
	 * er ueberschiesst und kreist (Halte-Grenzzyklus, ~15 m gemessen). Nur JENSEITS
	 * dieses Bandes (echter Transit) gilt der Boden, um die Rotor-Totzone zu
	 * ueberwinden. Muss > ArriveRadiusMeters sein.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "1.0"))
	float SettleRadiusMeters = 30.0f;

	/** Hoehen-Toleranz fuer "am Ziel" (m). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.5"))
	float ArriveAltToleranceMeters = 4.0f;

	/**
	 * Ab diesem Horizontalabstand (m) richtet sich der Rumpf zum Ziel aus.
	 *
	 * Bewusst GROSS: naeher dran wird NICHT mehr gegiert. Sonst dreht sich der
	 * Rumpf beim leichten Ueberschiessen zum jetzt hinter ihm liegenden Ziel, und
	 * die (jetzt echte) Geschwindigkeitsbremse wirkt im rotierenden Frame
	 * tangential -> der Heli umkreist das Ziel. Im festen Frame bremst sie sauber.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "1.0"))
	float FaceTargetMinDistanceMeters = 60.0f;

private:
	// Der Heli wird ueber die Familien-Naht angesprochen (Interface), nicht ueber
	// die konkrete Klasse - EIN Zugriffspfad, denselben wie Harness/WbDrive nutzen.
	IWiesbadenHeliControl* HeliControl() const;

	EWiesbadenAutopilotMode Mode = EWiesbadenAutopilotMode::Off;
	FVector Target = FVector::ZeroVector;

	/** Aufgelaufener horizontaler Positionsfehler (m*s, Weltframe) fuer den
	 *  Integralterm. Wird bei jedem neuen Ziel (FlyTo/HoldPosition/Off) genullt. */
	FVector IntegralErrorXY = FVector::ZeroVector;

	/** Fortschritts-Log im Sekundentakt + einmaliges "erreicht". */
	float ElapsedInMode = 0.0f;
	int32 LastLogSecond = -1;
	bool bArrivedLogged = false;
};
