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
	int32 SampleRate, int32 NumSamples, uint32 Seed, int16* OutSamples,
	double StartTimeSeconds)
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
	// Koaxial (Ka-52): der zweite gegenlaeufige Rotor dreht mit gleicher Drehzahl,
	// seine Blattpaesse verschraenken sich mit denen des ersten -> ein DICHTERER,
	// haerterer Schlag (naeher an 2x Blattpass) mit langsamer Schwebung. Vorwaerts
	// verstaerkt der vorlaufende Rotorblatt den Schlag ("advancing blade slap").
	const float SpeedFactor = FMath::Clamp(Params.ForwardSpeedMetersPerS / 60.0f, 0.0f, 1.0f);
	const float SlapDepth = FMath::Clamp(Params.BladeSlapDepth, 0.0f, 1.0f) * (1.0f + 0.35f * SpeedFactor);
	const float RotorVolume = (0.25f + 0.75f * FMath::Clamp(Params.Collective, 0.0f, 1.0f)) * 0.75f;

	// Triebwerk: 2x Klimov VK-2500 Wellenturbine - ein heller, metallischer
	// Turbinen-Whine (Spool-Grundton + Obertonkamm) mit Kompressor-Buzz und
	// Ansaug-/Abgas-Luftrauschen. KEIN Kolbenmotor-Ton mehr (frueher als 8-
	// Zylinder-V8 modelliert - fuer eine Wellenturbine falsch).
	const bool bMotorAudible = Params.bEngineRunning && Params.EngineRpm > 50.0f;
	const float Spool = FMath::Clamp(Params.EngineRpm / 3360.0f, 0.0f, 1.6f);   // ~1.0 bei Nenndrehzahl
	const float WhineHz = 300.0f + Spool * 600.0f;                              // ~300 Hz Leerlauf .. ~1.3 kHz Vollast (tief/weich)

	float Filtered = 0.0f;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		// Fortlaufende Zeitbasis in DOUBLE: der Aufrufer zaehlt ueber alle Puffer
		// hoch, sodass die Sinus-Phasen NICHT je Puffer springen (sonst buzzt die
		// Turbine mit der Pufferrate). Double haelt die Phase auch nach Minuten
		// praezise; die Sinus-Argumente promoten dadurch auf double.
		const double Time = StartTimeSeconds + static_cast<double>(Index) / SampleRateF;

		// Rotor-Anteil: Rauschen -> Tiefpass -> koaxiale Wop-Wop-Modulation.
		const float Noise = NextNoise(RandState);
		Filtered += FilterAlpha * (Noise - Filtered);

		float RotorSample = 0.0f;
		if (BladePass > 0.1f)
		{
			const float Phase = TwoPi * BladePass * Time;
			// Zwei verschraenkte Blattpaesse (Koaxial) + langsame Schwebung. Der
			// 2x-Anteil bewusst schwach, sonst wird der Schlag "brummig/buzzig".
			const float Wop = 0.55f
				+ SlapDepth * (0.75f * FMath::Sin(Phase + 1.7f)
							 + 0.28f * FMath::Sin(2.0f * Phase + 0.5f));
			const float Throb = 1.0f + 0.05f * FMath::Sin(TwoPi * 3.2f * Time);
			RotorSample = Filtered * FMath::Max(0.0f, Wop) * Throb * RotorVolume;
		}

		// Turbinen-Anteil: Whine-Kamm + Buzz + Luftrauschen (nur bei Lauf).
		float TurbineSample = 0.0f;
		if (bMotorAudible)
		{
			// Weicher, TIEFER Spool-Ton (nur Grundton + leiser 2. Oberton) plus
			// etwas Luftrauschen. Die frueheren hellen Obertoene (bis ~8,8 kHz)
			// klangen schrill/kuenstlich ("total unreal") - komplett entfernt.
			// Deutlich leiser gemischt, damit der Rotorschlag das Bild traegt.
			const float Tone = 0.30f * FMath::Sin(TwoPi * WhineHz * Time)
							 + 0.12f * FMath::Sin(TwoPi * 2.0f * WhineHz * Time);
			const float Air = NextNoise(RandState) * 0.35f;
			TurbineSample = (Tone + Air) * (0.10f + 0.20f * Spool);
		}

		const float Mix = (RotorSample + TurbineSample) * 32767.0f * 0.8f;
		OutSamples[Index] = static_cast<int16>(FMath::Clamp(Mix, -32768.0f, 32767.0f));
	}
}
