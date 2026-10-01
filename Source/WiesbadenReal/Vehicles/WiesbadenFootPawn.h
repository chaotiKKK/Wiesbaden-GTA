// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Audio/WiesbadenAudioZones.h"
#include "WiesbadenFootPawn.generated.h"

class UCameraComponent;
class UCapsuleComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UWiesbadenCarAudioComponent;
class UWiesbadenSebboFigureComponent;
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
	void SetRiding(bool bInRiding);

	/** Faehrt der Spieler gerade mit? Seit es ZWEI Bus-Actoren gibt (Linie 6 und
	 *  Linie 3), muss der Einstieg fragen, ob schon jemand den Fahrgast hat:
	 *  beide Actors sehen denselben Tastendruck und haetten sich sonst beide
	 *  denselben Pawn angehaengt (jeder mit eigenem Anker). */
	bool IsRiding() const { return bRiding; }

	/** Geduckt? (Taste X / rechter Stick gedrueckt; bleibt unter niedriger Decke.) */
	bool IsCrouched() const { return bCrouched; }

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

	/** Tempo geduckt in km/h (kein Sprint, kein Sprung). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "0.5"))
	float CrouchSpeedKmh = 3.5f;

	/**
	 * Halbe Kapselhoehe geduckt in cm (stehend 90). Die Duck-Clips sind 1,25 bis
	 * 1,36 m hoch - 70 laesst den Kopf in der Kapsel.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fuss", meta = (ClampMin = "40.0"))
	float CrouchHalfHeightCm = 70.0f;

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

	/** Zeit zwischen zwei Schuessen in Sekunden (Feuerrate). Wird beim
	 *  Waffenwechsel aus der Tabelle gesetzt; der Wert hier ist der Rueckfall. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Waffe", meta = (ClampMin = "0.02"))
	float FireIntervalSeconds = 0.12f;

	/**
	 * Ego-Modus: C schaltet Schulterkamera <-> Erste-Person.
	 *
	 * In der Ego-Ansicht sitzt die Kamera auf Augenhoehe im Kopf, die Figur
	 * (Koerper/Kopf/Skelett) blendet sich fuer den Traeger aus, und die Waffe
	 * wandert in Kameranaehe - die Shooter-Ueblichkeit: Man sieht die Waffe,
	 * nicht den eigenen Ruecken.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Waffe", meta = (ClampMin = "30.0"))
	float EgoArmLengthCm = 0.0f;

	/** Schulter-Abstand der Kamera im Ego-Modus (leicht rechts versetzt). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Waffe", meta = (ClampMin = "0.0", ClampMax = "40.0"))
	float EgoShoulderOffsetCm = 18.0f;

	/** Armlaenge der Schulterkamera in cm (stand frueher fest 300 im Code). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Waffe", meta = (ClampMin = "0.0"))
	float ShoulderArmLengthCm = 300.0f;

	// -- Zielen (ADS) & Mausrad ---------------------------------------------

	/**
	 * Zoom-Stufe je Mausradklick im Zielmodus. Die OBERGRENZE steht pro
	 * Waffe in der Spec-Tabelle (AdsZoomMax): ein Scharfschuetzengewehr zoomt
	 * weiter als eine Schrotflinte - zwei Stellen sollen nicht ueber dieselbe
	 * Zahl bestimmen.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Waffe", meta = (ClampMin = "0.05"))
	float AdsZoomStep = 0.25f;

	/** Armlaenge im Zielmodus als Anteil der normalen (Kamera rueckt heran). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Waffe", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float AdsArmLengthScale = 0.55f;

	/** Zielt der Spieler gerade (rechte Maustaste gehalten)? (HUD, Pruefung.) */
	bool IsAiming() const { return bAiming; }

	/**
	 * Zoom-Stufe im Zielmodus (1 = kein Zoom). Pruef-Zugang: das D-Pad
	 * ist am Gamepad der Mausrad-Weg, und im Zielmodus entscheidet
	 * RouteMausrad genau auf diese Stufe. Ohne den Zugang laesst sich am
	 * laufenden Spiel nicht unterscheiden, ob das D-Pad den Zoom, die
	 * Waffenwahl oder gar nichts getroffen hat.
	 */
	float GetAdsZoomLevel() const { return AdsZoomLevel; }

	/** Waffe waehlen (Tasten 1-8 + Mausrad); rueckwaerts zaehlt als Abwahl. */
	void SelectWeapon(int32 Index);

	/** Schaltet Schulter-/Ego-Ansicht um (Taste C, Flankenerkennung). */
	void ToggleEgoCamera();

	/** Ansicht abfragen/setzen (Dev-Exec, HUD); Setzen wendet sofort an. */
	bool IsEgoCamera() const { return bEgoCamera; }

	/** Laeuft gerade der Kettensaege-Modus (Slot 9)? (Pruef-Lauf, HUD.) */
	bool IsUsingChainsaw() const { return bUsesChainsaw; }

	void SetEgoCamera(bool bInEgo)
	{
		if (bInEgo != bEgoCamera)
		{
			bEgoCamera = bInEgo;
			ApplyCameraMode();
		}
	}

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
	 * Die animierte Spielfigur (SK_Sebbo) - besitzt Clips und Clip-Wahl; der
	 * Pawn meldet ihr nur Tempo, Luft, Mitfahrt, Blick und Gesundheit. Das
	 * statische BodyMesh bleibt als Rueckfall.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Figur")
	UWiesbadenSebboFigureComponent* FigureMesh = nullptr;

	/** Kettensaegen-Klang: der Fahrzeug-Synthesizer als Zweitakter. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Figur")
	UWiesbadenCarAudioComponent* SawAudio = nullptr;

	/** Dauer eines Nahkampfhiebs in Sekunden (mit A_Sebbo_Kick: dessen Laenge). */
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

	/** Uebernimmt Kameraposition, Sichtbarkeiten und Waffenlage je Modus. */
	void ApplyCameraMode();

	/** Tasten 1-8 abfragen und Waffe umschalten (Flanken je Taste). */
	void PollWeaponKeys(const APlayerController* PC);

	/**
	 * Zielen und Mausrad: rechte Maustaste gehalten = ADS (Zoom, halbe
	 * Streuung); im Zielmodus zoomt das Mausrad, sonst wechselt es die Waffe.
	 */
	void PollAimAndWheel(const APlayerController* PC);

	/** Wendet den Zielzustand an: Kamera-Zoom, Armlaenge, Streuung. */
	void ApplyAimState();

	/** Haelt die Figur auf dem Boden. */
	void FollowGround(float DeltaSeconds);

	/**
	 * Ducken/Aufstehen: Kapsel kuerzen bzw. verlaengern, die Fuesse bleiben am
	 * Boden (Actor sinkt/steigt um die Differenz, die Figur rueckt nach).
	 */
	void SetCrouched(bool bInCrouched);

	/** Ist ueber der geduckten Kapsel Platz zum Aufstehen? */
	bool HasRoomToStand() const;

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

	/** Meldet der Figur Tempo, Luft, Mitfahrt, Blick und Gesundheit. */
	void UpdateFigure(float DeltaSeconds, float SpeedMps);

	/**
	 * Fussschritte: Schrittlaenge aus dem gemessenen Tempo, Untergrund per
	 * Materialabfrage am Fuss. Getrennt von UpdateFigure, weil der Schritt
	 * nichts mit der Animation zu tun hat und eigene Grenzen hat.
	 */
	void UpdateFootsteps(float DeltaSeconds, float SpeedMps);

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

	/** Flanken der Gamepad-Schultertasten (Waffenwechsel RB/LB). */
	bool bWaffeVorHeld = false;
	bool bWaffeZurueckHeld = false;

	/** Flanken des D-Pads hoch/runter (Klicks wie das Mausrad). */
	bool bPadUpHeld = false;
	bool bPadDownHeld = false;

	/** Bodenabfrage: Ort nach der letzten und Restzeit der Versetz-Schonfrist. */
	FVector LastGroundCheckLocation = FVector(0.0, 0.0, -1e9);
	float TeleportGraceSeconds = 0.0f;

	/** Geduckt? Und die halbe Kapselhoehe im Stehen (aus dem Konstruktor). */
	bool bCrouched = false;
	float StandingHalfHeightCm = 90.0f;

	/** True, solange der Spieler in der Nerobergbahn mitfaehrt. */
	bool bRiding = false;

	/** Ego-Modus aktiv (C umgeschaltet)? Start: Schulterkamera wie bisher. */
	bool bEgoCamera = false;

	/** Flankenerkennung der C-Taste. */
	bool bEgoKeyHeld = false;

	/** Zuletzt gehaltene Zifferntasten 1-8 (Flanken je Taste). */
	bool WeaponKeyHeld[8] = {};

	/** Grund-FOV der Kamera in Grad - gemerkt beim Start, ADS teilt es. */
	float BaseCameraFOV = 90.0f;

	/** Aktueller Zoomfaktor im Zielmodus (1.0 = kein Zoom). */
	float AdsZoomLevel = 1.0f;

	/** Zielmodus aktiv (rechte Maustaste gehalten)? */
	bool bAiming = false;

	/** Angehaeuftes Mausrad-Signal: die Achse meldet ein Delta je Bild. */
	float WheelAccumulator = 0.0f;

	/** Restzeit des laufenden Saegehiebs; 0 = kein Hieb. */
	float SwingRemaining = 0.0f;

	/** Treffer dieses Hiebs bereits ausgefuehrt? */
	bool bMeleeHitDone = false;

	/** Standort im letzten Bild - fuer das gemessene Tempo. */
	FVector PreviousLocation = FVector::ZeroVector;

	/** Seit dem letzten Schritt zurueckgelegte Strecke (cm). */
	float StepDistanceAccumulatedCm = 0.0f;

	/** Untergrund des letzten Schritts - nur fuer den Laufbeleg. */
	EWbFootstepSurface LastFootstepSurface = EWbFootstepSurface::Pflaster;
};
