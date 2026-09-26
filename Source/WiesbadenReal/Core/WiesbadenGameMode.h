// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/TerrainGenerator.h"
#include "GIS/WiesbadenBuildSummary.h"

#include "WiesbadenGameMode.generated.h"

class UWiesbadenCitySubsystem;
class AWiesbadenStoreMerchant;
class AWiesbadenPlatterParking;
struct FWiesbadenRoadClearance;
struct FWiesbadenBuildingClearance;

/**
 * GameMode des Wiesbaden-Core-Moduls.
 *
 * Eigentlicher Orchestrierer der Stadt ist das UWiesbadenCitySubsystem (je
 * Welt); der GameMode
 *  - stoesst die Stadt-Initialisierung an (idempotent, auch per Blueprint),
 *  - leitet den Stadt-Status an Blueprints weiter (OnCityStatus),
 *  - bietet Blueprint-Getter fuer Bereitschaft, Streaming-Zustand und Fehler.
 *
 * Der Default-Pawn ist bewusst nicht fest verdrahtet (Spieler-System ist Phase
 * 6 der Spezifikation) - DefaultPawnClass kann in der World-Settings bzw. per
 * Blueprint-Unterklasse gesetzt werden.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWiesbadenGameMode();

	/** Initialisiert die Stadt (idempotent) - Delegiert ans City-Subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden")
	void InitializeCity();

	/** True, wenn die Stadt-Geometrie gespawnt wurde. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	bool IsCityReady() const;

	/** True, wenn alle World-Partition-Zellen um den Player geladen sind. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	bool IsCityStreamingComplete() const;

	/** True, wenn die Welt eine World-Partition-Welt ist. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	bool IsWorldPartitionActive() const;

	/** Aktueller Status der Stadt. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetCityStatus() const;

	/** Leer bei Erfolg, sonst die letzte Fehlermeldung. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetLastCityError() const;

	/**
	 * Letzter Laufzeit-Build als gemeinsame USTRUCT (delegiert an das
	 * CitySubsystem; leer, wenn keiner gelaufen). Die Einzel-Getter darunter
	 * sind Komfort-Wrapper auf diese eine Quelle.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FLastBuildInfo GetLastBuildInfo() const;

	/** Zeitpunkt des letzten Laufzeit-Builds (lokal); leer, wenn keiner gelaufen. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetLastBuildTimestamp() const;

	/** Dauer des letzten Laufzeit-Builds in Sekunden (0 = keiner gelaufen). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	double GetLastBuildDurationSeconds() const;

	/** Ergebnis des letzten Laufzeit-Builds ("ok" / "fehlgeschlagen: ..."). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetLastBuildResult() const;

	/** Kompakter Einzeiler des letzten Laufzeit-Builds (leer, wenn keiner). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetLastBuildSummary() const;

	/**
	 * Ergebnis der Terrain-Qualitaetskontrolle des letzten Laufzeit-Builds
	 * (leere WarningMessage = ok). Delegiert null-sicher ans City-Subsystem;
	 * damit erreichen Level-Blueprints die Warnung ohne GameInstance-Zugriff.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FTerrainQualityReport GetTerrainQuality() const;

	/** Blueprint-Ereignis: Stadt-Status hat sich geaendert. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWiesbadenCityStatus, FString, Status, bool, bSuccess);
	UPROPERTY(BlueprintAssignable, Category = "Wiesbaden")
	FOnWiesbadenCityStatus OnCityStatus;

	/**
	 * Naechster Haendler aus einer Kandidatenliste, dessen EIGENE Reichweite den
	 * Standort einschliesst.
	 *
	 * Bewusst ohne Welt: die Regel "der naechste gewinnt, aber nur innerhalb
	 * seiner eigenen Reichweite" ist so ohne Spielsitzung pruefbar. Die
	 * Weltsuche steckt in FindMerchantInReach.
	 *
	 * @return Der naechste passende Haendler oder nullptr.
	 */
	static AWiesbadenStoreMerchant* PickMerchantInReach(
		const TArray<AWiesbadenStoreMerchant*>& Merchants, const FVector& FromLocation);

	/**
	 * Standabstand zweier gleich ausgerichteter Helikopter in einer Reihe, cm.
	 * Datenrein und damit ohne Welt pruefbar (WiesbadenReal.Vehicles.HeliStandAbstand):
	 * die halben Rotordurchmesser plus 5 m Luft, mindestens die halben
	 * Rumpflaengen plus 2 m Luft.
	 *
	 * Beide Bedingungen sind noetig, keine ist Zierde: die Rotorscheiben der
	 * beiden Maschinen liegen nur rund 30 cm uebereinander (Ka-52 unten 3,77 m
	 * gegen das alte Modell oben 3,45 m) und wuerden sich bei zu kleinerem
	 * Abstand durchdringen; stehen die Rumpfspitzen aufeinander zu, entscheidet
	 * dagegen die Laenge.
	 */
	static double ComputeHelicopterStandDistanceCm(
		double OwnDiscCm, double LegacyDiscCm, double OwnLengthCm, double LegacyLengthCm);

	/**
	 * Standplaetze fuer eine Maschine, in der Reihenfolge des Vorzugs
	 * (datenrein, ohne Welt pruefbar: WiesbadenReal.Vehicles.HeliStandplaetze).
	 *
	 * Der erste Eintrag ist der bisherige Platz - geradeaus vor dem Spielerheli
	 * im gerechneten Abstand. Er wurde BLIND gesetzt: fuehrt dort eine Strasse
	 * entlang, stand die Maschine auf der Fahrbahn. Die weiteren Eintraege
	 * weichen faecherfoermig aus (erst seitlich im selben Abstand, dann weiter
	 * weg), damit ein Ausweichen so wenig wie moeglich an der gewohnten
	 * Aufstellung aendert.
	 *
	 * @param Anchor          Standort des Spielerhelis.
	 * @param ForwardYawDeg   Blickrichtung des Spielerhelis in Grad.
	 * @param StandDistanceCm Gerechneter Standabstand.
	 */
	static TArray<FVector> BuildHelicopterStandCandidates(
		const FVector& Anchor, double ForwardYawDeg, double StandDistanceCm);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Setzt das Spielerfahrzeug an der Startadresse ein und uebergibt die
	 * Steuerung. Wird nach dem Bereitstehen der Stadt aufgerufen; ein
	 * erneuter Aufruf ist wirkungslos, solange das Fahrzeug lebt.
	 * @return true, wenn das Fahrzeug steht und besessen ist.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Spieler")
	bool SpawnPlayerCarAtStartAddress();

	/**
	 * Streamt VOR der Startplatz-Bodensuche die World-Partition-Zellen um den
	 * Startort blockierend ein (Kollision der Fahrbahn), damit der Boden-Trace
	 * die Asphalt-Kollision trifft und nicht das ~1,5 m tiefere Terrain.
	 *
	 * Ursache des "auf Schienen / im Boden"-Problems: Die Platzsuche tracete den
	 * Boden, bevor die Fahrbahn-Zelle gestreamt war, und setzte das Auto aufs
	 * Terrain darunter. Der Chaos-Kaefer sackte damit unter die Strasse und kam
	 * nicht ueber 6 km/h. Block-Load raeumt die Rennbedingung an der Wurzel aus.
	 *
	 * @return true, wenn das Streaming abgeschlossen ist (oder Welt nicht
	 *         partitioniert); false bei Zeitlimit.
	 */
	bool BlockLoadSpawnCell(UWorld* World, const FVector& Location);

	/**
	 * Setzt den Helikopter neben dem Startpunkt ab.
	 *
	 * Die Klasse AWiesbadenHelicopter war samt Rotorphysik, Audio und Tests
	 * fertig - sie wurde nur nie in die Welt gesetzt. Ohne diesen Aufruf ist
	 * sie im Spiel schlicht nicht vorhanden.
	 */
	bool SpawnHelicopterNearStart();

	/**
	 * Den ZWEITEN fliegbaren Hubschrauber neben den ersten stellen.
	 *
	 * Der Spielerheli traegt seit dem Ka-52-Neubau (17.09.2026) das importierte
	 * Modell; das frueher benutzte Landmarken-Modell steht daneben als Ansicht -
	 * ohne Fluglogik, damit es nicht faellt und nicht besessen wird.
	 *
	 * Aufgestellt wird vom SPIELERHELI aus, nicht vom Auto: nur so stehen beide
	 * auf derselben Linie in der Strassenflucht, und der Abstand ist genau der
	 * gerechnete. Der Abstand kommt aus den beiden Rotorkreisen, sie liegen nur
	 * rund 30 cm uebereinander - ein zu kleiner Abstand zeigte ineinander
	 * stechende Rotoren.
	 */
	bool SpawnLegacyHelicopterNearStart();


	/**
	 * True, wenn an dieser Stelle KEINE Fahrbahn liegt und der Boden traegt.
	 *
	 * Die Fahrbahn kommt aus dem STRASSENNETZ, nicht aus der Kollision: das
	 * Platz wird im ersten Bild vergeben, da ist noch keine Stadtkachel
	 * gestreamt und ein Lot trifft nur die Landschaft. Die Hoehe kommt weiterhin
	 * aus dem Lot. Abgetastet wird der ganze RUMPF-Grundriss, nicht nur die
	 * Mitte - sonst steht die Maschine mit der Nase auf der Strasse.
	 *
	 * @param Point       Zu pruefender Standort (Z beliebig).
	 * @param FootprintCm Halbe Rumpflaenge als Grundriss-Radius.
	 * @param Carriageway Fahrbahn-Index um den Ankerpunkt.
	 * @param OutGroundZ  Hoehe der Aufstandsflaeche, wenn der Platz frei ist.
	 */
	bool IsHelicopterStandFree(
		const FVector& Point, double FootprintCm,
		const FWiesbadenRoadClearance& Carriageway,
		const FWiesbadenBuildingClearance& Buildings, double& OutGroundZ) const;

	/**
	 * Sucht den ersten freien Standplatz aus einer Vorzugsliste.
	 *
	 * Fuer BEIDE Hubschrauber. Der Ka-52 hatte bisher gar keine Pruefung - er
	 * wurde vor das Auto gesetzt, ein Lot fuer die Hoehe, fertig. Dass er
	 * heute frei steht, ist Glueck und keine Zusage: dieselbe Rechnung setzt
	 * ihn an einer anderen Startadresse in eine Wand.
	 *
	 * Geprueft wird gegen Fahrbahn UND Gebaeude. Beide Indizes stammen aus
	 * serialisierten Daten am WorldBuilder, nicht aus der Kollision - im
	 * ersten Bild ist noch keine Stadtkachel gestreamt.
	 *
	 * @param Candidates   Standplaetze in der Reihenfolge des Vorzugs.
	 * @param FootprintCm  Grundriss-Radius der Maschine.
	 * @param Wofuer       Name fuer das Protokoll.
	 * @param OutLocation  Gewaehlter Platz samt Bodenhoehe (Z aus dem Lot).
	 * @param OutIndex     Welcher Kandidat es wurde (Diagnose).
	 */
	bool FindFreeHelicopterStand(
		const TArray<FVector>& Candidates, double FootprintCm,
		const TCHAR* Wofuer, FVector& OutLocation, int32& OutIndex) const;

	/**
	 * Wechselt zwischen Fahrzeug und zu Fuss (Taste F).
	 *
	 * Am Steuer: der Spieler steigt neben dem Fahrzeug aus. Zu Fuss: das
	 * naechstgelegene Fahrzeug innerhalb von EntryRadiusMeters wird uebernommen.
	 */
	void TogglePlayerVehicle();

	/**
	 * Prueft NPC-Haendler-Interaktion fuer einen fuß-Pawn.
	 *
	 * Die Tastenflanke wertet der Tick aus; hier wird nur noch gesucht und
	 * delegiert.
	 */
	bool TryMerchantInteraction(class AWiesbadenFootPawn* FootPawn);

	/** F vor Dennos Laden: Lieferauftrag annehmen (true = Taste beansprucht). */
	bool TryDennoDelivery(class AWiesbadenFootPawn* FootPawn);

	/** Naechster Haendler, dessen eigene Reichweite den Fuss-Pawn einschliesst. */
	class AWiesbadenStoreMerchant* FindMerchantInReach(const APawn& FootPawn) const;

	/** Findet das naechste uebernehmbare Fahrzeug um eine Position. */
	APawn* FindNearbyVehicle(const FVector& Location) const;

	/**
	 * Uebernimmt das naechstgelegene VERKEHRSFAHRZEUG.
	 *
	 * Der Verkehr besteht aus Instanzen einer gemeinsamen Komponente und hat
	 * keine eigenen Actors - "einsteigen" heisst deshalb: das Fahrzeug aus der
	 * Simulation nehmen und an seiner Stelle ein fahrbares Auto absetzen.
	 *
	 * @return Der neue Wagen, oder nullptr wenn keiner in Reichweite ist.
	 */
	APawn* CommandeerTrafficVehicle(const FVector& Location);

	/**
	 * Reichweite fuer das Uebernehmen eines Verkehrsfahrzeugs, in Metern.
	 *
	 * Groesser als EntryRadiusMeters: das eigene Auto steht dort, wo man es
	 * abgestellt hat, ein Verkehrsfahrzeug faehrt vorbei und will erwischt
	 * werden.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug", meta = (ClampMin = "1.0"))
	float TrafficEntryRadiusMeters = 9.0f;

	/**
	 * Startadresse des Spielerfahrzeugs. Vorgabe: Platter Strasse 144.
	 * Das Fahrzeug wird auf die naechstgelegene befahrbare Spur gesetzt,
	 * nicht exakt auf diesen Punkt - der liegt im Gebaeude.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Spieler")
	FGeoCoordinate PlayerStartAddress = FGeoCoordinate(8.2234186, 50.0932604, 0.0);

	/** Suchradius um die Startadresse in Metern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Spieler", meta = (ClampMin = "10.0"))
	double PlayerStartSearchRadiusMeters = 250.0;

	/** Wenn false, wird kein Fahrzeug eingesetzt (z. B. fuer Editor-Tests). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Spieler")
	bool bSpawnPlayerCar = true;

	/**
	 * Helikopter beim Start absetzen.
	 *
	 * Der Spielerheli ist das Ka-52-Modell (AWiesbadenHelicopter, seit dem Neubau
	 * 17.09.2026); das alte Landmarken-Modell steht daneben und ist ebenso fliegbar
	 * (AWiesbadenLegacyHelicopter).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Spieler")
	bool bSpawnHelicopter = true;

	/** Den zweiten Hubschrauber (altes Modell) neben den ersten stellen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Spieler")
	bool bSpawnLegacyHelicopter = true;

	/**
	 * Abstand des Helikopters zum Startpunkt in Metern.
	 *
	 * 12 m statt 40: bei 40 m lag er in einer Strassenschlucht regelmaessig
	 * hinter Haeusern und war schlicht nicht zu finden.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Spieler", meta = (ClampMin = "5.0"))
	float HelicopterDistanceMeters = 12.0f;

	/** Reichweite zum Einsteigen in Metern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Spieler", meta = (ClampMin = "1.0"))
	float EntryRadiusMeters = 6.0f;

private:
	/** Leitet den Subsystem-Status an OnCityStatus weiter. */
	UFUNCTION()
	void HandleCityStatus(FString Status, bool bSuccess);

	/** Aktuell eingesetztes KINEMATISCHES Spielerfahrzeug (Kaefer). Bleibt nullptr,
	 *  wenn das Chaos-Physik-Auto faehrt; Kaefer-spezifische Pfade (Verkehr
	 *  uebernehmen) haengen weiter hieran. */
	UPROPERTY(Transient)
	class AWiesbadenCar* PlayerCar = nullptr;

	/** Das tatsaechlich besessene Spielerauto - Chaos-Physik-Auto ODER Kaefer.
	 *  Fahrzeug-typ-unabhaengige Pfade (Helikopter-Bezug, Wiedereinstieg,
	 *  Idempotenz des Einsetzens) nutzen diesen Zeiger statt PlayerCar. */
	UPROPERTY(Transient)
	TObjectPtr<APawn> PlayerVehicle = nullptr;

	/** Die runtime gebaute Anlage an der Standard-Startadresse. Der Actor ist
	 * waehrend GameMode::BeginPlay noch nicht per ActorIterator sichtbar. */
	UPROPERTY(Transient)
	TObjectPtr<AWiesbadenPlatterParking> PlatterParking = nullptr;

	/** Helikopter am Startpunkt. */
	UPROPERTY(Transient)
	class AWiesbadenHelicopter* PlayerHelicopter = nullptr;

	/** Der zweite fliegbare Hubschrauber: das alte Modell. */
	UPROPERTY(Transient)
	class AWiesbadenLegacyHelicopter* LegacyHelicopter = nullptr;

	/** Spielerfigur zu Fuss - entsteht beim ersten Aussteigen. */
	UPROPERTY(Transient)
	class AWiesbadenFootPawn* FootPawn = nullptr;

	/** Flankenerkennung der Ein/Aussteigen-Taste. */
	bool bEntryKeyHeld = false;

	/**
	 * Sekunden bis zum selbsttaetigen Aussteigen (-WbZuFuss=<Sekunden>).
	 *
	 * Nur zum Nachsehen: das Spiel beginnt am Steuer, und ohne Tastendruck
	 * bekommt man die Spielerfigur nie zu Gesicht. Ein Bild mit -WbShot zeigt
	 * dann immer nur das Auto. Negativ heisst: ausgeschaltet.
	 */
	float OnFootAfterSeconds = -1.0f;

	/** Schon ausgestiegen? Sonst geschaehe es in jedem Bild erneut. */
	bool bOnFootDone = false;

	/**
	 * Ego-Pruef-Lauf (-WbEgoProbe=<Sekunden>).
	 *
	 * Skriptbarer Ersatz fuer die C-Taste und die Zifferntasten 1-9: Tasten-
	 * Injektion erreicht das D3D-Fenster nicht, also faehrt der GameMode die
	 * KAMMERAD-KONVENTION hier ab: aussteigen (-WbZuFuss davor), Ansicht Ego,
	 * Bild, Ansicht Schulter, Bild, Waffen 0-8 je kurz gewaehlt und im Log
	 * verifiziert (Name + Masse gegen die Tabelle). Ergebnis: vier Bilder in
	 * Saved/Diagnose + Log-Marker je Schritt.
	 */
	float EgoProbeAfterSeconds = -1.0f;
	bool bEgoProbeDone = false;
	float EgoProbeElapsed = 0.0f;
	int32 EgoProbeStep = 0;
	float EgoProbeStepElapsed = 0.0f;

	/** Fuehrt einen Schritt des Ego-Pruef-Laufs aus (Tick). */
	void TickEgoProbe(float DeltaSeconds);

	/**
	 * Figur-Pruef-Lauf (-WbFigurProbe, mit -WbZuFuss): steht, geht, rennt,
	 * springt, dreht und duckt sich per SIMULIERTER Taste (W, Umschalt,
	 * Leertaste, Pfeil rechts, X) - der Pawn kennt keine Probe, er sieht nur
	 * Tasten. Zuletzt legt sie eine niedrige Platte ueber die geduckte Figur,
	 * laesst X los (die Figur muss geduckt bleiben) und nimmt die Platte weg
	 * (jetzt muss sie aufstehen).
	 *
	 * -WbFigurProbe=Boden: Aussteigen am Hang (mit -WbGoto=<Hangstrasse>),
	 * Figur 60/150/250 cm ins Gelaende setzen (muss wieder hochkommen), geduckt
	 * ins Auto und wieder aus (muss stehen), geduckt Mitfahrt beginnen.
	 * -WbFigurProbe=Treppe: der echte Fuss-Pawn geht per Tasten die Treppe des
	 * Sebbo-Turms hinauf (Wegpunkte von AWiesbadenSebboHq::GetStairWalk). Die Kamera
	 * schaut von schraeg vorn auf die Figur; je Phase ein Bild in
	 * Saved/Diagnose/figur_*.png, alle 0,5 s die gewaehlte Bewegung im Log.
	 */
	bool bFigurProbe = false;
	float FigurProbeTime = 0.0f;
	int32 FigurProbeShot = 0;
	float FigurProbeLogIn = 0.0f;
	TWeakObjectPtr<AActor> FigurProbeDecke;
	FString FigurProbeMode;
	float FigurProbeBodenZ = 0.0f;
	TArray<FVector> FigurProbeWeg;
	int32 FigurProbeWegIndex = -1;
	float FigurProbeWegZeit = 0.0f;
	FRotator FigurProbeBlick = FRotator::ZeroRotator;
	float FigurProbeStartFussZ = 0.0f;
	/** Bewegungen, deren Einblenden schon ein Bild bekam (je eine). */
	TSet<FString> FigurProbeBlendeBilder;

	/** Ein Bild des Figur-Pruef-Laufs (Tick). */
	void TickFigurProbe(float DeltaSeconds);

	/**
	 * Gamepad-Pruef-Lauf (-WbPadProbe, mit -WbZuFuss): spielt eine feste
	 * Sitzung auf dem Gamepad ab und belegt sie im Log - LT zielt (ADS),
	 * RT feuert, RB/LB wechseln die Waffe.
	 *
	 * Warum ueberhaupt eine Probe: die Belegungstabelle
	 * (Core/WiesbadenInputMap.h) ist im Unit-Test geprueft, sagt aber nichts
	 * darueber, ob der echte Weg durch PlayerInput, Pawn und Waffenkomponente
	 * auch wirklich ankommt. Genau diese Kette war nie belegt.
	 *
	 * Die Eingaben laufen als SIMULIERTE Tastenereignisse durch
	 * APlayerController::InputKey - derselbe Weg, den die Tastatur-Proben
	 * seit dem Sebbo-Haus nehmen. Der Pawn weiss nicht, dass er geprobt
	 * wird: er sieht nur Tasten. Das ist ein Testwerkzeug, kein Spielcode -
	 * ohne den Schalter passiert nichts.
	 */
	bool bPadProbe = false;
	float PadProbeTime = 0.0f;
	int32 PadProbeStep = 0;
	float PadProbeStepTime = 0.0f;
	int32 PadProbeSchuesseStart = 0;
	/** Hoehe der Figur beim Sprungschritt (Startwert, cm). */
	float PadProbeSprungZ = 0.0f;
	/** Hoechste erreichte Hoehe waehrend des Sprungs (cm). */
	float PadProbeSprungMaxZ = 0.0f;
	/** Ansichtszustand einmalig erfasst? (sonst wird er beim Umschalten mitgelesen). */
	bool PadProbeAnsichtErfasst = false;
	/** Ortspunkt beim Beginn einer Laufphase (L3-Probe). */
	FVector PadProbeLaufStart = FVector::ZeroVector;
	/** Ansicht vor dem Y-Druck (Ego oder Schulter). */
	bool PadProbeAnsichtVorher = false;
	/** Anzahl der bewerteten Schritte am Ende des Laufs. */
	int32 PadProbeSchritte = 12;
	/**
	 * Hat der laufende Schritt seine erwartete Wirkung erreicht? Die
	 * Tastenschritte warten darauf, statt nach einer festen Zeit zu
	 * urteilen: bei einem Hänger im Spiel fiel der Messpunkt sonst in
	 * eine Zeitlupe und die Probe meldete eine Wirkungslosigkeit, die
	 * es nicht gab.
	 */
	bool PadProbeBedingtErreicht = false;
	/** Strecke der letzten Laufphase in cm (L3-Probe). */
	float PadProbeLaufStrecke = 0.0f;
	/** Strecke mit L3 gedrueckt in cm (L3-Probe). */
	float PadProbeRennStrecke = 0.0f;
	/** Zoom-Stufe vor dem D-Pad-Schritt. */
	float PadProbeZoomVorher = 1.0f;
	/** Schnittwinkel vor dem D-Pad-Schritt mit Trennwaffe (Grad). */
	float PadProbeSchnittVorher = 0.0f;
	/**
	 * Waffenstand VOR dem Schultertasten-Druck. Einmal je Schritt lesen:
	 * der Schritt laeuft viele Bilder, und der Pawn schaltet im selben Bild,
	 * in dem der Druck ankommt. Jedes Bild neu gelesen ergaebe "2 -> 2" und
	 * liesse einen Waffenwechsel, der stattgefunden hat, als Fehler erscheinen.
	 */
	int32 PadProbeWaffeVorher = INDEX_NONE;
	/** Anzahl der Schritte, die ihre Erwartung erfuellt haben. */
	int32 PadProbeOk = 0;
	/** Anzahl der Schritte, die ihre Erwartung verfehlt haben. */
	int32 PadProbeFehl = 0;

	/** Ein Schritt des Gamepad-Pruef-Laufs (Tick). */
	void TickPadProbe(float DeltaSeconds);

	/** Spielzeit seit BeginPlay in Sekunden. */
	float ElapsedSeconds = 0.0f;

	/** World-Subsystem-Cache (GC-verfolgt, in BeginPlay bezogen). */
	UPROPERTY(Transient)
	UWiesbadenCitySubsystem* CitySubsystem = nullptr;
};
