// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "UI/WiesbadenUiSound.h"

namespace
{
	// xorshift32 -> [-1, 1], deterministisch (fuer den Buzz-Rauschanteil).
	float NextNoise(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return (static_cast<float>(State) / 4294967295.0f) * 2.0f - 1.0f;
	}

	int16 ToPcm(float Value)
	{
		return static_cast<int16>(FMath::Clamp(Value, -1.0f, 1.0f) * 32767.0f);
	}
}

bool FWiesbadenUiSoundModel::GenerateSamples(EWiesbadenUiSound Kind, int32 SampleRate,
	int32 NumSamples, uint32 Seed, int16* OutSamples)
{
	if (!OutSamples || NumSamples <= 0 || SampleRate <= 0)
	{
		return false;
	}

	uint32 Rng = Seed ? Seed : 0x9E3779B9u;
	const float SR = static_cast<float>(SampleRate);
	const float TwoPi = 2.0f * PI;
	// Phase inkrementell fortzaehlen: bei einem Frequenzwechsel (Chime) springt so
	// die Phase NICHT (kein Klick), anders als bei Sin(2*pi*f*t) mit wechselndem f.
	float Phase = 0.0f;

	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const float U = static_cast<float>(Index) / static_cast<float>(NumSamples); // 0..1
		float Sample = 0.0f;

		if (Kind == EWiesbadenUiSound::KaufChime)
		{
			// Aufsteigender Zwei-Ton: erste 40 % 660 Hz, danach 990 Hz (Quinte).
			const float Freq = (U < 0.4f) ? 660.0f : 990.0f;
			Phase += TwoPi * Freq / SR;
			const float Env = FMath::Exp(-4.0f * U); // exponentieller Ausklang -> ~0
			Sample = 0.42f * Env * FMath::Sin(Phase);
		}
		else // AblehnungBuzz
		{
			// Tiefer Buzz: 150 Hz, halb Sinus / halb Rechteck (Kante = "brummig"),
			// plus etwas Rauschen. Schnelle Attacke, dann Ausklang -> ~0.
			Phase += TwoPi * 150.0f / SR;
			const float Sine = FMath::Sin(Phase);
			const float Square = (Sine >= 0.0f) ? 1.0f : -1.0f;
			const float Tone = FMath::Lerp(Sine, Square, 0.5f);
			const float Noise = 0.15f * NextNoise(Rng);
			const float Env = FMath::Min(1.0f, U * 20.0f) * FMath::Exp(-5.0f * U);
			Sample = 0.40f * Env * (Tone + Noise);
		}

		if (Phase > TwoPi)
		{
			Phase -= TwoPi; // Praezisionsverlust von Sin bei grossen Argumenten vermeiden
		}
		OutSamples[Index] = ToPcm(Sample);
	}
	return true;
}
