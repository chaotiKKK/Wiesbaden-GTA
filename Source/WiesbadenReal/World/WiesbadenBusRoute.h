// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/WiesbadenBusInterior.h"
#include "World/WiesbadenBusLine.h"
#include "World/WiesbadenBusLineFile.h"
#include "World/WiesbadenBusDrive.h"
#include "World/WiesbadenRailTransport.h"
#include "WiesbadenBusRoute.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UGeoCoordinateConverter;
class UWiesbadenCitySubsystem;
class UWiesbadenVehicleCameraComponent;
class UMaterialInterface;
class UAudioComponent;
class USoundBase;
class UProceduralMeshComponent;

/**
 * Ein sichtbarer Linienbus, der eine OSM-Buslinie abfaehrt (Linie 6 und 3).
 *
 * Eine Instanz je LINIE (LineFile). Die Fahrzeuge sind FESTE Wagen: jeder hat
 * seine Nummer (FBusVehicle::Id) fuer die ganze Dienstzeit und faehrt seinen
 * Umlauf ununterbrochen - Endpunkt, 10 Minuten Wendezeit, zurueck, wieder 10
 * Minuten - ohne dass ein Fahrplan ihn zwischendurch aus dem Verkehr nimmt.
 * Der Takt kommt aus der Liniendatei (OSM `interval`); wie viele Wagen dafuer
 * noetig sind, rechnet WiesbadenBusLine::BuildFleet aus.
 *
 * Die reine Zeit->Bogenlaenge-Logik liegt datenrein in WiesbadenBusLine; dieser
 * Actor macht Geo->Welt, bewegt die Bus-Meshes und tastet den Boden je Tick an
 * der Busposition ab (die Strecke ist zu lang, um sie auf einmal zu verankern -
 * gestreamt ist nur, wo gerade jemand hinschaut).
 *
 * Ausserdem: Mitfahren (E-Taste, Innenraum + Ansagen) und die Ampel-Kopplung.
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

	/** Wendezeit an BEIDEN Endpunkten, soweit die Liniendatei nichts anderes sagt.
	 *  600 s = 10 Minuten: der Wagen steht am Terminus, bevor er zurueckfaehrt. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "0.0"))
	float TerminusDwellSeconds = 600.0f;

	/**
	 * Dauerbetrieb (Standard): feste Wagen fahren ihren Umlauf durch, Wendezeit an
	 * beiden Enden. Aus: der echte Fahrplan (ScheduleFile) bestimmt die Kurse.
	 * Kommandozeile: -WbBusSchedule erzwingt den Fahrplan, -WbBusFleet den
	 * Dauerbetrieb (fuer Vergleichsaufnahmen).
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	bool bContinuousService = true;

	/** Angestrebter Takt in Sekunden. 0 = aus der Liniendatei (`headway_seconds`,
	 *  aus dem OSM-`interval`), sonst 20 Minuten. Der tatsaechliche Abstand ist
	 *  Umlauf/Anzahl und damit nie groesser als dieser Wert. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "0.0"))
	float HeadwaySeconds = 0.0f;

	/** Fahrzeug-Pool: Obergrenze gleichzeitig sichtbarer Busse. Wie viele wirklich
	 *  fahren, rechnet der Dienst aus (Umlauf/Takt); im Fahrplanmodus ist es die
	 *  Spitzenzahl der Kurse. Muss >= Spitzenzahl sein. */
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

	/** Ziellaenge des Bus-Meshes in cm. Das SM_Bus-Mesh ist bereits in realer Groesse
	 *  gebacken (Blender: Breite 2,55 m -> Laenge 8,27 m, Bus/bake_eswebus.py), also
	 *  ergibt 827 eine Skalierung ~1,0. Andert sich die Breite des Meshes, hier die
	 *  daraus folgende Laenge eintragen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus", meta = (ClampMin = "100.0"))
	float BusLengthCm = 827.0f;

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

	/** Groesse der Front-/Seitenanzeige (Punktmatrix-Blind), cm. Aspekt ~5:1 wie die Textur. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float BlindWidthCm = 200.0f;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float BlindHeightCm = 40.0f;

	/** Kantenlaenge der quadratischen Heck-Anzeige (nur Liniennummer), cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float RearBlindSizeCm = 44.0f;

	/** Hoehe der Anzeigen ueber dem Boden, cm (Oberkante der Front des 2,25-m-Busses). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float BlindZAboveGroundCm = 195.0f;

	/** Laengsversatz Front-/Heckanzeige vom Bus-Mittelpunkt (Anteil BusLength). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float BlindEndFrac = 0.5f;

	/** Laengsversatz der Seitenanzeige nach vorn (Anteil BusLength; nahe der vorderen Tuer). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float BlindSideForwardFrac = 0.28f;

	/** Halbe Fahrzeugbreite fuer die Seitenanzeige, cm (Modell ~2,55 m breit). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bus")
	float BusHalfWidthCm = 128.0f;

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
	bool ResolveGround(double X, double Y, double& OutZ) const;
	// Setzt Bus + Zielschild in Slot k auf den uebergebenen Fahrzustand
	// (oder versteckt beides, wenn dort gerade kein Boden gestreamt ist).
	void PlaceBusAt(int32 SlotIndex, const WiesbadenBusLine::FBusState& St, bool bLog);
	void AdvanceAndPlaceBus(int32 SlotIndex, const WiesbadenBusLine::FBusState& St,
		float DeltaSeconds, bool bLog);
	void HideBusSlot(int32 SlotIndex);

	// Ampel-Kopplung: einmalig die Ampeln entlang der Linie als "Gates" (Bogenlaenge
	// + Ampel-Index) sammeln; St mit Rotlicht-Halt (verstrichene Zeit um die Haltezeit
	// gekuerzt, Position an roter Ampel geklemmt); pruefen ob die naechste Ampel rot ist.
	// bPeriodic = Dauerbetrieb: die verstrichene Zeit laeuft im Umlauf um (kein
	// "fertig"), und die Rotlicht-Zeit ist auf die Wendezeit begrenzt.
	void BuildGates();
	WiesbadenBusLine::FBusState ComputeHeldState(int64 VehicleId, double RawElapsed,
		float DeltaSeconds, bool bPeriodic, bool& bOutFinished);
	bool RedGateAhead(double InArcCm, const FVector& Dir, bool bForward, double& OutStopArcCm) const;

	/** Uebernimmt aus der (gemeinsam gelesenen) Liniendatei, was dieser Actor
	 *  braucht: Liniennummer, Takt, Wendezeit, Zielschilder. */
	void ApplyLineConfig(const WiesbadenBusLineFile::FLineFile& File);
	void LoadBlinds();
	void BuildServiceFleet();

	/**
	 * Nummer des ersten Wagens dieser Linie (Linie 6 -> 601, Linie 3 -> 301).
	 *
	 * Ohne das Vorzeichen fuhren zwei Busse verschiedener Linien als "Wagen 2" -
	 * im gemeinsamen Log, am Steg und in der Mitfahr-Diagnose nicht unterscheidbar.
	 * Ist die Liniennummer keine einstellige Zahl (oder leer), bleibt es bei 1.
	 */
	int32 FirstVehicleNumber() const;

	// Mitfahren: E-Taste steigt neben einem an der Halte stehenden Bus ein/aus.
	// Der Fahrgast wird an das gepoolte Bus-Mesh geheftet; sein Slot wird gepinnt,
	// damit der Dienst ihn nicht wegtauscht/versteckt.
	void UpdateRiding(float DeltaSeconds);
	void ToggleBoarding();
	/** Was steht bei der Mitfahrt vor der Kamera? (Bildbeleg allein sagt es nicht.) */
	void LogRideDiag();

	// Innenraum des Fahrgasts (World/WiesbadenBusInterior): Der Bus ist ein reines
	// Aussen-Modell - ohne Innenraum sass die Kamera in der geschlossenen Huelle
	// und man schwebte scheinbar ueber der Strasse. Das Mesh haengt am Mitfahr-
	// Anker (folgt also dem Bus) und wird nur waehrend der Mitfahrt gezeigt;
	// gleichzeitig wird die eigene Aussenhaut samt Zielschildern fuer den Fahrgast
	// ausgeblendet.
	void BuildInteriorMesh();
	void ShowInterior(bool bShow);
	/** Masse des Innenraums aus Wagenlaenge/-breite und Mesh-Oberkante. */
	WiesbadenBusInterior::FSpec InteriorSpec() const;

	// Diagnose (-WbBusRide=<Sekunden>): steigt nach so vielen Sekunden von selbst
	// in einen gerade haltenden Bus ein - fuer Bildbelege, weil Tastendruecke in
	// automatischen Laeufen nicht ankommen (vgl. -WbMitfahr an der Nerobergbahn).
	void UpdateDevRide(double WorldTime, float DeltaSeconds);
	void CreatePassengerCamera();
	void DestroyPassengerCamera();

	// Gesprochene Halteansagen (nur fuer den mitfahrenden Fahrgast): beim Verlassen
	// einer Halte kuendigt eine deutsche TTS-Stimme ueber den Voice-Bus die naechste
	// an und senkt dabei Musik + Ambiente ueber das Mischpult ab (Ducking).
	void SetupAnnouncements();
	// Naechste anzusagende Halte aus dem Fahrzustand (Fahrtrichtung + Bogenlaenge).
	int32 NextStopIndex(const WiesbadenBusLine::FBusState& St) const;
	// Wechselt die naechste Halte -> Ansage spielen (einmal je Halte-Uebergang).
	void UpdateStopAnnouncement(const WiesbadenBusLine::FBusState& St);
	void PlayStopAnnouncement(int32 StopIndex);
	void StopAnnouncement();          // Ansage abbrechen + Ducking beenden (Aussteigen)
	void EndDucking();                // Timer-Rueckruf: Ducking nach Ansagedauer aus
	void SetAudioDucking(bool bActive);

	UPROPERTY(Transient) USceneComponent* Root = nullptr;
	UPROPERTY(Transient) UGeoCoordinateConverter* Converter = nullptr;
	UPROPERTY(Transient) UStaticMesh* BusMesh = nullptr;
	UPROPERTY(Transient) UStaticMesh* BusWheelMesh = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Buses;
	/** Sechs Raeder je Bus in der Reihenfolge vorne/mittig/hinten, links/rechts. */
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> BusWheels;
	TArray<WiesbadenBusDrive::FState> DriveStates;

	// Zielanzeige (Blind): authentische Punktmatrix-Texturen (Bernstein-LEDs) auf
	// unlit Quads. Front + rechte (Tuer-)Seite zeigen Liniennummer + Ziel (Material
	// je Fahrtrichtung), das Heck nur die Liniennummer. Statische Texturen -> keine
	// Spiegel-/Achsen-Ueberraschungen wie beim frueheren aufgemalten Blind.
	UPROPERTY(Transient) UStaticMesh* BlindMesh = nullptr;              // Engine-Plane als Quad
	// Zielschilder DIESER Linie. Die Namen stehen in der Liniendatei (`blinds`),
	// damit eine zweite Linie ohne Codeänderung dazukommen kann; ohne Angabe gilt
	// die Linie-6-Bestellung. -1 Richtung: Hinrichtung des Pfades, Rueckrichtung
	// ist die Gegenrichtung.
	UPROPERTY(Transient) UMaterialInterface* BlindMatForward = nullptr;   // "[6] Mainz-Gonsenheim"
	UPROPERTY(Transient) UMaterialInterface* BlindMatBackward = nullptr;  // "[6] Nordfriedhof"
	UPROPERTY(Transient) UMaterialInterface* BlindMatLine = nullptr;      // "6" (Heck)
	/** Pfad der Blind-Assets (aus der Liniendatei, Vorgabe Linie 6). */
	FString BlindDir = TEXT("/Game/Vehicles/Bus/Blind");
	FString BlindNameForward = TEXT("M_WbBlindMainz");
	FString BlindNameBackward = TEXT("M_WbBlindNord");
	FString BlindNameLine = TEXT("M_WbBlindRoute6");
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> BlindFront;      // Front-Quad je Bus
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> BlindSide;       // rechte Seite je Bus
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> BlindRear;       // Heck je Bus
	TArray<int8> BlindForward;   // zuletzt gesetzte Richtung Front/Seite (-1 = noch keine)

	// Geparste Liniendatei samt Route in Weltkoordinaten - EIN Objekt aus dem
	// gemeinsamen Leser (World/WiesbadenBusLineFile). Der Haltestellenmonitor
	// derselben Linie haelt DASSELBE Objekt: es gibt nur einen Leser und nur eine
	// geparste Route je Datei. NUR LESEN - der Cache gehoert allen Aufrufern.
	TSharedPtr<const WiesbadenBusLineFile::FLineRoute> LineRoute;
	// Echter Fahrplan (Abfahrten ab Terminus), ebenfalls aus dem gemeinsamen
	// Leser; ohne Fahrplandatei bleibt er leer (Dauerbetrieb).
	TSharedPtr<const WiesbadenBusLine::FBusSchedule> Schedule;

	// Ampel-Gates entlang der Linie + Rotlicht-Haltezeit je Kurs.
	struct FBusGate { double ArcCm = 0.0; int32 LightIndex = 0; };
	TArray<FBusGate> Gates;
	bool bGatesBuilt = false;
	// Ampel-Haltezeit je WAGEN (im Dauerbetrieb umlaufend, auf die Wendezeit
	// begrenzt - sonst wuerde ein Wagen ueber Stunden immer spaeter).
	TMap<int32, double> HoldByVehicle;

	// Der Dienst des Tages: feste Wagen, die ihren Umlauf durchfahren.
	TArray<WiesbadenBusLine::FBusVehicle> Fleet;
	/** Angestrebter Takt in Sekunden (aus Liniendatei oder Eigenschaft). */
	double ServiceHeadwaySeconds = 1200.0;
	/** Liniennummer aus der Liniendatei (fuer Ansagen-/Logbezug). */
	FString LineRef = TEXT("6");
	UPROPERTY(Transient) UWiesbadenCitySubsystem* CitySubsystem = nullptr;

	// Mitfahren: Zustand je Slot (fuer die Einstiegssuche) + aktuelle Fahrt.
	TArray<FVector> SlotWorldPos;   // letzte Weltposition je Slot
	TArray<uint8> SlotState;        // 0 = leer, 1 = faehrt, 2 = haelt an der Halte
	TArray<int32> SlotVehicleId;    // welche WAGEN-Nummer in dem Slot faehrt (-1 = leer)
	WiesbadenRailTransport::FWiesbadenRideSession RideSession;
	UPROPERTY(Transient) UWiesbadenVehicleCameraComponent* PassengerCamera = nullptr;
	// Unskalierter Anker (Scale 1). Das Bus-Mesh ist inzwischen real gebacken
	// (MeshScale ~1,0), aber der Anker bleibt bewusst skalierungsunabhaengig, damit
	// die Kamera-Offsets echte cm sind. Der Anker folgt je Tick der Pose des
	// mitgefahrenen Busses; Fahrgast, Kamera und Innenraum haengen am Anker.
	//
	// Der Anker traegt die FAHRT-RICHTUNG (Komponentenrotation OHNE MeshOrient):
	// +X = vorn, +Y = rechts, +Z = oben. Mit der rohen Komponentenrotation laege
	// die Wagenlaengsachse auf +Y - die Fahrgast-Offsets zeigten dann 90 Grad quer
	// und die Kamera stand neben dem Bus in der Luft (gemeldeter Fehler).
	UPROPERTY(Transient) USceneComponent* RideAnchor = nullptr;
	// Innenraum-Mesh des Fahrgasts - haengt am Anker (folgt dem Bus) und ist
	// ausserhalb der Mitfahrt unsichtbar.
	UPROPERTY(Transient) UProceduralMeshComponent* InteriorMesh = nullptr;
	int32 RiddenSlot = INDEX_NONE;  // gepinnter Slot des mitgefahrenen Busses
	int32 RiddenVehicleId = -1;     // dessen feste Wagen-Nummer
	bool bBoardKeyHeld = false;
	// Dev-Mitfahrt (-WbBusRide): Startzeit + ob schon versucht wurde.
	float DevRideAfterSeconds = -1.0f;
	bool bDevRideDone = false;
	double DevRideRetryAccum = 0.0;
	/** Nur diesen Wagen einsteigen (-WbBusRideWagon=601): die Dev-Mitfahrt nimmt
	 *  sonst den ersten haltenden Bus, also einen beliebigen Wagen. Fuer einen
	 *  Beleg auf einem Wagen der neuen Nummernkreise muss sie den Wagen WAEHLEN
	 *  koennen statt auf den Zufall zu warten. */
	int32 DevRideWagonId = -1;
	// Dev-Ausstieg (-WbBusRideExit): Mitfahrzeit bis zum automatischen Aussteigen.
	float DevRideExitAfterSeconds = -1.0f;
	double DevRideRiddenSeconds = 0.0;
	bool bDevRideExitDone = false;
	float RideDiagAccum = 0.0;

	// Halteansagen: echte Namen (aus line6.json "stop_names"), eine vorgerenderte
	// TTS-Welle je Halte (/Game/Audio/Bus/Announce/A_00..), ein Cabin-PA-
	// AudioComponent (2D, ueber den Voice-Bus geroutet), zuletzt angesagte Halte
	// und ein Timer, der das Ducking nach der Ansagedauer wieder aufhebt.
	UPROPERTY(Transient) UAudioComponent* AnnounceAudio = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> AnnounceWaves;
	int32 LastAnnouncedStop = INDEX_NONE;
	FTimerHandle DuckTimer;

	double MeshScale = 1.0;
	double MeshBottomCm = 0.0;   // Pivot -> Unterkante (Anhebung, damit die Raeder aufsitzen)
	double MeshTopCm = 0.0;      // Pivot -> Oberkante (fuer die Hoehe der Zielanzeige)
	double CycleSeconds = 0.0;
	bool bReady = false;

	// Diagnose (-WbBusLog): Halte-Weltkoordinaten beim Start, danach je ~2 s die
	// Position/Verweilen jedes sichtbaren Busses. Nur zum Verifizieren; standard aus.
	bool bLogDiag = false;
	double LogAccum = 0.0;
	/** Umlauf-Protokoll EINES Wagens (-WbBusLogWagon=<Wagennummer>, z.B. 601):
	 *  zusaetzlich zu den ~2-s-Zeilen aller Wagen wird fuer diesen Wagen je Tick
	 *  Bogenlaenge, Unterkante, Fahrbahn- UND Gelaendehoehe, Zustand, Richtung
	 *  und Mitfahrt protokolliert - der Beleg fuer einen KOMPLETTEN Umlauf. */
	int32 LogWagonId = -1;
	/** Eigener 2-s-Takt des Umlauf-Protokolls (unabhaengig von -WbBusLog). */
	bool bLogWagonTick = false;
	double LogWagonAccum = 0.0;

	// Diagnose (-WbBusParkStop=N): parkt Slot 0 (Hinrichtung) + Slot 1 (Gegenrichtung)
	// haltend an Halt N und schaltet den Fahrbetrieb ab. So steht garantiert je ein Bus
	// beider Richtungen an einer bekannten Stelle - fuer Modell-/Zielanzeige-Aufnahmen.
	int32 ParkStop = -1;

	// Diagnose (-WbBusAnnounceTest): treibt die Halteansagen vom ERSTEN aktiven
	// Kurs, ohne dass jemand einsteigt - fuer automatische Laeufe (kein E-Tastendruck).
	bool bAnnounceDiag = false;

	/** Dienstzeit-Versatz in Sekunden (-WbBusClock=): der Dienst beginnt dann
	 *  nicht bei Dienstbeginn, sondern um diese Zeit spaeter. Nur fuer Belege:
	 *  damit laesst sich ein Wagen an den ZWEITEN Endpunkt stellen, ohne die
	 *  halbe Umlaufzeit abzuwarten. Standard 0 = unveraendert. */
	double ServiceClockOffsetSeconds = 0.0; 	// -- Diagnose Boden/Fahrbahn (-WbBusGroundAudit) --------------------------
	// Frage: Sitzt der Bus auf der FAHRBAHN? Gemessen wird an jeder Probe:
	//  (1) welche Flaeche der senkrechte Strahl trifft (Fahrbahn-Kollision der
	//      Stadt = RoadCollisionStaticMesh vs. nur Landscape),
	//  (2) ob darunter noch eine Flaeche liegt (Belag ueber Gelaende),
	//  (3) der Abstand der Bus-Unterkante zur Fahrbahn des STRASSENNETZES
	//      (die Sollbahn der Verkehrs-Simulation) und
	//  (4) wo die Fahrbahn-Kollision getroffen wurde: Abstand Belag <-> Spur.
	// (3) ist die entscheidende Zahl: sie muss 0 cm sein, sonst sitzen die
	// Raeder nicht auf dem Asphalt. (4) belegt, dass das Strassennetz dieselbe
	// Flaeche ist wie der sichtbare Belag.
	void AuditGround(int32 Slot, double AtArcCm, double X, double Y,
		double TraceZ, bool bLane, double LaneZ, double RejectedDevCm);
	void WriteGroundAudit();

	// -- Fahrbahnhoehe aus dem Strassennetz ----------------------------------
	// Die gebackene Karte traegt keine Fahrbahn-Kollision (gemessen: auf beiden
	// Linien trifft der vertikale Strahl in JEDER Probe nur das Landscape). Der
	// Bus stand damit auf dem Gelaende statt auf dem Belag. Die Fahrbahnhoehe
	// kommt deshalb aus dem Spur-Graph der Stadt - dieselbe Sollbahn, auf der
	// die Verkehrs-Simulation faehrt (Terrain + Fahrbahnversatz).
	void BuildLaneIndex();
	/** Fahrbahnhoehe an (X,Y). Unter allen Spuren in Reichweite gewinnt die mit dem
	 *  kleinsten GESAMTABSTAND (waagerecht UND Hoehe zur erwarteten Bodenhoehe
	 *  HintZ = Gelaende-Trace). Die rein waagerecht naechste Spur lag an Knoten bis
	 *  40 m daneben (Rampe/Parallelfahrbahn auf anderem Niveau) und setzte den Bus
	 *  in die Luft; die Hoehe im Fehlermass waehlt die Spur UNTER dem Bus.
	 *  Rueckgabe false = keine brauchbare Spur -> Gelaende-Trace. */
	bool RoadSurfaceZ(double X, double Y, double MaxDistCm, double HintZ, double& OutZ,
		double* OutRejectedDevCm = nullptr);

	/** Suchradius Spur (cm): eine Spurbreite links/rechts, nicht mehr. */
	float RoadReachCm = 900.0f;
	/** Zulaessiger Hoehenabstand Spur <-> Gelaende-Trace (cm). Mehr = grob falsche
	 *  Spur und damit schlechter als der Trace selbst. Gemessen: echte Fahrbahn
	 *  < 1 m, Brueckendeck 6 m ueber dem Grund darunter - 10 m trennt beides. */
	double MaxLaneDeviationCm = 1000.0;

	bool bLaneIndexBuilt = false;
	TArray<FVector> LanePt;        // alle Stuetzpunkte aller Spuren
	TArray<int32> LaneNext;        // Folgepunkt auf DERSELBEN Spur (INDEX_NONE am Ende)
	TMap<int64, TArray<int32>> LaneCells;   // 200-m-Zelle -> Punktindizes

	/**
	 * Abstand (cm) zur naechsten Spur, deren Spursegment in TravelDir zeigt -
	 * oder MaxDistCm, wenn es in Reichweite keine gibt.
	 *
	 * WOFUER: Die Linie folgt der OSM-Mitte der Fahrbahn, und der feste
	 * Rechtsversatz setzt den Bus auf die rechte Seite DIESER Linie. Die Richtung
	 * eines OSM-Wegs ist aber die des Wegs, nicht die unserer Fahrt: in Abschnitten
	 * mit umgekehrter Wegrichtung landete der Bus damit auf der Gegenspur (gemeldet
	 * als "beide Richtungen in derselben Spur"). Diese Messung entscheidet die
	 * Seite aus den Spurdaten (FRoadLane.Centerline ist in FAHRTRICHTUNG sortiert)
	 * statt aus einer Annahme. Dieselbe Zellenabfrage wie RoadSurfaceZ.
	 */
	double AlignedLaneDistanceCm(double X, double Y, const FVector& TravelDir, double MaxDistCm) const;

	/**
	 * Spurseite je Wagen (+1 = rechts der Fahrtrichtung, -1 = links). Gemerkt,
	 * damit die Seite nur bei deutlichem Vorsprung wechselt - sonst springt der
	 * Bus an Kreuzungen zwischen zwei parallelen Spuren hin und her.
	 */
	TArray<double> SlotLaneSide;
	/** Vorsprung, ab dem die Spurseite wechselt (cm). */
	float LaneSideHysteresisCm = 150.0f;
	/** So weit wird je Seite nach einer passenden Spur gesucht (cm). */
	float LaneSideProbeCm = 450.0f;

	/**Zellen-Schluessel (200-m-Raster). */
	static int64 LaneCellKey(double X, double Y)
	{
		const int64 CX = (int64)FMath::FloorToDouble(X / 20000.0);
		const int64 CY = (int64)FMath::FloorToDouble(Y / 20000.0);
		return (CX * 1000003LL) ^ CY;
	}

	/** Feste Wagen-Nummer eines Slots (-1 = leer). Im Log steht die Nummer, nicht
	 *  der Pool-Slot: bei zwei Linien war "Bus 2" nicht mehr zuordenbar. */
	int32 WagonId(int32 Slot) const
	{
		return SlotVehicleId.IsValidIndex(Slot) ? SlotVehicleId[Slot] : -1;
	}

	bool bGroundAudit = false;
	/** Abstand zweier Proben in cm (Default 25 m). */
	double GroundAuditStepCm = 2500.0;
	/** Zuletzt beprobte Bogenlaenge je Slot (eine Probe je 25 m und Fahrt). */
	TArray<double> AuditLastArcCm;
	int32 AuditSamples = 0;
	int32 AuditOnRoad = 0;        // oberste Flaeche = Fahrbahn-Kollision
	int32 AuditOnTerrain = 0;     // nur Landscape -> unter dem Belag
	int32 AuditOnOther = 0;       // Requisite/etwas anderes
	int32 AuditNested = 0;        // unter der obersten Flaeche liegt eine weitere
	double AuditNestedMinCm = 0.0;
	double AuditNestedMaxCm = 0.0;
	// Abstand der Fahrzeug-Unterkante zur FAHRBAHN des Strassennetzes.
	int32 AuditLaneSamples = 0;   // Proben mit einer Spur in Reichweite
	int32 AuditLaneBelow = 0;     // Fahrzeug lag UNTER der Fahrbahn
	int32 AuditLaneOn = 0;        // Fahrzeug lag auf der Fahrbahn (+-5 cm)
	double AuditLaneMinCm = 0.0;  // kleinster (negativster) Abstand
	double AuditLaneMaxCm = 0.0;
	double AuditLaneSumCm = 0.0;
	/** Die schlimmsten Spur<->Gelaende-Abweichungen mit Bogenlaenge und Wagen: nur so
	 *  ist belegbar, WO welche Spur getroffen wurde (Summenwerte allein nicht). */
	struct FWorstDev { double DevCm = 0.0; FString Text; };
	TArray<FWorstDev> AuditWorst;
	void NoteDeviation(double DevCm, const FString& Text);
	int32 AuditFallbacks = 0;     // Proben ohne brauchbare Spur (Gelaende-Trace)
	double AuditFallbackMaxCm = 0.0;   // groesste verworfene Abweichung dabei
	TMap<FString, int32> AuditSurfaces;   // Name der obersten getroffenen Komponente
	TArray<FString> AuditNotes;           // erste Auffaelligkeiten im Klartext
	double AuditWriteAccum = 0.0;
	// Woher kam die Hoehe: Strassennetz (Asphalt) oder Gelaende-Trace?
	int32 AuditPlacedOnLane = 0;
	int32 AuditPlacedOnTrace = 0;
	// Proben, in denen die FAHRBAHN-Kollision der Stadt getroffen wurde, und
	// ihr Abstand zur Spur des Strassennetzes (soll 0 sein).
	int32 AuditRoadHits = 0;
	double AuditRoadVsLaneMaxCm = 0.0;
	double AuditRoadVsLaneSumCm = 0.0;
	// Abgedeckter Bogenbereich der Linie (Beleg, dass die ganze Strecke beprobt wurde).
	double AuditArcMinCm = 0.0;
	double AuditArcMaxCm = 0.0;
};
