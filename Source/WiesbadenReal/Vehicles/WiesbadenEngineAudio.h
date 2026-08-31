// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Betriebszustand des Motors fuer die Klangsynthese (ein Push-Block).
 *
 * Der Kaefer hat einen luftgekuehlten Vierzylinder-Boxer. Sein Klang lebt von
 * drei Dingen, die hier nachgebildet werden:
 *  - der Zuendfrequenz (tiefes Knattern, steigt mit der Drehzahl),
 *  - der Ungleichfoermigkeit gegenueberliegender Zylinder (der typische
 *    "Boxer-Versatz"; ein perfekt gleichmaessiger Puls klingt nach Turbine),
 *  - dem Ansauggeraeusch, das mit der Last (Gaspedal) zunimmt.
 */
struct WIESBADENREAL_API FWiesbadenEngineAudioParams
{
	/** Kurbelwellendrehzahl in U/min. */
	float EngineRpm = 900.0f;

	/** Gaspedalstellung 0..1 - bestimmt Last und damit Klangfarbe/Lautstaerke. */
	float Throttle = 0.0f;

	/** Fahrgeschwindigkeit in km/h - speist ein leises Rollgeraeusch. */
	float SpeedKmh = 0.0f;

	/** Zylinderzahl. Vier beim Kaefer. */
	int32 CylinderCount = 4;

	/** Leerlaufdrehzahl - darunter wird nicht synthetisiert. */
	float IdleRpm = 850.0f;

	/** Gesamtlautstaerke 0..1. */
	float MasterGain = 0.7f;

	/**
	 * Hupe gedrueckt?
	 *
	 * Die Hupe fehlte dem Projekt vollstaendig - es gab keinen Ton und keine
	 * Taste. Sie sitzt hier im Motorsynthesizer statt in einer eigenen
	 * Klangquelle, weil beide dieselbe prozedurale Welle fuellen: eine zweite
	 * Quelle am selben Fahrzeug braeuchte eigenen Puffer, eigene Mischung und
	 * eigene Entfernungsdaempfung.
	 */
	bool bHorn = false;

	/** False = Motor aus, es wird Stille erzeugt. */
	bool bEngineRunning = true;
};

/**
 * Fortlaufender Zustand zwischen zwei Sample-Bloecken.
 *
 * Die Phase MUSS ueber Blockgrenzen hinweg mitgefuehrt werden. Wuerde jeder
 * Block bei Phase 0 beginnen, entstuende an jeder Blockgrenze ein Sprung im
 * Signal - hoerbar als Knacken im Takt der Puffergroesse. Aus demselben Grund
 * wird die Phase inkrementell fortgeschrieben und nicht aus einem
 * Sample-Index berechnet: bei wechselnder Drehzahl waere Letzteres unstetig.
 */
struct WIESBADENREAL_API FWiesbadenEngineAudioState
{
	/** Phase des Zuendzyklus, 0..1. */
	double FiringPhase = 0.0;

	/** Phase der Ansaugmodulation, 0..1. */
	double IntakePhase = 0.0;

	/** Tiefpass-Zustand des Rauschanteils. */
	float NoiseLowpass = 0.0f;

	/** Zustand des deterministischen Rauschgenerators. */
	uint32 NoiseSeed = 0x9E3779B9u;

	/** Geglaettete Drehzahl - verhindert Tonsprunge bei ruckartigem Gas. */
	float SmoothedRpm = 0.0f;

	/** Phasen der beiden Hupentoene (fortlaufend, damit es nicht knackt). */
	float HornPhaseA = 0.0f;
	float HornPhaseB = 0.0f;

	/** Geglaettete Hupenlautstaerke - hartes Ein/Aus knackt hoerbar. */
	float HornGain = 0.0f;

	void Reset()
	{
		FiringPhase = 0.0;
		IntakePhase = 0.0;
		NoiseLowpass = 0.0f;
		NoiseSeed = 0x9E3779B9u;
		SmoothedRpm = 0.0f;
	}
};

/**
 * Synthese des Motorklangs.
 *
 * Vollstaendig datenrein: gleiche Eingaben und gleicher Zustand ergeben
 * exakt dieselben Samples. Damit ist der Klang im Automation-Test pruefbar
 * (Pegelgrenzen, Frequenz, Stille bei abgestelltem Motor), ohne dass ein
 * Audiogeraet oder eine laufende Engine noetig waere.
 */
struct WIESBADENREAL_API FWiesbadenEngineAudioModel
{
	/**
	 * Zuendfrequenz in Hz.
	 *
	 * Beim Viertakter zuendet jeder Zylinder einmal je zwei Kurbelwellen-
	 * umdrehungen. Bei N Zylindern also: f = U/min / 60 * N / 2.
	 * Vierzylinder bei 3000 U/min -> 100 Hz.
	 */
	static float GetFiringFrequencyHz(float EngineRpm, int32 CylinderCount);

	/** Ausgangspegel 0..1 aus Drehzahl und Last. */
	static float GetOutputGain(const FWiesbadenEngineAudioParams& Params);

	/**
	 * Erzeugt NumSamples Mono-Samples (int16) und schreibt den Zustand fort.
	 *
	 * @param SampleRate  Abtastrate in Hz, > 0.
	 * @param OutSamples  Ziel, muss NumSamples Eintraege fassen.
	 * @return false bei ungueltigen Parametern; OutSamples wird dann mit
	 *         Stille gefuellt statt undefinierte Werte zu hinterlassen.
	 */
	static bool GenerateSamples(
		const FWiesbadenEngineAudioParams& Params,
		int32 SampleRate,
		int32 NumSamples,
		FWiesbadenEngineAudioState& State,
		int16* OutSamples);
};
