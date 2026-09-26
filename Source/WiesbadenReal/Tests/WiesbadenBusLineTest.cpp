// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "World/WiesbadenBusLine.h"

namespace
{
	WiesbadenBusLine::FBusRoute MakeRoute()
	{
		WiesbadenBusLine::FBusRoute R;
		R.StopArcCm = { 0.0, 1000.0, 3000.0 };
		R.TotalLengthCm = 3000.0;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenBusLineTest,
	"WiesbadenReal.Traffic.BusLine",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWiesbadenBusLineTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenBusLine;
	const FBusRoute R = MakeRoute();
	const double v = 500.0, dwell = 2.0, term = 4.0;
	// Phasen: Hin seg0(2s) dwell1(2s) seg1(4s) termA(4s), Rueck seg(4s) dwell1(2s) seg(2s) termB(4s) = 24 s.

	TestTrue(TEXT("Rundfahrtdauer 24 s"),
		FMath::IsNearlyEqual(RoundTripSeconds(R, v, dwell, term), 24.0, 0.01));

	FBusState s0 = EvaluateRoundTrip(0.0, R, v, dwell, term);
	TestTrue(TEXT("t=0 an Halte 0, vorwaerts, faehrt"),
		FMath::IsNearlyEqual(s0.ArcLengthCm, 0.0, 1.0) && s0.bForward && !s0.bDwelling);

	TestTrue(TEXT("t=1 Mitte Segment 0 (arc 500)"),
		FMath::IsNearlyEqual(EvaluateRoundTrip(1.0, R, v, dwell, term).ArcLengthCm, 500.0, 1.0));

	FBusState s3 = EvaluateRoundTrip(3.0, R, v, dwell, term);
	TestTrue(TEXT("t=3 verweilt an Halte 1"),
		FMath::IsNearlyEqual(s3.ArcLengthCm, 1000.0, 1.0) && s3.bDwelling);

	// -- Durchfahrtszeit je Richtung (zwei Saeulen an jeder Halte) -----------
	// Die Tafel auf der Gegenseite darf NICHT die Hinfahrtszeit nennen: deren
	// Wagen sind erst nach Hinfahrt + Wendezeit + Rueckfahrt dort.
	// Phasen (v=500, dwell=2, term=4, Halte 0/1000/3000 cm):
	//   Hin  seg(2) dwell(2) seg(4) Wende(4)  -> Halte 1 nach 2 s, Halte 2 nach 8 s
	//   Rueck seg(4) dwell(2) seg(2) Wende(4) -> Halte 1 nach 16 s, Halte 0 nach 20 s
	TestTrue(TEXT("Hinfahrt: Halte 1 nach 2 s, Endhalte nach 8 s"),
		FMath::IsNearlyEqual(SecondsToStopOnLeg(R, v, dwell, term, 1, true), 2.0, 0.01)
		&& FMath::IsNearlyEqual(SecondsToStopOnLeg(R, v, dwell, term, 2, true), 8.0, 0.01));
	TestTrue(TEXT("Gegenrichtung: Halte 1 nach 16 s, Halte 0 nach 20 s"),
		FMath::IsNearlyEqual(SecondsToStopOnLeg(R, v, dwell, term, 1, false), 16.0, 0.01)
		&& FMath::IsNearlyEqual(SecondsToStopOnLeg(R, v, dwell, term, 0, false), 20.0, 0.01));
	TestTrue(TEXT("Gegenrichtung an der Endhalte: Wendezeit beginnt nach der Hinfahrt (12 s)"),
		FMath::IsNearlyEqual(SecondsToStopOnLeg(R, v, dwell, term, 2, false), 12.0, 0.01));
	TestTrue(TEXT("die beiden Tafeln einer Halte nennen verschiedene Zeiten"),
		!FMath::IsNearlyEqual(SecondsToStopOnLeg(R, v, dwell, term, 1, true),
			SecondsToStopOnLeg(R, v, dwell, term, 1, false), 1.0));
	TestTrue(TEXT("Hinfahrt bleibt die alte Rechnung"),
		FMath::IsNearlyEqual(SecondsToStopOnLeg(R, v, dwell, term, 1, true),
			SecondsToStop(R, v, dwell, 1), 0.01));

	TestTrue(TEXT("t=6 Mitte Segment 1 (arc 2000)"),
		FMath::IsNearlyEqual(EvaluateRoundTrip(6.0, R, v, dwell, term).ArcLengthCm, 2000.0, 1.0));

	FBusState s10 = EvaluateRoundTrip(10.0, R, v, dwell, term);
	TestTrue(TEXT("t=10 verweilt am Terminus"),
		FMath::IsNearlyEqual(s10.ArcLengthCm, 3000.0, 1.0) && s10.bDwelling && s10.bForward);

	FBusState s14 = EvaluateRoundTrip(14.0, R, v, dwell, term);
	TestTrue(TEXT("t=14 Rueckfahrt (arc 2000, rueckwaerts)"),
		FMath::IsNearlyEqual(s14.ArcLengthCm, 2000.0, 1.0) && !s14.bForward);

	FBusState a = EvaluateRoundTrip(5.0, R, v, dwell, term);
	FBusState b = EvaluateRoundTrip(29.0, R, v, dwell, term);
	TestTrue(TEXT("periodisch: t und t+24 identisch"),
		FMath::IsNearlyEqual(a.ArcLengthCm, b.ArcLengthCm, 0.01) && a.bForward == b.bForward);

	bool bInBounds = true;
	for (int32 i = 0; i <= 96; ++i)
	{
		const double arc = EvaluateRoundTrip(i * 0.5, R, v, dwell, term).ArcLengthCm;
		bInBounds = bInBounds && arc >= -0.5 && arc <= 3000.5;
	}
	TestTrue(TEXT("Bogenlaenge stets in [0, Gesamtlaenge]"), bInBounds);

	// --- Echter Fahrplan: ActiveRuns ---
	{
		FBusSchedule Sched;
		Sched.DepartureSeconds = { 0.0, 100.0, 200.0 };
		Sched.DaySeconds = 1000.0;
		const double RT = 120.0;   // Rundfahrtdauer
		TArray<FBusRun> Runs;

		ActiveRuns(250.0, Sched, RT, Runs);
		TestEqual(TEXT("t=250: genau 1 Kurs unterwegs"), Runs.Num(), 1);
		if (Runs.Num() == 1)
		{
			TestTrue(TEXT("t=250: Kurs 'Abfahrt 200' seit 50 s, Index 2"),
				FMath::IsNearlyEqual(Runs[0].Elapsed, 50.0, 0.01) && Runs[0].Index == 2);
		}

		ActiveRuns(210.0, Sched, RT, Runs);
		TestEqual(TEXT("t=210: 2 Kurse gleichzeitig (dichter Takt)"), Runs.Num(), 2);

		ActiveRuns(700.0, Sched, RT, Runs);
		TestEqual(TEXT("t=700: Betriebspause -> kein Kurs"), Runs.Num(), 0);

		// Mitternacht: Abfahrt kurz vor Tagesende ist im Folgetag noch unterwegs.
		FBusSchedule Night;
		Night.DepartureSeconds = { 950.0 };
		Night.DaySeconds = 1000.0;
		ActiveRuns(1010.0, Night, RT, Runs);
		TestEqual(TEXT("Mitternacht: 1 Kurs vom Vortag noch unterwegs"), Runs.Num(), 1);
		if (Runs.Num() == 1)
		{
			TestTrue(TEXT("Mitternacht: seit 60 s unterwegs"),
				FMath::IsNearlyEqual(Runs[0].Elapsed, 60.0, 0.01));
		}

		FBusSchedule Empty;
		ActiveRuns(100.0, Empty, RT, Runs);
		TestEqual(TEXT("leerer Fahrplan -> kein Kurs"), Runs.Num(), 0);
	}

	// --- Haltebucht: BayFactor ---
	{
		const TArray<double> Stops = { 0.0, 1000.0, 3000.0 };
		const double Zone = 500.0;
		TestTrue(TEXT("Bay: an der Halte -> 1"),
			FMath::IsNearlyEqual(BayFactor(0.0, false, Stops, Zone), 1.0, 0.001));
		TestTrue(TEXT("Bay: Verweilen -> 1 (egal wo)"),
			FMath::IsNearlyEqual(BayFactor(500.0, true, Stops, Zone), 1.0, 0.001));
		TestTrue(TEXT("Bay: halbe Zone -> 0.5 (smoothstep)"),
			FMath::IsNearlyEqual(BayFactor(250.0, false, Stops, Zone), 0.5, 0.001));
		TestTrue(TEXT("Bay: ab Zone -> 0"),
			FMath::IsNearlyEqual(BayFactor(500.0, false, Stops, Zone), 0.0, 0.001));
		TestTrue(TEXT("Bay: monoton fallend mit Abstand"),
			BayFactor(100.0, false, Stops, Zone) > BayFactor(300.0, false, Stops, Zone));
		TestTrue(TEXT("Bay: kurz vor Halte 1000 wieder hoch"),
			BayFactor(950.0, false, Stops, Zone) > 0.5);
	}

	// --- Abfahrtsmonitor: SecondsToStop + NextDepartures ---
	{
		// Route {0,1000,3000}, v=500, dwell=2.
		TestTrue(TEXT("SecondsToStop(0) = 0"),
			FMath::IsNearlyEqual(SecondsToStop(R, v, dwell, 0), 0.0, 0.01));
		TestTrue(TEXT("SecondsToStop(1) = 2 (Fahrt 0->1)"),
			FMath::IsNearlyEqual(SecondsToStop(R, v, dwell, 1), 2.0, 0.01));
		TestTrue(TEXT("SecondsToStop(2) = 8 (2 + Halt + 4)"),
			FMath::IsNearlyEqual(SecondsToStop(R, v, dwell, 2), 8.0, 0.01));

		FBusSchedule Sched; Sched.DepartureSeconds = { 0.0, 600.0, 1200.0 }; Sched.DaySeconds = 86400.0;
		TArray<double> Until;
		NextDepartures(100.0, Sched, 0.0, 2, Until);
		TestEqual(TEXT("2 naechste Abfahrten am Terminus"), Until.Num(), 2);
		if (Until.Num() == 2)
		{
			TestTrue(TEXT("naechste in 500 s"), FMath::IsNearlyEqual(Until[0], 500.0, 0.01));
			TestTrue(TEXT("uebernaechste in 1100 s"), FMath::IsNearlyEqual(Until[1], 1100.0, 0.01));
		}
		NextDepartures(20.0, Sched, 50.0, 1, Until);
		TestTrue(TEXT("Zwischenhalte (Offset 50): Durchfahrt in 30 s"),
			Until.Num() == 1 && FMath::IsNearlyEqual(Until[0], 30.0, 0.01));
		NextDepartures(0.0, Sched, 0.0, 3, Until);
		TestTrue(TEXT("Restzeiten aufsteigend sortiert"),
			Until.Num() == 3 && Until[0] <= Until[1] && Until[1] <= Until[2]);
	}

	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenBusFleetTest,
	"WiesbadenReal.Traffic.BusFleet",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Dauerbetrieb: feste Wagen-IDs, Umlauf mit 10 Minuten Wendezeit an BEIDEN
 * Endpunkten, und der Fahrplan der Haltestellenanzeige kommt aus derselben
 * Flotte.
 *
 * Der Fehler, den der Test ausschliesst: dass ein Wagen seine Nummer wechselt
 * (der Bus an der Halte ist spaeter ein anderer), dass die Wendezeit nur an
 * EINEM Ende eingehalten wird, oder dass zu Dienstbeginn mehrere Wagen auf
 * derselben Position stehen (negative Zeiten wurden frueher auf 0 geklemmt -
 * damit standen alle noch nicht abgefahrenen Wagen am Anfangspunkt
 * uebereinander).
 */
bool FWiesbadenBusFleetTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenBusLine;

	// 2000 m je Richtung, 1000 cm/s -> 2 s Fahrt je Richtung. Mit 600 s Wendezeit
	// an beiden Enden dauert der Umlauf 2*2 + 2*600 = 1204 s.
	FBusRoute Route;
	Route.StopArcCm = { 0.0, 2000.0 };
	Route.TotalLengthCm = 2000.0;
	const double V = 1000.0, Dwell = 0.0, Term = 600.0;
	const double Cycle = RoundTripSeconds(Route, V, Dwell, Term);
	TestTrue(FString::Printf(TEXT("Umlauf %.0f s = 2*2 s Fahrt + 2*600 s Wende"), Cycle),
		FMath::IsNearlyEqual(Cycle, 1204.0, 0.01));

	// -- Wendezeit an BEIDEN Enden ------------------------------------------
	{
		// t = 2 s: gerade am Terminus angekommen -> Wendezeit laeuft (noch in
		// Hinrichtung - das ist die Ankunft, nicht schon die Rueckfahrt).
		const FBusState Arrived = EvaluateRoundTrip(2.0, Route, V, Dwell, Term);
		TestTrue(TEXT("am Terminus: verweilt"),
			Arrived.bDwelling && Arrived.bForward
			&& FMath::IsNearlyEqual(Arrived.ArcLengthCm, 2000.0, 0.5));

		// Die Wendezeit dauert die vollen 600 s: eine Sekunde vor Ablauf steht der
		// Wagen noch, kurz danach faehrt er zurueck.
		TestTrue(TEXT("1 s vor Ende der Wendezeit: steht noch"),
			EvaluateRoundTrip(601.0, Route, V, Dwell, Term).bDwelling);
		const FBusState EndOfLayover = EvaluateRoundTrip(602.5, Route, V, Dwell, Term);
		TestTrue(TEXT("nach 10 min Wendezeit: faehrt zurueck"),
			!EndOfLayover.bDwelling && !EndOfLayover.bForward);

		// Rueckfahrt: bei 602 + 2 = 604 s wieder am Anfangspunkt - dort laeuft die
		// zweite Wendezeit, sonst waere der Umlauf 600 s zu kurz.
		const FBusState BackHome = EvaluateRoundTrip(604.0, Route, V, Dwell, Term);
		TestTrue(TEXT("am Anfangspunkt: verweilt (zweite Wendezeit)"), BackHome.bDwelling);
		TestTrue(FString::Printf(TEXT("Anfangspunkt erreicht (%.0f cm)"), BackHome.ArcLengthCm),
			FMath::IsNearlyEqual(BackHome.ArcLengthCm, 0.0, 0.5));

		// Die RESTZEIT der Wendezeit ist die Zahl, die im Log steht ("VERWEILT
		// noch 600 s"): an beiden Enden die volle Wendezeit, und sie zaehlt ab.
		// Ohne diese Groesse war am fernen Endpunkt nur zu SEHEN, dass ein Wagen
		// dort steht - nicht, wie lange.
		TestTrue(FString::Printf(TEXT("Restzeit am fernen Endpunkt %.0f s"),
			Arrived.DwellRemainingSeconds),
			FMath::IsNearlyEqual(Arrived.DwellRemainingSeconds, 600.0, 0.01));
		TestTrue(TEXT("Restzeit zaehlt ab"),
			FMath::IsNearlyEqual(EvaluateRoundTrip(601.0, Route, V, Dwell, Term).DwellRemainingSeconds,
				1.0, 0.01));
		TestTrue(FString::Printf(TEXT("Restzeit am Start-Endpunkt %.0f s"),
			BackHome.DwellRemainingSeconds),
			FMath::IsNearlyEqual(BackHome.DwellRemainingSeconds, 600.0, 0.01));
		// Die GESAMTdauer der Verweilphase steht ebenfalls im Log. Sie macht die
		// volle Wendezeit auch an einer Probe mitten in der Wendezeit ablesbar.
		TestTrue(TEXT("Wendezeit am fernen Endpunkt 600 s"),
			FMath::IsNearlyEqual(Arrived.DwellTotalSeconds, 600.0, 0.01));
		TestTrue(TEXT("Wendezeit am Start-Endpunkt 600 s"),
			FMath::IsNearlyEqual(BackHome.DwellTotalSeconds, 600.0, 0.01));
	}

	// -- Flotte --------------------------------------------------------------
	// Fuer die Positionspruefungen eine REALISTISCHE Strecke: 4 km je Richtung
	// (12 m/s = 43 km/h), neun Halte alle 500 m. Auf der winzigen Teststrecke oben
	// bestehen 4 s Fahrt gegen 1200 s Stehen - dort stehen zwangslaeufig mehrere
	// Wagen am selben Endpunkt, und Aussagen ueber Positionen waeren Unsinn.
	FBusRoute Long;
	Long.TotalLengthCm = 400000.0;
	for (int32 i = 0; i <= 8; ++i) { Long.StopArcCm.Add(i * 50000.0); }
	const double LV = 1200.0, LStop = 8.0, LTerm = 600.0;
	const double LongCycle = RoundTripSeconds(Long, LV, LStop, LTerm);

	FServiceConfig Cfg;
	Cfg.CruiseSpeedCmS = LV;
	Cfg.StopDwellSeconds = LStop;
	Cfg.TerminusDwellSeconds = LTerm;
	Cfg.HeadwaySeconds = 1200.0;   // 20-Minuten-Takt wie Linie 6
	Cfg.MaxBuses = 12;

	double FleetCycle = 0.0;
	TArray<FBusVehicle> Fleet;
	BuildFleet(Long, Cfg, FleetCycle, Fleet);

	TestTrue(FString::Printf(TEXT("Umlaufdauer %.0f s kommt aus der Flottenrechnung"), FleetCycle),
		FMath::IsNearlyEqual(FleetCycle, LongCycle, 0.01));
	const int32 ExpectedCount = FMath::Clamp(
		(int32)FMath::CeilToDouble(LongCycle / Cfg.HeadwaySeconds), 2, Cfg.MaxBuses);
	TestEqual(FString::Printf(TEXT("Wagenzahl = aufgerundet Umlauf/Takt = %d"), ExpectedCount),
		Fleet.Num(), ExpectedCount);
	for (int32 k = 0; k < Fleet.Num(); ++k)
	{
		TestEqual(FString::Printf(TEXT("Wagen %d hat die feste Nummer %d"), k, k + 1), Fleet[k].Id, k + 1);
	}

	// Nummernkreis der Linie: Linie 6 -> 601, Linie 3 -> 301. Ohne das Vorzeichen
	// fuhren zwei Busse VERSCHIEDENER Linien beide als "Wagen 2" - im gemeinsamen
	// Log und in der Mitfahr-Diagnose nicht auseinanderzuhalten.
	{
		FServiceConfig Line6 = Cfg; Line6.FirstVehicleId = 600 + 1;
		FServiceConfig Line3 = Cfg; Line3.FirstVehicleId = 300 + 1;
		Line6.HeadwaySeconds = Line3.HeadwaySeconds = 1200.0;
		FServiceConfig Small6 = Line6; Small6.MaxBuses = 4;
		FServiceConfig Small3 = Line3; Small3.MaxBuses = 4;
		double C6 = 0.0, C3 = 0.0;
		TArray<FBusVehicle> F6, F3;
		BuildFleet(Long, Small6, C6, F6);
		BuildFleet(Long, Small3, C3, F3);
		TestEqual(TEXT("Linie 6 faengt bei Wagen 601 an"), F6[0].Id, 601);
		TestEqual(TEXT("Linie 3 faengt bei Wagen 301 an"), F3[0].Id, 301);
		TestEqual(TEXT("Linie 6 zaehlt fortlaufend weiter"), F6[1].Id, 602);
		TSet<int32> Ids;
		for (const FBusVehicle& L6 : F6) { Ids.Add(L6.Id); }
		for (const FBusVehicle& L3 : F3) { Ids.Add(L3.Id); }
		TestEqual(TEXT("keine Nummer doppelt ueber beide Linien"), Ids.Num(), F6.Num() + F3.Num());
	}
	TestTrue(TEXT("Wagen starten gleichmaessig verteilt (Umlauf/Anzahl)"),
		Fleet.Num() >= 2 && FMath::IsNearlyEqual(
			Fleet[1].PhaseSeconds - Fleet[0].PhaseSeconds, LongCycle / (double)Fleet.Num(), 0.01));

	// Takt wird eingehalten: der Abstand ist nie groesser als der Wunschtakt.
	{
		double MaxGap = 0.0;
		for (int32 k = 0; k < Fleet.Num(); ++k)
		{
			const double Next = (k + 1 < Fleet.Num()) ? Fleet[k + 1].PhaseSeconds
				: Fleet[0].PhaseSeconds + LongCycle;
			MaxGap = FMath::Max(MaxGap, Next - Fleet[k].PhaseSeconds);
		}
		TestTrue(FString::Printf(TEXT("groesster Abstand %.0f s <= Takt %.0f s"), MaxGap, Cfg.HeadwaySeconds),
			MaxGap <= Cfg.HeadwaySeconds + 0.01);
	}

	// Ausbauen: mehr Wagen als der Pool erlaubt ist nicht moeglich.
	{
		FServiceConfig Dense = Cfg;
		Dense.HeadwaySeconds = 10.0;   // verlangt weit mehr als der Pool fasst
		double DenseCycle = 0.0;
		TArray<FBusVehicle> DenseFleet;
		BuildFleet(Long, Dense, DenseCycle, DenseFleet);
		TestEqual(TEXT("Poolgrenze 12 wird eingehalten"), DenseFleet.Num(), 12);
	}

	// -- Positionen ----------------------------------------------------------
	{
		const double Spacing = LongCycle / (double)Fleet.Num();
		bool bInRange = true;
		bool bSameDirectionCrowded = false;
		for (double t = 0.0; t < LongCycle * 2.0; t += Spacing / 8.0)
		{
			TArray<FBusState> States;
			for (const FBusVehicle& Vehicle : Fleet)
			{
				const FBusState S = FleetStateAt(t, Vehicle, Long, LV, LStop, LTerm);
				bInRange = bInRange && S.ArcLengthCm >= -0.5 && S.ArcLengthCm <= 400000.5;
				States.Add(S);
			}
			for (int32 a = 0; a < States.Num(); ++a)
			{
				for (int32 b = a + 1; b < States.Num(); ++b)
				{
					// Zwei FAHRENDE Wagen gleicher Richtung duerfen nicht auf derselben
					// Stelle sein. Stehende (Wendezeit) duerfen es: an einem Endpunkt
					// stehen sie hintereinander, und auf der Teststrecke ueberlappt die
					// Wendezeit absichtlich mit dem Takt.
					if (States[a].bForward == States[b].bForward
						&& !States[a].bDwelling && !States[b].bDwelling
						&& FMath::Abs(States[a].ArcLengthCm - States[b].ArcLengthCm) < 1.0)
					{
						bSameDirectionCrowded = true;
					}
				}
			}
		}
		TestTrue(TEXT("alle Wagen bleiben auf der Strecke"), bInRange);
		TestTrue(TEXT("keine zwei FAHRENDEN Wagen gleicher Richtung auf derselben Stelle"),
			!bSameDirectionCrowded);
	}

	// Ein Wagen, dessen Abfahrt noch aussteht, ist im VORIGEN Umlauf unterwegs -
	// nicht am Anfangspunkt geparkt. Ohne das stuenden zu Dienstbeginn alle
	// spaeteren Wagen uebereinander (die frueher verwendete Zeitrechnung klemmte
	// negative Zeiten auf 0).
	{
		FBusVehicle Later;
		Later.Id = 2;

		// Eine Phase suchen, bei der der Wagen 100 s nach Dienstbeginn im RUECKLAUF
		// faehrt (Zustand nachrechnen statt Phase raten: der Umlauf besteht aus
		// Fahr- und Verweilabschnitten, eine feste Zahl waere bei jeder Aenderung an
		// Takten/Halten falsch).
		double FoundPhase = -1.0;
		FBusState S;
		for (double Phase = 1.0; Phase < LongCycle; Phase += 1.0)
		{
			Later.PhaseSeconds = Phase;
			const FBusState Try = FleetStateAt(100.0, Later, Long, LV, LStop, LTerm);
			if (!Try.bForward && !Try.bDwelling && Try.ArcLengthCm > 0.0)
			{
				FoundPhase = Phase;
				S = Try;
				break;
			}
		}
		TestTrue(FString::Printf(TEXT("Wagen vor seiner Abfahrt im Ruecklauf gefunden (Phase %.0f s, Bogen %.0f)"),
			FoundPhase, S.ArcLengthCm), FoundPhase > 0.0);
		TestTrue(FString::Printf(TEXT("Phase %.0f s liegt vor der Abfahrt 100 s"), FoundPhase),
			FoundPhase > 100.0);

		// Durchgehender Betrieb: ein Umlauf spaeter steht derselbe Wagen genau dort
		// wieder - es gibt kein Tagesende, an dem die Flotte neu aufgebaut wird.
		if (FoundPhase > 0.0)
		{
			Later.PhaseSeconds = FoundPhase;
			const FBusState Next = FleetStateAt(100.0 + LongCycle, Later, Long, LV, LStop, LTerm);
			TestTrue(TEXT("Zustand wiederholt sich je Umlauf"),
				FMath::IsNearlyEqual(S.ArcLengthCm, Next.ArcLengthCm, 0.01)
				&& S.bForward == Next.bForward && S.bDwelling == Next.bDwelling);
		}
	}

	// -- Haltestellenanzeige aus derselben Flotte ----------------------------
	{
		TArray<double> Until;
		FleetDepartures(0.0, Fleet, LongCycle, 0.0, 4, Until);
		TestEqual(TEXT("je Wagen eine naechste Durchfahrt"), Until.Num(), Fleet.Num());
		bool bAscending = true;
		bool bWithin = true;
		for (int32 i = 0; i < Until.Num(); ++i)
		{
			bWithin = bWithin && Until[i] >= 0.0 && Until[i] <= LongCycle;
			if (i > 0) { bAscending = bAscending && Until[i] >= Until[i - 1]; }
		}
		TestTrue(TEXT("Restzeiten aufsteigend"), bAscending);
		TestTrue(TEXT("Restzeiten innerhalb eines Umlaufs"), bWithin);
		if (Until.Num() > 0)
		{
			TestTrue(FString::Printf(TEXT("erste Durchfahrt sofort (%.1f s)"), Until[0]),
				FMath::IsNearlyEqual(Until[0], 0.0, 0.5));
		}

		// Nach einer halben Stunde kommt dieselbe Folge wieder - der Betrieb laeuft
		// durch, ohne Ausduennen am Tagesende.
		TArray<double> Later50min;
		FleetDepartures(3000.0, Fleet, LongCycle, 0.0, 4, Later50min);
		TestEqual(TEXT("auch nach 50 Minuten dieselbe Anzahl Durchfahrten"),
			Later50min.Num(), Fleet.Num());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenBusNextStopTest,
	"WiesbadenReal.Traffic.BusNextStop",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// Naechste anzusagende Halte (Halteansagen). Halte 100 m auseinander, damit der
// 50-cm-Eps (nur um "an einer Halte -> naechste ansagen" abzudecken) vernachlaessigbar ist.
bool FWiesbadenBusNextStopTest::RunTest(const FString& Parameters)
{
	using WiesbadenBusLine::NextStopIndex;
	const TArray<double> Stops = { 0.0, 10000.0, 20000.0, 30000.0 };   // 4 Halte, 100 m Abstand

	// Hinfahrt: an/zwischen Halten immer die naechste voraus.
	TestEqual(TEXT("hin, an Halt 0 -> 1"), NextStopIndex(0.0, true, Stops), 1);
	TestEqual(TEXT("hin, zwischen 0 und 1 -> 1"), NextStopIndex(5000.0, true, Stops), 1);
	TestEqual(TEXT("hin, an Halt 1 -> 2"), NextStopIndex(10000.0, true, Stops), 2);
	TestEqual(TEXT("hin, kurz vor 3 -> 3"), NextStopIndex(29000.0, true, Stops), 3);
	TestEqual(TEXT("hin, am Terminus -> Endhalte"), NextStopIndex(30000.0, true, Stops), 3);

	// Rueckfahrt: die naechste ist die Halte mit kleinerem Bogen.
	TestEqual(TEXT("rueck, am Terminus 3 -> 2"), NextStopIndex(30000.0, false, Stops), 2);
	TestEqual(TEXT("rueck, zwischen 2 und 1 -> 1"), NextStopIndex(15000.0, false, Stops), 1);
	TestEqual(TEXT("rueck, an Halt 0 -> 0"), NextStopIndex(0.0, false, Stops), 0);

	// Entartet: < 2 Halte -> keine Ansage.
	const TArray<double> One = { 5000.0 };
	TestEqual(TEXT("eine Halte -> -1"), NextStopIndex(0.0, true, One), -1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenBusReturnLegTest,
	"WiesbadenReal.Traffic.BusLine.Rueckweg",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWiesbadenBusReturnLegTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenBusLine;
	// Eigener Rueckweg (Gegenrichtungs-Relation): Halte 500/2000/3500 cm auf
	// einer 4000 cm langen eigenen Linie; die letzte ist der Ausstieg (Pause),
	// das Ende der Linie liegt an der Einstiegshaltestelle = Halt 0 des Hinwegs.
	FBusRoute R = MakeRoute();
	R.ReturnStopArcCm = { 500.0, 2000.0, 3500.0 };
	R.ReturnLengthCm = 4000.0;
	TestTrue(TEXT("Rueckweg erkannt"), R.HasReturnLeg());
	const double v = 500.0, dwell = 2.0, term = 4.0;
	// Hin: seg(2) dwell(2) seg(4) Wende(4) = 12 s
	// Rueck: Anschluss(1) Einstieg(2) seg(3) dwell(2) seg(3) AUSSTIEG+Pause(4)
	//        Leerfahrt zur Einstiegshaltestelle(1) Einsteigen(2) = 18 s
	TestTrue(TEXT("Umlauf 30 s"), FMath::IsNearlyEqual(RoundTripSeconds(R, v, dwell, term), 30.0, 0.01));

	const FBusState Anschluss = EvaluateRoundTrip(12.5, R, v, dwell, term);
	TestTrue(TEXT("nach der Wende auf der Rueckweg-Linie, in IHRER Richtung aufsteigend"),
		Anschluss.bReturnPath && !Anschluss.bForward && !Anschluss.bDwelling
		&& FMath::IsNearlyEqual(Anschluss.ArcLengthCm, 250.0, 1.0));
	const FBusState Einstieg = EvaluateRoundTrip(14.0, R, v, dwell, term);
	TestTrue(TEXT("haelt an der ersten Rueckweg-Halte"),
		Einstieg.bReturnPath && Einstieg.bDwelling && FMath::IsNearlyEqual(Einstieg.ArcLengthCm, 500.0, 1.0));
	const FBusState Pause = EvaluateRoundTrip(25.0, R, v, dwell, term);
	TestTrue(TEXT("am Ausstieg die volle Wendezeit als Pause"),
		Pause.bReturnPath && Pause.bDwelling && FMath::IsNearlyEqual(Pause.ArcLengthCm, 3500.0, 1.0)
		&& FMath::IsNearlyEqual(Pause.DwellTotalSeconds, term, 0.01));
	const FBusState Leer = EvaluateRoundTrip(27.5, R, v, dwell, term);
	TestTrue(TEXT("danach Leerfahrt weiter auf der Rueckweg-Linie"),
		Leer.bReturnPath && !Leer.bDwelling && FMath::IsNearlyEqual(Leer.ArcLengthCm, 3750.0, 1.0));
	const FBusState Abfahrt = EvaluateRoundTrip(29.0, R, v, dwell, term);
	TestTrue(TEXT("am Ende steht er an Halt 0 des Hinwegs (Einstieg)"),
		!Abfahrt.bReturnPath && Abfahrt.bForward && Abfahrt.bDwelling
		&& FMath::IsNearlyEqual(Abfahrt.ArcLengthCm, 0.0, 1.0));
	const FBusState Neu = EvaluateRoundTrip(31.0, R, v, dwell, term);
	TestTrue(TEXT("neuer Umlauf faehrt den Hinweg"),
		!Neu.bReturnPath && Neu.bForward && FMath::IsNearlyEqual(Neu.ArcLengthCm, 500.0, 1.0));

	// Die Tafel der Gegenrichtung muss dieselben Zeiten nennen, die der Bus faehrt.
	const double Expected[3] = { 13.0, 18.0, 23.0 };
	for (int32 j = 0; j < 3; ++j)
	{
		const double T = SecondsToReturnStop(R, v, dwell, term, j);
		TestTrue(FString::Printf(TEXT("Rueckweg-Halte %d nach %.0f s"), j, Expected[j]),
			FMath::IsNearlyEqual(T, Expected[j], 0.01));
		const FBusState At = EvaluateRoundTrip(T + 0.01, R, v, dwell, term);
		TestTrue(FString::Printf(TEXT("Rueckweg-Halte %d: dort steht der Bus dann wirklich"), j),
			At.bReturnPath && At.bDwelling && FMath::IsNearlyEqual(At.ArcLengthCm, R.ReturnStopArcCm[j], 1.0));
	}
	FBusRoute Ohne = MakeRoute();
	TestTrue(TEXT("ohne Rueckweg: 0 und alter Umlauf (24 s)"),
		SecondsToReturnStop(Ohne, v, dwell, term, 1) == 0.0
		&& FMath::IsNearlyEqual(RoundTripSeconds(Ohne, v, dwell, term), 24.0, 0.01));
	return true;
}
