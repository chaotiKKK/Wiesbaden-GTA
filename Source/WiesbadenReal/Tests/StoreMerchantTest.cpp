// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "NPC/WiesbadenStoreMerchant.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoreMerchantTypesTest,
	"WiesbadenReal.NPC.StoreMerchant.Types",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FStoreMerchantTypesTest::RunTest(const FString& Parameters)
{
	// Datenreine Typ-/Beschriftungs-Checks, ohne Welt.
	AWiesbadenStoreMerchant* Merchant = NewObject<AWiesbadenStoreMerchant>();
	Merchant->InteractRangeCm = 350.0f;
	Merchant->Title = TEXT("Helikopter-Händler");
	Merchant->ApproachHint = TEXT("[F]  Händler sprechen");

	TestEqual(TEXT("Titel auslesbar"), Merchant->Title, TEXT("Helikopter-Händler"));
	TestEqual(TEXT("Hinweis auslesbar"), Merchant->ApproachHint, TEXT("[F]  Händler sprechen"));
	TestTrue(TEXT("Interaktionsreichweite positiv"), Merchant->InteractRangeCm > 0.0f);

	// Kein Fuß-Pawn-Kontext im Test -> keine echte Interaktion, kein Crash.
	AWiesbadenStoreMerchant* Empty = NewObject<AWiesbadenStoreMerchant>();
	Empty->InteractRangeCm = 350.0f;
	Empty->Title = TEXT("Nordfriedhof-Haendler");
	TestEqual(TEXT("weiterer Titel auslesbar"), Empty->Title, TEXT("Nordfriedhof-Haendler"));
	TestTrue(TEXT("leere Marker-Anlage bleibt versteckt"), Empty->IsMarkerHidden());

	return true;
}
