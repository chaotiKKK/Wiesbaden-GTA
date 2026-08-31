// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Parameter des Flugsound-Modells (ein Tick).
 *
 * Werte kommen aus der Rotor-Physik / dem Heli-Pawn:
 *  - MainRotorRpm     : Hauptrotor-Drehzahl (U/min) - bestimmt Wop-Wop- und
 *                       Filterfrequenz (Blattpass + helle Anteile).
 *  - EngineRpm        : Triebwerksdrehzahl (U/min) - Grundfrequenz des Motor-Tons.
 *  - Collective       : 0..1 - Lautstaerke des Rotor-Geraeuschs (Blattlast).
 *  - ForwardSpeedMetersPerS: Vorwaertsgeschwindigkeit (Reserve, noch ungenutzt).
 *  - bEngineRunning   : false bei Autorotation - dann nur Rotor-Geraeusch.
 *  - BladeCount       : Blattzahl je Hauptrotor (Koaxial: je Rotor).
 *  - BladeSlapDepth   : AM-Tiefe des Rotorschlags (0 = glatt, 1 = harter
 *                       "Blade Slap" wie bei Kampfhelikoptern).
 *  - RotorCutoffBaseHz: Basis-Grenzfrequenz des Rotor-Tiefpasses - niedriger
 *                       bei schweren Kampfhelis (dumpfer Schlag).
 */
struct FWiesbadenHelicopterAudioParams
{
	float MainRotorRpm = 420.0f;
	float EngineRpm = 3000.0f;
	float Collective = 0.5f;
	float ForwardSpeedMetersPerS = 0.0f;
	bool bEngineRunning = true;
	int32 BladeCount = 4;
	float BladeSlapDepth = 0.38f;
	float RotorCutoffBaseHz = 180.0f;
};

/**
 * Deterministisches Modell fuer den prozeduralen Flugsound.
 *
 * Erzeugt Mono-int16-PCM aus den Rotor-/Motor-Parameter. Klaenge:
 *  - Rotor: weisses Rauschen durch einen One-Pole-Tiefpass, dessen Grenzfrequenz
 *    mit der Drehzahl und der Blattlast (Collective) steigt; dazu die typische
 *    "Wop-Wop"-Amplitudenmodulation mit der Blattpassfrequenz.
 *  - Motor: Sinus-Ton auf der Grundfrequenz (Drehzahl * Zylinderzahl) plus
 *    Oberwelle und gedaempftem Rauschen; nur bei laufendem Triebwerk.
 *
 * Deterministisch: gleiche Parameter + gleicher Seed erzeugen identische
 * Samples (xorshift32-PRNG), dadurch in Automation-/node-Tests pruefbar.
 */
struct FWiesbadenHelicopterAudioModel
{
	/** Blattpassfrequenz (Hz): Blattzahl * Umdrehungen pro Sekunde. */
	static float GetBladePassFrequency(float MainRotorRpm, int32 BladeCount);

	/** Grenzfrequenz des Rotor-Tiefpasses (Hz): steigt mit Drehzahl und Last. */
	static float GetFilterCutoffHz(float MainRotorRpm, float Collective);

	/** Grenzfrequenz mit konfigurierbarer Basis (Kampfheli: niedrige Basis = dumpf). */
	static float GetRotorCutoffHz(const FWiesbadenHelicopterAudioParams& Params);

	/**
	 * Generiert NumSamples Mono-Samples (SampleRate Hz) als int16-PCM.
	 * @param Seed PRNG-Seed - fester Wert fuer Tests, Laufzeit-Seed fuer Gameplay.
	 */
	static void GenerateSamples(const FWiesbadenHelicopterAudioParams& Params,
		int32 SampleRate, int32 NumSamples, uint32 Seed, int16* OutSamples);
};
