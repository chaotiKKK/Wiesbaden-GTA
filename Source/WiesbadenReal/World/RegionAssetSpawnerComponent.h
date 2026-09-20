// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "GIS/WiesbadenRegionAssets.h"

#include "RegionAssetSpawnerComponent.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/**
 * Visueller Spawner fuer regionen-abhaengige Assets: rendert das Layout des
 * Regionen-Asset-Passes (FRegionAssetLayout) als Instanced Meshes.
 *
 *  - Baeume: HISM (HierarchicalInstancedStaticMeshComponent) - tausende
 *    Instanzen mit BVH-Culling.
 *  - Ufer-Objekte + Industrie-Objekte: ISM (InstancedStaticMeshComponent) -
 *    deutlich weniger Instanzen, ein Draw-Call je Kategorie.
 *
 * Jede Kategorie bekommt ihr StaticMesh + Material zugewiesen (Details-Panel);
 * ohne Meshes wird nur der Report geloggt (kein Crash).
 */
UCLASS(BlueprintType, ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API URegionAssetSpawnerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	URegionAssetSpawnerComponent();

	/** Platziert die Assets aus dem Layout. */
	void SpawnRegionAssets(const FRegionAssetLayout& Layout);

	/**
	 * Setzt Mesh und Material der drei Kategorien, sofern noch nicht gesetzt.
	 *
	 * Liegt HIER und nicht mehr am WorldBuilder, weil es seit der Verteilung
	 * auf die Chunk-Actors zwei Aufrufer gibt. Zwei Kopien derselben
	 * Pfadliste waeren genau die Sorte Fehler, die sich erst als "in einem
	 * Chunk stehen graue Schachbrett-Kegel" zeigt.
	 */
	void EnsureDefaultAssets();

	/** Entfernt alle Instanzen. */
	void ClearRegionAssets();

	/**
	 * Verankert LEERE Instanz-Komponenten am uebergebenen Punkt.
	 *
	 * Eine ISM ohne Instanzen hat Punkt-Bounds an ihrer eigenen Position
	 * (UE 5.8, InstancedStaticMesh.cpp CalcBoundsImpl); sitzt sie am
	 * Chunk-Actor am Ursprung, zieht sie die Actor-Bounds auf 0,0,0 und
	 * World Partition kann die Zelle nicht raeumlich trennen. Der Aufruf
	 * verschiebt NUR Komponenten mit 0 Instanzen an den Mittelpunkt des
	 * Zell-Inhalts; sobald eine Komponente wieder Instanzen traegt, kehrt
	 * sie beim naechsten Aufruf an ihre Ursprungsposition zurueck.
	 *
	 * Erfasst die drei festen Komponenten (Trees/Waterfront/Industrial)
	 * UND die der Arten-Verteilung (Trees_01../Bushes_01..): SpawnVaried
	 * erzeugt je Modell eine Komponente, auch wenn es leer bleibt.
	 */
	void AnchorEmptyInstanceComponents(const FVector& AnchorLocation);

	// -- Kategorie-Assets -----------------------------------------------------

	/**
	 * Baum-Modelle. Je Eintrag entsteht eine eigene Instanz-Komponente.
	 *
	 * Mehrere Arten statt einer: Ein Wald aus lauter gleichen Baeumen faellt
	 * sofort als Muster auf, und zwar staerker als jede fehlende Textur. Die
	 * Zuordnung Instanz -> Art laeuft ueber einen Hash des STANDORTS, nicht
	 * ueber den Instanzindex - sonst haengt die Art davon ab, in welcher
	 * Streaming-Zelle ein Baum landet, und derselbe Baum waere nach einem
	 * Neubau der Stadt eine andere Sorte.
	 *
	 * Bleibt die Liste leer, greift TreeMesh als einzelnes Modell.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	TArray<UStaticMesh*> TreeMeshes;

	/**
	 * Busch-Modelle. Ein Teil der Baum-Standorte wird damit besetzt.
	 *
	 * Wald besteht nicht nur aus Baumkronen - ohne Unterholz sieht ein
	 * Waldstueck aus wie eine Rasenflaeche mit Staemmen darauf.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	TArray<UStaticMesh*> BushMeshes;

	/**
	 * Sichtweite fuer Baeume in cm; 0 schaltet die Begrenzung ab.
	 *
	 * Mit den echten Modellen stieg die GPU-Zeit von 35 auf 104 ms, und zwar
	 * in Tiefen- und Basisdurchgang (32,0 und 34,8 ms gegen vorher 2,4 und
	 * 3,7). Das ist rohe Dreiecksmenge, nicht Beschattung: Auch die feinste
	 * Detailstufe hat noch 900 bis 10.600 Dreiecke, und ohne Begrenzung
	 * zeichnet die Karte jeden Waldhang bis zum Horizont.
	 *
	 * 400 m ist ein Kompromiss - naeher als die Sichtweite der Stadt, aber
	 * weit genug, dass Waldraender nicht vor den Augen aufpoppen.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets", meta = (ClampMin = "0.0"))
	float TreeCullDistanceCm = 40000.0f;

	/** Sichtweite fuer Buesche in cm; sie sind kleiner und fallen frueher weg. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets", meta = (ClampMin = "0.0"))
	float BushCullDistanceCm = 12000.0f;

	/** Anteil der Baum-Standorte, die stattdessen einen Busch bekommen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BushShare = 0.3f;

	/** Einzelnes Baum-Mesh - Rueckfall, wenn TreeMeshes leer ist. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	UStaticMesh* TreeMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	UMaterialInterface* TreeMaterial = nullptr;

	/** StaticMesh der Ufer-Objekte (Laternen/Baenke; ISM). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	UStaticMesh* WaterfrontMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	UMaterialInterface* WaterfrontMaterial = nullptr;

	/** StaticMesh der Industrie-Objekte (Container; ISM). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	UStaticMesh* IndustrialMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	UMaterialInterface* IndustrialMaterial = nullptr;

	/** Hoehen-Offset ueber dem Terrain gegen Z-Fighting (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets", meta = (ClampMin = "0.0"))
	float ZOffsetCm = 2.0f;

	/**
	 * Grundgroesse eines Baumes in cm (Breite, Tiefe, Hoehe).
	 *
	 * Noetig, weil FPlacedRegionAsset::Scale nur die RELATIVE Streuung
	 * (0,85..1,15) traegt. Mit dem Engine-Kegel als Platzhalter (100 cm) waren
	 * die Baeume dadurch einen Meter hoch - in der Stadt schlicht nicht zu
	 * sehen. 9 m Hoehe entspricht einem ausgewachsenen Strassenbaum.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	//
	// 9 m waren zu klein: Neben vierstoeckigen Haeusern von rund 13 m wirkten
	// die Baeume wie Buesche. Ein ausgewachsener Strassenbaum steht bei 12 bis
	// 20 m, die Buchen der Wiesbadener Waldhaenge bei 25 bis 35 m. 16 m mit
	// 8 m Krone ist der Kompromiss fuer beide Lagen.
	FVector TreeBaseSizeCm = FVector(800.0, 800.0, 1600.0);

	/**
	 * Grundgroesse fuer Modelle, die schon in echten Massen vorliegen.
	 *
	 * Die Baeume aus dem Blendswap-Paket sind 14,8 bis 17,5 m hoch, die
	 * Buesche 3,9 bis 5,3 m - sie brauchen keine Skalierung mehr, nur noch die
	 * Streuung aus FPlacedRegionAsset::Scale. TreeBaseSizeCm gilt weiterhin
	 * fuer den Engine-Kegel als Rueckfall, der 100 cm misst.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	bool bMeshesAreRealScale = true;

	/**
	 * Globaler Groessen-Faktor fuer Baeume (zur Streuung dazu).
	 *
	 * Die Referenz (echtes Wiesbaden, Neroberg/Nerotal) ist von hohen, ueppigen
	 * Strassenbaeumen gepraegt; im Spiel wirkten sie zu klein/licht. > 1 macht
	 * die Kronen voller und die Baeume hoeher, ohne die Modelle zu tauschen.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets", meta = (ClampMin = "0.2"))
	float TreeScaleBoost = 1.35f;

	/** Grundgroesse eines Ufer-Objekts in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	FVector WaterfrontBaseSizeCm = FVector(120.0, 120.0, 90.0);

	/** Grundgroesse eines Industrie-Objekts in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|RegionAssets")
	FVector IndustrialBaseSizeCm = FVector(300.0, 300.0, 400.0);

	// -- Ergebnis (read-only) -------------------------------------------------

	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden|RegionAssets")
	int32 LastSpawnedTreeCount = 0;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden|RegionAssets")
	int32 LastSpawnedWaterfrontCount = 0;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden|RegionAssets")
	int32 LastSpawnedIndustrialCount = 0;

private:
	void SpawnCategory(const TArray<FPlacedRegionAsset>& Assets,
		UStaticMesh* Mesh, UMaterialInterface* Material,
		UHierarchicalInstancedStaticMeshComponent* ISM,
		const FVector& BaseSizeCm);

	/**
	 * Verteilt Baum-Standorte auf mehrere Modelle.
	 *
	 * Je Modell entsteht eine Instanz-Komponente zur Laufzeit; die
	 * vorhandenen drei aus dem Konstruktor reichen dafuer nicht.
	 */
	void SpawnVaried(const TArray<FPlacedRegionAsset>& Assets);

	/** Legt eine Instanz-Komponente fuer ein Modell an. */
	UHierarchicalInstancedStaticMeshComponent* MakeInstanceComponent(
		const FName& Name, UStaticMesh* Mesh);

	/**
	 * Zur Laufzeit angelegte Komponenten der Arten-Verteilung.
	 *
	 * Transient wie die drei festen: Instanz-Komponenten ueberleben das
	 * Speichern der Karte nicht, die Layout-Daten dagegen schon.
	 */
	UPROPERTY(Transient)
	TArray<UHierarchicalInstancedStaticMeshComponent*> VariedInstances;

	UPROPERTY(Transient)
	UHierarchicalInstancedStaticMeshComponent* TreeInstances = nullptr;

	UPROPERTY(Transient)
	UHierarchicalInstancedStaticMeshComponent* WaterfrontInstances = nullptr;

	UPROPERTY(Transient)
	UHierarchicalInstancedStaticMeshComponent* IndustrialInstances = nullptr;
};
