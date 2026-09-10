# WiesbadenCityMapWriter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pare the map-persistence half out of the `AWiesbadenWorldBuilder` god-object into a thin `WiesbadenCityMapWriter` namespace with pure, tested helpers — without changing behaviour.

**Architecture:** A namespace module (mirroring `WiesbadenChunkStaticMeshBaker`) with `Save(UWorld*, FCityMapSaveRequest) -> FCityMapSaveResult` that hides `SaveMap` + `DefaultEngine.ini` surgery + World-Partition save verification. The verification logic moves in as pure helpers: `VerifyWorldPartitionSave` (already pure, relocated) and a new `AllChunksInSeparatePackages` (extracts the inline chunk-package counting that once shipped a blank city). The actor's `SaveCityAsMap` becomes a thin forwarder that gathers chunk facts and calls `Save`.

**Tech Stack:** Unreal Engine 5.8 C++ (editor save utilities, `GConfig`/`FFileHelper`, World Partition), UE automation tests.

**Spec:** the design crystallized in `CONTEXT.md` (see the **CityMapWriter** entry) during the architecture-review grilling of Candidate 3.

## Global Constraints

- **⚠️ GATE — do not start until the foreign WIP has landed.** `WiesbadenWorldBuilder.h/.cpp`, `WiesbadenCityPipeline.h/.cpp`, and `WiesbadenCityData.h` had **uncommitted changes I did not make** (an in-flight pickup-spot generator). Before Task 1, run:
  ```
  git -C C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal status --short Source/WiesbadenReal/GIS/WiesbadenWorldBuilder.h Source/WiesbadenReal/GIS/WiesbadenWorldBuilder.cpp Source/WiesbadenReal/GIS/WiesbadenCityPipeline.h Source/WiesbadenReal/GIS/WiesbadenCityPipeline.cpp Source/WiesbadenReal/Core/WiesbadenCityData.h
  ```
  If the output is **non-empty**, STOP and report — the WIP has not landed; splitting now would clobber work that isn't ours.
- **TABU:** do not modify anything under `WiesbadenCityChunk` (`.h/.cpp`). The writer must NOT depend on `AWiesbadenCityChunk` — the actor gathers chunk package names and passes them in the request.
- **Behaviour-preserving:** the produced `Durchfall.txt`-equivalent here is the saved `.umap` + `DefaultEngine.ini` wiring. The Chaos car / city output must be byte-for-byte the same save; this is a refactor, not a feature.
- **Engine/paths & test workflow:** UE 5.8 at `C:\Program Files\Epic Games\UE_5.8`. Build with `Build.bat WiesbadenRealEditor Win64 Development` (confirm `Result: Succeeded`), then run tests directly (`run_tests.cmd` only builds — see the powertrain plan's note):
  ```
  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/WiesbadenReal.uproject" -ExecCmds="Automation RunTests <NAME>; Quit" -unattended -nop4 -nullrhi -stdout -log=cmw.log
  ```
  grep `Saved/Logs/cmw.log` for `Result={Success}`/`{Fail}`.
- **Never commit red; never fabricate consent.** If a test fails, stop and report.
- **Scoped staging:** `git add` only the exact files named per task. Never `git add -A`.
- **Style:** keep the German comment voice and the `// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.` header.

---

### Task 1: New pure helper — `AllChunksInSeparatePackages` + test

**Files:**
- Create: `Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.h`
- Create: `Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.cpp`
- Test: `Source/WiesbadenReal/Tests/CityMapWriterTest.cpp`

**Interfaces:**
- Produces: `namespace WiesbadenCityMapWriter { bool AllChunksInSeparatePackages(const TArray<FString>& DistinctPackageNames, int32 ChunkCount, int32 ChunksWithoutPackage); }`. Later tasks add `VerifyWorldPartitionSave`, `Save`, and the request/result structs to this same header.

- [ ] **Step 1: Write the failing test**

Create `Source/WiesbadenReal/Tests/CityMapWriterTest.cpp`:

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "GIS/WiesbadenCityMapWriter.h"

// Der Chunk-Package-Check, der beim ALKIS-Rebuild 08-2026 eine unsichtbare Stadt
// durchgehen liess: 1984 Chunks lagen in EINEM 531-MB-Package, der Check meldete
// faelschlich "ok". Hier reine, ohne Welt pruefbare Regressionsabdeckung dafuer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityMapWriterChunkPackagesTest,
    "WiesbadenReal.GIS.CityMapWriter.ChunkPackages",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCityMapWriterChunkPackagesTest::RunTest(const FString& Parameters)
{
    using namespace WiesbadenCityMapWriter;

    // Jeder Chunk in EIGENEM Package -> ok.
    {
        TArray<FString> Names;
        for (int32 I = 0; I < 1984; ++I) { Names.Add(FString::Printf(TEXT("Pkg_%d"), I)); }
        TestTrue(TEXT("1984 Chunks in 1984 Packages -> separat"),
            AllChunksInSeparatePackages(Names, /*ChunkCount=*/1984, /*Missing=*/0));
    }

    // DER HISTORISCHE BUG: alle Chunks teilen EIN Package -> NICHT separat.
    {
        TArray<FString> OnePackage; OnePackage.Add(TEXT("Riesen_Package"));
        TestFalse(TEXT("1984 Chunks in einem Package -> NICHT separat (unsichtbare Stadt)"),
            AllChunksInSeparatePackages(OnePackage, /*ChunkCount=*/1984, /*Missing=*/0));
    }

    // Ein Chunk ohne Package -> NICHT separat.
    {
        TArray<FString> Names = { TEXT("A"), TEXT("B") };
        TestFalse(TEXT("Chunk ohne Package -> NICHT separat"),
            AllChunksInSeparatePackages(Names, /*ChunkCount=*/3, /*Missing=*/1));
    }

    // Zwei Chunks teilen ein Package (distinct < count) -> NICHT separat.
    {
        TArray<FString> Names = { TEXT("A"), TEXT("B") };
        TestFalse(TEXT("Zwei Chunks, ein geteiltes Package -> NICHT separat"),
            AllChunksInSeparatePackages(Names, /*ChunkCount=*/3, /*Missing=*/0));
    }

    // Sauberer Kleinfall.
    {
        TArray<FString> Names = { TEXT("A"), TEXT("B"), TEXT("C") };
        TestTrue(TEXT("3 Chunks in 3 Packages -> separat"),
            AllChunksInSeparatePackages(Names, /*ChunkCount=*/3, /*Missing=*/0));
    }

    return true;
}
```

- [ ] **Step 2: Run the build to verify it fails**

Build: `MSYS_NO_PATHCONV=1 "C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" WiesbadenRealEditor Win64 Development -project="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/WiesbadenReal.uproject" -waitmutex`
Expected: FAILS — `WiesbadenCityMapWriter.h` does not exist.

- [ ] **Step 3: Create the header**

Create `Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.h`:

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Eingabe fuer WiesbadenCityMapWriter::Save (aus dem WorldBuilder-Zustand). */
struct FCityMapSaveRequest
{
    /** Ziel-Asset-Pfad; leer -> /Game/Maps/WiesbadenCity. */
    FString AssetPath;
    /** Chunks erzeugt -> World Partition + WP-Verifikation erwartet. */
    bool bGenerateCityChunks = false;
    /** DefaultEngine.ini als Default-/Startup-Map verdrahten. */
    bool bSetAsDefaultMap = true;
    /** Verschiedene Package-Namen der Chunk-Actors (vom Aufrufer gesammelt). */
    TArray<FString> ChunkPackageNames;
    /** Anzahl Chunk-Actors (CityChunks.Num()). */
    int32 ChunkCount = 0;
    /** Chunk-Actors ohne gueltiges Package. */
    int32 ChunksWithoutPackage = 0;
};

/** Ergebnis von WiesbadenCityMapWriter::Save. */
struct FCityMapSaveResult
{
    bool bSucceeded = false;
    FString Error;
    FString SavedMapRef;   // /Game/Maps/X.X
};

/**
 * Persistenz-Haelfte des WorldBuilders: die gebaute Stadt als echte .umap
 * speichern und als Default-Map verdrahten. Reines Namespace-Modul (wie
 * WiesbadenChunkStaticMeshBaker); die Verifikationslogik liegt als datenreine
 * Helfer hier, damit sie ohne Editor-Speichern pruefbar ist.
 */
namespace WiesbadenCityMapWriter
{
    /**
     * Liegt jeder Chunk in seinem EIGENEN External-Actor-Package? Ohne diese
     * Pruefung kam die kaputte Variante durch, in der alle Chunks EIN Package
     * teilen (unsichtbare Stadt, "Failed import"-Fehler beim Laden).
     */
    bool AllChunksInSeparatePackages(const TArray<FString>& DistinctPackageNames,
        int32 ChunkCount, int32 ChunksWithoutPackage);
}
```

- [ ] **Step 4: Create the implementation**

Create `Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.cpp`:

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenCityMapWriter.h"

bool WiesbadenCityMapWriter::AllChunksInSeparatePackages(
    const TArray<FString>& DistinctPackageNames, int32 ChunkCount, int32 ChunksWithoutPackage)
{
    // Verhalten identisch zur bisherigen Inline-Pruefung im WorldBuilder:
    // kein Chunk ohne Package UND so viele verschiedene Packages wie Chunks.
    return ChunksWithoutPackage == 0
        && DistinctPackageNames.Num() == ChunkCount;
}
```

- [ ] **Step 5: Build + run the test to verify it passes**

Build (as Step 2), then:
`"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/WiesbadenReal.uproject" -ExecCmds="Automation RunTests WiesbadenReal.GIS.CityMapWriter; Quit" -unattended -nop4 -nullrhi -stdout -log=cmw.log`
Expected: `WiesbadenReal.GIS.CityMapWriter.ChunkPackages` → `Result={Success}`.

- [ ] **Step 6: Commit**

```bash
git add Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.h Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.cpp Source/WiesbadenReal/Tests/CityMapWriterTest.cpp
git commit -m "CityMapWriter: Chunk-Package-Check als reinen, getesteten Helfer (Regression unsichtbare Stadt)"
```

---

### Task 2: Relocate the pure verification helpers into the namespace

**Files:**
- Modify: `Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.h` / `.cpp` (add `VerifyWorldPartitionSave`, `HasExternalActorPackages`)
- Modify: `Source/WiesbadenReal/GIS/WiesbadenWorldBuilder.h` (remove the two static decls — **WIP-gated file**)
- Modify: `Source/WiesbadenReal/GIS/WiesbadenWorldBuilder.cpp` (remove the two defs; `SaveCityAsMap` calls the namespace versions for now)
- Modify: `Source/WiesbadenReal/Tests/MapBakeTest.cpp` (repoint `VerifyWorldPartitionSave` references)

**Interfaces:**
- Consumes: Task 1's header.
- Produces: `WiesbadenCityMapWriter::VerifyWorldPartitionSave(bool,bool,bool,bool,FString&)` and `WiesbadenCityMapWriter::HasExternalActorPackages(const FString&)` (WITH_EDITOR).

- [ ] **Step 1: Move the failing test references**

In `Source/WiesbadenReal/Tests/MapBakeTest.cpp`, add `#include "GIS/WiesbadenCityMapWriter.h"` and change the five `AWiesbadenWorldBuilder::VerifyWorldPartitionSave(...)` calls (lines ~107–130) to `WiesbadenCityMapWriter::VerifyWorldPartitionSave(...)`. Leave the assertions and messages unchanged.

- [ ] **Step 2: Build to verify it fails**

Build. Expected: FAILS — `WiesbadenCityMapWriter::VerifyWorldPartitionSave` not declared.

- [ ] **Step 3: Add the two helpers to the namespace**

In `WiesbadenCityMapWriter.h`, add to the namespace:
```cpp
    /** Reine WP-Save-Verifikation (aus dem WorldBuilder hierher gezogen). */
    bool VerifyWorldPartitionSave(bool bIsPartitioned, bool bMapExists,
        bool bExternalActors, bool bChunksInSeparatePackages, FString& OutError);

#if WITH_EDITOR
    /** Liegen External-Actor-Packages fuer die Map auf der Platte? */
    bool HasExternalActorPackages(const FString& MapAssetPath);
#endif
```

In `WiesbadenCityMapWriter.cpp`, move the **exact bodies** of `AWiesbadenWorldBuilder::VerifyWorldPartitionSave` (WorldBuilder.cpp:753–784) and `AWiesbadenWorldBuilder::HasExternalActorPackages` (WorldBuilder.cpp:841–854) here, renaming the qualifier to `WiesbadenCityMapWriter::`. Guard `HasExternalActorPackages` with `#if WITH_EDITOR` and add the includes it needs: `#include "Misc/Paths.h"`, `#include "HAL/FileManager.h"`.

- [ ] **Step 4: Remove them from the actor**

In `WiesbadenWorldBuilder.h`, delete the two `static bool VerifyWorldPartitionSave(...)` and `static bool HasExternalActorPackages(...)` declarations (around lines 630 and 640, with their doc-comments). In `WiesbadenWorldBuilder.cpp`, delete the two definitions (753–784 and 841–854), add `#include "GIS/WiesbadenCityMapWriter.h"`, and update the two call sites inside `SaveCityAsMap` (lines ~729–732) to `WiesbadenCityMapWriter::VerifyWorldPartitionSave(...)` and `WiesbadenCityMapWriter::HasExternalActorPackages(...)`.

- [ ] **Step 5: Build + run tests**

Build, then run `WiesbadenReal.Core.MapBake` and `WiesbadenReal.GIS.CityMapWriter`. Expected: both `Result={Success}` (MapBake's five verification cases now exercise the relocated function).

- [ ] **Step 6: Commit**

```bash
git add Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.h Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.cpp Source/WiesbadenReal/GIS/WiesbadenWorldBuilder.h Source/WiesbadenReal/GIS/WiesbadenWorldBuilder.cpp Source/WiesbadenReal/Tests/MapBakeTest.cpp
git commit -m "CityMapWriter: WP-Verifikation + External-Actor-Check aus dem WorldBuilder ziehen"
```

---

### Task 3: Extract `Save()` and thin the actor's `SaveCityAsMap`

**Files:**
- Modify: `Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.h` / `.cpp` (add `Save`)
- Modify: `Source/WiesbadenReal/GIS/WiesbadenWorldBuilder.cpp` (`SaveCityAsMap` → thin forwarder)

**Interfaces:**
- Consumes: Task 1 + 2 (`AllChunksInSeparatePackages`, `VerifyWorldPartitionSave`, `HasExternalActorPackages`), plus the existing free function `ApplyDefaultMapToIniText` (from `GIS/WiesbadenBuildSummary.h`).
- Produces: `WiesbadenCityMapWriter::Save(UWorld*, const FCityMapSaveRequest&) -> FCityMapSaveResult` (WITH_EDITOR).

- [ ] **Step 1: Add `Save` to the header**

In `WiesbadenCityMapWriter.h`, inside `#if WITH_EDITOR`:
```cpp
    /**
     * Speichert die im Level liegende, gebaute Stadt als echte .umap unter
     * Request.AssetPath, verdrahtet sie (optional) als Default-Map und
     * verifiziert bei bGenerateCityChunks die World-Partition-Externalisierung.
     * Der Aufrufer sorgt vorher fuer bCityBaked und (bei Chunks) EnsureWorldPartition().
     */
    FCityMapSaveResult Save(UWorld* World, const FCityMapSaveRequest& Request);
```

- [ ] **Step 2: Implement `Save` by moving the SaveCityAsMap body**

In `WiesbadenCityMapWriter.cpp` add the editor includes (`UnrealEd`/`UEditorLoadingAndSavingUtils`, `Misc/ConfigCacheIni`, `Misc/FileHelper`, `Misc/PackageName`, `GIS/WiesbadenBuildSummary.h`, `WiesbadenReal.h`) and implement `Save` as the **behaviour-identical** extraction of `WorldBuilder.cpp:590–748` — with these substitutions: return `FCityMapSaveResult` (fill `Error`/`bSucceeded`/`SavedMapRef`) instead of writing `LastError`/`bAutoSaveSucceeded`; read `Request.AssetPath` (default `/Game/Maps/WiesbadenCity` when empty), `Request.bGenerateCityChunks`, `Request.bSetAsDefaultMap`; compute `bChunksInSeparatePackages` via `AllChunksInSeparatePackages(Request.ChunkPackageNames, Request.ChunkCount, Request.ChunksWithoutPackage)`; call the namespace `VerifyWorldPartitionSave` / `HasExternalActorPackages`. Keep the ini-write path exactly (`ApplyDefaultMapToIniText` + `FFileHelper` + disk verify) — do not touch `GConfig->Flush`. The `EnsureWorldPartition()` call stays on the actor (Step 3), NOT in `Save`.

- [ ] **Step 3: Rewrite the actor's `SaveCityAsMap` as a thin forwarder**

Replace the body of `AWiesbadenWorldBuilder::SaveCityAsMap` (560–753) with:

```cpp
void AWiesbadenWorldBuilder::SaveCityAsMap()
{
#if WITH_EDITOR
    bAutoSaveSucceeded = false;
    if (bBuildInProgress) { LastError = TEXT("Build laeuft noch - Speichern ignoriert."); return; }
    if (!bCityBaked)      { LastError = TEXT("Keine gebackene Stadt im Level - erst BuildCity ausfuehren."); return; }
    UWorld* World = GetWorld();
    if (!World)           { LastError = TEXT("Kein World verfuegbar."); return; }

    // World Partition VOR dem Speichern idempotent sicherstellen (actor-gekoppelt:
    // setzt SetIsSpatiallyLoaded auf diesem Actor + der Landscape).
    if (bGenerateCityChunks) { EnsureWorldPartition(); }

    // Chunk-Package-Fakten sammeln (der Writer bleibt frei von AWiesbadenCityChunk).
    FCityMapSaveRequest Req;
    Req.AssetPath = MapAssetPath.TrimStartAndEnd();
    Req.bGenerateCityChunks = bGenerateCityChunks;
    Req.bSetAsDefaultMap = true;
    Req.ChunkCount = CityChunks.Num();
    TSet<FString> Distinct;
    for (AWiesbadenCityChunk* Chunk : CityChunks)
    {
        UPackage* Pkg = Chunk ? Chunk->GetPackage() : nullptr;
        if (Pkg) { Distinct.Add(Pkg->GetName()); }
        else     { ++Req.ChunksWithoutPackage; }
    }
    Req.ChunkPackageNames = Distinct.Array();

    const FCityMapSaveResult R = WiesbadenCityMapWriter::Save(World, Req);
    LastError = R.Error;
    bAutoSaveSucceeded = R.bSucceeded;
#endif
}
```

- [ ] **Step 4: Build + run the full affected suites**

Build (`Result: Succeeded`), then run `WiesbadenReal.Core.MapBake`, `WiesbadenReal.GIS.CityMapWriter`, and `WiesbadenReal.GIS` (no regression). Expected: all `Result={Success}`.

- [ ] **Step 5: Behaviour-preservation integration check (manual editor, gated)**

`Save()` is editor side-effect heavy and has no unit test. Verify behaviour preservation by a real bake+save: run a WorldBuilder `BuildCity` with `bAutoSaveCityAsMap=true` (or call `SaveCityAsMap` on a baked level) and confirm (a) the `.umap` is written, (b) `DefaultEngine.ini` carries `GameDefaultMap`/`EditorStartupMap`, (c) `bAutoSaveSucceeded=true` / empty `LastError`, and — if chunks — the WP-verification log line appears. If a full bake is impractical in this session, note it as an open manual acceptance rather than committing an unverified `Save`.

- [ ] **Step 6: Commit**

```bash
git add Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.h Source/WiesbadenReal/GIS/WiesbadenCityMapWriter.cpp Source/WiesbadenReal/GIS/WiesbadenWorldBuilder.cpp
git commit -m "CityMapWriter: Save() aus SaveCityAsMap ziehen; Actor nur noch duenner Weiterleiter"
```

---

## Self-Review

**1. Spec coverage** (against the `CONTEXT.md` CityMapWriter entry): namespace module ✓ (Task 1); `AllChunksInSeparatePackages` new pure helper + regression test ✓ (Task 1); `VerifyWorldPartitionSave` relocated (already pure/tested) ✓ (Task 2); `ApplyDefaultMapToIni` — **already** a tested free function in `WiesbadenBuildSummary`, so NOT moved, only called ✓ (Task 3, noted); `Save()` + thin actor forwarder ✓ (Task 3); writer decoupled from `AWiesbadenCityChunk` ✓ (actor gathers facts, Task 3).

**2. Placeholder scan:** Task 3 Step 2 says "move the body of WorldBuilder.cpp:590–748 with these substitutions" rather than repeating ~130 lines — acceptable because it is a verbatim relocation of existing, cited code with an explicit substitution list, and inlining it here would only risk drift from the source of truth.

**3. Type consistency:** `FCityMapSaveRequest`/`FCityMapSaveResult`, `AllChunksInSeparatePackages(TArray<FString>, int32, int32)`, `VerifyWorldPartitionSave(bool,bool,bool,bool,FString&)`, `HasExternalActorPackages(FString)`, `Save(UWorld*, FCityMapSaveRequest)` — consistent across tasks. Actor forwarder fills exactly the request fields the header declares.

## Acceptance Criteria (whole feature)

1. `WiesbadenReal.GIS.CityMapWriter.ChunkPackages` green — including the historical "1984 chunks in one package → false" case.
2. `WiesbadenReal.Core.MapBake` green — its five `VerifyWorldPartitionSave` branches and the `ApplyDefaultMapToIniText` cases pass through the relocated/retained functions.
3. `AWiesbadenWorldBuilder::SaveCityAsMap` is a thin forwarder (guards + gather + `Save` + copy result); the save/ini/verify body lives in `WiesbadenCityMapWriter`.
4. The writer has no `#include`/reference to `AWiesbadenCityChunk`.
5. Behaviour preserved: a real bake+save produces the same `.umap`, the same `DefaultEngine.ini` wiring, and the same WP-verification outcome as before (manual editor check).
6. `WiesbadenWorldBuilder.h` shed the two static verification declarations — a small step toward the god-object's paring.

## Execution Handoff

**Blocked by the gate** — do not dispatch until `git status --short` for the five WIP files is empty. Once unblocked, two options:

1. **Subagent-Driven (recommended)** — a fresh subagent per task, review between tasks.
2. **Inline Execution** — tasks here with checkpoints.
