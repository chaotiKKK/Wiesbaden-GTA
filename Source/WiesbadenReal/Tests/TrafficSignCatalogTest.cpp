// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenSignAssets.h"
#include "GIS/WiesbadenTrafficSignCatalog.h"
#include "GIS/WiesbadenTrafficSignLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficSignParseTest,
	"WiesbadenReal.GIS.TrafficSigns.Parse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficSignParseTest::RunTest(const FString& Parameters)
{
	// Einzelnes Zeichen.
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:206"), Signs);
		TestEqual(TEXT("DE:206 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("Id == 206"), Signs[0].Id, TEXT("206"));
			TestTrue(TEXT("Kategorie Vorschriftzeichen"),
				Signs[0].Category == EWiesbadenSignCategory::Vorschriftzeichen);
			TestTrue(TEXT("Name enthaelt 'Halt'"), Signs[0].Name.Contains(TEXT("Halt")));
		}
	}

	// Tempolimit (Bindestrich- und Klammer-Form).
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:274-50"), Signs);
		TestEqual(TEXT("DE:274-50 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestTrue(TEXT("274 ist Tempolimit"), Signs[0].bSpeedLimit);
			TestEqual(TEXT("Limit == 50"), Signs[0].SpeedLimitKmh, 50);
		}
	}
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:274[30]"), Signs);
		TestEqual(TEXT("DE:274[30] -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("Klammer-Limit == 30"), Signs[0].SpeedLimitKmh, 30);
			TestEqual(TEXT("Klammer-Id kanonisch 274-30"), Signs[0].Id, TEXT("274-30"));
		}
	}

	// Mehrere Zeichen (";"-getrennt) - inkl. Bindestrich-Zusatzzeichen.
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:205;DE:1000-32"), Signs);
		TestEqual(TEXT("2 Zeichen"), Signs.Num(), 2);
		if (Signs.Num() == 2)
		{
			TestEqual(TEXT("Erstes Zeichen 205"), Signs[0].Id, TEXT("205"));
			TestEqual(TEXT("Zweites Zeichen 1000-32"), Signs[1].Id, TEXT("1000-32"));
			TestTrue(TEXT("1000-32 ist Zusatzzeichen"),
				Signs[1].Category == EWiesbadenSignCategory::Zusatzzeichen);
		}
	}

	// Bindestrich-Unternummern werden exakt aufgeloest (nicht als Unbekannt).
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:103-10"), Signs);
		TestEqual(TEXT("DE:103-10 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("Id == 103-10"), Signs[0].Id, TEXT("103-10"));
			TestTrue(TEXT("103-10 ist Gefahrzeichen"),
				Signs[0].Category == EWiesbadenSignCategory::Gefahrzeichen);
		}
	}

	// Punkt-Unternummern (offizielle VzKat) werden aufgeloest.
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:325.1"), Signs);
		TestEqual(TEXT("DE:325.1 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("Id == 325.1"), Signs[0].Id, TEXT("325.1"));
		}
	}

	// Kurz-/Alt-Formen werden auf die offizielle Nummer normalisiert.
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:325"), Signs);
		TestEqual(TEXT("DE:325 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("325 -> 325.1"), Signs[0].Id, TEXT("325.1"));
		}
	}
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:220"), Signs);
		TestEqual(TEXT("DE:220 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("220 -> 220-20"), Signs[0].Id, TEXT("220-20"));
		}
	}

	// Alias-Formen aus der Katalog-JSON (Leitpfosten, Autobahn, Radverkehr).
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:605"), Signs);
		TestEqual(TEXT("DE:605 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("605 -> 620-40"), Signs[0].Id, TEXT("620-40"));
		}
	}
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:330"), Signs);
		TestEqual(TEXT("DE:330 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("330 -> 330.1"), Signs[0].Id, TEXT("330.1"));
		}
	}
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:138"), Signs);
		TestEqual(TEXT("DE:138 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("138 -> 138-10"), Signs[0].Id, TEXT("138-10"));
		}
	}

	// Formen, die in den echten Wiesbadener Daten stehen.
	{
		// "DE: 274.1" (Leerzeichen hinter dem Doppelpunkt) ergab vorher die Id
		// " 274.1" - und damit ein Schild ohne auffindbare Grafik.
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE: 274.1"), Signs);
		TestEqual(TEXT("Leerzeichen hinter DE: -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("274.1 ohne Leerzeichen"), Signs[0].Id, TEXT("274.1"));
		}
	}
	{
		// Kurzform ohne Doppelpunkt.
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE240"), Signs);
		TestEqual(TEXT("DE240 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("DE240 -> 240"), Signs[0].Id, TEXT("240"));
		}
	}
	{
		// Doppelpunkt als Werttrenner ("DE:274.1:30").
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:274.1:30"), Signs);
		TestEqual(TEXT("274.1:30 -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("Id 274-30"), Signs[0].Id, TEXT("274-30"));
			TestEqual(TEXT("Limit 30"), Signs[0].SpeedLimitKmh, 30);
		}
	}
	{
		// Unterzeichen mit Punkt und Wert ("DE:274.1[30]").
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:274.1[30]"), Signs);
		TestEqual(TEXT("274.1[30] -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("Id 274-30"), Signs[0].Id, TEXT("274-30"));
			TestEqual(TEXT("Limit 30"), Signs[0].SpeedLimitKmh, 30);
		}
	}
	{
		// Bedingung in eckigen Klammern gehoert nicht zur Id.
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:1042-31[Mo-Sa 08:00-19:00]"), Signs);
		TestEqual(TEXT("Bedingung -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestEqual(TEXT("Id ohne Bedingung"), Signs[0].Id, TEXT("1042-31"));
			TestTrue(TEXT("kein Unbekannt-Eintrag"),
				Signs[0].Category != EWiesbadenSignCategory::Unbekannt);
		}
	}
	{
		// Komma und Semikolon innerhalb der Bedingung sind kein Zeichen-Trenner.
		// Diese Schreibweise kommt in den echten OSM-Werten vor.
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(
			TEXT("DE:1042-31[Mo-Fr 06:00-11:00,18:30-19:30;Sa 06:00-09:00];DE:206"),
			Signs);
		TestEqual(TEXT("Trenner in Bedingung -> 2 Zeichen"), Signs.Num(), 2);
		if (Signs.Num() == 2)
		{
			TestEqual(TEXT("Bedingtes Zeichen bleibt 1042-31"), Signs[0].Id, TEXT("1042-31"));
			TestEqual(TEXT("Zeichen nach Bedingung bleibt 206"), Signs[1].Id, TEXT("206"));
		}
	}
	{
		// Gemischte Trenner: Tempolimit + Zusatzzeichen.
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:274-30,1001-30-200"), Signs);
		TestEqual(TEXT("2 Zeichen"), Signs.Num(), 2);
		if (Signs.Num() == 2)
		{
			TestEqual(TEXT("Id 274-30"), Signs[0].Id, TEXT("274-30"));
			TestEqual(TEXT("Limit 30"), Signs[0].SpeedLimitKmh, 30);
			TestEqual(TEXT("Zusatzzeichen 1001-30-200"), Signs[1].Id, TEXT("1001-30-200"));
		}
	}
	{
		// "none"/"no" sind OSM-Platzhalter und keine Zeichen.
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("none"), Signs);
		TestEqual(TEXT("none -> 0 Zeichen"), Signs.Num(), 0);
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("no"), Signs);
		TestEqual(TEXT("no -> 0 Zeichen"), Signs.Num(), 0);
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:none;DE:206"), Signs);
		TestEqual(TEXT("none gemischt -> nur 206"), Signs.Num(), 1);
	}
	{
		// Auch ein bedingter OSM-Platzhalter ist kein unbekanntes Zeichen.
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(
			TEXT("DE:none[Mo-Fr 08:00-18:00];DE:no[Sa]"), Signs);
		TestEqual(TEXT("bedingte Platzhalter -> 0 Zeichen"), Signs.Num(), 0);
	}

	// Unbekanntes Zeichen geht nicht verloren.
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FWiesbadenTrafficSignCatalog::ParseOsmTag(TEXT("DE:9999"), Signs);
		TestEqual(TEXT("Unbekannt -> 1 Zeichen"), Signs.Num(), 1);
		if (Signs.Num() == 1)
		{
			TestTrue(TEXT("Kategorie Unbekannt"),
				Signs[0].Category == EWiesbadenSignCategory::Unbekannt);
		}
	}

	// Tempolimit-Extraktion.
	TestEqual(TEXT("Limit aus DE:274-70"), FWiesbadenTrafficSignCatalog::ParseSpeedLimitKmh(TEXT("DE:274-70")), 70);
	TestEqual(TEXT("Kein Limit bei DE:206"), FWiesbadenTrafficSignCatalog::ParseSpeedLimitKmh(TEXT("DE:206")), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficSignCatalogTest,
	"WiesbadenReal.GIS.TrafficSigns.Catalog",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficSignCatalogTest::RunTest(const FString& Parameters)
{
	// Katalog ist nicht leer und enthaelt Kernzeichen.
	TestTrue(TEXT("Katalog nicht leer"), FWiesbadenTrafficSignCatalog::GetCatalog().Num() > 30);

	// Lookup bekannter Zeichen.
	FWiesbadenTrafficSign Sign;
	TestTrue(TEXT("FindById 301"), FWiesbadenTrafficSignCatalog::FindById(TEXT("301"), Sign));
	if (FWiesbadenTrafficSignCatalog::FindById(TEXT("301"), Sign))
	{
		TestTrue(TEXT("301 ist Richtzeichen"), Sign.Category == EWiesbadenSignCategory::Richtzeichen);
		TestEqual(TEXT("301 Name"), Sign.Name, TEXT("Vorfahrt"));
	}

	TestTrue(TEXT("FindById 274 (Tempolimit-Basis)"), FWiesbadenTrafficSignCatalog::FindById(TEXT("274"), Sign));
	if (FWiesbadenTrafficSignCatalog::FindById(TEXT("274"), Sign))
	{
		TestTrue(TEXT("274 ist Tempolimit"), Sign.bSpeedLimit);
	}

	TestTrue(TEXT("FindById 325.1"), FWiesbadenTrafficSignCatalog::FindById(TEXT("325.1"), Sign));
	TestTrue(TEXT("FindById 330.1"), FWiesbadenTrafficSignCatalog::FindById(TEXT("330.1"), Sign));
	TestTrue(TEXT("FindById 620-40"), FWiesbadenTrafficSignCatalog::FindById(TEXT("620-40"), Sign));
	TestTrue(TEXT("FindById 9999 schlaegt fehl"), !FWiesbadenTrafficSignCatalog::FindById(TEXT("9999"), Sign));

	// Standard-Platzierung ist sinnvoll befuellt.
	const FWiesbadenSignPlacement Placement = FWiesbadenTrafficSignCatalog::GetDefaultPlacement();
	TestTrue(TEXT("Platzierungshoehe > 0"), Placement.HeightAboveGroundCm > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficSignJsonTest,
	"WiesbadenReal.GIS.TrafficSigns.Json",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficSignJsonTest::RunTest(const FString& Parameters)
{
	// Hermetisch: der Test haengt nicht an der Content-Datei, sondern parst
	// einen Inline-JSON-String - genau der Code, der die echte Datei liest.
	const FString Json = TEXT(
		"{ \"signs\": ["
		"  { \"id\": \"206\", \"name\": \"Halt\", \"category\": \"Vorschriftzeichen\" },"
		"  { \"id\": \"325.1\", \"name\": \"Verkehrsberuhigter Bereich\", \"category\": \"Richtzeichen\", \"aliases\": [\"325\"] },"
		"  { \"id\": \"274\", \"name\": \"Tempolimit\", \"category\": \"Vorschriftzeichen\", \"speedLimit\": true },"
		"  { \"id\": \"600\", \"name\": \"Absperrpfosten\", \"category\": \"Verkehrseinrichtung\", \"noTexture\": true }"
		"] }");

	TArray<FWiesbadenTrafficSign> Signs;
	FString Error;
	TestTrue(TEXT("JSON parst"), FWiesbadenTrafficSignCatalog::LoadFromJsonString(Json, Signs, Error));
	TestEqual(TEXT("4 Eintraege"), Signs.Num(), 4);
	if (Signs.Num() == 4)
	{
		TestEqual(TEXT("206 Id"), Signs[0].Id, TEXT("206"));
		TestTrue(TEXT("206 Kategorie"), Signs[0].Category == EWiesbadenSignCategory::Vorschriftzeichen);
		TestEqual(TEXT("206 OsmValue"), Signs[0].OsmValue, TEXT("DE:206"));

		TestEqual(TEXT("325.1 Id"), Signs[1].Id, TEXT("325.1"));
		TestTrue(TEXT("Alias 325 vorhanden"), Signs[1].Aliases.Contains(TEXT("325")));

		TestTrue(TEXT("274 ist Tempolimit"), Signs[2].bSpeedLimit);

		TestTrue(TEXT("600 ist noTexture"), Signs[3].bNoTexture);
	}

	// Fehlerfaelle.
	TestFalse(TEXT("Kein JSON"),
		FWiesbadenTrafficSignCatalog::LoadFromJsonString(TEXT("nicht json"), Signs, Error));
	TestFalse(TEXT("Kein signs-Array"),
		FWiesbadenTrafficSignCatalog::LoadFromJsonString(TEXT("{ \"foo\": 1 }"), Signs, Error));
	TestFalse(TEXT("Doppelte Id"),
		FWiesbadenTrafficSignCatalog::LoadFromJsonString(
			TEXT("{ \"signs\": [ {\"id\":\"206\",\"name\":\"a\",\"category\":\"Richtzeichen\"}, {\"id\":\"206\",\"name\":\"b\",\"category\":\"Richtzeichen\"} ] }"),
			Signs, Error));
	TestFalse(TEXT("Eintrag ohne id"),
		FWiesbadenTrafficSignCatalog::LoadFromJsonString(
			TEXT("{ \"signs\": [ {\"name\":\"a\",\"category\":\"Richtzeichen\"} ] }"),
			Signs, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficSignTextureValidationTest,
	"WiesbadenReal.GIS.TrafficSigns.TextureValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficSignTextureValidationTest::RunTest(const FString& Parameters)
{
	const TArray<FWiesbadenTrafficSign> Catalog = FWiesbadenTrafficSignCatalog::GetCatalog();

	// GetTexturedIds: Tempolimit-Basen (274/278) und noTexture (600) werden
	// uebersprungen, alles andere braucht eine eigene Textur-Datei.
	const TArray<FString> TexturedIds = FWiesbadenTrafficSignCatalog::GetTexturedIds(Catalog);
	TestTrue(TEXT("Texturierte Ids > 30"), TexturedIds.Num() > 30);
	TestFalse(TEXT("274 (Tempolimit-Basis) wird uebersprungen"), TexturedIds.Contains(TEXT("274")));
	TestFalse(TEXT("278 (Tempolimit-Basis) wird uebersprungen"), TexturedIds.Contains(TEXT("278")));
	TestFalse(TEXT("600 (noTexture) wird uebersprungen"), TexturedIds.Contains(TEXT("600")));
	TestTrue(TEXT("206 benoetigt Textur"), TexturedIds.Contains(TEXT("206")));
	TestTrue(TEXT("325.1 benoetigt Textur"), TexturedIds.Contains(TEXT("325.1")));
	TestTrue(TEXT("620-40 benoetigt Textur"), TexturedIds.Contains(TEXT("620-40")));

	// Punkt-Id wird zu Bindestrich-Dateiname (325.1 -> Sign_325-1).
	TestEqual(TEXT("BuildTextureName 325.1"),
		WiesbadenSignAssets::BuildTextureName(TEXT("325.1")), TEXT("Sign_325-1"));

	// Id-Normalisierung: in den GEBACKENEN Kacheln stehen noch Rohformen aus
	// aelteren Parser-Staenden - die Tafel muss ihre Grafik trotzdem finden.
	TestEqual(TEXT("Normalize ' 274.1' (Leerzeichen hinter DE:)"),
		WiesbadenSignAssets::NormalizeSignId(TEXT(" 274.1")), TEXT("274.1"));
	TestEqual(TEXT("Normalize '1042-31[Mo-Sa 08:00-19:00]'"),
		WiesbadenSignAssets::NormalizeSignId(TEXT("1042-31[Mo-Sa 08:00-19:00]")), TEXT("1042-31"));
	TestEqual(TEXT("Normalize 'DE:274-30'"),
		WiesbadenSignAssets::NormalizeSignId(TEXT("DE:274-30")), TEXT("274-30"));
	TestEqual(TEXT("Normalize laesst gueltige Id in Ruhe"),
		WiesbadenSignAssets::NormalizeSignId(TEXT("325.1")), TEXT("325.1"));

	// Jede Id, die in den gebackenen Kacheln steht, zeigt nach der
	// Normalisierung auf ein vorhandenes Asset.
	const FString Ordner = FWiesbadenTrafficSignCatalog::GetDefaultTextureFolder();
	for (const FString& Roh : {FString(TEXT(" 274.1")), FString(TEXT("1042-31[Mo-Sa 08:00-19:00]"))})
	{
		const FString Key = WiesbadenSignAssets::NormalizeSignId(Roh);
		const FString Asset = FPaths::Combine(Ordner, WiesbadenSignAssets::BuildTextureName(Key) + TEXT(".uasset"));
		TestTrue(FString::Printf(TEXT("Asset fuer '%s' vorhanden (%s)"), *Roh, *Asset),
			FPaths::FileExists(Asset));
	}

	// End-to-End gegen den eingecheckten Textur-Ordner: keine fehlenden Zeichen.
	TArray<FString> Missing;
	const int32 Checked = FWiesbadenTrafficSignCatalog::ValidateTextures(
		Catalog, FWiesbadenTrafficSignCatalog::GetDefaultTextureFolder(), Missing);
	TestEqual(TEXT("Pruefanzahl == texturierte Ids"), Checked, TexturedIds.Num());
	if (Missing.Num() > 0)
	{
		AddError(FString::Printf(TEXT("Fehlende Schild-Texturen: %s"),
			*FString::Join(Missing, TEXT(", "))));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficSignCatalogMutabilityTest,
	"WiesbadenReal.GIS.TrafficSigns.Mutability",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficSignCatalogMutabilityTest::RunTest(const FString& Parameters)
{
	// AddSign fuegt hinzu und ersetzt; FindById (auch ueber die Blueprint-
	// Library) sieht den neuen Eintrag sofort.
	FWiesbadenTrafficSign Custom;
	Custom.Id = TEXT("9999");
	Custom.Name = TEXT("Testzeichen");
	Custom.Category = EWiesbadenSignCategory::Richtzeichen;
	Custom.OsmValue = TEXT("DE:9999");
	FWiesbadenTrafficSignCatalog::AddSign(Custom);

	FWiesbadenTrafficSign Found;
	TestTrue(TEXT("AddSign -> FindById 9999"),
		FWiesbadenTrafficSignCatalog::FindById(TEXT("9999"), Found));
	TestTrue(TEXT("Blueprint-Library findet 9999"),
		UWiesbadenTrafficSignLibrary::FindTrafficSignById(TEXT("9999"), Found));

	// RemoveSign entfernt wieder.
	TestTrue(TEXT("RemoveSign 9999"), FWiesbadenTrafficSignCatalog::RemoveSign(TEXT("9999")));
	TestTrue(TEXT("9999 nicht mehr vorhanden"),
		!FWiesbadenTrafficSignCatalog::FindById(TEXT("9999"), Found));

	// Hot-Reload aus JSON-String ersetzt den Katalog vollstaendig.
	const FString Json = TEXT(
		"{ \"signs\": ["
		"  { \"id\": \"206\", \"name\": \"Halt\", \"category\": \"Vorschriftzeichen\" }"
		"] }");
	FString Error;
	TestTrue(TEXT("ReloadFromJsonString ok"),
		FWiesbadenTrafficSignCatalog::ReloadFromJsonString(Json, Error));
	TestEqual(TEXT("Katalog hat jetzt 1 Eintrag"),
		FWiesbadenTrafficSignCatalog::GetCatalog().Num(), 1);

	// Ein Parse-Fehler laesst den bisherigen Katalog unveraendert.
	TestFalse(TEXT("Reload mit kaputtem JSON -> false"),
		FWiesbadenTrafficSignCatalog::ReloadFromJsonString(TEXT("nicht json"), Error));
	TestEqual(TEXT("Katalog bleibt bei Fehler intakt"),
		FWiesbadenTrafficSignCatalog::GetCatalog().Num(), 1);

	// Aufraeumen: Standard-Katalog zurueckladen (auch fuer nachfolgende Tests).
	FWiesbadenTrafficSignCatalog::ResetCatalog();
	TestTrue(TEXT("Reset stellt Standard-Katalog wieder her"),
		FWiesbadenTrafficSignCatalog::GetCatalog().Num() > 30);

	return true;
}
