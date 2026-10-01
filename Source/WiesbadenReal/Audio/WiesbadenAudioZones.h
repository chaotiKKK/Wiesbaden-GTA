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

UENUM(BlueprintType)
enum class EWbAudioZone : uint8
{
	Residential,
	Commercial,
	Quiet,
	Industrial,
	MAX UMETA(Hidden)
};

/**
 * Pegel der Klanglagen; jede Lage ist eine ECHTE Aufnahme aus
 * /Game/Audio/Samples (A_Amb*, Field-Recordings von BigSoundBank, Import:
 * Tools/fetch_ambience_samples.py + Tools/import_audio_samples.py), keine
 * synthetische Noise-Lage mehr. Der Mix bleibt reine Mathematik, damit die
 * Zonen-Zuordnung headless testbar ist.
 */
struct FWbAmbienceMix
{
	/** A_AmbWind - Windbett, ueberall hoerbar. */
	float Wind = 0.5f;

	/** A_AmbTraffic - Verkehrssummen (alte "City"-Lage, jetzt echte Strasse). */
	float City = 0.45f;

	/** A_AmbBirds - Vogelchor (Tag/Gruen). */
	float Birds = 0.3f;

	/** A_AmbNight - Nachtambiente (Nachtstunden). */
	float Night = 0.3f;

	/** A_AmbCrowd - Menschenmurmeln (Innenstadt). */
	float Crowd = 0.0f;

	/** A_AmbChildren - Strassenleben mit Kindern (Wohngebiet). */
	float Children = 0.0f;

	/** A_AmbIndustry - Maschinen/Handwerk (Industrie). */
	float Industry = 0.0f;
};

/**
 * Reine Zuordnung Ort -> Klang. Kein UObject-Zugriff, damit die Zuordnung
 * headless unit-testbar bleibt.
 */
namespace WiesbadenAudioZones
{
	EWbAudioZone ClassifyZone(int32 NearbyTrees, int32 NearbyIndustry, int32 CommercialBuildings);
	FWbAmbienceMix AmbienceMix(EWbAudioZone Zone);
	FString ZoneName(EWbAudioZone Zone);
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
