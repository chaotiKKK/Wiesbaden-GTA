// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Weapons/WiesbadenGunshotSynth.h"

int32 FWiesbadenGunshotSynth::GetSampleCount(const FWiesbadenGunshotParams& Params)
{
	const int32 Rate = FMath::Max(Params.SampleRate, 8000);
	const float Duration = FMath::Max(Params.DurationSeconds, 0.05f);
	return FMath::Max(1, FMath::RoundToInt(Rate * Duration));
}

void FWiesbadenGunshotSynth::RenderShot(
	const FWiesbadenGunshotParams& Params, int32 Seed, TArray<int16>& OutSamples)
{
	const int32 Rate = FMath::Max(Params.SampleRate, 8000);
	const int32 Count = GetSampleCount(Params);
	const float InvRate = 1.0f / static_cast<float>(Rate);

	OutSamples.Reset();
	OutSamples.SetNumUninitialized(Count);

	// Gesetzter Zufall: Zwei Schuesse duerfen sich unterscheiden, aber derselbe
	// Seed muss dieselbe Kurve liefern - sonst ist nichts pruefbar.
	FRandomStream Random(Seed);

	// Zustand des Tiefpasses fuer den Nachhall. Der Nachhall kommt von
	// Hauswaenden zurueck, die hohe Frequenzen schlucken; ungefiltert zischt er.
	float TailFilterState = 0.0f;
	const float TailDamping = FMath::Clamp(Params.TailDamping, 0.01f, 1.0f);

	const float CrackDecay = FMath::Max(Params.CrackDecaySeconds, 0.001f);
	const float TailDecay = FMath::Max(Params.TailDecaySeconds, 0.01f);
	const float BodyDecay = FMath::Max(Params.BodyDecaySeconds, 0.005f);
	const float BodyOmega = 2.0f * PI * FMath::Max(Params.BodyFrequencyHz, 20.0f);

	const float BodyMix = FMath::Clamp(Params.BodyMix, 0.0f, 1.0f);
	const float TailMix = FMath::Clamp(Params.TailMix, 0.0f, 1.0f);
	const float Master = FMath::Clamp(Params.MasterGain, 0.0f, 1.0f);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Time = Index * InvRate;

		// Weisses Rauschen als gemeinsame Quelle fuer Knall und Nachhall - es
		// ist derselbe physikalische Vorgang, nur verschieden lang gehoert.
		const float Noise = Random.FRandRange(-1.0f, 1.0f);

		// 1) Muendungsknall: steile Flanke, sehr kurzes Abklingen.
		const float CrackEnvelope = FMath::Exp(-Time / CrackDecay);
		const float Crack = Noise * CrackEnvelope;

		// 2) Koerperschall von Lauf und Verschluss: tiefer, kurz klingender
		//    Ton. Ohne ihn klingt der Schuss nach Luftballon.
		const float BodyEnvelope = FMath::Exp(-Time / BodyDecay);
		const float Body = FMath::Sin(BodyOmega * Time) * BodyEnvelope;

		// 3) Nachhall: gefiltertes Rauschen, laenger stehend. Einpoliger
		//    Tiefpass - eine Mittelung ueber den Vorgaenger.
		TailFilterState += (Noise - TailFilterState) * TailDamping;
		const float TailEnvelope = FMath::Exp(-Time / TailDecay);
		const float Tail = TailFilterState * TailEnvelope;

		float Sample = Crack * (1.0f - BodyMix - TailMix)
			+ Body * BodyMix
			+ Tail * TailMix;

		Sample *= Master;

		// Weiche Begrenzung statt harten Abschneidens: Ein Schuss faehrt in
		// die Begrenzung, und ein hartes Clipping klaenge wie ein Defekt.
		Sample = FMath::Tanh(Sample * 1.6f);

		OutSamples[Index] = static_cast<int16>(
			FMath::Clamp(FMath::RoundToInt(Sample * 32767.0f), -32767, 32767));
	}
}

float FWiesbadenGunshotSynth::GetPeakLevel(const TArray<int16>& Samples)
{
	int32 Peak = 0;
	for (const int16 Sample : Samples)
	{
		Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(Sample)));
	}
	return Peak / 32767.0f;
}
