// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WiesbadenAudioZones.generated.h"

/**
 * Untergrund eines Fussschritts - bestimmt, welcher Klang und welcher
 * Filterbereich benutzt wird.
 *
 * Das Enum ist fuer die UFUNCTION-Schnittstelle reflektiert; die Aufloesung
 * bleibt reine Mathematik ueber Materialnamen und headless testbar. Erst die
 * Engine-Kopplung (UWiesbadenAudioZonesSubsystem) liest UMaterialInterface.
 */
UENUM(BlueprintType)
enum class EWbFootstepSurface : uint8
{
	/** Fahrbahn, Gehweg: hell und kurz. */
	Asphalt,

	/** Bordstein, Pflaster, Plaetze: mittel, mit Ausschlag. */
	Pflaster,

	/** Wiese, Waldboden, Beete: dumpf und weich. */
	Wiese,

	/** Innenraum (Fussboden im Turm, Laden): trocken und sehr kurz. */
	Innenraum,

	MAX UMETA(Hidden)
};

/**
 * Reine Zuordnung Ort -> Klang. Kein UObject-Zugriff, damit die Zuordnung
 * headless unit-testbar bleibt.
 */
namespace WiesbadenAudioZones
{
	/**
	 * Untergrund aus dem Materialnamen.
	 *
	 * Dasselbe Muster wie AWiesbadenCityChunk::BuildingUseFromMaterialName: die
	 * Nutzung steckt im Materialnamen der Section, und der Name ist auf der
	 * gebackenen Karte serialisiert - deshalb funktioniert die Aufloesung auch
	 * ohne FWiesbadenCityData (die es dort nicht gibt).
	 *
	 * Unbekannt oder leer -> Pflaster. Der gangbare Default, kein Fehler.
	 */
	EWbFootstepSurface SurfaceFromMaterialName(const FString& MaterialName);

	/** Bandpass-Mitte in Hz je Oberflaeche (der synthetische Schritt, Task 3). */
	float BandpassHzForSurface(EWbFootstepSurface Surface);

	/** Deutscher Name fuer Log-Zeilen und Tests. */
	FString SurfaceName(EWbFootstepSurface Surface);

	/** Index des geladenen Sounds fuer die Oberflaeche, sonst INDEX_NONE. */
	int32 FindStepSoundIndex(
		const TArray<EWbFootstepSurface>& LoadedSurfaces,
		EWbFootstepSurface Surface);
}
