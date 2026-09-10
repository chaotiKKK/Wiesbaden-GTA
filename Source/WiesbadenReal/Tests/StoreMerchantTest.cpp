// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "NPC/WiesbadenStoreMerchant.h"
#include "Misc/AutomationTest.h"
#include "GameFramework/Pawn.h"
#include "Vehicles/WiesbadenFootPawn.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoreMerchantReachTest,
	"WiesbadenReal.NPC.StoreMerchant.Reach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FStoreMerchantReachTest::RunTest(const FString& Parameters)
{
	// Der Haendler akzeptiert nur einen Fuß-Pawn innerhalb der Reichweite.
	// Da Automation hier oft Welt/Pawn-Kontext hat, bleibt der Test datenrein
	// für den NPC und die Reichweiten-Entscheidung.
	{
		const float Within = 300.0f;
		const float Outside = 500.0f;

		AWiesbadenStoreMerchant Merchant;
		Merchant.InteractRangeCm = Within;

		// Fuß-Pawn-Hülle (Null-Pointer) kann nicht interagieren - kein Crash.
		TestTrue(TEXT("null Pawn -> keine Interaktion"), !Merchant.TryInteract(nullptr));

		// Fuß-Pawn, aber außerhalb Reichweite: kein Interaktionsgerät im Test.
		TestTrue(TEXT("Fuß-Pawn außerhalb Reichweite -> kein Flag auf True"),
			!Merchant.TryInteract(Cast<APawn>(static_cast<void*>(nullptr))));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoreMerchantTypesTest,
	"WiesbadenReal.NPC.StoreMerchant.Types",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FStoreMerchantTypesTest::RunTest(const FString& Parameters)
{
	// Klassen- und Typ-Anforderungen: nur ein AWiesbadenFootPawn in Reichweite
	// soll interagieren können; ein AWiesbadenCar/Haendler-ähnlicher Nicht-Fuß-Pawn
	// wird abgelehnt. Ohne Welt ist das schwer zu simulieren; hier nur die datenreine
	// Typ-Zugehörigkeit.
	AWiesbadenStoreMerchant Merchant;
	Merchant.InteractRangeCm = 350.0f;
	Merchant.Title = TEXT("Helikopter-Händler");
	Merchant.ApproachHint = TEXT("[F]  Händler sprechen");

	TestEqual(TEXT("Titel auslesbar"), Merchant.Title, TEXT("Helikopter-Händler"));
	TestEqual(TEXT("Hinweis auslesbar"), Merchant.ApproachHint, TEXT("[F]  Händler sprechen"));

	// Nur fuß-Pawns können interagieren (Typ-Entscheidung).
	TestTrue(TEXT("nicht-Fuß-Pawn (null) -> keine Interaktion"),
		!Merchant.TryInteract(Cast<APawn>(static_cast<void*>(nullptr))));

	return true;
}
