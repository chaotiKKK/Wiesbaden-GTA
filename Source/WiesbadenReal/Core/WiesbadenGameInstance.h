// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"

#include "Core/WiesbadenCityData.h"
#include "GIS/WiesbadenBuildSummary.h"

#include <atomic>

#include "WiesbadenGameInstance.generated.h"

class AWiesbadenCityActor;
class UMaterialInterface;
class UBuildingGenerator;
class UWiesbadenRegionAssetGenerator;
class UWiesbadenRegionGenerator;
class UGeoCoordinateConverter;
class UHeightmapImporter;
class UOSMDataParser;
class URoadNetworkGenerator;
class URoadTypeLibrary;
class UTerrainGenerator;
class URoadFurnitureGenerator;

/**
 * Session-weiter Halter der Stadt-Daten und -Konfiguration.
 *
 * Der GameInstance ueberlebt Levelwechsel - damit laeuft die teure
 * GIS-Pipeline (OSM parsen, DEM importieren, Strassen/Gebaeude/Terrain
 * generieren) nur EINMAL pro Session. Das WorldSubsystem (je Welt) konsumiert
 * die fertigen Daten und spawnt daraus die Geometrie.
 *
 * LAUFZEIT-BUILD: LoadCityDataAsync() fuehrt die Pipeline auf einem
 * Worker-Thread aus (reine Datenverarbeitung, keine Engine-Objekte). Erst die
 * Fortsetzung FinalizeCityLoad() laeuft wieder auf dem Game-Thread und
 * uebernimmt die Ergebnisse per Move. Das Ergebnis wird ueber
 * OnCityDataReady (Blueprints) und OnCityDataLoadedNative (C++) gemeldet.
 *
 * PRODUKTION: Fuer das ausgelieferte Spiel wird die Stadt in einer
 * World-Partition-Map gebacken und bGenerateAtRuntime = false gesetzt - dann
 * findet kein Laufzeit-Build statt und World Partition streamt die Zellen.
 *
 * KONFIGURATION: Alle UPROPERTY(Config)-Felder sind ueber DefaultGame.ini
 * bzw. die Project Settings setzbar.
 */
UCLASS(Config = Game, defaultconfig, BlueprintType)
class WIESBADENREAL_API UWiesbadenGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// -- Konfiguration --------------------------------------------------------

	/** OSM-Datei fuer den Laufzeit-Build (.osm/.xml oder Overpass-.json). */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Daten")
	FString OsmFilePath;

	/** Textuelle Stadtbeschreibung (regelbasiert in Pipeline-Parameter uebersetzt). */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Daten")
	FString CityPrompt;

	/** Wenn true, wird ein DEM importiert (Terrain + Terrainhoehen der Bebauung). */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Daten")
	bool bImportDem = true;

	/** DEM-Datei (.asc = ESRI ASCII Grid, .hgt = SRTM-Kachel). */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Daten", meta = (EditCondition = "bImportDem"))
	FString DemFilePath;

	/** Pfad zur RoadTypeLibrary-JSON. Leer = Projektdefault (RASt 06/RAA). */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Konfiguration")
	FString RoadTypeConfigPath;

	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Ausgabe")
	bool bGenerateRoads = true;

	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Ausgabe")
	bool bGenerateBuildings = true;

	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Ausgabe")
	bool bGenerateTerrain = true;

	/** Strassenausstattung (Schilder, Leitpfosten, Halt-/Wartelinien) erzeugen. */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Ausgabe")
	bool bGenerateFurniture = true;

	/** Regionen-abhaengige Assets (Baeume, Ufer, Industrie) erzeugen. */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Ausgabe")
	bool bGenerateRegionAssets = true;

	/**
	 * Stadt zur Laufzeit aus OSM/DEM erzeugen.
	 *  - true  (Entwicklung/PIE): Laufzeit-Build beim Spielstart.
	 *  - false (Produktion):      Stadt liegt als gebackene World-Partition-Map
	 *                             im Level; es wird nichts generiert.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Ausgabe")
	bool bGenerateAtRuntime = true;

	/** Kollision fuer die erzeugten Procedural-Meshes (teuer bei grossen Netzen). */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Ausgabe")
	bool bCreateCollision = false;

	/** Orthometrische Hoehe in Metern, die auf Unreal Z = 0 gelegt wird. */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Georeferenz")
	double VerticalReferenceMeters = 75.0;

	/** Aufloesung des Terrain-Vorschau-Meshes zur Laufzeit. */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Terrain", meta = (ClampMin = "2", ClampMax = "2049"))
	int32 TerrainPreviewGridSize = 257;

	// -- Materialien (Soft-Refs, damit sie per INI/Config gesetzt werden) -----

	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Materialien")
	TSoftObjectPtr<UMaterialInterface> RoadMaterial;

	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Materialien")
	TSoftObjectPtr<UMaterialInterface> SidewalkMaterial;

	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Materialien")
	TSoftObjectPtr<UMaterialInterface> BuildingWallMaterial;

	/**
	 * Per-Adress-Override: Adresse (addr:street + addr:housenumber, z. B.
	 * "Mainzer Strasse 129") -> eigenes Fassaden-Material. Die Schluessel
	 * bilden gleichzeitig die FacadeOverrideAddresses des Generators.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Materialien")
	TMap<FString, TSoftObjectPtr<UMaterialInterface>> AddressFacadeMaterials;

	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Materialien")
	TSoftObjectPtr<UMaterialInterface> BuildingRoofMaterial;

	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Materialien")
	TSoftObjectPtr<UMaterialInterface> TerrainMaterial;

	/** Basismaterial der Schild-Tafel (Textur wird als Parameter gesetzt). */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Materialien")
	TSoftObjectPtr<UMaterialInterface> SignMaterial;

	/** Content-Ordner der Schild-Texturen (Sign_<VzKat>.png). */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Materialien")
	FString SignTextureFolder = TEXT("/Game/Textures/TrafficSigns/");

	/** Radius der World-Partition-Streaming-Quelle um den Spieler, in Metern. */
	UPROPERTY(EditAnywhere, Config, Category = "Wiesbaden|Streaming")
	float StreamingRadiusMeters = 2000.0f;

	// -- Laufzeit-API ----------------------------------------------------------

	/** True, wenn die Stadt-Daten geladen und verfuegbar sind. */
	bool HasCityData() const { return CityData.IsValid() && CityData->bReady; }

	/** Zugriff auf die Stadt-Daten (nur nach HasCityData() lesen). */
	TSharedPtr<FWiesbadenCityData> GetCityData() const { return CityData; }

	/** True, solange ein Hintergrund-Load laeuft. */
	bool IsCityDataLoading() const { return bLoadInProgress.load(); }

	/** Ob die Stadt zur Laufzeit generiert werden soll. */
	bool ShouldGenerateAtRuntime() const { return bGenerateAtRuntime; }

	/**
	 * Startet den asynchronen Ladevorgang (idempotent). Bei Erfolg wird
	 * OnCityDataReady / OnCityDataLoadedNative ausgeloest.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden")
	void LoadCityDataAsync();

	/** Aktueller Status der Stadt-Daten (z. B. "Stadt geladen: ..."). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetCityStatus() const;

	/** Leer bei Erfolg, sonst die Abbruchursache des letzten Ladevorgangs. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetLastCityError() const;

	/**
	 * Ergebnis der Terrain-Qualitaetskontrolle des letzten Laufzeit-Builds
	 * (leere WarningMessage = ok). Zusaetzlich zur Status-Zeile
	 * (GetCityStatus) als strukturierter Getter abrufbar.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FTerrainQualityReport GetTerrainQuality() const;

	/**
	 * Letzter Laufzeit-Build als gemeinsame USTRUCT (Zeitpunkt/Dauer/
	 * Ergebnis; leer, wenn keiner gelaufen). Die Einzel-Getter darunter sind
	 * Komfort-Wrapper auf diese eine Quelle.
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

	/**
	 * Kompakter Einzeiler des letzten Laufzeit-Builds
	 * ("Letzter Build: 83.4 s, ok (2026-08-16 15:45:00)") - gleiche
	 * Formatierung wie der WorldBuilder (Details-Panel). Leer, wenn kein Build
	 * gelaufen ist.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden")
	FString GetLastBuildSummary() const;

	/** Uebertraegt die konfigurierten Materialien auf den Stadt-Actor. */
	void ApplyMaterialsToCityActor(AWiesbadenCityActor* CityActor) const;

	// -- Ereignisse ------------------------------------------------------------

	/** Internes C++-Ereignis (das City-Subsystem wartet darauf). */
	DECLARE_MULTICAST_DELEGATE(FOnCityDataLoadedNative);
	FOnCityDataLoadedNative OnCityDataLoadedNative;

	/** Blueprint-Ereignis: Stadt-Daten sind geladen (bReady oder Fehler). */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWiesbadenCityDataReady);
	UPROPERTY(BlueprintAssignable, Category = "Wiesbaden")
	FOnWiesbadenCityDataReady OnCityDataReady;

protected:
	virtual void Shutdown() override;

private:
	/** Thread-sicherer Kontext eines Hintergrund-Loads (Definition in .cpp). */
	struct FCityLoadContext;

	/** Erzeugt die Pipeline-Objekte und rootet sie waehrend des Builds. */
	void CreatePipelineObjects();

	/** Gibt die Pipeline-Objekte wieder frei (nach dem Build). */
	void ReleasePipelineObjects();

	/**
	 * Worker-Thread: parst OSM/DEM und generiert die Stadt.
	 * Statisch, weil der Worker keinen Member-Zustand beruehren darf (nur den
	 * Kontext). Die Fortsetzung wird auf dem Game-Thread eingeplant
	 * (FinalizeCityLoad ueber Context->Owner).
	 */
	static void RunCityLoadPipeline(const TSharedRef<FCityLoadContext, ESPMode::ThreadSafe>& Context);

	/** Game-Thread: uebernimmt die Ergebnisse und meldet das Lade-Ende. */
	void FinalizeCityLoad(const TSharedRef<FCityLoadContext, ESPMode::ThreadSafe>& Context);

	void BroadcastCityDataReady();

	// Pipeline-Objekte. Im Game-Thread mit dem Transient-Package als Outer
	// erzeugt (keine GI-Subobjects - EndPlayMap markiert gerootete
	// GI-Subobjects beim PIE-Ende als Garbage, Assert !IsRooted()) und fuer
	// die Dauer des Hintergrund-Builds gerootet (AddToRoot), damit GC sie
	// nicht einsammelt. Freigabe in ReleasePipelineObjects() nach dem Build
	// bzw. im Shutdown nach dem Worker-Join.

	UPROPERTY(Transient)
	UGeoCoordinateConverter* PipelineConverter = nullptr;

	UPROPERTY(Transient)
	UOSMDataParser* PipelineParser = nullptr;

	UPROPERTY(Transient)
	UHeightmapImporter* PipelineImporter = nullptr;

	UPROPERTY(Transient)
	URoadTypeLibrary* PipelineTypeLibrary = nullptr;

	UPROPERTY(Transient)
	URoadNetworkGenerator* PipelineRoadGenerator = nullptr;

	UPROPERTY(Transient)
	UBuildingGenerator* PipelineBuildingGenerator = nullptr;

	UPROPERTY(Transient)
	UWiesbadenRegionGenerator* PipelineRegionGenerator = nullptr;

	/** Pipeline-Objekt fuer den Regionen-Asset-Pass (Baeume, Ufer, Industrie). */
	UPROPERTY(Transient)
	UWiesbadenRegionAssetGenerator* PipelineRegionAssetGenerator = nullptr;

	UPROPERTY(Transient)
	UTerrainGenerator* PipelineTerrainGenerator = nullptr;

	UPROPERTY(Transient)
	URoadFurnitureGenerator* PipelineFurnitureGenerator = nullptr;

	/** Die Stadt-Daten dieser Session (bleiben ueber Levelwechsel erhalten). */
	TSharedPtr<FWiesbadenCityData> CityData;

	/** True, solange ein Hintergrund-Load laeuft. */
	std::atomic<bool> bLoadInProgress{false};

	/**
	 * Aktiver Hintergrund-Build. Wird im Shutdown genutzt, um den Worker
	 * abzubrechen und auf sein Ende zu warten, BEVOR die Pipeline-Objekte aus
	 * dem Root-Set genommen werden (Definition des Typs in der .cpp).
	 */
	TSharedPtr<FCityLoadContext, ESPMode::ThreadSafe> ActiveBuildContext;

	/** Verhindert Broadcasts/Zugriffe waehrend des Shutdowns. */
	bool bIsShuttingDown = false;
};
