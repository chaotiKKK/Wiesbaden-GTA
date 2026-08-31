// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Core/WiesbadenCityData.h"
#include "GIS/BuildingGenerator.h"
#include "GIS/WiesbadenBuildSummary.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/HeightmapImporter.h"
#include "GIS/OSMDataParser.h"
#include "GIS/RoadFurnitureGenerator.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/TerrainGenerator.h"
#include "GIS/WiesbadenPedestrianSimulation.h"
#include "GIS/WiesbadenTrafficLights.h"
#include "GIS/WiesbadenTrafficSimulation.h"
#include "GIS/WiesbadenCityChunking.h"
#include "GIS/WiesbadenRegion.h"
#include "GIS/WiesbadenRegionAssets.h"

#include "WiesbadenWorldBuilder.generated.h"

struct FLandscapeImportLayerInfo;

class UProceduralMeshComponent;
class UBillboardComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class ALandscape;
class URoadTypeLibrary;
class URegionAssetSpawnerComponent;
class URoadFurnitureSpawnerComponent;
class AWiesbadenCityChunk;

/**
 * Editor-Workflow: orchestriert die komplette GIS-Pipeline in einem Durchlauf.
 *
 * ABLAUF (BuildCity, Button "Build City" im Details-Panel)
 *  1. Georeferenzierung initialisieren (Wiesbaden-Origin oder benutzerdefiniert)
 *  2. OSM-Datei parsen (XML oder Overpass-JSON)
 *  3. DEM importieren (optional) und Hoehensampler erzeugen
 *  4. Strassennetz generieren (Geometrie + Fahrspur-Graph)
 *  5. Gebaeude generieren (Extrusion, Daecher, Multipolygone)
 *  6. Landscape-Heightmap erzeugen und unter Strassen/Gebaeuden einebnen
 *  7. Strassenausstattung platzieren (Schilder, Leitpfosten, Markierungen)
 *
 * Die Datenverarbeitung laeuft asynchron ueber die gemeinsame, daten-reine
 * Pipeline WiesbadenCityPipeline::BuildCityData() (Worker-Thread); nur die
 * Mesh-/Landscape-Erzeugung bleibt nach bDone auf dem Game-Thread. Der
 * Editor bleibt waehrend des Builds bedienbar (FScopedSlowTask pumpt Slate
 * und stellt den Abbrechen-Button bereit).
 *
 * Die erzeugte Geometrie wird als Procedural-Mesh-Sections auf drei
 * Komponenten angezeigt (Strassen, Gebaeude, Terrain). Die Landscape-
 * Heightmap selbst (FTerrainTile) kann wahlweise als echte ALandscape oder
 * als grobe Vorschau (TerrainPreviewGridSize) erzeugt werden.
 *
 * Alle Ergebnis-Reports liegen als read-only Properties vor, damit ein Lauf
 * im Details-Panel nachvollziehbar ist (Node-/Way-Zahlen, verworfen, Dauer).
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenWorldBuilder : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenWorldBuilder();

	// -- Datenquellen --------------------------------------------------------

	/** OSM-Datei (.osm/.xml oder Overpass-.json). */
	UPROPERTY(EditAnywhere, Category = "GIS|Daten", meta = (FilePathFilter = "OSM (*.osm;*.xml;*.json)|*.osm;*.xml;*.json"))
	FString OsmFilePath;

	/**
	 * Textuelle Stadtbeschreibung (z. B. "dichte Gruenderzeit-Innenstadt mit
	 * Marktkirche, wolkig") - regelbasiert in Pipeline-Parameter uebersetzt
	 * (CityPromptParser): Bebauungsdichte steuert die Mindest-Grundflaeche der
	 * Gebaeude, Fassadenstil/Landmarken/Wetter stehen in der Spec.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Daten")
	FString CityPrompt;

	/** Wenn true, wird ein DEM importiert (fuer Terrain und Terrainhoehen der Bebauung). */
	UPROPERTY(EditAnywhere, Category = "GIS|Daten")
	bool bImportDem = true;

	/** DEM-Datei (.asc = ESRI ASCII Grid, .hgt = SRTM-Kachel). */
	UPROPERTY(EditAnywhere, Category = "GIS|Daten", meta = (EditCondition = "bImportDem", FilePathFilter = "DEM (*.asc;*.txt;*.hgt)|*.asc;*.txt;*.hgt"))
	FString DemFilePath;

	/**
	 * Amtliche Gebaeudegrundrisse (ALKIS) in Overpass-JSON-Form, erzeugt mit
	 *   node Tools/alkis_extract.mjs <alkis.xml> Data/Raw/ALKIS/wiesbaden.alkis.json
	 *
	 * Gesetzt: die Gebaeude stammen aus ALKIS (vermessungsgenau, vollstaendig,
	 * mit amtlichem Gebaeudefunktionsschluessel). Das OSM-Extrakt liefert dann
	 * weiterhin Strassen, Regionen und Ausstattung.
	 * Leer: Gebaeude wie bisher aus OSM.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Daten", meta = (FilePathFilter = "ALKIS-JSON (*.json)|*.json"))
	FString AlkisFilePath;

	// -- Georeferenzierung ---------------------------------------------------

	/** Wiesbaden-Origin (Schlossplatz/Marktkirche) verwenden. */
	UPROPERTY(EditAnywhere, Category = "GIS|Georeferenz")
	bool bUseWiesbadenOrigin = true;

	/** Benutzerdefinierter Origin, falls bUseWiesbadenOrigin = false. */
	UPROPERTY(EditAnywhere, Category = "GIS|Georeferenz", meta = (EditCondition = "!bUseWiesbadenOrigin"))
	FGeoCoordinate CustomOrigin;

	/**
	 * Orthometrische Hoehe in Metern, die auf Unreal Z = 0 gelegt wird. Nur
	 * eine Zentrierung (Float-Praezision) - alle Generatoren verwenden denselben
	 * Wert, daher ist der Absolutwert fuer die Geometrie unerheblich.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Georeferenz")
	double VerticalReferenceMeters = 75.0;

	// -- Konfiguration -------------------------------------------------------

	/** Pfad zur RoadTypeLibrary-JSON. Leer = Projektdefault. */
	UPROPERTY(EditAnywhere, Category = "GIS|Konfiguration", meta = (FilePathFilter = "JSON (*.json)|*.json"))
	FString RoadTypeConfigPath;

	/**
	 * Parameter der Verkehrs-Simulation (Dichte, Spawn-Rate, Abstaende). Wird
	 * beim Editor-Build an die Pipeline durchgereicht und - als nicht-transiente
	 * Property - mit der gebackenen Map serialisiert, damit das CitySubsystem
	 * die Simulation im gebackenen Pfad mit denselben Werten starten kann wie
	 * der Laufzeit-Pfad (dort kommen sie aus FWiesbadenCityData::TrafficSettings).
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Konfiguration")
	FWiesbadenTrafficSettings TrafficSettings;

	/**
	 * Einstellungen der Fussgaenger-Simulation.
	 *
	 * Wie TrafficSettings bewusst am WorldBuilder: im gebackenen Pfad gibt es
	 * kein FWiesbadenCityData mehr, aus dem sie kommen koennten.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Konfiguration")
	FWiesbadenPedestrianSettings PedestrianSettings;

	/**
	 * Einstellungen der Ampel-Steuerung.
	 *
	 * Wie TrafficSettings bewusst am WorldBuilder - im gebackenen Pfad gibt es
	 * kein FWiesbadenCityData mehr. Ohne diese Einstellungen bliebe das fertig
	 * implementierte Ampelsystem ungenutzt: die Verkehrs-Simulation haelt zwar
	 * einen Zeiger darauf bereit (SetTrafficLightSystem), bekam ihn aber nie
	 * gesetzt - der Verkehr fuhr durch jede rote Ampel.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Konfiguration")
	FWiesbadenTrafficLightSettings TrafficLightSettings;

	UPROPERTY(EditAnywhere, Category = "GIS|Straßen")
	FRoadGenerationSettings RoadSettings;

	UPROPERTY(EditAnywhere, Category = "GIS|Gebäude")
	FBuildingGenerationSettings BuildingSettings;

	UPROPERTY(EditAnywhere, Category = "GIS|Terrain")
	FTerrainGenerationSettings TerrainSettings;

	// -- Ausgabe-Steuerung ---------------------------------------------------

	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe")
	bool bGenerateRoads = true;

	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe")
	bool bGenerateBuildings = true;

	/** Landscape-Heightmap + Landscape/Vorschau erzeugen (setzt ein DEM voraus). */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe")
	bool bGenerateTerrain = true;

	/** Statt des Vorschau-Meshes eine echte ALandscape erzeugen. */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe", meta = (EditCondition = "bGenerateTerrain"))
	bool bCreateLandscapeActor = true;

	/**
	 * Quads je Subsection (Landscape-Component-Aufteilung). 63 ist der
	 * UE-Standard; die Quads je Seite werden auf ein Vielfaches von
	 * SubsectionSizeQuads * NumSubsections aufgerundet.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe", meta = (EditCondition = "bGenerateTerrain && bCreateLandscapeActor", ClampMin = "1", ClampMax = "255"))
	int32 LandscapeSubsectionSizeQuads = 63;

	/** Subsections je Component (Standard 1). */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe", meta = (EditCondition = "bGenerateTerrain && bCreateLandscapeActor", ClampMin = "1", ClampMax = "4"))
	int32 LandscapeNumSubsections = 1;

	/**
	 * Z-Scale der Landscape. UE-Default 100 bildet die uint16-Heightmap auf
	 * +/- 256 m ab; die Hoehe wird als (value - 32768) / 128 * ZScale in
	 * Unreal Units (cm) interpretiert. Unsere Hoehen liegen bereits in cm vor,
	 * daher wird der Wert mit 128 / ZScale zurueckgerechnet.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe", meta = (EditCondition = "bGenerateTerrain && bCreateLandscapeActor", ClampMin = "1.0"))
	double LandscapeZScale = 300.0;

	/** Aufloesung des Terrain-Vorschau-Meshes (nur wenn keine echte Landscape). */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe", meta = (EditCondition = "bGenerateTerrain && !bCreateLandscapeActor", ClampMin = "2", ClampMax = "2049"))
	int32 TerrainPreviewGridSize = 513;

	/** Strassenausstattung (Schilder, Leitpfosten, Halt-/Wartelinien) erzeugen. */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe")
	bool bGenerateFurniture = true;

	/**
	 * Ziel-Asset-Pfad der Map, in die SaveCityAsMap die gebaute Stadt speichert
	 * (z. B. "/Game/Maps/WiesbadenCity"). Die Map wird danach als Default-Map
	 * in der DefaultEngine.ini verdrahtet, damit Play/Standalone die Stadt ohne
	 * Neugenerierung laden.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe")
	FString MapAssetPath = TEXT("/Game/Maps/WiesbadenCity");

	/**
	 * Stadt-Geometrie in World-Partition-Zellen aufteilen statt monolithisch:
	 * Strassen/Gebaeude werden nach Dreieck-Schwerpunkt auf ein Grid verteilt
	 * und als eigene AWiesbadenCityChunk-Actors gespeichert. SaveCityAsMap
	 * aktiviert dafuer World Partition im Level (CreateOrRepairWorldPartition),
	 * damit die Zellen beim Oeffnen der Map gestreamt werden statt ungestreamt
	 * komplett im Speicher zu liegen.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe")
	bool bGenerateCityChunks = true;

	/** Kantenlaenge einer Chunk-Zelle in Metern (World-Partition-Grid). */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe", meta = (EditCondition = "bGenerateCityChunks", ClampMin = "50"))
	double CityChunkSizeMeters = 500.0;

	/**
	 * Auto-Save nach erfolgreichem Build: Wenn aktiviert, ruft BuildCity am
	 * Ende direkt SaveCityAsMap() auf (speichert die Stadt als Map unter
	 * MapAssetPath inkl. World-Partition-Aktivierung und verdrahtet sie als
	 * Default-Map). Default false - der Build schreibt ohne Opt-in keine Dateien.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe")
	bool bAutoSaveCityAsMap = false;

	/**
	 * Erzeugte Chunk-Actors - ZWINGEND Transient.
	 *
	 * Die Chunks liegen als eigene Actors in eigenen External-Actor-Packages;
	 * das Array wird zum Persistieren nicht gebraucht. Wuerde es mitgespeichert,
	 * haelt der WorldBuilder - ein NICHT raeumlich geladener Actor - harte
	 * Referenzen auf 1984 raeumlich geladene Chunks. World Partition meldet das
	 * je Chunk als Fehler und muss beim Laden ALLE Zellen nachziehen, womit das
	 * Streaming seinen Zweck verliert (GenerateStreaming dauerte dadurch ueber
	 * 3 Minuten).
	 *
	 * DestroyCityChunks kommt ohne das Array aus: sein Level-Pass iteriert alle
	 * AWiesbadenCityChunk-Actors der Welt.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "GIS|Ergebnis")
	TArray<AWiesbadenCityChunk*> CityChunks;

	/** Parameter des Strassenausstattungs-Passes. */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe", meta = (EditCondition = "bGenerateFurniture"))
	FRoadFurnitureSettings FurnitureSettings;

	/** Regionen-abhaengige Assets (Baeume, Ufer, Industrie) erzeugen. */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe")
	bool bGenerateRegionAssets = true;

	/** Parameter des Regionen-Asset-Passes. */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe", meta = (EditCondition = "bGenerateRegionAssets"))
	FRegionAssetSettings RegionAssetSettings;

	/**
	 * Kollision fuer die FAHRBAHN-Meshes.
	 *
	 * ZWINGEND: Das Spielerfahrzeug bestimmt seine Hoehe per Raycast nach
	 * unten. Ohne Fahrbahn-Kollision trifft der Strahl nur das Landscape - und
	 * das liegt unter der Strasse (Fahrbahnversatz 30 cm, Einebnung nochmals
	 * 45 cm darunter). Das Fahrzeug sass dadurch sichtbar IM Boden, zwischen
	 * den Bordsteinen, was die Strassen zugleich viel zu schmal wirken liess.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe")
	bool bCreateRoadCollision = true;

	/**
	 * Kollision fuer die GEBAEUDE-Meshes (teuer bei grossen Netzen).
	 *
	 * Bewusst aus: Dreieckskollision fuer 5.825 Gebaeude-Abschnitte kostet in
	 * Cook-Zeit wie Speicher zu viel. Die Gebaeude blockieren stattdessen ueber
	 * die Box-Koerper des UBuildingCollisionSpawnerComponent.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Ausgabe")
	bool bCreateCollision = false;

	// -- Materialien ---------------------------------------------------------

	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* RoadMaterial = nullptr;

	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* SidewalkMaterial = nullptr;

	/** Bordstein - bewusst eigener Slot, damit die Kante sich sichtbar vom
	 * Gehweg absetzt. Faellt ohne Zuweisung auf SidewalkMaterial zurueck. */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* KerbMaterial = nullptr;

	/**
	 * Fahrbahnmarkierung und Zebrastreifen (weiss).
	 *
	 * Ohne eigenes Material fielen beide in den Fahrbahn-Zweig und wuerden mit
	 * dem dunklen Asphaltmaterial gezeichnet - auf der Strasse also unsichtbar.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* LaneMarkingMaterial = nullptr;

	/** Radweg - in Deutschland ueblicherweise roter Belag. */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* CyclewayMaterial = nullptr;

	/**
	 * Belag fuer unbefestigte Wege (Schotter, Kies, Erde).
	 *
	 * Ohne ihn bekommen die 10.089 Pfade und 9.267 Feldwege der OSM-Daten
	 * dasselbe Asphaltmaterial wie eine Hauptstrasse - ein Waldweg sah aus wie
	 * eine Fahrbahn. Die Oberflaechenart wird ohnehin schon ermittelt und die
	 * Mesh-Abschnitte sind danach gruppiert; sie wurde bei der Materialwahl nur
	 * nicht ausgewertet.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* UnpavedMaterial = nullptr;

	/** Belag fuer Pflaster und Naturstein (Fussgaengerzonen, Altstadtgassen). */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* PavedStoneMaterial = nullptr;
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* BuildingWallMaterial = nullptr;

	/**
	 * Fassadenmaterial je Bauweise. Der Index ist
	 * FBuildingMeshSection::MaterialVariant, den der BuildingGenerator aus
	 * Gebaeudetyp und Baualter ableitet:
	 * 0 Putz, 1 Backstein, 2 Sandstein, 3 Glas, 4 Beton, 5 Fachwerk.
	 *
	 * Die Sections sind bereits nach Variante gruppiert; ohne diese Zuordnung
	 * bekaeme die ganze Stadt eine einzige Fassade.
	 * Leere Eintraege fallen auf BuildingWallMaterial zurueck.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	TArray<UMaterialInterface*> FacadeVariantMaterials;

	/**
	 * Per-Adress-Override: Adresse (addr:street + addr:housenumber, z. B.
	 * "Mainzer Strasse 129") -> eigenes Fassaden-Material. Die Schluessel
	 * bilden gleichzeitig die FacadeOverrideAddresses des BuildingGenerators.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	TMap<FString, UMaterialInterface*> AddressFacadeMaterials;

	/**
	 * City-Prompt-Override: FacadeOverrideKey ("PromptStyle:<Stil>" oder
	 * "PromptLandmark:<Name>", vom BuildingGenerator aus der FCityPromptSpec
	 * vergeben) -> eigenes Fassaden-Material. Leer lassen = Standardfalle.
	 */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	TMap<FString, UMaterialInterface*> PromptFacadeMaterials;

	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* BuildingRoofMaterial = nullptr;

	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* TerrainMaterial = nullptr;

	/** Ordner der Schilder-Texturen (Dateinamen Sign_<VzKat>.png). */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	FString SignTextureFolder = TEXT("/Game/Textures/TrafficSigns/");

	/** Basismaterial der Schilder-Tafeln; die Schild-Textur wird als Parameter gesetzt. */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* SignMaterial = nullptr;

	/** Schildpfosten (verzinkter Stahl). */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* PoleMaterial = nullptr;

	/** Leitpfosten - im Netz mit Abstand die haeufigste Ausstattung. */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* DelineatorMaterial = nullptr;

	/** Reflektor am Leitpfosten. */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* ReflectorMaterial = nullptr;

	/** Aufgesetzte Markierungs-Instanzen des Ausstattungs-Spawners. */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	UMaterialInterface* MarkingMaterial = nullptr;

	/** Parameter-Name der Schild-Textur im SignMaterial. */
	UPROPERTY(EditAnywhere, Category = "GIS|Materialien")
	FName SignTextureParameterName = TEXT("SignTexture");

	// -- Ergebnisse (read-only) ----------------------------------------------

	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FOSMParseResult LastParseResult;

	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FHeightmapImportResult LastDemImportResult;

	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FRoadGenerationReport LastRoadReport;

	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FBuildingGenerationReport LastBuildingReport;

	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FTerrainGenerationReport LastTerrainReport;

	/**
	 * Ergebnis der Terrain-Qualitaetskontrolle des letzten Builds (leer =
	 * ok): Warnt sichtbar im Details-Panel, wenn das Terrain-Tile deutlich
	 * groesser als die OSM-Ausdehnung ist (fehlender Crop) oder die
	 * Hoehenspanne unplausibel gross/klein ist.
	 */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FTerrainQualityReport LastTerrainQuality;

	/** Fahrspur-Graph fuer Verkehrs-KI und GPS (Ergebnis der Strassengenerierung). */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FRoadNetwork RoadNetwork;

	/** Metadaten aller erzeugten Gebaeude (Adressen, Typen, Bounds). */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	TArray<FGeneratedBuilding> Buildings;

	/** Ergebnis des Strassenausstattungs-Passes. */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FRoadFurnitureReport LastFurnitureReport;

	/** Platzierungsdaten der Strassenausstattung (Schilder, Leitpfosten, Markierungen). */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FRoadFurnitureLayout FurnitureLayout;

	/**
	 * Platzierungsdaten der Regionsobjekte (Baeume, Ufer- und Industrieobjekte).
	 *
	 * Diese Liste existierte bisher NUR waehrend des Builds im Arbeitsspeicher.
	 * Die zugehoerigen Instanz-Komponenten sind - wie die der Ausstattung -
	 * `Transient` und ueberleben das Speichern nicht. Von den erzeugten
	 * Baeumen stand deshalb kein einziger in der gebackenen Stadt, obwohl das
	 * Build-Log sie meldete.
	 *
	 * Serialisiert werden die DATEN, aufgebaut werden die Instanzen beim Start
	 * (siehe BeginPlay) - dasselbe Muster wie bei der Strassenausstattung.
	 */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FRegionAssetLayout RegionAssetLayout;

	/** Die zuletzt erzeugte Landscape (wird von ClearGeneratedGeometry entfernt). */
	UPROPERTY(Transient)
	ALandscape* GeneratedLandscape = nullptr;

	/** True, solange ein Build im Hintergrund laeuft. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "GIS|Ergebnis")
	bool bBuildInProgress = false;

	/**
	 * True, wenn der letzte BuildCity erfolgreich war und die Stadt-Geometrie
	 * im Level liegt. Bleibt beim Speichern der Map erhalten (nicht transient)
	 * - das CitySubsystem erkennt daran eine gebackene Stadt und baut sie zur
	 * Laufzeit nicht doppelt.
	 */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	bool bCityBaked = false;

	/** Fortschritt des laufenden Builds, 0..1. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "GIS|Ergebnis")
	float BuildProgress = 0.0f;

	/** Statusbeschreibung des laufenden Builds (z. B. "Generiere Gebaeude..."). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "GIS|Ergebnis")
	FString BuildStatus;

	/** Zuletzt aufgetretene Fehlermeldung (leer bei Erfolg). */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FString LastError;

	/**
	 * Ergebnis des letzten SaveCityAsMap-Versuchs (Auto-Save nach BuildCity
	 * oder manuelle Kachel): true = Map erfolgreich gespeichert, false =
	 * fehlgeschlagen (Grund steht in LastError). Sichtbares Ergebnis-Feedback
	 * im Details-Panel, damit man sieht, ob der automatische Map-Save geklappt
	 * hat. Nicht transient - bleibt auch nach dem Map-Speichern sichtbar.
	 */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	bool bAutoSaveSucceeded = false;

	/**
	 * Letzter BuildCity-Lauf als gemeinsame USTRUCT (Zeitpunkt/Dauer/
	 * Ergebnis; leer, wenn noch kein Lauf) - ohne CSV-/Datei-Lookup. Dieselbe
	 * Struktur wie im Laufzeit-Pfad (GameInstance/Subsystem/GameMode), damit
	 * Editor und Runtime dieselbe Quelle und Formatierung nutzen.
	 */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FLastBuildInfo LastBuild;

	/**
	 * Kompakter Einzeiler des letzten BuildCity-Laufs
	 * (z. B. "Letzter Build: 83.4 s, ok (2026-08-16 15:45:00)") - abgeleitet
	 * aus LastBuild.GetSummary() (Komfort-Anzeige im Details-Panel).
	 */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ergebnis")
	FString LastBuildSummary;

	// -- Aktionen ------------------------------------------------------------

	/** Fuehrt die komplette Pipeline aus (Parser -> Generatoren -> Meshes). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "GIS")
	void BuildCity();

	/** Loescht erzeugte Geometrie und Ergebnisse. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "GIS")
	void ClearGeneratedGeometry();

	/**
	 * Hot-Reload-Kachel: laedt den Verkehrszeichen-Katalog neu aus
	 * Content/Config/TrafficSignCatalog.json, ohne den Editor neu zu starten.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "GIS")
	void ReloadTrafficSignCatalog();

	/**
	 * Speichert die gebaute Stadt als echte Map unter MapAssetPath
	 * (Content/Maps/WiesbadenCity.umap) und verdrahtet sie als Default-Map in
	 * der DefaultEngine.ini. Danach starten Play/Standalone direkt in der
	 * Stadt - ohne Laufzeit-Neugenerierung. Voraussetzung: ein erfolgreicher
	 * BuildCity (bCityBaked).
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "GIS")
	void SaveCityAsMap();

	/**
	 * Verteilt die Regionsobjekte einer bereits gebackenen Stadt auf die
	 * vorhandenen Zell-Actors.
	 *
	 * Grund: Bis zum 26.08.2026 hingen alle 1.532.254 Baeume in EINER
	 * Instanz-Komponente an diesem Actor - und dieser Actor wird von World
	 * Partition nie gestreamt. Der komplette Bestand war damit jederzeit
	 * geladen. Gemessen an einer Stelle, an der ausser einer Handvoll winziger
	 * Kegel am Horizont kein Baum im Bild stand:
	 *
	 *     1.532.254 Instanzen   82 ms Bildzeit
	 *       153.226 Instanzen   64 ms
	 *             0 Instanzen   54 ms
	 *
	 * Die Kosten haengen an der VERWALTETEN Menge, nicht an der sichtbaren -
	 * eine Sichtweitenbegrenzung haette daran nichts geaendert.
	 *
	 * Neu gebaute Staedte bekommen die Verteilung schon beim Aufteilen
	 * (FWiesbadenCityChunking::BuildChunks). Diese Funktion holt sie fuer eine
	 * bestehende Karte nach, ohne die 40 bis 64 Minuten eines Neubaus.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "GIS")
	void DistributeRegionAssetsToChunks();

	/**
	 * Prueft im GESAMTEN Stadtgebiet, ob die Fahrbahn auf dem Gelaende liegt.
	 *
	 * Geht alle Kreuzungen des Netzes durch und vergleicht ihre Hoehe mit der
	 * Gelaendehoehe an derselben Stelle. Schreibt einen JSON-Bericht mit den
	 * groessten Abweichungen samt Strassennamen.
	 *
	 * Anlass: "Auf der Platter Strasse Richtung Taunusstein sitzen Fahrbahn
	 * und Gehwege in der Luft." Punktuell am Spieler zu messen sagt nichts
	 * ueber die restlichen 22.226 Kreuzungen.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "GIS|Diagnose")
	void CheckRoadTerrainHeights();

	/** Liefert die Textur zu einer VzKat-Schild-Id (Asset Sign_<Id>.png). */
	UFUNCTION(BlueprintCallable, Category = "GIS")
	UTexture2D* ResolveSignTexture(const FString& SignId) const;

	/** Erzeugt ein Material-Instanz-Dynamic mit der Schild-Textur fuer eine Schild-Id. */
	UFUNCTION(BlueprintCallable, Category = "GIS")
	UMaterialInstanceDynamic* ResolveSignMaterial(const FString& SignId);

	/** Baut den Textur-Asset-Namen zu einer Schild-Id (Sign_<Id>). */
	static FString BuildSignTextureName(const FString& SignId);

	/**
	 * Verifiziert nach dem Speichern, ob die Map wirklich als streamendes
	 * World-Partition-Level vorliegt (datenrein, headless testbar):
	 *   - bIsPartitioned: UWorldPartition-Objekt im Level (UWorld::IsPartitionedWorld)
	 *   - bMapExists:     die .umap liegt auf der Platte (FPackageName::DoesPackageExist)
	 *   - bExternalActors: External-Actor-Packages fuer die Map auf der Platte
	 *                     (HasExternalActorPackages) - der headless verlaessliche
	 *                     Indikator, dass die Chunk-Actors als WP-Zellen
	 *                     externalisiert wurden. IsStreamingEnabled() ist im
	 *                     selben Prozess nach frischer WP-Aktivierung nie true
	 *                     (das WP-Objekt wird erst beim naechsten Oeffnen der
	 *                     Map initialisiert) und taugt dort nicht als Beweis.
	 *   - bChunksInSeparatePackages: jeder CityChunk liegt in seinem EIGENEN
	 *                     External-Actor-Package. Ohne diese Pruefung laesst
	 *                     HasExternalActorPackages auch die kaputte Variante
	 *                     durch, in der alle Chunks in EINEM Package landen
	 *                     (z. B. 1984 Chunks in einem 531-MB-Package) - beim
	 *                     Laden scheitern dann alle Chunk-Imports
	 *                     ("Failed import for WiesbadenCityChunk") und die
	 *                     Stadt ist im Spiel unsichtbar, obwohl Packages auf
	 *                     der Platte liegen.
	 * @return true wenn alle Bedingungen erfuellt sind, sonst false und OutError
	 *         erklaert die fehlgeschlagene Bedingung.
	 */
	static bool VerifyWorldPartitionSave(bool bIsPartitioned, bool bMapExists, bool bExternalActors,
		bool bChunksInSeparatePackages, FString& OutError);

	/**
	 * Prueft, ob fuer eine Map External-Actor-Packages auf der Platte liegen
	 * (Content/__ExternalActors__/<MapPfad>/). Das ist der Nachweis, dass die
	 * gespawnten Actors (z. B. CityChunks) beim SaveMap als World-Partition-
	 * Zellen externalisiert wurden statt ungestreamt in der .umap zu landen.
	 * @param MapAssetPath Content-Pfad der Map, z. B. /Game/Maps/WiesbadenCity
	 */
	static bool HasExternalActorPackages(const FString& MapAssetPath);

private:
	/**
	 * Aktiviert World Partition im Level (idempotent) und markiert den
	 * WorldBuilder sowie eine ggf. vorhandene Landscape als always-loaded.
	 * MUSS VOR SpawnCityChunks aufgerufen werden, damit die Chunk-Actors in
	 * einer partitionierten Welt gespawnt und beim Save als WP-Zellen
	 * externalisiert werden (nachtraegliche Aktivierung erfasst bereits
	 * gespawnte Actors nicht).
	 */
	void EnsureWorldPartition();

	/**
	 * Baut die Ausstattungs-Instanzen aus dem gespeicherten Layout neu auf.
	 *
	 * Die ISM-Komponenten des Ausstattungs-Spawners sind Transient - sie
	 * ueberleben das Speichern der Map NICHT. Im Code stand dazu die Annahme
	 * "beim Bauen erzeugt, werden die Instanzen mit der Map gespeichert"; das
	 * trifft wegen des Transient-Markers nicht zu. Gemessen enthielt die
	 * gebackene Karte 0 Mast-Instanzen, waehrend die Layout-DATEN mit 50.875
	 * Schildern, 177.812 Leitpfosten und 3.332 Laternen vollstaendig vorlagen.
	 *
	 * Die Daten sind also da, nur wurden aus ihnen nie wieder Instanzen. Genau
	 * das passiert hier - einmal je Sitzung.
	 */
	virtual void BeginPlay() override;

	/** Setzt Materialien am Ausstattungs-Spawner und erzeugt seine Instanzen. */
	void RebuildFurnitureInstances();

	/** Erzeugt die Instanzen der Regionsobjekte aus dem gespeicherten Layout. */
	void RebuildRegionAssetInstances();

	/** Setzt Ergebnisse und Meshes zurueck. */
	void ResetResults();

	/**
	 * Schreibt die Build-Zusammenfassung als CSV-Zeile (alle Ausgaenge von
	 * BuildCity - auch Abbruch/Fehler, damit die Historie vollstaendig ist).
	 * Baut die Summary ueber die gemeinsame BuildSummaryFromCityData-Factory;
	 * die gemovten Ergebnis-Member (RoadNetwork/Buildings/FurnitureLayout des
	 * Erfolgspfads) werden als Overrides durchgereicht, damit die Zaehler
	 * nicht faelschlich aus dem (leeren) CityData kommen. CityData=nullptr,
	 * wenn der Build vor der Pipeline abbrach (alle Zaehler 0).
	 */
	void WriteBuildSummaryToCsv(const FWiesbadenCityData* CityData,
		const FRoadNetwork* MovedRoadNetwork, const TArray<FGeneratedBuilding>* MovedBuildings,
		const FRoadFurnitureLayout* MovedFurniture, const FString& Result, double DurationSeconds);

	/** Uebertraegt Strassen-Mesh-Sections auf die Procedural-Mesh-Komponente. */
	void ApplyRoadMesh(const FRoadMeshData& MeshData);

	/** Uebertraegt Gebaeude-Mesh-Sections auf die Procedural-Mesh-Komponente. */
	void ApplyBuildingMesh(const FBuildingMeshData& MeshData);

	/**
	 * Teilt Strassen-/Gebaeude-Meshes in Grid-Zellen und spawnt je belegter
	 * Zelle einen AWiesbadenCityChunk (Materialien aus den Resolve-Pfaden).
	 */
	void SpawnCityChunks(const FRoadMeshData& RoadMesh, const FBuildingMeshData& BuildingMesh);

	/** Zerstoert alle erzeugten Chunk-Actors. */
	void DestroyCityChunks();

	/** Baut ein niedrigaufgeloestes Vorschau-Mesh aus der Landscape-Heightmap. */
	void ApplyTerrainPreview(const FTerrainTile& Tile);

	/**
	 * Baut die Import-Maps fuer ALandscapeProxy::Import (datenrein, ohne Welt):
	 * Heightmap und Layer-Infos werden unter dem Default-Guid FGuid()
	 * abgelegt - die Engine sucht dort per FindChecked (LandscapeEdit.cpp);
	 * ein FGuid::NewGuid() besteht die Groessenpruefung, scheitert danach aber
	 * an FindChecked(FGuid()) und stuerzt den Editor ab.
	 *
	 * Public: wird vom Automation-Test LandscapeImportTest direkt aufgerufen.
	 */
public:
	static void BuildLandscapeImportMaps(TArray<uint16>&& Heightmap,
		TMap<FGuid, TArray<uint16>>& OutHeightData,
		TMap<FGuid, TArray<FLandscapeImportLayerInfo>>& OutMaterialLayerInfos);

	/** Erzeugt eine echte ALandscape aus der Heightmap. @return nullptr bei Fehler. */
	ALandscape* CreateLandscapeFromTile(const FTerrainTile& Tile);

	/** Material fuer einen Strassen-Mesh-Kanal. */
	/**
	 * Fuellt alle noch leeren Material-Slots aus /Game/Materials/City.
	 *
	 * Ohne das bleiben die Slots nullptr und UProceduralMeshComponent rendert
	 * mit dem Default-Material (Schachbrett). Das war der Grund, warum die
	 * fertig gebaute Stadt vollstaendig untexturiert aussah: Geometrie und
	 * Streaming waren in Ordnung, es fehlte nur die Zuweisung.
	 */
	void EnsureDefaultMaterials();

	/**
	 * Legt Sonne, Himmelslicht, Atmosphaere und Hoehennebel an, falls sie im
	 * Level fehlen.
	 *
	 * BuildCity laeuft ueblicherweise auf einem frisch erzeugten, LEEREN Level
	 * (Tools/build_alkis.py ruft new_level()). Ein solches Level enthaelt keinen
	 * einzigen Lichtakteur - die fertig gebaute Stadt rendert dann komplett
	 * schwarz, obwohl Geometrie und Materialien einwandfrei sind. Dieser Fall
	 * ist aufgetreten und war im Ergebnis von "Materialien fehlen" nicht zu
	 * unterscheiden: beides sieht nach "keine Stadt" aus.
	 *
	 * Die Akteure werden als nicht raeumlich geladen markiert, damit World
	 * Partition sie nicht wegstreamt - eine gestreamte Sonne waere je nach
	 * Spielerposition an oder aus.
	 */
	/**
	 * Weist den Region-Assets Meshes und Materialien zu, falls leer.
	 *
	 * Ohne Mesh laeuft SpawnRegionAssets ins Leere (der Spawner loggt nur den
	 * Report) - so stand trotz 1,53 Millionen erzeugter Baeume keiner in der
	 * Stadt.
	 */
	void EnsureDefaultRegionAssets();

	void EnsureLightingActors();

	UMaterialInterface* ResolveRoadMaterial(ERoadMeshChannel Channel, EOSMSurfaceType Surface) const;

	/** Material fuer einen Gebaeude-Mesh-Kanal. */
	UMaterialInterface* ResolveBuildingMaterial(EBuildingMeshChannel Channel, int32 MaterialVariant, const FString& FacadeOverrideKey);

	UPROPERTY(Transient)
	UBillboardComponent* BillboardComponent = nullptr;

	UPROPERTY(Transient)
	UProceduralMeshComponent* RoadMeshComponent = nullptr;

	UPROPERTY(Transient)
	UProceduralMeshComponent* BuildingMeshComponent = nullptr;

	UPROPERTY(Transient)
	UProceduralMeshComponent* TerrainMeshComponent = nullptr;

	UPROPERTY(Transient)
	URoadFurnitureSpawnerComponent* FurnitureSpawner = nullptr;

	/**
	 * Baeume, Ufer- und Industrie-Objekte.
	 *
	 * Bewusst HIER und nicht nur am CityActor: der gebackene Pfad ruft
	 * ApplyCityData gar nicht auf, weshalb von 1,53 Millionen erzeugten
	 * Baeumen kein einziger in der Stadt stand. Am WorldBuilder werden die
	 * Instanzen beim Bauen erzeugt und mit der Map gespeichert - wie die
	 * Strassenausstattung auch.
	 */
	UPROPERTY(VisibleAnywhere, Category = "GIS|Ausgabe")
	URegionAssetSpawnerComponent* RegionAssetSpawner = nullptr;

	// -- Pipeline-Objekte -----------------------------------------------------
	//
	// Werden im Game-Thread erzeugt und als UPROPERTY gehalten, damit GC sie
	// waehrend des Hintergrund-Builds nicht einsammelt. Der Worker nutzt sie
	// nur stateless/lesend; die Ergebnisse laufen ueber den Thread-Kontext.

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
};
