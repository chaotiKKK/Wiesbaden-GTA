# Wiesbaden Real — Vollständige Spezifikation

> Quelle: ursprünglicher Auftrag an den Generator (Claude Code). Unverändert
> übernommen als verbindliche Referenz für beide Entwicklungspfade.

## 1. Projekt-Übersicht & Zielsetzung

Open-World-Action-Spiel im Stil von GTA V, das die Landeshauptstadt Wiesbaden
(Hessen) in photorealistischer Qualität als Spielwelt abbildet. Multi-File,
modulare Architektur. Jede Funktion, jedes System und jedes Subsystem ist
vollständig, fehlerfrei und produktionsreif zu implementieren.

**Kernanforderung:** Der Spieler muss visuell und haptisch erkennen, dass er
durch echte Wiesbadener Straßen fährt — inklusive korrekter Topographie,
Straßenbreiten, Spuranzahl, Bürgersteiggeometrie, Kreuzungslogik und Bebauung.

## 2. Technologie-Stack

- **Engine:** Unreal Engine 5.8.1+ (Nanite, Lumen, Chaos Physics)
- **Sprache:** C++ (Core) + Blueprints (Gameplay)
- **GIS-Datenquellen:** OSM (Straßen, Gebäude, POIs), NASA SRTM / ASTER GDEM
  (Topographie), Hessen-Atlas (Grenzen), Overpass API
- **Plugins:** Cesium for Unreal (optional), OSM2World/Blender-OSM, Houdini
  Engine (optional), SpeedTree, Quixel Megascans
- **DB:** PostgreSQL + PostGIS (optional, Multiplayer)
- **Version Control:** Git mit LFS

## 3. Architektur & Projektstruktur

```
/Source
  /Core     WiesbadenGame, GameMode, PlayerController, GameState
  /GIS      OSMDataParser, HeightmapImporter, GeoCoordinateConverter,
            RoadNetworkGenerator, BuildingGenerator, TerrainGenerator
  /World    WorldSubsystem, TrafficSystem, PedestrianSystem, WeatherSystem,
            TimeOfDayManager
  /Vehicles VehicleBase, PhysicsComponent, InputComponent, TrafficVehicle,
            DamageSystem, Inventory
  /Player   PlayerCharacter, Inventory, WantedLevelSystem, PlayerStats
  /AI       TrafficAIController, PedestrianAIController, PoliceAIController,
            AINavigationSystem, AIPerceptionSystem
  /UI       HUDWidget, MinimapWidget, GPSNavigationWidget, PhoneWidget,
            PauseMenuWidget
  /Audio    AmbientSoundManager, VehicleAudioComponent, MusicManager,
            SpatialAudioSystem
  /Network  MultiplayerSession, ReplicationSystem
  /Data     /Config (RoadTypes, BuildingPresets, VehiclePresets)
            /Raw (/OSM, /DEM, /Satellite)
/Content
  /Maps     Wiesbaden_Main.umap + SubLevels (CityCenter, Biebrich, Nordost,
            Suedost, Rheingauviertel)
  /Blueprints /Materials /Meshes /Textures /Sounds
```

## 4. GIS-Daten-Integration (kritisch)

### 4.1 OSM-Datenpipeline
- Bounding Box: 50.0°N–50.1°N, 8.1°E–8.4°E (erweitert: 8.08–8.42, 49.995–50.16)
- Abfrage: highway, building, landuse, natural, amenity, barrier,
  traffic_signals, crossing, sidewalk, lanes, maxspeed, oneway, surface
- Eigener XML-Parser; WGS84 → ECEF → UE (Maßstab 1:1, 1 uu = 1 cm)

### 4.2 Höhendaten (DEM)
- SRTM 1-Arc-Second (~30 m) oder ASTER GDEM v3
- Konvertierung zu UE5 Landscape Heightmap (8129×8129 oder 4033×4033 Tiles)
- Topographie: Taunus-Ausläufer Nord, Rheinebene Süd, Gefälle Richtung
  Biebrich — Höhendifferenzen müssen spürbar sein.

### 4.3 Straßennetz-Generierung (präzise)
Jede Straße mit: `lanes`, `lanes:forward/backward`, `width`, `sidewalk`,
`cycleway`, `surface`, `oneway`, `maxspeed`, `highway`.

Kreuzungslogik: `highway=crossing` / `traffic_signals`, Ampel-Phasen,
`turn:lanes`, Zebrastreifen, Kreisverkehre.

Referenzstraßen: Rheinstraße (B263), Mainzer Straße (3+2), Wilhelmstraße,
Taunusstraße, Dotzheimer Straße, Biebricher Allee; alle Einbahnstraßen.

## 5. Gebäude-Generierung

Gebäudetypen aus `building=*` (residential, apartments, office, commercial,
industrial, church, civic, train_station, university, hospital).
Prozedurale Fassaden (Fenster, Balkone, Dachformen, Materialien), LOD0–LOD3.

**Landmarken (manuell):** Kurhaus, Hessischer Landtag, Rathaus, Marktkirche,
Neroberg + Nerobergbahn, Schloss Biebrich, BRITA-Arena, Hauptbahnhof,
Mauritius-Therme, Lilien-Carré, Wilhelmstraße komplett.

## 6. Fahrzeug-System

Chaos-Vehicle-Physik, Fahrzeugklassen (PKW, SUV, Motorrad, LKW, Bus),
Schadensmodell, Tank-System, Interieur mit Instrumenten.
Verkehrs-KI: Spurhaltung, Tempolimit, Spurwechsel, Vorfahrt, Rush-Hour-Dichte,
deutsche Marken, ESWE-Buslinien (RMV-GTFS).

## 7. Fußgänger-System

Spawn-Logik nach Tageszeit/Ortstyp; Gehweg-Nutzung, Ampel-/Zebrastreifen-
Beachtung; Innenstadt > 500 gleichzeitig, Vororte < 100; Reaktionen.

## 8. Spieler-System

Third/First-Person, Cover, Klettern, Schwimmen.
Wanted-Level 1–6 (Polizei → SEK → Heli → BFE+ → GSG9/Militär Erbenheim),
reale Polizei-Standorte.
Waffen: HK MP5/G36/USP, Walther P99, Ballistik, Lizenz-Mechanik.

## 9. UI/UX

Minimap (echte Karte, GPS-Route, POIs), GPS-Navigation (A* auf OSM-Graph,
Sprachansagen), Smartphone (Karte, Kontakte, Internet, Kamera, Radio).

## 10. Audio

Umgebungsaudio (Verkehr, Fußgänger, Parks, Rhein), Fahrzeug-Audio (Motor,
Reifen, Crash, Doppler), Radiosender (HR1/HR3/YOU FM-Varianten, Klassik,
Elektro, Talkradio).

## 11. Wetter & Tageszeit

Sky Atmosphere + Volumetric Clouds + Niagara; 1 Real-Stunde = 24 Ingame-Stunden;
Klar/Bewölkt/Nebel (Rhein-Nebel!)/Regen/Gewitter/Schnee; Jahreszeiten.

## 12. Performance & Optimierung

World Partition, Nanite, Lumen, Virtual Shadow Maps; Traffic-Culling (1 km),
Pedestrian-Culling (500 m); Budget < 4 GB VRAM / < 16 GB RAM;
60 FPS @ 1440p auf RTX 4070.

## 13. Qualitätsanforderungen

Keine Platzhalter/TODOs; vollständige Fehlerbehandlung (Null-Pointer, leere
Daten, Netzwerk-Ausfall); strukturiertes Logging pro Subsystem (UE_LOG);
Unit-Tests für Parser/Konverter/Mathematik; UE Coding Standard.

## 14. Datenquellen-Referenz

| Daten | Quelle |
|---|---|
| Straßennetz | OpenStreetMap (overpass-api.de) |
| Höhendaten | NASA SRTM (earthexplorer.usgs.gov), Copernicus DEM |
| Satellitenbilder | Sentinel-2 (scihub.copernicus.eu) |
| 3D-Gebäude | LoD2 Hessen (geoportal.hessen.de) |
| Verkehrsdaten | Hessen Mobil (vmz.hessen.de) |
| ÖPNV | RMV GTFS (rmv.de) |
| Fotoreferenz | Street View / Mapillary |

## 15. Implementierungs-Reihenfolge

1. GIS-Pipeline (Parser, Konverter, Terrain)
2. Straßennetz-Generator
3. Gebäude-Generator
4. Fahrzeug-Physik & Basisfahrmechanik
5. Verkehrs-KI & Fußgänger-KI
6. Spieler-Charakter & Kamera
7. Wanted-Level & Polizei-KI
8. UI-System
9. Audio-System
10. Wetter, Tageszeit, Optimierung
11. Landmarken-Modellierung
12. Polishing, Bugfixing, Performance-Tuning

## 16. Finale Anweisung

Vollständigen, ungekürzten, produktionsreifen Code erzeugen. Jede Datei
kompilierfähig, keine Platzhalter. Sofort in UE 5.8.1+ einsetzbar. Externe
Datenquellen korrekt angebunden und verarbeitet.
