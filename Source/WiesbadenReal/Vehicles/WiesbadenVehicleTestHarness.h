// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "WiesbadenVehicleTestHarness.generated.h"

class IWiesbadenHeliControl;
class IWiesbadenVehicleControl;
struct FWiesbadenHeliMastSample;

/**
 * Dev-Test-Harness: fuehrt ein Fahrzeug ueber seinen NORMALEN Steuereingang
 * durch ein Skript-Profil und protokolliert Telemetrie - damit sich Funktionen
 * (Gieren, Steig-/Sinkflug) im echten Fenster ohne Tastatur nachweisen lassen.
 *
 * Bewusst KEIN Teil der Fahrzeugklasse: Die Flugsimulation bleibt frei von
 * Test-/Skript-Code, die Choreografie lebt hier. Die Komponente wird zur
 * Laufzeit von den Dev-Konsolenbefehlen (WbHeliYaw/WbHeliFly) auf dem
 * besessenen Pawn angelegt - im normalen Spiel existiert sie also gar nicht.
 * Sie treibt den Hubschrauber ueber SetExternalControl an (echte Rotorphysik),
 * nicht per direkter Transformation.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenVehicleTestHarness : public UActorComponent
{
	GENERATED_BODY()

public:
	UWiesbadenVehicleTestHarness();

	/** Gierprobe: stetiges Gierpedal + etwas Kollektiv fuer <Seconds> Sekunden. */
	void StartYawProbe(float Seconds);

	/** Flugprofil: Steigen -> Schweben -> Marsch -> Sinken ueber <Seconds> Sekunden. */
	void StartFlightProfile(float Seconds);

	/** Fahrprofil: Vollgas geradeaus, dann Lenk-Sweep ueber <Seconds> Sekunden (Fahrzeug). */
	void StartDriveProfile(float Seconds);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Beide Zweige sprechen ueber die Steuernaht-FAMILIE (Interface), nicht ueber
	// konkrete Klassen: das Flug-/Gierprofil den Heli, das Fahrprofil das Fahrzeug.
	IWiesbadenHeliControl* HeliControl() const;
	IWiesbadenVehicleControl* VehicleControl() const;

	// Ein Harness lebt auf EINEM Pawn: Gier-/Flugprofil treiben einen Helikopter,
	// das Fahrprofil ein Fahrzeug. Beide Zweige pruefen ihren Owner-Typ selbst.
	void TickHeliProfiles(float DeltaTime);
	void TickDriveProfile(float DeltaTime);

	// In-Flight-Telemetrie der Flugprobe, zwei Messgruppen:
	//  - ROTOR/MASTACHSE: je Bild einsammeln (Maxima der Versaetze, Drehrate aus
	//    den Drehlagen), je Sekunde EINE Zeile. Nur so faellt auf, ob die Blaetter
	//    im Flug um die Stange laufen oder um einen Punkt daneben.
	//  - KAMERA: Modus + die wirklich gerenderte Blicklage (PlayerCameraManager),
	//    damit "Kamera folgt, Horizont bleibt ruhig" belegbar wird.
	void AccumulateHeliTelemetry(float DeltaTime, const FWiesbadenHeliMastSample& Sample);
	void LogHeliTelemetry(const IWiesbadenHeliControl& Heli, float ElapsedSeconds);

	// Gier- und Flugprofil laufen UNABHAENGIG (und ggf. gleichzeitig), wie im
	// urspruenglichen Entwurf: verschiedene Achsen, ein gemeinsamer Steuerbefehl
	// je Bild. So kann der Rauchtest beide in einer Sitzung nachweisen.
	float YawElapsed = 0.0f;
	float YawDuration = 0.0f;   // 0 = inaktiv
	int32 YawLastSecond = -1;

	float FlyElapsed = 0.0f;
	float FlyDuration = 0.0f;   // 0 = inaktiv
	int32 FlyLastSecond = -1;

	// Fahrprofil (Fahrzeug): Startkurs merken, damit die Kursaenderung wrap-sicher
	// (ueber +-180 Grad) gemessen wird - so beweist der Rauchtest die Lenkung.
	float DriveElapsed = 0.0f;
	float DriveDuration = 0.0f;   // 0 = inaktiv
	int32 DriveLastSecond = -1;
	float DriveStartYaw = 0.0f;
	bool bDriveReverse = false; // -WbDriveReverse: gleiches Manoever im Rueckwaertsgang.
	bool bDriveCornerBrake = false; // -WbDriveKurvenbremsung: Bremsphase mit gehaltener Linkslenkung.

	// -- Mast-/Kamera-Telemetrie der laufenden Sekunde ---------------------
	// Summen/Maxima werden je Sekunde geloggt und dann zurueckgesetzt.
	float TelMainRateSum = 0.0f;   // aufsummierte Drehung oben (Grad)
	float TelLowerRateSum = 0.0f;  // aufsummierte Drehung unten (Grad)
	float TelRateDt = 0.0f;        // Framezeit der BRAUCHBAREN Bilder (s)
	int32 TelRateFrames = 0;
	int32 TelRateSkipped = 0;      // Bilder mit Riesenschritt (Streaming-Hitch) - verworfen
	float TelMaxMainHubOffsetCm = 0.0f;
	float TelMaxLowerHubOffsetCm = 0.0f;
	float TelMaxMainBladeOffsetCm = 0.0f;
	float TelMaxLowerBladeOffsetCm = 0.0f;

	/** Groesster Seitenabstand des gedrehten Scheiben-Drehpunkts von der Stange (cm), muss 0 sein. */
	float TelMaxMainAxisResidualCm = 0.0f;
	float TelMaxLowerAxisResidualCm = 0.0f;
	// Blattstern-Mitte (Rumpf-Frame): Summe ueber die Bilder, in denen sich der
	// Stern WIRKLICH gedreht hat - erst der Mittelwert ist die Sternmitte.
	FVector TelMainBladeCentreSum = FVector::ZeroVector;
	FVector TelLowerBladeCentreSum = FVector::ZeroVector;
	int32 TelBladeCentreSamples = 0;
	float TelMaxMastTiltDeg = 0.0f;
	float TelMaxMainSpinTiltDeg = 0.0f;
	float TelMaxLowerSpinTiltDeg = 0.0f;
	// Die Drehlage ist ein ZUSTAND: sie wird an der Sekundengrenze NICHT
	// zurueckgesetzt, sonst entstuende dort ein Schein-Riesenschritt.
	bool bTelHasPrevAzimuth = false;
	float TelPrevMainAzimuth = 0.0f;
	float TelPrevLowerAzimuth = 0.0f;
};
