// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "WiesbadenVehicleTestHarness.generated.h"

class AWiesbadenHelicopter;

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

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	AWiesbadenHelicopter* Heli() const;

	// Gier- und Flugprofil laufen UNABHAENGIG (und ggf. gleichzeitig), wie im
	// urspruenglichen Entwurf: verschiedene Achsen, ein gemeinsamer Steuerbefehl
	// je Bild. So kann der Rauchtest beide in einer Sitzung nachweisen.
	float YawElapsed = 0.0f;
	float YawDuration = 0.0f;   // 0 = inaktiv
	int32 YawLastSecond = -1;

	float FlyElapsed = 0.0f;
	float FlyDuration = 0.0f;   // 0 = inaktiv
	int32 FlyLastSecond = -1;
};
