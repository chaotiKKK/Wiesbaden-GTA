// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Reine Lautstaerke-Mathematik fuer den Audio-Mixer. KEINE Reflection, damit sie
 * headless unit-testbar bleibt (die eigentliche Mixer-Anwendung braucht ein
 * Audio-Device und ist nicht unit-testbar).
 *
 * Kernregel der Tonmischung: Lautheit ist logarithmisch -> Regler in dB fuehren,
 * nicht linear. Ein 0..1-Regler wird als Mischpult-Fader interpretiert: linear in
 * dB von MinDb (unten) bis 0 dB (oben), 0 = Stille. So verhaelt sich der Regler
 * wie ein echter Fader und nicht "tut bis ganz unten fast nichts".
 */
namespace WiesbadenAudioMix
{
	/** Unterer Fader-Anschlag in dB; darunter gilt der Kanal als still. */
	inline constexpr float DefaultMinDb = -60.0f;

	/** dB -> linearer Amplitudenfaktor (0 dB = 1,0). */
	inline float DbToLinear(float Db)
	{
		return FMath::Pow(10.0f, Db / 20.0f);
	}

	/** Linearer Amplitudenfaktor -> dB (1,0 = 0 dB). <=0 -> sehr leise Schranke. */
	inline float LinearToDb(float Linear)
	{
		return (Linear > SMALL_NUMBER) ? 20.0f * FMath::LogX(10.0f, Linear) : -160.0f;
	}

	/** 0..1-Regler -> dB (Fader: linear in dB von MinDb bis 0). */
	inline float SliderToDb(float Slider01, float MinDb = DefaultMinDb)
	{
		return FMath::Lerp(MinDb, 0.0f, FMath::Clamp(Slider01, 0.0f, 1.0f));
	}

	/**
	 * 0..1-Regler -> linearer Faktor fuer SoundClass-Lautstaerke.
	 * Slider 0 -> exakt still (0), Slider 1 -> 0 dB (1,0), dazwischen perzeptiv.
	 */
	inline float SliderToLinear(float Slider01, float MinDb = DefaultMinDb)
	{
		if (Slider01 <= 0.0f)
		{
			return 0.0f;
		}
		return DbToLinear(SliderToDb(Slider01, MinDb));
	}
}
