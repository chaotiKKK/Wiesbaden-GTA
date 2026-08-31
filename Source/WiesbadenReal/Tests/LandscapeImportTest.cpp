// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenWorldBuilder.h"
#include "LandscapeProxy.h"

/**
 * Regressionstest fuer den Landscape-Import (AWiesbadenWorldBuilder).
 *
 * HINTERGRUND: ALandscapeProxy::Import stellt zwei Bedingungen, die beide den
 * Editor per Assertion beenden, wenn sie verletzt werden - kein abfangbarer
 * Fehler, sondern ein sofortiger Absturz mitten im Stadtbau:
 *
 *   check(InImportHeightData.Num() == InImportMaterialLayerInfos.Num());
 *   const FGuid FinalLayerGuid = FGuid();
 *   const TArray<uint16>& HeightData = InImportHeightData.FindChecked(FinalLayerGuid);
 *
 * Beide Fehler sind einzeln aufgetreten:
 *   1. Layer-Map leer bei gefuellter Hoehen-Map -> Groessenpruefung schlaegt fehl.
 *   2. Beide Maps mit FGuid::NewGuid() geschluesselt -> Groessen stimmen, aber
 *      FindChecked(FGuid()) findet nichts.
 * Der zweite Fall ist heimtueckisch, weil er wie eine gueltige Korrektur des
 * ersten aussieht. Dieser Test prueft daher beide Invarianten getrennt.
 *
 * Datenrein und damit ohne Editor-Welt, Landscape-Actor oder geladenes Level
 * ausfuehrbar - genau deshalb ist die Map-Erzeugung als statische Funktion
 * aus CreateLandscapeFromTile herausgezogen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeImportTest,
	"WiesbadenReal.GIS.LandscapeImport",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeImportTest::RunTest(const FString& Parameters)
{
	// -- Testheightmap mit wiedererkennbaren Werten -------------------------
	constexpr int32 Grid = 8;

	TArray<uint16> Heightmap;
	Heightmap.SetNumUninitialized(Grid * Grid);
	for (int32 Index = 0; Index < Heightmap.Num(); ++Index)
	{
		Heightmap[Index] = static_cast<uint16>(1000 + Index);
	}

	TMap<FGuid, TArray<uint16>> HeightData;
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerInfos;

	AWiesbadenWorldBuilder::BuildLandscapeImportMaps(
		MoveTemp(Heightmap), HeightData, MaterialLayerInfos);

	// -- 1. Groessenpruefung der Engine ------------------------------------
	// Exakt die Bedingung aus check(...) in LandscapeEdit.cpp.
	TestEqual(TEXT("Hoehen-Map und Layer-Map sind gleich gross (Engine-check)"),
		HeightData.Num(), MaterialLayerInfos.Num());

	TestEqual(TEXT("Genau ein Hoehen-Eintrag"), HeightData.Num(), 1);
	TestEqual(TEXT("Genau ein Layer-Eintrag"), MaterialLayerInfos.Num(), 1);

	// -- 2. Schluessel ist der Default-Guid ---------------------------------
	// Die Engine sucht ausschliesslich mit FGuid(); ein NewGuid() wuerde die
	// Groessenpruefung bestehen und danach an FindChecked scheitern.
	const FGuid ExpectedKey = FGuid();

	TestFalse(TEXT("Der Import-Schluessel ist der (ungueltige) Default-Guid"),
		ExpectedKey.IsValid());
	TestTrue(TEXT("Hoehen-Map ist mit dem Default-Guid geschluesselt"),
		HeightData.Contains(ExpectedKey));
	TestTrue(TEXT("Layer-Map ist mit dem Default-Guid geschluesselt"),
		MaterialLayerInfos.Contains(ExpectedKey));

	// -- 3. Daten unveraendert uebernommen ---------------------------------
	if (const TArray<uint16>* StoredHeights = HeightData.Find(ExpectedKey))
	{
		TestEqual(TEXT("Heightmap vollstaendig uebernommen"), StoredHeights->Num(), Grid * Grid);

		if (StoredHeights->Num() == Grid * Grid)
		{
			TestEqual(TEXT("Erster Hoehenwert unveraendert"),
				static_cast<int32>((*StoredHeights)[0]), 1000);
			TestEqual(TEXT("Letzter Hoehenwert unveraendert"),
				static_cast<int32>((*StoredHeights)[Grid * Grid - 1]), 1000 + Grid * Grid - 1);
		}
	}
	else
	{
		AddError(TEXT("Hoehen-Map enthaelt keinen Eintrag unter dem Default-Guid."));
	}

	// Ohne Gewichtsmaps: der Eintrag existiert, ist aber leer. Beides ist
	// erforderlich - ein fehlender Eintrag stuerzt ab, ein gefuellter wuerde
	// Layer-Infos verlangen, die es noch nicht gibt.
	if (const TArray<FLandscapeImportLayerInfo>* StoredLayers = MaterialLayerInfos.Find(ExpectedKey))
	{
		TestEqual(TEXT("Layer-Eintrag ist vorhanden, aber leer"), StoredLayers->Num(), 0);
	}
	else
	{
		AddError(TEXT("Layer-Map enthaelt keinen Eintrag unter dem Default-Guid."));
	}

	// -- 4. Move-Semantik ---------------------------------------------------
	// Die Heightmap einer 4033x4033-Kachel belegt 32 MB; sie darf nicht kopiert
	// werden. Nach dem Verschieben muss die Quelle leer sein.
	TestEqual(TEXT("Quell-Heightmap wurde verschoben, nicht kopiert"), Heightmap.Num(), 0);

	// -- 5. Wiederholter Aufruf setzt zurueck -------------------------------
	// CreateLandscapeFromTile kann je Kachel mehrfach laufen; Restdaten aus
	// einem vorherigen Aufruf wuerden die Groessenpruefung verletzen.
	TArray<uint16> SecondHeightmap;
	SecondHeightmap.Init(42, 4);

	AWiesbadenWorldBuilder::BuildLandscapeImportMaps(
		MoveTemp(SecondHeightmap), HeightData, MaterialLayerInfos);

	TestEqual(TEXT("Zweiter Aufruf: weiterhin genau ein Hoehen-Eintrag"), HeightData.Num(), 1);
	TestEqual(TEXT("Zweiter Aufruf: Maps bleiben gleich gross"),
		HeightData.Num(), MaterialLayerInfos.Num());

	if (const TArray<uint16>* StoredHeights = HeightData.Find(ExpectedKey))
	{
		TestEqual(TEXT("Zweiter Aufruf: alte Heightmap wurde ersetzt"), StoredHeights->Num(), 4);
	}

	return true;
}
