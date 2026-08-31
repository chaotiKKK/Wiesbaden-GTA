// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

#include "Core/WiesbadenGameInstance.h"
#include "Core/WiesbadenGameMode.h"
#include "World/WiesbadenCitySubsystem.h"

/**
 * Verhindert die Regression "Stadt ist weg": Die Laufzeit-Pipeline braucht
 * drei Verdrahtungen, die alle in den Projekt-Configs liegen muessen:
 *  1. GameInstanceClass (/Script/WiesbadenReal.WiesbadenGameInstance) in
 *     DefaultEngine.ini - ohne sie liefert World->GetGameInstance() den
 *     Default-UGameInstance und das CitySubsystem bricht die Initialisierung
 *     ab (kein Haus, keine Strasse, kein Terrain im PIE).
 *  2. GlobalDefaultGameMode (AWiesbadenGameMode) - ohne ihn laeuft die Welt
 *     mit dem Basis-GameModeBase.
 *  3. OsmFilePath/DemFilePath in DefaultGame.ini - relativ zum Projekt
 *     aufgeloest und auf existierende Dateien geprueft (die Daten sind
 *     gitignored, ein frischer Checkout haette sie nicht).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeConfigTest,
	"WiesbadenReal.Core.RuntimeConfig",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRuntimeConfigTest::RunTest(const FString& Parameters)
{
	// -- 1. GameInstance-Klasse verdrahtet? ----------------------------------
	FString GameInstanceClass;
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameInstanceClass"),
		GameInstanceClass, GEngineIni);
	TestTrue(TEXT("GameInstanceClass auf WiesbadenGameInstance verdrahtet"),
		GameInstanceClass.Contains(TEXT("WiesbadenGameInstance")));

	// -- 2. GameMode verdrahtet? ---------------------------------------------
	FString GameMode;
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"),
		GameMode, GEngineIni);
	TestTrue(TEXT("GlobalDefaultGameMode auf WiesbadenGameMode verdrahtet"),
		GameMode.Contains(TEXT("WiesbadenGameMode")));

	// -- 3. Datenpfade des GameInstance (DefaultGame.ini) --------------------
	const UWiesbadenGameInstance* GI = GetDefault<UWiesbadenGameInstance>();
	TestNotNull(TEXT("GameInstance-CDO vorhanden"), GI);

	TestFalse(TEXT("OsmFilePath konfiguriert"), GI->OsmFilePath.IsEmpty());
	if (!GI->OsmFilePath.IsEmpty())
	{
		const FString OsmPath = FPaths::IsRelative(GI->OsmFilePath)
			? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), GI->OsmFilePath)
			: GI->OsmFilePath;
		TestTrue(FString::Printf(TEXT("OSM-Datei vorhanden: %s"), *OsmPath), FPaths::FileExists(OsmPath));
	}

	TestFalse(TEXT("DemFilePath konfiguriert"), GI->DemFilePath.IsEmpty());
	if (!GI->DemFilePath.IsEmpty())
	{
		const FString DemPath = FPaths::IsRelative(GI->DemFilePath)
			? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), GI->DemFilePath)
			: GI->DemFilePath;
		TestTrue(FString::Printf(TEXT("DEM-Datei vorhanden: %s"), *DemPath), FPaths::FileExists(DemPath));
	}

	TestTrue(TEXT("Laufzeit-Build aktiv (bGenerateAtRuntime)"), GI->ShouldGenerateAtRuntime());

	// -- 3b. Terrain-Qualitaetswarnung (Blueprint/GetCityStatus-abrufbar) -----
	// Die Warnung steckt in CityData->Status (GetCityStatus) und ist zusaetzlich
	// als strukturierter Getter abrufbar. CDO: noch kein Build -> leere Meldung.
	{
		const FTerrainQualityReport Q = GI->GetTerrainQuality();
		TestTrue(TEXT("Terrain-Qualitaet: keine Warnung im CDO"), Q.WarningMessage.IsEmpty());
		TestFalse(TEXT("Terrain-Qualitaet: kein Tile-Flag im CDO"), Q.bWarnTileTooLarge);
		TestFalse(TEXT("Terrain-Qualitaet: kein Hoehen-Flag im CDO"), Q.bWarnHeightRangeSuspicious);
	}

	// -- 4. Letzter Laufzeit-Build (Blueprint/GetCityStatus-abrufbar) ---------
	// Zeitpunkt/Dauer/Ergebnis des letzten Play-Builds liegen in den
	// CityData und sind ueber die Getter abrufbar. Defaults: noch kein Lauf.
	TestTrue(TEXT("Letzter Build: Zeitpunkt leer"), GI->GetLastBuildTimestamp().IsEmpty());
	TestEqual(TEXT("Letzter Build: Dauer 0"), GI->GetLastBuildDurationSeconds(), 0.0);
	TestTrue(TEXT("Letzter Build: Ergebnis leer"), GI->GetLastBuildResult().IsEmpty());
	TestTrue(TEXT("Letzter Build: Einzeiler leer"), GI->GetLastBuildSummary().IsEmpty());
	TestTrue(TEXT("Letzter Build: FLastBuildInfo leer"), GI->GetLastBuildInfo().IsEmpty());

	// -- 5. Letzter Build ueber Subsystem/GameMode (Level-Blueprints) ---------
	// Die Getter delegieren an den GameInstance; ohne Welt/GameInstance
	// (CDO) muessen sie leere Defaults liefern, nie crashen.
	const UWiesbadenCitySubsystem* Sub = GetDefault<UWiesbadenCitySubsystem>();
	TestNotNull(TEXT("CitySubsystem-CDO vorhanden"), Sub);

	// Terrain-Qualitaet ueber Subsystem/GameMode: die Getter delegieren an den
	// GameInstance (bzw. ans Subsystem); ohne Welt/GameInstance (CDO) muessen
	// sie leere Defaults liefern, nie crashen.
	{
		const FTerrainQualityReport SubQ = Sub->GetTerrainQuality();
		TestTrue(TEXT("Subsystem: Terrain-Qualitaet ohne Warnung (CDO)"), SubQ.WarningMessage.IsEmpty());
		TestFalse(TEXT("Subsystem: kein Tile-Flag (CDO)"), SubQ.bWarnTileTooLarge);
		TestFalse(TEXT("Subsystem: kein Hoehen-Flag (CDO)"), SubQ.bWarnHeightRangeSuspicious);
	}

	TestTrue(TEXT("Subsystem: Zeitpunkt leer"), Sub->GetLastBuildTimestamp().IsEmpty());
	TestEqual(TEXT("Subsystem: Dauer 0"), Sub->GetLastBuildDurationSeconds(), 0.0);
	TestTrue(TEXT("Subsystem: Ergebnis leer"), Sub->GetLastBuildResult().IsEmpty());
	TestTrue(TEXT("Subsystem: Einzeiler leer"), Sub->GetLastBuildSummary().IsEmpty());
	TestTrue(TEXT("Subsystem: FLastBuildInfo leer"), Sub->GetLastBuildInfo().IsEmpty());

	const AWiesbadenGameMode* GM = GetDefault<AWiesbadenGameMode>();
	TestNotNull(TEXT("GameMode-CDO vorhanden"), GM);

	{
		const FTerrainQualityReport GMQ = GM->GetTerrainQuality();
		TestTrue(TEXT("GameMode: Terrain-Qualitaet ohne Warnung (CDO)"), GMQ.WarningMessage.IsEmpty());
		TestFalse(TEXT("GameMode: kein Tile-Flag (CDO)"), GMQ.bWarnTileTooLarge);
		TestFalse(TEXT("GameMode: kein Hoehen-Flag (CDO)"), GMQ.bWarnHeightRangeSuspicious);
	}

	TestTrue(TEXT("GameMode: Zeitpunkt leer"), GM->GetLastBuildTimestamp().IsEmpty());
	TestEqual(TEXT("GameMode: Dauer 0"), GM->GetLastBuildDurationSeconds(), 0.0);
	TestTrue(TEXT("GameMode: Ergebnis leer"), GM->GetLastBuildResult().IsEmpty());
	TestTrue(TEXT("GameMode: Einzeiler leer"), GM->GetLastBuildSummary().IsEmpty());
	TestTrue(TEXT("GameMode: FLastBuildInfo leer"), GM->GetLastBuildInfo().IsEmpty());

	return true;
}
