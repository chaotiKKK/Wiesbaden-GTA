// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenCityChunk.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowLightUseTest,
	"WiesbadenReal.World.FensterlichtNutzung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWindowLightUseTest::RunTest(const FString& Parameters)
{
	using FChunk = AWiesbadenCityChunk;

	// Die Nutzung steckt im Fassadennamen, den die Pipeline vergibt. Die Namen
	// unten sind die, die im Spiel tatsaechlich zugewiesen werden
	// (Protokoll der Material-Faecher), nicht die der zweiten, ungenutzten
	// Fassadenfamilie.
	TestEqual(TEXT("Buerohaus -> Buero"),
		FChunk::BuildingUseFromMaterialName(TEXT("MI_WbFacade_Buerohaus")), EWbBuildingUse::Office);
	TestEqual(TEXT("Glasturm -> Buero"),
		FChunk::BuildingUseFromMaterialName(TEXT("M_WbFacade_Glasturm")), EWbBuildingUse::Office);
	TestEqual(TEXT("Hochhaus -> Buero"),
		FChunk::BuildingUseFromMaterialName(TEXT("MI_WbFacade_Hochhaus_Nacht")), EWbBuildingUse::Office);

	TestEqual(TEXT("Sandstein -> Wohnen"),
		FChunk::BuildingUseFromMaterialName(TEXT("M_WbFacade_Sandstein")), EWbBuildingUse::Residential);
	TestEqual(TEXT("Backstein -> Wohnen"),
		FChunk::BuildingUseFromMaterialName(TEXT("M_WbFacade_Backstein")), EWbBuildingUse::Residential);
	TestEqual(TEXT("Altbau -> Wohnen"),
		FChunk::BuildingUseFromMaterialName(TEXT("MI_WbFacade_Altbau")), EWbBuildingUse::Residential);

	// Dach und unbekannte Materialien duerfen NICHT als Wohnhaus durchgehen -
	// sonst leuchtet ein Dach wie ein Wohnzimmer.
	TestEqual(TEXT("Dach -> Sonstige"),
		FChunk::BuildingUseFromMaterialName(TEXT("M_WbBuildingRoof")), EWbBuildingUse::Other);
	TestEqual(TEXT("Unbekannt -> Sonstige"),
		FChunk::BuildingUseFromMaterialName(TEXT("M_WbIrgendwas")), EWbBuildingUse::Other);

	// Eine dynamische Instanz traegt den Namen der Vorlage im Namen - die
	// Zuordnung muss deshalb auch dann noch stimmen, wenn schon einmal
	// gesetzt wurde.
	TestEqual(TEXT("Dynamische Instanz behaelt die Nutzung"),
		FChunk::BuildingUseFromMaterialName(TEXT("MID_M_WbFacade_Sandstein_0")),
		EWbBuildingUse::Residential);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowLightCurveTest,
	"WiesbadenReal.World.FensterlichtKurve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWindowLightCurveTest::RunTest(const FString& Parameters)
{
	using FChunk = AWiesbadenCityChunk;
	const EWbBuildingUse Wohnen = EWbBuildingUse::Residential;
	const EWbBuildingUse Buero = EWbBuildingUse::Office;

	// -- 1. Mittags ist aus. ------------------------------------------------
	//
	// Ein leuchtendes Fenster bei Sonnenschein sieht falsch aus, und sehen
	// wuerde man es ohnehin nicht.
	TestTrue(TEXT("Wohnen um 12 Uhr dunkel"),
		FChunk::WindowLightStrength(Wohnen, 12.0f) < 0.01f);
	TestTrue(TEXT("Wohnen um 14 Uhr dunkel"),
		FChunk::WindowLightStrength(Wohnen, 14.0f) < 0.01f);

	// -- 2. Abends am hellsten. ---------------------------------------------
	const float Evening = FChunk::WindowLightStrength(Wohnen, 21.0f);
	TestTrue(FString::Printf(TEXT("Wohnen um 21 Uhr voll (%.2f)"), Evening), Evening > 0.95f);

	// -- 3. Nachts nur noch einzelne Fenster, aber nicht null. --------------
	const float Night = FChunk::WindowLightStrength(Wohnen, 3.0f);
	TestTrue(FString::Printf(TEXT("Wohnen um 3 Uhr schwach, aber nicht aus (%.2f)"), Night),
		Night > 0.0f && Night < 0.2f);

	// -- 4. Wohnen und Buero laufen AUSEINANDER. ----------------------------
	//
	// Der Kern der Aufgabe: ein Buerohaus, das um 22 Uhr so hell ist wie ein
	// Wohnblock, verraet sofort, dass eine einzige Kurve fuer alles gilt.
	const float WohnenSpaet = FChunk::WindowLightStrength(Wohnen, 22.0f);
	const float BueroSpaet = FChunk::WindowLightStrength(Buero, 22.0f);
	TestTrue(FString::Printf(
		TEXT("Um 22 Uhr Wohnen %.2f deutlich heller als Buero %.2f"), WohnenSpaet, BueroSpaet),
		WohnenSpaet > BueroSpaet * 2.0f);

	// Und frueh am Morgen ist es umgekehrt: im Buero geht das Licht an,
	// waehrend die Wohnungen noch schlafen.
	const float WohnenFrueh = FChunk::WindowLightStrength(Wohnen, 7.5f);
	const float BueroFrueh = FChunk::WindowLightStrength(Buero, 7.5f);
	TestTrue(FString::Printf(
		TEXT("Um 7:30 Buero %.2f mindestens so hell wie Wohnen %.2f"), BueroFrueh, WohnenFrueh),
		BueroFrueh >= WohnenFrueh);

	// -- 5. Sonstige Nutzung ist gedaempft. ---------------------------------
	const float Other = FChunk::WindowLightStrength(EWbBuildingUse::Other, 21.0f);
	TestTrue(FString::Printf(TEXT("Sonstige um 21 Uhr gedaempft (%.2f gegen %.2f)"), Other, Evening),
		Other > 0.0f && Other < Evening);

	// -- 6. Die Uhr laeuft ueber Mitternacht. -------------------------------
	//
	// Die Tageszeit kann als 25.0 hereinkommen; ohne Normierung liefe die
	// Kurve aus der Tabelle und das Licht spraenge an der Tagesgrenze.
	TestTrue(TEXT("25 Uhr ist 1 Uhr"),
		FMath::IsNearlyEqual(FChunk::WindowLightStrength(Wohnen, 25.0f),
			FChunk::WindowLightStrength(Wohnen, 1.0f), 0.001f));
	TestTrue(TEXT("-2 Uhr ist 22 Uhr"),
		FMath::IsNearlyEqual(FChunk::WindowLightStrength(Wohnen, -2.0f),
			FChunk::WindowLightStrength(Wohnen, 22.0f), 0.001f));

	// -- 7. Kein Sprung an der Tagesgrenze. ---------------------------------
	//
	// 0 und 24 Uhr sind derselbe Augenblick - waeren die Endpunkte der Tabelle
	// verschieden, flackerte die ganze Stadt einmal je Nacht.
	TestTrue(TEXT("Mitternacht ist stetig"),
		FMath::IsNearlyEqual(FChunk::WindowLightStrength(Wohnen, 23.99f),
			FChunk::WindowLightStrength(Wohnen, 0.01f), 0.02f));

	// -- 8. Nie ausserhalb 0..1. --------------------------------------------
	bool bInRange = true;
	for (int32 i = 0; i <= 240; ++i)
	{
		const float H = i * 0.1f;
		for (const EWbBuildingUse Use : { Wohnen, Buero, EWbBuildingUse::Other })
		{
			const float V = FChunk::WindowLightStrength(Use, H);
			bInRange = bInRange && V >= 0.0f && V <= 1.0f;
		}
	}
	TestTrue(TEXT("Die Staerke bleibt ueber den ganzen Tag in 0..1"), bInRange);

	return true;
}
