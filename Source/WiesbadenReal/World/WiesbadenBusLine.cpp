// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenBusLine.h"

namespace
{
	// Ein Fahrabschnitt der Rundfahrt: Dauer, Bogenlaenge Start->Ende, Halt?, Richtung.
	struct FPhase { double Dur; double A; double B; bool bDwell; bool bForward; };

	double BuildPhases(const WiesbadenBusLine::FBusRoute& R, double v,
		double Dwell, double Term, TArray<FPhase>& Out)
	{
		Out.Reset();
		const int32 N = R.StopArcCm.Num();
		if (N < 2 || v <= 0.0)
		{
			return 0.0;
		}
		const double Vv = FMath::Max(v, 1.0);
		// Hinfahrt: Halt 0 -> Halt N-1, an jeder Zielhalte verweilen.
		for (int32 i = 0; i < N - 1; ++i)
		{
			const double A = R.StopArcCm[i];
			const double B = R.StopArcCm[i + 1];
			Out.Add({ FMath::Abs(B - A) / Vv, A, B, false, true });
			Out.Add({ (i + 1 == N - 1) ? Term : Dwell, B, B, true, true });
		}
		// Rueckfahrt: Halt N-1 -> Halt 0.
		for (int32 i = N - 1; i > 0; --i)
		{
			const double A = R.StopArcCm[i];
			const double B = R.StopArcCm[i - 1];
			Out.Add({ FMath::Abs(A - B) / Vv, A, B, false, false });
			Out.Add({ (i - 1 == 0) ? Term : Dwell, B, B, true, false });
		}
		double Total = 0.0;
		for (const FPhase& P : Out) { Total += P.Dur; }
		return Total;
	}
}

double WiesbadenBusLine::RoundTripSeconds(const FBusRoute& Route, double v, double Dwell, double Term)
{
	TArray<FPhase> Phases;
	return BuildPhases(Route, v, Dwell, Term, Phases);
}

WiesbadenBusLine::FBusState WiesbadenBusLine::EvaluateRoundTrip(double Elapsed,
	const FBusRoute& Route, double v, double Dwell, double Term)
{
	FBusState S;
	TArray<FPhase> Phases;
	const double Cycle = BuildPhases(Route, v, Dwell, Term, Phases);
	if (Cycle <= 0.0 || Phases.Num() == 0)
	{
		S.ArcLengthCm = Route.StopArcCm.Num() > 0 ? Route.StopArcCm[0] : 0.0;
		return S;
	}
	double t = FMath::Fmod(FMath::Max(Elapsed, 0.0), Cycle);
	for (int32 i = 0; i < Phases.Num(); ++i)
	{
		const FPhase& P = Phases[i];
		if (t < P.Dur || i == Phases.Num() - 1)
		{
			const double f = (P.Dur > 0.0) ? FMath::Clamp(t / P.Dur, 0.0, 1.0) : 0.0;
			S.ArcLengthCm = P.bDwell ? P.A : FMath::Lerp(P.A, P.B, f);
			S.bForward = P.bForward;
			S.bDwelling = P.bDwell;
			return S;
		}
		t -= P.Dur;
	}
	S.ArcLengthCm = Route.StopArcCm[0];
	return S;
}

double WiesbadenBusLine::BayFactor(double ArcLengthCm, bool bDwelling,
	const TArray<double>& StopArcCm, double BayZoneCm)
{
	if (bDwelling) { return 1.0; }
	if (StopArcCm.Num() == 0 || BayZoneCm <= 0.0) { return 0.0; }
	double Nearest = TNumericLimits<double>::Max();
	for (const double S : StopArcCm)
	{
		Nearest = FMath::Min(Nearest, FMath::Abs(ArcLengthCm - S));
	}
	const double t = FMath::Clamp(Nearest / BayZoneCm, 0.0, 1.0);
	// smoothstep, aber invertiert: 1 an der Halte (t=0), 0 ab BayZoneCm (t>=1).
	return 1.0 - t * t * (3.0 - 2.0 * t);
}

void WiesbadenBusLine::ActiveRuns(double ServiceSeconds, const FBusSchedule& Schedule,
	double RoundTripSeconds, TArray<FBusRun>& OutRuns)
{
	OutRuns.Reset();
	const int32 M = Schedule.DepartureSeconds.Num();
	if (M == 0 || Schedule.DaySeconds <= 0.0 || RoundTripSeconds <= 0.0)
	{
		return;
	}
	// Fahrplan wiederholt sich alle DaySeconds. Abfahrten von GESTERN und HEUTE
	// pruefen, damit Kurse ueber Mitternacht mitgezaehlt werden (setzt voraus, dass
	// eine Rundfahrt kuerzer als ein Tag ist - hier ~42 min << 24 h).
	const int64 DayIndex = (int64)FMath::FloorToDouble(ServiceSeconds / Schedule.DaySeconds);
	for (int64 d = DayIndex - 1; d <= DayIndex; ++d)
	{
		for (int32 i = 0; i < M; ++i)
		{
			const double Dep = (double)d * Schedule.DaySeconds + Schedule.DepartureSeconds[i];
			const double Elapsed = ServiceSeconds - Dep;
			if (Elapsed >= 0.0 && Elapsed < RoundTripSeconds)
			{
				FBusRun Run;
				Run.Elapsed = Elapsed;
				Run.Index = d * (int64)M + i;   // global fortlaufend (auch ueber Tagesgrenze)
				OutRuns.Add(Run);
			}
		}
	}
}

double WiesbadenBusLine::SecondsToStop(const FBusRoute& Route, double v,
	double Dwell, int32 StopIndex)
{
	const int32 N = Route.StopArcCm.Num();
	const double Vv = FMath::Max(v, 1.0);
	if (StopIndex <= 0 || N < 2) { return 0.0; }
	const int32 Target = FMath::Min(StopIndex, N - 1);
	double t = 0.0;
	for (int32 i = 0; i < Target; ++i)
	{
		t += (Route.StopArcCm[i + 1] - Route.StopArcCm[i]) / Vv;   // Fahrsegment i->i+1
		if (i + 1 < Target) { t += Dwell; }                        // Verweilen an Zwischenhalte
	}
	return t;
}

void WiesbadenBusLine::NextDepartures(double ServiceSeconds, const FBusSchedule& Schedule,
	double OffsetToStopSeconds, int32 MaxCount, TArray<double>& OutSecondsUntil)
{
	OutSecondsUntil.Reset();
	const int32 M = Schedule.DepartureSeconds.Num();
	if (M == 0 || MaxCount <= 0 || Schedule.DaySeconds <= 0.0) { return; }
	const int64 DayIndex = (int64)FMath::FloorToDouble(ServiceSeconds / Schedule.DaySeconds);
	TArray<double> Cand;
	// Heute und morgen pruefen -> auch Durchfahrten kurz nach Mitternacht / erste
	// Frueh-Abfahrten des Folgetags erscheinen korrekt.
	for (int64 d = DayIndex; d <= DayIndex + 1; ++d)
	{
		for (int32 i = 0; i < M; ++i)
		{
			const double PassTime = (double)d * Schedule.DaySeconds + Schedule.DepartureSeconds[i] + OffsetToStopSeconds;
			const double Until = PassTime - ServiceSeconds;
			if (Until >= 0.0) { Cand.Add(Until); }
		}
	}
	Cand.Sort();
	const int32 K = FMath::Min(MaxCount, Cand.Num());
	for (int32 i = 0; i < K; ++i) { OutSecondsUntil.Add(Cand[i]); }
}
