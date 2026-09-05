// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Strangzeiten EINES Bildes (ms), wie sie das Subsystem misst. */
struct FWbStrandTimes
{
	double LightMs = 0.0;
	double TrafficMs = 0.0;
	double PedestrianMs = 0.0;
	double GameThreadMs = 0.0;
	double RenderThreadMs = 0.0;
	double GpuMs = 0.0;
};

/** Ergebnis eines Messfensters - die Mittelwerte, die die Melder ablesen. */
struct FWbFrameReport
{
	int32 FrameCount = 0;
	double MeanMs = 0.0;
	double Fps = 0.0;
	double WorstMs = 0.0;
	int32 SpikeCount = 0;
	int32 HitchCount = 0;
	double MeanLightMs = 0.0;
	double MeanTrafficMs = 0.0;
	double MeanPedestrianMs = 0.0;
	double MeanSubsystemMs = 0.0;
	double MeanGameThreadMs = 0.0;
	double MeanRenderThreadMs = 0.0;
	double MeanGpuMs = 0.0;
};

/**
 * Gefensterte Bildzeit-Statistik.
 *
 * "Es ruckelt" ist keine Groesse, mit der sich arbeiten laesst. Dieses reine
 * Wertetyp-Modul liefert eine: mittlere/schlechteste Bildzeit, Ausreisser
 * (relativ zum laufenden Mittel), Aussetzer (> 50 ms) und die Strangzeiten.
 *
 * Vorlauf (4 s, bis Streaming/Shader durch sind) und Fensterlaenge (15 s, damit
 * die Zahl WIEDERHOLT und damit belastbar erscheint) stecken hier drin - die
 * frueher an ZWEI Stellen wortgleich duplizierte Reset-Logik faellt damit auf
 * eine Methode (BeginWindow) zusammen. Die Engine-Globalzeiten (GGameThreadTime
 * etc.) liest der Aufrufer und reicht sie ueber AddStrands herein; dadurch bleibt
 * das Modul rein und ohne Welt pruefbar (Test World.FrameProfiler).
 */
struct WIESBADENREAL_API FWbFrameProfiler
{
	/** Ein Bild einrechnen (das erste Riesen-Ladebild >= 500 ms wird uebersprungen). */
	void SampleFrame(double FrameMs);

	/** Strangzeiten eines Bildes aufsummieren (Ampeln/Verkehr/Fussgaenger/Straenge). */
	void AddStrands(const FWbStrandTimes& Strands);

	/** Zeit dieses Subsystems im Bild aufsummieren. */
	void AddSubsystemTime(double Ms);

	/**
	 * Zeit weiterdrehen. Rueckgabe true = ein 15-s-Fenster ist voll und eine
	 * Meldung ist faellig; der Aufrufer liest dann Report() und ruft BeginWindow().
	 * Startet die Messung intern nach 4 s Vorlauf.
	 */
	bool AdvanceWindow(float DeltaSeconds);

	/** Fenster zuruecksetzen (Akkumulatoren + Vorlauf; Mess-Latch bleibt). */
	void BeginWindow();

	/** True, sobald der 4-s-Vorlauf vorbei ist. */
	bool IsMeasuring() const { return bMeasurementStarted; }

	/** Mittelwerte des laufenden Fensters (null-sicher bei 0 Bildern). */
	FWbFrameReport Report() const;

private:
	double FrameTimeSumMs = 0.0;
	double WorstFrameMs = 0.0;
	int32 FrameCount = 0;
	int32 HitchCount = 0;
	int32 SpikeCount = 0;

	double LightMs = 0.0;
	double TrafficMs = 0.0;
	double PedestrianMs = 0.0;
	double SubsystemMs = 0.0;
	double GameThreadMs = 0.0;
	double RenderThreadMs = 0.0;
	double GpuMs = 0.0;

	float MeasurementDelay = 0.0f;
	bool bMeasurementStarted = false;
};
