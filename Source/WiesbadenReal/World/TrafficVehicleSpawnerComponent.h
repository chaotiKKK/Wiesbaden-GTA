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
 * Sichtbarer Stadtverkehr: zeichnet die Fahrzeuge aus
 * UWiesbadenCitySubsystem::GetTrafficVehicles() als InstancedStaticMesh.
 *
 *  - Drei Tripo-Modelle (WiesbadenTrafficCars: Golf III, Peugeot 207, T6
 *    California), je Typ eine Karosserie und vier eigene Raeder - fuenf
 *    Nanite-ISM-Gruppen je Typ. Die Raeder rollen mit dem Tempo der Physik und
 *    schlagen vorn ein; die Karosserie nickt und wankt (Gewichtsverlagerung)
 *    und folgt der Steigung.
 *  - Jedes Fahrzeug behaelt seinen Instanzplatz, solange es sichtbar ist:
 *    frueher wurden die Gruppen je Bild geleert und neu gefuellt - dabei ging
 *    die Vorbild-Lage verloren, und TSR/Bewegungsunschaerfe zogen Schlieren.
 *  - Sichtweite CullRadiusMeters = FWiesbadenTrafficSettings::DrawDistanceMeters:
 *    innerhalb davon setzt die Simulation im Blick kein Fahrzeug ein.
 */
UCLASS(BlueprintType, ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UTrafficVehicleSpawnerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UTrafficVehicleSpawnerComponent();

	/**
	 * Aktualisiert die Fahrzeug-Instanzen aus der Simulation (je Bild).
	 *
	 * @param bNight Scheinwerfer und Rueckleuchten an. Kommt von der
	 *               24-h-Beleuchtung; die Simulation selbst kennt keine Uhrzeit.
	 */
	void UpdateVehicles(const TArray<FTrafficVehicle>& Vehicles, bool bNight = false);

	/** Entfernt alle Fahrzeug-Instanzen. */
	void ClearVehicles();

	/** Karosserie je Typ (Index wie WiesbadenTrafficCars::Types()). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> BodyMeshes;

	/** Raeder je Typ: Index Typ * 4 + Rad (FL, FR, RL, RR). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> WheelMeshes;

	/** Sichtweite in Metern (0 = alle) - muss zur Simulation passen
	 *  (FWiesbadenTrafficSettings::DrawDistanceMeters, Test Vehicles.Traffic.SpawnOutOfView). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr", meta = (ClampMin = "0.0"))
	float CullRadiusMeters = 550.0f;

	/** Anzahl der tatsaechlich gezeichneten Fahrzeuge (Diagnose). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden|Verkehr")
	int32 LastVisibleVehicleCount = 0;

	/**
	 * Gesetzte Lampen des letzten Bildes: Bremse, Blinker, Scheinwerfer,
	 * Rueckleuchte (Diagnose).
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden|Verkehr")
	TArray<int32> LastLampCounts;

	// -- Kollision ----------------------------------------------------------

	/**
	 * Anzahl der Kollisionskoerper, die den naechstgelegenen Fahrzeugen
	 * folgen. Die Fahrzeuge selbst sind InstancedStaticMesh ohne Kollision;
	 * eine kleine Zahl unsichtbarer Koerper wandert mit den Fahrzeugen mit,
	 * die dem Spieler nahe genug sind, um ihn beruehren zu koennen.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr|Kollision", meta = (ClampMin = "0", ClampMax = "128"))
	int32 CollisionProxyCount = 24;

	/** Umkreis, in dem Kollisionskoerper mitgefuehrt werden, in Metern. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr|Kollision", meta = (ClampMin = "5.0"))
	float CollisionRadiusMeters = 60.0f;

	/** Ersatzmass eines Verkehrsfahrzeugs in cm (halbe Laenge/Breite/Hoehe), falls kein Mesh. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Verkehr|Kollision")
	FVector VehicleCollisionExtent = FVector(207.0, 85.0, 75.0);

	/** Zahl der aktuell aktiven Kollisionskoerper (Diagnose). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Wiesbaden|Verkehr|Kollision")
	int32 ActiveCollisionProxyCount = 0;

	/**
	 * Lampenpaar im WELT-Raum (datenrein, testbar): links am Katalogpunkt
	 * (Unreal: links = -Y), rechts gespiegelt.
	 */
	static void ComputeLampTransforms(
		const FTransform& BodyTransform,
		const FVector& LeftLampCm,
		FTransform& OutLeft,
		FTransform& OutRight);

	/**
	 * Waehlt die dem Bezugspunkt naechsten Fahrzeuge (datenrein, testbar).
	 * Horizontal gemessen (die Stadt hat ueber 100 m Hoehenunterschied).
	 *
	 * @param OutIndices Indizes in Vehicles, aufsteigend nach Abstand,
	 *                   hoechstens MaxCount Eintraege.
	 */
	static void SelectNearestVehicles(
		const TArray<FTrafficVehicle>& Vehicles,
		const FVector& Center,
		double RadiusCm,
		int32 MaxCount,
		TArray<int32>& OutIndices);

	/** Fahrgestell im Welt-Raum: Karosserie-Ort, Gier, Steigung (hier sitzen die Raeder). */
	static FTransform ComputeChassisTransform(const FTrafficVehicle& Vehicle);

	/** Karosserie im Welt-Raum: Fahrgestell plus Nicken/Wanken der Federung. */
	static FTransform ComputeBodyTransform(const FTrafficVehicle& Vehicle);

protected:
	virtual void BeginPlay() override;

private:
	/** Beobachter fuer das Culling (Player-Pawn oder Streaming-Quelle). */
	FVector GetObserverLocation() const;

	/** Legt je Typ die fuenf ISM-Gruppen an (einmalig) und misst die Radmitten. */
	void EnsureInstancePools();

	/** Legt die vier Lampen-Gruppen an (Bremse, Blinker, Scheinwerfer, Rueckleuchte). */
	void EnsureLampPools();

	/** Setzt die Lampen-Instanzen aus den sichtbaren Fahrzeugen neu. */
	void UpdateLamps(const TArray<const FTrafficVehicle*>& Visible, bool bNight);

	/** Legt den Pool der Kollisionskoerper an (einmalig). */
	void EnsureCollisionProxies();

	/** Fuehrt die Kollisionskoerper den naechsten Fahrzeugen nach. */
	void UpdateCollisionProxies(const TArray<FTrafficVehicle>& Vehicles);

	/** Instanzgruppen eines Fahrzeugtyps mit stabilen Plaetzen je Fahrzeug. */
	struct FTypePool
	{
		/** 0 Karosserie, 1..4 Raeder FL, FR, RL, RR. */
		TArray<UInstancedStaticMeshComponent*> Parts;
		/** Radmitte je Rad (Mitte der Rad-Bounds, Fahrzeugrahmen). */
		TArray<FVector> WheelCenters;
		/** Instanzplatz -> Fahrzeug-Id (INDEX_NONE = frei, versteckt). */
		TArray<int32> SlotVehicle;
		TMap<int32, int32> VehicleSlot;
	};
	TArray<FTypePool> Pools;
	bool bPoolsBuilt = false;

	/** Alle Instanzgruppen (fuer die Speicherbereinigung). */
	UPROPERTY(Transient)
	TArray<UInstancedStaticMeshComponent*> PoolComponents;

	UPROPERTY(Transient)
	TArray<UBoxComponent*> CollisionProxies;

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
};
