// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "WiesbadenZufahrtProbe.generated.h"

/**
 * MESSWERKZEUG: welche Fahrbahn bedient das SebboTower-Grundstueck?
 *
 * Die Ankunftssonde meldete vor dem Portal einen Belag 7,6 m unter der
 * Fahrbahn, aus der das Bauplateau seine Hoehe nahm. Daraus wurde die These
 * "der Pad haengt an der falschen Strasse" - UNGEPRUEFT, denn der Taster der
 * Sonde sitzt bei 2500 cm vom Mittelpunkt und der Pad-Radius ist 2504 cm: er
 * steht auf der Boeschungskante und misst moeglicherweise schon den Hang
 * statt einer Zufahrt.
 *
 * Diese Sonde entscheidet das. Sie liest das in die Karte GEBACKENE
 * Strassennetz vom AWiesbadenWorldBuilder (RoadNetwork ist eine
 * nicht-transiente UPROPERTY) und beantwortet drei Fragen:
 *
 *   1. Welche Segmente liegen im Umkreis, wie heissen sie, wie hoch liegen
 *      sie, und welche haelt der Pad-Filter ueberhaupt fuer zulaessig?
 *   2. Welches Segment waehlt die Regel des Pad-Passes (naechstes in 2D)?
 *   3. Welches Segment waehlt ResolveRoadAccess, also die Road-Pipeline?
 *      Stimmen beide nicht ueberein, ist die Doppel-Suche der Defekt.
 *
 * Dazu ein Ring von Tastern, der NICHT nur die Hoehe, sondern die getroffene
 * KOMPONENTE meldet - ein WiesbadenCityChunk traegt Fahrbahn und Gebaeude in
 * getrennten Komponenten (RoadMesh/BuildingMesh/...), erst der Name sagt, ob
 * dort Asphalt liegt oder eine Hauswand.
 *
 * NUR LESEN. Diese Sonde veraendert weder Gelaende noch Turm; sie schreibt
 * ausschliesslich Saved/Diagnose/zufahrtsprobe.json.
 *
 * Startschalter: -WbZufahrtProbe
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenZufahrtProbe : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	/** Laeuft einmal, nachdem die Zellen um das Grundstueck gestreamt sind. */
	void Messen();

	FTimerHandle Verzoegerung;
};
