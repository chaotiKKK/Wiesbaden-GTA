# WiesbadenReal — Domain glossary

Names for the good seams in the codebase. Architecture reviews and plans use these
terms for the domain; the design vocabulary (module, interface, depth, seam,
adapter, leverage, locality) comes from the `codebase-design` skill.

## Vehicles

- **Käfer 1302** — the player car: a 1969 VW Beetle, 1.5 l boxer, 44 PS @ 4000 rpm,
  102 Nm @ 2600 rpm, four gears `{3.80, 2.06, 1.32, 0.89}`, final drive 4.375,
  820 kg, rear-wheel drive. The canonical vehicle spec the game ships.

- **Powertrain-Spec** (`FWiesbadenPowertrainSpec`) — the single source of truth for
  the Käfer's drivetrain: the engine **torque curve (Nm over rpm)** as the physical
  truth, gearbox ratios, final drive, wheel radius, mass, aero drag. A pure value
  type with a `Kaefer1302()` factory. Two adapters consume it — the kinematic model
  and the Chaos config — which is what makes it a real seam rather than a
  hypothetical one. Both engines were previously modelled differently (kinematic:
  32 kW power; Chaos: 102 Nm curve) and drifted apart; the spec unifies them on the
  torque curve, so the kinematic model evaluates the same curve Chaos does.
  Deliberately *drivetrain only*: steering, the bicycle model, and fuel stay
  vehicle-specific.

- **Kinematic drive model** (`FWiesbadenVehiclePhysics`) — the Beetle's deep, pure,
  engine-free longitudinal + lateral model (`Tick(Input, dt, Out)` plus pure
  statics). Integrates speed/yaw; the actor integrates position and ground contact.
  The bar every other vehicle-math module should clear for testability.

- **Chaos car** (`AWiesbadenChaosCar`) — the same Käfer driven by UE Chaos vehicle
  physics (real wheels, suspension, tire model). Drives through its `EngineSetup`/
  `TransmissionSetup`, configured in its constructor. Its PhysicsAsset (belly
  collision) is a known, separately-owned problem and is off-limits to this work.

- **External-control seam** (`IWiesbadenVehicleControl` / `FWiesbadenCarControl`) —
  a driver (AI, test, replay) pushes finished inputs through the same drive path a
  keyboard would use. Real seam: two implementers (kinematic + Chaos), several
  callers (harness, auto-drive AI, seam test).

## Streaming & diagnostics

- **Durchfall-Test / Durchfall-Wächter** — the regression that moves a pawn fast
  through the World-Partition-streamed city and checks that loaded ground is always
  under it (no fall through an unloaded cell). Runs after every city build. Two
  modes: the **trace mode** (default) teleports the pawn a deterministic +X line
  and traces downward for gaps — this is the reliable, reproducible signal; the
  **car mode** (`-WbChaosCar`) drives the real car and measures actual body drop,
  but is `UNGÜLTIG` until the Chaos PhysicsAsset (belly collision) is fixed — a
  tabu-owned limitation, NOT resolvable by driving the kinematic car (that path is
  non-deterministic and does not move headless).

- **Fall-Through-Monitor** (`FWbFallThroughMonitor`) — the deepened, pure module
  the Durchfall logic wants to become: it accumulates void runs / worst gap
  (trace mode) or body drop (car mode) and produces an `FWbFallReport`
  (`Verdict{Passed|FellThrough|Invalid}`). Ground queries sit behind the
  **`IWbGroundProbe`** seam — a world-trace adapter in-game, a synthetic adapter in
  tests. This makes "scripted 40 m gap → FellThrough" and the `UNGÜLTIG`/`Invalid`
  guard assertable without the 6-minute runtime run. Emerges by paring the
  [[Durchfall-Test]] arm out of `UWiesbadenCitySubsystem` (the god-object).

- **Frame-Profiler** (`FWbFrameProfiler`) — the deepened value type for the
  windowed-performance concern in `UWiesbadenCitySubsystem`: ~12 accumulator fields
  (frame mean/worst/spike/hitch + per-strand sim/game/render/gpu times) + the
  4 s-warmup/15 s-window timing + the (currently verbatim-duplicated) reset. Fed by
  focused `AddStrands`/`AddSubsystemTime`/`SampleFrame` calls at their natural sites;
  emits an `FWbFrameReport` the three readers (two log sites + `BuildHealthReport`)
  share. Stays pure — the subsystem reads the engine globals and passes them in — so
  "30×20 ms + 1×120 ms → SpikeCount 1, worst 120" is a unit test. Excludes the perf
  **snapshot** (component counts), a separate concern. Another tool pared out of the
  [[Durchfall-Test]] god-object; unblocked (does not touch the WorldBuilder files).

## City generation & persistence

- **WorldBuilder** (`AWiesbadenWorldBuilder`) — the build-orchestration god-object:
  ~55 editor properties, the async build lifecycle, mesh application, material
  policy, landscape, World-Partition chunk placement, and map persistence, plus an
  840-line header that leaks all 11 concrete generator includes + Transient anchors.
  Under active development (a pickup-spot generator is being added) — treat as a
  shared, live file.

- **CityMapWriter** (`namespace WiesbadenCityMapWriter`) — the persistence half,
  paring `SaveCityAsMap` out of the [[WorldBuilder]] god-object: a
  `Save(UWorld*, FCityMapSaveRequest) -> FCityMapSaveResult` that hides `SaveMap`,
  the `DefaultEngine.ini` surgery, and the World-Partition save verification. Three
  pure helpers carry the testable logic — `VerifyWorldPartitionSave` and
  `ApplyDefaultMapToIni` (both already pure) and a new `AllChunksInSeparatePackages`
  (the check that once shipped a blank city when 1984 chunks shared one 531 MB
  package). Mirrors the `WiesbadenChunkStaticMeshBaker` namespace pattern. Design
  crystallized; **implementation gated** on the live WorldBuilder/pipeline WIP.
