# Wiesbaden Real

Open-World-Action-Spiel im Stil von GTA V, das die Landeshauptstadt Wiesbaden
(Hessen) photorealistisch als Spielwelt abbildet — generiert aus echten
GIS-Daten (OpenStreetMap + Copernicus/SRTM-Höhendaten).

**Engine:** Unreal Engine 5.8.1+ · **Sprache:** C++ (Core) + Blueprints ·
**Maßstab:** 1 Unreal Unit = 1 cm · **Georeferenz:** WGS84 → ECEF → East-South-Up

## 🏁 Meilensteine

| Neue Spielfigur Sebbo | Stadtverkehr | Dennos Laden |
|---|---|---|
| ![Sebbo rennt](docs/meilensteine/bilder/14-sebbo-rennen.gif) | ![Verkehr am Kaiser-Friedrich-Ring](docs/meilensteine/bilder/13-verkehr-ring.gif) | ![Dennos Laden am Sedanplatz](docs/meilensteine/bilder/11-denno-laden.gif) |

Alle 14 Meilensteine mit GIFs und Fotos aus dem Spiel:
**[docs/meilensteine.md](docs/meilensteine.md)** – dazu je ein
[Release](https://github.com/chaotiKKK/Wiesbaden-GTA/releases). Öffentlich
(ohne Code) als Schaufenster:
**[chaotikkk.github.io/wiesbaden-real-meilensteine](https://chaotikkk.github.io/wiesbaden-real-meilensteine/)**
– aktualisieren mit `python Tools/schaufenster.py --ziel <Klon von chaotiKKK/wiesbaden-real-meilensteine>`,
dort committen und pushen.

## 📖 Dokumentation

Anleitungen, Referenzen und Hintergründe liegen unter [`docs/`](docs/README.md) —
nach dem [Diátaxis](https://diataxis.fr/)-Rahmen geordnet (Tutorial / How-to /
Reference / Explanation). Guter Einstieg: das Tutorial
[Erste Fahrt durch Wiesbaden](docs/tutorials/erste-fahrt.md).

**Frischer Klon:** Die gebackene Stadt liegt nicht im Repo (2,5 GB je Karte, größte
Einzeldatei 1,21 GB). Einmal `Tools\fetch_city_content.cmd` laufen lassen — das holt,
prueft und entpackt den Inhalt der Standardkarte `WiesbadenCity_Alkis16`. Ohne diesen
Schritt startet die Karte leer. Details:
[docs/reference/stadtinhalt-holen.md](docs/reference/stadtinhalt-holen.md).

## Herkunft & Aufteilung

Dieses Projekt wurde ursprünglich in Claude Code unter
`C:\Users\ssonn\aivideo\WiesbadenReal` begonnen und wird dort **weiterhin von
Claude fortgesetzt** (Limit-Resets: 16.08.).

Dieser Ordner ist der **eigenständige, abgesicherte Freebuff-Pfad** desselben
Projekts: eine isolierte Arbeitskopie mit eigenem Git-Verlauf, damit beide
Pfade unabhängig voneinander weiterarbeiten können.

- Ursprung (Claude): `C:\Users\ssonn\aivideo\WiesbadenReal`
- Freebuff-Pfad (hier): `/d/freebuff_city_wi/WiesbadenReal`

## Stand der Implementierung

**Phase 1 – GIS-Pipeline** (Spezifikation Abschnitt 15):

| Modul | Status |
|---|---|
| `GeoCoordinateConverter` (WGS84 ↔ UE) | ✅ fertig |
| `OSMTypes` / `OSMDataParser` (OSM XML/JSON/Overpass) | ✅ fertig |
| `PolygonUtils` (2D-Geometrie, Triangulierung) | ✅ fertig |
| `RoadTypeLibrary` / `RoadNetworkTypes` | ✅ fertig |
| `RoadNetworkGenerator` (Straßen, Kreuzungen, Spuren) | ✅ fertig |
| `BuildingGenerator` (Gebäude, Dächer, Multipolygone) | ✅ fertig |
| `HeightmapImporter` (DEM-Import, ASCII-Grid + SRTM .hgt) | ✅ fertig |
| `TerrainGenerator` (Landscape-Heightmaps + Einebnung) | ✅ fertig |
| `WiesbadenWorldBuilder` (Editor-Workflow, orchestriert alles) | ✅ fertig |
| `WiesbadenTrafficSignCatalog` (amtl. Verkehrszeichen + OSM-Parser) | ✅ fertig |
| `WiesbadenRoadMarkings` (RMS/StVO-Maße für Markierung & Ausstattung) | ✅ fertig |
| `RoadFurnitureGenerator` (Pass: Schilder, Leitpfosten, Haltlinien) | ✅ fertig |

**Phase 1 (GIS-Pipeline) ist damit vollständig und über einen Editor-Actor bedienbar.**

**Noch offen:** Phasen 6–12 (Pedestrians, Player, Wanted-Level, UI, Audio,
Optimierung) sowie die DEM-Verdrahtung im Editor; die sichtbare Darstellung
der Verkehrs-Simulation (Fahrzeug-Meshes auf den Sim-Positionen) ist ein
Folgeschritt.

**Umgesetzt ueber Phase 1 hinaus:** Laufzeit-Core (GameInstance/GameMode/
WorldSubsystem + World Partition), fliegbarer Helikopter, generische
Fahrzeug-Kamera, Rotor-Physik, Bodenfahrzeug, Wetter-System + Niagara-FX,
Regionen + regionen-abhaengige Assets sowie die datenreine Verkehrs-KI
(siehe unten).

### Nachtrag 19.08.2026 — Gelaende-Korrektur und Spielerfahrzeug

| Modul | Status |
|---|---|
| `HeightmapImporter::ParseSrtmTileOrigin` (Georeferenz aus Dateiname) | ✅ korrigiert |
| Terrain-Crop auf die OSM-Ausdehnung (Aufloesung 27,6 m → 7,8 m/Quad) | ✅ wirksam |
| `AWiesbadenWorldBuilder::BuildLandscapeImportMaps` (Landscape-Import) | ✅ korrigiert |
| `WiesbadenCarLightsComponent` (Fahr-/Brems-/Rueckfahrlicht, Blinker) | ✅ fertig |
| `WiesbadenEngineAudio` + `WiesbadenCarAudioComponent` (Motorklang) | ✅ fertig |
| `WiesbadenCarSpawn` (Startadresse → naechste befahrbare Spur) | ✅ fertig |
| VW Kaefer 1969 als Platzhalter (Spieler + Verkehr) | ✅ eingebunden |

**Gelaende:** Die SRTM-Kachel wurde zuvor 1 Grad zu weit suedlich verortet — die
Stadt stand auf fremdem Relief. Nach der Korrektur liegt Platter Strasse 144 bei
230 m ueber NN, was der tatsaechlichen Hanglage am Taunus entspricht. Der
Stadt-Build liefert seither 121.721 Strassensegmente und 20.213 Kreuzungen
(zuvor 108.073 / 14.164).

**Spielerfahrzeug:** Start an Platter Strasse 144, auf die naechstgelegene
befahrbare Spur gesetzt (32 m Luftlinie) und in Fahrtrichtung ausgerichtet.
Bedienung: `W`/`A`/`S`/`D`, `Leertaste` Handbremse, `R` Rueckwaertsgang,
`L` Fahrlicht (Aus → Stand → Abblend → Fern), `Q`/`E` Blinker, `H` Warnblinker.

**Einschraenkung:** Die Raeder des Kaefer-Platzhalters drehen und lenken nicht.
Das Quellmodell ist nach Materialien gruppiert, nicht nach Bauteilen; es gibt
keine Rad-Objekte. Die Rad-Komponenten in `AWiesbadenCar` bleiben erhalten und
werden von `UpdateWheels()` weiter gefuehrt — ein separates Radmesh muss nur
zugewiesen werden.

## Verzeichnisstruktur

```
WiesbadenReal/
  WiesbadenReal.uproject
  Source/
    WiesbadenReal.Target.cs
    WiesbadenRealEditor.Target.cs
    WiesbadenReal/
      WiesbadenReal.Build.cs
      GIS/                 # Phasen 1–3
```

## Editor-Workflow (AWiesbadenWorldBuilder)

1. In der UE-Level-View einen **WiesbadenWorldBuilder** platzieren.
2. Im Details-Panel `OsmFilePath` (`.osm`/`.xml`/`.json`) und optional
   `DemFilePath` (`.asc`/`.hgt`) setzen, Materialien zuweisen.
3. Button **„Build City“** klicken — die Pipeline läuft einmal durch:
   OSM parsen → Georeferenzierung → DEM importieren → Straßen → Gebäude →
   Landscape (+ Einebnung unter Straßen/Gebäuden).
4. Ergebnis als Procedural-Meshes (Straßen, Gebäude) und einer echten
   `ALandscape` im Viewport (alternativ Vorschau-Mesh via
   `bCreateLandscapeActor = false`); Reports (Zähler, Dauer) unter
   „GIS|Ergebnis“. „Clear Generated Geometry“ entfernt alles inkl. Landscape.

Hinweis: Die Generierung läuft asynchron auf einem Worker-Thread; im Editor
zeigt ein Fortschrittsdialog (mit Abbrechen) den Stand, der Editor bleibt
bedienbar. Nur das finale Erzeugen der Meshes/Landscape passiert im
Game-Thread.

## Laufzeit-Core (GameInstance / GameMode / WorldSubsystem)

Das Core-Modul laedt die Stadt zur Laufzeit und streamt sie per World Partition:

| Datei | Rolle |
|---|---|
| `Core/WiesbadenCityData.h` | Session-weite Stadt-Daten (OSM, DEM, Netz, Meshes, Reports) |
| `Core/WiesbadenGameInstance` | Konfiguration (Config) + asynchroner Laufzeit-Build der Pipeline |
| `Core/WiesbadenGameMode` | Spielfluss: Init anstossen, Status an Blueprints (OnCityStatus) |
| `World/WiesbadenCitySubsystem` | Orchestrierung je Welt: Daten holen, Stadt spawnen, Streaming-Zustand |
| `World/WiesbadenCityActor` | Procedural-Mesh-Host der Stadt (Strassen, Gebaeude, Terrain) |
| `World/WiesbadenStreamingSource` | World-Partition-Streaming-Quelle, folgt dem Player-Pawn |

**Ablauf (OnWorldBeginPlay):** 1) World Partition pruefen → Streaming-Quelle
registrieren. 2) Daten aus dem GameInstance holen bzw. Laufzeit-Build
anstoessen (`bGenerateAtRuntime`). 3) Stadt-Actor spawnen, Meshes anwenden.

**Zwei Betriebsmodi:**
- **Laufzeit-Build** (`bGenerateAtRuntime = true`, Standard): Beim Spielstart
  laeuft die komplette GIS-Pipeline auf einem Worker-Thread; erst das Spawnen
  der Meshes passiert im Game-Thread. Ideal fuer PIE/Entwicklung.
- **Produktion** (`bGenerateAtRuntime = false`): Die Stadt liegt als gebackene
  World-Partition-Map im Level; World Partition streamt die Zellen automatisch
  um die Streaming-Quelle (Player). Es wird nichts generiert.

**Setup (einmalig):** In den Project Settings unter *Maps & Modes* den
`WiesbadenGameInstance` als Game Instance Class und den `WiesbadenGameMode`
als Default GameMode setzen. Alle Parameter (OSM/DEM-Pfade, Materialien als
Soft-Refs, `StreamingRadiusMeters`) stehen in den Project Settings bzw.
`DefaultGame.ini` unter `[/Script/WiesbadenReal.WiesbadenGameInstance]`.

**Ehrliche Hinweise:** Laufzeit erzeugte Geometrie landet im persistenten Teil
einer partitionierten Welt (nicht zellenweise gestreamt) - das Zellen-Streaming
wirkt auf die gebackenen Inhalte. Eine echte `ALandscape` laesst sich nur im
Editor erzeugen (Import-API ist editor-only); im Packaged Game zeigt der
Terrain-Kanal das Vorschau-Mesh. Streaming-Zustand und Stadt-Status sind ueber
`IsCityStreamingComplete()` / `GetCityStatus()` und die BP-Ereignisse
erreichbar.

## Fliegbarer Helikopter (Fahrzeug-Prototyp)

`Vehicles/WiesbadenHelicopter` ist ein fliegbarer `APawn` mit Platzhalter-
Geometrie (Engine-Basis-Shapes) und adressiert die drei gemeldeten Punkte:

| Punkt | Umsetzung |
|---|---|
| Kamera dreht nicht mit | Kamera-Boom ist Kind der Heli-Wurzel und erbt Pitch/Yaw/Roll (`USpringArmComponent`) |
| Cockpit-Ansicht | Zweite `UCameraComponent` an einem Cockpit-Socket; Umschaltung per Taste |
| Rotoren zu weit hinten | Hauptrotor auf dem Mast ueber dem Schwerpunkt, Heckrotor am Ende des Heckauslegers |
| Kampfheli (Ka-52-Stil) | `bCoaxialRotors` aktiv: gegenlaeufiger Doppelrotor (zweites Platzhalter-Blatt-Mesh rotiert entgegengesetzt), flinkere Physik (hoehere Momenten-Autoritaet, weniger Daempfung, vmax 85 m/s) |

**Steuerung (zero-config, Tasten werden gepollt):**
W/S = Pitch, A/D = Roll, Q/E = Yaw, Space/Shift = steigen, Ctrl = sinken,
G = Triebwerk an/aus (Autorotation testbar). Die **Kamera ist fest im
Follow-Modus direkt hinter der Flugmaschine** (`bLockFollowMode` an der
Fahrzeug-Kamera) - kein Umschalten auf Orbit/Cockpit, keine Orbit-Schwenkung.

**Hinweise:** Die Rotor-Physik kommt aus `FWiesbadenRotorPhysics` (Governor,
Lift, Collective, Zyklik, Heckrotor, Autorotation); der Heli integriert nur
Kraft/Drehmoment. Bodenkontakt ueber einen Down-Raycast. Zum Fliegen den Pawn
in ein Level setzen - `AutoPossessPlayer = Player0` uebernimmt ihn beim Start
automatisch. Alle Parameter liegen editierbar in den Kategorien
`Wiesbaden|Heli|...` bzw. `Wiesbaden|Heli|Rotor`.

## Generische Fahrzeug-Kamera

`Vehicles/WiesbadenVehicleCameraComponent` ist eine wiederverwendbare Kamera-
Komponente fuer beliebige Fahrzeuge/Pawns. Sie erzeugt ihr Rig selbst in
BeginPlay (SpringArm + Third-Person-Kamera + Cockpit-Socket/-Kamera) und bietet
Follow, Orbit (Pfeiltasten) und Cockpit mit Tastenumschaltung (`ToggleKey`,
Default `C`). Der Helikopter nutzt sie, statt eigene Kamera-Logik zu duplizieren.

Einsatz: Komponente an einen Pawn haengen, `CockpitOffset`/Armlaenge/etc. in der
Kategorie `Wiesbaden|Kamera` anpassen - fertig. Voraussetzung: der Owner sollte
ein `APawn` sein, der von einem `APlayerController` besessen wird.

## Bodenfahrzeug (PKW-Prototyp)

`Vehicles/WiesbadenCar` ist ein fahrbarer `APawn` mit Platzhalter-Geometrie
(Engine-Basis-Shapes) und denselben Konventionen wie der Helikopter
(zero-config-Input-Polling, `AutoPossessPlayer = Player0`, generische
Fahrzeug-Kamera mit C-Umschaltung).

**Steuerung:** W/S = Gas/Bremse, A/D = Lenken, Space = Handbremse,
R = Rueckwaertsgang, C = Kamera (Follow -> Orbit -> Cockpit).

Die **Physik** kommt aus dem reinen, deterministischen Modul
`Vehicles/WiesbadenVehiclePhysics` (`FWiesbadenVehiclePhysics`, analog zur
Rotor-Physik und damit in Automation-Tests pruefbar):

- **Motor** – Drehmomentkurve ueber der Drehzahl (T_max aus der Leistung,
  Abfall zum Leerlauf und zur Drehzahlgrenze).
- **Automatik-Getriebe** – Gangwechsel ueber Drehzahlschwellen, Drehzahl aus
  Radumfang und Uebersetzung; Rueckwaertsgang nur im Stand.
- **Laengsdynamik** – Antriebskraft (begrenzt durch das Traktionslimit
  mu*Gewicht) minus Rollreibung, Luftwiderstand und Bremse.
- **Querdynamik** – kinematisches Bicycle-Modell (yaw = v/L * tan(delta)),
  begrenzt durch das Seitenkraftlimit (Untersteuern bei hohem Tempo).

Der Pawn integriert nur Geschwindigkeit -> Position und Gierrate ->
Ausrichtung, haelt den Bodenkontakt per weichem Down-Raycast und animiert
Raeder (Drehung + Vorderrad-Lenkeinschlag). Alle Parameter (Leistung,
Gangverhaeltnisse, Reibwerte, Radstand) liegen editierbar unter
`Wiesbaden|Fahrzeug|...`.

## Rotor-Physik

Das reine, deterministische Drehfluegler-Modell: Governor, Lift, Zyklik, Heckrotor, Koaxial-Rotoren, Blattspitzenverlust und Autorotation.

> Ausgelagert nach [`docs/explanation/rotor-physik.md`](docs/explanation/rotor-physik.md) (Diataxis-Explanation).

## Flugsound (Helikopter)

Prozeduraler Helikopter-Klang aus dem Flugzustand (kein Asset noetig) plus optionale Asset-Wiedergabe.

> Ausgelagert nach [`docs/explanation/flugsound.md`](docs/explanation/flugsound.md) (Diataxis-Explanation).

## Verkehrszeichen & Strassenausstattung

Die GIS-gestuetzten Inhalts-Systeme: Beschilderung, Strassenklassen, Fassaden, Markierung/Ausstattung, City-Prompt, Wetter, Verkehrs-KI und Stadt-Regionen.

> Ausgelagert nach [`docs/explanation/weltinhalte.md`](docs/explanation/weltinhalte.md) (Diataxis-Explanation).

## Automation-Tests

Unter `Source/WiesbadenReal/Tests/` liegen editorfaehige Automation-Tests fuer
die datenverarbeitenden Kernmodule sowie das Rotor-Physik-Modul (ohne geladenes
Level ausfuehrbar):

| Datei | Abgedeckt |
|---|---|
| `GeoCoordinateConverterTest.cpp` | ECEF/ESU-Roundtrip, Achsenkonvention, Metrik |
| `OSMDataParserTest.cpp` | XML/JSON-Parsing, Fehlerfaelle, Tag-Interpretation |
| `HeightmapImporterTest.cpp` | Raster/Bilinear, ASCII-Grid, SRTM-Rohdaten, Sampler |
| `GeneratorTest.cpp` | Straßen/Kreuzungen, Gebaeude, Terrain-Tile, Heightmap, Fassaden-/Landmarken-Overrides (`Buildings.PromptOverrides`) |
| `RotorPhysicsTest.cpp` | Rotor: Schwebeflug (Lift ~ Gewicht), Autorotation, Zyklik-/Heckrotor-Vorzeichen |
| `VehiclePhysicsTest.cpp` | Fahrzeug: Stand/Gang, Beschleunigung+vmax, Bremsen, Lenkung/Seitenkraftlimit, Rueckwaerts, Traktionslimit |
| `HelicopterAudioTest.cpp` | Flugsound: Blattpass-/Filterfrequenzen, Determinismus (Seed), int16-Bereich, Lautstaerke (Blattlast, Triebwerk an/aus) |
| `TrafficSignCatalogTest.cpp` | Verkehrszeichen: OSM-Parsing, Tempolimits, Katalog-Lookup |
| `CityPromptTest.cpp` | City-Prompt: Dichte/Stile/Landmarken/Wetter (deutsch+englisch), Umlaute, Defaults, Determinsmus, Tageszeit/Verkehr/Hoehen (`ExtendedRules`) |
| `RegionGeneratorTest.cpp` | Regionen: Tag-Klassifikation, Erzeugung (Bounds/Flaeche/Fleckenfilter), Punkt-Klassifikation, regionale Hoehen-/Fassaden-Regeln + Generator-Integration (kein Haus im See im Mesh) |
| `WeatherSystemTest.cpp` | Wetter: sanfter Uebergang (Blend/Intensitaeten), Tageszeit (1 Realstunde = 24 Ingame-Stunden), Sonnenstand/Nacht/Licht, CityPrompt-Integration |
| `WeatherFXTest.cpp` | Wetter-FX: Param-Ableitung (Wetterlage -> Effekte), Uebergangs-Blend, Tageslicht-Farbtemperatur, Sonnen-Intensitaet (Tag/Nacht, Bewoelkung), Determinsmus + Vertrags-Validierung (FindMissingParameters) + Asset-Pfade (kanonische `path`-Felder) + Editor: Katalog-Modul-Referenzen gegen UE-5.8-Engine-Content |
| `RegionAssetTest.cpp` | Regionen-Assets: Kategorie-Zuordnung (Gruen->Baeume, Wasser->Ufer, Industrie/Gewerbe->Industrie), Platzierung im Polygon, Determinsmus, deaktivierte Kategorien |
| `RoadFurnitureGeneratorTest.cpp` | Ausstattungs-Pass: Schilder, Leitpfosten, Haltlinien |
| `TrafficSimulationTest.cpp` | Verkehrs-KI: Dichte->Spawn-Rate (0/2/7/10, Monotonie), Graphfolge (Spur->Verbindung->Folgespur), Sackgassen-Entfernung, Kopf-zu-Schwanz (MinGap, kein Ueberholen), Determinsmus, Report |

Ausfuehrung im Editor (Session Frontend → Automation) oder per Commandlet:

```
UnrealEditor-Cmd.exe WiesbadenReal.uproject -ExecCmds="Automation RunTests WiesbadenReal; Quit" -unattended -nop4 -nullrhi
```

Die Tests sind `Shipping`-Builds nicht beigemischt (Build.cs); alle
Generierungs-Aufrufe laufen mit `nullptr`-Hoehenmodell auf Z=0 bzw. mit
synthetischen Mini-DEMs, das Rotor-Physik-Modul ist rein mathematisch -
alles deterministisch und ohne externe Datensaetze.

Die vollständige Spezifikation liegt in [`SPEC.md`](SPEC.md).

## Hinweis zu Datenquellen

Für den Import werden echte Daten benötigt (noch nicht eingecheckt):

- OSM-Extrakt: Overpass-API (Query siehe `UOSMDataParser::BuildOverpassQuery`)
- DEM: Copernicus DEM / SRTM, per `gdal_translate -of AAIGrid` in das
  ESRI-ASCII-Grid-Format konvertiert (wird von `UHeightmapImporter` gelesen)
