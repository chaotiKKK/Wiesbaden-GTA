// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Zombie-Schutz fuer entmannte Messlaeufe (-WbQuitAfter).
 *
 * Zwei Haenge-Ketten sind aus den Messlaeufen vom 26./27.09.2026 belegt:
 *
 * 1. GPU-Stall: ein Zeichen-Durchgang haengt im Scene-Pass
 *    ("GPU timeout: A payload ... has not completed", D3D12RHI), der
 *    Spiel-Strang friert mit ein. Probe3 stand so 347 s still und erholte
 *    sich spaeter; ein Prozess, der sich NIE erhaelt, laeuft ewig und haelt
 *    Engine-Run-Lock und die gebaute DLL - der naechste Lauf/Bau scheitert.
 * 2. Exit-Hang: nach der Exit-Anfrage kann der Shutdown Minuten haengen
 *    ("LogExit: Preparing to exit", DDC-Wartung) - ebenfalls mit Lock + DLL.
 *
 * Der Watchdog laeuft in einem eigenen Strang (ueberlebt also eingefrorene
 * Spiel-/Render-Threads) und beendet den Prozess hart, wenn
 *   - der Spiel-Strang laenger als StallHartSek keinen Tick mehr lief
 *     (ohne dass ein Exit angefragt waere), oder
 *   - nach einer Exit-Anfrage laenger als ExitHartSek kein Ende kam.
 * "Hart" heisst FPlatformMisc::RequestExitWithStatus(true, ...): die Engine
 * spuelt vorher das Log (WindowsPlatformMisc.cpp), die Beweiszeilen bleiben
 * also erhalten - nur der Shutdown-Larm entfaellt.
 *
 * Bewusst NUR fuer Messlaeufe: NotifyTick haengt am -WbQuitAfter-Block des
 * Subsystems, und ohne Exit-Anfrage greift nur die Stall-Frist. Interaktive
 * Editor-Spiele werden nie stillschweigend gekillt.
 *
 * Fristen (Sekunden, per Kommandozeile ueberschreibbar):
 *   -WbWatchdogWarnSec=   Tick-Pause ab der gewarnt wird       (45)
 *   -WbWatchdogStallSec=  Tick-Pause ab der hart beendet wird  (420)
 *   -WbWatchdogExitSec=   Restlebenszeit nach Exit-Anfrage     (120)
 * Die Poll-Schleife laeuft fest alle 0,5 s.
 */
class WIESBADENREAL_API FWiesbadenQuitWatchdog
{
public:
	/** Entscheidung des Watchdogs - reine Funktion, damit Tests sie pruefen koennen. */
	enum class EEntscheidung : uint8
	{
		Keine,        // alles in Ordnung (oder noch in der Ladephase)
		WarnungStall, // Tick-Pause erkannt, aber noch innerhalb der Frist
		KillStall,    // Spiel-Strang eingefroren -> hartes Ende (Code 43)
		KillExit      // Exit haengt -> hartes Ende (Code 44)
	};

	/** Vom Spiel-Strang bei jedem Tick eines Messlaufs (-WbQuitAfter) aufrufen. */
	static void NotifyTick();

	/** Vor jeder Exit-Anfrage des Messlaufs aufrufen (Grund fuer die Logzeile). */
	static void NotifyExitRequested(const TCHAR* Grund);

	/**
	 * Reine Entscheidungsfunktion. Alle Zeiten in Sekunden (FPlatformTime::Seconds),
	 * ExitAnfrage/LetzterTick 0 = noch nie gesetzt. Rangfolge: nach einer
	 * Exit-Anfrage zaehlt nur die Exit-Frist (im Shutdown laeuft kein Tick mehr,
	 * das waere sonst ein Fehlalarm); vor dem ersten Tick (Karten-Ladephase)
	 * greift keine Stall-Frist.
	 */
	static EEntscheidung Evaluate(double Jetzt, double LetzterTick, double ExitAnfrage,
		double WarnSek, double StallHartSek, double ExitHartSek);
};
