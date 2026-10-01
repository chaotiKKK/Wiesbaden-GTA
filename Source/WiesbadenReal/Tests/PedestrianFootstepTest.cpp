// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Audio/WiesbadenFootstepPool.h"
#include "GIS/WiesbadenPedestrianSimulation.h"

namespace
{
	/**
	 * N Passanten an gestaffelten Positionen, alle mit derselben Phase.
	 *
	 * FirstSeed ist wichtig: die Phase wird JE SEED gemerkt. Wer zwei
	 * unabhaengige Faelle mit denselben Seeds faehrt, mischt die Zustaende
	 * und prueft danach etwas anderes, als er zu pruefen glaubt.
	 */
	TArray<FPlacedPedestrian> MakePedestrians(int32 Count, float Phase, int32 FirstSeed = 0)
	{
		TArray<FPlacedPedestrian> Out;
		Out.Reserve(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			FPlacedPedestrian Pedestrian;
			Pedestrian.Location = FVector(i * 150.0, 0.0, 0.0);
			Pedestrian.Rotation = FRotator::ZeroRotator;
			Pedestrian.StridePhase = Phase;
			Pedestrian.ScaleFactor = FVector::OneVector;
			Pedestrian.Seed = FirstSeed + i;
			Out.Add(Pedestrian);
		}
		return Out;
	}
}

/**
 * Test der Passanten-Fussschritte.
 *
 * Kernpunkt: die StridePhase laeuft 0..1 und springt bei 1 -> 0
 * (ComputeStridePhase = frac(Distanz / Schrittlaenge)). Genau dieser Sprung
 * IST der Schritt - die Phase ist die Uhr, ein Zeitdelta braucht es nicht.
 *
 * Der Test faehrt deshalb IMMER von einer hohen auf eine tiefe Phase. Ein
 * Rueckwaertsgang von 0.4 auf 0.05 etwa ist KEIN Grenzuebertritt; eine
 * Sprungerkennung, die nur "kleiner als vorher" prueft, wuerde dort schon
 * ausloesen, und eine, die den Grenzuebertritt nicht prueft, schweigt ganz.
 * Beides ist im Bild nicht zu unterscheiden, deshalb wird es hier
 * festgenagelt - samt der 0.5-Schwelle als Grenzfall.
 *
 * Der Pool bekommt nullptr als Welt: er zaehlt dann, ohne zu spielen, und
 * der Test bleibt headless.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPedestrianFootstepTest,
	"WiesbadenReal.Audio.Footsteps.Pedestrians",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPedestrianFootstepTest::RunTest(const FString& Parameters)
{
	FWiesbadenFootstepPool Pool;

	// Bild 1: Phase 0.95, noch nie gesehene Passanten. Ohne vorigen Wert ist
	// kein Uebergang feststellbar - sie schweigen.
	Pool.NotePedestrians(nullptr, MakePedestrians(10, 0.95f));
	TestEqual(TEXT("erstes Bild erzeugt keinen Schritt"),
		Pool.GetPedestrianStepsLastFrame(), 0);

	// Bild 2: Phase 0.05, also ueber die Grenze. Zehn wollen, das Budget
	// laesst sechs zu.
	Pool.NotePedestrians(nullptr, MakePedestrians(10, 0.05f));
	TestEqual(TEXT("Sprung ueber die Grenze erzeugt Schritte, aber nur bis zum Budget"),
		Pool.GetPedestrianStepsLastFrame(), FWiesbadenFootstepPool::MaxStepsPerFrame);

	// Bild 3: gleiche Phase - kein Schritt. Gegenprobe zur zu lockeren
	// Variante (jede Aenderung zaehlt).
	Pool.NotePedestrians(nullptr, MakePedestrians(10, 0.05f));
	TestEqual(TEXT("gleiche Phase erzeugt nichts"),
		Pool.GetPedestrianStepsLastFrame(), 0);

	// Bild 4: zurueck auf 0.95 - wieder kein Grenzuebertritt, also still.
	Pool.NotePedestrians(nullptr, MakePedestrians(10, 0.95f));
	TestEqual(TEXT("Ruecklauf ohne Grenzuebertritt erzeugt keinen Schritt"),
		Pool.GetPedestrianStepsLastFrame(), 0);

	// Bild 5: von 0.95 auf 0.25 - ueber die Grenze, aber nur drei Passanten:
	// unter dem Budget bekommt jeder seinen Schritt.
	Pool.NotePedestrians(nullptr, MakePedestrians(3, 0.25f));
	TestEqual(TEXT("unter dem Budget: je Passant ein Schritt"),
		Pool.GetPedestrianStepsLastFrame(), 3);

	// Grenzfall der Schwelle, auf eigenen Seeds: 0.95 -> 0.46 liegt mit 0.49
	// knapp darunter, 0.95 -> 0.44 mit 0.51 knapp darueber.
	Pool.NotePedestrians(nullptr, MakePedestrians(1, 0.95f, 2000));
	Pool.NotePedestrians(nullptr, MakePedestrians(1, 0.46f, 2000));
	TestEqual(TEXT("Differenz 0.49 ist noch kein Schritt"),
		Pool.GetPedestrianStepsLastFrame(), 0);
	Pool.NotePedestrians(nullptr, MakePedestrians(1, 0.95f, 2000));
	Pool.NotePedestrians(nullptr, MakePedestrians(1, 0.44f, 2000));
	TestEqual(TEXT("Differenz 0.51 ist ein Schritt"),
		Pool.GetPedestrianStepsLastFrame(), 1);

	// Leerer Durchgang: nichts, kein Absturz.
	Pool.NotePedestrians(nullptr, TArray<FPlacedPedestrian>());
	TestEqual(TEXT("leer -> kein Schritt"),
		Pool.GetPedestrianStepsLastFrame(), 0);

	// 200 Passanten auf frischen Seeds duerfen nicht 200 Schritte erzeugen.
	Pool.NotePedestrians(nullptr, MakePedestrians(200, 0.95f, 1000));
	TestEqual(TEXT("frische Seeds erzeugen im ersten Bild nichts"),
		Pool.GetPedestrianStepsLastFrame(), 0);

	Pool.NotePedestrians(nullptr, MakePedestrians(200, 0.05f, 1000));
	TestTrue(TEXT("Schrittzahl ist begrenzt"),
		Pool.GetPedestrianStepsLastFrame() <= FWiesbadenFootstepPool::MaxStepsPerFrame);

	// Und die Begrenzung darf NICHT das Phasen-Update abbrechen: sonst
	// loesten die unterdrueckten Passanten im naechsten Bild alle auf einmal
	// aus und das Budget verfehlte genau dort seinen Zweck.
	Pool.NotePedestrians(nullptr, MakePedestrians(200, 0.05f, 1000));
	TestEqual(TEXT("unterdrueckte Passanten loesen spaeter nicht nach"),
		Pool.GetPedestrianStepsLastFrame(), 0);

	return true;
}
