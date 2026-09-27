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
 *
 * ESWE-HALTESTELLE (25.09.): je Halte und Fahrtrichtung steht die komplette
 * Ausstattung im ESWE-Stil (Tools/Blender/make_eswe_haltestelle.py): Wartehalle,
 * Haltemast mit H-Schild, Namens- und Linienschild, DFI-Stele. Sie steht RECHTS
 * der Fahrtrichtung hinter der Bordsteinkante - nicht mehr 4,80 m neben der
 * Linie, wo der haltende Bus selbst steht (LaneOffset 2,20 + Bucht 2,60 m).
 * Hat die Linie einen eigenen Rueckweg, steht die Gegenrichtung an IHRER Halte
 * (am Hauptbahnhof auf der anderen Richtungsfahrbahn) statt der Hinweg-Halte
 * gegenueber. Teilen sich zwei Linien eine Halte, bekommt die zweite nur eine
 * eigene DFI-Stele und ihre Nummer aufs Linienschild der ersten.
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

	/**
	 * Abstand der Bordsteinkante von der Linie, rechts der Fahrtrichtung, cm.
	 * Der haltende Bus steht mit seiner Mitte LaneOffset + Bucht = 480 cm neben
	 * der Linie, halbe Breite 128 cm, dazu 30 cm Luft: 640. Davon aus steht die
	 * Ausstattung auf dem Gehweg (Mast 45, Halle 70..230, Stele 50 cm dahinter).
	 * Liegt dort Bebauung (Boden deutlich hoeher als die Fahrbahn), rueckt die
	 * Halte in 40-cm-Schritten bis zu 2,40 m naeher an die Fahrbahn.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Monitor", meta = (ClampMin = "0.0"))
	float CurbOffsetCm = 640.0f;

	/**
	 * Eine aufgestellte Haltestelle (Richtung + Ort). Eine zweite Linie mit
	 * derselben Halte in derselben Richtung stellt keine zweite Halle auf,
	 * sondern meldet sich hier an (JoinStop).
	 */
	struct FStopFurniture
	{
		FVector Base = FVector::ZeroVector;   // Bordsteinkante an der Halte (Boden)
		FVector Dir = FVector::ForwardVector;  // Fahrtrichtung
		FVector Side = FVector::RightVector;   // vom Bordstein weg (rechts der Fahrt)
		TArray<FString> Lines;
		TArray<UTextRenderComponent*> LineTexts;   // Linienschild, beide Seiten
		int32 DfiCount = 1;
	};

	/**
	 * Steht an Base/Dir schon eine Halte (dieses Actors)? Dann Linie anmelden,
	 * Linienschild ergaenzen und die Stelle fuer die naechste DFI-Stele liefern.
	 */
	bool JoinStop(const FVector& Base, const FVector& Dir, const FString& Line,
		FStopFurniture& OutStop, int32& OutDfiSlot);

private:
	/** Holt die Linie aus dem gemeinsamen Leser (World/WiesbadenBusLineFile) und
	 *  uebernimmt daraus Liniennummer, Ziel, Takt, Wendezeit und Monitorhalte. */
	void LoadLine();
	void LoadSchedule();
	bool ResolveGround(double X, double Y, double& OutZ) const;
	void BuildMonitors();
	/**
	 * Die Haltestellen EINER Fahrtrichtung bauen. `bForward` waehlt die
	 * Richtung und damit Linie (Hinweg bzw. eigener Rueckweg), Halte, Zieltext
	 * und Durchfahrtszeit der Tafel. BuildMonitors ruft das je Richtung auf.
	 */
	void BuildMonitorsForSide(bool bForward, const TArray<FString>& Wanted, double SpeedCmS);
	UStaticMeshComponent* AddPart(UStaticMesh* Mesh, const FVector& Location, const FQuat& Rot);
	UTextRenderComponent* AddText(const FVector& Location, const FVector& Facing, float Size,
		const FColor& Color, const FString& Text);

	struct FMonitor
	{
		int32 StopIndex = 0;
		FString Name;
		/** Zieltext DIESER Saeule: `to` auf der Hinfahrtsseite, `from` gegenueber. */
		FString Destination;
		/** Richtung, deren Durchfahrten diese Saeule ankuendigt. */
		bool bForward = true;
		/** Eigener Rueckweg: OffsetSeconds zaehlt bis zur Rueckweg-Halte. */
		bool bReturnPath = false;
		double OffsetSeconds = 0.0;   // Fahrzeit ab Terminus bis zu dieser Halte (ihrer Richtung)
		UTextRenderComponent* Text = nullptr;       // Schirm zur Strasse
		UTextRenderComponent* TextBack = nullptr;   // Schirm zum Gehweg
	};
	TArray<FMonitor> Monitors;
	TArray<FStopFurniture> Furniture;

	UPROPERTY(Transient) USceneComponent* Root = nullptr;
	UPROPERTY(Transient) UGeoCoordinateConverter* Converter = nullptr;
	UPROPERTY(Transient) UStaticMesh* ShelterMesh = nullptr;
	UPROPERTY(Transient) UStaticMesh* MastMesh = nullptr;
	UPROPERTY(Transient) UStaticMesh* DfiMesh = nullptr;
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
