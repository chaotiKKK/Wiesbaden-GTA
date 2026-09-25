// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "GIS/BuildingGenerator.h"
#include "Missions/WiesbadenDennoDelivery.h"
#include "Missions/WiesbadenMissionRunner.h"
#include "World/WiesbadenDennoShop.h"

namespace
{
	FDennoDeliveryAddress At(const TCHAR* Address, double EastMeters)
	{
		return { Address, FVector(EastMeters * 100.0, 0.0, 0.0) };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoDeliveryPayoutTest,
	"WiesbadenReal.Missions.DennoDelivery.Payout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoDeliveryPayoutTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenDennoDelivery;
	// Je weiter weg, desto mehr Geld: 40 EUR Grund + 60 EUR je km, auf 5 gerundet.
	TestEqual(TEXT("Grundbetrag"), ComputePayout(0.0), 40);
	TestEqual(TEXT("1 km"), ComputePayout(100000.0), 100);
	TestEqual(TEXT("2,5 km"), ComputePayout(250000.0), 190);
	TestEqual(TEXT("4 km (Obergrenze des Bands)"), ComputePayout(MaxDistanceCm), 280);
	int32 Previous = ComputePayout(0.0);
	for (double Cm = 0.0; Cm <= MaxDistanceCm; Cm += 1000.0)
	{
		const int32 Now = ComputePayout(Cm);
		TestTrue(FString::Printf(TEXT("Nie weniger fuer mehr Weg (%.0f m)"), Cm / 100.0), Now >= Previous);
		TestEqual(FString::Printf(TEXT("Auf 5 EUR gerundet (%.0f m)"), Cm / 100.0), Now % 5, 0);
		Previous = Now;
	}
	TestTrue(TEXT("3 km bringt deutlich mehr als 500 m"), ComputePayout(300000.0) > ComputePayout(50000.0) + 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoDeliveryAddressesTest,
	"WiesbadenReal.Missions.DennoDelivery.Addresses",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoDeliveryAddressesTest::RunTest(const FString& Parameters)
{
	// Nur Gebaeude mit "Strasse Hausnummer", jede Adresse einmal.
	auto Building = [](const TCHAR* Address, double X)
	{
		FGeneratedBuilding B;
		B.Address = Address;
		B.Centroid = FVector(X, 0.0, 0.0);
		return B;
	};
	const TArray<FGeneratedBuilding> Buildings = {
		Building(TEXT(""), 0.0),                       // ohne Adresse
		Building(TEXT("Platter Straße 144"), 100.0),
		Building(TEXT("Sedanplatz"), 200.0),           // ohne Hausnummer
		Building(TEXT("Platter Straße 144"), 300.0),   // doppelt (zweiter Hausteil)
		Building(TEXT("Adolfsallee 12a"), 400.0),
		Building(TEXT("  Wilhelmstraße 7  "), 500.0) };
	const TArray<FDennoDeliveryAddress> Addresses = WiesbadenDennoDelivery::CollectAddresses(Buildings);
	TestEqual(TEXT("Drei belieferbare Adressen"), Addresses.Num(), 3);
	if (Addresses.Num() == 3)
	{
		TestEqual(TEXT("Platter Str. 144 einmal, erster Hausteil"), Addresses[0].Location.X, 100.0);
		TestEqual(TEXT("Hausnummer mit Buchstabe zaehlt"), Addresses[1].Address, FString(TEXT("Adolfsallee 12a")));
		TestEqual(TEXT("Leerraum abgeschnitten"), Addresses[2].Address, FString(TEXT("Wilhelmstraße 7")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoDeliveryPickTest,
	"WiesbadenReal.Missions.DennoDelivery.RandomAddress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoDeliveryPickTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenDennoDelivery;
	// Zu nah (100 m) und zu weit (10 km) werden nie gezogen; die beiden im Band
	// kommen BEIDE vor (zufaellig, nicht immer dieselbe) - und nie an Index 0.
	const TArray<FDennoDeliveryAddress> Addresses = {
		At(TEXT("Nah 1"), 100.0), At(TEXT("Mittel 2"), 1000.0),
		At(TEXT("Weit 3"), 10000.0), At(TEXT("Band 4"), 2500.0) };
	const FVector Shop = FVector::ZeroVector;
	TMap<int32, int32> Hits;
	FRandomStream Random(4711);
	for (int32 I = 0; I < 400; ++I)
	{
		++Hits.FindOrAdd(PickAddress(Addresses, Shop, Random));
	}
	TestFalse(TEXT("Zu nah nie"), Hits.Contains(0));
	TestFalse(TEXT("Zu weit nie"), Hits.Contains(2));
	TestFalse(TEXT("Immer eine gefunden"), Hits.Contains(INDEX_NONE));
	TestTrue(TEXT("1 km kommt vor"), Hits.FindRef(1) > 100);
	TestTrue(TEXT("2,5 km kommt vor"), Hits.FindRef(3) > 100);

	FRandomStream A(12), B(12);
	TestEqual(TEXT("Gleicher Seed, gleiche Adresse"), PickAddress(Addresses, Shop, A), PickAddress(Addresses, Shop, B));

	FRandomStream C(3);
	const TSet<int32> Excluded = { 1 };
	for (int32 I = 0; I < 50; ++I)
	{
		TestEqual(TEXT("Verworfene Adresse (keine Strasse) wird nicht erneut gezogen"),
			PickAddress(Addresses, Shop, C, Excluded), 3);
	}
	const TArray<FDennoDeliveryAddress> NoneInBand = { At(TEXT("Nah"), 50.0), At(TEXT("Weit"), 9000.0) };
	TestEqual(TEXT("Nichts im Band -> keine Adresse"), PickAddress(NoneInBand, Shop, C), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoDeliveryMissionTest,
	"WiesbadenReal.Missions.DennoDelivery.Mission",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoDeliveryMissionTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenDennoDelivery;
	FDennoDeliveryJob Job;
	Job.Address = TEXT("Adolfsallee 12");
	Job.Cargo = TEXT("Kuchenpaket");
	Job.DropPoint = FVector(150000.0, 20000.0, 0.0);
	Job.DistanceCm = 151000.0;
	Job.Payout = ComputePayout(Job.DistanceCm);
	const FVector ShopFront(0.0, 0.0, 0.0);
	FMission Mission = BuildMission(Job, ShopFront, 7);

	TestEqual(TEXT("Id"), Mission.Id, FName(TEXT("denno_lieferung_7")));
	TestTrue(TEXT("Titel nennt die Adresse"), Mission.Title.Contains(Job.Address));
	TestTrue(TEXT("Frist aus der Route (auto)"), Mission.IsAutoDeadline());
	TestEqual(TEXT("Belohnung = Bezahlung nach Entfernung"), Mission.Reward.Guthaben, Job.Payout);
	if (!TestEqual(TEXT("Zwei Ziele"), Mission.Objectives.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("Erst bei Denno abholen"), Mission.Objectives[0].Type == EObjectiveType::PickUpCargo);
	TestTrue(TEXT("Dann an der Adresse abgeben"), Mission.Objectives[1].Type == EObjectiveType::DropOffCargo);
	TestEqual(TEXT("Abgabe am Strassenpunkt vor der Adresse"), Mission.Objectives[1].Location, Job.DropPoint);
	TestTrue(TEXT("Abgabe-Text nennt Ware und Adresse"),
		Mission.Objectives[1].Label.Contains(Job.Cargo) && Mission.Objectives[1].Label.Contains(Job.Address));

	// Ablauf wie im Subsystem: am Laden aufnehmen, an der Adresse abgeben ->
	// Auszahlung. Ohne Abholung zaehlt die Ankunft an der Adresse nicht.
	Mission.DeadlineSeconds = 0.0;   // Frist spielt hier keine Rolle
	FMissionContext Ctx;
	Ctx.PlayerLocation = Job.DropPoint;
	TestFalse(TEXT("Direkt zur Adresse ohne Ware: nichts passiert"),
		FWiesbadenMissionRunner::Step(Mission, 0, Ctx).bAdvanced);
	Ctx.PlayerLocation = ShopFront + FVector(0.0, -400.0, 0.0);
	const FMissionProgressResult Picked = FWiesbadenMissionRunner::Step(Mission, 0, Ctx);
	TestTrue(TEXT("Vor dem Laden: Ware aufgenommen"), Picked.bAdvanced && Picked.NextObjectiveIndex == 1);
	Ctx.bCarryingCargo = true;
	Ctx.PlayerLocation = Job.DropPoint + FVector(600.0, 0.0, 0.0);
	const FMissionProgressResult Dropped = FWiesbadenMissionRunner::Step(Mission, 1, Ctx);
	TestTrue(TEXT("An der Adresse abgegeben: Auftrag erledigt"), Dropped.bMissionCompleted);
	TestEqual(TEXT("Ausgezahlt wird die Bezahlung"), Dropped.GuthabenAwarded, Job.Payout);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoDeliveryReachTest,
	"WiesbadenReal.World.DennoShop.DeliveryReach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoDeliveryReachTest::RunTest(const FString& Parameters)
{
	// Laden-lokal: X laengs der Front, +Y ins Haus, -Y zur Strasse.
	TestTrue(TEXT("Auf dem Gehweg vor dem Cafe"), AWiesbadenDennoShop::IsInDeliveryReach(FVector(300.0, -300.0, 0.0)));
	TestTrue(TEXT("Vor dem Friseur (gleicher Laden)"), AWiesbadenDennoShop::IsInDeliveryReach(FVector(-500.0, -200.0, 20.0)));
	TestFalse(TEXT("Im Laden hinter der Scheibe"), AWiesbadenDennoShop::IsInDeliveryReach(FVector(300.0, 250.0, 0.0)));
	TestFalse(TEXT("Auf der anderen Strassenseite"), AWiesbadenDennoShop::IsInDeliveryReach(FVector(300.0, -1200.0, 0.0)));
	TestFalse(TEXT("Vor dem Nachbarhaus"), AWiesbadenDennoShop::IsInDeliveryReach(FVector(1000.0, -300.0, 0.0)));
	TestFalse(TEXT("Auf dem Dach darueber"), AWiesbadenDennoShop::IsInDeliveryReach(FVector(300.0, -300.0, 900.0)));
	TestTrue(TEXT("Hinweis nennt die Taste"), AWiesbadenDennoShop::BuildDeliveryPrompt(false).StartsWith(TEXT("F")));
	TestTrue(TEXT("Laeuft schon ein Auftrag, sagt Denno das"),
		AWiesbadenDennoShop::BuildDeliveryPrompt(true).Contains(TEXT("laufenden Auftrag")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoDeliveryCustomerSpotTest,
	"WiesbadenReal.Missions.DennoDelivery.CustomerSpot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoDeliveryCustomerSpotTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenDennoDelivery;
	// Der Kunde steht zwischen Fahrspur und Haus auf dem Gehweg.
	const FVector Drop(1000.0, 2000.0, 300.0);
	const FVector House = Drop + FVector(0.0, 1500.0, 0.0);   // 15 m hinter der Spur
	const FVector Spot = ComputeCustomerSpot(Drop, House);
	const double FromDrop = FVector::Dist2D(Spot, Drop);
	TestTrue(FString::Printf(TEXT("2,5 bis 6,5 m von der Spur (%.0f cm)"), FromDrop), FromDrop >= 250.0 && FromDrop <= 650.0);
	TestTrue(TEXT("Richtung Haus, nicht auf die andere Strassenseite"), Spot.Y > Drop.Y);
	TestEqual(TEXT("Hoehe vom Abgabepunkt (Boden holt der Actor)"), Spot.Z, Drop.Z);

	// Steht das Haus dicht an der Strasse, nie ins Gebaeude hinein.
	const FVector NearHouse = Drop + FVector(320.0, 0.0, 0.0);
	const FVector NearSpot = ComputeCustomerSpot(Drop, NearHouse);
	TestTrue(TEXT("Dichtes Haus: Kunde bleibt davor"), FVector::Dist2D(NearSpot, Drop) < 320.0);
	TestTrue(TEXT("Dichtes Haus: trotzdem neben der Spur"), FVector::Dist2D(NearSpot, Drop) >= 150.0);

	// Adresse genau auf dem Abgabepunkt: trotzdem ein fester Platz (kein NaN).
	const FVector Same = ComputeCustomerSpot(Drop, Drop);
	TestFalse(TEXT("Keine ungueltige Position"), Same.ContainsNaN());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoDeliveryTipTest,
	"WiesbadenReal.Missions.DennoDelivery.Tip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoDeliveryTipTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenDennoDelivery;
	// Puenktlichkeit wird belohnt: je mehr von der Frist uebrig, desto mehr.
	const double Deadline = 400.0;
	TestEqual(TEXT("Mehr als die Haelfte uebrig: 25 %"), ComputeTip(200, 300.0, Deadline).Amount, 50);
	TestEqual(TEXT("Ein Viertel bis Haelfte: 15 %"), ComputeTip(200, 150.0, Deadline).Amount, 30);
	TestEqual(TEXT("Knapp: 5 %"), ComputeTip(200, 20.0, Deadline).Amount, 10);
	TestEqual(TEXT("Keine Restzeit: nichts"), ComputeTip(200, 0.0, Deadline).Amount, 0);
	TestEqual(TEXT("Ohne Frist: nichts"), ComputeTip(200, 100.0, 0.0).Amount, 0);
	TestEqual(TEXT("Kleiner Auftrag, knapp: mindestens 1 EUR"), ComputeTip(10, 5.0, Deadline).Amount, 1);
	int32 Previous = 0;
	for (double Remaining = 1.0; Remaining <= Deadline; Remaining += 10.0)
	{
		const int32 Now = ComputeTip(235, Remaining, Deadline).Amount;
		TestTrue(FString::Printf(TEXT("Schneller nie weniger (Rest %.0f s)"), Remaining), Now >= Previous);
		Previous = Now;
	}
	TestNotEqual(TEXT("Flott und knapp sagen Verschiedenes"),
		ComputeTip(200, 300.0, Deadline).Thanks, ComputeTip(200, 20.0, Deadline).Thanks);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoDeliveryWalkHomeTest,
	"WiesbadenReal.Missions.DennoDelivery.WalkHome",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoDeliveryWalkHomeTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenDennoDelivery;
	const FVector Spot(1000.0, 2000.0, 50.0);
	const FVector House = Spot + FVector(0.0, 1500.0, 900.0);   // Schwerpunkt 15 m ins Haus

	// Wand gemessen: knapp davor stehen bleiben, Richtung Haus.
	const FVector Door = ComputeDoorPoint(Spot, House, 400.0);
	TestTrue(TEXT("Tuer knapp vor der Wand"),
		FMath::IsNearlyEqual(FVector::Dist2D(Spot, Door), 400.0 - DoorWallGapCm, 0.5));
	TestTrue(TEXT("Tuer liegt Richtung Haus"), Door.Y > Spot.Y && FMath::IsNearlyEqual(Door.X, Spot.X, 0.5));
	TestEqual(TEXT("Hoehe vom Warteplatz (Boden holt der Actor)"), Door.Z, Spot.Z);

	// Wand direkt hinter ihm: an Ort und Stelle, nie rueckwaerts.
	TestTrue(TEXT("Wand dichter als der Abstand: bleibt stehen"),
		FVector::Dist2D(ComputeDoorPoint(Spot, House, 10.0), Spot) < 0.5);

	// Kein Wandtreffer: begrenzt, nie in den Schwerpunkt hinein.
	TestTrue(TEXT("Ohne Wand hoechstens DoorFallbackMaxCm"),
		FVector::Dist2D(ComputeDoorPoint(Spot, House, -1.0), Spot) <= DoorFallbackMaxCm + 0.5);
	const FVector NearHouse = Spot + FVector(300.0, 0.0, 0.0);
	TestTrue(TEXT("Ohne Wand 1,5 m vor dem Schwerpunkt"),
		FMath::IsNearlyEqual(FVector::Dist2D(ComputeDoorPoint(Spot, NearHouse, -1.0), Spot), 150.0, 0.5));
	TestFalse(TEXT("Schwerpunkt auf dem Warteplatz: keine ungueltige Position"),
		ComputeDoorPoint(Spot, Spot, -1.0).ContainsNaN());

	// Gangbild: beginnt in der Wartepose und laeuft die vier Posen der Reihe nach.
	TestEqual(TEXT("Stehend = Pose 1 (wie beim Warten)"), ComputeWalkPose(0.0), 1);
	const int32 Expected[] = { 1, 2, 3, 0, 1, 2, 3, 0 };
	for (int32 Step = 0; Step < UE_ARRAY_COUNT(Expected); ++Step)
	{
		const double Walked = (Step + 0.5) * CustomerStrideCm / 4.0;
		TestEqual(FString::Printf(TEXT("Pose nach %.0f cm"), Walked), ComputeWalkPose(Walked), Expected[Step]);
	}
	TestEqual(TEXT("Negative Strecke = Wartepose"), ComputeWalkPose(-20.0), 1);
	return true;
}
