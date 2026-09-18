// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "GIS/WiesbadenCityChunking.h"

#include "WiesbadenCityChunk.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;
class URegionAssetSpawnerComponent;

/**
 * Eine World-Partition-Zelle der gebackenen Stadt.
 *
 * Traegt die Road-/Building-Mesh-Sections einer Grid-Zelle (FCityChunkMesh)
 * als zwei Procedural-Mesh-Komponenten. Der AWiesbadenWorldBuilder spawnt
 * nach BuildCity einen Chunk je belegter Zelle; die Chunks werden - nicht
 * transient - in der Map serialisiert. Beim Oeffnen der Map verteilt World
 * Partition die Chunk-Actors anhand ihrer kleinen Bounds auf Streaming-Zellen,
 * sodass die Stadt zellenweise geladen/entladen wird statt als ein
 * monolithischer Riesen-Actor komplett im Speicher zu liegen.
 *
 * Materialien werden vom WorldBuilder nach dem Spawn gesetzt (die
 * ResolveRoadMaterial/ResolveBuildingMaterial-Lookup-Pfade haengen an dessen
 * UPROPERTY-Material-Maps).
 */
/** Nutzungsart eines Gebaeudes - entscheidet, wann seine Fenster leuchten. */
UENUM(BlueprintType)
enum class EWbBuildingUse : uint8
{
	/** Wohnen: abends lange hell, nachts einzelne Fenster. */
	Residential UMETA(DisplayName = "Wohnen"),

	/** Buero/Gewerbe: frueh am Abend hell, nach Feierabend dunkel. */
	Office      UMETA(DisplayName = "Buero"),

	/** Alles andere - behandelt wie Wohnen, aber gedaempft. */
	Other       UMETA(DisplayName = "Sonstige")
};

UCLASS()
class WIESBADENREAL_API AWiesbadenCityChunk : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenCityChunk();

	/**
	 * Uebernimmt die Mesh-Sections einer Zelle (Road + Building). Die
	 * Section-Indizes starten je Komponente bei 0 in der Reihenfolge der
	 * Chunk-Arrays; danach koennen Materialien ueber SetRoadSectionMaterial/
	 * SetBuildingSectionMaterial gesetzt werden.
	 *
	 * Kollision getrennt schaltbar: Die FAHRBAHN braucht sie zwingend - das
	 * Spielerfahrzeug tastet per Raycast nach unten und sass sonst auf dem
	 * Landscape, also rund 45 cm UNTER der Strasse (sichtbar im Boden
	 * versunken). Gebaeude kommen dagegen mit den guenstigeren Box-Koerpern
	 * des BuildingCollisionSpawners aus; Dreieckskollision fuer 5.825
	 * Gebaeude-Abschnitte waere unbezahlbar.
	 */
	void ApplyChunk(const FCityChunkMesh& Chunk, bool bRoadCollision, bool bBuildingCollision);

	/**
	 * Backt die gefuellten Road-/Building-ProcMeshes in serialisierte StaticMeshes
	 * mit VORGEKOCHTER Kollision (WiesbadenChunkStaticMeshBaker) und haengt sie an
	 * die StaticMesh-Komponenten; danach werden die ProcMesh-Sections geleert.
	 *
	 * Zweck: Beim Stream-in wird ein StaticMesh nur GELADEN - kein ProcMesh-Render-
	 * Proxy-Neuaufbau (~40-50 ms/Chunk) und kein Kollisions-Cook (die 736-848-ms-
	 * ProcessLoadedPackages-Aussetzer). Nur im Editor/Bake-Pfad; ausserhalb No-op.
	 *
	 * Render und Kollision der Fahrbahn werden GETRENNT gebacken, weil SM-Kollision
	 * (anders als das per-Section schaltbare ProcMesh) immer die GANZE Asset-
	 * Geometrie kocht: Das sichtbare RoadStaticMesh traegt alle Sections OHNE
	 * gekochte Kollision; ein separates, unsichtbares RoadCollisionStaticMesh kocht
	 * die Trimesh-Kollision nur fuer die kollisionsfaehigen Sections (ApplyChunk
	 * setzt bEnableCollision = bRoadCollision && !Boeschung). So bleiben die
	 * Boeschungen (allein in Wiesbaden ~3,5 Mio Dreiecke) physikfrei - wie im
	 * ProcMesh-Pfad. Gebaeude nutzen weiter die guenstigeren Box-Koerper.
	 */
	void BakeToStaticMeshes(int32 CellX, int32 CellY, bool bRoadCollision, bool bBuildingCollision);

	/**
	 * Wirft die Verweise der Komponenten auf die gerade gebackenen StaticMesh-
	 * Assets weg und laesst die Pakete purgen.
	 *
	 * Nur fuer den Bake-Pfad: BakeToStaticMeshes haelt JEDES frisch gebackene
	 * Mesh als starken Verweis an der Komponente. Bei ~4600 Chunks x 3 Meshes
	 * blieben dadurch alle Render-/Kollisionsdaten bis zum Ende von BuildCity im
	 * Speicher (mehrere GiB Commit), obwohl jedes Asset laengst auf der Platte
	 * liegt - genau der Commit-OOM des Stadt-Bakes. Nach dem Aufruf sind die
	 * Komponenten leer; die gebackene Karte liest die Meshes beim Stream-in
	 * sowieso neu aus den Paketen. Ausserhalb des Editors ein No-op.
	 */
	void UnloadBakedChunkMeshes();

	/** Setzt das Material einer Road-Section (Index wie in ApplyChunk). */
	void SetRoadSectionMaterial(int32 SectionIndex, UMaterialInterface* Material);

	/** Setzt das Material einer Building-Section (Index wie in ApplyChunk). */
	void SetBuildingSectionMaterial(int32 SectionIndex, UMaterialInterface* Material);

	UProceduralMeshComponent* GetRoadMesh() const { return RoadMesh; }

	/**
	 * Nutzungsart aus dem Namen des Fassaden-Materials (datenrein, testbar).
	 *
	 * Die Nutzung steckt schon in der Fassadenwahl der Pipeline
	 * (MI_WbFacade_Buerohaus, _Wohnhaus, _Altbau ...) - sie dort abzulesen ist
	 * genauer als jede Schaetzung aus Hoehe oder Grundflaeche und braucht
	 * keinen Re-Bake.
	 */
	static EWbBuildingUse BuildingUseFromMaterialName(const FString& MaterialName);

	/**
	 * Staerke des Fensterlichts 0..1 zu einer Tageszeit (datenrein, testbar).
	 *
	 * Am Tag null: ein leuchtendes Fenster bei Sonnenschein sieht falsch aus,
	 * und sehen wuerde man es ohnehin nicht. Wohnen und Buero laufen bewusst
	 * AUSEINANDER - ein Buerohaus, das um zwei Uhr nachts hell ist wie ein
	 * Wohnblock, verraet sofort, dass da eine einzige Kurve fuer alles gilt.
	 */
	static float WindowLightStrength(EWbBuildingUse Use, float TimeOfDayHours);

	/**
	 * Setzt das Fensterlicht dieser Zelle nach der Tageszeit.
	 *
	 * Je Material-Fach eine eigene Staerke, denn in einer Zelle stehen Wohn-
	 * und Buerohaeuser nebeneinander. Idempotent: gleiche Stunde, keine Arbeit.
	 */
	void ApplyWindowLight(float TimeOfDayHours);

	/** Kanal einer Road-Section; INDEX_NONE, wenn unbekannt. */
	int32 GetRoadSectionChannel(int32 SectionIndex) const
	{
		return RoadSectionChannels.IsValidIndex(SectionIndex)
			? static_cast<int32>(RoadSectionChannels[SectionIndex]) : INDEX_NONE;
	}
	UProceduralMeshComponent* GetBuildingMesh() const { return BuildingMesh; }

	/**
	 * True, wenn diese Zelle zur LAUFZEIT sichtbare Geometrie traegt.
	 *
	 * Entweder gefuellte ProcMesh-Sections (waehrend des Bakes) ODER ein
	 * gebackenes Render-StaticMesh (auf der fertigen Karte). Zwingend fuer die
	 * Geometrie-Diagnose: BakeToStaticMeshes LEERT die ProcMeshes nach dem Bake,
	 * die StaticMeshes tragen dann die Stadt. Eine Diagnose, die nur die
	 * ProcMesh-Sections zaehlt, meldet jede gebackene Stadt faelschlich als leer.
	 */
	bool HasRenderGeometry() const;

	/**
	 * Setzt die Regionsobjekte dieser Zelle und baut ihre Instanzen auf.
	 *
	 * Getrennt von ApplyChunk, weil die Objekte auch NACHTRAEGLICH in eine
	 * bereits gebackene Stadt verteilt werden koennen. Der Alternativweg waere
	 * ein kompletter Neubau; laut Bauhistorie dauert der 40 bis 64 Minuten,
	 * waehrend hier nur Punkte umsortiert werden.
	 */
	void SetRegionAssets(const TArray<FPlacedRegionAsset>& InAssets);

	/**
	 * Anzahl der Regionsobjekte dieser Zelle.
	 *
	 * BlueprintCallable, damit das Verteil-Skript NACHZAEHLEN kann. Eine
	 * Verteilung, die nichts verteilt, meldet sich sonst genauso ruhig wie
	 * eine erfolgreiche - und gespeichert waere dann eine Stadt ohne Baeume.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Chunk")
	int32 GetRegionAssetCount() const { return RegionAssets.Num(); }

	/**
	 * Haelt die Actor-Bounds auf dem Zell-Inhalt.
	 *
	 * Ohne Inhalt traegt der Chunk nur LEERE Komponenten (RoadMesh/
	 * BuildingMesh ohne Section, ISMs ohne Instanz). Die haben Punkt-Bounds
	 * an ihrer eigenen Position (UE 5.8: SceneComponent.cpp CalcBounds,
	 * InstancedStaticMesh.cpp CalcBoundsImpl) - und der Chunk-Actor spawnt
	 * am Ursprung. GetComponentsBoundingBox vereinigt diese Ursprungs-Punkte
	 * mit der Geometrie voller Zellen: Die Actor-Bounds spannen vom Ursprung
	 * BIS zur Zelle (gemessen 3 x 4 km), World Partition kann die Zelle nicht
	 * raeumlich trennen und laedt sie mit jedem geladenen Chunk mit.
	 *
	 * Der Aufruf setzt leere Komponenten auf den Mittelpunkt des Zell-
	 * Inhalts (Mesh-Sections oder Regions-Assets), so vorhanden. ApplyChunk
	 * und die nachtraegliche Verteilung (SetRegionAssets) rufen ihn auf;
	 * ist die Zelle voellig leer, bleibt es bei der Ursprungs-Ankerung.
	 *
	 * BlueprintCallable, damit der Re-Bake der gebackenen Karte ohne
	 * Neubau aus einem Editor-Skript laufen kann: Karte oeffnen, je Chunk
	 * ankeren, speichern - die WP-Zellzuordnung folgt den Bounds der
	 * ActorDescs, die beim Speichern neu geschrieben werden.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Chunk")
	void AnchorStreamingBounds();

protected:
	virtual void BeginPlay() override;

private:
	/**
	 * Baeume, Ufer- und Industrie-Objekte dieser Zelle.
	 *
	 * KEIN Transient - die Instanz-Komponente dagegen schon, weshalb die
	 * Instanzen beim Oeffnen der Karte aus diesen Daten neu entstehen muessen
	 * (BeginPlay). Genau daran ist die Strassenausstattung schon einmal
	 * gescheitert: Das Build-Protokoll meldete 50.875 Schilder, in der
	 * gebackenen Stadt stand keines - die Meldung beschrieb die
	 * Editor-Sitzung, in der gebaut wurde.
	 */
	UPROPERTY()
	TArray<FPlacedRegionAsset> RegionAssets;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Chunk")
	URegionAssetSpawnerComponent* RegionAssetSpawner = nullptr;

	/**
	 * Kanal je Road-Section (Reihenfolge wie in ApplyChunk).
	 *
	 * KEIN Transient: Ohne Serialisierung waere die Zuordnung nach dem
	 * Speichern der Karte verloren, und die Diagnose koennte Fahrbahn nicht
	 * mehr von Boeschung unterscheiden. Genau daran ist die Verdeckungs-
	 * Statistik gescheitert.
	 */
	UPROPERTY()
	TArray<uint8> RoadSectionChannels;

	/**
	 * ProcMesh-Puffer NUR fuer den Bake-Pfad (Editor): ApplyChunk fuellt sie,
	 * BakeToStaticMeshes backt sie in die StaticMeshes und LEERT sie danach.
	 * Auf der gebackenen Karte tragen sie zur Laufzeit keine Geometrie -> kein
	 * Proxy-Neuaufbau, kein Kollisions-Cook beim Stream-in.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Chunk")
	UProceduralMeshComponent* RoadMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Chunk")
	UProceduralMeshComponent* BuildingMesh = nullptr;

	/** Gebackene, vorgekochte StaticMeshes (Render + Kollision serialisiert). Ein
	 *  Material-Slot je ProcMesh-Section; SetRoadSectionMaterial/-Building setzen
	 *  die echten Materialien als Komponenten-Override je Slot. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Chunk")
	UStaticMeshComponent* RoadStaticMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Chunk")
	UStaticMeshComponent* BuildingStaticMesh = nullptr;

	/**
	 * Unsichtbares Kollisions-StaticMesh der Fahrbahn: NUR die kollisionsfaehigen
	 * Road-Sections (Boeschungen ausgenommen), mit vorgekochter Trimesh-Kollision.
	 *
	 * SM-Kollision ist immer die GANZE Asset-Geometrie (kein per-Section-Schalter
	 * wie beim ProcMesh). Damit Boeschungen keine unbezahlbare Trimesh-Kollision
	 * bekommen, wird gerendert (RoadStaticMesh, alle Sections) und kollidiert
	 * (dieses Mesh, nur Fahrbahn) GETRENNT - wie im ProcMesh-Pfad.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Chunk")
	UStaticMeshComponent* RoadCollisionStaticMesh = nullptr;

	/** True, sobald BakeToStaticMeshes gelaufen ist: die Material-Setter zielen
	 *  dann auf die StaticMesh-Slots statt auf die (geleerten) ProcMeshes. */
	bool bBakedToStaticMesh = false;

	/** Zuletzt gesetzte Stunde des Fensterlichts (-1 = noch nie gesetzt). */
	float AppliedWindowLightHours = -1.0f;
};
