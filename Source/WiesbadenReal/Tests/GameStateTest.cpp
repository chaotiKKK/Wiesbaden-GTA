// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/WiesbadenGameStateSubsystem.h"

// Kontostand-Arithmetik: Add summiert (nie < 0), Spend bucht nur bei Deckung ab.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameStateEconomyTest,
	"WiesbadenReal.GameState.Economy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGameStateEconomyTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenEconomy;

	// ApplyDelta
	TestEqual(TEXT("add: 100+250"), ApplyDelta(100, 250), 350);
	TestEqual(TEXT("add: 0+0"), ApplyDelta(0, 0), 0);
	TestEqual(TEXT("add negativ klemmt >=0"), ApplyDelta(100, -250), 0);
	TestEqual(TEXT("add negativ teilweise"), ApplyDelta(300, -100), 200);

	// TrySpend
	int32 New = -1;
	TestTrue(TEXT("spend gedeckt"), TrySpend(300, 250, New));
	TestEqual(TEXT("spend gedeckt Rest 50"), New, 50);

	New = -1;
	TestTrue(TEXT("spend exakt gedeckt"), TrySpend(250, 250, New));
	TestEqual(TEXT("spend exakt Rest 0"), New, 0);

	New = -1;
	TestFalse(TEXT("spend ungedeckt"), TrySpend(100, 250, New));
	TestEqual(TEXT("spend ungedeckt unveraendert"), New, 100);

	// InitialGuthaben: frisches Spiel -> Startguthaben; mit Speicherstand -> dessen Wert.
	TestEqual(TEXT("frisch -> Startguthaben"), InitialGuthaben(false, 0), StartGuthaben);
	TestEqual(TEXT("frisch ignoriert Save-Wert"), InitialGuthaben(false, 999), StartGuthaben);
	TestEqual(TEXT("mit Save -> Save-Wert"), InitialGuthaben(true, 250), 250);
	TestEqual(TEXT("mit Save 0 -> 0"), InitialGuthaben(true, 0), 0);

	return true;
}
