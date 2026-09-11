// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "Core/WiesbadenGameMode.h"
#include "Engine/World.h"
#include "NPC/WiesbadenStoreMerchant.h"

/**
 * Auswahlregel der NPC-Haendler-Interaktion.
 *
 * Der naechste Haendler gewinnt - aber nur, wenn der Fuss-Pawn in SEINER
 * eigenen Interaktionsreichweite steht. Die Regel liegt als reine Funktion vor
 * (AWiesbadenGameMode::PickMerchantInReach), damit sie ohne Spielsitzung
 * pruefbar bleibt: der Tick kommt ohne Eingabesimulation nicht in den
 * Tastendruck-Zweig.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoreMerchantInReachTest,
	"WiesbadenReal.NPC.StoreMerchant.InReach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/**
	 * Haendler an einer Position: die Klasse legt bewusst keine Wurzel an (siehe
	 * Konstruktor, CreateDefaultSubobject im Kommandlet-Kontext), fuer eine
	 * Position im Test bekommt jeder eine leere Szenen-Wurzel.
	 */
	AWiesbadenStoreMerchant* SpawnMerchantAt(UWorld* World, const FVector& Location)
	{
		AWiesbadenStoreMerchant* Merchant = World->SpawnActor<AWiesbadenStoreMerchant>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Merchant)
		{
			return nullptr;
		}

		USceneComponent* Root = NewObject<USceneComponent>(Merchant);
		Merchant->SetRootComponent(Root);
		Root->RegisterComponent();
		Merchant->SetActorLocation(Location);
		return Merchant;
	}
} // namespace

bool FStoreMerchantInReachTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	const FVector Foot(0.0, 0.0, 0.0);

	AWiesbadenStoreMerchant* Near = SpawnMerchantAt(World, FVector(300.0, 0.0, 0.0));
	AWiesbadenStoreMerchant* Far = SpawnMerchantAt(World, FVector(900.0, 0.0, 0.0));
	// Steht am naechsten, laesst aber nur 50 cm zu: wer nicht in der eigenen
	// Reichweite steht, zaehlt nicht - egal wie nah er waere.
	AWiesbadenStoreMerchant* TooShortRange = SpawnMerchantAt(World, FVector(100.0, 0.0, 0.0));

	if (TestNotNull(TEXT("Haendler gespawnt"), Near) && Far && TooShortRange)
	{
		Near->InteractRangeCm = 350.0f;
		Far->InteractRangeCm = 1000.0f;
		TooShortRange->InteractRangeCm = 50.0f;

		const TArray<AWiesbadenStoreMerchant*> Candidates{Near, Far, TooShortRange};

		TestTrue(TEXT("naechster Haendler in eigener Reichweite gewinnt"),
			AWiesbadenGameMode::PickMerchantInReach(Candidates, Foot) == Near);

		TestTrue(TEXT("ausserhalb aller Reichweiten kein Treffer"),
			AWiesbadenGameMode::PickMerchantInReach(Candidates, FVector(20000.0, 0.0, 0.0))
				== nullptr);

		const TArray<AWiesbadenStoreMerchant*> Empty;
		TestTrue(TEXT("leere Kandidatenliste liefert nullptr"),
			AWiesbadenGameMode::PickMerchantInReach(Empty, Foot) == nullptr);

		const TArray<AWiesbadenStoreMerchant*> WithNull{Near, nullptr};
		TestTrue(TEXT("Null-Eintrag wird uebersprungen"),
			AWiesbadenGameMode::PickMerchantInReach(WithNull, Foot) == Near);
	}

	World->DestroyWorld(false);
	return true;
}
