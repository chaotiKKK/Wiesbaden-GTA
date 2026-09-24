// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "GIS/WiesbadenPedestrianSimulation.h"
#include "World/PedestrianSpawnerComponent.h"

/**
 * Regressionstest des Fussgaenger-Sichtbarkeitszaehlers.
 *
 * Sobald die vier Gangphasen-Meshes geladen sind, wird animiert: der GRUNDPOOL
 * wird geleert und jede Figur lebt in ihrem Pose-Pool. GetVisibleCount() las
 * frueher nur den Grundpool und meldete dann DAUERHAFT 0, obwohl die Figuren
 * gezeichnet werden. Dieser Test nagelt fest, dass der Zaehler bei aktiver
 * Animation die SUMME aller Pose-Pools liefert und nie faelschlich 0 meldet.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPedestrianVisibleCountTest,
	"WiesbadenReal.World.PedestrianVisibleCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** N Fussgaenger, deren Schrittphase gleichmaessig ueber 0..1 laeuft - so
	 *  verteilen sie sich ueber ALLE vier Gangphasen-Pools (der Grundpool bleibt
	 *  leer). Genau die Konstellation, in der der alte Zaehler 0 lieferte. */
	TArray<FPlacedPedestrian> MakeSpread(int32 N)
	{
		TArray<FPlacedPedestrian> Out;
		Out.Reserve(N);
		for (int32 i = 0; i < N; ++i)
		{
			FPlacedPedestrian P;
			P.Location = FVector(i * 120.0, 0.0, 0.0);
			P.StridePhase = (N > 1) ? static_cast<float>(i) / static_cast<float>(N - 1) : 0.0f;
			Out.Add(P);
		}
		return Out;
	}
}

bool FPedestrianVisibleCountTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AActor* Owner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Traeger-Actor gespawnt"), Owner))
	{
		World->DestroyWorld(false);
		return false;
	}

	USceneComponent* Root = NewObject<USceneComponent>(Owner);
	Owner->SetRootComponent(Root);
	Root->RegisterComponent();

	UPedestrianSpawnerComponent* Spawner = NewObject<UPedestrianSpawnerComponent>(Owner);
	Spawner->SetupAttachment(Root);
	// OnRegister laedt Mesh + die vier Gangphasen und legt die Pose-Pools an.
	Spawner->RegisterComponent();

	// Voraussetzung der Kernpruefung: die Gangphasen sind geladen (Animation aktiv).
	// Ohne sie liefe nur der Grundpool-Pfad - dann testet dieser Test nicht die
	// Regression. Im Editor (ungecookt) laden die SM_WbPerson_0..3 problemlos.
	TestTrue(TEXT("Gangphasen-Animation ist aktiv (vier Pose-Pools geladen)"),
		Spawner->IsAnimated());

	// -- Kern: verteilte Figuren -> Grundpool leer, alles in den Pose-Pools ----
	const int32 N = 40;
	Spawner->UpdateInstances(MakeSpread(N));

	TestEqual(TEXT("Zaehler == Summe aller Pose-Pools (nicht der leere Grundpool)"),
		Spawner->GetVisibleCount(), N);
	TestTrue(TEXT("Zaehler niemals faelschlich 0 bei N>0"),
		Spawner->GetVisibleCount() > 0);

	// -- Zaehler folgt der Menge (kein Haengenbleiben) -------------------------
	Spawner->UpdateInstances(MakeSpread(12));
	TestEqual(TEXT("Nach Update auf 12 -> Zaehler 12"), Spawner->GetVisibleCount(), 12);

	// -- ECHTE 0 (leer) vs. faelschliche 0 (Bug) -------------------------------
	Spawner->UpdateInstances(TArray<FPlacedPedestrian>());
	TestEqual(TEXT("Leer -> echte 0"), Spawner->GetVisibleCount(), 0);

	// -- Wieder befuellen -> wieder > 0 (kommt aus der 0 heraus) ---------------
	Spawner->UpdateInstances(MakeSpread(N));
	TestEqual(TEXT("Wieder befuellt -> Zaehler N"), Spawner->GetVisibleCount(), N);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPedestrianClothingTest,
	"WiesbadenReal.Vehicles.Pedestrian.Clothing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Deterministische Kleidungsfarben je Fussgaenger-Seed: gleicher Seed -> gleiche
 * Kleidung (die Person wechselt nicht je Bild die Farbe), und ueber viele Seeds
 * streut es breit ueber die Paletten (nicht alle gleich angezogen).
 */
bool FPedestrianClothingTest::RunTest(const FString& Parameters)
{
	using Ped = UPedestrianSpawnerComponent;

	// -- Determinismus -------------------------------------------------------
	for (int32 Seed : {0, 1, 7, 42, 1000, -5, 999999})
	{
		FLinearColor S1, T1, S2, T2; float K1, K2;
		Ped::ComputePedestrianColors(Seed, S1, T1, K1);
		Ped::ComputePedestrianColors(Seed, S2, T2, K2);
		TestTrue(TEXT("gleicher Seed -> gleiches Hemd"), S1 == S2);
		TestTrue(TEXT("gleicher Seed -> gleiche Hose"), T1 == T2);
		TestEqual(TEXT("gleicher Seed -> gleicher Hautton"), K1, K2);
		TestTrue(TEXT("Hautton in 0..1"), K1 >= 0.0f && K1 <= 1.0f);
	}

	// -- Vielfalt: ueber viele Seeds werden mehrere Hemd- UND Hosenfarben genutzt
	{
		TSet<FString> ShirtSet, TrouserSet;
		int32 SkinLow = 0, SkinHigh = 0;
		for (int32 Seed = 0; Seed < 4000; ++Seed)
		{
			FLinearColor Sh, Tr; float Sk;
			Ped::ComputePedestrianColors(Seed, Sh, Tr, Sk);
			ShirtSet.Add(Sh.ToString());
			TrouserSet.Add(Tr.ToString());
			if (Sk < 0.5f) { ++SkinLow; } else { ++SkinHigh; }
		}
		// Beide Paletten werden breit genutzt (mind. 6 Hemd-, 4 Hosenfarben).
		TestTrue(FString::Printf(TEXT("viele Hemdfarben (%d)"), ShirtSet.Num()), ShirtSet.Num() >= 6);
		TestTrue(FString::Printf(TEXT("viele Hosenfarben (%d)"), TrouserSet.Num()), TrouserSet.Num() >= 4);
		// Hauttoene sind gemischt (nicht alle hell oder alle dunkel).
		TestTrue(TEXT("dunkle Hauttoene kommen vor"), SkinLow > 200);
		TestTrue(TEXT("helle Hauttoene kommen vor"), SkinHigh > 200);
	}

	return true;
}
