// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Store/WiesbadenStoreTypes.h"
#include "Store/WiesbadenStoreLoader.h"
#include "Store/WiesbadenStore.h"

// Katalog-Loader: gueltiges JSON -> korrekte Eintraege; kaputt -> leer + Fehler;
// Eintrag ohne Id -> uebersprungen + Fehler.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoreLoaderTest,
	"WiesbadenReal.Store.Loader",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FStoreLoaderTest::RunTest(const FString& Parameters)
{
	// -- Gueltig --
	const FString Good = TEXT(
		"{ \"unlocks\": ["
		"  { \"id\": \"kurierlizenz\", \"title\": \"Kurierlizenz\", \"beschreibung\": \"Mehr Praemie\", \"kosten\": 200 },"
		"  { \"id\": \"stadtplan\", \"title\": \"Stadtplan\", \"kosten\": 150 } ] }");
	FStoreLoadResult R = FWiesbadenStoreLoader::ParseItems(Good);
	TestTrue(TEXT("gueltig: geparst"), R.bParsed);
	TestEqual(TEXT("gueltig: 2 Eintraege"), R.Items.Num(), 2);
	if (R.Items.Num() == 2)
	{
		TestEqual(TEXT("Id0"), R.Items[0].Id.ToString(), FString(TEXT("kurierlizenz")));
		TestEqual(TEXT("Titel0"), R.Items[0].Title, FString(TEXT("Kurierlizenz")));
		TestEqual(TEXT("Beschreibung0"), R.Items[0].Beschreibung, FString(TEXT("Mehr Praemie")));
		TestEqual(TEXT("Kosten0"), R.Items[0].Kosten, 200);
		TestEqual(TEXT("Kosten1"), R.Items[1].Kosten, 150);
	}

	// -- Kaputt --
	FStoreLoadResult Bad = FWiesbadenStoreLoader::ParseItems(TEXT("{ kein json"));
	TestFalse(TEXT("kaputt: nicht geparst"), Bad.bParsed);
	TestEqual(TEXT("kaputt: 0 Eintraege"), Bad.Items.Num(), 0);
	TestTrue(TEXT("kaputt: Fehler gemeldet"), Bad.Errors.Num() > 0);

	// -- Eintrag ohne Id -> uebersprungen, Container bleibt geparst --
	const FString NoId = TEXT("{ \"unlocks\": [ { \"title\": \"X\", \"kosten\": 10 } ] }");
	FStoreLoadResult NR = FWiesbadenStoreLoader::ParseItems(NoId);
	TestTrue(TEXT("noid: geparst"), NR.bParsed);
	TestEqual(TEXT("noid: 0 Eintraege (uebersprungen)"), NR.Items.Num(), 0);
	TestTrue(TEXT("noid: Fehler geloggt"), NR.Errors.Num() > 0);

	return true;
}

// Kauf-Bewertung + Lizenz-Effekt (rein, ohne Welt/Save).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStorePurchaseTest,
	"WiesbadenReal.Store.Purchase",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FStorePurchaseTest::RunTest(const FString& Parameters)
{
	FStoreItem Item;
	Item.Id = FName(TEXT("x"));
	Item.Kosten = 200;

	// Genug Guthaben, nicht besessen -> Kauf, Restbetrag ausgewiesen.
	FPurchaseEvaluation E = FWiesbadenStore::Evaluate(Item, 250, false);
	TestTrue(TEXT("genug: Erfolg"), E.Outcome == EPurchaseOutcome::Success);
	TestEqual(TEXT("genug: Rest 50"), E.NewGuthaben, 50);

	// Exakt gedeckt -> Kauf, Rest 0.
	E = FWiesbadenStore::Evaluate(Item, 200, false);
	TestTrue(TEXT("exakt: Erfolg"), E.Outcome == EPurchaseOutcome::Success);
	TestEqual(TEXT("exakt: Rest 0"), E.NewGuthaben, 0);

	// Zu teuer -> kein Kauf, Guthaben unveraendert.
	E = FWiesbadenStore::Evaluate(Item, 199, false);
	TestTrue(TEXT("zu teuer: abgelehnt"), E.Outcome == EPurchaseOutcome::NotEnoughGuthaben);
	TestEqual(TEXT("zu teuer: unveraendert"), E.NewGuthaben, 199);

	// Schon besessen -> kein Zweitkauf, Guthaben unveraendert.
	E = FWiesbadenStore::Evaluate(Item, 1000, true);
	TestTrue(TEXT("besessen: abgelehnt"), E.Outcome == EPurchaseOutcome::AlreadyOwned);
	TestEqual(TEXT("besessen: unveraendert"), E.NewGuthaben, 1000);

	// Lizenz-Bonus: +50% abgerundet, sonst unveraendert.
	TestEqual(TEXT("ohne Lizenz"), FWiesbadenStore::ApplyLicenseBonus(300, false), 300);
	TestEqual(TEXT("mit Lizenz +50%"), FWiesbadenStore::ApplyLicenseBonus(300, true), 450);
	TestEqual(TEXT("mit Lizenz abgerundet"), FWiesbadenStore::ApplyLicenseBonus(255, true), 382);
	TestTrue(TEXT("Lizenz-Id gesetzt"), FWiesbadenStore::KurierlizenzId() != NAME_None);

	return true;
}
