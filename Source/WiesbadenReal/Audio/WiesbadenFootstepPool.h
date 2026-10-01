// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;
struct FPlacedPedestrian;

/**
 * Fussschritte der Passanten - ohne einen Actor je Passant.
 *
 * Die Passanten sind ISM-Instanzen (FPlacedPedestrian), keine Actors. Ein
 * UAudioComponent je Figur waere dort unmoeglich, und die Figuren werden
 * zudem gepoolt, nicht einzeln erzeugt.
 *
 * Stattdessen dient die vorhandene StridePhase als Takt: sie laeuft 0..1 und
 * springt bei 1 -> 0 (FWiesbadenPedestrianSimulation::ComputeStridePhase
 * rechnet frac(Distanz / Schrittlaenge), der Sprung ist also exakt der
 * Uebergang). Dieser Sprung IST der Schritt. Deshalb braucht die Klasse
 * KEINE Zeit: die Phase ist die Uhr, und ein DeltaSeconds-Parameter waere
 * nur eine Zahl, die niemand benutzt.
 *
 * Eine belebte Innenstadt darf nicht wie Feuerwerk klingen, deshalb ist die
 * Zahl der Schritte je Aufruf hart begrenzt.
 *
 * Kein UObject: der Besitz liegt in einem TUniquePtr-Member des
 * AWiesbadenCityActor, nicht in einem UPROPERTY. Die Klasse merkt sich nur
 * Phasen und zaehlt, sie braucht keine Reflection und keine GC-Verwaltung.
 * Das F-Praefix ist die UE-Konvention fuer solche Klassen; ein U-Praefix
 * waere irrefuehrend, weil es ein UObject verspricht.
 */
class WIESBADENREAL_API FWiesbadenFootstepPool
{
public:
	/** Hoechstens so viele Passantenschritte je Aufruf. */
	static constexpr int32 MaxStepsPerFrame = 6;

	/**
	 * Phasen merken und bei Ueberschreitung melden.
	 *
	 * Das Budget greift VOR dem Abspielen. Ein Lauf ohne Welt (headless Test)
	 * oder ohne geladene Klaenge zaehlt die Uebergaenge trotzdem - sonst waere
	 * die Regel, auf die es ankommt, ohne Audio nicht messbar.
	 *
	 * @param World  nullptr erlaubt: dann wird gezaehlt, aber nicht gespielt.
	 * @param Placed die gezeichneten Passanten dieses Bildes.
	 * @return nichts - gezaehlt wird ueber GetPedestrianStepsLastFrame().
	 */
	void NotePedestrians(UWorld* World, const TArray<FPlacedPedestrian>& Placed);

	/**
	 * Uebergaenge, die der letzte Aufruf ausgeloest hat (nach dem Budget).
	 *
	 * Bewusst NICHT "gehoerte Schritte": ob ein Klang erklang, weiss nur das
	 * Audio-Zonen-Subsystem, und nur wenn es die Klaenge geladen hat.
	 */
	int32 GetPedestrianStepsLastFrame() const { return StepsThisFrame; }

private:
	/** Vorige Phase je Passant-Seed. Der Seed ist stabil, der Index nicht. */
	TMap<int32, float> LastPhaseBySeed;

	int32 StepsThisFrame = 0;
};
