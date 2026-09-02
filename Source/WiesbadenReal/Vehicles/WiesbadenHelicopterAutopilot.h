// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "WiesbadenHelicopterAutopilot.generated.h"

class AWiesbadenHelicopter;

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
 *            Restgeschwindigkeit weg und HAELT die Position (Schweben).
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

	/** Aktuelle Betriebsart. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Autopilot")
	EWiesbadenAutopilotMode GetMode() const { return Mode; }

	/** Aktuelles Weltziel (nur sinnvoll bei Hold/Goto). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Autopilot")
	FVector GetTarget() const { return Target; }

	// -- Regel-Parameter (datengetrieben, im Details-Panel einstellbar) --------

	/** Ziel-Horizontalgeschwindigkeit je Meter Abstand (m/s pro m), gekappt.
	 *  Niedrig, damit der Anflug frueh abbremst und nicht ueberschiesst. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.05"))
	float ApproachGain = 0.18f;

	/**
	 * Maximale Anflug-Horizontalgeschwindigkeit (m/s).
	 *
	 * Bewusst gemaessigt: schneller Vorwaertsflug kippt den Rotor stark, klaut
	 * Vertikalschub (der Heli sackt ab) und baut so viel Schwung auf, dass der
	 * Anflug ueber das Ziel hinausschiesst und es umkreist. Ruhiger = stabiler.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "1.0"))
	float MaxApproachSpeed = 11.0f;

	/** Nick/Roll je m/s Geschwindigkeitsfehler (Anteil pro m/s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.005"))
	float TiltGain = 0.12f;

	/** Maximaler Nick-/Roll-Ausschlag des Autopiloten (0..1). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MaxTilt = 0.28f;

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

	/** Hoehen-Toleranz fuer "am Ziel" (m). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "0.5"))
	float ArriveAltToleranceMeters = 4.0f;

	/** Ab diesem Horizontalabstand (m) richtet sich der Rumpf zum Ziel aus. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Autopilot", meta = (ClampMin = "1.0"))
	float FaceTargetMinDistanceMeters = 12.0f;

private:
	AWiesbadenHelicopter* Heli() const;

	EWiesbadenAutopilotMode Mode = EWiesbadenAutopilotMode::Off;
	FVector Target = FVector::ZeroVector;

	/** Fortschritts-Log im Sekundentakt + einmaliges "erreicht". */
	float ElapsedInMode = 0.0f;
	int32 LastLogSecond = -1;
	bool bArrivedLogged = false;
};
