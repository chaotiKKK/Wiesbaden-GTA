# Runtime-Paketinhalt: gezielter Cook und JSON-Staging

Stand: 30.09.2026. Dieser Schnitt schliesst die im Bestandsbericht belegten
**Konfigurationsluecken**, ohne Featurelogik, Assetinhalt oder Paketarchive zu
veraendern. Ein neuer Cook und eine Abnahme der archivierten EXE stehen noch aus.

## Eine Quelle fuer die Auswahl

[Config/DefaultGame.ini](../../Config/DefaultGame.ini) haelt beide Listen:

- `[/Script/UnrealEd.ProjectPackagingSettings] / DirectoriesToAlwaysCook`:
  begrenzte Asset-Familien fuer dynamische `LoadObject`-Pfade.
- `[WiesbadenReal.RuntimeStaging] / RuntimeFile`:
  **neun explizite JSON-Dateien**, relativ zum Projekt, keine Wildcards.

Die bestehenden acht Cook-Gruppen bleiben erhalten. Kein `bCookAll`, kein
pauschaler `/Game`-Cook und kein Staging ganzer Rohdaten-Verzeichnisse.

## Ergaenzte Cook-Gruppen

| Gruppe | Bestehender Verbraucher / Grund |
|---|---|
| `/Game/Audio/Meta` | [AudioPropagation](../../Source/WiesbadenReal/Audio/WiesbadenAudioPropagation.cpp) laedt Boxer/5 Ambience-Betten; [AudioZonesSubsystem](../../Source/WiesbadenReal/Audio/WiesbadenAudioZonesSubsystem.cpp) vier `MS_Step_*`. |
| `/Game/Audio/Samples` | [CarAudio](../../Source/WiesbadenReal/Vehicles/WiesbadenCarAudioComponent.cpp) und [TireEffects](../../Source/WiesbadenReal/Vehicles/WiesbadenTireEffectsComponent.cpp) laden Fahrzeug-/Reifen-/Effektklaenge. |
| `/Game/Audio/Bus/Announce` | [BusRoute](../../Source/WiesbadenReal/World/WiesbadenBusRoute.cpp) loest beide Ansageindizes nach Haltenamen zu Wellen auf. JSON allein genuegt nicht. |
| `/Game/Props/EsweHalte` | [BusStopMonitor](../../Source/WiesbadenReal/World/WiesbadenBusStopMonitor.cpp) laedt Halle, Mast und DFI. Die alte Gruppe `/Game/Props/DFI` deckt diese nicht ab. |
| `/Game/SebboTower/Meshes` | [SebboHqShape](../../Source/WiesbadenReal/World/SebboHqShape.cpp) liefert Schuessel, Mast, Logo, Pflanzen und Magazin. Der Cooker folgt den Material-/Texturreferenzen der Meshes. |
| `/Game/Vehicles/Beetle/Restored` | [CarLights](../../Source/WiesbadenReal/Vehicles/WiesbadenCarLightsComponent.cpp) laedt `M_WbBeetleLamp`; die begrenzte Familie ersetzt keinen Cook aller Fahrzeuge. |

`.blend`, `.fbx`, PNG-Quellen und Logs sind keine UFS-Laufzeitdateien. Die
Cook-Gruppen selektieren Unreal-Pakete; lose Quellen unter ihnen werden nicht
als RuntimeDependencies hinzugefuegt.

## Explizite Laufzeitdaten

| Projektpfad | Verbraucher |
|---|---|
| `Data/Raw/Bus/line3.json` | [GameMode](../../Source/WiesbadenReal/Core/WiesbadenGameMode.cpp) -> [BusLineFile](../../Source/WiesbadenReal/World/WiesbadenBusLineFile.cpp): Linie 3 und Monitore |
| `Data/Raw/Bus/line6.json` | dieselbe Kette fuer Linie 6 |
| `Data/Raw/Bus/line6_schedule.json` | Linie-6-Fahrplan; fuer Linie 3 ist kein Fahrplan konfiguriert |
| `Data/Raw/Bus/announce_line3.json` | [BusRoute](../../Source/WiesbadenReal/World/WiesbadenBusRoute.cpp): Haltename -> Soundasset |
| `Data/Raw/Bus/announce_line6.json` | dieselbe Kette fuer Linie 6 |
| `Data/Missions/missions.json` | [MissionSubsystem](../../Source/WiesbadenReal/Missions/WiesbadenMissionSubsystem.cpp) |
| `Data/Store/unlocks.json` | [StoreSubsystem](../../Source/WiesbadenReal/Store/WiesbadenStoreSubsystem.cpp) |
| `Content/Config/TrafficSignCatalog.json` | [TrafficSignCatalog](../../Source/WiesbadenReal/GIS/WiesbadenTrafficSignCatalog.cpp) |
| `Content/Config/WiesbadenRoadTypes.json` | [RoadTypeLibrary](../../Source/WiesbadenReal/GIS/RoadTypeLibrary.cpp) |

`WeatherFXCatalog.json` ist eine Authoring-/Testreferenz; die Runtime verwendet
fest kodierte Vertraege, keinen Dateilader fuer diesen Katalog.
`CityPromptSpecFixture.json` ist eine Testfixture. Beide werden hier nicht
pauschal mitgenommen. OSM, DEM, Tripo-/Blender-Quellen bleiben Entwicklungsdaten.

## Wie die Auswahl im Paket landet

[WiesbadenReal.Build.cs](../../Source/WiesbadenReal/WiesbadenReal.Build.cs)
liest die Game-Konfigurationshierarchie mit `ConfigCache.ReadHierarchy`, dann
die `RuntimeFile`-Liste. Jeder Eintrag wird als
`RuntimeDependencies.Add("$(ProjectDir)/" + RelativePath, StagedFileType.UFS)`
in den Target-Receipt geschrieben. UAT uebernimmt diese Receipt-Abhaengigkeiten
beim Staging und erhaelt den relativen Projektpfad (z. B.
`WiesbadenReal/Data/Raw/Bus/line6.json`).

**Warum UFS:** Die vorhandenen Lader nutzen `FFileHelper` ueber Unreals
Dateisystem. Die Daten bleiben so auch aus dem Paket unter
`FPaths::ProjectDir()` / `ProjectContentDir()` lesbar. Es werden weder die
Lader umgeschrieben noch JSON neben alten Archiven manuell nachkopiert.

Die Buildregel lehnt leere Listen, fehlende Dateien sowie absolute, aus dem
Projekt fuehrende und Nicht-JSON-Pfade ab. Wildcards, Laufwerksbuchstaben und
UNC-Pfade fallen ueber dieselben beiden Pruefungen (gemesst).

**Damit der gecachte Receipt jede Aenderung mitnimmt**, registriert die Regel
alle Orte der Game-Konfigurationshierarchie im Projekt als `ExternalDependency`
- auch die noch nicht existierenden. GEMESSEN am 30.09.2026 in einem
Wegwerfprojekt mit derselben Regel: mit `DefaultGame.ini` allein blieb eine
nachtraeglich angelegte `Config/Windows/WindowsGame.ini` ohne Wirkung - UBT
meldete `Result: Succeeded` und schrieb die alte Dateiliste in den Receipt.
Ein Plattform-Override ist **additiv** (`+RuntimeFile=...` erweitert die
Default-Liste); `!RuntimeFile=ClearArray` leert sie und laesst den Build mit
einer `BuildException` scheitern, statt die Dateien still zu verlieren.

## Checks

[Tools/test_runtime_packaging.py](../../Tools/test_runtime_packaging.py) laeuft
ohne Editor oder Cook und mit der vorhandenen `unittest discover -s Tools`
-Suite mit. `WB_PACKAGING_RECEIPT` schaltet den echten UBT-Receipt ein,
`WB_PACKAGING_ENGINE` die drei Engine-Proben (Wegwerfprojekt unter
`Intermediate/`, ~3 min) - beide nur unter dem Engine-Lock starten:

```bash
python -m unittest discover -s Tools -p test_runtime_packaging.py -v

WB_PACKAGING_RECEIPT=Binaries/Win64/WiesbadenReal.target \
  python -m unittest discover -s Tools -p test_runtime_packaging.py -v

WB_PACKAGING_ENGINE="C:\Program Files\Epic Games\UE_5.8\Engine" \
  WB_PACKAGING_RECEIPT=Binaries/Win64/WiesbadenReal.target \
  python -m unittest discover -s Tools -p test_runtime_packaging.py -k RuntimePackagingUbtTest -v
```

Ohne gesetzte Variablen wird der betreffende Test explizit uebersprungen, nicht
als bestanden ausgegeben.

**Nachweise 30.09.2026:**

- Win64 Game Development Build (`Build.bat`): `Result: Succeeded`, 7,51 s. Kein
  Cook/Stage.
- Echter [Game-Receipt](../../Binaries/Win64/WiesbadenReal.target): alle neun
  ausgewaehlten JSON als `$(ProjectDir)/...`, `Type=UFS`, sonst keine
  Projekt-Laufzeitdaten.
- Vertrags-Suite inkl. echtem Receipt: **9/9 gruen**; Engine-Proben **3/3
  gruen** (173 s). Dort gemessen und behoben: der eigene INI-Leser muss
  Anfuehrungszeichen **entfernen**, Schluessel sind case-insensitiv, ein leerer
  Schluesselname stuertzte ihn. Wegwerfprojekt, 14 echte UBT-Laeufe: additiver
  Override, Aenderung nach gecachtem Makefile, leerer Override, geloeschte
  Override, sieben Grenzwerte, fehlende Datei - die Fehlerfaelle melden
  `RulesError` mit `RuntimeStaging:`.
- Die UAT-Kette ohne Cook belegt: `TargetReceipt.Read` (der Aufruf aus
  [WinPlatform.GetFilesToDeployOrStage]) loest `$(ProjectDir)` auf, danach
  existieren alle neun Pfade (`Exists=True`) und
  `DeploymentContext.GetStagedFileLocation` legt sie unterhalb des
  Projektwurzelrelativs ab. Ein nicht aufgeloester Marker wuerde die Dateien
  still ueberspringen - das war der Fehler des Pakets vom 30.09.
- `git diff --check` gruen. Zusaetzliche allgemeine Textleser-Suite: **8/9**,
  bestehender Fehler in
  [test_engine_run_lock.py:115](../../Tools/test_engine_run_lock.py#L115).
  Datei nicht geaendert; kein gruener Gesamt-Gate-Stand behauptet.

## Spaeteres Abnahme-Gate

Nach Abschluss der Feature-Reste: frischer Development-Cook/Stage/Archive in
einem neuen datierten Ordner, fremde Archive unangetastet. Nicht `-skipcook`
oder alte Receipts verwenden, um neue Auswahlregeln zu beweisen.

Dann UFS-Manifest und archivierte EXE pruefen: beide Buslinien mit Route > 0,
Haltestellen-/Dachassets vorhanden, Fussschrittpool 4/4, Boxer/Reifen/Ambience
ladbar, Missions-/Store-Kataloge geladen, Kaefer-Lampenmaterial verfuegbar.
Erst diese Pruefung beweist die Paketverfuegbarkeit; Quellpfade und ein
UBT-Receipt ersetzen sie nicht.

Vor jedem Engine-Lauf Lock, Prozesse und Speicher neu pruefen, fremde Laeufe
abwarten. Dieser Pass erstellte keinen neuen Paket-/Desktoplink und fuehrte
keine Release-/Rollback-Operation aus.
