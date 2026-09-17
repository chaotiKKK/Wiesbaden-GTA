// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/WiesbadenBusLine.h"
#include "World/WiesbadenRailTransport.h"
#include "WiesbadenBusRoute.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UGeoCoordinateConverter;
class UWiesbadenCitySubsystem;
class UWiesbadenVehicleCameraComponent;

/**
 * Ein sichtbarer Linienbus, der eine OSM-Buslinie abfaehrt (Pilot: ESWE-Linie 6).
 *
 * Sichtbar-only (kein Mitfahren, keine Kopplung an die Verkehrs-Sim): mehrere
 * Busse folgen der aus Data/Raw/Bus/<LineFile> geladenen Halte-Polylinie,
 * verweilen an jeder Halte und wenden am Terminus. Die reine Zeit->Bogenlaenge-
 * Logik liegt datenrein in WiesbadenBusLine; dieser Actor macht Geo->Welt,
 * bewegt die Bus-Meshes und tastet den Boden je Tick an der Busposition ab
 * (die Strecke ist zu lang, um sie auf einmal zu verankern - gestreamt ist nur,
 * wo gerade jemand hinschaut).
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenBusRoute : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenBusRoute();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Liniendatei unter Data/Raw/Bus/ (path[[lat,lon]] + stops[[lat,lon]]). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	FString LineFile = TEXT("line6.json");

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "1.0"))
	float SpeedKmh = 32.0f;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "0.0"))
	float StopDwellSeconds = 8.0f;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "0.0"))
	float TerminusDwellSeconds = 25.0f;

	/** Fahrzeug-Pool: Obergrenze gleichzeitig sichtbarer Busse. Wie viele wirklich
	 *  fahren, bestimmt der Fahrplan (in der HVZ ~5-6). Muss >= Spitzenzahl sein. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "1"))
	int32 NumBuses = 12;

	/** Fahrplandatei unter Data/Raw/Bus/ (Abfahrten ab Terminus). Leer = alter
	 *  Modus (gleichverteilte Busse). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	FString ScheduleFile = TEXT("line6_schedule.json");

	/** Dienst-Uhrzeit beim Spielstart (Stunde 0-24). 7 = HVZ, damit sofort Busse
	 *  fahren; die Spielzeit laeuft danach echt weiter (Fahrplan wiederholt taeglich). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float ServiceStartHour = 7.0f;

	/** Ziellaenge des Bus-Meshes (Gelenkbus ~18 m). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "100.0"))
	float BusLengthCm = 1800.0f;

	/** Zusaetzliche Anhebung ueber den Boden, cm (0 = Raeder sitzen auf; der Pivot-
	 *  Ausgleich passiert automatisch ueber die Mesh-Unterkante). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float BusLiftCm = 0.0f;

	/**
	 * Seitenversatz von der Trassen-Mittellinie nach rechts (Rechtsverkehr), cm.
	 * Die Buslinie ist eine gemeinsame Mittellinie fuer beide Richtungen; ohne
	 * Versatz fuehren Gegenrichtungs-Busse durcheinander. Jeder Bus faehrt so viel
	 * rechts seiner Fahrtrichtung -> Begegnung nebeneinander (Abstand = 2x Wert).
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "0.0"))
	float LaneOffsetCm = 220.0f;

	/** Zusaetzlicher Rechts-Versatz an den Halten (Haltebucht), cm. Der Bus schert
	 *  weich um bis zu diesen Betrag aus und wieder ein. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "0.0"))
	float BayDepthCm = 260.0f;

	/** Laenge der Ein-/Ausscher-Rampe vor/nach der Halte, cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "1.0"))
	float BayZoneCm = 2600.0f;

	/** Korrektur der glTF-Achsen des importierten Mesh (Blender-Z -> UE-Y). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	FRotator MeshOrient = FRotator(0.0f, -90.0f, 0.0f);

	/** Groesse des Zielanzeige-Schilds an der Front (Breite/Hoehe, cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float SignWidthCm = 210.0f;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float SignHeightCm = 52.0f;

	/** Hoehe der Zielanzeige ueber dem Boden, cm (deckt die Original-Anzeige oben). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float SignZAboveGroundCm = 280.0f;

	/** Laengsversatz der Anzeige vom Bus-Mittelpunkt nach vorn (Anteil BusLength;
	 *  knapp vor der Frontflaeche, damit sie nicht im gewoelbten Bug steckt). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float SignFrontFrac = 0.505f;

	/** Bus haelt an roten Ampeln vor der Haltelinie (Kopplung ans Ampel-Aspektmodell;
	 *  Autos halten bereits ueber die Verkehrs-Sim). Nur auf gebackenen Karten aktiv. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus|Ampel")
	bool bStopAtRed = true;

	/** Haltelinie so viele cm vor dem Ampelknoten. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus|Ampel", meta = (ClampMin = "0.0"))
	float RedStopMarginCm = 550.0f;

	/** Ab dieser Entfernung wird die naechste Ampel voraus geprueft. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus|Ampel", meta = (ClampMin = "100.0"))
	float RedApproachCm = 5000.0f;

	/** Max. Abstand Ampelknoten<->Trasse, damit die Ampel als "auf der Linie" gilt. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus|Ampel", meta = (ClampMin = "100.0"))
	float RedGateMatchCm = 1500.0f;

	/** Mitfahren: Einstiegsreichweite um einen an der Halte stehenden Bus, cm (E-Taste). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus|Mitfahren", meta = (ClampMin = "100.0"))
	float BoardRangeCm = 700.0f;

private:
	void LoadLine();
	void LoadSchedule();
	void BuildWorldPath();
	bool ResolveGround(double X, double Y, double& OutZ) const;
	// Setzt Bus + Zielschild in Slot k auf den uebergebenen Fahrzustand
	// (oder versteckt beides, wenn dort gerade kein Boden gestreamt ist).
	void PlaceBusAt(int32 SlotIndex, const WiesbadenBusLine::FBusState& St, bool bLog);
	void HideBusSlot(int32 SlotIndex);

	// Ampel-Kopplung: einmalig die Ampeln entlang der Linie als "Gates" (Bogenlaenge
	// + Ampel-Index) sammeln; St mit Rotlicht-Halt (verstrichene Zeit um die Haltezeit
	// gekuerzt, Position an roter Ampel geklemmt); pruefen ob die naechste Ampel rot ist.
	void BuildGates();
	WiesbadenBusLine::FBusState ComputeHeldState(int64 RunKey, double RawElapsed, float DeltaSeconds, bool& bOutFinished);
	bool RedGateAhead(double ArcCm, const FVector& Dir, bool bForward, double& OutStopArcCm) const;

	// Mitfahren: E-Taste steigt neben einem an der Halte stehenden Bus ein/aus.
	// Der Fahrgast wird an das gepoolte Bus-Mesh geheftet; sein Slot wird gepinnt,
	// damit die Fahrplan-Slotvergabe ihn nicht wegtauscht/versteckt.
	void UpdateRiding();
	void ToggleBoarding();
	void CreatePassengerCamera();
	void DestroyPassengerCamera();

	UPROPERTY(Transient) USceneComponent* Root = nullptr;
	UPROPERTY(Transient) UGeoCoordinateConverter* Converter = nullptr;
	UPROPERTY(Transient) UStaticMesh* BusMesh = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Buses;

	// Zielanzeige (Blind) vor der Front: pro Bus ein Quad, dessen Material je
	// Fahrtrichtung wechselt (Mainz-Gonsenheim hin, Nordfriedhof zurueck).
	UPROPERTY(Transient) UStaticMesh* SignMesh = nullptr;
	UPROPERTY(Transient) UMaterialInterface* SignMatMainz = nullptr;
	UPROPERTY(Transient) UMaterialInterface* SignMatNord = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Signs;
	TArray<int8> SignForward;   // pro Bus zuletzt gesetzte Richtung (-1 = noch keine)

	TArray<FVector2D> GeoPath;    // lat,lon
	TArray<FVector2D> GeoStops;   // lat,lon
	TArray<FVector> WorldPath;    // cm (Z=0, wird je Tick lokal getastet)
	TArray<double> ArcCm;         // kumulierte 2D-Bogenlaenge
	WiesbadenBusLine::FBusRoute Route;
	WiesbadenBusLine::FBusSchedule Schedule;   // echter Fahrplan (Abfahrten ab Terminus)

	// Ampel-Gates entlang der Linie + Rotlicht-Haltezeit je Kurs.
	struct FBusGate { double ArcCm = 0.0; int32 LightIndex = 0; };
	TArray<FBusGate> Gates;
	bool bGatesBuilt = false;
	TMap<int64, double> HoldByRun;   // Kurs-Index -> aufsummierte Rotlicht-Haltezeit (s)
	UPROPERTY(Transient) UWiesbadenCitySubsystem* CitySubsystem = nullptr;

	// Mitfahren: Zustand je Slot (fuer die Einstiegssuche) + aktuelle Fahrt.
	TArray<FVector> SlotWorldPos;   // letzte Weltposition je Slot
	TArray<uint8> SlotState;        // 0 = leer, 1 = faehrt, 2 = haelt an der Halte
	TArray<int64> SlotRunKey;       // welcher Kurs gerade in dem Slot faehrt
	WiesbadenRailTransport::FWiesbadenRideSession RideSession;
	UPROPERTY(Transient) UWiesbadenVehicleCameraComponent* PassengerCamera = nullptr;
	// Unskalierter Anker (das Bus-Mesh ist ~18x skaliert; daran hingen die Kamera-
	// Offsets das 18-fache). Der Anker folgt je Tick der Pose des mitgefahrenen Busses;
	// Fahrgast + Kamera haengen am Anker.
	UPROPERTY(Transient) USceneComponent* RideAnchor = nullptr;
	int32 RiddenSlot = INDEX_NONE;  // gepinnter Slot des mitgefahrenen Busses
	int64 RiddenRunKey = -1;        // dessen Kurs-Index
	bool bBoardKeyHeld = false;

	double MeshScale = 1.0;
	double MeshBottomCm = 0.0;   // Pivot -> Unterkante (Anhebung, damit die Raeder aufsitzen)
	double MeshTopCm = 0.0;      // Pivot -> Oberkante (fuer die Hoehe der Zielanzeige)
	double CycleSeconds = 0.0;
	bool bReady = false;

	// Diagnose (-WbBusLog): Halte-Weltkoordinaten beim Start, danach je ~2 s die
	// Position/Verweilen jedes sichtbaren Busses. Nur zum Verifizieren; standard aus.
	bool bLogDiag = false;
	double LogAccum = 0.0;
};