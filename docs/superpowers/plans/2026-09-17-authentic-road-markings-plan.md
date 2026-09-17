# Implementierungsplan — Echte Fahrspuren & Beschriftungen (OSM)

**Spec:** `docs/superpowers/specs/2026-09-17-authentic-road-markings-design.md`
**Branch:** `feature/ampeln-phase2`
**Vorgehen:** phasenweise, testgetrieben (RED→GREEN im datenreinen Layer),
nach jeder Phase Build. Datenreine Tests via `IMPLEMENT_SIMPLE_AUTOMATION_TEST`
(`Tests/GeneratorTest.cpp`); Sicht-Nachweis je Modul per Re-Bake in eine
Scratch-Map + Foto (HighResShot-Pose). Jede Phase ist eigenständig baubar und
lässt Alkis4 (die einzige spielbare Karte) unangetastet.

Ein Modul = ein Settings-Bool = ein Re-Bake-Foto. Fertige, aber noch nicht
gebackene Module werden vor einem Bake gebündelt, um die ~2 h zu amortisieren.

---

## Ausgangslage (Stand 2026-09-17)

Implementierung hat die strenge Spec-Reihenfolge übersprungen; dieser Plan
schreibt den Ist-Stand fest und plant den Rest.

- **Phase 0 (Bake-Gate): BESTANDEN.** No-Code-Re-Bake Alkis4 → `WiesbadenCity_Phase0`
  lieferte eine befüllte Karte (Build 900,9 s). Pipeline gesund. Nebenbefund:
  3813 schwebende / 13 vergrabene Kreuzungen im Höhenreport — vorbestehend,
  eigenes Anliegen, **kein Blocker** für Markierungen.
- **Phase 1 (Datenmodell + Resolver): FERTIG, committet `39813e3`.**
  `FLaneAttributes`, `ELaneBoundaryStyle`, `FRoadLane`/`FRoadSegment`-Felder,
  `URoadTypeLibrary::ResolveLaneAttributes` (bus/psv/busway, cycleway, turn:lanes),
  `ApplyBuiltInDefaults`→`bHasEdgeLineMarking`, Generator-Verdrahtung, Tests A–F.
- **Phase 2 (Rand-/StVO-Linien): IMPLEMENTIERT, UNCOMMITTET, UNGEGATET.**
  `BuildLaneMarkings` iteriert über `BoundaryStyleAt(b)`; Randlinien um Breite/2
  eingesetzt; Dashed R=0 / Solid `WideLineWidthCm` / Edge / DirSplit (solid ≥50,
  sonst dashed). EdgeLines-Test vorhanden. Working-Tree:
  `Source/WiesbadenReal/GIS/RoadNetworkGenerator.cpp`,
  `Source/WiesbadenReal/Tests/GeneratorTest.cpp`.
  **Offen:** Settings-Bool `bGenerateEdgeLines` fehlt noch (heute
  unbedingt gerendert); Commit + Re-Bake-Foto ausstehend.
- **Phasen 3–7:** noch nicht begonnen (Greenfield).

Verworrenes Working-Tree: Phase 2 liegt neben unabhängiger uncommitteter Arbeit
(Audio-Mixer, Bus-Modell/Blinds/Monitor) und Phase-0-Bake-Artefakten
(`Content/Maps/WiesbadenCity_Phase0.umap`, Chunk-Churn). **Diese gehören NICHT
in die Markierungs-Commits** — thematisch trennen (Backup → `checkout HEAD` →
Ziel-Hunks reapplizieren, s. Commit-Schnitt).

---

## Gemeinsame Konventionen (für alle Phasen)

**Symbole/Anker im Code (verifiziert):**
- Linien/Zebra/Flächen (linear) → `FRoadGenerationSettings`
  (`RoadNetworkGenerator.h:103`, als `RoadSettings` auf `WiesbadenWorldBuilder`
  und `WiesbadenCityPipeline`).
- Symbole (Pfeile, BUS, Rad, Haifischzähne) → `FRoadFurnitureSettings`
  (`RoadFurnitureGenerator.h:162`, als `FurnitureSettings`).
- Mesh-Kanäle: `ERoadMeshChannel` (`RoadNetworkGenerator.h:16`) =
  {Carriageway, Sidewalk, Kerb, Intersection, LaneMarking, Crossing, Cycleway}.
  **Neu für Phase 7:** `BikeLaneSurface`.
- Symbol-Arten: `ERoadMarkingKind` (`RoadFurnitureGenerator.h:16`) =
  {StopLine, GiveWayLine, SpeedZone30, MAX}. **Neu:** `TurnArrow`, `BusText`,
  `BikeSymbol`, `GiveWayTeeth` (vor `MAX`).
- Konstanten: `WiesbadenRoadMarkings.h` (hat `ZebraStripeWidthCm=50`,
  `WideLineWidthCm=25`). Je Phase ergänzen.
- Kanal→Material-Mapping: `WiesbadenWorldBuilder.cpp` (~:1778) **und**
  `WiesbadenCityActor.cpp` — bei jedem neuen Kanal BEIDE Stellen pflegen.

**Build:** `Tools/build_only.cmd` (bzw. `build_gate1.cmd`). Nach jeder Phase grün.

**Test:** datenreine Automation-Suite (`GeneratorTest.cpp`) — headless, kein
Bake. RED zuerst, dann GREEN.

**Re-Bake (nur Phasen mit Geometrie):** `Tools/rebuild_city.py` über
`rebake_phase0.cmd`-Muster mit `WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis4`,
`WB_TARGET_MAP=/Game/Maps/WiesbadenCity_<Modul>`. Editor direkt via Bash
`run_in_background` starten (nicht `cmd //c` — öffnet interaktive Shell),
Log pollen. Alkis4 bleibt erhalten; erst bei finaler Abnahme wird die Scratch-Map
zur neuen Default-Karte.

**Foto:** HighResShot-Posensystem (`MSYS_NO_PATHCONV=1`, `-WbShotPoseFile`) auf
bekannte Kreuzungen / Linie-6-Korridor; Abgleich mit realen Wiesbaden-Referenzen.

**Gating:** Jedes Modul default `true`. Für ein sauberes Einzel-Modul-Foto die
noch nicht abgenommenen Bools temporär aus (Env-Override in `rebuild_city.py` via
`override_struct_field` oder direkt im Settings-Actor). Amortisieren: mehrere
fertige Module in einem Bake bündeln.

**Commit:** ein Commit je Phase, thematisch isoliert (kein `git add -A`).

---

## Phase 2 — Rand-/StVO-Linien ABSCHLIESSEN (Modul 1)

*Code liegt; hier nur Gate + Commit + Beleg.*

1. `bGenerateEdgeLines` (bool, default `true`) in `FRoadGenerationSettings`
   ergänzen; `BuildLaneMarkings`-Erweiterung (Rand-/Grenzlinien) daran hängen —
   ist es aus, verhält sich `BuildLaneMarkings` wie vor Phase 2 (nur innere
   Grenzen).
2. Test: EdgeLines-Test grün + ein Guard „`bGenerateEdgeLines=false` ⇒ keine
   Randlinien-Verts" ergänzen. **Build + Suite grün.**
3. **Commit** (isoliert): `RoadNetworkGenerator.cpp` (nur Phase-2-Hunks) +
   `GeneratorTest.cpp` (nur EdgeLines-Test) + `RoadNetworkGenerator.h`
   (Settings-Bool). Botschaft: „Fahrspuren: Rand-/StVO-Linien (Modul 1)".
4. **Re-Bake** `WiesbadenCity_Lines` → **Foto** klassifizierte Straße
   (Randlinie + durchgezogene Richtungstrennung) vs. residential (keine
   Randlinie). Beleg an die Spec-Erfolgskriterien.

---

## Phase 3 — Zebrastreifen (Modul 3)

**Ziel:** `Crossing`-Kanal (`M_WbLaneMarking`) mit Querstreifen füllen; heute
emittiert `BuildIntersectionMesh` keine.

1. Konstanten in `WiesbadenRoadMarkings.h`: `ZebraGapCm=50`, `ZebraMinWidthCm=300`
   (`ZebraStripeWidthCm=50` existiert).
2. **RED** `GeneratorTest.cpp`: Geometrie-Mathe — Streifenzahl/-lage über eine
   gegebene Fahrbahnspanne (parallele Streifen quer zur Fahrtrichtung, Breite 50 /
   Lücke 50); Guard: Spanne < `ZebraMinWidthCm` ⇒ kein Streifen (no-op).
3. **GREEN** freie Funktion `BuildZebraStripes(span, dir) → FRoadMeshData`
   (datenrein, testbar), dann `BuildCrossingMesh` in `RoadNetworkGenerator.cpp`
   neben `BuildIntersectionMesh`, das sie in den `Crossing`-Kanal schreibt.
4. Quelle: primär OSM-`crossing`-Knoten am Weg (präzise Mittblock-Lage);
   `FRoadIntersection::bHasZebraCrossing` (bereits gesetzt) als Fallback.
5. `bGenerateCrossings` (default `true`) in `FRoadGenerationSettings`.
6. **Build + Suite grün** → **Re-Bake** `WiesbadenCity_Zebra` → **Foto**
   Fußgängerüberweg.

---

## Phase 4 — Abbiegepfeile (Modul 4)

**Ziel:** `turn:lanes` → `TurnFlags` (Phase 1 liefert sie bereits pro Spur) →
Pfeil-Instanzen im Anlauf zur Kreuzung.

1. `ERoadMarkingKind::TurnArrow`; Konstanten `SymbolRepeatSpacingCm=4000`,
   `TurnArrowsPerApproach` (1–2) in `WiesbadenRoadMarkings.h`.
2. Assets via `Tools/make_road_marking_textures.py` (Pillow, Muster
   `make_nerobergbahn_textures.py`): `T_WbArrows` (Atlas: through/left/right/
   left+through/right+through/u-turn) + `M_WbRoadArrow` (unlit, alpha-maskiert
   weiß). UE-Import-Skript wie `make_dfi_materials.py`.
3. **RED** `GeneratorTest.cpp`: Pfeil-Platzierung — je Zufahrtsspur mit
   `TurnFlags` genau N Instanzen, Offset zur Kreuzung, in Spurrichtung;
   Flag-Kombi → korrekte Atlas-Kachel; unbekannter/leerer Flag ⇒ kein Pfeil.
4. **GREEN** `URoadFurnitureGenerator::PlaceMarkings` um `TurnArrow` erweitern
   (liest angereicherten Lane-Graph); `SpawnMarkings` (HISM-Plane) kennt das
   neue Kind + Material/Atlas-UV.
5. `bGenerateTurnArrows` (default `true`) in `FRoadFurnitureSettings`.
6. **Build + Suite grün** → **Re-Bake** `WiesbadenCity_Arrows` → **Foto**
   Kreuzung mit `turn:lanes`.

---

## Phase 5 — Wartelinien / Haifischzähne (Modul 5)

**Ziel:** Haifischzähne (Z.342) an Vorfahrt-gewähren-Zufahrten; die vorhandene
`GiveWayLine`/Haltlinie bleibt.

1. `ERoadMarkingKind::GiveWayTeeth`; Textur `T_WbSharkTeeth` (kachelbares
   Dreieckband) + leichtes unlit-maskiertes Material (in Phase-4-Toolskript
   mitziehen).
2. **RED** `GeneratorTest.cpp`: Zahnband über Spurbreite an Yield-Zufahrt;
   Guard: keine Yield-Zufahrt ⇒ kein Band.
3. **GREEN** `PlaceMarkings` + `SpawnMarkings` um `GiveWayTeeth` (Plane in
   Spurbreite, gekachelte Textur) an Yield-Zufahrten erweitern.
4. `bGenerateGiveWayTeeth` (default `true`) in `FRoadFurnitureSettings`.
5. **Build + Suite grün** → **Re-Bake** `WiesbadenCity_Yield` → **Foto**
   Vorfahrt-gewähren-Zufahrt.

---

## Phase 6 — Busspuren (Modul 2)

**Ziel:** `bIsBusLane` (Phase 1 setzt es) → breite durchgezogene Grenzlinie +
„BUS"-Piktogramm. **KEINE Farbfläche** (deutsche Praxis).

1. Linear: in `BuildLaneMarkings` für Bus-Spurgrenze breite durchgezogene Linie
   (`WideLineWidthCm=25`, R=255). Symbol: `ERoadMarkingKind::BusText` + Textur
   `T_WbBusText` („BUS") + unlit-maskiertes Material.
2. **RED** `GeneratorTest.cpp`: „BUS" periodisch je `bIsBusLane`-Spur
   (`SymbolRepeatSpacingCm`-Takt) **und** einmal direkt nach jeder Kreuzung;
   breite Grenzlinie an der Bus-Spurgrenze.
3. **GREEN** `BuildLaneMarkings` (Grenzlinie) + `PlaceMarkings`/`SpawnMarkings`
   (BusText).
4. `bGenerateBusLanes` (default `true`) in `FRoadGenerationSettings` (Grenzlinie)
   — Symbolteil an dasselbe Flag koppeln.
5. **Build + Suite grün** → **Re-Bake** `WiesbadenCity_Bus` → **Foto**
   Busspur-Korridor. (Hinweis: beeinflusst nur die Verkehrs-KI, nicht die
   Linie-6-Polylinie — s. Spec §12.)

---

## Phase 7 — On-Street-Radstreifen (Modul 6)

**Ziel:** `cycleway=lane/track` (Phase 1 setzt `bIsBikeLane`) → **rote Fläche** +
durchgezogene Randlinie + Fahrrad-Piktogramm.

1. **Neuer Kanal** `ERoadMeshChannel::BikeLaneSurface` + Material
   `M_WbBikeLaneSurface` (rot). Kanal→Material-Mapping in
   `WiesbadenWorldBuilder.cpp` UND `WiesbadenCityActor.cpp` ergänzen (nicht mit
   dem bestehenden `Cycleway`-Kanal/`M_WbCycleway` für eigenständige Radwege
   verwechseln).
2. Konstante `BikeLaneSurfaceInsetCm` (Rand der roten Fläche) in
   `WiesbadenRoadMarkings.h`. Symbol `ERoadMarkingKind::BikeSymbol` + Textur
   `T_WbBikeSymbol` + Material.
3. **RED** `GeneratorTest.cpp`: rote Sub-Ribbon über Spurbreite (Inset korrekt) +
   durchgezogene Randlinie + Rad-Piktogramm im `SymbolRepeatSpacingCm`-Takt je
   `bIsBikeLane`-Spur; Guard: keine Radspur ⇒ keine Fläche.
4. **GREEN** linearer Flächen-Emitter in den neuen Kanal +
   `PlaceMarkings`/`SpawnMarkings` (BikeSymbol).
5. `bGenerateBikeLanes` (default `true`) in `FRoadGenerationSettings`.
6. **Build + Suite grün** → **Re-Bake** `WiesbadenCity_Bike` → **Foto**
   `cycleway=lane`-Straße.

---

## Test-Matrix (datenrein, `Tests/GeneratorTest.cpp`)

Aus Spec §8; Phase 1 (Tag→Spur, Pro-Spur-Token-Mapping, Grenzstil-Auflösung)
ist grün. Ergänzend je Phase:

- **P3 Zebra:** Streifenzahl/-lage je Spanne; degenerierte Spanne → no-op.
- **P4 Pfeile:** Flag→Atlas-Kachel; Instanzzahl/Offset; unbekannter Token → kein
  Pfeil.
- **P5 Haifischzähne:** Band über Spurbreite an Yield; sonst nichts.
- **P6 Bus:** breite Grenzlinie + „BUS"-Takt inkl. Nach-Kreuzung.
- **P7 Rad:** rote Fläche (Inset) + Randlinie + Piktogramm-Takt.
- **Guards (alle):** < 2 Spuren, degeneriert, fehlende Tags → keine Geometrie,
  kein Crash.

---

## Definition of Done (aus Spec §13)

- Randlinien + korrekte durchgezogen/gestrichelt-Linien auf klassifizierten
  Straßen (P2). ✔ Code; Foto offen.
- Zebrastreifen an OSM-Überwegen (P3).
- Abbiegepfeile an `turn:lanes`-Kreuzungen (P4).
- Haifischzähne an Yield-Zufahrten (P5).
- Busspuren: breite Grenzlinie + „BUS", `bIsBusLane` gesetzt (P6).
- Radstreifen: rote Fläche + Randlinie + Piktogramm an `cycleway=lane` (P7).
- Alle datenreinen Tests grün; je Modul ein Foto-Beleg auf der Scratch-Bake;
  keine Erfindung von Fakten, wo OSM schweigt.

---

## Risiken & offene Punkte

- **Bake-Gesundheit:** Gate bestanden. 3813 schwebende/13 vergrabene Kreuzungen
  bleiben ein separates, nachgelagertes Anliegen.
- **Dreiecks-/Instanz-Budget** stadtweit beobachten (dünne Ribbons + instanzierte
  Symbole sind günstig).
- **OSM-Tag-Sparsity:** viele Straßen bekommen nur Konventionslinien — by design.
- **Amtliche Straßengeometrie** (ATKIS Basis-DLM) = eigenes Zukunftsprojekt
  (Spec §12); Spur-Paint bleibt OSM-only.

---

## Commit-Schnitt

Ein Commit je Phase (P2 Rand-/StVO-Linien, P3 Zebra, P4 Pfeile, P5
Haifischzähne, P6 Bus, P7 Rad). Weil das Working-Tree entzerrt werden muss
(Phase 2 liegt neben Audio-Mixer- und Bus-Modell-Arbeit): je Commit die
Zieldateien nach `/tmp` sichern, geteilte Dateien auf `HEAD` zurücksetzen, nur
die Modul-Hunks reapplizieren, bauen, committen, Rest zurückspielen, neu bauen
(bewährtes Muster, da `git add -p` in dieser Umgebung fehlt). Push auf
`feature/ampeln-phase2` nach deiner Freigabe je Schritt (Projekt-Konvention).
Phase-0-Bake-Artefakte (`WiesbadenCity_Phase0.umap`, Chunk-Churn) NICHT
committen — verwerfen.
