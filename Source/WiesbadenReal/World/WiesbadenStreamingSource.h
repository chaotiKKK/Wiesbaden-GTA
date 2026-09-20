// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"

#include "WiesbadenStreamingSource.generated.h"

class UWorldPartitionSubsystem;
class UMaterialParameterCollection;

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

	/**
	 * Geschwindigkeits-Vorausladung: Sekunden Fahrweg, die VOR dem Fahrzeug
	 * zusaetzlich geladen werden.
	 *
	 * Reine Velocity-SORTIERUNG (bUseVelocityContributionToCellsSorting) laedt die
	 * Zellen im Fahrweg nur ZUERST - sie vergroessert die Reichweite nicht. Bei
	 * Tempo (250 km/h = 69 m/s) reicht der enge Boden-Radius (900 m ~ 13 s Vorlauf)
	 * nicht: der Durchfall-Test fuhr 34,5 % der Strecke ueber noch NICHT geladene
	 * Kollision. Die Quelle wird darum um SpeedLookAheadSeconds * Tempo nach vorn
	 * verschoben (halb als Zentrums-Versatz, halb als Radius-Zuwachs): der Vorwaerts-
	 * Puffer waechst mit dem Tempo, HINTER dem Wagen bleibt der Boden-Radius stehen,
	 * und im Stand (Tempo 0) ist alles wie zuvor - die FPS am Zellrand bleiben hoch.
	 * 0 schaltet die Vorausladung ab. -WbLookAhead=<s> ueberschreibt (Diagnose).
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "0.0"))
	float SpeedLookAheadSeconds = 6.0f;

	/** Obergrenze der Vorausladung (m) - deckelt Zellenzahl/Versatz bei Extremtempo. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "0.0"))
	float MaxLookAheadMeters = 1500.0f;

	/** Dem lokalen Player-Pawn folgen (sonst eigene Actor-Position nutzen). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming")
	bool bFollowPlayerPawn = true;

	// IWorldPartitionStreamingSourceProvider
	virtual bool GetStreamingSource(FWorldPartitionStreamingSource& OutStreamingSource) const override;

	/**
	 * Heftet die Quelle FEST an einen Ort (Pawn-unabhaengig) und aktualisiert sie
	 * SOFORT. Fuer den Block-Load der Spawn-Zelle im GameMode VOR dem Fahrzeug-
	 * Einsatz: dort existiert noch kein Pawn, dem die Quelle sonst folgt, also
	 * bliebe sie inaktiv und World Partition wuesste nichts vom Startort.
	 * bFollowPlayerPawn wird abgeschaltet - nach dem Einsatz mit true zuruecksetzen.
	 */
	void PinSourceToLocation(const FVector& WorldLocation);

	/**
	 * Reine Radius-Kurve (m) aus der Hoehe ueber Grund - ohne Weltzugriff,
	 * damit testbar. Unter Start -> GroundRadius, ueber Full -> AirRadius,
	 * dazwischen smoothstep-geblendet. Degeneriert (Full<=Start) -> harte Stufe.
	 */
	static float ComputeAdaptiveRadiusMeters(float AltitudeMeters, float GroundRadiusM,
		float AirRadiusM, float StartAltM, float FullAltM);

	/**
	 * Vorausladeweite (m) aus horizontalem Tempo - ohne Weltzugriff, damit testbar.
	 * = clamp(SpeedMetersPerS * LookAheadSeconds, 0, MaxMeters). Tempo 0 -> 0.
	 */
	static float ComputeLookAheadMeters(float SpeedMetersPerS, float LookAheadSeconds,
		float MaxMeters);

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

	/** Vorige Quell-Position + geglaettete Geschwindigkeit (cm/s) fuer die
	 *  Velocity-Vorausladung (auch beim Teleport-Autopilot, wo GetVelocity 0 ist). */
	FVector PrevSourceLocation = FVector::ZeroVector;
	bool bHasPrevSourceLocation = false;
	FVector SmoothedVelocity = FVector::ZeroVector;

	/** Sekundentakt-Drossel fuer das Diagnose-Log (Hoehe/Radius). */
	int32 LastRadiusLogSecond = -1;

	/**
	 * Strassen-Einblenden: MPC, in die je Bild der gelebte Streaming-Radius
	 * geschrieben wird (build_streaming_fade.py legt sie an). Wird bis zum
	 * Erfolg nachgeladen - ein einmaliger Ladefehler darf das Material nicht
	 * dauerhaft auf dem Default-Radius (900 m) haengen lassen, sonst
	 * verschwaenden ferne Strassen im Flug.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> FadeMpc = nullptr;

	UPROPERTY(Transient)
	UWorldPartitionSubsystem* WorldPartitionSubsystem = nullptr;
};
