# Umbau-Spezifikation: Chunk-Geometrie beim Backen vorkochen (ProcMesh -> StaticMesh)

Status: Spezifikation fuer den Worker. Betrifft `Source/WiesbadenReal/World/WiesbadenCityChunk.h/.cpp`
(Worker-WIP) und den nicht-tabu Bake-Pfad in `WiesbadenWorldBuilder.cpp`.
Erstellt 2026-09-05 nach A/B-Messung des Zellwechsel-Rucklers.

## Problem (gemessen)

Beim Streamen einer WP-Zelle ruckelt der Spielstrang. Ursache: der Chunk haelt
seine Geometrie als `UProceduralMeshComponent` (`WiesbadenCityChunk.h:146/149`,
`RoadMesh`/`BuildingMesh`, UPROPERTY -> Section-Geometrie wird in die Karte
serialisiert). Ein ProcMesh serialisiert aber **weder die gekochte Kollision
noch den Render-Proxy** -> beim Stream-in werden **Render-Sections neu gebaut
UND die Trimesh-Kollision neu gekocht** ("LogChaos: Input trimesh contains N bad
triangles").

Messung (250 km/h ueber Alkis3, async Cooking bereits an):
- stehend: Mittel 13-15 ms, 0 Aussetzer, Spielstrang 6 ms.
- waehrend Fahrt: Fenster-Mittel 41 -> 75 -> 90 ms (24 -> 11 fps), 61-89
  Aussetzer > 50 ms je 15 s, **Spielstrang-Spitzen 163-223 ms**.
- Dabei 620 Kollisions-Cooks nebenher (async) und **0** ProcessLoadedPackages.

**Kernbefund:** Der Cook ist NICHT der Engpass (er laeuft bereits async, s.
`WiesbadenCityChunk.cpp:32-33 bUseAsyncCooking=true`, und trotzdem 223 ms
Spielstrang). Dominierend sind **Render-Section-Neuaufbau + Actor-Registrierung +
Instanz-Setup** pro Zelle. Reines Kollisions-Vorkochen wuerde daher kaum helfen.

## Ziel

Eine gestreamte Zelle baut zur Laufzeit **nichts** neu auf und kocht **nichts**:
Geometrie, Render-Daten und gekochte Kollision liegen fertig serialisiert vor.

## Loesung: gebackene Chunks auf StaticMesh umstellen

Ein `UStaticMesh` serialisiert gekochte Kollision **und** Render-Daten
(LODs/Render-Proxy). Ein `UStaticMeshComponent` laedt sie beim Stream-in ohne
Cook und ohne Section-Neuaufbau.

### Aenderungen in `WiesbadenCityChunk.h/.cpp` (Worker)

1. **Komponenten:** `RoadMesh`/`BuildingMesh` von `UProceduralMeshComponent`
   auf `UStaticMeshComponent` umstellen (UPROPERTY beibehalten). Getter
   `GetRoadMesh()/GetBuildingMesh()` entsprechend anpassen (Aufrufer:
   `WiesbadenCitySubsystem` Wicklungs-/Hoehen-Diagnose liest `GetProcMeshSection`
   -> auf StaticMesh-RenderData bzw. eine gecachte Vertex-Kopie umstellen, ODER
   die Diagnose auf die Bake-Zeit ziehen).

2. **`ApplyChunk` (nur Bake, `#if WITH_EDITOR`):**
   - Geometrie wie bisher je Section aufbauen (Vertices/Indices/Normalen/UVs/
     Vertexfarben aus `FRoadMeshSection`/`FBuildingMeshSection`).
   - Aus den Sections eine `FMeshDescription` je Chunk-Mesh bauen (ein Section
     -> ein PolygonGroup = ein Material-Slot; Reihenfolge wie bisher, damit
     `SetRoadSectionMaterial(Index)` weiter passt).
   - `UStaticMesh` erzeugen (`NewObject<UStaticMesh>` in ein je-Chunk-Paket) und
     `BuildFromMeshDescriptions({&MeshDesc}, Params)` aufrufen.
   - Kollision: `BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple`
     (Trimesh, wie bisher die ProcMesh-Kollision), `CreatePhysicsMeshes()` beim
     Bake ausloesen, damit die gekochte Kollision im Asset landet. Kollision je
     nach `bRoadCollision`/`bBuildingCollision` (unveraendert durchgereicht).
   - StaticMesh dem `UStaticMeshComponent` zuweisen; Material-Slots je Section.
   - Alternativ (falls MeshDescription-Weg zu aufwendig): temporaeren ProcMesh
     lokal aufbauen und `UKismetProceduralMeshLibrary`-nahe Konvertierung /
     `FMeshDescriptionBuilder` nutzen; das Ziel-Asset ist in jedem Fall ein
     `UStaticMesh` mit gekochter Kollision.

3. **Asset-Ablage:** je Chunk ein `UStaticMesh` in einem WP-tauglichen Paket
   (External-Actor-Paket des Chunks oder ein `/Game/Generated/Chunks/`-Pfad).
   Beim Spielstart wird NUR geladen, nicht gebaut. `bUseAsyncCooking` entfaellt
   (kein Laufzeit-Cook mehr).

4. **`PostLoad`/Konstruktor:** kein Cook, kein Section-Aufbau mehr noetig.

5. **Bounds/Anker:** `AnchorEmptyInstanceComponents` und die Punkt-Bounds-Logik
   fuer leere Chunks auf `UStaticMeshComponent` (bzw. fehlendes StaticMesh)
   uebertragen.

### Bake-Pfad `WiesbadenWorldBuilder.cpp` (nicht-tabu, kann ich liefern)

- `SpawnCityChunks` bleibt strukturell gleich; `ApplyChunk` erzeugt jetzt
  StaticMesh-Assets. Nach dem Bake die erzeugten Assets speichern
  (`UPackage::SavePackage` / `save_dirty_packages`).
- Wenn gewuenscht, kann ich eine wiederverwendbare Bake-Hilfsroutine
  (Sections -> `FMeshDescription` -> `UStaticMesh` inkl. Kollisions-Cook) im
  WorldBuilder bereitstellen, die `ApplyChunk` aufruft - so bleibt der
  tabu-seitige Diff im Chunk klein.

## Akzeptanzkriterien (Pflicht)

- **Trimesh-Kollision ist vorgekocht und im Chunk-Paket serialisiert:** zur
  Laufzeit erscheint **keine** einzige `LogChaos: Input trimesh contains N bad
  triangles`-Zeile mehr beim Zellwechsel (die Kollision kommt gekocht aus dem
  Asset/DDC statt neu gekocht). Das ist der explizit angeforderte Punkt.
- Der Chunk kocht/baut beim Stream-in nichts neu (weder Kollision noch
  Render-Sections).
- `bUseAsyncCooking` am Chunk ist damit gegenstandslos und kann entfallen.

## Validierung (nach Umbau)

- Re-Bake der Stadt, dann 250-km/h-Fahrt wie in `flug_zellwechsel.cmd`.
- Erwartung: **keine** "bad triangles" mehr zur Laufzeit; Spielstrang-Spitzen
  und Aussetzer-Quote je 15 s deutlich runter; Fenster-Mittel nahe dem
  stehenden Wert (~13-16 ms).
- Gegenprobe-Metriken schon vorhanden: 15-s-Bildzeit-Logger
  ("Bildzeit ... schlechtestes ... Aussetzer") und "Straenge: Spiel X ms" in
  `WiesbadenCitySubsystem`.

## Nicht-Ziele / Hinweise

- Reines Kollisions-Vorkochen (separate Kollisions-Actor) NICHT verfolgen - die
  Messung zeigt, dass der Cook nicht der Engpass ist.
- Nanite fuer die Chunk-StaticMeshes optional; erst nach der Kollisions-/Render-
  Serialisierung bewerten.
- Speicher/Paketgroesse im Auge behalten (ein StaticMesh je der ~1.394 Zellen).
