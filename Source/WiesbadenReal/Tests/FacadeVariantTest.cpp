// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/BuildingGenerator.h"
#include "GIS/OSMTypes.h"
#include "Misc/AutomationTest.h"

// Variantenwerte (EFacadeVariant in BuildingGenerator.cpp, anonym):
//   0 Putz, 1 Backstein, 2 Sandstein, 3 Glas, 4 Beton, 5 Fachwerk.
namespace
{
	constexpr int32 V_Plaster = 0;
	constexpr int32 V_Brick = 1;
	constexpr int32 V_Sandstone = 2;
	constexpr int32 V_Glass = 3;
	constexpr int32 V_Concrete = 4;

	int32 Variant(const TMap<FName, FString>& Tags, EOSMBuildingType Type, int64 SeedId)
	{
		return UBuildingGenerator::SelectMaterialVariant(Tags, Type, SeedId);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFacadeVariantSpreadTest,
	"WiesbadenReal.GIS.FacadeVariant.Spread",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FFacadeVariantSpreadTest::RunTest(const FString& Parameters)
{
	const TMap<FName, FString> NoTags;

	// 1) Wohnbauten OHNE Tags streuen ueber mehrere Varianten. Frueher fielen
	//    ALLE auf Putz - ein ganzer Block sah gleich aus.
	TSet<int32> Seen;
	for (int64 Id = 1; Id <= 200; ++Id)
	{
		Seen.Add(Variant(NoTags, EOSMBuildingType::Residential, Id));
	}
	TestTrue(TEXT("Wohnbauten streuen ueber mindestens drei Varianten"), Seen.Num() >= 3);
	TestFalse(TEXT("Nicht alle Wohnbauten sind identisch"), Seen.Num() == 1);

	// 2) Benachbarte Gebaeude (verschiedene Ids) unterscheiden sich oft. Ueber
	//    aufeinanderfolgende Ids muss die Mehrheit der Nachbarn abweichen.
	int32 Differ = 0;
	for (int64 Id = 1; Id < 200; ++Id)
	{
		if (Variant(NoTags, EOSMBuildingType::Residential, Id)
			!= Variant(NoTags, EOSMBuildingType::Residential, Id + 1))
		{
			++Differ;
		}
	}
	TestTrue(TEXT("Mehrheit benachbarter Wohnbauten weicht ab"), Differ > 120);

	// 3) Deterministisch: dieselbe Id liefert immer dieselbe Variante.
	for (int64 Id = 1; Id <= 50; ++Id)
	{
		TestEqual(TEXT("Gleiche Id -> gleiche Variante"),
			Variant(NoTags, EOSMBuildingType::Residential, Id),
			Variant(NoTags, EOSMBuildingType::Residential, Id));
	}

	// 4) Explizites Material-Tag bleibt authoritativ - KEINE Streuung.
	TMap<FName, FString> Brick;
	Brick.Add(TEXT("building:material"), TEXT("brick"));
	for (int64 Id = 1; Id <= 30; ++Id)
	{
		TestEqual(TEXT("Backstein-Tag bleibt Backstein"),
			Variant(Brick, EOSMBuildingType::Residential, Id), V_Brick);
	}

	// 5) Eindeutige Nicht-Wohn-Typen bleiben einheitlich.
	for (int64 Id = 1; Id <= 30; ++Id)
	{
		TestEqual(TEXT("Bueroturm bleibt Glas"),
			Variant(NoTags, EOSMBuildingType::Office, Id), V_Glass);
		TestEqual(TEXT("Parkhaus bleibt Beton"),
			Variant(NoTags, EOSMBuildingType::Garage, Id), V_Concrete);
		TestEqual(TEXT("Kirche bleibt Sandstein"),
			Variant(NoTags, EOSMBuildingType::Church, Id), V_Sandstone);
	}

	// 6) Epochengerecht: Gruenderzeit-Wohnbauten (1850-1920) streuen nur ueber
	//    Sandstein/Putz/Backstein, nie Glas oder Beton.
	TMap<FName, FString> Gruenderzeit;
	Gruenderzeit.Add(TEXT("start_date"), TEXT("1895"));
	TSet<int32> EraSeen;
	for (int64 Id = 1; Id <= 200; ++Id)
	{
		const int32 Var = Variant(Gruenderzeit, EOSMBuildingType::Residential, Id);
		EraSeen.Add(Var);
		TestTrue(TEXT("Gruenderzeit ohne Glas/Beton"),
			Var == V_Plaster || Var == V_Sandstone || Var == V_Brick);
	}
	TestTrue(TEXT("Gruenderzeit streut ueber mehrere Varianten"), EraSeen.Num() >= 2);

	return true;
}
