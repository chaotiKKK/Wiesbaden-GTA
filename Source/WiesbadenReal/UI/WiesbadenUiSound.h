// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Kurze UI-/Oekonomie-Signaltoene (prozedural erzeugt, kein Asset noetig). */
enum class EWiesbadenUiSound : uint8
{
	KaufChime,     // aufsteigender Zwei-Ton-Chime: Kauf erfolgreich
	AblehnungBuzz, // tiefer, kurzer Buzz: abgelehnt / gesperrt
};

/**
 * Deterministisches Modell fuer kurze UI-Signaltoene als Mono-int16-PCM.
 *
 * Rein/statisch wie FWiesbadenHelicopterAudioModel: gleiche Parameter + gleicher
 * Seed erzeugen identische Samples (xorshift32-PRNG), daher ohne Welt/Audio-
 * geraet in Automation-Tests pruefbar. Kein Asset, keine Ueberkonfiguration -
 * nur zwei Toene fuer Erfolg/Ablehnung.
 */
struct FWiesbadenUiSoundModel
{
	/**
	 * Fuellt NumSamples Mono-Samples (SampleRate Hz) als int16-PCM fuer den Ton.
	 * Beide Toene tragen eine Ausklang-Huellkurve, damit das Ende auf ~0 laeuft
	 * (kein Klick). Erwartet OutSamples != null, NumSamples > 0, SampleRate > 0.
	 * @return true bei gueltigen Argumenten (Samples geschrieben), sonst false.
	 */
	static bool GenerateSamples(EWiesbadenUiSound Kind, int32 SampleRate,
		int32 NumSamples, uint32 Seed, int16* OutSamples);
};
