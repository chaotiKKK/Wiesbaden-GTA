// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/WiesbadenSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Missions/WiesbadenCourierStats.h"
#include "Missions/WiesbadenDennoDelivery.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCourierStatsRecordTest,
	"WiesbadenReal.Missions.CourierStats.Record",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCourierStatsRecordTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenCourierStats;
	FWbCourierStats Stats;
	TestEqual(TEXT("Ohne Lieferung keine Puenktlichkeit"), PunctualityPercent(Stats), -1);

	TestTrue(TEXT("Erstes Trinkgeld ist ein Rekord"), RecordDelivery(Stats, 59, /*bFast=*/true));
	TestFalse(TEXT("Kleineres Trinkgeld: kein Rekord"), RecordDelivery(Stats, 12, false));
	TestFalse(TEXT("Gleich hohes Trinkgeld: kein neuer Rekord"), RecordDelivery(Stats, 59, true));
	TestTrue(TEXT("Hoeheres Trinkgeld: Rekord"), RecordDelivery(Stats, 80, false));
	TestFalse(TEXT("Negatives Trinkgeld zaehlt als 0"), RecordDelivery(Stats, -5, false));
	TestEqual(TEXT("Fuenf geliefert"), Stats.Delivered, 5);
	TestEqual(TEXT("Zwei flott"), Stats.Fast, 2);
	TestEqual(TEXT("Trinkgeld summiert"), Stats.TipTotal, 59 + 12 + 59 + 80);
	TestEqual(TEXT("Rekord"), Stats.TipRecord, 80);
	TestEqual(TEXT("Alles puenktlich"), PunctualityPercent(Stats), 100);

	RecordMissed(Stats);
	TestEqual(TEXT("Verpasst gezaehlt"), Stats.Missed, 1);
	TestEqual(TEXT("5 von 6 = 83 % (abgerundet)"), PunctualityPercent(Stats), 83);
	TestEqual(TEXT("Verpasst aendert Lieferungen nicht"), Stats.Delivered, 5);

	// Nie "100 %", solange eine verpasst ist.
	FWbCourierStats Many;
	Many.Delivered = 999;
	Many.Missed = 1;
	TestEqual(TEXT("999 von 1000 ist nicht 100 %"), PunctualityPercent(Many), 99);
	FWbCourierStats OnlyMissed;
	RecordMissed(OnlyMissed);
	TestEqual(TEXT("Nur verpasst: 0 %"), PunctualityPercent(OnlyMissed), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCourierStatsDescribeTest,
	"WiesbadenReal.Missions.CourierStats.Describe",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCourierStatsDescribeTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenCourierStats;
	const FString First = Describe(FWbCourierStats());
	TestTrue(TEXT("Leere Bilanz begruesst die erste Lieferung"), First.Contains(TEXT("erste")));
	TestFalse(TEXT("Leere Bilanz nennt keine Prozent"), First.Contains(TEXT("%")));

	FWbCourierStats Stats;
	Stats.Delivered = 12;
	Stats.Missed = 1;
	Stats.Fast = 5;
	Stats.TipRecord = 59;
	const FString Line = Describe(Stats);
	TestEqual(TEXT("Volle Bilanz"), Line,
		FString(TEXT("Kurier-Bilanz: 12 geliefert, 92 % puenktlich, 5 flott - Trinkgeld-Rekord 59 EUR.")));
	TestFalse(TEXT("Einzeilig (der Hinweis haengt sie als eigene Zeile an)"), Line.Contains(TEXT("\n")));

	// Ohne Flotte und ohne Trinkgeld fallen die Zusaetze weg.
	FWbCourierStats Plain;
	Plain.Missed = 2;
	TestEqual(TEXT("Nur verpasst"), Describe(Plain),
		FString(TEXT("Kurier-Bilanz: 0 geliefert, 0 % puenktlich.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCourierStatsSaveTest,
	"WiesbadenReal.Missions.CourierStats.SaveRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCourierStatsSaveTest::RunTest(const FString& Parameters)
{
	// Im Speicher serialisieren - der echte Spielstand auf Platte bleibt unberuehrt.
	UWiesbadenSaveGame* Out = Cast<UWiesbadenSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UWiesbadenSaveGame::StaticClass()));
	if (!TestNotNull(TEXT("Spielstand-Objekt"), Out))
	{
		return false;
	}
	TestEqual(TEXT("Spielstand v3"), Out->SaveVersion, 3);
	TestEqual(TEXT("Frische Bilanz leer"), Out->CourierStats.Delivered, 0);
	Out->Guthaben = 1234;
	Out->CourierStats.Delivered = 7;
	Out->CourierStats.Missed = 2;
	Out->CourierStats.Fast = 3;
	Out->CourierStats.TipTotal = 210;
	Out->CourierStats.TipRecord = 59;

	TArray<uint8> Bytes;
	TestTrue(TEXT("Serialisiert"), UGameplayStatics::SaveGameToMemory(Out, Bytes));
	const UWiesbadenSaveGame* In = Cast<UWiesbadenSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Zurueckgelesen"), In))
	{
		return false;
	}
	TestEqual(TEXT("Guthaben bleibt"), In->Guthaben, 1234);
	TestEqual(TEXT("Geliefert"), In->CourierStats.Delivered, 7);
	TestEqual(TEXT("Verpasst"), In->CourierStats.Missed, 2);
	TestEqual(TEXT("Flott"), In->CourierStats.Fast, 3);
	TestEqual(TEXT("Trinkgeld gesamt"), In->CourierStats.TipTotal, 210);
	TestEqual(TEXT("Rekord"), In->CourierStats.TipRecord, 59);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCourierStatsHooksTest,
	"WiesbadenReal.Missions.CourierStats.DeliveryHooks",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCourierStatsHooksTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenDennoDelivery;
	// Nur Dennos Auftraege zaehlen in der Bilanz, nicht die Kurierjobs des Pools.
	FDennoDeliveryJob Job;
	Job.Address = TEXT("Wuerttembergstrasse 29");
	Job.Payout = 235;
	TestTrue(TEXT("Denno-Auftrag erkannt"), IsDeliveryMission(BuildMission(Job, FVector::ZeroVector, 3).Id));
	TestFalse(TEXT("Pool-Kurierjob nicht"), IsDeliveryMission(FName(TEXT("courier_proc_12"))));
	TestFalse(TEXT("Kein Auftrag"), IsDeliveryMission(NAME_None));

	// "Flott" = hoechste Trinkgeldstufe (mindestens die halbe Frist uebrig).
	TestTrue(TEXT("Halbe Frist uebrig: flott"), ComputeTip(200, 200.0, 400.0).bFast);
	TestFalse(TEXT("Ein Drittel uebrig: nicht flott"), ComputeTip(200, 133.0, 400.0).bFast);
	TestFalse(TEXT("Ohne Restzeit: nicht flott"), ComputeTip(200, 0.0, 400.0).bFast);
	return true;
}
