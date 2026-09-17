# Echte Fahrspuren & Beschriftungen (OSM) - Design

- Datum: 2026-09-17
- Status: Entwurf (im Brainstorming freigegeben; wartet auf Spec-Review)
- Testkarte / Bake-Ziel: Alkis4 als Quelle -> Scratch-Map via `WB_TARGET_MAP`
- Teil von: Strassen-/Stadt-System (T2 Fahrbahn/Markierungen; ergaenzt T1 Ampeln, T3 OEPNV-Bus)
- Quelle: OSM (einzige Quelle fuer Spur-Paint). Amtliche Centerlines = separates Zukunftsprojekt (s. Abschnitt 12)

## 1. Problem & Kontext

Die Strassen sind heute eine **einzige flache Fahrbahn-Ribbon** je Segment
(`URoadNetworkGenerator::BuildSegmentMesh`, RoadNetworkGenerator.cpp:2190 ->
`FPolygonUtils::BuildRibbonMesh` mit `CarriagewayWidthCm`). Ein **vollstaendiger
Lane-Graph existiert bereits in DATEN**, wird aber nur von der Verkehrs-KI
genutzt, nicht gerendert: `BuildLanes` (RoadNetworkGenerator.cpp:1696) erzeugt
`FRoadLane` (RoadNetworkTypes.h:75-145) mit `Direction`, `WidthCm`,
`LaneIndexFromLeft`, `TurnFlags`, `bIsBusLane`.

Was heute schon gerendert wird:
- Laengslinien (`BuildLaneMarkings`, :2638): nur INNERE Spurgrenzen, 12 cm,
  durchgezogen an der Richtungstrennung (Vertex-Farbe R=255) vs. gestrichelt
  (R=0, Strich per Shader). Nur wenn `bHasCenterLineMarking` und `TotalLanes>=2`.
  KEINE Randlinien, KEINE Pfeile, KEIN Text.
- Quermarkierungen als Furniture-Instanzen (`URoadFurnitureGenerator::PlaceMarkings`,
  RoadFurnitureGenerator.cpp:590; `SpawnMarkings` -> HISM `/Engine/BasicShapes/Plane`,
  RoadFurnitureSpawnerComponent.cpp:99): Haltlinie, Wartelinie, "30"-Zonensymbol.

Was in OSM erkannt, aber NICHT gebaut wird:
- **Busspuren**: `FRoadLane::bIsBusLane` wird NIRGENDS gesetzt; `lanes:psv`/`busway`
  werden nicht gelesen.
- **Abbiegepfeile**: `ParseTurnIndication` (:180) + `FOSMTagParser::SplitLaneValues`
  (OSMTypes.cpp:430) existieren, werden aber nur aus Tests aufgerufen; `BuildLanes`
  setzt `TurnFlags = Through` hart (:1763).
- **Zebrastreifen**: Kreuzungen tragen `bHasZebraCrossing` (gesetzt :1453-1456), der
  `ERoadMeshChannel::Crossing`-Kanal + `M_WbLaneMarking`-Mapping sind verdrahtet
  (WiesbadenWorldBuilder.cpp:1778-1782), aber `BuildIntersectionMesh` (:2753) emittiert
  keine Streifen. Greenfield.
- **Randlinien** (Fahrbahnbegrenzung) fehlen ganz.
- **On-Street-Radstreifen** (`cycleway=lane/track` am Weg) werden ignoriert; nur
  eigenstaendige `highway=cycleway` werden modelliert.

Alles ist **gebacken** (ProcMesh je Chunk -> StaticMesh-.uasset via
`WiesbadenChunkStaticMeshBaker`, World-Partition-Map). Aenderungen brauchen einen
vollstaendigen Re-Bake (`Tools/rebuild_city.py`, ~2 h).

## 2. Ziele / Nicht-Ziele

**Ziele** - sechs isolierte Module, gespeist aus dem vorhandenen Lane-Graph,
emittiert in die gebackenen Kanaele/Instanzen:
1. **Rand- & StVO-Linien** - fehlende Randlinien (Fahrbahnbegrenzung) + je Klasse
   korrekte durchgezogen/gestrichelt-Wahl.
2. **Busspuren** - aus OSM, `bIsBusLane` setzen, breite durchgezogene Grenzlinie +
   "BUS"-Piktogramm (KEINE farbige Flaeche - deutsche Praxis).
3. **Zebrastreifen** - `Crossing`-Kanal aus OSM-`crossing`-Knoten fuellen.
4. **Abbiegepfeile** - `turn:lanes` -> `TurnFlags` -> Pfeile vor der Kreuzung.
5. **Wartelinien** - Haifischzaehne (Z.342) an Yield-Zufahrten (Haltlinie bleibt).
6. **On-Street-Radstreifen** - `cycleway=lane/track` -> rote Flaeche + Randlinie +
   Fahrrad-Piktogramm.

**Echtheits-Strategie:** Fakten (Busspur, Abbiege-, Radspuren, Zebra) NUR aus OSM -
nie erfinden. Reine Malkonvention (Mittel-/Randlinien je Strassenklasse nach StVO)
als Default, wo Tags fehlen.

**Nicht-Ziele (bewusst spaeter):** exotische Symbole (Sperrflaechen/Schraffuren,
Kastenmarkierung "Keep clear", Parkbuchten, TAXI-Text); Umstieg der
Strassen-GEOMETRIE auf amtliche Daten (Abschnitt 12); Tram-Spuren.

## 3. Ansatz A - vorhandene Bake-Mechanik erweitern

Kein neues Render-System. Zwei bereits bewaehrte Wege:
- **Linear** (Rand-/Spur-/Grenzlinien, Zebra) -> duenne Ribbon-Meshes via
  `FPolygonUtils::BuildRibbonMesh` in die `FRoadMeshData`-Kanaele. Die rote
  Radstreifen-Flaeche braucht einen NEUEN Kanal `ERoadMeshChannel::BikeLaneSurface`
  (Material `M_WbBikeLaneSurface`), da der bestehende `Cycleway`-Kanal auf
  `M_WbCycleway` (eigenstaendige Radwege) zeigt; Kanal->Material-Mapping in
  `WiesbadenWorldBuilder.cpp` + `WiesbadenCityActor.cpp` ergaenzen. Linien/Zebra
  laufen in `LaneMarking`/`Crossing` (`M_WbLaneMarking`). Alles `MarkingOffsetCm`
  (1,5 cm) ueber der Fahrbahn; durchgezogen/gestrichelt weiter ueber Vertex-Farbe
  R + Shader.
- **Symbole** (Pfeile, BUS, Rad, Haifischzaehne) -> `FMarkingInstance` ->
  HISM-Plane-Instanzen mit alpha-maskiertem Paint-Material (wie das "30"-Symbol).

Verworfen: **B Deferred Decals** (Laufzeitkosten, Parallelsystem im sonst
gebackenen World), **C Material/RVT-Painting** (zu aufwendig fuer diskrete
Pfeile/Zebra/Text). Details der Abwaegung im Brainstorming.

## 4. Datenmodell: OSM -> Lane-Anreicherung (gemeinsame Grundlage)

Bleibt im reinen Generator/Library-Layer (`URoadTypeLibrary` + `BuildLanes`),
damit unit-testbar (wie GeneratorTest/OSMDataParserTest). OSM-Parser bleibt
unveraendert - er speichert bereits jeden Tag verbatim.

`FRoadLane` erhaelt zusaetzlich:
- `bIsBikeLane` (bool)
- `LeftBoundary` / `RightBoundary` : enum `ELaneBoundaryStyle`
  `{ None, Dashed (Leitlinie), Solid (Fahrstreifenbegrenzung), Edge
  (Fahrbahnbegrenzung), DirSplit (Richtungstrennung) }` - treibt Abschnitt 5
  direkt, statt spaeter neu herzuleiten.

Gelesene Tags (Fakten - nur wenn vorhanden):
- **Bus:** `bus:lanes`/`psv:lanes` (Pro-Spur-Tokens `|`), `lanes:psv[:forward|:backward]`,
  `busway[:left|:right]=lane`, `bus=designated` (dedizierter Weg) -> `bIsBusLane`.
- **Rad:** `cycleway[:left|:right|:both]=lane|track`, `cycleway:lanes` -> `bIsBikeLane`.
- **Abbiegen:** `turn:lanes[:forward|:backward]` -> `SplitLaneValues` ->
  `ParseTurnIndication` -> `TurnFlags` je Spur (heute hart `Through`).

**Pro-Spur-Mapping:** `|`-Tokens sind links->rechts geordnet; bei
Zweirichtungsstrassen ueber beide Richtungen, daher Zuordnung ueber
`LaneIndexFromLeft` nach dem forward/backward-Split (die `:forward`/`:backward`-
Varianten mappen innerhalb ihrer Seite). Dieses Mapping bekommt eigene Tests.

**Grenzstile (Konvention - StVO-Defaults je Klasse/Tempo):**
Richtungstrennung = durchgezogen auf klassifiziert/>=50, gestrichelt auf
Tempo-30/Anlieger; Spurteiler innerhalb einer Richtung = gestrichelt; Aussenrand
= durchgezogene Randlinie auf primary/secondary/tertiary, keine auf residential.
Optional hebt `change:lanes=no` einen gestrichelten Teiler auf durchgezogen.
Neue Defaults in `URoadTypeLibrary::ApplyBuiltInDefaults` / `WiesbadenRoadTypes.json`.

## 5. Linear-Builder (Linien, Zebra, Grenzlinien)

**`BuildLaneMarkings` erweitern:** statt nur innerer Grenzen ueber die
`Left/RightBoundary`-Stile jeder Spur iterieren:
- Randlinie (Edge): durchgezogenes 12-cm-Ribbon an beiden Aussenkanten, auf
  Klassen mit Randlinie.
- Innenteiler: gestrichelt (R=0).
- Richtungstrennung: durchgezogen (R=255).
- Bus-/Rad-Grenze: breite durchgezogene Linie (`WideLineWidthCm` 25, R=255).

**Neuer `BuildCrossingMesh`** (neben `BuildIntersectionMesh`) in den
`Crossing`-Kanal (`M_WbLaneMarking`): parallele Streifen (`ZebraStripeWidthCm` 50 /
`ZebraGapCm` 50) quer zur Fahrtrichtung ueber die Fahrbahnbreite, min. Laenge
`ZebraMinWidthCm` 300. Primaer aus OSM-**crossing-Knoten** am Weg (praezise
Mittblock-Lage), `bHasZebraCrossing` der Kreuzung als Fallback.

**Bus-/Rad-Flaechenbehandlung:**
- Busspur: KEINE Farbflaeche (deutsche Praxis) - nur breite Grenzlinie + "BUS"
  (Abschnitt 6).
- Radstreifen: **rote Flaeche** (eigenes Material `M_WbBikeLaneSurface`) als
  Sub-Ribbon ueber der Spurbreite + durchgezogene Randlinie + Fahrrad-Piktogramm.

## 6. Symbole & Assets

`RoadFurnitureGenerator::PlaceMarkings` + `ERoadMarkingKind` + `SpawnMarkings`
(HISM-Plane) erweitern. Neue Kinds: `TurnArrow`, `BusText`, `BikeSymbol`,
`GiveWayTeeth`.

Platzierung ueber den angereicherten Lane-Graph:
- **Abbiegepfeile** (Z.297): je Zufahrtsspur mit `TurnFlags`, 1-2 Instanzen im
  Anlauf zur Kreuzung, in Spurrichtung; Flag-Kombi waehlt die Atlas-Kachel.
- **BUS-Schriftzug:** periodisch je `bIsBusLane`-Spur - Default alle
  `SymbolRepeatSpacingCm` (~4000 cm = 40 m) sowie einmal direkt nach jeder Kreuzung.
- **Rad-Piktogramm:** periodisch je `bIsBikeLane`-Spur, gleicher
  `SymbolRepeatSpacingCm`-Takt.
- **Haifischzaehne** (Wartelinie Z.342): Plane in Spurbreite mit gekachelter
  Dreieck-Textur an Yield-Zufahrten.

Neue Konstanten in `WiesbadenRoadMarkings.h`: `SymbolRepeatSpacingCm` (4000),
`TurnArrowsPerApproach` (1-2), `BikeLaneSurfaceInsetCm` (Rand der roten Flaeche).

Neue Assets (klein, generiert - Pillow ist im Projekt etabliert, vgl.
`make_nerobergbahn_textures.py`):
- `T_WbArrows` (Atlas: through/left/right/left+through/right+through/u-turn) +
  `M_WbRoadArrow` (unlit, alpha-maskiert weiss).
- `T_WbBusText` ("BUS"), `T_WbBikeSymbol`, `T_WbSharkTeeth` (kachelbar), je auf
  leichtem unlit-maskiertem Material.
- `M_WbBikeLaneSurface` (rot).
- Linien & Zebra nutzen das vorhandene `M_WbLaneMarking`.
- Erzeugt von `Tools/make_road_marking_textures.py` (Pillow) + UE-Import-Skript
  wie `make_dfi_materials.py`.

## 7. Fehler & Randfaelle

- Token-Anzahl != Spuranzahl -> Best-Effort-Ausrichtung, sonst Tag ignorieren +
  EINMAL loggen; nie den Bake abbrechen.
- Unbekannter turn-Token -> `Through`/kein Pfeil (Fakten-Prinzip: lieber nichts als
  falsch).
- `<2` Spuren, degenerierte Spannen (Zebra breiter als Fahrbahn) -> Builder no-op.
- Fehlende Textur/Material -> Symboltyp ueberspringen + einmal loggen.
- Z-Fighting -> bewaehrter `MarkingOffsetCm` 1,5 cm.

## 8. Tests (datenrein, headless in der Automation-Suite)

- Tag->Spur-Parsing: bus/psv/busway -> `bIsBusLane`; cycleway -> `bIsBikeLane`;
  turn:lanes -> `TurnFlags`.
- Pro-Spur-Token-Mapping (links->rechts, forward/backward-Split, oneway,
  Anzahl-Mismatch) - mehrere Faelle.
- Grenzstil-Aufloesung (Klasse/Tempo -> Edge/Solid/Dashed; `change:lanes`-Override).
- Geometrie-Mathe: Zebra-Streifenzahl/-lage je Spanne, Randlinien-Offset =
  Fahrbahn/2, Haifischzaehne ueber Spurbreite, Pfeil-Offset zur Kreuzung.
- Guard-Tests: <2 Spuren, degeneriert, fehlende Tags -> keine Geometrie, kein Crash.

## 9. Umsetzungsreihenfolge (je Phase testbar, je 1 Modul pro Re-Bake)

- **Phase 0 (Risiko-Gate):** No-Code-Re-Bake Alkis4 -> Scratch, bestaetigen dass
  der Bake eine BEFUELLTE Karte liefert (Notizen: fruehere Leer-Bakes + jueng.
  "Bake-Fix"-Commit). Bei Defekt: Bake zuerst reparieren (separates Anliegen).
- **Phase 1:** Datenmodell (Abschnitt 4) + Unit-Tests. Kein Re-Bake noetig (rein).
- **Phase 2:** Rand-/StVO-Linien (Modul 1) -> Re-Bake -> Foto.
- **Phase 3:** Zebrastreifen (Modul 3) -> Re-Bake -> Foto.
- **Phase 4:** Abbiegepfeile (Modul 4) -> Re-Bake -> Foto.
- **Phase 5:** Wartelinien/Haifischzaehne (Modul 5) -> Re-Bake -> Foto.
- **Phase 6:** Busspuren (Modul 2) -> Re-Bake -> Foto.
- **Phase 7:** Radstreifen (Modul 6, rote Flaeche) -> Re-Bake -> Foto.

Jedes Modul haengt an einem eigenen Settings-Bool in `FRoadGenerationSettings` /
`FRoadFurnitureSettings` (`bGenerateEdgeLines`, `bGenerateCrossings`,
`bGenerateTurnArrows`, `bGenerateGiveWayTeeth`, `bGenerateBusLanes`,
`bGenerateBikeLanes`), damit einzeln bakebar. Fertige Module koennen vor einem
Bake gebuendelt werden, um die ~2 h zu amortisieren.

## 10. Re-Bake & Verifikation

- Bake via `Tools/rebuild_city.py` (`WB_SOURCE_MAP=Alkis4`, `WB_TARGET_MAP=Scratch`),
  altes Alkis4 bleibt erhalten; bei Erfolg wird Scratch zur neuen Default-Karte.
- Sicht-Pruefung je Modul: bekannte Kreuzungen / Linie-6-Korridor via
  HighResShot-Posensystem (Recipe: `MSYS_NO_PATHCONV=1`, `-WbShotPoseFile`);
  Abgleich mit echten Wiesbaden-Referenzen.

## 11. Risiken

- **Bake-Gesundheit** -> Phase-0-Gate.
- Dreiecks-/Instanz-Budget stadtweit (duenne Ribbons + instanzierte Symbole sind
  guenstig; beobachten).
- OSM-Tag-Sparsity -> viele Strassen bekommen nur Konventionslinien (by design).
- Pro-Spur-Token-Fehler -> Unit-Tests + Fallback "kein Symbol".

## 12. Offene Punkte / Nahtstellen

- **Amtliche Strassengeometrie (Zukunftsprojekt):** die vorhandene
  `Data/Raw/ATKIS/wiesbaden_roads.geojson` ist als Unterbau UNBRAUCHBAR (765
  attributlose RoadArea-Polygone, nur `name`(null)+`id`, keine Centerlines/
  Spuren/Topologie) - bestaetigt die Projektnotiz "INSPIRE Strassen = nur Plaetze".
  Falls je amtliche Geometrie gewuenscht: eigenes Teilprojekt = ATKIS Basis-DLM
  Verkehr (Portal-Download) + OSM-Conflation (Spur-Paint bleibt OSM-only). Fraglicher
  Nutzen fuer eine fahrbare Stadt, da OSM das kompletteste Netz ist.
- Bus-Linie 6 (`AWiesbadenBusRoute`) faehrt eine EIGENE Polylinie, ist NICHT an
  den Road-/Lane-Graph gekoppelt - Busspuren hier beeinflussen den Bus nicht,
  nur die Verkehrs-KI (die `bIsBusLane` bereits liest).

## 13. Erfolgskriterien

- Randlinien + korrekte durchgezogen/gestrichelt-Linien auf klassifizierten Strassen.
- Zebrastreifen an OSM-Fussgaengerueberwegen.
- Abbiegepfeile an Kreuzungen mit `turn:lanes`.
- Haifischzaehne an Yield-Zufahrten.
- Busspuren: breite Grenzlinie + "BUS", `bIsBusLane` gesetzt (KI nutzt es).
- Radstreifen: rote Flaeche + Randlinie + Piktogramm an `cycleway=lane`-Strassen.
- Alle datenreinen Tests gruen; je Modul ein Foto-Beleg auf der Scratch-Bake;
  keine Erfindung von Fakten wo OSM schweigt.
