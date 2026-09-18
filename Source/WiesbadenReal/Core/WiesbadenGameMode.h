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
struct FWiesbadenRoadClearance;

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
	 * Standplaetze fuer das Standstueck, in der Reihenfolge des Vorzugs
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
	 * Das ALTE Heli-Modell als Standstueck neben den Spielerheli stellen.
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
	 * Standstueck wird im ersten Bild gesetzt, da ist noch keine Stadtkachel
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
		const FWiesbadenRoadClearance& Carriageway, double& OutGroundZ) const;

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
	 * 17.09.2026); das alte Landmarken-Modell steht daneben als Standstueck
	 * (AWiesbadenLegacyHelicopter).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Spieler")
	bool bSpawnHelicopter = true;

	/** Standstueck (altes Heli-Modell) neben den Spielerheli stellen. */
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

	/** Helikopter am Startpunkt. */
	UPROPERTY(Transient)
	class AWiesbadenHelicopter* PlayerHelicopter = nullptr;

	/** Standstueck: das alte Heli-Modell neben dem Spielerheli. */
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

	/** Spielzeit seit BeginPlay in Sekunden. */
	float ElapsedSeconds = 0.0f;

	/** World-Subsystem-Cache (GC-verfolgt, in BeginPlay bezogen). */
	UPROPERTY(Transient)
	UWiesbadenCitySubsystem* CitySubsystem = nullptr;
};
