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

	/** OBERER (Luft-)Radius der Streaming-Quelle in Metern - gilt in Reiseflughoehe. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "100.0"))
	//
	// 2000 m reichten nur fuer die Fahrt. Aus dem Helikopter sieht man weiter,
	// und alles dahinter war schlicht nicht geladen: Baeume, Schilder und
	// Laternen haengen am immer geladenen WorldBuilder und standen weiter da,
	// Strassen und Gebaeude aber liegen in gestreamten Chunks und fehlten.
	// Das ergab genau den Eindruck zerrissener Strassen - ohne dass an der
	// Geometrie etwas fehlte (aus 400 m Hoehe ist das Netz vollstaendig).
	// ABER: 6000 m am BODEN laden zu viele Zellen -> Streaming-Hitches (~6 FPS
	// statt ~76 bei 800 m, gemessen ccc01d7). Deshalb ist der Radius jetzt
	// HOEHENADAPTIV: am Boden GroundRadiusMeters (schnell), in der Luft dieser
	// Wert (vollstaendige Sicht). Zwischen den Hoehenbaendern linear geblendet.
	float StreamingRadiusMeters = 6000.0f;

	/** UNTERER (Boden-)Radius in Metern - gilt bei Fahrt/Fuss, haelt die FPS hoch. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "100.0"))
	float GroundRadiusMeters = 900.0f;

	/** Ab dieser Hoehe ueber Grund (m) beginnt der Radius aufzuweiten. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "0.0"))
	float AdaptiveStartAltitudeMeters = 60.0f;

	/** Ab dieser Hoehe ueber Grund (m) gilt voll der Luft-Radius. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "1.0"))
	float AdaptiveFullAltitudeMeters = 350.0f;

	/** Dem lokalen Player-Pawn folgen (sonst eigene Actor-Position nutzen). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming")
	bool bFollowPlayerPawn = true;

	// IWorldPartitionStreamingSourceProvider
	virtual bool GetStreamingSource(FWorldPartitionStreamingSource& OutStreamingSource) const override;

	/**
	 * Reine Radius-Kurve (m) aus der Hoehe ueber Grund - ohne Weltzugriff,
	 * damit testbar. Unter Start -> GroundRadius, ueber Full -> AirRadius,
	 * dazwischen smoothstep-geblendet. Degeneriert (Full<=Start) -> harte Stufe.
	 */
	static float ComputeAdaptiveRadiusMeters(float AltitudeMeters, float GroundRadiusM,
		float AirRadiusM, float StartAltM, float FullAltM);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Aktualisiert Position/Rotation/Radius der Quelle aus dem Player-Pawn. */
	void UpdateSource(float DeltaSeconds);

	/** Zuletzt berechnete Quelle (wird an den Provider zurueckgegeben). */
	FWorldPartitionStreamingSource CurrentSource;

	/** True, sobald eine gueltige Quelle (mit Pawn) vorliegt. */
	bool bHasValidSource = false;

	/** -WbRadius erzwingt einen FESTEN Radius (Diagnose) -> Adaptivitaet aus. */
	bool bForceFixedRadius = false;

	/** Geglaettete Hoehe ueber Grund (m), gegen Radius-Springen beim Ueberfliegen. */
	float SmoothedAltitudeMeters = 0.0f;

	/** Sekundentakt-Drossel fuer das Diagnose-Log (Hoehe/Radius). */
	int32 LastRadiusLogSecond = -1;

	UPROPERTY(Transient)
	UWorldPartitionSubsystem* WorldPartitionSubsystem = nullptr;
};
