// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenWorldBuilder.h"
#include "World/WiesbadenCitySubsystem.h"

/**
 * Verdrahtung des Map-Bake-Workflows (Stadt nach dem Editor-Build als echte
 * .umap speichern, damit sie ohne Neugenerierung sofort da ist):
 *  1. ShouldRunRuntimeBuild: Liegt eine gebackene Stadt (AWiesbadenWorldBuilder
 *     mit bCityBaked) im Level, darf die Laufzeit-Pipeline NICHT erneut bauen
 *     (sonst doppelte Geometrie + Minuten Wartezeit beim Play).
 *  2. WorldBuilder-Defaults: bCityBaked startet false (nur ein erfolgreicher
 *     BuildCity setzt es), MapAssetPath zeigt auf den Maps-Ordner.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapBakeTest,
	"WiesbadenReal.Core.MapBake",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMapBakeTest::RunTest(const FString& Parameters)
{
	// -- 1. Laufzeit-Entscheidung bei gebackener Stadt -----------------------
	// Gebackene Stadt im Level -> nie bauen (auch wenn sonst alles frei waere).
	TestFalse(TEXT("Gebackene Stadt: kein Laufzeit-Build"),
		UWiesbadenCitySubsystem::ShouldRunRuntimeBuild(/*bCityBakedInLevel=*/true, /*bHasCityData=*/false,
			/*bGenerateAtRuntime=*/true, /*bIsCityDataLoading=*/false));
	TestFalse(TEXT("Gebackene Stadt + vorhandene Daten: kein Laufzeit-Build"),
		UWiesbadenCitySubsystem::ShouldRunRuntimeBuild(true, true, true, false));

	// Bestehende Regeln bleiben erhalten (Backing hat keine Auswirkung).
	TestFalse(TEXT("Daten vorhanden: kein Laufzeit-Build"),
		UWiesbadenCitySubsystem::ShouldRunRuntimeBuild(false, true, true, false));
	TestFalse(TEXT("Laufzeit-Build deaktiviert: kein Build"),
		UWiesbadenCitySubsystem::ShouldRunRuntimeBuild(false, false, false, false));
	TestFalse(TEXT("Build laeuft bereits: kein zweiter Build"),
		UWiesbadenCitySubsystem::ShouldRunRuntimeBuild(false, false, true, true));
	TestTrue(TEXT("Alles frei: Laufzeit-Build starten"),
		UWiesbadenCitySubsystem::ShouldRunRuntimeBuild(false, false, true, false));

	// -- 2. WorldBuilder-Defaults --------------------------------------------
	const AWiesbadenWorldBuilder* WB = GetDefault<AWiesbadenWorldBuilder>();
	TestNotNull(TEXT("WorldBuilder-CDO vorhanden"), WB);

	TestFalse(TEXT("bCityBaked startet false (erst BuildCity setzt es)"), WB->bCityBaked);
	TestFalse(TEXT("MapAssetPath konfiguriert"), WB->MapAssetPath.IsEmpty());
	TestTrue(TEXT("MapAssetPath zeigt auf den Maps-Ordner"),
		WB->MapAssetPath.StartsWith(TEXT("/Game/Maps/")));

	// Auto-Save ist ein Opt-in: erst wenn der Nutzer es im Details-Panel
	// aktiviert, speichert BuildCity die Map nach erfolgreichem Build selbst
	// (SaveCityAsMap inkl. World-Partition-Aktivierung). Default false, damit
	// der Build nicht ungefragt Dateien schreibt.
	TestFalse(TEXT("Auto-Save default deaktiviert (Opt-in)"), WB->bAutoSaveCityAsMap);

	// Ergebnis-Feedback des Auto-Saves im Details-Panel: bAutoSaveSucceeded
	// zeigt, ob der letzte SaveCityAsMap-Versuch geklappt hat (Details dazu in
	// LastError). Default false - erst ein tatsaechlicher Save-Aufruf setzt es
	// auf das echte Ergebnis (True erst nach erfolgreichem SaveMap).
	TestFalse(TEXT("Auto-Save-Ergebnis startet false (noch kein Versuch)"), WB->bAutoSaveSucceeded);

	// Letzter Build im Details-Panel (ohne CSV/Datei-Lookup): Dauer, Ergebnis
	// und Zeitpunkt werden bei JEDEM BuildCity-Ausgang gesetzt (Erfolg,
	// Abbruch, Fehler) und liegen seit der FLastBuildInfo-Konsolidierung in
	// einer gemeinsamen USTRUCT (Zeitpunkt/Dauer/Ergebnis + Einzeiler).
	// Defaults: noch kein Lauf.
	TestTrue(TEXT("Letzter Build: FLastBuildInfo leer"), WB->LastBuild.IsEmpty());
	TestEqual(TEXT("Letzter Build: Dauer startet 0"), WB->LastBuild.DurationSeconds, 0.0);
	TestTrue(TEXT("Letzter Build: Ergebnis leer"), WB->LastBuild.Result.IsEmpty());
	TestTrue(TEXT("Letzter Build: Zeitpunkt leer"), WB->LastBuild.Timestamp.IsEmpty());
	TestTrue(TEXT("Letzter Build: Einzeiler leer"), WB->LastBuildSummary.IsEmpty());

	// -- 3. Verkehrs-Simulation im gebackenen Pfad ----------------------------
	// Der WorldBuilder traegt die TrafficSettings (EditAnywhere, nicht transient
	// -> in der Map serialisiert), damit das CitySubsystem die Simulation im
	// gebackenen Pfad mit denselben Werten starten kann wie der Laufzeit-Pfad
	// (dort kommen sie aus FWiesbadenCityData::TrafficSettings).
	TestEqual(TEXT("WorldBuilder: TrafficSettings-Defaultdichte 0.5"),
		WB->TrafficSettings.TrafficDensity, 0.5f);
	TestTrue(TEXT("WorldBuilder: MaxSpawnRate konfigurierbar"),
		WB->TrafficSettings.MaxSpawnRatePerSecond > 0.0f);

	// Entscheidungsregel des Subsystems: Eine gebackene Stadt ohne Strassennetz
	// darf die Simulation nicht initialisieren (leeres Netz = kein Verkehr); mit
	// Netz wird initialisiert. Die Bedingung ist dieselbe wie im Laufzeit-Pfad
	// (SpawnCityActor: nur bei nicht-leerem Netz).
	FRoadNetwork EmptyNetwork;
	FRoadNetwork NonEmptyNetwork;
	NonEmptyNetwork.Segments.Emplace();
	TestTrue(TEXT("Leeres Netz: IsEmpty true"), EmptyNetwork.IsEmpty());
	TestFalse(TEXT("Netz mit Segment: nicht leer"), NonEmptyNetwork.IsEmpty());

	// -- 4. World-Partition-Verifikation nach dem Auto-Save ------------------
	// VerifyWorldPartitionSave prueft nach dem SaveCityAsMap, ob die Map
	// wirklich als streamendes World-Partition-Level gespeichert wurde:
	// bIsPartitioned (UWorldPartition-Objekt im Level), bMapExists (.umap auf
	// der Platte), bExternalActors (External-Actor-Packages fuer die Map auf
	// der Platte = die Chunk-Actors wurden als WP-Zellen externalisiert) und
	// bChunksInSeparatePackages (jeder Chunk in seinem EIGENEN Package - ohne
	// diese Pruefung kaeme die kaputte Variante durch, in der alle Chunks in
	// EINEM Package liegen und beim Laden mit "Failed import" scheitern,
	// obwohl Packages auf der Platte existieren; genau das passierte beim
	// ALKIS-Rebuild 08-2026). Nur wenn alle vier stimmen, gilt der Auto-Save
	// als verifiziert; sonst false + spezifische Fehlermeldung.
	FString VerifyError;
	TestTrue(TEXT("WP-Verifikation: alles ok"),
		AWiesbadenWorldBuilder::VerifyWorldPartitionSave(true, true, true, true, VerifyError));
	TestTrue(TEXT("WP-Verifikation: keine Fehlermeldung bei Erfolg"), VerifyError.IsEmpty());

	TestFalse(TEXT("WP-Verifikation: nicht partitioniert -> fail"),
		AWiesbadenWorldBuilder::VerifyWorldPartitionSave(false, true, true, true, VerifyError));
	TestTrue(TEXT("WP-Verifikation: Fehlermeldung nennt bIsPartitioned"),
		VerifyError.Contains(TEXT("bIsPartitioned")));

	TestFalse(TEXT("WP-Verifikation: Map-Datei fehlt -> fail"),
		AWiesbadenWorldBuilder::VerifyWorldPartitionSave(true, false, true, true, VerifyError));
	TestTrue(TEXT("WP-Verifikation: Fehlermeldung nennt die Map-Datei"),
		VerifyError.Contains(TEXT("Map")));

	TestFalse(TEXT("WP-Verifikation: keine External-Actors -> fail"),
		AWiesbadenWorldBuilder::VerifyWorldPartitionSave(true, true, false, true, VerifyError));
	TestTrue(TEXT("WP-Verifikation: Fehlermeldung nennt External-Actors"),
		VerifyError.Contains(TEXT("External-Actor")));

	// Die kaputte Layout-Variante: Packages existieren, aber alle Chunks
	// teilen sich EIN Package. Genau das meldete der ALKIS-Rebuild faelschlich
	// als "ok", obwohl die Stadt beim Laden unsichtbar war (1984
	// "Failed import for WiesbadenCityChunk"-Fehler im Game-Lauf).
	TestFalse(TEXT("WP-Verifikation: Chunks in einem Package -> fail"),
		AWiesbadenWorldBuilder::VerifyWorldPartitionSave(true, true, true, false, VerifyError));
	TestTrue(TEXT("WP-Verifikation: Fehlermeldung nennt eigene Packages"),
		VerifyError.Contains(TEXT("eigenen External-Actor-Packages")));

	// -- 5. Default-Map-Verdrahtung (datenreiner Ini-Helfer) ------------------
	// SaveCityAsMap schreibt GameDefaultMap/EditorStartupMap in die
	// DefaultEngine.ini. Der Helfer ist datenrein (Text -> Text), damit die
	// Logik headless testbar ist - der echte Datei-Write passiert im Editor
	// ueber FFileHelper mit Verifikation (GConfig->SetString+Flush kann in
	// UE 5.8 still nichts schreiben, wenn der Branch-Lookup fehlschlaegt).
	{
		const FString MapRef = TEXT("/Game/Maps/WiesbadenCity.WiesbadenCity");

		// Leerer Text: Section wird angelegt, beide Keys gesetzt.
		const FString FromEmpty = ApplyDefaultMapToIniText(TEXT(""), MapRef);
		TestTrue(TEXT("Leer: Section angelegt"),
			FromEmpty.Contains(TEXT("[/Script/EngineSettings.GameMapsSettings]")));
		TestTrue(TEXT("Leer: GameDefaultMap gesetzt"),
			FromEmpty.Contains(TEXT("GameDefaultMap=") + MapRef));
		TestTrue(TEXT("Leer: EditorStartupMap gesetzt"),
			FromEmpty.Contains(TEXT("EditorStartupMap=") + MapRef));

		// Bestehende Section mit anderen Keys: Keys werden ergaenzt, andere
		// Eintraege bleiben unveraendert.
		const FString WithSection = FString::Printf(TEXT("[/Script/EngineSettings.GameMapsSettings]\r\n")
			TEXT("GlobalDefaultGameMode=/Script/WiesbadenReal.WiesbadenGameMode\r\n"));
		const FString Extended = ApplyDefaultMapToIniText(WithSection, MapRef);
		TestTrue(TEXT("Bestehend: GameMode bleibt"),
			Extended.Contains(TEXT("GlobalDefaultGameMode=/Script/WiesbadenReal.WiesbadenGameMode")));
		TestTrue(TEXT("Bestehend: GameDefaultMap ergaenzt"),
			Extended.Contains(TEXT("GameDefaultMap=") + MapRef));
		TestTrue(TEXT("Bestehend: EditorStartupMap ergaenzt"),
			Extended.Contains(TEXT("EditorStartupMap=") + MapRef));

		// Vorhandene Keys mit anderem Wert: Wert wird ersetzt, nicht dupliziert.
		const FString OldValue = FString::Printf(TEXT("[/Script/EngineSettings.GameMapsSettings]\r\n")
			TEXT("GameDefaultMap=/Game/Maps/Old.Old\r\n")
			TEXT("EditorStartupMap=/Game/Maps/Old.Old\r\n"));
		const FString Replaced = ApplyDefaultMapToIniText(OldValue, MapRef);
		{
			int32 Count = 0;
			int32 Pos = 0;
			while (Replaced.Find(TEXT("GameDefaultMap="), ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos) != INDEX_NONE)
			{
				++Count;
				Pos = Replaced.Find(TEXT("GameDefaultMap="), ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos) + 15;
			}
			TestEqual(TEXT("Ersetzen: genau ein GameDefaultMap-Vorkommen"), Count, 1);
		}
		TestTrue(TEXT("Ersetzen: neuer Wert gesetzt"),
			Replaced.Contains(TEXT("GameDefaultMap=") + MapRef));
		TestFalse(TEXT("Ersetzen: alter Wert weg"),
			Replaced.Contains(TEXT("Old.Old")));

		// Idempotenz: zweimal anwenden aendert nichts mehr.
		TestEqual(TEXT("Idempotent: zweiter Lauf identisch"),
			ApplyDefaultMapToIniText(Extended, MapRef), Extended);
	}

	return true;
}
