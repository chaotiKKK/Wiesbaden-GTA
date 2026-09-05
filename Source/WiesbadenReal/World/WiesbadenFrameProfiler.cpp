// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenFrameProfiler.h"

void FWbFrameProfiler::SampleFrame(double FrameMs)
{
	// Das erste Riesen-Ladebild nicht mitzaehlen (frueher: FrameCount>0 || dt<0.5).
	if (FrameCount == 0 && FrameMs >= 500.0)
	{
		return;
	}

	// Ausreisser RELATIV zum laufenden Mittel zaehlen, nicht gegen eine feste
	// Schranke - die 50-ms-Schranke wird wertlos, sobald das Mittel in ihre Naehe
	// kommt. Erst ab elf Bildern ist ein Mittel belastbar.
	if (FrameCount > 10)
	{
		const double RunningMean = FrameTimeSumMs / FrameCount;
		if (FrameMs > RunningMean * 2.0)
		{
			++SpikeCount;
		}
	}

	FrameTimeSumMs += FrameMs;
	WorstFrameMs = FMath::Max(WorstFrameMs, FrameMs);
	++FrameCount;
	if (FrameMs > 50.0)
	{
		++HitchCount;
	}
}

void FWbFrameProfiler::AddStrands(const FWbStrandTimes& Strands)
{
	LightMs += Strands.LightMs;
	TrafficMs += Strands.TrafficMs;
	PedestrianMs += Strands.PedestrianMs;
	GameThreadMs += Strands.GameThreadMs;
	RenderThreadMs += Strands.RenderThreadMs;
	GpuMs += Strands.GpuMs;
}

void FWbFrameProfiler::AddSubsystemTime(double Ms)
{
	SubsystemMs += Ms;
}

bool FWbFrameProfiler::AdvanceWindow(float DeltaSeconds)
{
	MeasurementDelay += DeltaSeconds;

	if (bMeasurementStarted && FrameCount > 0 && MeasurementDelay > 15.0f)
	{
		return true;   // Fenster voll - Aufrufer: Report() + BeginWindow()
	}
	if (!bMeasurementStarted && MeasurementDelay > 4.0f)
	{
		bMeasurementStarted = true;   // Vorlauf durch: ab jetzt messen
		BeginWindow();
	}
	return false;
}

void FWbFrameProfiler::BeginWindow()
{
	MeasurementDelay = 0.0f;
	FrameTimeSumMs = 0.0;
	WorstFrameMs = 0.0;
	FrameCount = 0;
	HitchCount = 0;
	SpikeCount = 0;
	LightMs = 0.0;
	TrafficMs = 0.0;
	PedestrianMs = 0.0;
	SubsystemMs = 0.0;
	GameThreadMs = 0.0;
	RenderThreadMs = 0.0;
	GpuMs = 0.0;
}

FWbFrameReport FWbFrameProfiler::Report() const
{
	FWbFrameReport R;
	R.FrameCount = FrameCount;
	R.WorstMs = WorstFrameMs;
	R.SpikeCount = SpikeCount;
	R.HitchCount = HitchCount;
	if (FrameCount > 0)
	{
		const double Inv = 1.0 / static_cast<double>(FrameCount);
		R.MeanMs = FrameTimeSumMs * Inv;
		R.Fps = 1000.0 / FMath::Max(R.MeanMs, 0.01);
		R.MeanLightMs = LightMs * Inv;
		R.MeanTrafficMs = TrafficMs * Inv;
		R.MeanPedestrianMs = PedestrianMs * Inv;
		R.MeanSubsystemMs = SubsystemMs * Inv;
		R.MeanGameThreadMs = GameThreadMs * Inv;
		R.MeanRenderThreadMs = RenderThreadMs * Inv;
		R.MeanGpuMs = GpuMs * Inv;
	}
	return R;
}
