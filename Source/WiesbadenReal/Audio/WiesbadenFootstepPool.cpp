// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WiesbadenFootstepPool.h"

#include "Audio/WiesbadenAudioZones.h"
#include "Audio/WiesbadenAudioZonesSubsystem.h"
#include "GIS/WiesbadenPedestrianSimulation.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbFootstepPool, Log, All);

void FWiesbadenFootstepPool::NotePedestrians(
	UWorld* World, const TArray<FPlacedPedestrian>& Placed)
{
	StepsThisFrame = 0;

	// nullptr-Welt ist der erlaubte Fall (headless Test UND ein Lauf ohne
	// geladene Klaenge): dann zaehlt der Pool die Uebergaenge, ohne zu spielen.
	// Kein Absturz, kein Fehler - dieselbe Konvention wie im Subsystem, das
	// ohne Assets still bleibt.
	//
	// Das ist bewusst so: die BUDGET-Regel muss auch ohne Audio pruefbar sein,
	// sonst haette der einzige Test, den diese Klasse hat, nichts zu messen.
	// Ob wirklich ein Klang erklang, weiss das Subsystem und zaehlt es selbst
	// (GetPlayedFootstepCount) - zwei Zaehler mit zwei Bedeutungen.
	UWiesbadenAudioZonesSubsystem* Zones = World
		? World->GetSubsystem<UWiesbadenAudioZonesSubsystem>() : nullptr;

	int32 Emitted = 0;
	int32 Suppressed = 0;

	for (const FPlacedPedestrian& Pedestrian : Placed)
	{
		// FindOrAdd mit Vorgabe -1: damit ist "noch nie gesehen" von einer
		// echten Phase 0.0 unterscheidbar. Sonst wuerde der erste Schritt eines
		// neu erschienenen Passanten im Bild nach dem Vergleich mit 0.0
		// ausgeloest und die Strasse einmal zu laut.
		float& Last = LastPhaseBySeed.FindOrAdd(Pedestrian.Seed, -1.0f);
		const bool bWasKnown = Last >= 0.0f;

		// Sprung ueber die 0/1-Grenze: 0.95 -> 0.05 heisst Schritt. Ein
		// Gleichstand ist keiner, und ein kleiner Sprung (0.2 -> 0.3, die
		// Phase lief nur ein Stueck weiter) auch nicht - deshalb die 0.5 als
		// Schwelle statt jeder Aenderung.
		const bool bCrossed = bWasKnown && (Pedestrian.StridePhase < Last - 0.5f);

		// Die Phase wird IMMER aktualisiert, auch wenn das Budget schon
		// erschoepft ist. Ein fruehes "break" wuerde die uebrigen Passanten
		// auf ihrer alten Phase stehen lassen, und beim naechsten Bild
		// wuerden sie alle gleichzeitig ausloesen - die Begrenzung wuerde
		// dann genau in dem Bild greifen, in dem sie wirken soll.
		Last = Pedestrian.StridePhase;

		if (!bCrossed)
		{
			continue;
		}

		// Das Budget ist die ganze Regel gegen die Feuerwerk-Innenstadt. Wer
		// durchfaellt, gehoert zu den zuerst gezeichneten Figuren - das ist
		// auch die naechste Plausibilitaetsgrenze, wenn der Pool spaeter
		// einmal nach Entfernung sortieren soll.
		//
		// Es greift VOR der Audio-Abfrage: die Regel haengt am Uebergang, nicht
		// am Abspielen. Sonst waere ein Lauf ohne Klaenge unbegrenzt, und die
		// Regel, auf die es ankommt, waere genau dort nicht mehr messbar.
		if (Emitted >= MaxStepsPerFrame)
		{
			++Suppressed;
			continue;
		}

		++Emitted;
		++StepsThisFrame;

		if (!Zones)
		{
			continue;
		}

		const EWbFootstepSurface Surface = Zones->SurfaceUnderFoot(Pedestrian.Location);
		Zones->PlayFootstepAt(Pedestrian.Location, Surface);
	}

	// Auf Log-Ebene, nicht Verbose: das ist die Beweiszeile fuer die
	// Budget-Regel, und Verbose landet nicht im Standardlog. Sie erscheint
	// nur, wenn wirklich gedeckelt wurde - eine Zeile je Bild waere Laerm.
	if (Suppressed > 0)
	{
		UE_LOG(LogWbFootstepPool, Log,
			TEXT("Passanten-Schritte: %d ausgeloest, %d wegen Budget %d unterdrueckt."),
			Emitted, Suppressed, MaxStepsPerFrame);
	}
}
