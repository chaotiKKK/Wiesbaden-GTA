// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "WiesbadenFootPawn.generated.h"

class UAnimSequence;
class UCameraComponent;
class UCapsuleComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UWiesbadenCarAudioComponent;
class UWiesbadenWeaponComponent;

/**
 * Spieler zu Fuss - fuer Aus- und Einsteigen.
 *
 * Bewusst schlank gehalten und OHNE Input-Bindings: das Projekt hat keine
 * (weder DefaultInput.ini noch SetupPlayerInputComponent). Fahrzeug und
 * Helikopter fragen die Tasten direkt ueber den PlayerController ab; dieser
 * Pawn macht es genauso, damit es nur einen Weg gibt statt zwei.
 *
 * Die Hoehe wird wie beim Fahrzeug per Raycast nach unten bestimmt. Seit die
 * Fahrbahn Kollision hat, trifft der Strahl die Strasse und nicht mehr nur das
 * Landscape darunter.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenFootPawn : public APawn
{
	GENERATED_BODY()

public:
	AWiesbadenFootPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Mitfahrmodus (Nerobergbahn): eigene Bewegung und Bodenverfolgung aus,
	 * Umschauen bleibt an. Ohne diese Sperre wuerde FollowGround den
	 * angehaengten Fahrgast in jedem Bild wieder auf das Gelaende ziehen -
	 * die Bahn fuehre ohne ihn ab.
	 */
	void SetRiding(bool bInRiding) { bRiding = bInRiding; }

	/** Faehrt der Spieler gerade mit? Seit es ZWEI Bus-Actoren gibt (Linie 6 und
	 *  Linie 3), muss der Einstieg fragen, ob schon jemand den Fahrgast hat:
	 *  beide Actors sehen denselben Tastendruck und haetten sich sonst beide
	 *  denselben Pawn angehaengt (jeder mit eigenem Anker). */
	bool IsRiding() const { return bRiding; }

	// -- Gesundheit ---------------------------------------------------------
	/**
	 * Gesundheit der Figur in Punkten.
	 *
	 * Bewusst hier und nicht in einem allgemeinen Health-Component: das
	 * Projekt hat keine Treffer-Physik gegen den Spieler - die Quelle fuer
	 * Schaden/Heilung sind heute ausschliesslich die Pickups. Sobald echte
	 * Gesundheitsmechanik dazukommt, gehoert das in eine Komponente.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wiesbaden|Gesundheit", meta = (ClampMin = "1.0"))
	float MaxHealthPoints = 100.0f;

	/** Aktueller Gesundheitszustand in Punkten. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Gesundheit")
	float HealthPoints = 100.0f;

	/**
	 * Heilt die Figur.
	 * @return false, wenn bereits volle Gesundheit bestand (das Pickup bleibt dann liegen).
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Gesundheit")
	bool Heal(float Points)
	{
		if (HealthPoints >= MaxHealthPoints - 0.01f)
		{
			return false;
		}
		HealthPoints = FMath::Min(HealthPoints + FMath::Max(Points, 0.0f), MaxHealthPoints);
		return true;
	}

	/** Gehgeschwindigkeit in km/h. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "1.0"))
	float WalkSpeedKmh = 6.0f;

	/** Laufgeschwindigkeit bei gehaltener Umschalttaste, in km/h. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "1.0"))
	float SprintSpeedKmh = 16.0f;

	/** Drehgeschwindigkeit ueber die Pfeiltasten in Grad je Sekunde. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "10.0"))
	float LookSpeedDegPerS = 120.0f;

	/** Mausempfindlichkeit (Grad je Mauseinheit). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "0.01"))
	float MouseSensitivity = 1.0f;

	/** Totzone der Gamepad-Sticks (Anteil des Vollausschlags). */
	UPROPERTY(EditAnywhere, Category = "Fussgaenger|Steuerung", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float GamepadDeadzone = 0.15f;

	/** Kruemmung der Umschau-Kennlinie am Stick (0 = linear, 1 = stark). */
	UPROPERTY(EditAnywhere, Category = "Fussgaenger|Steuerung", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GamepadLookExpo = 0.5f;

	/** Umschau-Geschwindigkeit am rechten Stick in Grad je Sekunde. */
	UPROPERTY(EditAnywhere, Category = "Fussgaenger|Steuerung", meta = (ClampMin = "10.0"))
	float GamepadLookSpeedDegPerS = 180.0f;

	/**
	 * Abstand der Fuesse zum Boden in cm.
	 *
	 * Die Kapsel wird mit ihrem MITTELPUNKT gesetzt; ohne diesen Zuschlag
	 * steckt die Figur bis zur Huefte im Asphalt.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "0.0"))
	float FootFloorClearanceCm = 3.0f;

	/**
	 * Hoechste Stufe, die die Figur ohne Sprung nimmt, in cm.
	 *
	 * 40 cm decken den Bordstein (12 cm), Randsteine und flache Treppen ab.
	 * Ohne Stufenlogik glitt die Kapsel an jeder Bordsteinkante ENTLANG - der
	 * Gehweg war schlicht nicht betretbar und wirkte, als schwebe er.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "0.0"))
	float MaxStepHeightCm = 40.0f;

	/** Zuschlag beim Vorwaertstasten ueber die Stufe, in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "0.0"))
	float StepForwardProbeCm = 12.0f;

	/**
	 * Absprunggeschwindigkeit in cm/s.
	 *
	 * 420 cm/s ergeben bei 980 cm/s^2 Fallbeschleunigung rund 90 cm
	 * Sprunghoehe - genug fuer Mauern und Treppenabsaetze, aber keine
	 * Hauswand.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "0.0"))
	float JumpSpeedCmS = 420.0f;

	/** Fallbeschleunigung in cm/s^2. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "0.0"))
	float GravityCmPerS2 = 980.0f;

	/**
	 * Ab dieser Hoehe ueber dem Boden faellt die Figur, statt nachgezogen zu
	 * werden. Kleinere Abstaende glaettet die Bodenverfolgung weich weg.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "1.0"))
	float FallThresholdCm = 45.0f;

	/** Zeit zwischen zwei Schuessen in Sekunden (Feuerrate). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Waffe", meta = (ClampMin = "0.02"))
	float FireIntervalSeconds = 0.12f;

	/** Oeffnungswinkel der Handlampe in Grad. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Figur", meta = (ClampMin = "5.0", ClampMax = "80.0"))
	float TorchOuterConeAngle = 34.0f;

	/** Reichweite der Handlampe in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Figur", meta = (ClampMin = "100.0"))
	float TorchRangeCm = 3500.0f;

	/** Helligkeit der Handlampe (Candela). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Figur", meta = (ClampMin = "0.0"))
	float TorchIntensity = 20000.0f;

	/**
	 * Die Waffe des Spielers.
	 *
	 * Zuvor war die Waffe ein flacher Quader am Koerper und ein
	 * DrawDebugLine-Strahl beim Schuss: kein Ton, kein sichtbares Geschoss,
	 * kein Muendungsfeuer. Diese Komponente ersetzt beides.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fussgaenger|Waffe")
	UWiesbadenWeaponComponent* Weapon = nullptr;

	/** Kollisionskapsel - traegt alle uebrigen Teile. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fuss")
	UCapsuleComponent* Capsule = nullptr;

	/** Rumpf der Figur. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Figur")
	UStaticMeshComponent* BodyMesh = nullptr;

	/**
	 * Die animierte Spielfigur (SK_Sebbo).
	 *
	 * Sobald das Skelett-Modell vorliegt, uebernimmt sie: Gehen wird als
	 * Schrittzyklus abgespielt, und statt der Pistole schwingt Sebbo die
	 * Kettensaege. Das statische BodyMesh bleibt als Rueckfall bestehen.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Figur")
	USkeletalMeshComponent* FigureMesh = nullptr;

	/** Kettensaegen-Klang: der Fahrzeug-Synthesizer als Zweitakter. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Figur")
	UWiesbadenCarAudioComponent* SawAudio = nullptr;

	/** Dauer eines Saegehiebs in Sekunden (Laenge von Sebbo_Swing). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Nahkampf", meta = (ClampMin = "0.1"))
	float SwingSeconds = 0.7f;

	/** Zeitpunkt des Treffers innerhalb des Hiebs (Durchzug bei Bild 11/30). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Nahkampf", meta = (ClampMin = "0.0"))
	float SwingHitAtSeconds = 0.37f;

	/** Reichweite des Saegehiebs in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Nahkampf", meta = (ClampMin = "50.0"))
	float MeleeRangeCm = 180.0f;

	/** Radius der Trefferkugel in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Nahkampf", meta = (ClampMin = "10.0"))
	float MeleeRadiusCm = 70.0f;

	/** Wie lange ein getroffener Fussgaenger liegen bleibt, in Sekunden. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Nahkampf", meta = (ClampMin = "0.5"))
	float PedestrianDownSeconds = 12.0f;

	/**
	 * Gehtempo, fuer das der Schrittzyklus einmal je Sekunde laeuft (m/s).
	 * Schnelleres Gehen beschleunigt die Bewegung im selben Verhaeltnis.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Figur", meta = (ClampMin = "0.1"))
	float WalkAnimSpeedMps = 1.67f;

	/** Kopf der Figur. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Figur")
	UStaticMeshComponent* HeadMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fuss")
	USpringArmComponent* CameraArm = nullptr;

	/** Handlampe - schaltet sich in der Daemmerung selbst ein. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Figur")
	USpotLightComponent* Torch = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fuss")
	UCameraComponent* Camera = nullptr;

private:
	/** Setzt Rumpf, Kopf und Materialien - braucht eine laufende Welt. */
	void BuildBody();

	/** Gibt einen Schuss ab (zielt aus der Kamera). */
	void FireWeapon();

	/** Haelt die Figur auf dem Boden. */
	void FollowGround(float DeltaSeconds);

	/**
	 * Versucht, ein blockierendes Hindernis hinaufzusteigen.
	 *
	 * @return True, wenn die Figur versetzt wurde.
	 */
	bool TryStepUp(const FVector& Wanted, const FHitResult& Blocked);

	/** Beginnt einen Saegehieb (Bewegung, Klang, Trefferfenster). */
	void StartSwing();

	/** Fuehrt den Treffer des laufenden Hiebs aus (Kugel-Sweep nach vorn). */
	void DoMeleeHit();

	/** Waehlt Idle oder Walk nach dem tatsaechlichen Tempo. */
	void UpdateFigure(float DeltaSeconds, float SpeedMps);

	/** Restzeit bis zum naechsten moeglichen Schuss. */
	float FireCooldownSeconds = 0.0f;

	/** True, solange die Feuertaste gehalten wird. */
	bool bFireKeyHeld = false;

	/** True, wenn die animierte Kettensaegen-Figur aktiv ist. */
	bool bUsesChainsaw = false;

	/** Senkrechte Geschwindigkeit in cm/s (positiv = aufwaerts). */
	float VerticalSpeedCmS = 0.0f;

	/** True, solange die Figur nicht auf dem Boden steht. */
	bool bAirborne = false;

	/** Flankenerkennung der Sprungtaste. */
	bool bJumpKeyHeld = false;

	/** True, solange der Spieler in der Nerobergbahn mitfaehrt. */
	bool bRiding = false;

	/** Restzeit des laufenden Saegehiebs; 0 = kein Hieb. */
	float SwingRemaining = 0.0f;

	/** Treffer dieses Hiebs bereits ausgefuehrt? */
	bool bMeleeHitDone = false;

	/** Bewegungen der Figur. */
	UPROPERTY(Transient) UAnimSequence* IdleAnim = nullptr;
	UPROPERTY(Transient) UAnimSequence* WalkAnim = nullptr;
	UPROPERTY(Transient) UAnimSequence* SwingAnim = nullptr;

	/** Welche Dauerschleife gerade laeuft (0 = keine, 1 = Idle, 2 = Walk). */
	int32 CurrentLoop = 0;

	/** Standort im letzten Bild - fuer das gemessene Tempo. */
	FVector PreviousLocation = FVector::ZeroVector;
};
