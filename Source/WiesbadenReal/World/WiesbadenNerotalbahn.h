// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "World/WiesbadenRailTransport.h"

#include "WiesbadenNerotalbahn.generated.h"

class UProceduralMeshComponent;
class USceneComponent;
class UWiesbadenVehicleCameraComponent;

/**
 * Die Nerotalbahn - Wiesbadens historische Talstrassenbahn.
 *
 * Erste Pferdebahn 1875 (Wiesbaden Tramways Company), ab 1888 meterspurige
 * Dampfbahn, ab 1900 elektrisch (SEG), Einstellung des Strassenbahnbetriebs
 * bis 1955. Sie verband das Nerotal ueber die Taunusstrasse mit Kochbrunnen,
 * Wilhelmstrasse und Rheinstrasse. Nachgebildet ist der obere Abschnitt vom
 * Nerotal-Kopf die Taunusstrasse hinunter zum Kranzplatz - Streckenpunkte aus
 * OpenStreetMap (Taunusstrasse), Terminus im Nerotal ergaenzt.
 *
 * Anders als die Nerobergbahn ist dies eine EINGEBETTETE Strassenbahn: eine
 * gepflasterte Trasse mit meterspurigen Rillenschienen, kein Viadukt. Ein
 * einzelner historischer Triebwagen pendelt die Strecke auf und ab und haelt
 * an beiden Endpunkten (Nerotal-Kopf und Kranzplatz) - anders als die
 * gegenlaeufige Nerobergbahn faehrt er allein.
 *
 * Der Spieler kann MITFAHREN: E-Taste neben dem haltenden Wagen steigt ein,
 * E-Taste erneut steigt aus - dasselbe Fahrgast-Rig wie bei der Nerobergbahn
 * (Aussen-/Innenkamera ueber UWiesbadenVehicleCameraComponent).
 *
 * Die Gleishoehen werden zur Laufzeit vom Gelaende abgetastet und ueber
 * denselben korrigierten Profil-Pfad wie die Nerobergbahn gelegt
 * (WiesbadenRailTransport::StationRailEndpoints + BuildConstrainedGrade-
 * Profile): die Streckenenden RUHEN auf dem Terrain, dazwischen ein
 * steigungsbegrenztes Profil - so haengt nichts in der Luft.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenNerotalbahn : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenNerotalbahn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Groesste zulaessige Steigung der Trasse (Strassenbahn: sanft). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Nerotalbahn", meta = (ClampMin = "0.01"))
	float MaxGrade = 0.10f;

	/** Schienenoberkante ueber dem Gelaende in cm (eingebettet, knapp ueber Strasse). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Nerotalbahn", meta = (ClampMin = "0.0"))
	float RailClearanceCm = 6.0f;

	/** Fahrgeschwindigkeit des Triebwagens in km/h (historische Talbahn, gemaechlich). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Nerotalbahn", meta = (ClampMin = "1.0"))
	float SpeedKmh = 18.0f;

	/** Haltezeit an den beiden Endpunkten, Sekunden. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Nerotalbahn", meta = (ClampMin = "0.0"))
	float DwellSeconds = 8.0f;

	/** Einstiegsreichweite um den haltenden Wagen, cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Nerotalbahn", meta = (ClampMin = "100.0"))
	float BoardRangeCm = 450.0f;

private:
	struct FTrackPoint
	{
		FVector Position = FVector::ZeroVector;   // Welt, cm
		double ArcLength = 0.0;                    // ab Nerotal-Terminus, cm
		bool bHeightResolved = false;
	};

	/** Baut die Weltpunkte der Strecke aus den OSM-Koordinaten. */
	void BuildTrack();

	/** Tastet fehlende Gleishoehen vom Gelaende ab; legt bei Vollstaendigkeit
	 *  das steigungsbegrenzte Profil auf und baut die Trasse neu. */
	bool ResolveHeights();

	/** Baut Pflasterbett und zwei Rillenschienen als Bandgeometrie. */
	void BuildTrackMesh();

	/** Baut den historischen Triebwagen als einfache Bandgeometrie (Kasten,
	 *  Fensterband, Dach) mit Vertexfarben - kein externes Mesh noetig. */
	void BuildCarMesh();

	/** Uebernimmt die aufgeloesten Gleispunkte in den Fahrpfad des Wagens. */
	void RefreshCarPath();

	/** Kamera des mitfahrenden Spielers ueber das gemeinsame Fahrzeug-Rig. */
	void CreatePassengerCamera();
	void DestroyPassengerCamera();

	/** Ein- oder Aussteigen des Spielers am haltenden Wagen. */
	void ToggleBoarding();

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Nerotalbahn")
	USceneComponent* Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Nerotalbahn")
	UProceduralMeshComponent* TrackMesh = nullptr;

	/** Pivot des Triebwagens - wird je Tick auf die Strecke gesetzt. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Nerotalbahn")
	USceneComponent* Car = nullptr;

	/** Wagenkasten als Bandgeometrie, am Pivot haengend. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Nerotalbahn")
	UProceduralMeshComponent* CarMesh = nullptr;

	TArray<FTrackPoint> Points;
	double TotalLength = 0.0;

	/** Alle Hoehen geloest und Geometrie final gebaut? */
	bool bHeightsFinal = false;

	/** Naechster Abtastversuch fuer die Hoehen. */
	float HeightRetryRemaining = 0.0f;

	/** Fahrpfad des Wagens (aus Points nach der Hoehenaufloesung). */
	TArray<FVector> CarPathPos;
	TArray<double> CarPathArc;
	bool bCarPathReady = false;

	/** Pendel-Fahrzustand des Wagens auf [0, TotalLength]. */
	WiesbadenRailTransport::FWiesbadenShuttleState Shuttle;

	/** Anhebung des Wagenpivots ueber die Schienenoberkante, cm. */
	float CarLiftCm = 10.0f;

	/** Besitz- und Zustandsdaten der aktuellen Fahrt. */
	WiesbadenRailTransport::FWiesbadenRideSession RideSession;

	/** Flanke der Einstiegstaste. */
	bool bBoardKeyHeld = false;

	/** Gemeinsames Kamera-Rig fuer Follow/Orbit/Cockpit und Mausradzoom. */
	UPROPERTY(Transient)
	UWiesbadenVehicleCameraComponent* PassengerCamera = nullptr;
};
