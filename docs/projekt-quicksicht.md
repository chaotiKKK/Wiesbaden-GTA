# Projekt-Quicksicht

**Worum es geht** (SPEC.md): Open-World-Actionspiel im GTA-V-Stil, das Wiesbaden 1:1 aus echten
Geodaten nachbaut - OSM-Strassen mit Spuren/Gehwegen/Ampeln, ALKIS-Grundrisse mit amtlichen
LoD2-Hoehen, ein Hoehenmodell vom Taunus bis zur Rheinebene (`Data/Raw/DEM`), prozedurale Gebaeude
plus Landmarken (Marktkirche, Nerobergbahn, SebboTower), dazu Verkehrs- und Fussgaenger-KI,
Fahrphysik, Missionen, HUD, Audio und Wetter/Tageszeit. Unreal 5.8 (Nanite, Lumen, World Partition).
SPEC.md sieht Gameplay in Blueprints vor - tatsaechlich ist praktisch alles C++.

**Code-Organisation:** `Source/WiesbadenReal/` nach Domaene (Core, GIS, World, Vehicles, NPC,
Missions, Weapons, Store, UI, Audio, Tests), `Content/` Maps + Assets (Kataloge als JSON in
`Content/Config/`), `Data/Raw/` die gitignorierten OSM-/ALKIS-/DEM-Rohdaten (nur `Data/Raw/Bus/*.json`
ist versioniert), `Tools/` die Python-/Node-Werkzeuge, `docs/` die Doku nach Diataxis. Kern ist die
datenreine `WiesbadenCityPipeline::BuildCityData()`: Strassen, Gebaeude, Terrain und Moebel identisch
fuer Editor (`AWiesbadenWorldBuilder`) und Laufzeit. Die Stadt wird als World-Partition-Karte
gebacken - eine gebackene Karte schaltet den Laufzeit-Build ab. Welche Karte gerade gilt, steht in
`Config/DefaultEngine.ini` (`GameDefaultMap`), nicht hier.

**Regeln fuer Agenten** (AGENTS.md): Code strikt ASCII, Antworten und Kommentare auf Deutsch; nur EINE
Engine (Launcher-5.8.2, Baeume nie mischen), Zeilenenden LF per `.gitattributes`. Keine CI - die
Release-Gates laufen als Git-Hook (`Tools/git-hooks/`, je Klon per `python Tools/hooks_einrichten.py`):
pre-commit Engine-Pfade (Gate 0) und das Kompilat (Gate 1, nur wenn C++ vorgemerkt ist); pre-push
zusaetzlich Python-Suiten, Unit-Tests und Rauchtest (Gate 2+3). Die Zuordnung prueft
`Tools/test_projekt_quicksicht.py` gegen `Tools/vor_dem_commit.py`.
Belegen statt behaupten - "implementiert" ist nicht "verbunden", "gebacken" nicht "live"; Funde nach
AGENTS.md.
