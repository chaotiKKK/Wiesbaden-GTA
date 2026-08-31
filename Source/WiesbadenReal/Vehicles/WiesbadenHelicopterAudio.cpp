// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenHelicopterAudio.h"

namespace
{
	constexpr float TwoPi = 2.0f * PI;

	/** xorshift32 - kleiner, schneller, deterministischer PRNG. */
	inline uint32 NextRand(uint32& State)
	{
		uint32 X = State;
		X ^= X << 13;
		X ^= X >> 17;
		X ^= X << 5;
		State = X;
		return X;
	}

	/** Normiertes weisses Rauschen in [-0.5, 0.5). */
	inline float NextNoise(uint32& State)
	{
		return (static_cast<float>(NextRand(State)) / 4294967296.0f) - 0.5f;
	}
}

float FWiesbadenHelicopterAudioModel::GetBladePassFrequency(float MainRotorRpm, int32 BladeCount)
{
	return (FMath::Max(MainRotorRpm, 0.0f) / 60.0f) * FMath::Max(1, BladeCount);
}

float FWiesbadenHelicopterAudioModel::GetFilterCutoffHz(float MainRotorRpm, float Collective)
{
	// Leerlauf-Drehzahl ~420 U/min als Referenz: je hoeher die Drehzahl und je
	// hoeher die Blattlast, desto heller (mehr Hoehenanteile im Rotor-Geraeusch).
	const float Cutoff = 180.0f
		+ (FMath::Max(MainRotorRpm, 0.0f) / 420.0f) * 1400.0f
		+ FMath::Clamp(Collective, 0.0f, 1.0f) * 250.0f;
	return FMath::Clamp(Cutoff, 60.0f, 8000.0f);
}

float FWiesbadenHelicopterAudioModel::GetRotorCutoffHz(const FWiesbadenHelicopterAudioParams& Params)
{
	const float Cutoff = FMath::Clamp(Params.RotorCutoffBaseHz, 40.0f, 2000.0f)
		+ (FMath::Max(Params.MainRotorRpm, 0.0f) / 420.0f) * 1400.0f
		+ FMath::Clamp(Params.Collective, 0.0f, 1.0f) * 250.0f;
	return FMath::Clamp(Cutoff, 60.0f, 8000.0f);
}

void FWiesbadenHelicopterAudioModel::GenerateSamples(
	const FWiesbadenHelicopterAudioParams& Params,
	int32 SampleRate, int32 NumSamples, uint32 Seed, int16* OutSamples)
{
	if (!OutSamples || NumSamples <= 0 || SampleRate <= 0)
	{
		return;
	}

	uint32 RandState = (Seed != 0) ? Seed : 0x9E3779B9u;
	const float SampleRateF = static_cast<float>(SampleRate);

	// Rotor: One-Pole-Tiefpass-Zustand + Wop-Wop-Phase.
	const float Cutoff = GetRotorCutoffHz(Params);
	const float FilterAlpha = 1.0f - FMath::Exp(-TwoPi * Cutoff / SampleRateF);
	const float BladePass = GetBladePassFrequency(Params.MainRotorRpm, Params.BladeCount);
	const float SlapDepth = FMath::Clamp(Params.BladeSlapDepth, 0.0f, 1.0f);
	const float RotorVolume = (0.25f + 0.75f * FMath::Clamp(Params.Collective, 0.0f, 1.0f)) * 0.75f;

	// Motor: Grundfrequenz aus Drehzahl * 8 Zylinder (V8-Sound-Charakter).
	const bool bMotorAudible = Params.bEngineRunning && Params.EngineRpm > 50.0f;
	const float MotorFreq = bMotorAudible ? (Params.EngineRpm / 60.0f) * 8.0f : 0.0f;

	float Filtered = 0.0f;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const float Time = static_cast<float>(Index) / SampleRateF;

		// Rotor-Anteil: Rauschen -> Tiefpass -> Wop-Wop-Amplitudenmodulation.
		const float Noise = NextNoise(RandState);
		Filtered += FilterAlpha * (Noise - Filtered);

		float RotorSample = 0.0f;
		if (BladePass > 0.1f)
		{
			// Blade Slap: je hoeher die Slap-Tiefe, desto deutlicher der
			// Schlag (Kampfheli) statt eines gleichmaessigen Rauschens.
			const float Wop = 0.62f + SlapDepth * FMath::Sin(TwoPi * BladePass * Time + 1.7f);
			RotorSample = Filtered * Wop * RotorVolume;
		}

		// Motor-Anteil: Ton + Oberwelle + gedaempftes Rauschen.
		float MotorSample = 0.0f;
		if (bMotorAudible)
		{
			const float Tone = FMath::Sin(TwoPi * MotorFreq * Time)
				+ 0.4f * FMath::Sin(TwoPi * 2.0f * MotorFreq * Time);
			const float MotorNoise = NextNoise(RandState) * 0.25f;
			MotorSample = (Tone + MotorNoise) * 0.5f;
		}

		const float Mix = (RotorSample + MotorSample) * 32767.0f * 0.8f;
		OutSamples[Index] = static_cast<int16>(FMath::Clamp(Mix, -32768.0f, 32767.0f));
	}
}
