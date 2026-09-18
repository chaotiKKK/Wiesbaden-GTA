// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Zaehler fuer die Arbeit, die das Nachladen der Welt im SPIEL-STRANG kostet.
 *
 * Der Bildzeit-Melder sagt "Aussetzer 90 ms" und nennt keine Ursache. Die
 * Simulationen (Ampeln/Verkehr/Fussgaenger) sind zusammen ~1 ms und scheiden
 * damit aus - was bleibt, ist die Last beim Hereinstreamen einer Zelle. Diese
 * Zaehler ordnen genau sie zu: wie viele Zellen in diesem Bild ihre
 * Regionsobjekte aufgebaut haben, wie viele Instanzen dabei entstanden und wie
 * lange das gedauert hat.
 *
 * Nur Spiel-Strang, kein Schutz noetig: alle Schreiber (Chunk-BeginPlay,
 * SpawnRegionAssets) und der Leser (Subsystem-Tick) laufen dort.
 *
 * Der Aufrufer setzt die Zaehler je Bild zurueck (Reset), nachdem er sie
 * gelesen hat.
 */
struct WIESBADENREAL_API FWbStreamingCost
{
	/** Zeit in SpawnRegionAssets seit dem letzten Reset (ms). */
	static double SpawnMs;

	/** Dabei angelegte Instanzen (Baeume/Buesche/Ufer/Industrie). */
	static int32 Instances;

	/** Zellen, die dabei ihre Regionsobjekte aufgebaut haben. */
	static int32 Cells;

	/** Zeit im BeginPlay der Chunk-Actors (enthaelt SpawnMs und AnchorMs). */
	static double BeginPlayMs;

	/** Zeit in AnchorStreamingBounds (Bounds-Ankerung je Zelle). */
	static double AnchorMs;

	static void Reset()
	{
		SpawnMs = 0.0;
		Instances = 0;
		Cells = 0;
		BeginPlayMs = 0.0;
		AnchorMs = 0.0;
	}
};

/** Misst einen Block und schlaegt ihn auf den uebergebenen Zaehler auf. */
struct FWbStreamingCostScope
{
	explicit FWbStreamingCostScope(double& InTarget)
		: Target(InTarget), StartCycles(FPlatformTime::Cycles64()) {}

	~FWbStreamingCostScope()
	{
		Target += FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles);
	}

private:
	double& Target;
	uint64 StartCycles = 0;
};
