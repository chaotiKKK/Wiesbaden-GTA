// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/TerrainGenerator.h"
#include "GIS/WiesbadenBuildSummary.h"

#include "WiesbadenGameMode.generated.h"

class UWiesbadenCitySubsystem;

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
	 * Wechselt zwischen Fahrzeug und zu Fuss (Taste F).
	 *
	 * Am Steuer: der Spieler steigt neben dem Fahrzeug aus. Zu Fuss: das
	 * naechstgelegene Fahrzeug innerhalb von EntryRadiusMeters wird uebernommen.
	 */
	void TogglePlayerVehicle();

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

	/** Helikopter beim Start absetzen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wiesbaden|Spieler")
	bool bSpawnHelicopter = true;

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
