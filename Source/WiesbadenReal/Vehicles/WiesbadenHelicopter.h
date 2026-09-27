// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"

#include "Vehicles/WiesbadenHeliGunComponent.h"
#include "Vehicles/WiesbadenHelicopterAudioComponent.h"
#include "Vehicles/WiesbadenHeliLightRig.h"
#include "Vehicles/WiesbadenRotorPhysics.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
#include "Vehicles/WiesbadenVehicleControl.h"

#include "WiesbadenHelicopter.generated.h"

class APlayerController;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

// FWiesbadenHeliControl liegt jetzt im neutralen Steuernaht-Header
// WiesbadenVehicleControl.h (gemeinsame Interface-Familie), von hier mit-inkludiert.

/**
 * Fliegbarer Helikopter-Pawn mit Platzhalter-Geometrie (Engine-Basis-Shapes).
 *
 * Die Rotor-Physik (Lift, Collective, Zyklik, Heckrotor, Autorotation) kommt
 * aus dem reinen Modul FWiesbadenRotorPhysics; der Heli integriert nur noch
 * Kraft -> Beschleunigung und Drehmoment -> Winkelbeschleunigung. Die Kamera
 * (Follow/Orbit/Cockpit) kommt aus UWiesbadenVehicleCameraComponent.
 *
 * Steuerung (zero-config, Tasten werden gepollt, keine Input-Assets noetig):
 *  - W/S = Pitch (Nase runter/hoch), A/D = Roll, Q/E = Yaw (Heckrotor)
 *  - Space/Shift = Collective hoch (steigen), Ctrl = Collective runter (sinken)
 *  - G = Triebwerk an/aus (Autorotation testbar)
 *
 * Kamera: Umsehen per Maus/Gamepad-Rechtsstick (Freilook im Follow-Modus),
 * Umschalten Follow -> Orbit -> Cockpit per C. Der Horizont bleibt ruhig
 * (bLevelHorizon), der Rumpf neigt sich im Bild statt das Bild mitzukippen.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenHelicopter : public APawn, public IWiesbadenHeliControl
{
	GENERATED_BODY()

public:
	AWiesbadenHelicopter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	/**
	 * Schaden annehmen - mit Folgen fuer den Flug.
	 *
	 * Der Heli hatte bisher kein Leben: Treffer von Fahrzeugen, Fussgaengern
	 * oder dem eigenen Geschuetz wurden empfangen und ignoriert. Damit war
	 * "MG-Bordgeschuetz" eine Attrappe, und die Zerstoerung, nach der der
	 * Auftrag einen Respawn auf dem Helipad verlangt, hatte keinen Ausloeser.
	 *
	 * Bei 0 Punkten geht der Hubschrauber in den Absturz: Triebwerk aus,
	 * Steuerung weg, Rotoren stehen, Licht aus, Rumpf taumelt, und nach
	 * RespawnDelay steht er wieder auf dem Landeplatz des Sebbotower.
	 */
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	/** Trefferpunkte. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Schaden")
	float GetHealth() const { return Health; }

	/** Trefferpunkte anteilig (0 = zerstoert, 1 = unversehrt). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Schaden")
	float GetHealthFraction() const;

	/** Zerstoert? Dann fliegt er nicht mehr. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Schaden")
	bool IsDestroyed() const { return bDestroyed; }

	/**
	 * Sofort wieder auf dem Landeplatz des Sebbotower aufsetzen.
	 *
	 * Auch ohne Zerstoerung aufrufbar: damit laesst sich der Anflug pruefen,
	 * ohne den Hubschrauber erst abschiessen zu muessen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|Schaden")
	bool RespawnOnTowerHelipad();

	/**
	 * Stellt den Hubschrauber DistanzMeter vor einen Zielpunkt und peilt ihn an.
	 *
	 * Aufnahmewerkzeug, kein Spielverhalten: damit zeigt die Muendung auf ein
	 * bestimmtes Bauwerk (am 26.09.2026 ein Zeltdach), statt nur "nach vorn".
	 * Die Höhe kommt über eine Bodenspur - ein geratenes Z landete sonst im
	 * Erdreich oder in der Luft.
	 *
	 * X/Y = Weltkoordinaten des Ziels (cm), HoeheUeberBodenCm = Zielpunkt über
	 * dem dortigen Boden (First des Dachs), DistanzMeter = Abstand der
	 * Schwebeposition. Der Abflug erfolgt aus Sueden, damit die Nase nach Norden
	 * zeigt und die Kamera dahinter die Muendung vor dem Ziel sieht.
	 */
	bool AimAtWorldTarget(float Xcm, float Ycm, float HoeheUeberBodenCm,
		float DistanzMeter);

	/** Sekunden bis zum Wiederaufsetzen nach der Zerstoerung (-1 = keins). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Schaden")
	float GetRespawnCountdown() const { return RespawnCountdown; }

	/** Geraet: Lichtbastel (Positionslichter, Strobe, Landeslicht, 2 Scheinwerfer). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Geraet")
	UWiesbadenHeliLightRig* GetLightRig() const { return LightRig; }

	/** Geraet: Bordgeschuetz. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Geraet")
	UWiesbadenHeliGunComponent* GetGun() const { return Gun; }

	/** Rumpfgehaeuse (traegt Modelldrehung, Massstab und Lage). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Geraet")
	UStaticMeshComponent* GetFuselageMesh() const { return FuselageMesh; }

	/** Kabinen-Innenraum: haengt am Rumpf, damit er dessen Drehung erbt. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Geraet")
	UStaticMeshComponent* GetCockpitMesh() const { return CockpitMesh; }


	/**
	 * Fahrzeugkamera. Sie haengt am SceneRoot, NICHT am Rumpf - darum wird
	 * ihr CockpitOffset im Actorraum addiert. Der Test braucht sie, um den
	 * Augpunkt gegen die Kabine zu messen.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Geraet")
	UWiesbadenVehicleCameraComponent* GetVehicleCamera() const { return VehicleCamera; }

	/** Flugsound-Komponente (Rotor, Triebwerk, Wind). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Geraet")
	UWiesbadenHelicopterAudioComponent* GetHelicopterAudio() const { return HelicopterAudio; }

	/** Nabe des oberen Koaxialrotors. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Geraet")
	USceneComponent* GetMainRotorHub() const { return MainRotorHub; }

	/** Radscheibe des oberen Koaxialrotors (traegt den Achsversatz). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Geraet")
	UStaticMeshComponent* GetMainRotorBlade() const { return MainRotorBlade; }

	/** Radscheibe des unteren Koaxialrotors. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Geraet")
	UStaticMeshComponent* GetLowerRotorBlade() const { return LowerRotorBlade; }

	/** Nabe des unteren Koaxialrotors. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Geraet")
	USceneComponent* GetLowerRotorHub() const { return LowerRotorHub; }

	/** Suchscheinwerfer schalten. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|Geraet")
	void SetSearchlights(bool bOn);

	/** Landeslicht schalten. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|Geraet")
	void SetLandingLight(bool bOn);

	/** Umschalten der Kamera (Forward an die Fahrzeug-Kamera-Komponente). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli")
	void CycleCameraMode();

	/** Aktueller Kameramodus (Forward an die Fahrzeug-Kamera-Komponente). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli")
	EWiesbadenVehicleCameraMode GetCameraMode() const;

	/** Aktuelle Hauptrotor-Drehzahl (U/min). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli")
	float GetMainRotorRpm() const override;

	/**
	 * Mastachse/Rotormontage als Momentaufnahme (siehe FWiesbadenHeliMastSample).
	 *
	 * Der Heli kennt als einziger seine Naben- und Blatt-Komponenten; das
	 * Urteil (Versatz zu gross? Blaetter laufen nicht um die Stange?) faellt
	 * draussen - hier steht nur die Geometrie-Wahrheit.
	 */
	virtual FWiesbadenHeliMastSample SampleRotorMast() const override;

	/**
	 * Traegt der Heli das importierte Ka-52-Modell (statt der Wuerfel-Notloesung)?
	 *
	 * Das importierte Modell bringt seine eigenen PBR-Materialien mit. Die alte
	 * Zell-Tarnung (M_WbHelicopter) darf dann NICHT daruebergelegt werden, sonst
	 * sieht der neue Heli aus wie der alte - der GameMode fragt das vor dem
	 * Lackieren ab.
	 */
	bool HasImportedModel() const { return bImportedModel; }

	/** Durchmesser des oberen Rotorkreises in cm (aus der Geometrie gemessen). */
	double GetUpperRotorDiameterCm() const;

	/**
	 * Laenge des Rumpfes in cm (aus der Geometrie gemessen).
	 *
	 * Fuer den Standabstand zweier geparkter Maschinen: nebeneinander aufgestellt
	 * begrenzen entweder die Rotorkreise oder die Rumpflaengen den Abstand -
	 * beides kommt aus dem Mesh, nicht aus zweiten Zahlen.
	 */
	double GetNoseToTailCm() const;

	// -- Cockpit-Instrumente ----------------------------------------------
	// Telemetrie fuer die Cockpit-Anzeige (WiesbadenVehicleHUD). Bewusst als
	// einfache Abfragen aus dem bereits gefuehrten Flugzustand.

	/** Waagerechte Fluggeschwindigkeit in km/h. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	float GetAirspeedKmh() const;

	/** Gemeinsamer Familien-Readout (IWiesbadenExternalControl): fuer den Heli die
	 *  Fahrt/Airspeed. */
	virtual float GetSpeedKmh() const override { return GetAirspeedKmh(); }

	/** Steig-/Sinkrate in m/s (positiv = steigen). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	virtual float GetVerticalSpeedMs() const override;

	/** Hoehe ueber Grund in Metern (Strahl nach unten; Fallback: Welthoehe). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	virtual float GetAltitudeMeters() const override;

	/** Steuerkurs 0..360 Grad (aus dem Gier-Winkel des Rumpfes). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	virtual float GetHeadingDegrees() const override;

	/** Kollektiv-Blattverstellung, 0..1 (Hebelstellung). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	float GetCollective() const;

	/** Triebwerk laeuft? */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	bool IsEngineRunning() const { return bEngineRunning; }

	/** Momentane Gierrate in Grad/s (Telemetrie fuer KI/Test/Anzeige). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	virtual float GetYawRateDegPerSec() const override;

	/**
	 * Weltgeschwindigkeit in m/s aus der internen Integration.
	 *
	 * WICHTIG: Dieser Pawn bewegt sich kinematisch (AddActorWorldOffset), setzt
	 * keine ComponentVelocity und hat keine MovementComponent - `AActor::GetVelocity()`
	 * liefert daher 0. Fuer Regelung/KI MUSS dieser Getter genutzt werden, nicht
	 * GetVelocity(), sonst ist jede Geschwindigkeitsrueckfuehrung wirkungslos.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Instrumente")
	virtual FVector GetVelocityMetersPerSecond() const override { return Velocity * 0.01f; }

	// -- Externe Steuerung ------------------------------------------------
	// Sauberer Eingang, ueber den ein anderer Treiber (KI, Zwischensequenz,
	// Replay, Test-Harness) den Hubschrauber steuert - ueber die ECHTE
	// Rotorphysik, nicht per direkter Transformation. Solange aktiv,
	// ueberschreibt er Tastatur/Gamepad. So bleibt die Flugsimulation frei von
	// Test-/Skript-Code (die Choreografie liegt in UWiesbadenVehicleTestHarness).

	/** Geglaettete Steuerwerte setzen (aktiviert die externe Steuerung). */
	virtual void SetExternalControl(const FWiesbadenHeliControl& Control) override
	{
		ExternalControl = Control;
		bExternalControlActive = true;
	}

	/** Externe Steuerung abschalten - der Rumpf hoert wieder auf Tastatur/Gamepad. */
	virtual void ClearExternalControl() override { bExternalControlActive = false; }

	/** True, solange die externe Steuerung aktiv ist (Familien-Naht). */
	virtual bool IsExternalControlActive() const override { return bExternalControlActive; }

	/** Triebwerk laeuft; sonst arbeitet der Rotor nur ueber Autorotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Heli|Physik")
	/** Im Konstruktor gesetzt: importiertes Ka-52-Netz gebunden (eigene Materialien). */
	bool bImportedModel = false;

	bool bEngineRunning = false;

	// -- Fahrzeug-Integration (Kraft/Drehmoment -> Bewegung) --------------
	/** Traegheitsmoment des Rumpfes (Roll, Pitch, Yaw) in kg*m^2. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.01"))
	//
	// Traegheitsmomente eines 10-Tonners statt eines Ultraleichten. Roll um die
	// Laengsachse ist am kleinsten, Nicken um die Querachse am groessten - der
	// Rumpf ist 16 m lang und nur 2 m breit.
	FVector MomentOfInertiaKgM2 = FVector(12000.0f, 45000.0f, 40000.0f);

	/** Glattung der Steuereingaenge (hoeher = direkter). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.01"))
	float ControlResponse = 6.0f;

	// -- Steuergefuehl ----------------------------------------------------
	//
	// Die Steuerung las bisher nur Tasten: jede Eingabe war +1, -1 oder 0, und
	// eine einzige exponentielle Glaettung lag darueber. Fliegen per An/Aus,
	// mit gleicher Reaktion fuer Kollektiv, Nick, Roll und Gier - obwohl ein
	// Hubschrauber auf diesen Achsen voellig verschieden traege ist. Dazu kam,
	// dass die Lage ohne Eingabe stehen blieb: einmal schraeg, immer schraeg.

	/**
	 * Totzone der Analogsticks (Anteil des Vollausschlags).
	 *
	 * Ohne Totzone driftet der Hubschrauber, weil kein Stick exakt mittig
	 * ruht. 0,15 ist der uebliche Wert fuer Xbox-Sticks.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float StickDeadzone = 0.15f;

	/**
	 * Kruemmung der Stick-Kennlinie (0 = linear, 1 = stark).
	 *
	 * Mit Expo sind kleine Ausschlaege fein aufgeloest und der volle Ausschlag
	 * bleibt erreichbar - genau das, was Schweben ueberhaupt erst moeglich
	 * macht. Linear ist ein Hubschrauber kaum auf der Stelle zu halten.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StickExpo = 0.72f;   // mehr Expo: kleine Ausschlaege feiner, weniger nervoes

	/** Aufbau des zyklischen Ausschlags (Anteil je Sekunde). */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float CyclicRiseRate = 1.6f;   // langsamerer Aufbau: weniger abrupt/uebersteuernd

	/** Ruecklauf des zyklischen Ausschlags zur Mitte. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float CyclicReturnRate = 3.5f;

	/** Aufbau des Kollektivs - traeger, es haengt an der Blattverstellung. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float CollectiveRiseRate = 1.4f;

	/** Ruecklauf des Kollektivs. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float CollectiveReturnRate = 2.0f;

	/** Aufbau des Gierpedals - die schnellste Achse. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float YawRiseRate = 3.0f;

	/** Ruecklauf des Gierpedals. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.1"))
	float YawReturnRate = 5.0f;

	/**
	 * Staerke der Selbststabilisierung (Grad Ausschlag je Grad Schraeglage).
	 *
	 * Ohne sie bleibt die Lage stehen, sobald man loslaesst - der Hubschrauber
	 * kippt weiter in die zuletzt kommandierte Richtung, bis man gegensteuert.
	 * Das ist der Hauptgrund, aus dem sich eine Hubschraubersteuerung
	 * unbeherrschbar anfuehlt.
	 *
	 * Wirkt NUR, solange auf der Achse nichts kommandiert wird - wer bewusst
	 * schraeg fliegt, wird nicht gegen sich selbst arbeiten muessen.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0"))
	float AutoLevelStrength = 0.12f;   // deutlich staerkere Selbstnivellierung: kippt nicht mehr so leicht um

	/** Groesster Ausschlag, den die Selbststabilisierung allein erzeugt. */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AutoLevelMaxAuthority = 0.72f;

	/**
	 * Schwebehilfe: Daempfung der Vertikalgeschwindigkeit bei neutralem
	 * Kollektiv (1/s). Mit losgelassenem Hebel strebt der Hubschrauber
	 * gegen Schweben, statt jede Stoerung als Dauersinken fortzuschreiben -
	 * so arbeitet auch das Stabilisierungssystem des echten Ka-52.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0"))
	float HoverAssistStrength = 1.1f;

	/**
	 * Driftdaempfung: Abbau der waagerechten Geschwindigkeit bei mittigem
	 * zyklischen Stick (1/s).
	 *
	 * Das Gegenstueck zur Schwebehilfe. Ohne sie haelt der Hubschrauber zwar
	 * die Hoehe, rutscht aber nach jedem Kippen weiter, bis man exakt
	 * gegensteuert - der Grund fuer "kann kaum navigieren".
	 *
	 * Bewusst schwaecher als die Schwebehilfe: ein Hubschrauber soll sich
	 * vorwaerts noch traege anfuehlen, nur nicht unkontrollierbar.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0"))
	float DriftAssistStrength = 1.15f;   // weniger Nachrutschen nach dem Kippen

	/**
	 * Ratendaempfung um Quer- und Laengsachse ohne Knueppelausschlag (1/s).
	 * Loslassen laesst die Drehbewegung abklingen, statt sie als
	 * Restdrehung weiterlaufen zu lassen.
	 */
	UPROPERTY(EditAnywhere, Category = "Helikopter|Steuerung", meta = (ClampMin = "0.0"))
	float RateAssistStrength = 3.0f;   // mehr Ratendaempfung: Restdrehung klingt schneller ab

	/** Luftwiderstand des Rumpfes (pro Sekunde). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.0"))
	float LinearDrag = 0.35f;

	/** Hoechstgeschwindigkeit (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.0"))
	float MaxSpeedCmPerS = 8300.0f;   // 300 km/h, Ka-52-Hoechstgeschwindigkeit

	/** Schwerkraft (UE-Default 980 cm/s^2). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.0"))
	float GravityCmPerS2 = 980.0f;

	/** Minimaler Abstand zum Boden (weiche Boden-Kollision per Raycast). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Physik", meta = (ClampMin = "0.0"))
	float MinGroundClearanceCm = 40.0f;

	// -- Geraet: Licht, Scheinwerfer, Bordgeschuetz -------------------------

	/**
	 * Entfernung (cm), auf die das Bordgeschuetz zielt (200 m).
	 *
	 * Das Geschuetz bekommt einen Punkt in dieser Entfernung auf der Blick-
	 * achse, nicht den Blickwinkel selbst: bei 2 km Zieldistanz faellt der
	 * Zielpunkt in die Nase, und alle Winkel zwischen Muendung und Ziel
	 * liegen dann unter der Wahrnehmungsschwelle - der Turm schiene still.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Geraet", meta = (ClampMin = "1000.0"))
	float ZielDistanzCm = 20000.0f;

	/** Entfernung (cm) des Punktes, auf den die Suchscheinwerfer zeigen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Geraet", meta = (ClampMin = "100.0"))
	float LichtDistanzCm = 5000.0f;

	/** Trefferpunkte, bevor der Hubschrauber abstuerzt. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Schaden", meta = (ClampMin = "1.0"))
	float MaxHealth = 900.0f;

	/** Sekunden zwischen Absturz und Wiederaufsetzen auf dem Landeplatz. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Schaden", meta = (ClampMin = "0.0"))
	float RespawnDelay = 8.0f;

	/** Wie viele Grad je Sekunde der abgestuerzte Rumpf taumelt. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Schaden", meta = (ClampMin = "0.0"))
	float CrashTumbleDegPerSec = 74.0f;

	/** Wie schnell der Rumpf beim Absturz absinkt (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Schaden", meta = (ClampMin = "0.0"))
	float CrashSinkCmPerSec = 520.0f;

	/** Rotor-Physik-Modul (Lift, Collective, Zyklik, Heckrotor, Autorotation). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Rotor")
	FWiesbadenRotorPhysics RotorPhysics;

	/**
	 * Totzone anwenden und die Expo-Kennlinie auflegen.
	 *
	 * Datenrein und statisch, damit die Kennlinie ohne Welt pruefbar ist.
	 * Nach der Totzone wird auf den vollen Bereich gestreckt - sonst waere der
	 * Vollausschlag nicht mehr erreichbar.
	 */
	static float ApplyStickShaping(float RawAxis, float Deadzone, float Expo);

	/**
	 * Delta-Rotation der beiden Koaxialrotoren fuer einen Zeitschritt.
	 *
	 * Aus der Rechnung gemacht, damit der Test pruefen kann, was die
	 * Forderung sagt - gleicher Betrag, entgegengesetztes Vorzeichen. Die
	 * Naben selbst drehen ueber AddLocalRotation um ihre eigene
	 * Komponentenachse; dass diese Achse die Rotorstange ist, prueft
	 * Ka52Ausstattung an den VERTEXEN des Assets, nicht an
	 * Component-Positionen (die lagen auch dann bei 0,0,0, wenn die
	 * Scheibe 1,8 m daneben sitzt).
	 */
	static void ComputeCoaxialRotorRotation(
		float MainRotorRpm, float DeltaSeconds, FRotator& OutUpper, FRotator& OutLower);

	/**
	 * Versatz, mit dem ein Rotor-Component auf die Rotorstangenachse (0, 0)
	 * im Modellraum zu legen ist.
	 *
	 * DER EINZIGE Ort, an dem die Achse der beiden Koaxialrotoren gesetzt
	 * wird. Beide Scheiben laufen durch dieselbe Funktion; der Mesh-Drehpunkt
	 * ist gemessen (Tools/ka52_rotorachse.py, Saved/Diagnose/ka52/
	 * rotorachse.txt), der Versatz dreht ihn mit der Modelldrehung zurueck.
	 * Der Hub-Node bleibt unveraendert, ebenso der Gegenlauf und der
	 * Ho henabstand von 118,5 cm.
	 */
	static FVector ComputeRotorMountOffset(
		const FVector& MeshDrehpunktCm, const FRotator& ModelYaw, float HubHeightCm);

	/**
	 * Gemessener Drehpunkt einer Radscheibe im Modellraum des Assets, cm.
	 *
	 * BEWIESENE WERTE, keine Schaetzung. Quelle ist die Datei, aus der UE das
	 * Asset importiert hat (Content/Data/Raw/Ka52/ka52_ue.fbx, 208 009 bzw.
	 * 221 119 Vertex), gemessen mit Tools/ka52_rotorachse.py ueber die
	 * 3-fach-Rotationssymmetrie, Beleg in Saved/Diagnose/ka52/
	 * rotorachse_fbx.txt (Restfehler 4,2 bzw. 6,3 mm gegen 375 mm an der
	 * Kontrollstelle).
	 *
	 * Oeffentlich, weil der Automationstest dieselben Zahlen braucht: prueft
	 * man nur die Geometrie des Assets, misst man den Ersatzdatensatz (Nanite,
	 * 773 Dreiecke) und nicht das Flugmodell. Der Test vergleicht deshalb
	 * beides - die Rechnung exakt, die Geometrie mit der Aufloesung, die
	 * dieser Datensatz hergibt.
	 *
	 * @param bUnten true = untere Scheibe des Koaxialpaars.
	 */
	static FVector GetRotorDrehpunktCm(bool bUnten);

	/**
	 * Achse mit getrennten Raten fuer Aufbau und Ruecklauf nachfuehren.
	 *
	 * Ruecklauf heisst: Bewegung zur Mitte. Die Pruefung muss ausschliessen,
	 * dass die Achse bereits mittig steht - FMath::Sign(0) ist 0 und weicht
	 * damit von JEDEM Ziel ab. Genau dieser Fehler hat bei der Lenkung des
	 * Fahrzeugs dazu gefuehrt, dass aus der Mitte heraus mit der schnelleren
	 * Ruecklaufrate eingelenkt wurde.
	 */
	static float AdvanceControlAxis(
		float Current, float Target, float RiseRate, float ReturnRate, float DeltaSeconds);

	/**
	 * Zusaetzlicher Ausschlag, der den Hubschrauber aufrichtet.
	 *
	 * Wirkt nur, solange auf der Achse nichts kommandiert wird: Der Rueckgabe-
	 * wert wird mit (1 - |Eingabe|) gewichtet, damit bewusstes Schraegfliegen
	 * nicht gegen die Stabilisierung ankaempfen muss.
	 */
	static float ComputeAutoLevel(
		float AttitudeDegrees, float CommandedInput, float Strength, float MaxAuthority);


protected:
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	USceneComponent* SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	USphereComponent* CollisionSphere = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* FuselageMesh = nullptr;

	/**
	 * Sitzschalen, Pulte, Knueppel: die Kabine, die es im Rumpf-Asset nicht
	 * gibt (das Modell ist eine AUSSENansicht).
	 *
	 * Haengt am Rumpf und nicht am Szenenwurzel, damit Kabine, Rumpf und
	 * Rotoren sich Massstab und Gierdrehung teilen statt sie zu fuehren.
	 * Ausdruecklich NICHT bei AddCockpitHiddenMesh: die Kamera blendet den
	 * Rumpf aus, um in die Kabine sehen zu koennen - die Kabine selbst
	 * muss dabei sichtbar bleiben, sonst sitzt der Pilot im Nichts.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* CockpitMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* TailBoomMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* TailFinMesh = nullptr;

	/** Rotor-Nabe des Hauptrotors (Mast ueber dem Schwerpunkt). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	USceneComponent* MainRotorHub = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* MainRotorBlade = nullptr;

	/** Unterer Hauptrotor des Koaxial-Paars (gegenlaeufig, Ka-52-Stil). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	USceneComponent* LowerRotorHub = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* LowerRotorBlade = nullptr;

	/** Rotor-Nabe des Heckrotors (Ende des Heckauslegers). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	USceneComponent* TailRotorHub = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UStaticMeshComponent* TailRotorBlade = nullptr;

	/** Durchscheinende Rotor-Blur-Scheiben (blenden mit der Drehzahl ein, waehrend
	 *  die soliden Blaetter ausblenden). Je eine pro Koaxialrotor. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|FX")
	UStaticMeshComponent* UpperRotorBlur = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|FX")
	UStaticMeshComponent* LowerRotorBlur = nullptr;

	/** Staub-/Downwash-Scheibe am Boden (zieht bei Bodennaehe + Rotorschub auf). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|FX")
	UStaticMeshComponent* GroundDust = nullptr;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* RotorBlurMID = nullptr;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* DownwashMID = nullptr;

	/** Rotor-Blur-Scheiben und Downwash-Staub aus Drehzahl/Bodennaehe treiben. */
	void UpdateVisualEffects(float DeltaSeconds);

	/** Phase fuer das leichte Pulsieren der Staubscheibe. */
	float DustPhase = 0.0f;

	/**
	 * Lichtbastel: Positionslichter, Stroboskop, Landlicht, 2 Suchscheinwerfer.
	 *
	 * Sie traegt dieselbe Modelldrehung wie Rumpf und Rotoren (SetModel-
	 * Transform im Konstruktor). Ohne diese Uebergabe saeßen die Leuchten an
	 * einer anderen Stelle als die Koerper, die sie beleuchten sollen.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Geraet")
	UWiesbadenHeliLightRig* LightRig = nullptr;

	/** Bordgeschuetz (30 mm) auf dem Steuerbord-Pylon. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Geraet")
	UWiesbadenHeliGunComponent* Gun = nullptr;

	/** Generische Fahrzeug-Kamera (Follow/Orbit/Cockpit). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UWiesbadenVehicleCameraComponent* VehicleCamera = nullptr;

	/** Flugsound (Rotor-/Motor-Assets oder prozeduraler Fallback). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli")
	UWiesbadenHelicopterAudioComponent* HelicopterAudio = nullptr;

private:
	void ReadInput(float DeltaSeconds);
	void ApplyFlightPhysics(float DeltaSeconds);
	void ApplyGroundConstraint(float DeltaSeconds);

	/**
	 * Haelt den Helikopter am Boden, solange niemand ihn fliegt.
	 *
	 * Ohne diesen Zustand lief die volle Flugsimulation auch fuer den
	 * abgestellten Helikopter weiter - bei laufendem Triebwerk und 50 %
	 * Kollektiv stieg er von selbst davon. Gemessen stand er nach acht
	 * Sekunden 126 m ueber Grund und war vom Startplatz aus nicht mehr zu
	 * sehen.
	 */
	/**
	 * Eingaben fuer Geraet und Waffe auslesen (getrennt vom Flug, damit
	 * die Steuerung nicht in einem Block von 200 Zeilen verschwindet).
	 */
	void ReadDeviceInput(float DeltaSeconds);

	/** Absturz: Rumpf taumeln, sinken, Rotation stehen lassen. */
	void UpdateCrash(float DeltaSeconds);

	/** Wiederaufsetzen: Ort suchen, setzen, Zustand zuruecksetzen. */
	bool PlaceOnTowerHelipad();

	void ParkOnGround();
	void UpdateRotors(float DeltaSeconds);
	void UpdateAudio(float DeltaSeconds);

	bool IsKeyDown(const FKey& Key);

	/**
	 * Meldet den Zustand der Kabinenhuelle, sobald sich der Kameramodus
	 * aendert. Am 26.09.2026 war im Cockpitbild keine Kabine zu sehen, ohne
	 * dass das Log eine Ursache nannte - "nie gezeichnet" und "an anderer
	 * Stelle" sehen im Bild gleich aus.
	 */
	void MeldeKabine();

	/** Merker fuer MeldeKabine: letzter gemeldeter Kameramodus. */
	EWiesbadenVehicleCameraMode KameraModusMerker = EWiesbadenVehicleCameraMode::Follow;


	/** Analogwert einer Achse (Gamepad-Stick oder Trigger), 0 ohne Controller. */
	float GetAnalogAxis(const FKey& Key);
	APlayerController* GetHeliController();

	// Geglaettete Steuereingaenge (-1..1).
	float CollectiveInput = 0.0f;
	float CyclicPitchInput = 0.0f;
	float CyclicRollInput = 0.0f;
	float YawInput = 0.0f;

	// Lokale Winkelgeschwindigkeit (Roll, Pitch, Yaw) in rad/s.
	FVector AngularVelocity = FVector::ZeroVector;

	// Weltgeschwindigkeit in cm/s.
	FVector Velocity = FVector::ZeroVector;

	bool bEngineToggleHeld = false;
	bool bGrounded = false;

	// -- Schaden -------------------------------------------------------------
	// Health steht hier und nicht im Rotorphysik-Modul: das Modul rechnet
	// Flug, der Pawn weiss, wann er tot ist.
	float Health = 900.0f;
	bool bDestroyed = false;
	float RespawnCountdown = -1.0f;
	float CrashYawRate = 0.0f;
	float CrashRollRate = 0.0f;

	// Geraet: Flanken, damit ein gehaltener Schalter nicht im Frame
	// mehrfach umschaltet.
	bool bSearchlightToggleHeld = false;
	bool bLandingLightToggleHeld = false;
	bool bTriggerHeld = false;

	// Boden-Cache: ApplyGroundConstraint fuellt ihn einmal pro Frame; der
	// visuelle Pfad (GetAltitudeMeters, Downwash-Staub) liest ihn, statt eigene
	// Down-Traces zu schiessen -> ein Boden-Raycast pro Frame statt drei bis vier.
	bool bGroundCacheValid = false;
	float CachedGroundZ = 0.0f;

	/** Gecachter Pilot-Controller (in PossessedBy gesetzt) - spart ~17 Casts/Frame. */
	UPROPERTY(Transient)
	APlayerController* CachedPlayerController = nullptr;

	/** Externe Steuerung (KI/Zwischensequenz/Test), siehe SetExternalControl. */
	FWiesbadenHeliControl ExternalControl;
	bool bExternalControlActive = false;
};
