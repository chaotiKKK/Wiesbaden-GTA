// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Datenreine Bewegung eines Linienbusses entlang einer Halte-Polylinie.
 *
 * Deterministisch aus verstrichener Zeit (keine Random, kein Welt-Zugriff) und
 * damit unit-testbar. Baut auf demselben Muster wie WiesbadenRailTransport auf
 * (Bogenlaenge + SamplePolyline uebernimmt der Actor); HIER liegt nur die
 * Zeit->Bogenlaenge-Logik mit Verweilen an jeder Halte und Wende am Terminus.
 * Eine Zustaendigkeit: WO ist ein Bus zum Zeitpunkt t.
 */
namespace WiesbadenBusLine
{
	/** Route als Halte-Bogenlaengen (cm, aufsteigend) + Gesamtlaenge der Polylinie. */
	struct FBusRoute
	{
		TArray<double> StopArcCm;
		double TotalLengthCm = 0.0;
	};

	/** Momentaner Fahrzustand eines Busses. */
	struct FBusState
	{
		double ArcLengthCm = 0.0;   // Position entlang der Polylinie
		bool bForward = true;       // Hinrichtung true, Rueckrichtung false
		bool bDwelling = false;     // haelt gerade an einer Halte
		/** Restsekunden der laufenden Verweilphase (0, wenn keine laeuft). An
		 *  der Endhalte ist das die Wendezeit - nur so ist im Log nachweisbar,
		 *  dass dort wirklich 600 s ablaufen und nicht nur ein paar Sekunden. */
		double DwellRemainingSeconds = 0.0;
		/** Gesamtdauer der laufenden Verweilphase (0, wenn keine laeuft).
		 *  Zusammen mit der Restzeit belegt sie die VOLLE Wendezeit auch dann,
		 *  wenn die Probe mitten in der Wendezeit liegt: "noch 480 s von 600 s". */
		double DwellTotalSeconds = 0.0;
	};

	/** Dauer einer vollstaendigen Rundfahrt (Hin + Rueck inkl. aller Verweilzeiten). */
	WIESBADENREAL_API double RoundTripSeconds(const FBusRoute& Route,
		double CruiseSpeedCmS, double StopDwellSeconds, double TerminusDwellSeconds);

	/**
	 * Zustand eines Busses, der bei t=0 an der ersten Halte abfaehrt, nach
	 * ElapsedSeconds: Halt fuer Halt zur Endhalte (Verweilen an jeder), Wende am
	 * Terminus, zurueck, dann Schleife. ElapsedSeconds < 0 wird als 0 behandelt.
	 * Degeneriert (< 2 Halte oder Speed <= 0) -> Position an der ersten Halte.
	 */
	WIESBADENREAL_API FBusState EvaluateRoundTrip(double ElapsedSeconds,
		const FBusRoute& Route, double CruiseSpeedCmS,
		double StopDwellSeconds, double TerminusDwellSeconds);

	/**
	 * Haltebucht-Faktor 0..1: wie weit der Bus an der Halte nach rechts ausschert.
	 * 1 beim Verweilen bzw. direkt an einer Halte, 0 in der Segmentmitte, smoothstep
	 * ueber BayZoneCm dazwischen (weiches Ein-/Ausscheren). Datenrein aus Bogenlaenge
	 * + Haltenliste; keine Wende-/Terminuslogik noetig, die Halten stehen ja fest.
	 */
	WIESBADENREAL_API double BayFactor(double ArcLengthCm, bool bDwelling,
		const TArray<double>& StopArcCm, double BayZoneCm);

	/**
	 * Echter Fahrplan: Abfahrtszeiten am Terminus (Sekunden seit 00:00 Uhr,
	 * aufsteigend) und Tageslaenge zum Umlaufen. Statt gleichverteilter Offsets
	 * faehrt jeder Bus einen konkreten Kurs, der zur Fahrplanminute abfaehrt.
	 *
	 * ACHTUNG: seit dem Dauerbetrieb (17.09.2026) ist das nur noch der zweite
	 * Modus. Standard ist die Flotte (FBusVehicle) - feste Wagen, die ihren Umlauf
	 * ununterbrochen fahren und an BEIDEN Endpunkten ihre Wendezeit abwarten.
	 */
	struct FBusSchedule
	{
		TArray<double> DepartureSeconds;   // Abfahrten ab Terminus, Sek. seit 00:00
		double DaySeconds = 86400.0;       // Tageslaenge (Fahrplan wiederholt sich taeglich)
	};

	/**
	 * Ein festes Fahrzeug im Dauerbetrieb.
	 *
	 * `Id` ist die Wagen-Nummer: sie bleibt dem Fahrzeug fuer die ganze Dienstzeit
	 * - der Bus, den man an der Halte stehen sieht, ist spaeter an der anderen
	 * Halte derselbe (und im Mitfahrbetrieb heisst er genauso). Die frueher
	 * verwendete Zuordnung "Kurs-Index modulo Poolgroesse" tauschte Wagen
	 * gegeneinander aus, sobald ein Kurs endete.
	 */
	struct FBusVehicle
	{
		int32 Id = 0;               // feste Wagen-Nummer (siehe FServiceConfig::FirstVehicleId)
		double PhaseSeconds = 0.0;  // Abfahrt dieses Wagens am Anfangspunkt (Sek.)
	};

	/**
	 * Dienstparameter eines Umlaufs. Aus ihnen folgen Umlaufdauer und Flotte -
	 * Bus und Haltestellenmonitor rechnen damit dieselben Zeiten.
	 */
	struct FServiceConfig
	{
		double CruiseSpeedCmS = 0.0;
		double StopDwellSeconds = 8.0;
		double TerminusDwellSeconds = 600.0;   // 10 min Wendezeit an BEIDEN Enden
		double HeadwaySeconds = 1200.0;        // angestrebter Takt
		int32 MaxBuses = 12;                   // Obergrenze des Pools

		/**
		 * Nummer des ersten Wagens; die weiteren zaehlen fortlaufend weiter.
		 *
		 * Beide Linien fahren in DERSELBEN Welt und schreiben in dasselbe Log: mit
		 * "1..N" je Linie hiessen zwei fahrende Busse gleich "Wagen 2". Der Actor
		 * setzt deshalb das Hundertfache der Liniennummer davor (Linie 6 -> 601,
		 * Linie 3 -> 301), sodass eine Wagen-Nummer im Log, am Steg und in der
		 * Mitfahrt genau ein Fahrzeug bezeichnet.
		 */
		int32 FirstVehicleId = 1;
	};

	/**
	 * Flotte und Umlaufdauer aus einem Dienstauftrag bauen.
	 *
	 * Anzahl = ceil(Umlauf/Takt), mindestens 2, hoechstens MaxBuses; die Wagen
	 * starten GLEICHMAESSIG ueber den Umlauf verteilt. Damit ist der Abstand
	 * zwischen zwei Abfahrten nie groesser als der gewuenschte Takt (bei einem
	 * Umlauf, der kein Vielfaches des Takts ist, sogar kleiner - ein Rest-Takt von
	 * 2 Minuten waere schlechter als gleichmaessige 17,5).
	 */
	WIESBADENREAL_API void BuildFleet(const FBusRoute& Route, const FServiceConfig& Config,
		double& OutCycleSeconds, TArray<FBusVehicle>& OutFleet);

	/**
	 * Wo ist Wagen V zur Dienstzeit ServiceSeconds?
	 *
	 * Rein periodisch: der Umlauf wiederholt sich unendlich (durchgehender
	 * Betrieb) - ein Wagen kurz VOR seiner Abfahrt ist also noch im Ruecklauf des
	 * vorigen Umlaufs und nicht etwa geparkt. Negative Zeiten werden entsprechend
	 * umlaufen (kein Clamp auf 0).
	 */
	WIESBADENREAL_API FBusState FleetStateAt(double ServiceSeconds, const FBusVehicle& Vehicle,
		const FBusRoute& Route, double CruiseSpeedCmS,
		double StopDwellSeconds, double TerminusDwellSeconds);

	/**
	 * Die naechsten MaxCount Durchfahrten an einer Halte (Hinfahrt) aus der Flotte:
	 * je Wagen ist die naechste Durchfahrt Phase + OffsetToStopSeconds + k*Umlauf.
	 * Reihenfolge nach Restzeit, aufsteigend - die Anzeigetafel bekommt damit
	 * dieselben Zeiten, die die Busse tatsaechlich fahren.
	 */
	WIESBADENREAL_API void FleetDepartures(double ServiceSeconds, const TArray<FBusVehicle>& Fleet,
		double CycleSeconds, double OffsetToStopSeconds, int32 MaxCount,
		TArray<double>& OutSecondsUntil);

	/** Ein gerade fahrender Kurs: seit Abfahrt verstrichene Zeit + stabiler Index. */
	struct FBusRun
	{
		double Elapsed = 0.0;   // Sekunden seit Abfahrt dieses Kurses am Terminus
		int64 Index = 0;        // global fortlaufender Kurs-Index (stabile Zuordnung)
	};

	/**
	 * Alle zur Dienstzeit ServiceSeconds gerade unterwegs befindlichen Kurse:
	 * jede Abfahrt D mit 0 <= (ServiceSeconds - D) < RoundTripSeconds ist aktiv
	 * (auch ueber Mitternacht). OutRuns enthaelt je Kurs die verstrichene Zeit und
	 * einen fortlaufenden Index (fuer eine ruckelfreie Slot-Zuordnung im Actor).
	 * Datenrein, deterministisch, ohne Weltzugriff.
	 */
	WIESBADENREAL_API void ActiveRuns(double ServiceSeconds, const FBusSchedule& Schedule,
		double RoundTripSeconds, TArray<FBusRun>& OutRuns);

	/**
	 * Fahrzeit ab Terminus (Halt 0) bis zur Ankunft an StopIndex auf der Hinfahrt:
	 * Summe der Fahrsegmente + Verweilzeiten an den ZWISCHENhalten. StopIndex 0 -> 0.
	 * Fuer den Abfahrtsmonitor: Durchfahrtszeit einer Abfahrt an einer Zwischenhalte.
	 */
	WIESBADENREAL_API double SecondsToStop(const FBusRoute& Route, double CruiseSpeedCmS,
		double StopDwellSeconds, int32 StopIndex);

	/**
	 * Dasselbe fuer BEIDE Richtungen: `bForward` = Hinfahrt, sonst Gegenrichtung
	 * (deren Wagen erreichen die Halte erst nach Hinfahrt + Wendezeit + Rueckfahrt).
	 * Fuer die zwei Saeulen an einer Halte - je Strassenseite eine Richtung.
	 */
	WIESBADENREAL_API double SecondsToStopOnLeg(const FBusRoute& Route, double CruiseSpeedCmS,
		double StopDwellSeconds, double TerminusDwellSeconds, int32 StopIndex, bool bForward);

	/**
	 * Die naechsten MaxCount Durchfahrten an einer Halte ab ServiceSeconds:
	 * je Fahrplan-Abfahrt D ist die Durchfahrt D + OffsetToStopSeconds. OutSecondsUntil
	 * enthaelt aufsteigend die Restzeiten (Sekunden bis zur Durchfahrt), ueber
	 * Mitternacht hinweg. Datenrein. OffsetToStopSeconds = SecondsToStop der Halte.
	 */
	WIESBADENREAL_API void NextDepartures(double ServiceSeconds, const FBusSchedule& Schedule,
		double OffsetToStopSeconds, int32 MaxCount, TArray<double>& OutSecondsUntil);

	/**
	 * Index der naechsten anzusagenden Halte aus Position + Fahrtrichtung: die
	 * erste Halte VOR dem Bus (Hinfahrt: kleinster Bogen > Position; Rueckfahrt:
	 * groesster Bogen < Position). An bzw. kurz nach einer Halte gilt die FOLGENDE
	 * als naechste (Eps). Am/hinter dem Terminus die Endhalte (Hinfahrt) bzw.
	 * Halt 0 (Rueckfahrt). -1 bei < 2 Halten. Datenrein fuer die Halteansagen.
	 */
	WIESBADENREAL_API int32 NextStopIndex(double ArcLengthCm, bool bForward,
		const TArray<double>& StopArcCm);
}