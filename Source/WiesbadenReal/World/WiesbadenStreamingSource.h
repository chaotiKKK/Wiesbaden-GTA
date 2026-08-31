// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"

#include "WiesbadenStreamingSource.generated.h"

class UWorldPartitionSubsystem;

/**
 * World-Partition-Streaming-Quelle, die dem lokalen Player-Pawn folgt.
 *
 * Registriert sich beim UWorldPartitionSubsystem als
 * IWorldPartitionStreamingSourceProvider und liefert jede Tick eine aktive
 * Streaming-Quelle an der Position des Players. Dadurch werden die Zellen der
 * partitionierten Welt rund um den Spieler geladen/entladen - die Stadt
 * streamt mit dem Spieler mit (Produktionspfad mit gebackener
 * World-Partition-Map).
 *
 * Ist noch kein Pawn vorhanden (z. B. waehrend des Ladens), liefert der
 * Provider keine Quelle (TargetState bleibt inaktiv), bis der Player da ist.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenStreamingSource : public AActor, public IWorldPartitionStreamingSourceProvider
{
	GENERATED_BODY()

public:
	AWiesbadenStreamingSource();

	/** Radius der Streaming-Quelle in Metern. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "100.0"))
	//
	// 2000 m reichten nur fuer die Fahrt. Aus dem Helikopter sieht man weiter,
	// und alles dahinter war schlicht nicht geladen: Baeume, Schilder und
	// Laternen haengen am immer geladenen WorldBuilder und standen weiter da,
	// Strassen und Gebaeude aber liegen in gestreamten Chunks und fehlten.
	// Das ergab genau den Eindruck zerrissener Strassen - ohne dass an der
	// Geometrie etwas fehlte (aus 400 m Hoehe ist das Netz vollstaendig).
	float StreamingRadiusMeters = 6000.0f;

	/** Dem lokalen Player-Pawn folgen (sonst eigene Actor-Position nutzen). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming")
	bool bFollowPlayerPawn = true;

	// IWorldPartitionStreamingSourceProvider
	virtual bool GetStreamingSource(FWorldPartitionStreamingSource& OutStreamingSource) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Aktualisiert Position/Rotation der Quelle aus dem Player-Pawn. */
	void UpdateSource();

	/** Zuletzt berechnete Quelle (wird an den Provider zurueckgegeben). */
	FWorldPartitionStreamingSource CurrentSource;

	/** True, sobald eine gueltige Quelle (mit Pawn) vorliegt. */
	bool bHasValidSource = false;

	UPROPERTY(Transient)
	UWorldPartitionSubsystem* WorldPartitionSubsystem = nullptr;
};
