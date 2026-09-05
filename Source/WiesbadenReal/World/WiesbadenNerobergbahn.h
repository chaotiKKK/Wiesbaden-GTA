// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "World/WiesbadenRailTransport.h"

#include "WiesbadenNerobergbahn.generated.h"

class UProceduralMeshComponent;
class UWiesbadenVehicleCameraComponent;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Die Nerobergbahn - Wiesbadens Wasserballast-Standseilbahn von 1888.
 *
 * Strecke und Stationen kommen aus OpenStreetMap (railway=funicular,
 * Betreiber ESWE, eroeffnet 25.09.1888): zwei parallele Gleise vom Nerotal
 * hinauf zum Neroberg, 438 m lang, mit dem Viadukt im unteren Drittel. Die
 * Streckenpunkte sind hier fest einkodiert - die Bahn ist ein Einzelstueck,
 * ein Datenpfad durch den OSM-Parser waere Aufwand ohne zweiten Nutzer.
 *
 * Zwei Wagen laufen im Gegenlauf am Seil, wie beim Vorbild: faehrt der eine
 * bergauf, rollt der andere talwaerts. 7,8 km/h Hoechstgeschwindigkeit
 * (maxspeed aus OSM), Haltezeit in den Stationen.
 *
 * Der Spieler kann MITFAHREN: E-Taste neben einem haltenden Wagen steigt
 * ein, E-Taste unterwegs oder in der Station steigt aus.
 *
 * Die Gleishoehen werden zur Laufzeit vom Gelaende abgetastet. Beim Start
 * ist der Neroberg meist noch nicht gestreamt - bis dahin traegt eine
 * lineare Rampe (83 m Steigung wie beim Vorbild) die Bahn, danach wird auf
 * die echten Hoehen umgebaut.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenNerobergbahn : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenNerobergbahn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Fahrgeschwindigkeit in km/h (OSM: maxspeed 7.8). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bahn", meta = (ClampMin = "1.0"))
	float SpeedKmh = 7.8f;

	/** Haltezeit in den Stationen, Sekunden. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bahn", meta = (ClampMin = "0.0"))
	float DwellSeconds = 12.0f;

	/** Einstiegsreichweite um einen haltenden Wagen, cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bahn", meta = (ClampMin = "100.0"))
	float BoardRangeCm = 450.0f;

private:
	struct FTrackPoint
	{
		FVector Position = FVector::ZeroVector;   // Welt, cm
		double ArcLength = 0.0;                    // ab Talstation, cm
		bool bHeightResolved = false;
	};

	struct FTrack
	{
		TArray<FTrackPoint> Points;
		double TotalLength = 0.0;
	};

	/** Baut die Weltpunkte beider Gleise aus den OSM-Koordinaten. */
	void BuildTracks();

	/** Tastet fehlende Gleishoehen vom Gelaende ab; true, wenn neu geloest. */
	bool ResolveHeights();

	/** Baut Bett und Schienen beider Gleise als Bandgeometrie. */
	void BuildTrackMeshes();

	/** Punkt und Richtung bei Bogenlaenge s auf einem Gleis. */
	void SampleTrack(const FTrack& Track, double S, FVector& OutPos, FVector& OutTangent) const;

	/** Legt einen Wagen als StaticMesh-Komponente an (SM_WbNbWagen). */
	USceneComponent* BuildCar(const TCHAR* Name);

	/**
	 * Setzt Tal-/Bergstation und Viadukt auf die aufgeloeste Trasse.
	 *
	 * Erst nach der Gelaendeabtastung: vorher stuenden die Bauwerke auf der
	 * Rueckfallrampe weit unter dem Hang. Laeuft genau einmal.
	 */
	void PlaceStructures();

	/** Kamera des mitfahrenden Spielers ueber das gemeinsame Fahrzeug-Rig. */
	void CreatePassengerCamera();
	void DestroyPassengerCamera();

	/** Ein- oder Aussteigen des Spielers. */
	void ToggleBoarding();

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	USceneComponent* Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UProceduralMeshComponent* TrackMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	USceneComponent* CarA = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	USceneComponent* CarB = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UStaticMeshComponent* Talstation = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UStaticMeshComponent* Bergstation = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UStaticMeshComponent* Viadukt = nullptr;

	/** Bauwerke bereits gesetzt? */
	bool bStructuresPlaced = false;

	FTrack TrackA;
	FTrack TrackB;

	/** Fahrposition des Wagens A ab Talstation, cm. B laeuft gegenlaeufig. */
	double CablePosition = 0.0;

	/** +1 bergauf (Wagen A), -1 talwaerts, 0 Haltezeit. */
	int32 Direction = +1;

	/** Restliche Haltezeit. */
	float DwellRemaining = 0.0f;

	/** Alle Hoehen aus dem Gelaende geloest und Geometrie neu gebaut? */
	bool bHeightsFinal = false;

	/** Naechster Abtastversuch fuer die Hoehen. */
	float HeightRetryRemaining = 0.0f;

	/** Besitz- und Zustandsdaten der aktuellen Fahrt. */
	WiesbadenRailTransport::FWiesbadenRideSession RideSession;

	/** Flanke der Einstiegstaste. */
	bool bBoardKeyHeld = false;

	/** Gemeinsames Kamera-Rig fuer Follow/Orbit/Cockpit und Mausradzoom. */
	UPROPERTY(Transient)
	UWiesbadenVehicleCameraComponent* PassengerCamera = nullptr;

	/** Entwicklungshilfe: Profil, Clearance und Segmentgrenzen anzeigen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bahn|Debug")
	bool bDebugRailway = false;

#if !UE_BUILD_SHIPPING
	void DrawRailwayDebug();
#endif
};
