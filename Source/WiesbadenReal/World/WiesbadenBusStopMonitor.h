// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/WiesbadenBusLine.h"
#include "World/WiesbadenBusLineFile.h"
#include "WiesbadenBusStopMonitor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UGeoCoordinateConverter;
class UMaterialInterface;

/**
 * Dynamischer Abfahrtsmonitor (DFI-Saeule) an ausgewaehlten Halten einer Linie.
 *
 * Je gewaehltem Halt stehen ZWEI Saeulen - eine auf der Bordsteinkante jeder
 * Richtung, wie im Vorbild, wo die Gegenfahrbahn ihre eigene Tafel hat. Jede
 * Tafel zeigt per UTextRenderComponent LIVE die naechsten Durchfahrten IHRER
 * Richtung samt deren Ziel; die Gegenseite nennt also das andere Fahrziel und
 * andere Zeiten. Die Zeiten stammen aus demselben Dienst wie die Busse
 * (dieselbe Flotte, dieselben Phasen); die Durchfahrtszeit haengt an der
 * Fahrzeit vom Terminus der jeweiligen Richtung
 * (WiesbadenBusLine::SecondsToStopOnLeg).
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

	/** Wendezeit an den Endpunkten - muss zu den Bussen passen (Dauerbetrieb). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "0.0"))
	float TerminusDwellSeconds = 600.0f;

	/** Angestrebter Takt in Sekunden; 0 = aus der Liniendatei. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "0.0"))
	float HeadwaySeconds = 0.0f;

	/** Obergrenze der Wagen (muss wie beim Bus sein - sonst zeigt der Monitor
	 *  andere Zeiten an, als tatsaechlich gefahren wird). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "2"))
	int32 MaxBuses = 12;

	/** Dauerbetrieb (feste Wagen, Wendezeit an beiden Enden) - Standard wie beim
	 *  Bus. Aus: die Zeiten kommen aus dem echten Fahrplan (ScheduleFile). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor")
	bool bContinuousService = true;

	/** Dienst-Uhrzeit beim Spielstart (wie beim Bus). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float ServiceStartHour = 7.0f;

	/**
	 * Halte, an denen eine Saeule steht - als NAMEN aus der Liniendatei.
	 *
	 * Frueher waren es Indizes in line6.json (0, 1, 4, 6, 10). Seit die Linie 6 bis
	 * Mainz durchgebaut ist und 40 statt 19 Halte hat, zeigt jeder Index auf eine
	 * andere Halte: die Saeulen waeren an den falschen Strassen gestanden. Namen
	 * sind davon unabhaengig.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor")
	TArray<FString> MonitorStopNames;

	/** Wie viele naechste Abfahrten je Halte anzeigen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "1"))
	int32 DisplayRows = 4;

	/** Zieltext der Hinfahrtsseite; die Gegenseite zeigt `from` aus der Liniendatei. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor")
	FString Destination = TEXT("Mainz-Gonsenheim");

	/** Seitlicher Versatz der Saeule von der Trasse nach rechts (auf den Gehweg), cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "0.0"))
	float SidewalkOffsetCm = 480.0f;

private:
	/** Holt die Linie aus dem gemeinsamen Leser (World/WiesbadenBusLineFile) und
	 *  uebernimmt daraus Liniennummer, Ziel, Takt, Wendezeit und Monitorhalte. */
	void LoadLine();
	void LoadSchedule();
	bool ResolveGround(double X, double Y, double& OutZ) const;
	void BuildMonitors();
	/**
	 * Die Saeulen EINER Strassenseite bauen. `bForward` waehlt die Seite (rechts
	 * der Hinfahrt bzw. gegenueber) und damit Richtung, Zieltext und
	 * Durchfahrtszeit der Tafel. Je Halte ruft BuildMonitors das zweimal auf.
	 */
	void BuildMonitorsForSide(bool bForward, const TArray<int32>& Indices,
		const TArray<FString>& Wanted, double SpeedCmS);

	struct FMonitor
	{
		int32 StopIndex = 0;
		FString Name;
		/** Zieltext DIESER Saeule: `to` auf der Hinfahrtsseite, `from` gegenueber. */
		FString Destination;
		/** Richtung, deren Durchfahrten diese Saeule ankuendigt. */
		bool bForward = true;
		double OffsetSeconds = 0.0;   // Fahrzeit ab Terminus bis zu dieser Halte (ihrer Richtung)
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

	// Geparste Liniendatei samt Route in Weltkoordinaten: DASSELBE Objekt, das der
	// Bus-Actor derselben Linie benutzt (World/WiesbadenBusLineFile). Der Monitor
	// liest die Datei damit nicht noch einmal - und kann keine anderen Zeiten
	// anzeigen, als die Busse fahren. NUR LESEN.
	TSharedPtr<const WiesbadenBusLineFile::FLineRoute> LineRoute;
	FString LineRef = TEXT("6");            // Liniennummer (Anzeige)
	// Echter Fahrplan aus demselben Leser; ohne Fahrplandatei bleibt er leer.
	TSharedPtr<const WiesbadenBusLine::FBusSchedule> Schedule;
	// Dauerbetrieb: dieselbe Flotte wie die Busse - die Anzeige darf nicht andere
	// Zeiten nennen, als die Wagen fahren.
	TArray<WiesbadenBusLine::FBusVehicle> Fleet;
	double CycleSeconds = 0.0;

	double UpdateAccum = 1.0;   // beim ersten Tick sofort aktualisieren
	bool bReady = false;
};
