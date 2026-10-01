// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NPC/WiesbadenPoliceHeli.h"
#include "NPC/WiesbadenPoliceSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/** Schlichter Punkt-Actor mit Szenenwurzel (SetActorLocation braucht ein Root). */
	AActor* MakePoint(UWorld* World, const FVector& P)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor) { return nullptr; }
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(P);
		return Actor;
	}

	/** Sichtwand: Engine-Cube, gestreckt, mit Kollision (BlockAll). */
	AActor* MakeWall(UWorld* World, const FVector& P, const FVector& Scale)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor) { return nullptr; }
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Actor, TEXT("Wall"));
		Actor->SetRootComponent(Comp);
		Comp->RegisterComponent();
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (Cube)
		{
			Comp->SetStaticMesh(Cube);
			Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		Comp->SetWorldScale3D(Scale);
		Actor->SetActorLocation(P);
		return Actor;
	}
}

/**
 * Sichtverfolgungs-Integrationstest.
 *
 * Die Regel, die hier haengt: die Verfolgerfahrt faehrt den Spieler DIREKT nur
 * bei freier Sicht an - sonst bleibt die Route. Die Sicht kommt aus echten
 * Strahlen gegen echte Geometrie (UWorld + LineTrace), die Entscheidung aus
 * WiesbadenPolice::PursuitTarget. Genau die Kombination war vorher nirgends
 * geprueft: die Sichtlogik sass in Tick(), die Entscheidung inline daneben.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoliceSightPursuitTest,
	"WiesbadenReal.Polizei.Sichtverfolgung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPoliceSightPursuitTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AActor* Beobachter = MakePoint(World, FVector(0, 0, 150));
	AActor* Ziel = MakePoint(World, FVector(3000, 0, 150));
	if (!TestNotNull(TEXT("Beobachter gespawnt"), Beobachter)
		|| !TestNotNull(TEXT("Ziel gespawnt"), Ziel))
	{
		World->DestroyWorld(false);
		return false;
	}

	// -- Freie Strecke: gesehen -------------------------------------------
	const FVector SpielerPos = Ziel->GetActorLocation();
	const bool Frei = WiesbadenPolice::CanSee(World, Beobachter, Ziel, 14000.0f);
	TestTrue(TEXT("freie Strecke -> Sicht"), Frei);

	// Bei Sicht, in Nahziel-Reichweite (<10 m), gleiche Hoehe: die
	// Verfolgerfahrt nimmt den Spieler direkt (Integration: ECHTE Sicht
	// steuert die ECHTE Entscheidung). Das Fahrzeug steht dafuer 5 m vor
	// dem Spieler - 30 m Abstand waere ausserhalb MaxDirectCm und Route.
	TestTrue(TEXT("Sicht -> direktes Nahziel (Spieler)"),
		WiesbadenPolice::PursuitTarget(FVector(2500, 0, 150), FVector(99999, 99999, 0),
			SpielerPos, Frei).Equals(SpielerPos, 1.0));

	// -- Wand dazwischen: keine Sicht --------------------------------------
	AActor* Wand = MakeWall(World, FVector(1500, 0, 150), FVector(0.5f, 20.0f, 4.0f));
	if (!TestNotNull(TEXT("Sichtwand gespawnt"), Wand))
	{
		World->DestroyWorld(false);
		return false;
	}
	const bool HinterWand = WiesbadenPolice::CanSee(World, Beobachter, Ziel, 14000.0f);
	TestFalse(TEXT("Wand dazwischen -> keine Sicht"), HinterWand);

	// Ohne Sicht faehrt die Streife weiter die Route - kein Beeline durch Haus.
	TestTrue(TEXT("keine Sicht -> Routen-Ziel"),
		WiesbadenPolice::PursuitTarget(Beobachter->GetActorLocation(), FVector(99999, 99999, 0),
			SpielerPos, HinterWand).Equals(FVector(99999, 99999, 0), 1.0));

	// Wand wieder weg: dieselbe Konstellation sieht wieder (kein Zustand klebt).
	Wand->Destroy();
	TestTrue(TEXT("Wand entfernt -> wieder Sicht"),
		WiesbadenPolice::CanSee(World, Beobachter, Ziel, 14000.0f));

	// -- Reichweite: zu weit bleibt zu weit --------------------------------
	Ziel->SetActorLocation(FVector(20000, 0, 150)); // 200 m > 140 m
	TestFalse(TEXT("jenseits der Reichweite -> keine Sicht"),
		WiesbadenPolice::CanSee(World, Beobachter, Ziel, 14000.0f));
	Ziel->SetActorLocation(FVector(3000, 0, 150));

	// -- Nahziel-Grenzen der Entscheidung ----------------------------------
	// Zu weit weg: auch bei Sicht bleibt die Route (der Beobachter ist hier
	// das Fahrzeug - 30 m Abstand, MaxDirectCm = 10 m -> Route).
	TestTrue(TEXT("Sicht, aber >1 m... 10 m -> Routen-Ziel"),
		WiesbadenPolice::PursuitTarget(FVector(0, 0, 150), FVector(99999, 99999, 0),
			FVector(3000, 0, 150), true).Equals(FVector(99999, 99999, 0), 1.0));
	// Gleiche Distanz, aber grosse Hoehendifferenz (Bruecke/Tal): Route.
	TestTrue(TEXT("Sicht, aber grosse Hoehendifferenz -> Routen-Ziel"),
		WiesbadenPolice::PursuitTarget(FVector(0, 0, 150), FVector(99999, 99999, 0),
			FVector(500, 0, 900), true).Equals(FVector(99999, 99999, 0), 1.0));
	// Nah + Sicht + gleiche Hoehe: Spieler.
	TestTrue(TEXT("Sicht + nah + gleiche Hoehe -> Spieler"),
		WiesbadenPolice::PursuitTarget(FVector(0, 0, 150), FVector(99999, 99999, 0),
			FVector(500, 0, 150), true).Equals(FVector(500, 0, 150), 1.0));

	World->DestroyWorld(false);
	return true;
}

/**
 * Eskalation ab Fahndungsstufe 4 (SPEC §8: Polizei -> SEK -> Heli -> ...).
 * Feste Solls: ab Stufe 4 SEK, ab Stufe 5 Luft-Verfolger, Festnahme wird
 * mit den Spezialkraeften schneller. Kein Zufallswert darf sich einschleichen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoliceEscalationTest,
	"WiesbadenReal.Polizei.Eskalation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPoliceEscalationTest::RunTest(const FString& Parameters)
{
	using WiesbadenPolice::EscalationFor;

	TestEqual(TEXT("Stufe 0: keine Kräfte"), EscalationFor(0).Streifen, 0);
	TestEqual(TEXT("Stufe 0: kein SEK"), EscalationFor(0).Sek, 0);
	TestEqual(TEXT("Stufe 0: kein Heli"), EscalationFor(0).Heli, 0);

	TestEqual(TEXT("Stufe 3: nur Streifen"), EscalationFor(3).Streifen, 3);
	TestEqual(TEXT("Stufe 3: noch kein SEK"), EscalationFor(3).Sek, 0);
	TestEqual(TEXT("Stufe 3: noch kein Heli"), EscalationFor(3).Heli, 0);
	TestEqual(TEXT("Stufe 3: Festnahme in 5 s"), EscalationFor(3).ArrestSecondsNeeded, 5.0f);

	TestEqual(TEXT("Stufe 4: SEK raeckt aus"), EscalationFor(4).Sek, 2);
	TestEqual(TEXT("Stufe 4: weiterhin kein Heli"), EscalationFor(4).Heli, 0);
	TestEqual(TEXT("Stufe 4: Festnahme in 4 s"), EscalationFor(4).ArrestSecondsNeeded, 4.0f);

	TestEqual(TEXT("Stufe 5: Luft-Verfolger"), EscalationFor(5).Heli, 1);
	TestEqual(TEXT("Stufe 5: Festnahme in 3,5 s"), EscalationFor(5).ArrestSecondsNeeded, 3.5f);

	TestEqual(TEXT("Stufe 6: drittes SEK"), EscalationFor(6).Sek, 3);
	TestEqual(TEXT("Stufe 6: Festnahme in 3 s"), EscalationFor(6).ArrestSecondsNeeded, 3.0f);

	// Streifen-Soll bleibt die alte Regel (Test Polizei.PursuitControls deckt
	// PatrolCount ab) - Eskalation darf sie nicht ersetzen.
	for (int32 Level = 0; Level <= 6; ++Level)
	{
		TestEqual(TEXT("Streifen-Soll bleibt PatrolCount"), EscalationFor(Level).Streifen,
			WiesbadenPolice::PatrolCount(Level));
	}
	return true;
}

/**
 * Heli-Verfolger (datenrein): Schwebeposition mit Seitenabstand, gedeckelte
 * Annäherung, Sicht-Hysterese. Werte hand gerechnet wie im PursuerTest.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoliceHeliTest,
	"WiesbadenReal.Polizei.HeliVerfolger",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPoliceHeliTest::RunTest(const FString& Parameters)
{
	const FWiesbadenPoliceHeliParams P; // Hover 12000, Follow 6000, Speed 2500, Spot 25000, Lose 32000

	// (1) Annäherung gedampft auf MaxSpeed*dt: Ziel ist Spieler + Follow-Richtung
	//     (-X, weil der Heli westlich startet) + Hover 12000. Ziel also
	//     (-1000, 0, 12000); Distanz ~12042 -> Schritt genau 2500.
	{
		FWiesbadenPoliceHeliState S;
		S.Position = FVector(0, 0, 0);
		const FWiesbadenPoliceHeliState R = FWiesbadenPoliceHeli::Step(S, FVector(5000, 0, 0), P, 1.0);
		TestTrue(TEXT("Schritt gedeckelt auf 2500 cm"),
			FMath::IsNearlyEqual(FVector::Dist(R.Position, S.Position), 2500.0, 1.0));
		TestTrue(TEXT("bewegt sich auf die Schwebeposition zu"),
			FVector::Dist(R.Position, FVector(-1000, 0, 12000))
			< FVector::Dist(S.Position, FVector(-1000, 0, 12000)));
		TestTrue(TEXT("im Spot-Radius -> Spieler im Blick"), R.bSpotted);
	}

	// (2) In Position: Ziel = eigene Position -> kein Schleudern, Sicht bleibt.
	{
		FWiesbadenPoliceHeliState S;
		S.Position = FVector(-1000, 0, 12000);
		const FWiesbadenPoliceHeliState R = FWiesbadenPoliceHeli::Step(S, FVector(5000, 0, 0), P, 1.0);
		TestTrue(TEXT("in Position -> haelt still"), R.Position.Equals(S.Position, 1.0));
		TestTrue(TEXT("Seitenabstand 60 m statt direkt darueber"),
			FMath::IsNearlyEqual(FVector2D::Distance(FVector2D(R.Position.X, R.Position.Y),
				FVector2D(5000, 0)), 6000.0, 1.0));
	}

	// (3) Direkt darueber: degenerierter Seitenabstand waehlt die Vorgabe (+X).
	{
		FWiesbadenPoliceHeliState S;
		S.Position = FVector(5000, 0, 12000);
		const FWiesbadenPoliceHeliState R = FWiesbadenPoliceHeli::Step(S, FVector(5000, 0, 0), P, 1.0);
		TestTrue(TEXT("Vorgabe-Seitenabstand nach +X"),
			R.Position.X > S.Position.X || R.Position.Equals(S.Position, 1.0));
	}

	// (4) Sicht-Hysterese: im Band zwischen Spot und Lose bleibt der Kontakt
	//     erhalten (DeltaSeconds 0 = keine Bewegung, saubere Messung).
	{
		FWiesbadenPoliceHeliState S;
		S.Position = FVector(0, 0, 12000);
		S.bSpotted = true;
		const FWiesbadenPoliceHeliState R = FWiesbadenPoliceHeli::Step(S, FVector(30000, 0, 0), P, 0.0);
		TestTrue(TEXT("Band 250-320 m -> Kontakt bleibt"), R.bSpotted);
	}

	// (5) Jenseits des Lose-Radius ist der Kontakt weg.
	{
		FWiesbadenPoliceHeliState S;
		S.Position = FVector(0, 0, 12000);
		S.bSpotted = true;
		const FWiesbadenPoliceHeliState R = FWiesbadenPoliceHeli::Step(S, FVector(33000, 0, 0), P, 0.0);
		TestFalse(TEXT("jenseits 320 m -> Kontakt verloren"), R.bSpotted);
	}

	// (6) Aufnahme: ohne Kontakt und nah genug wird der Spieler gesehen.
	{
		FWiesbadenPoliceHeliState S;
		S.Position = FVector(0, 0, 12000);
		const FWiesbadenPoliceHeliState R = FWiesbadenPoliceHeli::Step(S, FVector(10000, 0, 0), P, 0.0);
		TestTrue(TEXT("im Spot-Radius -> Kontakt aufgenommen"), R.bSpotted);
	}

	return true;
}
