// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UAudioComponent;
class USoundAttenuation;
class USoundSubmix;
class USubmixEffectReverbPreset;
struct FWiesbadenEngineAudioParams;

/**
 * Entfernungskategorie einer Klangquelle - entscheidet ueber das
 * Attenuation-Asset mit Distanzkurve, Entfernungs-Tiefpass und Occlusion.
 */
enum class EWbAudioRange : uint8
{
	Near,	// nahe Einzelklaenge (kleine Quellen)
	Mid,	// Schuss, Kettensaege, Reifen-Quietschen
	Far		// Motor, Heli-Rotor - weit tragen
};

/**
 * Raumklasse aus der Raumsonde - bestimmt Hall-Preset und Sendepegel.
 */
enum class EWbReverbSpace : uint8
{
	Outdoor,	// freier Himmel - kein Hall
	Indoor,		// Zimmer - kurzer, enger Hall
	Hall,		// hohe Raeume (Halle, Kirche) - langer Hall
	Tunnel		// Rohr/Unterfuehrung - mittellang, dicht
};

/**
 * Hall-Abstimmung je Raumklasse. Felder entsprechen
 * FSubmixEffectReverbSettings (Submix-Effekt "Reverb"), damit die
 * Umrechnung eine reine Zuordnung bleibt.
 */
struct WIESBADENREAL_API FWbReverbTuning
{
	bool bBypass = true;
	bool bBypassEarlyReflections = false;
	bool bBypassLateReflections = false;
	float ReflectionsDelay = 0.007f;
	float GainHF = 0.89f;
	float ReflectionsGain = 0.05f;
	float LateDelay = 0.03f;
	float DecayTime = 0.5f;
	float Density = 0.5f;
	float Diffusion = 0.6f;
	float AirAbsorptionGainHF = 0.9f;
	float DecayHFRatio = 0.7f;
	float LateGain = 0.6f;
};

/**
 * Raeumliche Ausbreitung: Distanzkurven, Occlusion, Hall-Sendepegel und
 * Doppler fuer ALLE Klangquellen.
 *
 * Die reine Mathematik steht oben und ist headless testbar; die Engine-
 * Kopplung darunter laedt die Assets unter /Game/Audio/Mix und haelt die
 * registrierten Quellen auf dem aktuellen Raumzustand. Fehlen die Assets,
 * arbeitet alles ohne Fehler weiter (wie beim Mischpult).
 */
namespace WiesbadenAudioPropagation
{
	// -- reine Mathematik (headless testbar) --------------------------------

	/**
	 * Doppler-Faktor fuer die Tonhoehe: positive Geschwindigkeit = Quelle
	 * kommt dem Hoerer naeher -> hoeherer Ton. Faktor c/(c-v), v hart auf
	 * +-0,45*c begrenzt und das Ergebnis auf [0,7; 1,4] - ein Vorbeifahren
	 * darf sich anhoeren wie eines, nicht wie eine Sirene.
	 */
	float DopplerPitchFactor(float SourceSpeedTowardListenerMetersPerS, float SpeedOfSoundMetersPerS = 343.0f);

	/**
	 * Raumklasse aus drei Sondenwerten:
	 *  SkyBlocked01     Anteil der Aufwaertsstrahlen, die Geometrie treffen;
	 *  WallClosure01    Anteil der Horizontalstrahlen, die Hindernisse treffen;
	 *  CeilingHeightM   gemessene Deckenhoehe (Meter; gross = offen).
	 */
	EWbReverbSpace ClassifySpace(float SkyBlocked01, float WallClosure01, float CeilingHeightMeters);

	/** Hall-Abstimmung je Raumklasse (Outdoor = komplett by-passed). */
	FWbReverbTuning ReverbTuningForSpace(EWbReverbSpace Space);

	/** Sendepegel der Quelle in den Hall-Submix je Raumklasse (0..1). */
	float ReverbSendForSpace(EWbReverbSpace Space);

	/**
	 * Tag-Anteil des Ambience-Bettes (Voegel, heller Wind): 1 zwischen 7 und
	 * 19 Uhr, 0 zwischen 21 und 5 Uhr, dazwischen weiche Ueberblaende.
	 */
	float DayBedGain(float TimeOfDayHours);

	/** Nacht-Anteil des Ambience-Bettes: exakt 1 - DayBedGain. */
	float NightBedGain(float TimeOfDayHours);

	/**
	 * Motorparameter als (MetaSound-Name, Wert)-Paare. Das ist die
	 * Parameterschnittstelle, die die MetaSound-Layer der Fahrzeuge
	 * speist - identisch zu FWiesbadenEngineAudioParams, damit die
	 * C++-Synthese als Referenz vergleichbar bleibt.
	 */
	TArray<TPair<FName, float>> EngineParamPairs(const FWiesbadenEngineAudioParams& Params);

	// -- Asset-Namen: eine Quelle der Wahrheit ------------------------------

	FString AttenuationPath(EWbAudioRange Range);	// /Game/Audio/Mix/ATT_*
	FString ReverbSubmixPath();						// /Game/Audio/Mix/SBX_Reverb
	FString ReverbPresetPath();						// /Game/Audio/Mix/SFXP_Reverb
	FString AmbienceBedPath(FName BedName);			// /Game/Audio/Meta/MS_Amb<Bett>
	FString EngineMetaSoundPath();					// /Game/Audio/Meta/MS_EngineBoxer

	// -- Engine-Kopplung ----------------------------------------------------

	USoundAttenuation* LoadAttenuation(EWbAudioRange Range);
	USoundSubmix* LoadReverbSubmix();
	USubmixEffectReverbPreset* LoadReverbPreset();

	/**
	 * Weist einer frisch erzeugten Klangquelle ihre Distanzkurve zu
	 * (Attenuation-Asset mit Occlusion und Entfernungs-Tiefpass), haelt sie
	 * fuer die Raumzustands-Umschaltung registriert und legt sofort den
	 * aktuellen Hall-Sendepegel an.
	 */
	void ConfigureSource(UAudioComponent* Source, EWbAudioRange Range, bool bSpatialized = true);

	/** Quelle abmelden (bei Zerstoerung der Komponente). */
	void UnregisterSource(UAudioComponent* Source);

	/**
	 * Raumzustand anwenden: Hall-Preset umschalten und den Sendepegel aller
	 * registrierten Quellen nachziehen. Wird vom Ambience-Subsystem bei
	 * Raumwechsel aufgerufen (gedaempft), aendert sonst nichts.
	 */
	void ApplySpaceState(EWbReverbSpace Space);

	/** Zuletzt angewandter Raumzustand (Start: Outdoor). */
	EWbReverbSpace GetCurrentSpace();

	/**
	 * Doppler-Faktor aus der Relativbewegung zweier Actors: positiv, wenn
	 * sich die Quelle dem Hoerer naehert. Liefert 1, solange eines der
	 * Geschwindigkeitsdaten fehlt.
	 */
	float ComputeDopplerForActors(const AActor* SourceActor, const AActor* ListenerActor);
}
