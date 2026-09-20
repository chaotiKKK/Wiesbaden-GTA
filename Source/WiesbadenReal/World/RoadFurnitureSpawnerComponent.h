// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "GIS/RoadFurnitureGenerator.h"
#include "World/StreetFurnitureShapes.h"

#include "RoadFurnitureSpawnerComponent.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UPointLightComponent;
class UMaterialInterface;
class UStaticMesh;

/**
 * Visueller Ausstattungs-Spawner: rendert die Platzierungsdaten des
 * Strassenausstattungs-Passes (FRoadFurnitureLayout) als echte Meshes.
 *
 *  - Schilder: Tafel (Engine-Plane, eine Instanz je Zeichen) + Mast
 *    (Engine-Zylinder, instanziert). Die Tafel bekommt je Zeichen ein
 *    Material-Instanz-Dynamic ueber WiesbadenSignAssets::CreateMaterial
 *    (entspricht ResolveSignMaterial); Tafeln mit gleichem Zeichen teilen
 *    sich einen ISM und damit einen Draw-Call.
 *  - Leitpfosten: Pfosten (Zylinder) + Reflektor (duenne Box) als ISMs.
 *  - Markierungen: Haltlinie (durchgehend) und Wartelinie (unterbrochen) als
 *    flache Quad-Instanzen auf der Fahrbahn.
 *
 * Alle Basismeshes stammen aus /Engine/BasicShapes; ohne zugewiesene
 * Materialien rendern sie mit dem UE-Default-Material (Warn-Log).
 */
UCLASS(BlueprintType, ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API URoadFurnitureSpawnerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	URoadFurnitureSpawnerComponent();

	virtual void BeginPlay() override;

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Platziert Schilder, Leitpfosten und Markierungen aus dem Layout. */
	void SpawnFurniture(const FRoadFurnitureLayout& Layout);

	/** Entfernt alle erzeugten Meshes. */
	void ClearFurniture();

	// -- Schilder (Tafel + Mast) ---------------------------------------------

	/** Content-Ordner der Schild-Texturen (Sign_<VzKat>.png). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Schilder")
	FString SignTextureFolder = TEXT("/Game/Textures/TrafficSigns/");

	/** Basismaterial der Schild-Tafel; die Textur wird als Parameter gesetzt. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Schilder")
	UMaterialInterface* SignMaterial = nullptr;

	/** Parameter-Name der Schild-Textur im SignMaterial. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Schilder")
	FName SignTextureParameterName = TEXT("SignTexture");

	/** Material der Schildermasten (leer = UE-Default-Material). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Schilder")
	UMaterialInterface* PoleMaterial = nullptr;

	/** Kantenlaenge der Schild-Tafel (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Schilder", meta = (ClampMin = "10.0"))
	float SignSizeCm = 60.0f;

	/** Hoehe des Schildermasts (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Schilder", meta = (ClampMin = "10.0"))
	float SignPoleHeightCm = 220.0f;

	/** Radius des Schildermasts (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Schilder", meta = (ClampMin = "0.5"))
	float SignPoleRadiusCm = 3.0f;

	// -- Leitpfosten ---------------------------------------------------------

	/** Material der Leitpfosten (leer = PoleMaterial/Default). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Leitpfosten")
	UMaterialInterface* DelineatorMaterial = nullptr;

	/** Material der Reflektor-Platte (leer = UE-Default-Material). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Leitpfosten")
	UMaterialInterface* ReflectorMaterial = nullptr;

	/** Hoehe des Leitpfostens (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Leitpfosten", meta = (ClampMin = "10.0"))
	float DelineatorHeightCm = 100.0f;

	/** Radius des Leitpfostens (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Leitpfosten", meta = (ClampMin = "0.5"))
	float DelineatorRadiusCm = 4.0f;

	/** Hoehe der Reflektor-Platte ueber dem Boden (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Leitpfosten", meta = (ClampMin = "0.0"))
	float ReflectorHeightCm = 60.0f;

	/** Reflektor-Masse (Dicke, Breite, Hoehe) in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Leitpfosten")
	FVector ReflectorSizeCm = FVector(2.0f, 10.0f, 15.0f);

	// -- Markierungen --------------------------------------------------------

	/** Material der Fahrbahnmarkierungen (Halt-/Wartelinie, weiss). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Markierungen")
	UMaterialInterface* MarkingMaterial = nullptr;

	/** Hoehen-Offset ueber der Fahrbahn gegen Z-Fighting (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Markierungen", meta = (ClampMin = "0.0"))
	float MarkingZOffsetCm = 1.0f;

	/** Strichlaenge der unterbrochenen Wartelinie (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Markierungen", meta = (ClampMin = "10.0"))
	float GiveWayDashLengthCm = 100.0f;

	/** Lueckenlaenge der unterbrochenen Wartelinie (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Markierungen", meta = (ClampMin = "10.0"))
	float GiveWayGapLengthCm = 100.0f;

	// -- Strassenlaternen ----------------------------------------------------

	/** Material von Mast und Ausleger (leer = UE-Default-Material). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Laternen")
	UMaterialInterface* LampPostMaterial = nullptr;

	/** Masthoehe in cm. Deutsche Strassenleuchten stehen bei 4 bis 10 m. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Laternen", meta = (ClampMin = "100.0"))
	float LampPostHeightCm = 700.0f;

	/** Mastradius in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Laternen", meta = (ClampMin = "1.0"))
	float LampPostRadiusCm = 9.0f;

	/**
	 * Wie viele Laternen gleichzeitig ein echtes Licht tragen.
	 *
	 * Die Masten selbst kosten als ISM praktisch nichts und stehen deshalb
	 * alle. Punktlichter dagegen sind teuer - es leuchten nur die naechsten.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Laternen", meta = (ClampMin = "0"))
	int32 MaxActiveLampLights = 48;

	/** Reichweite einer Leuchte in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Laternen", meta = (ClampMin = "100.0"))
	//
	// 18 m liessen zwischen den Masten (30 m Abstand) dunkle Luecken. 26 m
	// lassen die Lichtkegel einander beruehren, wie an einer echten Strasse.
	float LampLightRadiusCm = 2600.0f;

	/** Lichtstrom je Leuchte in Candela. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Laternen", meta = (ClampMin = "0.0"))
	//
	// 6.000 waren zu wenig: Ein Punktlicht verteilt seine Leistung auf die
	// ganze Kugel, waehrend ein Scheinwerfer sie buendelt. In 7 m Masthoehe
	// kamen davon rund 10 Lux auf der Fahrbahn an - gegen die Belichtung des
	// Nachtbildes praktisch nichts. Gemessen war die naechste Leuchte 9 m
	// entfernt, sichtbar und eingeschaltet, und trotzdem war im Bild kein
	// Lichtkegel zu erkennen.
	float LampLightIntensity = 40000.0f;

	/** Lichtfarbe - warmweiss wie Natriumdampf-/LED-Leuchten. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Laternen")
	FLinearColor LampLightColor = FLinearColor(1.0f, 0.78f, 0.48f);

	// -- Sichtweiten ---------------------------------------------------------
	//
	// Die Ausstattung lag in EINFACHEN InstancedStaticMeshComponents. Ein
	// solches Component zeichnet ALLE seine Instanzen, sobald es sichtbar ist -
	// ohne Aussortieren nach Entfernung und ohne LOD-Auswahl je Instanz. Bei
	// 50.875 Schildern, 355.624 Leitpfosten und Reflektoren und 70.874
	// Laternenmasten auf einem IMMER GELADENEN Actor sind das rund 480.000
	// Instanzen in jedem Bild.
	//
	// Als Hierarchical-Variante sortiert die Engine je Instanz nach Entfernung
	// aus. Die Grenzen darunter kappen zusaetzlich: Ein Leitpfosten ist auf
	// 150 m ohnehin nicht mehr zu erkennen.

	/** Sichtweite der Leitpfosten und Reflektoren in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Sichtweite", meta = (ClampMin = "0.0"))
	float DelineatorCullDistanceCm = 15000.0f;

	/** Sichtweite der Schilder in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Sichtweite", meta = (ClampMin = "0.0"))
	float SignCullDistanceCm = 25000.0f;

	/** Sichtweite der Laternenmasten in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Sichtweite", meta = (ClampMin = "0.0"))
	float LampPostCullDistanceCm = 40000.0f;

	/**
	 * Sichtweite der Strassenmoebel in cm.
	 *
	 * Kuerzer als bei Schildern und Masten: eine Bank ist kein Wegweiser,
	 * sondern Beiwerk des Gehwegs, auf dem man steht. Auf 100 m traegt sie
	 * nichts mehr zum Bild bei und kostet nur Instanzen.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Sichtweite", meta = (ClampMin = "0.0"))
	float FurnitureCullDistanceCm = 10000.0f;

	// -- Kollision -----------------------------------------------------------

	UPROPERTY(EditAnywhere, Category = "Wiesbaden")
	bool bCreateCollision = false;

	// -- Ergebnis (read-only) ------------------------------------------------

	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden")
	int32 LastSpawnedSignCount = 0;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden")
	int32 LastSpawnedDelineatorCount = 0;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden")
	int32 LastSpawnedMarkingCount = 0;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden")
	int32 LastSpawnedLampCount = 0;

	/** Moebel (Baenke, Poller, ...), nicht deren Einzelteile. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden")
	int32 LastSpawnedFurnitureCount = 0;

	/** Erzeugte Teil-Instanzen - die Zahl, die das Rendering kostet. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden")
	int32 LastSpawnedFurniturePartCount = 0;

	// -- Strassenmoebel ------------------------------------------------------

	/** Masse der Moebel (Gestaltung, kein Code). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Moebel")
	FStreetFurnitureDimensions FurnitureDimensions;

	/** Material der Metallteile (leer = UE-Default-Material). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Moebel")
	UMaterialInterface* FurnitureMetalMaterial = nullptr;

	/** Material der Holzteile (Sitzlatten, Tischplatten). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Moebel")
	UMaterialInterface* FurnitureWoodMaterial = nullptr;

	/** Material der farbigen Teile (Briefkasten, Hydrant, Automatenfront). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Moebel")
	UMaterialInterface* FurnitureSignalMaterial = nullptr;

private:
	void SpawnSigns(const TArray<FSignInstance>& Signs, UStaticMesh* PanelMesh, UStaticMesh* PoleMesh);
	void SpawnDelineators(const TArray<FDelineatorInstance>& Delineators, UStaticMesh* CylinderMesh, UStaticMesh* CubeMesh);
	void SpawnMarkings(const TArray<FMarkingInstance>& Markings, UStaticMesh* PlaneMesh);
	void SpawnStreetLamps(const TArray<FStreetLampInstance>& Lamps, UStaticMesh* CylinderMesh);

	/**
	 * Setzt die Strassenmoebel als Instanzen.
	 *
	 * Ein HISM je Kombination aus Basismesh und Werkstoff (hoechstens sechs);
	 * alle Baenke der Stadt teilen sich damit einen Draw-Call fuer ihre
	 * Holzteile und einen fuer die Wangen.
	 */
	void SpawnStreetFurniture(
		const TArray<FFurnitureInstance>& Furniture,
		UStaticMesh* CubeMesh,
		UStaticMesh* CylinderMesh);

	/**
	 * Laedt das gebaute Mesh einer Art/Variante - oder nullptr.
	 *
	 * Fehlt es (frischer Klon: die .uassets sind gitignored), faellt
	 * SpawnStreetFurniture auf die Primitivteile zurueck. Das Ergebnis wird
	 * gemerkt, damit nicht je Moebel ein LoadObject auf ein fehlendes Asset
	 * laeuft - bei 2.000 Moebeln ist das der Unterschied zwischen einem
	 * Fehlversuch und zweitausend.
	 */
	UStaticMesh* ResolveFurnitureMesh(EStreetFurnitureKind Kind, int32 Variant);

	/** Eine Protokollzeile je Lauf - je Art, damit eine leere Kategorie auffaellt. */
	void ProtokolliereMoebel(const TArray<FFurnitureInstance>& Furniture,
		int32 TeilInstanzen, int32 Zeichengruppen);

	/** Liefert (und erzeugt bei Bedarf) den HISM einer Mesh/Werkstoff-Paarung. */
	UHierarchicalInstancedStaticMeshComponent* GetFurnitureInstances(
		EFurnitureMeshKind Mesh,
		EFurnitureMaterialKind Material,
		UStaticMesh* CubeMesh,
		UStaticMesh* CylinderMesh);

	/** Setzt die begrenzte Zahl echter Leuchten auf die Laternen um den Spieler. */
	void UpdateLampLights(const FVector& Reference);

	/** Legt den begrenzten Vorrat an Punktlichtern an (idempotent). */
	void CreateLampLightPool();

	// Schildermasten (gemeinsames Material) -> ISM.
	UPROPERTY(Transient)
	UHierarchicalInstancedStaticMeshComponent* SignPoleInstances = nullptr;

	// Tafeln: ein ISM je eindeutiger SignId (gemeinsame MID), zur Laufzeit erzeugt.
	UPROPERTY(Transient)
	TArray<UHierarchicalInstancedStaticMeshComponent*> SignPanelInstances;

	UPROPERTY(Transient)
	UHierarchicalInstancedStaticMeshComponent* DelineatorPostInstances = nullptr;

	UPROPERTY(Transient)
	UHierarchicalInstancedStaticMeshComponent* DelineatorReflectorInstances = nullptr;

	UPROPERTY(Transient)
	UHierarchicalInstancedStaticMeshComponent* MarkingInstances = nullptr;

	// "30"-Zonensymbole -> eigenes ISM, weil sie ein anderes (maskiertes)
	// Material tragen als die weissen Linien in MarkingInstances.
	UPROPERTY(Transient)
	UHierarchicalInstancedStaticMeshComponent* Zone30Instances = nullptr;

	// Laternenmasten -> ein ISM, ein Draw-Call.
	UPROPERTY(Transient)
	UHierarchicalInstancedStaticMeshComponent* LampPostInstances = nullptr;

	// Moebelteile: ein ISM je Mesh/Werkstoff-Paarung, zur Laufzeit erzeugt
	// (wie die Schild-Tafeln). Der Index ist Mesh * EFurnitureMaterialKind::MAX
	// + Werkstoff; leere Paarungen bleiben nullptr und kosten nichts.
	UPROPERTY(Transient)
	TArray<UHierarchicalInstancedStaticMeshComponent*> FurnitureInstances;

	// Gebaute Moebel-Meshes: ein ISM je Art/Variante, eine Instanz je Moebel.
	// Der Index ist Art * 2 + (Variante > 0).
	UPROPERTY(Transient)
	TArray<UHierarchicalInstancedStaticMeshComponent*> FurnitureMeshInstances;

	// Gemerkte Mesh-Suche (nullptr-Eintrag = gesucht und nicht gefunden).
	UPROPERTY(Transient)
	TArray<UStaticMesh*> FurnitureMeshCache;
	TArray<bool> FurnitureMeshSearched;

	/** Wie viele Moebel der gebauten Meshes statt der Primitive bekamen. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden")
	int32 LastFurnitureWithMeshCount = 0;

	// Echte Punktlichter, auf MaxActiveLampLights begrenzt.
	UPROPERTY(Transient)
	TArray<UPointLightComponent*> LampLights;

	/**
	 * Standorte aller Laternen - Grundlage fuer die Auswahl der Leuchten.
	 *
	 * MUSS eine gespeicherte UPROPERTY sein, kein blosses TArray.
	 *
	 * In der gebackenen Karte laeuft SpawnFurniture nicht mehr: Die Masten
	 * liegen als ISM-Instanzen in der Map, aber alles, was nur waehrend des
	 * Builds im Speicher stand, ist beim Laden weg. Als einfaches Member war
	 * diese Liste im Spiel leer - die Laternenmasten standen sichtbar da und
	 * trugen kein einziges Licht. Im Build-Log stand trotzdem
	 * "3332 Laternen (48 davon mit Licht)", was aus dem Editor-Lauf stammte.
	 */
	UPROPERTY()
	TArray<FVector> LampLocations;

	/** Bezugspunkt der letzten Auswahl; erspart das Umsetzen bei Stillstand. */
	FVector LastLampReference = FVector(TNumericLimits<double>::Max());

	/** Sekunden bis zur naechsten Pruefung. */
	float LampUpdateCountdown = 0.0f;
};
