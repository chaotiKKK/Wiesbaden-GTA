// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "GIS/WiesbadenTrafficSimulation.h"

#include "TrafficVehicleSpawnerComponent.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/**
 * Sichtbare Verkehrs-Simulation: rendert die Fahrzeuge aus
 * UWiesbadenCitySubsystem::GetTrafficVehicles() als InstancedStaticMesh.
 *
 *  - ISM-Pool: Ein UInstancedStaticMeshComponent je Palette-Farbe; die
 *    Transform je Instanz wird pro Tick aus der Simulation uebernommen
 *    (Location + Yaw aus der Fahrtrichtung, deterministisch via
 *    FWiesbadenTrafficSimulation::PlaceTrafficVehicles).
 *  - Farbvariation: VehicleMaterial wird je Palette-Eintrag als MID erzeugt
 *    (VehicleColorParameterName, Default "VehicleColor"); ohne Palette nur
 *    ein ISM mit dem Basis-Material.
 *  - 1-km-Culling: Fahrzeuge ausserhalb CullRadiusMeters um den Player-Pawn
 *    (oder die Streaming-Quelle) werden nicht instanziert - SPEC Phase 12.
 *
 * Die Platzierungslogik ist datenrein und in den TrafficSimulation-Tests
 * abgedeckt; diese Komponente ist nur der duenne Render-/Tick-Ueberzug.
 */
UCLASS(BlueprintType, ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UTrafficVehicleSpawnerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UTrafficVehicleSpawnerComponent();

	/** Aktualisiert die Fahrzeug-Instanzen aus der Simulation (idempotent). */
	/**
	 * Aktualisiert die Fahrzeug-Instanzen aus der Simulation (idempotent).
	 *
	 * @param bNight Scheinwerfer und Rueckleuchten an. Kommt von der
	 *               24-h-Beleuchtung; die Simulation selbst kennt keine Uhrzeit.
	 */
	void UpdateVehicles(const TArray<FTrafficVehicle>& Vehicles, bool bNight = false);

	/** Entfernt alle Fahrzeug-Instanzen. */
	void ClearVehicles();

	/** Das Fahrzeug-Mesh (Platzhalter oder echtes Modell). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr")
	UStaticMesh* VehicleMesh = nullptr;

	/** Basismaterial; je Palette-Farbe wird eine MID erzeugt. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr")
	UMaterialInterface* VehicleMaterial = nullptr;

	/** Parameter-Name der Farbauswahl im VehicleMaterial. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr")
	FName VehicleColorParameterName = TEXT("VehicleColor");

	/** Farbpalette (eine Instanz-Gruppe je Farbe; leer = nur eine Gruppe). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr")
	TArray<FLinearColor> ColorPalette;

	/** Radius in Metern, innerhalb dessen Fahrzeuge gerendert werden (0 = alle). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr", meta = (ClampMin = "0.0"))
	float CullRadiusMeters = 1000.0f;

	/** Anzahl der tatsaechlich instanziierten Fahrzeuge (Diagnose). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden|Verkehr")
	int32 LastVisibleVehicleCount = 0;

	/**
	 * Gesetzte Lampen des letzten Bildes: Bremse, Blinker, Scheinwerfer,
	 * Rueckleuchte (Diagnose).
	 *
	 * Auf einem Nachtbild ist nicht zu unterscheiden, ob eine rote Flaeche vom
	 * neuen Bremslicht kommt oder vom Eigenlicht des Spielerautos. Gezaehlt
	 * ist sie eindeutig.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden|Verkehr")
	TArray<int32> LastLampCounts;

	// -- Kollision ----------------------------------------------------------

	/**
	 * Anzahl der Kollisionskoerper, die den naechstgelegenen Fahrzeugen
	 * folgen.
	 *
	 * Die Fahrzeuge selbst werden als InstancedStaticMesh gezeichnet und
	 * haben keine Kollision: der Instanz-Pool wird jeden Tick neu aufgebaut,
	 * und ein Kollisionsneuaufbau fuer hunderte Instanzen je Frame waere
	 * unbezahlbar. Stattdessen wandert eine kleine Zahl unsichtbarer Koerper
	 * mit den Fahrzeugen mit, die dem Spieler nahe genug sind, um ihn
	 * ueberhaupt beruehren zu koennen.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr|Kollision", meta = (ClampMin = "0", ClampMax = "128"))
	int32 CollisionProxyCount = 24;

	/** Umkreis, in dem Kollisionskoerper mitgefuehrt werden, in Metern. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr|Kollision", meta = (ClampMin = "5.0"))
	float CollisionRadiusMeters = 60.0f;

	/** Abmessungen eines Verkehrsfahrzeugs in cm (Laenge/Breite/Hoehe). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr|Kollision")
	FVector VehicleCollisionExtent = FVector(207.0, 77.0, 77.0);

	/** Zahl der aktuell aktiven Kollisionskoerper (Diagnose). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden|Verkehr|Kollision")
	int32 ActiveCollisionProxyCount = 0;

	/**
	 * Waehlt die dem Bezugspunkt naechsten Fahrzeuge (datenrein, testbar).
	 *
	 * Horizontal gemessen: die Stadt hat ueber 100 m Hoehenunterschied, eine
	 * 3D-Messung wuerde am Hang Fahrzeuge bevorzugen, die in der Draufsicht
	 * weiter weg sind.
	 *
	 * @param OutIndices Indizes in Vehicles, aufsteigend nach Abstand,
	 *                   hoechstens MaxCount Eintraege.
	 */
	/**
	 * Lampenpunkte eines Fahrzeugs im WELT-Raum (datenrein, testbar).
	 *
	 * Die Abstaende kommen aus den Mesh-Bounds, nicht aus festen Zahlen: sonst
	 * haengen die Lampen in der Luft, sobald ein anderes Fahrzeugmodell
	 * einzieht.
	 */
	static void ComputeLampTransforms(
		const FTransform& VehicleTransform,
		const FVector& BoundsOrigin,
		const FVector& BoundsExtent,
		bool bFront,
		FTransform& OutLeft,
		FTransform& OutRight);

	static void SelectNearestVehicles(
		const TArray<FTrafficVehicle>& Vehicles,
		const FVector& Center,
		double RadiusCm,
		int32 MaxCount,
		TArray<int32>& OutIndices);

protected:
	virtual void BeginPlay() override;

private:
	/** Beobachter fuer das Culling (Player-Pawn oder Streaming-Quelle). */
	FVector GetObserverLocation() const;

	/** Baut die ISM-Gruppen (eine je Palette-Farbe) einmalig auf. */
	void EnsureInstancePools();

	/**
	 * Legt die vier Lampen-Gruppen an (Bremse, Blinker, Scheinwerfer, Rueckleuchte).
	 *
	 * Eigene Instanzen statt emissiver Materialslots: das Verkehrs-Mesh des
	 * Kaefers hat nur vier allgemeine Slots (nachgesehen, nicht vermutet), also
	 * keinen, den man je Fahrzeug leuchten lassen koennte. Ein winziger Wuerfel
	 * je Lampe kostet im ISM praktisch nichts und traegt die Farbe selbst.
	 */
	void EnsureLampPools();

	/** Setzt die Lampen-Instanzen aus den platzierten Fahrzeugen neu. */
	void UpdateLamps(const TArray<FPlacedTrafficVehicle>& Placed, bool bNight);


	/** Legt den Pool der Kollisionskoerper an (einmalig). */
	void EnsureCollisionProxies();

	/** Fuehrt die Kollisionskoerper den naechsten Fahrzeugen nach. */
	void UpdateCollisionProxies(const TArray<FTrafficVehicle>& Vehicles);

	UPROPERTY(Transient)
	TArray<UInstancedStaticMeshComponent*> VehicleInstances;

	UPROPERTY(Transient)
	TArray<UBoxComponent*> CollisionProxies;

	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> InstanceMaterials;

	/** Zahl der Lampen-Gruppen: Bremse, Blinker, Scheinwerfer, Rueckleuchte. */
	static constexpr int32 LampPoolCount = 4;

	/** Lampen-Gruppen: 0 Bremse, 1 Blinker, 2 Scheinwerfer, 3 Rueckleuchte. */
	UPROPERTY(Transient)
	TArray<UInstancedStaticMeshComponent*> LampInstances;

	/** Material je Lampen-Gruppe (Bremse, Blinker, Scheinwerfer, Rueckleuchte). */
	UPROPERTY(Transient)
	TArray<UMaterialInterface*> LampMaterials;

	/** Wuerfel als Lampenkoerper (Engine-Grundform). */
	UPROPERTY(Transient)
	UStaticMesh* LampMesh = nullptr;

	/** Letzte Fahrzeug-Ids je Instanz-Index (stabile Zuordnung). */
	TArray<int32> LastVehicleIds;
};
