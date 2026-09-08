// Unit-Test fuer den Adress-Override an der Platter Strasse.
//
// Hintergrund: In-Game hatten die realen Gebaeude Platter Strasse 140/142/144/146
// weiter die falschen Etagen, obwohl die Adress-Kette (ALKIS addr:street +
// addr:housenumber -> "Platter Strasse 142" -> NormalizeAddressForMatch) im Code
// korrekt aussah. Die Override-Logik liegt in der rein datenbasierten Funktion
// UBuildingGenerator::ApplyPlatterAddressOverride und wird hier direkt geprueft -
// ohne kompletten City-Bake.
//
// Erwartung (von der Hochhaus-Seite): 140 -> 12 Geschosse, 142 -> eingeschossige
// Garage mit Flachdach, 144/146 -> 6 Geschosse. Zusaetzlich die ECHTE ALKIS-
// Schreibweise mit sz-Ligatur, damit bewiesen ist, dass NormalizeAddressForMatch
// sie ueberbrueckt.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/OSMTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlatterAddressOverrideTest,
	"WiesbadenReal.GIS.BuildingGenerator.PlatterOverride",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlatterAddressOverrideTest::RunTest(const FString& Parameters)
{
	const double MetersPerLevel = 3.2;

	auto MakeBuilding = [](const FString& Address) -> FGeneratedBuilding
	{
		FGeneratedBuilding B;
		B.Address = Address;
		B.LevelCount = 3;                              // Ausgangswert, ueberschreibbar
		B.HeightCm = 3.0 * 3.2 * 100.0;
		B.BuildingType = EOSMBuildingType::Generic;
		B.RoofShape = EOSMRoofShape::Gabled;
		return B;
	};

	// 140 -> 12 Geschosse
	{
		FGeneratedBuilding B = MakeBuilding(TEXT("Platter Strasse 140"));
		TestTrue(TEXT("140 griff"), UBuildingGenerator::ApplyPlatterAddressOverride(B, MetersPerLevel));
		TestEqual(TEXT("140 -> 12 Geschosse"), B.LevelCount, 12);
		TestTrue(TEXT("140 Hoehe 12*3.2 m"),
			FMath::IsNearlyEqual(B.HeightCm, 12.0 * MetersPerLevel * 100.0, 1.0));
	}

	// 142 -> eingeschossige Garage, Flachdach
	{
		FGeneratedBuilding B = MakeBuilding(TEXT("Platter Strasse 142"));
		TestTrue(TEXT("142 griff"), UBuildingGenerator::ApplyPlatterAddressOverride(B, MetersPerLevel));
		TestEqual(TEXT("142 -> 1 Geschoss"), B.LevelCount, 1);
		TestTrue(TEXT("142 Typ Garage"), B.BuildingType == EOSMBuildingType::Garage);
		TestTrue(TEXT("142 Flachdach"), B.RoofShape == EOSMRoofShape::Flat);
	}

	// 142 mit ECHTER sz-Schreibweise (so liefert ALKIS die Adresse)
	{
		FGeneratedBuilding B = MakeBuilding(TEXT("Platter Straße 142"));
		TestTrue(TEXT("142 (sz) griff"), UBuildingGenerator::ApplyPlatterAddressOverride(B, MetersPerLevel));
		TestEqual(TEXT("142 (sz) -> 1 Geschoss"), B.LevelCount, 1);
		TestTrue(TEXT("142 (sz) Typ Garage"), B.BuildingType == EOSMBuildingType::Garage);
	}

	// 144 und 146 -> 6 Geschosse (frueher faelschlich als Garage)
	for (const TCHAR* Nr : { TEXT("144"), TEXT("146") })
	{
		FGeneratedBuilding B = MakeBuilding(FString::Printf(TEXT("Platter Straße %s"), Nr));
		TestTrue(FString::Printf(TEXT("%s griff"), Nr),
			UBuildingGenerator::ApplyPlatterAddressOverride(B, MetersPerLevel));
		TestEqual(FString::Printf(TEXT("%s -> 6 Geschosse"), Nr), B.LevelCount, 6);
		TestTrue(FString::Printf(TEXT("%s Typ unveraendert Generic"), Nr),
			B.BuildingType == EOSMBuildingType::Generic);
	}

	// Kein Treffer: Nachbar-Hausnummer, fremde Strasse, leere Adresse -> unveraendert
	{
		FGeneratedBuilding B = MakeBuilding(TEXT("Platter Straße 148"));
		TestFalse(TEXT("148 kein Override"), UBuildingGenerator::ApplyPlatterAddressOverride(B, MetersPerLevel));
		TestEqual(TEXT("148 LevelCount unveraendert"), B.LevelCount, 3);
	}
	{
		FGeneratedBuilding B = MakeBuilding(TEXT("Andere Straße 142"));
		TestFalse(TEXT("fremde Strasse kein Override"),
			UBuildingGenerator::ApplyPlatterAddressOverride(B, MetersPerLevel));
		TestEqual(TEXT("fremde Strasse LevelCount unveraendert"), B.LevelCount, 3);
	}
	{
		FGeneratedBuilding B = MakeBuilding(FString());
		TestFalse(TEXT("leere Adresse kein Override"),
			UBuildingGenerator::ApplyPlatterAddressOverride(B, MetersPerLevel));
	}

	return true;
}
