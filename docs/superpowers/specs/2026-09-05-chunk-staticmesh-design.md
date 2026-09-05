# Design-Spec: Stadt-Chunks von ProceduralMesh auf gebackenes StaticMesh

Datum: 2026-09-05
Status: Design freigegeben (Brainstorming). Naechster Schritt: Implementierungsplan.
Ersetzt die kurze Uebergabe-Notiz `docs/plans/2026-09-05-chunk-staticmesh-vorkochen.md`
(die bleibt als Kurzfassung bestehen; DIESE Spec ist die massgebliche Fassung).

## 1. Motivation (gemessen)

Beim schnellen Durchfahren der World-Partition-Stadt ruckelt der Spielstrang. Per
`stat dumphitches` (250 km/h, Alkis3) wurde je Ruckler-Frame die groesste
Stream-in-Arbeit aufgeschluesselt:

| Rang | Arbeit | Scope | Kosten/Hitch |
| --- | --- | --- | --- |
| 1 | Render-Proxy-Aufbau der ProcMesh-Chunks | `STAT_ProcMesh_CreateSceneProxy` | ~40-51 ms je Chunk |
| 2 | Foliage/HISM-Proxy (Baeume/Buesche) | `STAT_FoliageCreateProxy` | ~29 ms |
| 3 | Physik/Kollision | `STAT_TG_EndPhysics` | ~16 ms |
| 4 | Actor-/Komponenten-Registrierung | `STAT_AddToWorldTime`, `STAT_RegisterComponent` | ~11-14 ms |

Kernbefund: Der Kollisions-Cook ist NICHT der Engpass (er laeuft bereits async,
`bUseAsyncCooking=true`), und der groesste Posten ist der **Render-Proxy-Aufbau der
ProceduralMesh-Chunks**. Ein `UProceduralMeshComponent` serialisiert weder Render-
Proxy noch gekochte Kollision -> jede gestreamte Zelle baut beides zur Laufzeit neu.

Ziel: eine gestreamte Zelle baut/kocht zur Laufzeit **nichts**. Ein `UStaticMesh`
serialisiert Render-Daten UND gekochte Kollision -> Stream-in ist reines Laden.

Nicht-Ziel (Rang 2, separat): die Baum-/Busch-HISM bleiben unveraendert beim
`RegionAssetSpawner`. Nanite bleibt vorerst aussen vor (Wechselwirkung mit dem
Masked-Dither-Fade; spaeter zu bewerten).

## 2. Festgelegte Entscheidungen

- **Ansatz C:** Der (nicht-tabu) `WorldBuilder` backt die StaticMesh-Assets; der
  (Worker-WIP) `WiesbadenCityChunk` wird duenn und referenziert sie nur.
- **Speicherung:** je Zelle eigene StaticMesh-Assets unter `/Game/Generated/Chunks/`.
- **Migration:** frische Karte **Alkis7** aus Alkis3-Quelle; Alkis3/4 bleiben Rueckweg.
- **Nanite:** vorerst nein.
- **Ergebnis dieses Dokuments:** Architektur-Spec. Umsetzung: Chunk-Teil durch den
  Worker (tabu), Bake-Pfad/Baker-Erweiterung durch uns (nicht-tabu), separat.

## 3. Architektur (zwei Seiten, getrennt an der Tabu-Grenze)

**Bake-Seite (nicht-tabu):** `WiesbadenWorldBuilder.cpp` + `WiesbadenChunkStaticMeshBaker`.
Erzeugt je Zelle aus den Section-Daten je ein Strassen- und ein Gebaeude-`UStaticMesh`,
speichert die Pakete und reicht die Assets an den Chunk.

**Laufzeit-Seite (Worker-WIP, tabu):** `WiesbadenCityChunk.h/.cpp`. Haelt statt der zwei
`UProceduralMeshComponent` kuenftig zwei `UStaticMeshComponent`. Wird mit Komponenten +
Asset-Referenz ins WP-External-Actor-Paket serialisiert; zur Laufzeit laedt WP den Actor,
die SMCs ziehen ihr Asset -> kein Build, kein Cook.

### Neue Chunk-Schnittstelle (Worker)

```cpp
// Komponenten: RoadMesh/BuildingMesh -> UStaticMeshComponent (statt UProceduralMeshComponent)

// Bake-Zeit (WITH_EDITOR): weist die gebackenen Assets zu und merkt sich die
// Slot-Metadaten fuer die Laufzeit-Materialaufloesung.
void SetChunkStaticMeshes(
    UStaticMesh* RoadSM,
    UStaticMesh* BuildingSM,
    const TArray<uint8>& RoadSectionChannels,        // je Road-Slot: ERoadMeshChannel
    const TArray<FBuildingSectionKey>& BuildingKeys);// je Building-Slot: MaterialVariant + FacadeOverrideKey

UStaticMeshComponent* GetRoadMesh() const;      // Rueckgabetyp AENDERT sich
UStaticMeshComponent* GetBuildingMesh() const;  //   von UProceduralMeshComponent*
```

`ApplyChunk` entfaellt im Geometrie-Teil (kein `CreateMeshSection` mehr). Region-Assets
und Bounds/Anker bleiben im Chunk.

### Bewusster Bruch (Folgeaenderung)

`GetRoadMesh()/GetBuildingMesh()` liefern kuenftig `UStaticMeshComponent*`. **Konsument**
`WiesbadenCitySubsystem` liest heute `GetProcMeshSection()` (Wicklungs-/Hoehen-Diagnose,
`LogHeightStackNearPlayer`, Wicklungs-Vergleich). Umstellung: entweder auf StaticMesh-
RenderData lesen ODER die Diagnose auf die Bake-Zeit ziehen (empfohlen, da die Geometrie
dort ohnehin vorliegt). In DIESER Umstellung mit einplanen.

## 4. Datenfluss

### Beim Backen (Editor, WorldBuilder)

1. Je Zelle Chunk-Actor spawnen (wie heute, `SpawnCityChunks`).
2. Aus `ChunkMesh.RoadSections/BuildingSections` je ein `UStaticMesh` backen
   (temporaeres ProcMesh -> getesteter Baker -> `BuildFromMeshDescriptions` +
   Complex-as-Simple-Kollision gekocht). Paket unter `/Game/Generated/Chunks/` speichern.
3. `chunk->SetChunkStaticMeshes(RoadSM, BuildingSM, Channels, Keys)`.
4. Materialien wie heute aufloesen (`ResolveRoadMaterial`, Fassaden-Varianten) und je
   Slot am StaticMeshComponent setzen (`SetMaterial(slot, mat)`).
5. `SetRegionAssets` unveraendert. Karte + External-Actor-Pakete + StaticMesh-Pakete speichern.

### Zur Laufzeit (WP-Stream-in)

1. WP aktiviert Zelle -> laedt Chunk-Actor.
2. SMCs referenzieren ihr Asset -> async laden (Render-Daten UND gekochte Kollision fertig).
   Kein `CreateMeshSection`, kein Cook.
3. Serialisierte Material-Overrides + Fade-MPC greifen; Region-Asset-HISM wie bisher.

Netto: ProcMesh-`CreateSceneProxy` (~50 ms) + Trimesh-Cook je Zellwechsel entfallen; uebrig
bleiben guenstiger StaticMesh-Proxy (serialisiert) + der separate HISM-Posten.

## 5. Material-Slots & Overrides

- Der Baker legt je Section EINEN Slot an (Reihenfolge = Section-Reihenfolge), Default-Material.
- Laufzeit-Aufloesung 1:1 als Komponenten-Override:
  - Strasse: `RoadSectionChannels[i]` -> `ResolveRoadMaterial(channel)` -> `SMC->SetMaterial(i, ...)`.
  - Gebaeude: `BuildingKeys[i]` (MaterialVariant + FacadeOverrideKey) -> Fassaden-Material -> `SMC->SetMaterial(i, ...)`.
- AAA/Wb-Auswahl, Fassaden-Varianten und das radius-getriebene Fade (MPC `FadeRadiusM`)
  wirken unveraendert als Material-Instanz-Overrides; die serialisierte Override-Liste der
  SMC traegt sie. Terrain bleibt ausgenommen; Glas-Transluzenz-Schutz wie im Fade-Skript.

## 6. Kollision (Paritaet wahren)

Heute pro Section entschieden: `bRoadCollision && !bIsEmbankment` (Boeschungen ohne Kollision).
Bei einem Strassen-Mesh je Chunk nicht direkt moeglich. Loesung:

- Baker-Erweiterung: getrennte Eingaben `renderSections` (alle) und `collisionSections`
  (nur Nicht-Boeschung).
- Umsetzung ueber die UE-Standard-API: das Render-Strassen-`UStaticMesh` aus `renderSections`
  bauen; ein zweites, reines Kollisions-`UStaticMesh` aus `collisionSections` backen und es als
  `RoadSM->ComplexCollisionMesh` setzen (bei `CTF_UseComplexAsSimple` nutzt die BodySetup dann
  die Geometrie dieses Kollisions-Meshes statt der Render-Geometrie). Sind alle Sections
  kollidierbar (keine Boeschung), entfaellt das zweite Mesh -> Complex-as-Simple direkt.
- Gebaeude: `bBuildingCollision` wie heute (alle Sections, Complex-as-Simple).
- `-WbNoRoadCollision` (`GetRoadMesh()->SetCollisionEnabled(NoCollision)`) funktioniert weiter.

## 7. Bounds, leere Chunks, Diagnose-Flags

- Leerer Chunk (0 Sections) -> kein StaticMesh (SMC ohne Mesh) -> vorhandene Punkt-Bounds-/
  Anker-Logik (`AnchorEmptyInstanceComponents`) fuer SMC + HISM, vom ProcMesh auf SMC uebertragen.
- `-WbHideChunks` (`SetActorHiddenInGame`), `-WbNoRoadCollision` funktionieren weiter
  (Getter liefert jetzt SMC).

## 8. Fehlerbehandlung (robust, nicht abbrechend)

- Bake-Fehler je Zelle: Baker liefert `nullptr` + `OutError` -> loggen, Zelle ueberspringen
  (SMC leer -> Punkt-Bounds), Fehler zaehlen, am Ende bilanzieren. Kein Gesamt-Abbruch.
- Paket-Speicherfehler -> loggen/zaehlen, nicht-fatal.
- Deterministische Asset-Namen `SM_Road_X_Y` / `SM_Bld_X_Y` -> stabil, re-backbar.
- Laufzeit: fehlt ein Asset -> SMC ohne Mesh -> Zelle rendert nichts, kein Crash. Cook muss
  `/Game/Generated/Chunks/` einschliessen.

## 9. Tests & Akzeptanzkriterien

- Unit (vorhanden): `WiesbadenReal.GIS.ChunkStaticMeshBaker.BakeProducesCollisionMesh`.
- Unit (neu): Bake mit Boeschungs-Sections -> Kollision schliesst sie aus (`collisionSections`).
- End-to-end nach Re-Bake Alkis7: `durchfall_regression.ps1` (0 m Luecke) UND `stat dumphitches`
  bei 250 km/h.

Pflicht-Akzeptanz:
- Zur Laufzeit KEINE `Input trimesh contains N bad triangles`-Zeilen mehr beim Zellwechsel.
- Kein ProcMesh-`CreateSceneProxy` zur Laufzeit; der Chunk baut/kocht beim Stream-in nichts.
- Spielstrang-Spitzen und Aussetzer-Quote je 15 s deutlich unter dem Alkis3-Basiswert;
  Fenster-Mittel nahe dem stehenden Wert (~13-16 ms).
- Visuelle Paritaet: Materialien, Fade und Kollision wie in Alkis3.

## 10. Migration

- Frische Karte Alkis7 aus Alkis3-Quelle backen (`WB_SOURCE_MAP=Alkis3`, `WB_TARGET_MAP=Alkis7`)
  ueber den modifizierten WorldBuilder-Bake. Alkis3/4 unangetastet (Rueckweg).
- `GameDefaultMap` + Launcher-Standard erst NACH Verifikation auf Alkis7 umstellen.
- `/Game/Generated/Chunks/` sind git-ignorierte Content-Assets; reproduzierbar ueber Re-Bake.

## 11. Arbeitsaufteilung

- **Nicht-tabu (wir):** `WiesbadenChunkStaticMeshBaker` (renderSections/collisionSections-
  Erweiterung), `WiesbadenWorldBuilder`-Bake-Pfad (Assets erzeugen/speichern, `SetChunkStaticMeshes`
  aufrufen, Material je Slot setzen), die `WiesbadenCitySubsystem`-Diagnose-Umstellung
  (Konsument von `GetProcMeshSection`), das Alkis7-Re-Bake + die End-to-end-Verifikation.
- **Tabu (Worker):** `WiesbadenCityChunk.h/.cpp` — Komponententyp -> `UStaticMeshComponent`,
  `SetChunkStaticMeshes`, Getter-Rueckgabetyp, Bounds/Anker auf SMC, `ApplyChunk`-Geometrieteil entfernen.

## 12. Nicht-Ziele / Hinweise

- Baum-/Busch-HISM-Streaming bleibt unveraendert (Rang 2, eigenes Vorhaben).
- Nanite fuer die Chunk-Meshes: spaeter bewerten (Fade/Kollision/Cook mitdenken).
- Kein reines Kollisions-Vorkochen ohne Render-Serialisierung — die Messung zeigt, dass der
  Render-Proxy der groessere Posten ist; StaticMesh loest beides zugleich.
