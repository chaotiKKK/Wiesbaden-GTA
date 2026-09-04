// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "WiesbadenNerotalbahn.generated.h"

class UProceduralMeshComponent;
class USceneComponent;

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
 * gepflasterte Trasse mit meterspurigen Rillenschienen, kein Viadukt, keine
 * Wagen (folgt bei Bedarf). Der Zweck ist die STRECKE, sauber auf dem
 * Gelaende.
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

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Nerotalbahn")
	USceneComponent* Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Nerotalbahn")
	UProceduralMeshComponent* TrackMesh = nullptr;

	TArray<FTrackPoint> Points;
	double TotalLength = 0.0;

	/** Alle Hoehen geloest und Geometrie final gebaut? */
	bool bHeightsFinal = false;

	/** Naechster Abtastversuch fuer die Hoehen. */
	float HeightRetryRemaining = 0.0f;
};
