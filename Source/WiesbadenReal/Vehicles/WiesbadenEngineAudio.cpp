// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenEngineAudio.h"

namespace
{
	/** Deterministischer Rauschgenerator (xorshift32), damit Tests reproduzierbar sind. */
	FORCEINLINE float NextNoise(uint32& Seed)
	{
		Seed ^= Seed << 13;
		Seed ^= Seed >> 17;
		Seed ^= Seed << 5;
		// [0,1) -> [-1,1)
		return (static_cast<float>(Seed & 0x00FFFFFFu) / static_cast<float>(0x01000000u)) * 2.0f - 1.0f;
	}

	/**
	 * Amplituden der Oberwellen des Zuendpulses. Der Boxer klingt tief und
	 * rau, die Energie liegt also auf der Grundwelle und den ersten
	 * Oberwellen; hohe Oberwellen wuerden ihn nach Motorrad klingen lassen.
	 */
	constexpr int32 HarmonicCount = 6;
	constexpr float HarmonicWeights[HarmonicCount] = { 1.00f, 0.62f, 0.38f, 0.22f, 0.13f, 0.07f };
}

float FWiesbadenEngineAudioModel::GetFiringFrequencyHz(float EngineRpm, int32 CylinderCount)
{
	const float Rpm = FMath::Max(0.0f, EngineRpm);
	const int32 Cylinders = FMath::Clamp(CylinderCount, 1, 16);

	// Viertakt: je Zylinder eine Zuendung pro zwei Umdrehungen.
	return (Rpm / 60.0f) * (static_cast<float>(Cylinders) * 0.5f);
}

float FWiesbadenEngineAudioModel::GetOutputGain(const FWiesbadenEngineAudioParams& Params)
{
	if (!Params.bEngineRunning)
	{
		return 0.0f;
	}

	const float Rpm = FMath::Max(0.0f, Params.EngineRpm);
	const float Idle = FMath::Max(1.0f, Params.IdleRpm);

	// Drehzahlanteil: im Leerlauf leise, gegen 4000 U/min voll.
	const float RpmFactor = FMath::Clamp((Rpm - Idle * 0.5f) / 3200.0f, 0.0f, 1.0f);

	// Lastanteil: Gasgeben ist deutlich lauter als Schiebebetrieb bei
	// gleicher Drehzahl - das macht den Unterschied zwischen "faehrt" und
	// "rollt aus" hoerbar.
	const float LoadFactor = 0.45f + 0.55f * FMath::Clamp(Params.Throttle, 0.0f, 1.0f);

	const float Gain = (0.22f + 0.78f * RpmFactor) * LoadFactor
		* FMath::Clamp(Params.MasterGain, 0.0f, 1.0f);

	return FMath::Clamp(Gain, 0.0f, 1.0f);
}

bool FWiesbadenEngineAudioModel::GenerateSamples(
	const FWiesbadenEngineAudioParams& Params,
	int32 SampleRate,
	int32 NumSamples,
	FWiesbadenEngineAudioState& State,
	int16* OutSamples)
{
	if (!OutSamples || NumSamples <= 0)
	{
		return false;
	}

	if (SampleRate <= 0)
	{
		FMemory::Memzero(OutSamples, sizeof(int16) * NumSamples);
		return false;
	}

	if (!Params.bEngineRunning)
	{
		FMemory::Memzero(OutSamples, sizeof(int16) * NumSamples);
		State.SmoothedRpm = 0.0f;
		return true;
	}

	// Drehzahl glaetten: die Physik liefert sie je Frame, die Synthese
	// arbeitet je Sample. Ein harter Sprung waere als Sirenenton hoerbar.
	const float TargetRpm = FMath::Max(Params.IdleRpm * 0.5f, Params.EngineRpm);
	if (State.SmoothedRpm <= 0.0f)
	{
		State.SmoothedRpm = TargetRpm;
	}

	const double SampleRateD = static_cast<double>(SampleRate);
	const float BlockSeconds = static_cast<float>(NumSamples) / static_cast<float>(SampleRate);

	// Zeitkonstante ~60 ms: schnell genug fuer Gasstoesse, langsam genug
	// gegen Zittern.
	const float SmoothAlpha = FMath::Clamp(BlockSeconds / 0.06f, 0.0f, 1.0f);
	State.SmoothedRpm = FMath::Lerp(State.SmoothedRpm, TargetRpm, SmoothAlpha);

	const float FiringHz = GetFiringFrequencyHz(State.SmoothedRpm, Params.CylinderCount);
	const float Gain = GetOutputGain(Params);

	const double FiringIncrement = static_cast<double>(FiringHz) / SampleRateD;

	// Ansaugmodulation laeuft mit der halben Zuendfrequenz (ein voller
	// Arbeitstakt), das erzeugt den typischen Zweiertakt im Leerlauf.
	const double IntakeIncrement = FiringIncrement * 0.5;

	const float Throttle = FMath::Clamp(Params.Throttle, 0.0f, 1.0f);

	// Rauschanteil: Ansaug- und Auspuffgeraeusch, last- und drehzahlabhaengig.
	const float NoiseAmount = 0.10f + 0.28f * Throttle
		+ 0.10f * FMath::Clamp(State.SmoothedRpm / 4000.0f, 0.0f, 1.0f);

	// Rollgeraeusch, damit das Fahrzeug auch im Schiebebetrieb nicht stumm ist.
	const float RollAmount = 0.05f * FMath::Clamp(FMath::Abs(Params.SpeedKmh) / 90.0f, 0.0f, 1.0f);

	// Tiefpass fuer das Rauschen; hoehere Last oeffnet den Filter.
	const float NoiseCutoff = FMath::Clamp(0.05f + 0.22f * Throttle, 0.02f, 0.95f);

	// Hupe als SCHIFFSNEBELHORN.
	//
	// Ein Nebelhorn liegt zwei Oktaven unter einer Autohupe: 65 und 98 Hz,
	// also eine reine Quinte statt der kleinen Terz einer Zweiklanghupe. Die
	// Quinte klingt voll und ruhig, die Terz schwebt nervoes - genau der
	// Unterschied zwischen Hafen und Stadtverkehr.
	//
	// Der Anstieg dauert 180 Millisekunden statt 12: ein Nebelhorn kommt
	// traege in Fahrt, weil eine grosse Luftsaeule bewegt werden muss. Mit
	// dem schnellen Anstieg klaenge es nach einem tiefen Piepser.
	const float HornIncrementA = 65.0f / static_cast<float>(SampleRate);
	const float HornIncrementB = 98.0f / static_cast<float>(SampleRate);
	const float HornAttackPerSample = 1.0f / (0.180f * static_cast<float>(SampleRate));
	const float HornGain = 0.85f * FMath::Clamp(Params.MasterGain, 0.0f, 1.0f);

	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		State.FiringPhase += FiringIncrement;
		if (State.FiringPhase >= 1.0)
		{
			State.FiringPhase -= FMath::FloorToDouble(State.FiringPhase);
		}

		State.IntakePhase += IntakeIncrement;
		if (State.IntakePhase >= 1.0)
		{
			State.IntakePhase -= FMath::FloorToDouble(State.IntakePhase);
		}

		const float Phase = static_cast<float>(State.FiringPhase);

		// Zuendpuls aus Oberwellen.
		float Tone = 0.0f;
		for (int32 H = 0; H < HarmonicCount; ++H)
		{
			const float HarmonicPhase = Phase * static_cast<float>(H + 1);
			Tone += FMath::Sin(HarmonicPhase * UE_TWO_PI) * HarmonicWeights[H];
		}
		Tone /= 2.42f; // Summe der Gewichte, normiert auf etwa [-1,1]

		// Boxer-Versatz: gegenueberliegende Zylinder zuenden minimal
		// ungleichmaessig. Ohne diese Modulation klingt der Motor zu glatt.
		const float BoxerOffset = 1.0f + 0.14f * FMath::Sin(static_cast<float>(State.IntakePhase) * UE_TWO_PI);
		Tone *= BoxerOffset;

		// Rauschanteil mit einpoligem Tiefpass.
		const float White = NextNoise(State.NoiseSeed);
		State.NoiseLowpass += NoiseCutoff * (White - State.NoiseLowpass);

		const float Mixed = Tone * (1.0f - NoiseAmount * 0.5f)
			+ State.NoiseLowpass * (NoiseAmount + RollAmount);

		// Weiche Saettigung statt hartem Clipping - Letzteres klingt digital.
		const float Shaped = FMath::Tanh(Mixed * 1.3f);

		// -- Nebelhorn ---------------------------------------------------------
		//
		// Grundton 65 Hz mit Quinte 98 Hz. Bei so tiefen Toenen traegt die
		// Grundschwingung ueber kleine Lautsprecher kaum - der Klang entsteht
		// erst aus den OBERWELLEN, die das Ohr zur fehlenden Grundfrequenz
		// ergaenzt. Deshalb hier vier Teiltoene je Ton statt einer.
		const float HornTarget = Params.bHorn ? 1.0f : 0.0f;
		State.HornGain += (HornTarget - State.HornGain)
			* FMath::Clamp(HornAttackPerSample, 0.0f, 1.0f);

		float HornSample = 0.0f;
		if (State.HornGain > 0.001f)
		{
			State.HornPhaseA += HornIncrementA;
			if (State.HornPhaseA >= 1.0f) { State.HornPhaseA -= 1.0f; }
			State.HornPhaseB += HornIncrementB;
			if (State.HornPhaseB >= 1.0f) { State.HornPhaseB -= 1.0f; }

			auto Stack = [](float Phase) -> float
			{
				// Abfallende Teiltoene - so klingt eine angeblasene Luftsaeule,
				// nicht ein Rechteckgenerator.
				return FMath::Sin(Phase * UE_TWO_PI)
					+ 0.60f * FMath::Sin(Phase * 2.0f * UE_TWO_PI)
					+ 0.32f * FMath::Sin(Phase * 3.0f * UE_TWO_PI)
					+ 0.16f * FMath::Sin(Phase * 4.0f * UE_TWO_PI);
			};

			const float A = Stack(State.HornPhaseA);
			const float B = Stack(State.HornPhaseB);

			HornSample = FMath::Tanh((A + B) * 0.42f) * State.HornGain * HornGain;
		}

		const float Sample = FMath::Clamp(Shaped * Gain + HornSample, -1.0f, 1.0f);
		OutSamples[Index] = static_cast<int16>(Sample * 32767.0f);
	}

	return true;
}
