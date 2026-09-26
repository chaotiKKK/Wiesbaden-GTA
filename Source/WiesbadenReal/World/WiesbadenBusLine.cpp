// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenBusLine.h"

namespace
{
	// Ein Fahrabschnitt der Rundfahrt: Dauer, Bogenlaenge Start->Ende, Halt?, Richtung,
	// auf dem eigenen Rueckweg?
	struct FPhase { double Dur; double A; double B; bool bDwell; bool bForward; bool bReturn = false; };

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
		// Eigener Rueckweg: Anschluss vom Hinweg-Ende zur ersten Rueckweg-Halte
		// (Einstieg), Halt fuer Halt bis zum Ausstieg (letzte Halte, dort die
		// Wendezeit als Pause), dann leer zur Einstiegshaltestelle = Halte 0 des
		// Hinwegs und dort einsteigen lassen.
		if (R.HasReturnLeg())
		{
			const TArray<double>& RS = R.ReturnStopArcCm;
			const int32 M = RS.Num();
			Out.Add({ RS[0] / Vv, 0.0, RS[0], false, false, true });
			Out.Add({ Dwell, RS[0], RS[0], true, false, true });
			for (int32 j = 0; j < M - 1; ++j)
			{
				Out.Add({ FMath::Abs(RS[j + 1] - RS[j]) / Vv, RS[j], RS[j + 1], false, false, true });
				Out.Add({ (j + 1 == M - 1) ? Term : Dwell, RS[j + 1], RS[j + 1], true, false, true });
			}
			Out.Add({ FMath::Max(R.ReturnLengthCm - RS[M - 1], 0.0) / Vv, RS[M - 1], R.ReturnLengthCm, false, false, true });
			Out.Add({ Dwell, R.StopArcCm[0], R.StopArcCm[0], true, true, false });
			double TotalR = 0.0;
			for (const FPhase& P : Out) { TotalR += P.Dur; }
			return TotalR;
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
			S.bReturnPath = P.bReturn;
			S.bDwelling = P.bDwell;
			S.DwellRemainingSeconds = P.bDwell ? FMath::Max(P.Dur - t, 0.0) : 0.0;
			S.DwellTotalSeconds = P.bDwell ? P.Dur : 0.0;
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

void WiesbadenBusLine::BuildFleet(const FBusRoute& Route, const FServiceConfig& Config,
	double& OutCycleSeconds, TArray<FBusVehicle>& OutFleet)
{
	OutFleet.Reset();
	OutCycleSeconds = RoundTripSeconds(Route, Config.CruiseSpeedCmS,
		Config.StopDwellSeconds, Config.TerminusDwellSeconds);
	if (OutCycleSeconds <= 0.0)
	{
		return;
	}

	const double Headway = FMath::Max(Config.HeadwaySeconds, 1.0);
	const int32 MaxBuses = FMath::Max(Config.MaxBuses, 2);
	const int32 Count = FMath::Clamp((int32)FMath::CeilToDouble(OutCycleSeconds / Headway), 2, MaxBuses);
	const double Spacing = OutCycleSeconds / (double)Count;
	for (int32 k = 0; k < Count; ++k)
	{
		FBusVehicle V;
		V.Id = Config.FirstVehicleId + k;   // feste Nummer, ueber beide Linien eindeutig
		V.PhaseSeconds = Spacing * (double)k;
		OutFleet.Add(V);
	}
}

WiesbadenBusLine::FBusState WiesbadenBusLine::FleetStateAt(double ServiceSeconds,
	const FBusVehicle& Vehicle, const FBusRoute& Route, double v, double Dwell, double Term)
{
	const double Cycle = RoundTripSeconds(Route, v, Dwell, Term);
	if (Cycle <= 0.0)
	{
		FBusState S;
		S.ArcLengthCm = Route.StopArcCm.Num() > 0 ? Route.StopArcCm[0] : 0.0;
		return S;
	}
	// Positiver Modulo: ein Wagen, dessen Abfahrt noch aussteht, ist im RUECKLAUF
	// des vorigen Umlaufs unterwegs - nicht geparkt. Ohne das stuenden zu
	// Dienstbeginn alle spaeteren Wagen am Anfangspunkt uebereinander.
	double Local = FMath::Fmod(ServiceSeconds - Vehicle.PhaseSeconds, Cycle);
	if (Local < 0.0) { Local += Cycle; }
	return EvaluateRoundTrip(Local, Route, v, Dwell, Term);
}

void WiesbadenBusLine::FleetDepartures(double ServiceSeconds, const TArray<FBusVehicle>& Fleet,
	double CycleSeconds, double OffsetToStopSeconds, int32 MaxCount, TArray<double>& OutSecondsUntil)
{
	OutSecondsUntil.Reset();
	if (Fleet.Num() == 0 || CycleSeconds <= 0.0 || MaxCount <= 0)
	{
		return;
	}
	TArray<double> Cand;
	Cand.Reserve(Fleet.Num());
	for (const FBusVehicle& V : Fleet)
	{
		// Durchfahrt = Abfahrt dieses Wagens + Fahrzeit bis zur Halte, plus ganze
		// Umlaeufe, bis sie NACH der Dienstzeit liegt.
		const double First = V.PhaseSeconds + OffsetToStopSeconds;
		double Until = First - ServiceSeconds;
		if (Until < 0.0)
		{
			Until += CycleSeconds * FMath::CeilToDouble(-Until / CycleSeconds);
		}
		Cand.Add(Until);
	}
	Cand.Sort();
	const int32 K = FMath::Min(MaxCount, Cand.Num());
	for (int32 i = 0; i < K; ++i) { OutSecondsUntil.Add(Cand[i]); }
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

double WiesbadenBusLine::SecondsToStopOnLeg(const FBusRoute& Route, double v,
	double Dwell, double Term, int32 StopIndex, bool bForward)
{
	// Die Saeule auf der Gegenseite zeigt die Gegenrichtung. Deren Wagen sind
	// erst nach der Hinfahrt UND der Wendezeit dort - mit der Hinfahrtszeit
	// stuenden auf der Tafel der Gegenseite die Zeiten der falschen Richtung.
	//
	// Dieselbe Phasenfolge wie BuildPhases: Hinfahrt 0->N-1 (Verweilen an jeder
	// Zwischenhalte, am Ende die Wendezeit), dann Rueckfahrt N-1->0.
	const int32 N = Route.StopArcCm.Num();
	const double Vv = FMath::Max(v, 1.0);
	if (N < 2) { return 0.0; }
	const int32 Target = FMath::Clamp(StopIndex, 0, N - 1);
	if (bForward) { return SecondsToStop(Route, v, Dwell, Target); }

	double t = 0.0;
	for (int32 i = 0; i < N - 1; ++i)
	{
		t += (Route.StopArcCm[i + 1] - Route.StopArcCm[i]) / Vv;
		t += (i + 1 == N - 1) ? Term : Dwell;   // Wendezeit am fernen Ende
	}
	// Rueckfahrt bis zur Zielhalte: die Ankunft liegt am ENDE des Fahrsegments,
	// die Verweilzeiten davor gehoeren zu den weiter entfernten Halten.
	for (int32 i = N - 1; i > Target; --i)
	{
		t += (Route.StopArcCm[i] - Route.StopArcCm[i - 1]) / Vv;
		if (i - 1 > Target) { t += Dwell; }
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
double WiesbadenBusLine::SecondsToReturnStop(const FBusRoute& Route, double v,
	double Dwell, double Term, int32 ReturnStopIndex)
{
	if (!Route.HasReturnLeg() || Route.StopArcCm.Num() < 2)
	{
		return 0.0;
	}
	const double Vv = FMath::Max(v, 1.0);
	const TArray<double>& S = Route.StopArcCm;
	const TArray<double>& RS = Route.ReturnStopArcCm;
	const int32 N = S.Num();
	const int32 Target = FMath::Clamp(ReturnStopIndex, 0, RS.Num() - 1);
	// Dieselbe Phasenfolge wie BuildPhases: Hinfahrt samt Wendezeit ...
	double t = 0.0;
	for (int32 i = 0; i < N - 1; ++i)
	{
		t += (S[i + 1] - S[i]) / Vv;
		t += (i + 1 == N - 1) ? Term : Dwell;
	}
	// ... Anschluss zur ersten Rueckweg-Halte, dann Halt fuer Halt.
	t += RS[0] / Vv;
	for (int32 j = 0; j < Target; ++j)
	{
		t += Dwell;
		t += (RS[j + 1] - RS[j]) / Vv;
	}
	return t;
}

int32 WiesbadenBusLine::NextStopIndex(double ArcLengthCm, bool bForward,
	const TArray<double>& StopArcCm)
{
	const int32 Num = StopArcCm.Num();
	if (Num < 2) { return -1; }
	// An/kurz nach einer Halte gilt die FOLGENDE Halte in Fahrtrichtung als naechste.
	constexpr double Eps = 50.0;   // cm
	if (bForward)
	{
		for (int32 i = 0; i < Num; ++i)
		{
			if (StopArcCm[i] > ArcLengthCm + Eps) { return i; }
		}
		return Num - 1;   // am/hinter dem Terminus: Endhalte
	}
	for (int32 i = Num - 1; i >= 0; --i)
	{
		if (StopArcCm[i] < ArcLengthCm - Eps) { return i; }
	}
	return 0;
}
