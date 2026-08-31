// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#include "GIS/CityPrompt.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityPromptParsingTest,
	"WiesbadenReal.GIS.CityPrompt.Parsing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCityPromptParsingTest::RunTest(const FString& Parameters)
{
	// Deutscher Prompt: Dichte + Gruenderzeit + Landmarke + Wetter.
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(
			TEXT("dichte Gruenderzeit-Innenstadt mit Marktkirche, wolkig"));
		TestTrue(TEXT("Dichte erkannt"), Spec.BuildingDensity > 0.7f);
		const float* PutzWeight = Spec.FacadeVariantWeights.Find(0);
		TestTrue(TEXT("Gruenderzeit -> Putz-Variante (0) gewichtet"),
			PutzWeight && *PutzWeight > 0.0f);
		TestTrue(TEXT("Marktkirche als Landmarke erkannt"),
			Spec.DetectedLandmarks.Contains(TEXT("Marktkirche")));
		TestEqual(TEXT("Wetter: wolkig"), Spec.Weather, ECityWeatherPreset::Cloudy);
		TestTrue(TEXT("Anzeigename nicht leer"), !Spec.DisplayName.IsEmpty());
	}

	// Locker + zwei Stile + klares Wetter.
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(
			TEXT("lockere Vorstadt mit Backstein und Fachwerk, klarer Sonnenschein"));
		TestTrue(TEXT("Locker erkannt"), Spec.BuildingDensity < 0.4f);
		const float* BrickWeight = Spec.FacadeVariantWeights.Find(1);
		const float* TimberWeight = Spec.FacadeVariantWeights.Find(5);
		TestTrue(TEXT("Backstein gewichtet"), BrickWeight && *BrickWeight > 0.0f);
		TestTrue(TEXT("Fachwerk gewichtet"), TimberWeight && *TimberWeight > 0.0f);
		TestEqual(TEXT("Wetter: klar"), Spec.Weather, ECityWeatherPreset::Clear);
	}

	// Englischer Prompt.
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(
			TEXT("dense modern office district, thunderstorm"));
		TestTrue(TEXT("Englisch: Dichte erkannt"), Spec.BuildingDensity > 0.7f);
		const float* GlassWeight = Spec.FacadeVariantWeights.Find(3);
		TestTrue(TEXT("Englisch: Moderne -> Glas gewichtet"), GlassWeight && *GlassWeight > 0.0f);
		TestEqual(TEXT("Englisch: Gewitter"), Spec.Weather, ECityWeatherPreset::Thunderstorm);
	}

	// Umlaute: "gr\u00FCner Vorort" -> "gruener vorort" (sz/ae/oe/ue-Normalisierung).
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("gr\u00FCner Vorort"));
		TestTrue(TEXT("Umlaut-Normalisierung: gruener Vorort -> locker"),
			Spec.BuildingDensity < 0.4f);
	}

	// Wortgrenzen-Luecke: "dichte" matcht wegen der Wortgrenze NICHT "dicht"
	// (boundary after = 'e' ist alnum) - die Flektionsform muss eigens gefuehrt
	// werden, sonst faellt "dichte Bebauung" auf die Default-Dichte 0.5 zurueck.
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("dichte Bebauung am Stadtrand"));
		TestTrue(TEXT("Flektion dichte -> Dichte 0.85"), Spec.BuildingDensity > 0.7f);
	}
	// Derselbe Fall fuer den Plural "Vororte" (boundary after 'e' ist alnum).
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Vororte am Stadtrand"));
		TestTrue(TEXT("Plural vororte -> Dichte 0.3"), Spec.BuildingDensity < 0.4f);
	}

	// Leerer Prompt -> Defaults.
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT(""));
		TestEqual(TEXT("Leerer Prompt: Default-Dichte"), Spec.BuildingDensity, 0.5f);
		TestTrue(TEXT("Leerer Prompt: keine Landmarken"), Spec.DetectedLandmarks.Num() == 0);
		TestEqual(TEXT("Leerer Prompt: Wetter klar"), Spec.Weather, ECityWeatherPreset::Clear);
		TestEqual(TEXT("Leerer Prompt: Standard-Anzeigename"),
			Spec.DisplayName, TEXT("Standard-Stadt"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityPromptDeterminismTest,
	"WiesbadenReal.GIS.CityPrompt.Determinism",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCityPromptDeterminismTest::RunTest(const FString& Parameters)
{
	// Gleicher Prompt -> gleiche Spezifikation (kein Hash-Iterations-Zufall).
	const FString Prompt = TEXT("dichte Backstein-Innenstadt mit Rathaus und Neroberg, Nebel");
	const FCityPromptSpec A = CityPromptParser::Parse(Prompt);
	const FCityPromptSpec B = CityPromptParser::Parse(Prompt);

	TestEqual(TEXT("Dichte deterministisch"), A.BuildingDensity, B.BuildingDensity);
	TestEqual(TEXT("Wetter deterministisch"), A.Weather, B.Weather);
	TestEqual(TEXT("Landmarken deterministisch"), A.DetectedLandmarks.Num(), B.DetectedLandmarks.Num());
	TestEqual(TEXT("Anzeigename deterministisch"), A.DisplayName, B.DisplayName);
	TestTrue(TEXT("Anzeigename nennt den haeufigsten Stil"),
		A.DisplayName.StartsWith(TEXT("Backstein")));

	// Bei Gleichstand gewinnt die kleinere Variantennummer (deterministisch).
	const FCityPromptSpec Tie = CityPromptParser::Parse(TEXT("Backstein und Fachwerk"));
	TestTrue(TEXT("Gleichstand -> kleinere Variantennummer (Backstein)"),
		Tie.DisplayName.StartsWith(TEXT("Backstein")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityPromptExtendedRulesTest,
	"WiesbadenReal.GIS.CityPrompt.ExtendedRules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCityPromptExtendedRulesTest::RunTest(const FString& Parameters)
{
	// -- Tageszeit (0..24 Uhr, -1 = nicht gesetzt) --------------------------
	TestEqual(TEXT("Abend -> 19 Uhr"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt am Abend")).TimeOfDayHours, 19.0f);
	TestEqual(TEXT("Morgens -> 8 Uhr"),
		CityPromptParser::Parse(TEXT("lockere Vorstadt morgens")).TimeOfDayHours, 8.0f);
	TestEqual(TEXT("Nachts -> 0 Uhr"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt nachts")).TimeOfDayHours, 0.0f);
	TestEqual(TEXT("Englisch: night -> 0 Uhr"),
		CityPromptParser::Parse(TEXT("quiet suburb at night")).TimeOfDayHours, 0.0f);
	TestEqual(TEXT("Ohne Zeitwort -> nicht gesetzt"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt")).TimeOfDayHours, -1.0f);

	// -- Verkehrsdichte (0..1) ---------------------------------------------
	TestEqual(TEXT("Viel Verkehr -> 0.85"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt mit viel verkehr")).TrafficDensity, 0.85f);
	TestEqual(TEXT("Wenig Verkehr -> 0.25"),
		CityPromptParser::Parse(TEXT("ruhige Vorstadt, wenig verkehr")).TrafficDensity, 0.25f);
	TestEqual(TEXT("Stau -> 0.95"),
		CityPromptParser::Parse(TEXT("Innenstadt mit Stau")).TrafficDensity, 0.95f);
	TestEqual(TEXT("Ruhig -> 0.3"),
		CityPromptParser::Parse(TEXT("ruhige Vorstadt")).TrafficDensity, 0.3f);
	TestEqual(TEXT("Leere Strassen -> 0.2 (Flektion leere)"),
		CityPromptParser::Parse(TEXT("leere Strassen am Nachmittag")).TrafficDensity, 0.2f);
	TestEqual(TEXT("Ohne Verkehrswort -> 0.5"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt")).TrafficDensity, 0.5f);

	// -- Gebaeudehoehen-Skalierung -----------------------------------------
	TestEqual(TEXT("Hohe Gebaeude -> 1.4"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt mit hohen Gebaeuden")).BuildingHeightScale, 1.4f);
	TestEqual(TEXT("Niedrige Vorstadt -> 0.6"),
		CityPromptParser::Parse(TEXT("niedrige Vorstadt")).BuildingHeightScale, 0.6f);
	TestEqual(TEXT("Hochhaeuser -> 2.0 (Flektion hochhaeusern)"),
		CityPromptParser::Parse(TEXT("Innenstadt mit Hochhaeusern")).BuildingHeightScale, 2.0f);
	TestEqual(TEXT("Wolkenkratzer -> 2.0"),
		CityPromptParser::Parse(TEXT("Wolkenkratzer-Viertel")).BuildingHeightScale, 2.0f);
	TestEqual(TEXT("Ohne Hoehenwort -> 1.0"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt")).BuildingHeightScale, 1.0f);

	// -- Wortgrenzen-/Flektion-Luecken bei den weiteren numerischen Paaren -----
	// "ruhiger" matcht wegen der Wortgrenze NICHT "ruhig" (boundary after = 'e'
	// ist alnum) - ohne eignen Eintrag faellt "ruhiger Verkehr" auf den
	// generischen "verkehr"-Fallback (0.7) statt auf 0.3 zurueck.
	TestEqual(TEXT("Flektion ruhiger Verkehr -> 0.3"),
		CityPromptParser::Parse(TEXT("ruhiger Verkehr")).TrafficDensity, 0.3f);
	// "leerer Platz" -> ohne "leerer"-Eintrag faellt es auf Default 0.5 zurueck.
	TestEqual(TEXT("Flektion leerer Platz -> 0.2"),
		CityPromptParser::Parse(TEXT("leerer Platz")).TrafficDensity, 0.2f);
	// "belebter" -> gleiche Luecke wie ruhiger/leerer.
	TestEqual(TEXT("Flektion belebter Platz -> 0.8"),
		CityPromptParser::Parse(TEXT("belebter Platz")).TrafficDensity, 0.8f);
	// "hoher Turm" -> ohne "hoher" faellt es auf Default 1.0 zurueck.
	TestEqual(TEXT("Flektion hoher Turm -> 1.4"),
		CityPromptParser::Parse(TEXT("hoher Turm im Zentrum")).BuildingHeightScale, 1.4f);
	// "niedriges Haus" -> gleiche Luecke wie hoher.
	TestEqual(TEXT("Flektion niedriges Haus -> 0.6"),
		CityPromptParser::Parse(TEXT("niedriges Haus am Rand")).BuildingHeightScale, 0.6f);
	// "Vormittag" -> ohne eignen Eintrag faellt es auf -1 (nicht gesetzt) zurueck.
	TestEqual(TEXT("Vormittag -> 10 Uhr"),
		CityPromptParser::Parse(TEXT("ruhiger Vormittag")).TimeOfDayHours, 10.0f);
	// "spaet(er)" -> ohne eignen Eintrag wird nur der generische "abend"-Teil
	// (19) getroffen; "spaeter Abend" soll die spaetere Stunde (21) ergeben.
	TestEqual(TEXT("Spaeter Abend -> 21 Uhr"),
		CityPromptParser::Parse(TEXT("spaeter Abend")).TimeOfDayHours, 21.0f);

	// -- Wetterlage: Wortgrenzen-/Flektion-Luecken --------------------------
	// "neblig" matcht wegen der Wortgrenze NICHT "nebel" (boundary after = 'g'
	// ist alnum) - ohne eignen Eintrag faellt "nebliger Morgen" auf Clear
	// (Default) zurueck statt Fog.
	TestEqual(TEXT("Flektion neblig -> Fog"),
		CityPromptParser::Parse(TEXT("nebliger Morgen")).Weather, ECityWeatherPreset::Fog);
	TestEqual(TEXT("Flektion neblige -> Fog"),
		CityPromptParser::Parse(TEXT("neblige Strasse")).Weather, ECityWeatherPreset::Fog);
	// "foggy" enthaelt "fog" nur mit alnum-Folgezeichen ('g') -> eigener Eintrag.
	TestEqual(TEXT("Englisch foggy -> Fog"),
		CityPromptParser::Parse(TEXT("foggy morning")).Weather, ECityWeatherPreset::Fog);
	// "schneit"/"schneien" matchen "schnee" nicht (e-i statt e-e).
	TestEqual(TEXT("Flektion schneit -> Snow"),
		CityPromptParser::Parse(TEXT("es schneit")).Weather, ECityWeatherPreset::Snow);
	TestEqual(TEXT("Flektion schneien -> Snow"),
		CityPromptParser::Parse(TEXT("schneien am Abend")).Weather, ECityWeatherPreset::Snow);
	// "verschneit" enthaelt "schnee" nicht als Teilstring.
	TestEqual(TEXT("Flektion verschneit -> Snow"),
		CityPromptParser::Parse(TEXT("verschneite Landschaft")).Weather, ECityWeatherPreset::Snow);
	TestEqual(TEXT("Englisch snowy -> Snow"),
		CityPromptParser::Parse(TEXT("snowy day")).Weather, ECityWeatherPreset::Snow);
	// "regnerisch"/"regnet" matchen "regen" nicht (g-n statt g-e).
	TestEqual(TEXT("Flektion regnerisch -> Rain"),
		CityPromptParser::Parse(TEXT("regnerischer Tag")).Weather, ECityWeatherPreset::Rain);
	TestEqual(TEXT("Flektion regnet -> Rain"),
		CityPromptParser::Parse(TEXT("es regnet")).Weather, ECityWeatherPreset::Rain);
	TestEqual(TEXT("Englisch rainy -> Rain"),
		CityPromptParser::Parse(TEXT("rainy day")).Weather, ECityWeatherPreset::Rain);
	// "Sturm" hat GAR kein Schluesselwort - faellt auf Clear zurueck statt Rain.
	TestEqual(TEXT("Sturm -> Rain"),
		CityPromptParser::Parse(TEXT("Sturm an der Kueste")).Weather, ECityWeatherPreset::Rain);
	TestEqual(TEXT("Englisch stormy -> Rain"),
		CityPromptParser::Parse(TEXT("stormy weather")).Weather, ECityWeatherPreset::Rain);
	// "wolkige" matcht "wolkig" nicht (boundary after = 'e' ist alnum).
	TestEqual(TEXT("Flektion wolkige -> Cloudy"),
		CityPromptParser::Parse(TEXT("wolkige Aussicht")).Weather, ECityWeatherPreset::Cloudy);
	TestEqual(TEXT("Flektion bewoelkte -> Cloudy"),
		CityPromptParser::Parse(TEXT("bewoelkter Himmel")).Weather, ECityWeatherPreset::Cloudy);
	// "trueb" (trueber Himmel) hat kein Schluesselwort.
	TestEqual(TEXT("Flektion trueb -> Cloudy"),
		CityPromptParser::Parse(TEXT("trueber Himmel")).Weather, ECityWeatherPreset::Cloudy);

	// -- Fassadenstile: Wortgrenzen-/Flektion-Luecken -----------------------
	// "moderner" matcht weder "modern" noch "moderne" (boundary after ist in
	// beiden Faellen alnum: 'e' bzw. 'r') - ohne eignen Eintrag faellt es auf
	// keine Variante (Glas 3) zurueck.
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("moderner Buerokomplex"));
		const float* GlassWeight = Spec.FacadeVariantWeights.Find(3);
		TestTrue(TEXT("Flektion moderner -> Glas (3) gewichtet"),
			GlassWeight && *GlassWeight > 0.0f);
	}
	// "Backsteinfassaden" enthaelt "backstein" nur mit alnum-Folgezeichen ('f').
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Backsteinfassaden am Ring"));
		const float* BrickWeight = Spec.FacadeVariantWeights.Find(1);
		TestTrue(TEXT("Kompositum Backsteinfassaden -> Backstein (1) gewichtet"),
			BrickWeight && *BrickWeight > 0.0f);
	}
	// "Glasfassade" enthaelt "glas" nur mit alnum-Folgezeichen ('f').
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Glasfassade im Zentrum"));
		const float* GlassWeight = Spec.FacadeVariantWeights.Find(3);
		TestTrue(TEXT("Kompositum Glasfassade -> Glas (3) gewichtet"),
			GlassWeight && *GlassWeight > 0.0f);
	}
	// "Betonbauten" enthaelt "beton" nur mit alnum-Folgezeichen ('b').
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Betonbauten am Fluss"));
		const float* ConcreteWeight = Spec.FacadeVariantWeights.Find(4);
		TestTrue(TEXT("Kompositum Betonbauten -> Beton (4) gewichtet"),
			ConcreteWeight && *ConcreteWeight > 0.0f);
	}
	// Dasselbe Muster fuer die uebrigen Varianten (Singular + Plural).
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Putzfassade in der Altstadt"));
		const float* PutzWeight = Spec.FacadeVariantWeights.Find(0);
		TestTrue(TEXT("Kompositum Putzfassade -> Putz (0) gewichtet"),
			PutzWeight && *PutzWeight > 0.0f);
	}
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Sandsteinfassaden am Kurhaus"));
		const float* SandWeight = Spec.FacadeVariantWeights.Find(2);
		TestTrue(TEXT("Kompositum Sandsteinfassaden -> Sandstein (2) gewichtet"),
			SandWeight && *SandWeight > 0.0f);
	}
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Fachwerkfassaden in der Vorstadt"));
		const float* TimberWeight = Spec.FacadeVariantWeights.Find(5);
		TestTrue(TEXT("Kompositum Fachwerkfassaden -> Fachwerk (5) gewichtet"),
			TimberWeight && *TimberWeight > 0.0f);
	}
	// "Buerohaeuser" (Bürohäuser, umlaut-normalisiert) enthaelt "buerohaus"
	// nicht (Plural ae statt au) - der Test nutzt die echte Umlaut-Schreibweise
	// und beweist damit die sz-/umlaut-Normalisierung end-to-end.
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("B\u00FCroh\u00E4user am Bahnhof"));
		const float* GlassWeight = Spec.FacadeVariantWeights.Find(3);
		TestTrue(TEXT("Kompositum Buerohaeuser -> Glas (3) gewichtet"),
			GlassWeight && *GlassWeight > 0.0f);
	}

	// -- Kombination + Determinismus ---------------------------------------
	const FCityPromptSpec Combo = CityPromptParser::Parse(
		TEXT("dichte Gruenderzeit-Innenstadt mit Marktkirche, wolkig, abends, viel verkehr, hohe Gebaeude"));
	TestEqual(TEXT("Kombination: Abend"), Combo.TimeOfDayHours, 19.0f);
	TestEqual(TEXT("Kombination: Verkehr 0.85"), Combo.TrafficDensity, 0.85f);
	TestEqual(TEXT("Kombination: Hoehen 1.4"), Combo.BuildingHeightScale, 1.4f);
	TestEqual(TEXT("Kombination: Wetter unveraendert"), Combo.Weather, ECityWeatherPreset::Cloudy);

	// Leerer Prompt: alle neuen Felder auf Default.
	const FCityPromptSpec Empty = CityPromptParser::Parse(TEXT(""));
	TestEqual(TEXT("Leer: Zeit nicht gesetzt"), Empty.TimeOfDayHours, -1.0f);
	TestEqual(TEXT("Leer: Verkehr 0.5"), Empty.TrafficDensity, 0.5f);
	TestEqual(TEXT("Leer: Hoehen 1.0"), Empty.BuildingHeightScale, 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityPromptTerrainThresholdsTest,
	"WiesbadenReal.GIS.CityPrompt.TerrainThresholds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCityPromptTerrainThresholdsTest::RunTest(const FString& Parameters)
{
	// -- Tile-Vergroesserungsfaktor (numerisch, deutsch + englisch) ---------
	TestEqual(TEXT("Tile-Faktor 3.5 (deutsch)"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt, tile faktor 3.5")).TerrainMaxTileToOsmRatio, 3.5f);
	TestEqual(TEXT("Tile-Faktor 2.5 (englisch)"),
		CityPromptParser::Parse(TEXT("dense city center, tile ratio 2.5")).TerrainMaxTileToOsmRatio, 2.5f);
	// Komma als Dezimaltrenner (deutsche Schreibweise 3,0).
	TestEqual(TEXT("Tile-Faktor mit Komma (3,0)"),
		CityPromptParser::Parse(TEXT("Innenstadt, crop faktor 3,0")).TerrainMaxTileToOsmRatio, 3.0f);
	// Doppelpunkt-Schreibweise ("tile faktor: 3.5").
	TestEqual(TEXT("Tile-Faktor mit Doppelpunkt (3.5)"),
		CityPromptParser::Parse(TEXT("Innenstadt, tile faktor: 3.5")).TerrainMaxTileToOsmRatio, 3.5f);

	// -- Hoehenspanne (min..max, deutsch + englisch) ------------------------
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Innenstadt, hoehenspanne 5..2000"));
		TestEqual(TEXT("Hoehenspanne min 5"), Spec.TerrainMinHeightSpanMeters, 5.0f);
		TestEqual(TEXT("Hoehenspanne max 2000"), Spec.TerrainMaxHeightSpanMeters, 2000.0f);
	}
	// Doppelpunkt-Schreibweise ("hoehenspanne: 5..2000") - darf NICHT
	// stillschweigend ignoriert werden (Defaults 1..3000 wuerden bleiben).
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Innenstadt, hoehenspanne: 5..2000"));
		TestEqual(TEXT("Hoehenspanne mit Doppelpunkt: min 5"), Spec.TerrainMinHeightSpanMeters, 5.0f);
		TestEqual(TEXT("Hoehenspanne mit Doppelpunkt: max 2000"), Spec.TerrainMaxHeightSpanMeters, 2000.0f);
	}
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("dense district, height span 2 to 1500"));
		TestEqual(TEXT("Height span min 2"), Spec.TerrainMinHeightSpanMeters, 2.0f);
		TestEqual(TEXT("Height span max 1500"), Spec.TerrainMaxHeightSpanMeters, 1500.0f);
	}
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Vorstadt, hoehen min 10 max 4000"));
		TestEqual(TEXT("Hoehen min 10"), Spec.TerrainMinHeightSpanMeters, 10.0f);
		TestEqual(TEXT("Hoehen max 4000"), Spec.TerrainMaxHeightSpanMeters, 4000.0f);
	}

	// -- Umgekehrte Bereiche werden geswappt (min > max -> min/max tauschen)
	// "hoehenspanne 2000..5" ist eine Benutzereingabe mit vertauschten Grenzen
	// (kleinere Zahl zuerst) - der Parser normalisiert sie statt die Schwellen
	// stillschweigend unbrauchbar (min 2000 > max 5) in die Pipeline zu geben.
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Innenstadt, hoehenspanne 2000..5"));
		TestEqual(TEXT("Umgekehrte Span: min 5"), Spec.TerrainMinHeightSpanMeters, 5.0f);
		TestEqual(TEXT("Umgekehrte Span: max 2000"), Spec.TerrainMaxHeightSpanMeters, 2000.0f);
	}
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Innenstadt, hoehenspanne: 2000..5"));
		TestEqual(TEXT("Umgekehrte Span mit Doppelpunkt: min 5"), Spec.TerrainMinHeightSpanMeters, 5.0f);
		TestEqual(TEXT("Umgekehrte Span mit Doppelpunkt: max 2000"), Spec.TerrainMaxHeightSpanMeters, 2000.0f);
	}
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("dense district, height span 1500 to 2"));
		TestEqual(TEXT("Umgekehrte Span englisch: min 2"), Spec.TerrainMinHeightSpanMeters, 2.0f);
		TestEqual(TEXT("Umgekehrte Span englisch: max 1500"), Spec.TerrainMaxHeightSpanMeters, 1500.0f);
	}
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Vorstadt, hoehen min 4000 max 10"));
		TestEqual(TEXT("Umgekehrtes hoehen min/max: min 10"), Spec.TerrainMinHeightSpanMeters, 10.0f);
		TestEqual(TEXT("Umgekehrtes hoehen min/max: max 4000"), Spec.TerrainMaxHeightSpanMeters, 4000.0f);
	}
	// Komma als Dezimaltrenner + umgekehrte Span zusammen: der Parser muss
	// "2000,5" als eine Zahl (2000.5) lesen, "5,5" als zweite (5.5) und dann
	// min/max tauschen (2000.5 > 5.5) - Swap UND Dezimaltrenner zusammen.
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Innenstadt, hoehenspanne 2000,5..5,5"));
		TestEqual(TEXT("Komma-Dezimaltrenner + Swap: min 5.5"), Spec.TerrainMinHeightSpanMeters, 5.5f);
		TestEqual(TEXT("Komma-Dezimaltrenner + Swap: max 2000.5"), Spec.TerrainMaxHeightSpanMeters, 2000.5f);
	}
	// Derselbe Komma-Dezimaltrenner in korrekter Reihenfolge: kein Blind-Swap.
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Innenstadt, hoehenspanne 5,5..2000,5"));
		TestEqual(TEXT("Komma-Dezimaltrenner normal: min 5.5"), Spec.TerrainMinHeightSpanMeters, 5.5f);
		TestEqual(TEXT("Komma-Dezimaltrenner normal: max 2000.5"), Spec.TerrainMaxHeightSpanMeters, 2000.5f);
	}
	// Korrekte Reihenfolge bleibt unveraendert (kein Blind-Swap bei min < max).
	{
		const FCityPromptSpec Spec = CityPromptParser::Parse(TEXT("Innenstadt, hoehenspanne 5..2000"));
		TestEqual(TEXT("Normale Span bleibt: min 5"), Spec.TerrainMinHeightSpanMeters, 5.0f);
		TestEqual(TEXT("Normale Span bleibt: max 2000"), Spec.TerrainMaxHeightSpanMeters, 2000.0f);
	}

	// -- Defaults ohne Erwaehnung / leerer Prompt ---------------------------
	TestEqual(TEXT("Ohne Faktor -> 2.0"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt")).TerrainMaxTileToOsmRatio, 2.0f);
	TestEqual(TEXT("Ohne Span -> min 1"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt")).TerrainMinHeightSpanMeters, 1.0f);
	TestEqual(TEXT("Ohne Span -> max 3000"),
		CityPromptParser::Parse(TEXT("dichte Innenstadt")).TerrainMaxHeightSpanMeters, 3000.0f);
	TestEqual(TEXT("Leer: Faktor -> 2.0"),
		CityPromptParser::Parse(TEXT("")).TerrainMaxTileToOsmRatio, 2.0f);
	TestEqual(TEXT("Leer: Span min -> 1"),
		CityPromptParser::Parse(TEXT("")).TerrainMinHeightSpanMeters, 1.0f);
	TestEqual(TEXT("Leer: Span max -> 3000"),
		CityPromptParser::Parse(TEXT("")).TerrainMaxHeightSpanMeters, 3000.0f);

	return true;
}

// ---------------------------------------------------------------------------
// Fixture-Test: gemeinsame Erwartungen mit dem Node-Port
// (Tools/verify_cityprompt.mjs). Die Faelle liegen in
// Content/Config/CityPromptSpecFixture.json - ein Fall wird hier gegen den
// ECHTEN C++-Parser geprueft, im Node-Port gegen den Port-Parser. Aendert
// eine Seite den Parser, muss die Fixture (und damit beide Konsumenten) gruen
// bleiben - der Parser-Abgleich ist damit bidirektional abgesichert.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityPromptSpecFixtureTest,
	"WiesbadenReal.GIS.CityPrompt.SpecFixture",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCityPromptSpecFixtureTest::RunTest(const FString& Parameters)
{
	const FString FixturePath = FPaths::ProjectContentDir() + TEXT("Config/CityPromptSpecFixture.json");

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FixturePath))
	{
		AddError(FString::Printf(TEXT("Fixture nicht lesbar: %s"), *FixturePath));
		return false;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			AddError(TEXT("Fixture-JSON ungueltig."));
			return false;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* CasesPtr = nullptr;
	if (!Root->TryGetArrayField(TEXT("Cases"), CasesPtr))
	{
		AddError(FString::Printf(TEXT("Fixture ohne 'Cases'-Array: %s"), *FixturePath));
		return false;
	}

	int32 CheckedCases = 0;
	for (const TSharedPtr<FJsonValue>& CaseValue : *CasesPtr)
	{
		const TSharedPtr<FJsonObject> CaseObj = CaseValue->AsObject();
		if (!CaseObj)
		{
			AddError(TEXT("Fixture-Fall ist kein JSON-Objekt."));
			continue;
		}
		const FString CaseName = CaseObj->GetStringField(TEXT("Name"));
		const FString Prompt = CaseObj->GetStringField(TEXT("Prompt"));
		const TSharedPtr<FJsonObject> Expect = CaseObj->GetObjectField(TEXT("Expect"));
		if (!Expect.IsValid())
		{
			AddError(FString::Printf(TEXT("%s: ohne 'Expect'-Objekt"), *CaseName));
			continue;
		}

		const FCityPromptSpec Spec = CityPromptParser::Parse(Prompt);
		const FString Label = FString::Printf(TEXT("%s"), *CaseName);

		// Gleitkomma-Vergleich mit Toleranz (JSON-Roundtrip).
		const auto CheckNear = [&](const TCHAR* Field, float Actual, float Expected)
		{
			TestTrue(*FString::Printf(TEXT("%s: %s (erwartet %.4f, erhalten %.4f)"),
				*Label, Field, Expected, Actual),
				FMath::IsNearlyEqual(Actual, Expected, 1e-3f));
		};

		TestEqual(*FString::Printf(TEXT("%s: DisplayName"), *Label),
			Spec.DisplayName, Expect->GetStringField(TEXT("DisplayName")));
		CheckNear(TEXT("BuildingDensity"), Spec.BuildingDensity, Expect->GetNumberField(TEXT("BuildingDensity")));
		CheckNear(TEXT("TimeOfDayHours"), Spec.TimeOfDayHours, Expect->GetNumberField(TEXT("TimeOfDayHours")));
		CheckNear(TEXT("TrafficDensity"), Spec.TrafficDensity, Expect->GetNumberField(TEXT("TrafficDensity")));
		CheckNear(TEXT("BuildingHeightScale"), Spec.BuildingHeightScale, Expect->GetNumberField(TEXT("BuildingHeightScale")));
		CheckNear(TEXT("TerrainMaxTileToOsmRatio"), Spec.TerrainMaxTileToOsmRatio, Expect->GetNumberField(TEXT("TerrainMaxTileToOsmRatio")));
		CheckNear(TEXT("TerrainMinHeightSpanMeters"), Spec.TerrainMinHeightSpanMeters, Expect->GetNumberField(TEXT("TerrainMinHeightSpanMeters")));
		CheckNear(TEXT("TerrainMaxHeightSpanMeters"), Spec.TerrainMaxHeightSpanMeters, Expect->GetNumberField(TEXT("TerrainMaxHeightSpanMeters")));
		TestEqual(*FString::Printf(TEXT("%s: Weather"), *Label),
			static_cast<int32>(Spec.Weather), static_cast<int32>(Expect->GetNumberField(TEXT("Weather"))));
		TestEqual(*FString::Printf(TEXT("%s: MatchedKeywordCount"), *Label),
			Spec.MatchedKeywordCount, Expect->GetIntegerField(TEXT("MatchedKeywordCount")));

		// Fassaden-Gewichte: Schluessel-Mengen vergleichen (TMap-Iteration ist
		// Hash-basiert, Reihenfolge nicht garantiert).
		{
			const TSharedPtr<FJsonObject> WeightsObj = Expect->GetObjectField(TEXT("FacadeVariantWeights"));
			TMap<int32, float> ExpectedWeights;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : WeightsObj->Values)
			{
				ExpectedWeights.Add(FCString::Atoi(*Pair.Key), Pair.Value->AsNumber());
			}
			TArray<int32> ActualKeys;
			for (const TPair<int32, float>& Pair : Spec.FacadeVariantWeights) { ActualKeys.Add(Pair.Key); }
			ActualKeys.Sort();
			TArray<int32> ExpectedKeys;
			for (const TPair<int32, float>& Pair : ExpectedWeights) { ExpectedKeys.Add(Pair.Key); }
			ExpectedKeys.Sort();

			TestEqual(*FString::Printf(TEXT("%s: FacadeVariantWeights-Anzahl"), *Label),
				ActualKeys.Num(), ExpectedKeys.Num());
			if (ActualKeys.Num() == ExpectedKeys.Num())
			{
				for (int32 i = 0; i < ExpectedKeys.Num(); ++i)
				{
					if (ActualKeys[i] != ExpectedKeys[i])
					{
						AddError(FString::Printf(TEXT("%s: Varianten-Schluessel %d erwartet, %d erhalten"),
							*Label, ExpectedKeys[i], ActualKeys[i]));
						break;
					}
					const float* ActualWeight = Spec.FacadeVariantWeights.Find(ExpectedKeys[i]);
					TestTrue(*FString::Printf(TEXT("%s: Gewicht Variante %d (erwartet %.2f, erhalten %.2f)"),
						*Label, ExpectedKeys[i], ExpectedWeights[ExpectedKeys[i]],
						ActualWeight ? *ActualWeight : -1.0f),
						ActualWeight && FMath::IsNearlyEqual(*ActualWeight, ExpectedWeights[ExpectedKeys[i]], 1e-3f));
				}
			}
		}

		// Landmarken als Mengen vergleichen (Insertionsreihenfolge egal).
		{
			TArray<FString> ExpectedLm;
			for (const TSharedPtr<FJsonValue>& V : Expect->GetArrayField(TEXT("DetectedLandmarks")))
			{
				ExpectedLm.Add(V->AsString());
			}
			ExpectedLm.Sort();
			TArray<FString> ActualLm = Spec.DetectedLandmarks;
			ActualLm.Sort();
			TestTrue(*FString::Printf(TEXT("%s: DetectedLandmarks (erwartet [%s], erhalten [%s])"),
				*Label, *FString::Join(ExpectedLm, TEXT(",")), *FString::Join(ActualLm, TEXT(","))),
				ActualLm == ExpectedLm);
		}

		++CheckedCases;
	}

	AddInfo(FString::Printf(TEXT("%d Fixture-Faelle gegen den echten Parser geprueft."), CheckedCases));
	return CheckedCases > 0;
}
