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
 *    mit der Drehzahl und der Blattlast (Collective) steigt; dazu die koaxiale
 *    "Wop-Wop"-Amplitudenmodulation (zwei verschraenkte Blattpaesse des Ka-52-
 *    Gegenlaufrotors) mit langsamer Schwebung.
 *  - Triebwerk: heller Wellenturbinen-Whine (Spool-Grundton + Obertonkamm +
 *    Kompressor-Buzz + Luftrauschen), steigt mit der Triebwerksdrehzahl; nur bei
 *    laufendem Triebwerk. Modelliert die 2x Klimov VK-2500 - kein Kolbenmotor.
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
	 * @param StartTimeSeconds Fortlaufende Zeitbasis fuer die Sinus-Phasen. Der
	 *        Aufrufer zaehlt die abgespielte Zeit ueber alle Puffer hoch, damit
	 *        die Turbinen-/Rotortoene NICHT bei jedem Puffer (2048 Samples) auf
	 *        Phase 0 zuruckspringen - sonst buzzt der Turbinen-Whine mit der
	 *        Pufferrate (~21 Hz). Default 0 fuer die deterministischen Tests.
	 */
	static void GenerateSamples(const FWiesbadenHelicopterAudioParams& Params,
		int32 SampleRate, int32 NumSamples, uint32 Seed, int16* OutSamples,
		double StartTimeSeconds = 0.0);
};
