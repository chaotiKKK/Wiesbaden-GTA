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
	 */
	struct FBusSchedule
	{
		TArray<double> DepartureSeconds;   // Abfahrten ab Terminus, Sek. seit 00:00
		double DaySeconds = 86400.0;       // Tageslaenge (Fahrplan wiederholt sich taeglich)
	};

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
	 * Die naechsten MaxCount Durchfahrten an einer Halte ab ServiceSeconds:
	 * je Fahrplan-Abfahrt D ist die Durchfahrt D + OffsetToStopSeconds. OutSecondsUntil
	 * enthaelt aufsteigend die Restzeiten (Sekunden bis zur Durchfahrt), ueber
	 * Mitternacht hinweg. Datenrein. OffsetToStopSeconds = SecondsToStop der Halte.
	 */
	WIESBADENREAL_API void NextDepartures(double ServiceSeconds, const FBusSchedule& Schedule,
		double OffsetToStopSeconds, int32 MaxCount, TArray<double>& OutSecondsUntil);
}