# Fahndungs-/Polizei-System — Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **STATUS: Plan zur Abnahme** — der Plan ist zusammen mit der Design-Spec `docs/superpowers/specs/2026-09-02-fahndung-polizei-design.md` (Status dort: **ENTWURF zur Abnahme**) abzunehmen. NICHTS wird umgesetzt, bevor beide Dokumente abgenommen sind. Nach Abnahme: Spec-Statuszeile auf abgenommen setzen, dann Teil A starten.

**Goal:** Ein GTA-artiges Fahndungssystem (Sternchen 0–5, Straßennetz-Verfolgung, Festnahme) gemäß Design-Spec `docs/superpowers/specs/2026-09-02-fahndung-polizei-design.md` — datenreiner, deterministischer Kern mit Unit-Tests, Actor-/KI-Glue darüber, HUD-Sternchen, Dev-Exec, Rauchtest-Nachweis.

**Architecture:** Vier Einheiten, Abhängigkeiten nur nach unten (Spec §Architektur): `FWiesbadenWantedState` (USTRUCT, DATENREIN, testbar wie `FWiesbadenWeatherSystem`/`FWiesbadenTrafficSimulation`) ← Crime-Glue in `AWiesbadenCar` → `UWiesbadenPoliceSubsystem` (UWorldSubsystem, Orchestrierung/Besitz) → `UWiesbadenPolicePursuitComponent` (je Streifenwagen, spiegelbildlich zum Heli-Autopiloten) → `AWiesbadenPoliceCar : AWiesbadenCar`. Die Verfolgung fährt **ausschließlich über die vorhandene Naht** `AWiesbadenCar::SetExternalControl(FWiesbadenCarControl)` — der Fahrzeug-Code bleibt frei von KI.

**Tech Stack:** UE 5.8 C++, Automation Framework (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`), `FRoadNetwork`-Spur-Graph, `SetExternalControl`-Naht, Rauchtest (`Tools/smoke_test.ps1`).

**Spec:** `docs/superpowers/specs/2026-09-02-fahndung-polizei-design.md` (Status: **ENTWURF zur Abnahme** — die Abnahme beider Dokumente ist Gate vor Teil A).

## Global Constraints

- **Nur ASCII im Code.** Neue Strings: „Strasse", „Gebaude", „Verfolger", „Festnahme" — keine Umlaute. (verbatim AGENTS.md)
- **Antworten und Code-Kommentare auf Deutsch.**
- **Unity-Build ist AUS** (`bUseUnity = false`) — je `.cpp` eine TU; keine gleichnamigen anonyme-Namespace-Helfer über Dateien hinweg (`FWiesbadenPolice.*` Namen einmalig halten).
- **Build:** `cmd //c "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\build_only.cmd"` (Bash startet NICHT im `cd`-Verzeichnis — absoluter Pfad, AGENTS.md-Fallstrick).
- **Tests:** `run_tests_only.cmd` via PowerShell `Start-Process` starten (ExecCmds-Quotierung), Ergebnis in `Saved\Logs\WiesbadenReal.log` gegen `grep -c "Result={Success}"` (aktuell **138**), Abschluss-Marker `**** TEST COMPLETE. EXIT CODE: 0 ****`.
- **138 bestehende Tests bleiben grün** — jede neue `IMPLEMENT_*_AUTOMATION_TEST` erhöht die Registrierungszahl.
- **Verfolger sind schwergewichtige Actors:** das Spiel ist spiel-strang-gebunden (~110–160 ms Bildzeit vor dem WP-Fix). Harte Kappung (max 6 Verfolger, Default-Scope der Spec) und strenge Entfern-Disziplin sind Teil des Plans, nicht Optional.
- **WP-Streaming-Fix: erledigt und gemessen (Stand 2026-09-02).** Der Fix (`AWiesbadenCityChunk::AnchorStreamingBounds`, `URegionAssetSpawnerComponent::AnchorEmptyInstanceComponents`) ist implementiert, die gebackene Karte wurde per `Tools/anchor_chunk_bounds.py` re-gebacken (2060 Chunk-Packages neu gesichert) und die Diagnose gemessen: **geladene Chunks jenseits 2 km: 602 → 9, jenseits 4 km: 0** (Log `LogWbStreaming`, 20:21). Damit ist der Spec-Perf-Vorbehalt ("nach dem WP-Streaming-Fix bauen") erfüllt; das manuelle Re-Bake-Gate aus Teil E2 ist bereits durch. Übrig bleibt nur das Festziehen der Perf-Schranke (E2).

---

## Datei-Struktur

- `Source/WiesbadenReal/Police/WiesbadenWantedState.h` / `.cpp` — **NEU.** Datenreiner Kern: `EWiesbadenCrime`, `FWiesbadenWantedTier`, `FWiesbadenWantedConfig`, `FWiesbadenWantedState` (Spec §Datenmodell). Keine Welt-Abhängigkeit, `USTRUCT` + `WIESBADENREAL_API`.
- `Source/WiesbadenReal/Tests/WantedStateTest.cpp` — **NEU.** Unit-Tests des Kerns (Spec §Test-Strategie).
- `Source/WiesbadenReal/Police/WiesbadenPoliceCar.h` / `.cpp` — **NEU.** `AWiesbadenPoliceCar : AWiesbadenCar` (Spec §Fahrbarer Actor).
- `Source/WiesbadenReal/Police/WiesbadenPolicePursuitComponent.h` / `.cpp` — **NEU.** Verfolgungs-KI über `SetExternalControl` (Spec §Verfolgungs-KI).
- `Source/WiesbadenReal/Police/WiesbadenPoliceSubsystem.h` / `.cpp` — **NEU.** `UWorldSubsystem`: Roster, Sicht, De-Eskalation, Festnahme, `WbWanted`-Exec-Brücke (Spec §Orchestrierung).
- `Source/WiesbadenReal/Core/WiesbadenDevActions.h` / `.cpp` — **MODIFY (Teil C).** Neuer Teleport-artiger Dev-Eingang: `WbWanted` landet als Exec am PlayerController (AGENTS.md: `-ExecCmds` erreicht nur die `ULocalPlayer::Exec`-Kette, nicht Subsysteme).
- `Source/WiesbadenReal/Core/WiesbadenPlayerController.cpp` — **MODIFY (Teil C).** `WbWanted <0-5>` + `WbWantedOff`.
- `Source/WiesbadenReal/Vehicles/WiesbadenCar.cpp` — **MODIFY (Teil D).** Hit-Hook meldet `PedestrianHit`/`PoliceRam` (dünner Glue, Spec §Crime-Detektion).
- `Source/WiesbadenReal/UI/WiesbadenVehicleHUD.cpp` — **MODIFY (Teil D).** Sternchen oben rechts (Spec §HUD).
- `Tools/smoke_test.ps1` — **MODIFY (Teil E).** 8. Prüfung: Fahndungs-Nachweis.

Der neue Ordner `Source/WiesbadenReal/Police/` trägt dem Spec-Graphen Rechnung (eigene Einheit, Abhängigkeiten nur nach unten); keine Zirkularbezüge — der Kern kennt niemanden.

---

## Teil A — Datenreiner Kern: `FWiesbadenWantedState` + Tests

Ziel: Die gesamte Fahndungs-**Regellogik** (Crimes → Heat → Sternchen → Verfall → Eskalationsstufe) ohne Welt, deterministisch, unit-getestet. Das ist der Spec-Zweig mit der höchsten Test-Abdeckung.

### Task A1: Enum, Config-Typen und Zustand (Header)

**Files:**
- Create: `Source/WiesbadenReal/Police/WiesbadenWantedState.h`
- Test: `Source/WiesbadenReal/Tests/WantedStateTest.cpp`

**Interfaces (Produkte):**

```cpp
// Vergehen (Spec §Datenmodell). Heat-Zuwaechse liegen in FWiesbadenWantedConfig.
enum class EWiesbadenCrime : uint8 { PedestrianHit, PoliceRam, RecklessNearPolice };

// Eskalationsleiter je Sternchen (Spec-Tabelle, tunebar).
struct FWiesbadenWantedTier
{
    int32   PursuerCount = 1;    // Verfolger bei dieser Stufe
    float   SpeedMultiplier = 1.0f;
    bool    bBoxIn = false;      // Einkesseln
    bool    bRoadblock = false;  // Sperren voraus
    bool    bHelicopter = false;
    float   RamAggression = 0.0f; // 0..1
};

// Uebergabe-Konfiguration (kein globaler CVar, keine Defaults im Kern).
struct FWiesbadenWantedConfig
{
    TArray<float> StarHeatThresholds;               // aufsteigend, 5 Eintraege
    TMap<EWiesbadenCrime, float> CrimeHeat;         // Heat-Zuwachs je Vergehen
    float HeatDecayPerSecond = 1.0f;
    float SeenGraceSeconds = 5.0f;                  // Verfall beginnt nach X s ohne Sicht
};

// Der Zustand. Rein, deterministisch, ohne Welt.
struct FWiesbadenWantedState
{
    int32  Stars = 0;                    // 0..5
    float  Heat = 0.0f;
    float  TimeSinceSeenSeconds = 0.0f;
    FVector LastKnownPlayerLocation = FVector::ZeroVector;

    void Initialize(const FWiesbadenWantedConfig& InConfig);
    void RegisterCrime(EWiesbadenCrime Crime, double WorldTimeSeconds = 0.0);
    void Tick(float Dt, bool bSeenByPolice, const FVector& PlayerLocation);
    bool IsWanted() const { return Stars > 0; }
    const FWiesbadenWantedTier* GetTier() const;   // nullptr bei Stars==0
};
```

**Design-Entscheidungen (aus dem Spec-Text abgeleitet):**

1. **Recompute statt Inkrement:** `Stars = größter Index i mit Heat >= StarHeatThresholds[i]` — nach JEDEM Heat-Change neu bestimmen (Spec-Zeile wörtlich). `RegisterCrime` erhöht Heat und recomputet; ein Verbrechen unterhalb der nächsten Schwelle ändert die Stufe nicht.
2. **Verfall nur nach Grace:** `Tick` setzt bei Sicht `TimeSinceSeenSeconds = 0` und `LastKnown = PlayerLocation`; ohne Sicht zählt die Zeit hoch, und erst NACH `SeenGraceSeconds` verfällt Heat mit `HeatDecayPerSecond` — `Heat = Max(0, Heat - Decay * Dt)`. Sterne fallen beim Verfall NICHT sofort, sondern erst wenn Heat unter die Schwelle rutscht (denselben Recompute-Pfad wie beim Aufbau — so blinkt das HUD später auf natürliche Weise beim Grenzfall).
3. **Eskalations-Tabelle gehört zum Subsystem, nicht zum Kern:** Der Kern kennt `FWiesbadenWantedTier`, aber die Instanz-Verwaltung (Tabelle als `UPROPERTY` am Subsystem, EditAnywhere) übergibt die aktive Stufe nach außen. Der Kern selbst hält KEINE Tune-Werte — nur die ihm übergebene Config. Damit bleibt er serialize-frei und testbar mit beliebigen Tabellen.

- [ ] **Step A1.1: Schreibe den Test zuerst** (`WantedStateTest.cpp`, Pfad `WiesbadenReal.Police.WantedState`, ProductFilter):

```cpp
// Kernaussagen (vollstaendig im Step implementieren):
//  1. RegisterCrime(PedestrianHit) mit CrimeHeat[PedestrianHit]=30,
//     Thresholds={25,60,100,150,220} -> Stars==1.
//  2. Zwei weitere Crimes (30+30=90) -> Stars==2 (groesster i mit 90>=Th[i]).
//  3. Tick mit bSeenByPolice=true haelt TimeSinceSeenSeconds auf 0 UND
//     LastKnownPlayerLocation == PlayerLocation.
//  4. Nach 3 s ohne Sicht (Grace 5) ist Heat UNVERAENDERT.
//  5. Nach insgesamt 8 s ohne Sicht ist Heat um 3*Decay gefallen (Grace 5).
//  6. Heat unter Schwelle -> Stern faellt (Recompute-Pfad beim Verfall).
//  7. Heat==0 -> Stars==0, IsWanted()==false.
//  8. GetTier() liefert bei Stars==0 nullptr, sonst die Stufe der
//     Test-Tabelle (Verfolgerzahl je Stufe pruefen).
//  9. Clamp: Heat bleibt >= 0; Stars <= 5 auch bei riesigem Crime-Heat.
```

- [ ] **Step A1.2: `bau_neuerpc.cmd`-Äquivalent** — Build schlägt fehl (`Police/WiesbadenWantedState.h` fehlt). Erwarteter roter Zustand.

- [ ] **Step A1.3: Implementiere Header + Cpp minimal** (nur was der Test fordert; `RegisterCrime` clamped Heat auf >= 0 nach Decay, `Stars` via Schleife über `StarHeatThresholds` abwärts).

- [ ] **Step A1.4: Testlauf** — `run_tests_only.cmd`, dann `grep -c "Result={Success}"` → **139** (138 + WantedState), 0 Fail. Gegen `TEST COMPLETE. EXIT CODE: 0` prüfen.

---

## Teil B — Verfolgungs-KI: `AWiesbadenPoliceCar` + `UWiesbadenPolicePursuitComponent`

Ziel: Ein fahrbarer Streifenwagen, der den Spieler über den Spur-Graph verfolgt — komplett über `SetExternalControl`, wie der Heli-Autopilot über die Heli-Naht.

### Task B1: `AWiesbadenPoliceCar`

**Files:**
- Create: `Source/WiesbadenReal/Police/WiesbadenPoliceCar.h` / `.cpp`

**Interfaces:**
- Erbt `AWiesbadenCar` (bereits `SetExternalControl`-fähig, Kamera-Rig inklusive).
- Konstruktor: Blaulicht-Variante der vorhandenen `UWiesbadenCarLightsComponent` (Blink-Rate hoch), Sirene über `UWiesbadenCarAudioComponent` (Sirene-Sound, Fallback still wenn Asset fehlt — wie die Heli-Fallbacks).
- `UFUNCTION(BlueprintCallable) void SetLightsActive(bool bActive)` — Blaulicht/Sirene an/aus (Verfolger ohne Fahndung fahren unauffällig).
- KEINE Physik-Änderungen, KEINE neuen Kollisionskanäle — der Wagen fährt exakt mit der erprobten `FWiesbadenVehiclePhysics`.

- [ ] **Step B1.1: Implementierung** (kein eigener Test — der Wagen ist Glue; Verhalten wird über den Rauchtest in Teil E nachgewiesen).

### Task B2: `UWiesbadenPolicePursuitComponent` — datenreine Wegwahl + Exec-Steuerung

**Files:**
- Create: `Source/WiesbadenReal/Police/WiesbadenPolicePursuitComponent.h` / `.cpp`

**Interfaces (Produkte):**

```cpp
// DATENREIN (namespace, testbar): Kreuzungsentscheidung.
// Waehlt unter den Nachfolgern die Spur, deren ENDPUNKT dem Ziel am
// naechsten liegt. Tie = erste (deterministisch). Die Hysterese (Spec
// Risiken #2) liegt NICHT hier, sondern im Component (siehe unten).
WIESBADENREAL_API int32 ChoosePursuitLane(
    const FRoadNetwork& Network, int32 CurrentLaneId,
    const FVector& TargetLocationCm);

// UActorComponent am Streifenwagen. Tick -> FWiesbadenCarControl ->
// SetExternalControl. Spaegtest hier endet jeder datenreine Anteil.
UCLASS()
class UWiesbadenPolicePursuitComponent : public UActorComponent
{
    // Konfiguration (EditAnywhere, vom Subsystem je Spawn gesetzt):
    float  PursuitSpeedKmh = 90.0f;    // gekappt per Tier.SpeedMultiplier
    float  RamAggression = 0.0f;       // 0..1 (Nahbereich: Beeline + Vollgas)
    float  RethinkIntervalSeconds = 1.0f; // Hysterese: Zielspur-Neuwahl nur alle N s

    // Zustand:
    int32  CurrentLaneId = INDEX_NONE;
    int32  TargetLaneId = INDEX_NONE;
    double LastRethinkTime = 0.0;
    bool   bDirectRam = false;         // Nahbereich/Beeline aktiv

    // API:
    void BeginPursuit(AWiesbadenCar* InCar, const FRoadNetwork* InNetwork);
    void UpdatePursuit(const FVector& TargetLocationCm, float SpeedMultiplier, float RamAggression);
    void EndPursuit();
    // Meldung ans Subsystem (Sichtlinie + Distanz, Spec Punkt 5):
    bool HasLineOfSight() const;
    float GetDistanceToTargetCm() const;
};
```

**Verfolgungs-Verfahren (Spec §Verfolgungs-KI, 1:1):**

1. **Spuren bestimmen:** eigene Spur = nächste `FRoadLane`-Centerline zum eigenen Standort (Lineare Suche über `Network.Lanes` ist bei ~2.000 Spuren je Tick zu teuer — **Kandidat je Fahrzeug cachen** und nur bei Neu-Spawn/Track-Verlust neu suchen; die Kandidatensuche selbst läuft im `UpdatePursuit`-Rethink-Intervall, nicht je Tick). Spieler-Spur analog, aber nur im Rethink-Takt.
2. **Kreuzungsentscheid:** an jeder Kreuzung `ChoosePursuitLane` unter `Network.GetSuccessors(CurrentLaneId)` — gierig auf den Endpunkt, der dem Spieler am nächsten liegt. **Pendel-Schutz:** die Entscheidung wird im `RethinkIntervalSeconds`-Takt neu gefasst, nicht je Tick (Spec-Risiko #2: „Neuplanung nur alle N Sekunden").
3. **Spurfolge:** Zielpunkt = nächstliegender Centerline-Punkt voraus + Vorhalt (~8 m); `Steering` = seitlicher Fehler, normiert −1..1 (Vorzeichen: rechts = +, siehe `FWiesbadenCarControl`); `Throttle` = proportional zur freien Strecke, gekappt auf `PursuitSpeedKmh * SpeedMultiplier` über die Ist-Geschwindigkeit (`GetSpeedKmh()` — NICHT `GetVelocity()`, das hier für Kinematik-Nahfahrzeuge greift, aber der Wagen ist ein echter `AWiesbadenCar` mit Physik-Integration und liefert korrekte Werte; der AGENTS.md-Vorbehalt gilt nur für den kinematischen Heli).
4. **Nahbereich (Beeline):** wenn Sichtlinie UND Distanz < `RamRangeCm` (Default 12 m) → `bDirectRam = true`, Ziel = Spielerposition direkt, Throttle = 1, Intensität aus `RamAggression`. Bei Sichtlinien-Verlust sofort zurück zur Spurfolge (kein „Nachschieben" in Wände — der Sweep des Autos bremst ohnehin, siehe Punkt 3).
5. **Meldung:** `HasLineOfSight` (Raycast — im Component, nicht im Kern; Kanal wie die Bodenprobe des Fahrzeugs) + `GetDistanceToTargetCm` fließen dem Subsystem zu (Spec Orchestrierung Punkt 1 und 5).

**Determinismus-Hinweis:** `ChoosePursuitLane` ist reine Funktion → eigener Automation-Test mit handgebautem Mini-Netz (2 Kreuzungen, `LaneSuccessors` per Index wie in `TrafficSimulationTest.cpp`).

- [ ] **Step B2.1: Test zuerst** (`WiesbadenReal.Police.PursuitLaneChoice`, ProductFilter): Mini-Netz mit 2 Folgespuren, Ziel links → wählt linke; Ziel geradeaus → gerade; restricted-Nachfolger wird nie gewählt (siehe `GetSuccessors`-Filter); Tie → erste.

- [ ] **Step B2.2: Implementiere `ChoosePursuitLane` + Component** (Component-Tick ruft nur `UpdatePursuit`-Logik und `SetExternalControl` — kein weiterer datenreiner Anteil).

- [ ] **Step B2.3: Testlauf** → **140** grün.

---

## Teil C — Orchestrierung: `UWiesbadenPoliceSubsystem` + `WbWanted`-Exec

Ziel: Besitz und Tick-Orchestrierung (Spec §Orchestrierung), Dev-Zugriff über die PlayerController-Exec-Kette.

### Task C1: Subsystem

**Files:**
- Create: `Source/WiesbadenReal/Police/WiesbadenPoliceSubsystem.h` / `.cpp`

**Interfaces:**

```cpp
UCLASS()
class UWiesbadenPoliceSubsystem : public UWorldSubsystem
{
    // -- Tunebar (Spec Eskalationsleiter, EditAnywhere) --
    UPROPERTY(EditAnywhere) TArray<FWiesbadenWantedTier> Tiers; // 5 Eintraege, Defaults = Spec-Tabelle
    UPROPERTY(EditAnywhere) FWiesbadenWantedConfig Config;      // Thresholds/CrimeHeat/Decay/Grace
    UPROPERTY(EditAnywhere) float PursuitMaxCount = 6;          // harte Kappung (Spec Performance)
    UPROPERTY(EditAnywhere) float SightRadiusCm = 60000.0f;     // 600 m
    UPROPERTY(EditAnywhere) float ArrestRadiusCm = 400.0f;      // Umzingelungs-Radius
    UPROPERTY(EditAnywhere) int32 ArrestMinCops = 2;
    UPROPERTY(EditAnywhere) float ArrestSpeedKmh = 8.0f;        // "nahezu Stillstand"
    UPROPERTY(EditAnywhere) float ArrestSeconds = 3.0f;

    // -- Zustand --
    FWiesbadenWantedState Wanted;             // der datenreine Kern
    TArray<AWiesbadenPoliceCar*> Pursuers;    // roster
    AWiesbadenHelicopter* PoliceHelicopter = nullptr;

    // -- API (BlueprintCallable fuer HUD/Diagnose) --
    const FWiesbadenWantedState& GetWantedState() const;
    void RegisterCrimeFromGameplay(EWiesbadenCrime Crime, const FVector& Location);
    void ForceWantedLevel(int32 Stars0to5);   // Dev/Tests

    virtual bool ShouldCreateSubsystem(UObject* Outer) const override; // nur Spiel-Welten
    virtual void Tick(float DeltaTime) override;   // via FWorldDelegates wie CitySubsystem
};
```

**Tick-Ablauf (Spec 1–6, wörtlich):**

1. **Sicht:** `bSeenByPolice = any Pursuer.HasLineOfSight() && dist < SightRadiusCm`.
2. `Wanted.Tick(DeltaTime, bSeenByPolice, PlayerLocation)`.
3. **Roster angleichen:** Soll = `Tiers[Stars-1].PursuerCount`, gekappt auf `PursuitMaxCount`. Fehlende: Spawn an Straßen-Spawnpunkt **außerhalb der Spieler-Sicht, aber in Reichweite** (Kandidaten: Kreuzungen aus `FRoadNetwork::Intersections`, Filter Distanz-Fenster [100 m, 400 m] + Line-of-Sight-Check vom Spieler). Zu viele (Stufe gefallen): fernste entfernen (`EndPursuit` + `Destroy`).
4. **Heli ab 4★** (`Tiers[i].bHelicopter`): einmalig `AWiesbadenHelicopter` spawnen + `UWiesbadenHelicopterAutopilot::FlyTo` auf `LastKnownPlayerLocation` (AGENTS.md: der Autopilot braucht `GetVelocityMetersPerSecond()`-fären Code NICHT — er ist fertig; nur das Ziel je Rethink-Takt aktualisieren).
5. **Festnahme:** `>= ArrestMinCops` in `ArrestRadiusCm` UND Spielertempo < `ArrestSpeedKmh` UND Dauer > `ArrestSeconds` → Arrest (v1: Screen-Fade über HUD-Kanal + Spieler-Reset auf Teleport-Spawn + `ForceWantedLevel(0)`).
6. **De-Eskalation:** `Heat == 0 && !IsWanted()` → alle Verfolger + Heli entfernen, `EndPursuit`.

**Netz-Zugriff:** `FRoadNetwork` über `UWiesbadenGameInstance::GetCityData()->RoadNetwork` (verifiziert: `GetCityData()` existiert, liefert `TSharedPtr<FWiesbadenCityData>`; im gebackenen Pfad hält der **WorldBuilder** das Netz — siehe AGENTS.md MoveTemp-Falle. Der Subsystem-Zugriff läuft deshalb über dieselbe Quelle wie die HUD-Suche: `FindRoadNetwork()`-Muster von `WiesbadenVehicleHUD` kopieren, das beide Fälle abdeckt).

### Task C2: `WbWanted`-Exec

**Files:**
- Modify: `Source/WiesbadenReal/Core/WiesbadenPlayerController.cpp` (+ `.h` falls nötig)

**Interfaces:** `WbWanted <0-5>` erzwingt die Stufe via `ForceWantedLevel` (Suchpfad wie die übrigen Wb-Execs: `UWorld::GetSubsystem<UWiesbadenPoliceSubsystem>(GetWorld())`), `WbWanted 0` räumt ab. AGENTS.md-Fallstricke beachten: `-ExecCmds` trennt per **Komma**, die Execs liegen am **PlayerController** (nicht Subsystem — würde nie feuern).

- [ ] **Step C2.1: Implementierung** (kein eigener Test; das Verhalten läuft in den Rauchtest ein).

- [ ] **Step C2.2: Editor-Rauchprobe (manuell):** `-ExecCmds="WbWanted 3"` → Log-Zeile je Spawn (`WbPolice: Verfolger gespawnt ... Distanz X m`), Sternchen im HUD, bei `WbWanted 0` Räumung im Log.

---

## Teil D — Crime-Detektion + HUD

### Task D1: Hit-Glue in `AWiesbadenCar`

**Files:**
- Modify: `Source/WiesbadenReal/Vehicles/WiesbadenCar.cpp` (Sweep-/Hit-Hook)

**Verfahren (Spec §Crime-Detektion — dünn):** beim Fahrzeug-Hit: `OtherActor` ist `AWiesbadenFootPawn` → `RegisterCrimeFromGameplay(PedestrianHit, ...)`, ist `AWiesbadenPoliceCar` → `PoliceRam`. Das Auto kennt das Subsystem nur als `UWorld::GetSubsystem`-Lookup beim Ereignis (kein UPROPERTY, keine Include-Zirkularität — `WiesbadenWantedState.h` reicht als Include). `RecklessNearPolice` (Tempo × Nähe, Spec Punkt 2) prüft das Subsystem je Tick selbst — das Auto bleibt für dieses Vergehen unangetastet.

- [ ] **Step D1.1: Implementierung + Gegenprobe im Rauchtest** (Teil E nutzt `PoliceRam` als auslösendes Vergehen — der Rauchtest rammt bewusst einen Streifenwagen).

### Task D2: HUD-Sternchen

**Files:**
- Modify: `Source/WiesbadenReal/UI/WiesbadenVehicleHUD.cpp`

**Verfahren:** oben rechts 5 Sterne (gefüllt = `Stars`, leer = Rest), blinkend solange Heat verfällt (Grace läuft). Quelle: `UWorld::GetSubsystem<UWiesbadenPoliceSubsystem>` je Draw (das HUD hat kein Tick-Budget-Problem: eine `GetSubsystem`-Abfrage je Bild ist billig; **keine** Additional-Caches — siehe AGENTS.md-Fallstrick „HUD-Suchläufe nicht mehr je Bild": einmal je Draw suchen ist OK, Caches nur bei Nachweis eines Problems). Sternchen-Daten laufen über die BlueprintPure-Getter des Subsystems — für das Fuß-HUD (`WiesbadenFootPawn`-HUD) dieselbe Quelle.

- [ ] **Step D2.1: Implementierung** (Sichtprobe im Screenshot-Rauchtest, Teil E).

---

## Teil E — Rauchtest-Nachweis + Perf-Schranke

> Der ursprüngliche Re-Bake-Gate-Schritt ist **erledigt** (Stand 2026-09-02): Fix implementiert, Karte re-gebacken (`Tools/anchor_chunk_bounds.py`, 2060 Packages), Kennzahl 602 → 9 jenseits 2 km / 0 jenseits 4 km. Siehe Global Constraints.

### Task E1: Rauchtest 8. Prüfung „Fahndung"

**Files:**
- Modify: `Tools/smoke_test.ps1`

**Verfahren:** dritte kurze Editorsitzung (Helper `Invoke-Session` existiert; Fahrzeug und Heli teilen die Besitzung — die Polizei-Sitzung ist bewusst EIGEN, weil die Verfolger den Spieler-Pawn nie besetzen). Ablauf: `WbWanted 2` → 15 s fahren lassen → Log auswerten:
- `Verfolger gespawnt` >= 1
- Distanz eines Verfolgers zum Spieler fällt über die Zeit (Log-Messpunkte, wie beim Autopilot-Nachweis `WbDev Autopilot t=N: Abstand X m`)
- `WbWanted 0` → `Verfolger entfernt` im Log
- **Perf-Schranke:** die 8-s-Diagnoseblock-Zeit (Spiel-Strang) darf mit Verfolgung NICHT über die Schranke des Perf-Regression-Checks steigen — bei Verstoß Kappung senken, nicht Schranke lockern (Spec Performance: ehrlich bleiben).

- [ ] **Step E1.1: Prüfung implementieren + Rauchtest grün** (Exit 0, 8 Pruefungen).

### Task E2: Perf-Schranke festziehen (manuell, nach allen Teilen)

> Re-Bake ist bereits durch (Global Constraints). Nur noch Schritt 3 nötig:

1. ~~`cmd //c rebuild_stadt.cmd` (oder `Tools/rebuild_city.py`-Muster) — 40–64 min.~~ (erledigt)
2. ~~WbCompDiag-Kennzahl: `Streaming-Diagnose: ... jenseits 2 km` muss von ~602 gegen 0 fallen (WP-Fix-Wirkung, siehe Commit 2201928).~~ (erledigt: 602 → 9, 4 km: 0)
3. Jetzt den Perf-Regression-Schwellwert (Rauchtest-Parameter `$MaxSpielMs`, aktuell 220) anhand eines frischen Diagnoseblocks nach dem WP-Fix enger ziehen, um den WP-Fix-Gewinn festzunageln (AGENTS.md-Vorsatz aus Commit 69e0136).

- [ ] **Step E2.1: Neue Schranke `$MaxSpielMs` gemessen und gesetzt** (AGENTS.md-Eintrag).

---

## Testabdeckung (Ziel)

| Bereich | Test | Art |
|---|---|---|
| Heat/Stars/Verfall/Grace/Clamp | `WiesbadenReal.Police.WantedState` | Unit (datenrein) |
| Eskalations-Tabelle | im WantedState-Test (GetTier je Stufe) | Unit |
| Kreuzungs-Gier + restricted-Filter | `WiesbadenReal.Police.PursuitLaneChoice` | Unit (Mini-Netz) |
| Roster/Spawn/Entfernen | Rauchtest-Log (`Verfolger gespawnt/entfernt`) | Rauchtest |
| Distanz-Verringerung | Rauchtest-Log (Messpunkte wie Autopilot) | Rauchtest |
| Festnahme | manuell (WbWanted 5 + Stillstand) | manuell |
| HUD-Sternchen | Screenshot im Rauchtest (CopyFromScreen) | Rauchtest |
| Perf | 8-s-Diagnoseblock + Schranke | Rauchtest |

## Reihenfolge und Gates

```
A (Kern + Tests)          -> 139 Tests gruen
B (KI + Wegwahl-Test)     -> 140 Tests gruen
C (Subsystem + WbWanted)  -> Editor-Rauchprobe (Log)
D (Crime-Glue + HUD)      -> Editor-Rauchprobe (Screenshots)
E (Rauchtest + Schranke)  -> Rauchtest 8 Pruefungen, dann $MaxSpielMs festziehen
```

Jeder Teil ist für sich commit-fähig; die Re-Bake-Abhängigkeit betrifft nur den finalen Perf-Nachweis, nicht die Funktionalität.

## Risiken und Gegenmaßnahmen (aus der Spec, mit Plan-Anschluss)

1. **Perf (Spec #1):** Kappung `PursuitMaxCount=6` + strenge Entfern-Disziplin; Rauchtest-Perf-Schranke als Fall-Sicherheit. Der WP-Fix ist durch (602 → 9 jenseits 2 km); die neue Schranke wird in E2 gemessen.
2. **Pfad-Pendeln (Spec #2):** Rethink-Intervall 1 s; falls unzureichend, Zielspur nur bei Distanzverschlechterung > 20 % wechseln (nachmessen, nicht vorbauen).
3. **Festnahme-UX (Spec #3):** v1 Fade + Respawn; der Reset-Pfad (`WbResetVehicle`-Mechanik + Teleport-Spawn) existiert bereits in `FWiesbadenDevActions`.
4. **Spieler zu Fuß (Spec #4):** v1 kein Fuß-Cops; Cops verlieren Sicht → Heat verfällt → Flucht gilt als gelungen (Spec-Verhalten, kein Extra-Code).
5. **Determinismus (Spec #5):** Kern und `ChoosePursuitLane` sind rein; Raycasts/Spawns liegen im Component/Subsystem. Kein `FMath::RandRange` im Kern — Fluchtpfad-Verfall ist rein zeitsynchron.
