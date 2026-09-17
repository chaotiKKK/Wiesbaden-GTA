// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/WiesbadenBusLine.h"
#include "WiesbadenBusStopMonitor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UGeoCoordinateConverter;
class UMaterialInterface;

/**
 * Dynamischer Abfahrtsmonitor (DFI-Saeule) an ausgewaehlten Linie-6-Halten.
 *
 * Ein Manager-Actor stellt an mehreren Halten je eine Saeule mit dunklem Panel
 * auf und zeigt darauf per UTextRenderComponent LIVE die naechsten Abfahrten der
 * Linie 6 (Richtung Mainz-Gonsenheim). Die Zeiten kommen aus demselben echten
 * Fahrplan wie die Busse: am Terminus die Abfahrtsminute, an Zwischenhalten die
 * Durchfahrtszeit (Abfahrt + Fahrzeit bis zur Halte, WiesbadenBusLine).
 * Sichtbar-only, deterministisch aus der Dienstzeit.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenBusStopMonitor : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenBusStopMonitor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Liniendatei unter Data/Raw/Bus/ (path + stops). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor")
	FString LineFile = TEXT("line6.json");

	/** Fahrplandatei unter Data/Raw/Bus/ (Abfahrten ab Terminus). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor")
	FString ScheduleFile = TEXT("line6_schedule.json");

	/** Muss zu den Bus-Werten passen (fuer die Durchfahrtszeiten). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "1.0"))
	float SpeedKmh = 32.0f;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "0.0"))
	float StopDwellSeconds = 8.0f;

	/** Dienst-Uhrzeit beim Spielstart (wie beim Bus). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float ServiceStartHour = 7.0f;

	/** Wie viele naechste Abfahrten je Halte anzeigen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "1"))
	int32 DisplayRows = 4;

	/** Fahrtziel-Text auf dem Monitor (Richtung Mainz). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor")
	FString Destination = TEXT("Mainz-Gonsenheim");

	/** Seitlicher Versatz der Saeule von der Trasse nach rechts (auf den Gehweg), cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "0.0"))
	float SidewalkOffsetCm = 480.0f;

private:
	void LoadLine();
	void LoadSchedule();
	void BuildWorldPath();
	bool ResolveGround(double X, double Y, double& OutZ) const;
	void BuildMonitors();

	struct FMonitor
	{
		int32 StopIndex = 0;
		FString Name;
		double OffsetSeconds = 0.0;   // Fahrzeit ab Terminus bis zu dieser Halte
		UTextRenderComponent* Text = nullptr;
	};
	TArray<FMonitor> Monitors;

	UPROPERTY(Transient) USceneComponent* Root = nullptr;
	UPROPERTY(Transient) UGeoCoordinateConverter* Converter = nullptr;
	UPROPERTY(Transient) UStaticMesh* PoleMesh = nullptr;
	UPROPERTY(Transient) UStaticMesh* PanelMesh = nullptr;
	UPROPERTY(Transient) UMaterialInterface* PoleMat = nullptr;
	UPROPERTY(Transient) UMaterialInterface* PanelMat = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Parts;
	UPROPERTY(Transient) TArray<UTextRenderComponent*> Texts;

	TArray<FVector2D> GeoPath;
	TArray<FVector2D> GeoStops;
	TArray<FVector> WorldPath;
	TArray<double> ArcCm;
	WiesbadenBusLine::FBusRoute Route;
	WiesbadenBusLine::FBusSchedule Schedule;

	double UpdateAccum = 1.0;   // beim ersten Tick sofort aktualisieren
	bool bReady = false;
};
