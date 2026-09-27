// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Core/WiesbadenQuitWatchdog.h"

#include "WiesbadenReal.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include <atomic>
#include <thread>

namespace
{
	std::atomic<double> GLetzterTick{0.0};
	std::atomic<double> GExitAnfrage{0.0};
	std::atomic<bool> GStrangLaeuft{false};

	// Fristen. Werden nur beim Start des Strangs aus der Kommandozeile gelesen
	// und danach ausschliesslich gelesen - kein Dateneingriff ueber Threads.
	double GWarnSek = 45.0;
	double GStallHartSek = 420.0;
	double GExitHartSek = 120.0;

	void LeseFristen()
	{
		FParse::Value(FCommandLine::Get(), TEXT("WbWatchdogWarnSec="), GWarnSek);
		FParse::Value(FCommandLine::Get(), TEXT("WbWatchdogStallSec="), GStallHartSek);
		FParse::Value(FCommandLine::Get(), TEXT("WbWatchdogExitSec="), GExitHartSek);
	}

	void StarteStrangFallsNoetig()
	{
		if (!GStrangLaeuft.exchange(true))
		{
			LeseFristen();
			UE_LOG(LogWbCore, Log,
				TEXT("WbWatchdog aktiv: Warnung nach %.0f s ohne Tick, hartes Ende nach %.0f s ohne Tick bzw. %.1f s nach Exit-Anfrage."),
				GWarnSek, GStallHartSek, GExitHartSek);
			std::thread(
				[]
				{
					bool Verwarnt = false;
					for (;;)
					{
						FPlatformProcess::Sleep(0.5f);
						const double Jetzt = FPlatformTime::Seconds();
						const double LetzterTick = GLetzterTick.load();
						const double ExitAnfrage = GExitAnfrage.load();

						switch (FWiesbadenQuitWatchdog::Evaluate(
							Jetzt, LetzterTick, ExitAnfrage, GWarnSek, GStallHartSek, GExitHartSek))
						{
						case FWiesbadenQuitWatchdog::EEntscheidung::WarnungStall:
							if (!Verwarnt)
							{
								Verwarnt = true;
								UE_LOG(LogWbCore, Warning,
									TEXT("WbWatchdog: Spiel-Strang seit %.0f s ohne Tick (GPU-Stall?). Hartes Ende in %.0f s, falls keine Erholung kommt."),
									Jetzt - LetzterTick, GStallHartSek - (Jetzt - LetzterTick));
							}
							break;

						case FWiesbadenQuitWatchdog::EEntscheidung::KillStall:
							UE_LOG(LogWbCore, Error,
								TEXT("WbWatchdog: kein Tick seit %.0f s - erzwinge Beenden (Zombie-Schutz, Exit-Code 43)."),
								Jetzt - LetzterTick);
							// RequestExitWithStatus(true) spuelt vorher das Log
							// und terminiert dann hart - der eingefrorene
							// Spiel-Strang kann das Beenden nicht mehr selbst
							// erledigen.
							FPlatformMisc::RequestExitWithStatus(true, 43, TEXT("WiesbadenQuitWatchdog: Stall"));
							return;

						case FWiesbadenQuitWatchdog::EEntscheidung::KillExit:
							UE_LOG(LogWbCore, Error,
								TEXT("WbWatchdog: Exit haengt seit %.1f s nach Anfrage - erzwinge Beenden (Zombie-Schutz, Exit-Code 44)."),
								Jetzt - ExitAnfrage);
							FPlatformMisc::RequestExitWithStatus(true, 44, TEXT("WiesbadenQuitWatchdog: ExitHang"));
							return;

						default:
							Verwarnt = false;
							break;
						}
					}
				}).detach();
		}
	}
}

void FWiesbadenQuitWatchdog::NotifyTick()
{
	GLetzterTick.store(FPlatformTime::Seconds());
	StarteStrangFallsNoetig();
}

void FWiesbadenQuitWatchdog::NotifyExitRequested(const TCHAR* Grund)
{
	GExitAnfrage.store(FPlatformTime::Seconds());
	StarteStrangFallsNoetig();
	UE_LOG(LogWbCore, Log,
		TEXT("WbWatchdog: Exit angefragt (%s) - falls der Prozess laenger als %.1f s weiterlebt, beendet ihn der Watchdog hart."),
		Grund ? Grund : TEXT("?"), GExitHartSek);
}

FWiesbadenQuitWatchdog::EEntscheidung FWiesbadenQuitWatchdog::Evaluate(
	double Jetzt, double LetzterTick, double ExitAnfrage,
	double WarnSek, double StallHartSek, double ExitHartSek)
{
	// Nach einer Exit-Anfrage zaehlt nur noch die Exit-Frist: der Shutdown
	// laesst die Ticks ausfallen (Welt abgebaut), das waere sonst ein
	// Fehlalarm am sauberen Ende.
	if (ExitAnfrage > 0.0)
	{
		if (Jetzt - ExitAnfrage >= ExitHartSek)
		{
			return EEntscheidung::KillExit;
		}
		return EEntscheidung::Keine;
	}

	// Vor dem ersten Tick (Karten-Ladephase) gibt es nichts, das laufen muesste.
	if (LetzterTick <= 0.0)
	{
		return EEntscheidung::Keine;
	}

	const double Pause = Jetzt - LetzterTick;
	if (Pause >= StallHartSek)
	{
		return EEntscheidung::KillStall;
	}
	if (Pause >= WarnSek)
	{
		return EEntscheidung::WarnungStall;
	}
	return EEntscheidung::Keine;
}
