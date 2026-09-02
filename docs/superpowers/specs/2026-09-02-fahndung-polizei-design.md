# Fahndungs-/Polizei-System - Design

Status: ENTWURF zur Abnahme. Nichts wird gebaut, bevor dieses Dokument abgenommen ist.
Datum: 2026-09-02

## Überblick

Ein GTA-artiges Fahndungssystem: Vergehen erhöhen ein Sternchen-Level (0-5), das
Polizeiautos als Verfolger spawnt, die den Spieler über das Straßennetz jagen,
mit steigender Eskalation. Der Spieler entkommt, indem er der Sicht der Polizei
lange genug entgeht.

Gewählte Scope-Defaults (überschreibbar):
- **Crime-Trigger:** fahrzeugbezogen (Fußgänger anfahren, Streifenwagen rammen,
  rücksichtslos nah an Polizei fahren). KEIN Waffensystem.
- **Auflösung:** Rammen/Blockieren + Festnahme, wenn der Spieler zu lange von
  Cops umringt und (nahezu) im Stillstand ist. Keine schießende Polizei.
- **Verfolgungs-KI:** Straßennetz-Verfolgung über den Spur-Graph.

### Ziele
- Sternchen 0-5 mit nachvollziehbarer Auf-/Abstufung.
- Polizeiautos verfolgen den Spieler realistisch über Straßen (bleiben nicht an
  Gebäuden hängen).
- Datengetriebene Eskalationsleiter (tunebar).
- Der datenreine Kern ist deterministisch und unit-getestet (Projekt-Muster).

### Nicht-Ziele (v1)
- Kein Waffen-/Schuss-/Gesundheitssystem (eigener späterer Zyklus).
- Keine Fuß-Polizei-KI (Cops bleiben im Auto; Verfolgung zu Fuß ist Follow-up).
- Keine Pay'n'Spray-/Versteck-Mechanik (nur Sicht-Flucht in v1).

## Architektur

Vier klar getrennte Einheiten, Abhängigkeiten nur nach unten:

```
UWiesbadenPoliceSubsystem (UWorldSubsystem)      <- Orchestrierung, Besitz
  |  besitzt
  v
FWiesbadenWantedState (USTRUCT, DATENREIN)       <- Entscheidungslogik, testbar
  ^  meldet Crimes
  |
Crime-Detektion (dünner Glue in AWiesbadenCar)   <- Ereignisse -> RegisterCrime
  
UWiesbadenPolicePursuitComponent (UActorComp)    <- KI je Streifenwagen
  |  treibt
  v
AWiesbadenPoliceCar : AWiesbadenCar              <- fahrbarer Actor
  ^  fährt über
  |
FWiesbadenCarControl / SetExternalControl        <- vorhandene Naht
```

Warum diese Trennung: die Fahndungs-**Regeln** (Sternchen/Heat/Eskalation) sind
datenrein und ohne Welt testbar - wie `FWiesbadenWeatherSystem` und
`FWiesbadenTrafficSimulation`. Actor-/Spawn-/KI-Glue hängt davon ab, nie umgekehrt.

### Kontext-Fakten, die das Design festlegen
- Verkehrsfahrzeuge sind **ISM-Instanzen, keine fahrbaren Actors** -> Polizei-
  Verfolger sind **eigene, schwergewichtige Auto-Actors** in begrenzter Zahl,
  nicht der Schwarm.
- Das **Straßennetz `FRoadNetwork`** (Spur-Graph, `GetLane`, Lane-Verbindungen)
  navigiert der Verkehr bereits - dieselbe Navigation trägt die Verfolgung.
- Der **Helikopter-Autopilot** existiert -> Polizeiheli bei hoher Fahndung fällt
  praktisch ab (spawnt einen Heli mit `UWiesbadenHelicopterAutopilot`-Ziel Spieler).

## Datenmodell

### `EWiesbadenCrime`
```
PedestrianHit       // Fußgänger angefahren
PoliceRam           // Streifenwagen gerammt/angefahren
RecklessNearPolice  // hohes Tempo dicht an Polizei
```
Jedes Vergehen hat einen **Heat-Zuwachs** (datengetriebene Tabelle, kein
hartkodierter Wert im Code).

### `FWiesbadenWantedState` (USTRUCT, datenrein)
Felder:
- `int32 Stars` (0..5)
- `float Heat` (kontinuierlicher Akkumulator)
- `float TimeSinceSeenSeconds`
- `FVector LastKnownPlayerLocation`

Konfiguration (übergeben, nicht global):
- `TArray<float> StarHeatThresholds` (Heat-Schwellen je Sternchen)
- `TMap<EWiesbadenCrime,float> CrimeHeat`
- `float HeatDecayPerSecond`, `float SeenGraceSeconds` (erst nach X s ohne Sicht
  beginnt der Verfall)

Methoden (rein, deterministisch):
- `void RegisterCrime(EWiesbadenCrime)` - addiert Heat, recomputet Stars.
- `void Tick(float Dt, bool bSeenByPolice, FVector PlayerLoc)` - bei Sicht:
  `TimeSinceSeen=0`, `LastKnown=PlayerLoc`; sonst `TimeSinceSeen+=Dt` und nach
  `SeenGraceSeconds` verfällt Heat mit `HeatDecayPerSecond`. Stars = größter
  Index mit `Heat >= StarHeatThresholds[i]`.
- `bool IsWanted() const { return Stars > 0; }`

Determinismus = unit-testbar (C++-Test + optional Node-Port), Kern der Testabdeckung.

### Eskalationsleiter (datengetrieben, je Sternchen)
Eine Tabelle `FWiesbadenWantedTier[]` (UPROPERTY, tunebar):
| ★ | Verfolger | Verhalten |
|---|---|---|
| 1 | 1 | folgen, langsam nachrücken |
| 2 | 2-3 | folgen + gelegentlich rammen |
| 3 | 3-4 | einkesseln versuchen, schneller |
| 4 | 4-5 + **Heli** | Straßensperren an Kreuzungen voraus, Heli über dem Spieler |
| 5 | 6 + Heli | maximal aggressiv, dauerhaftes Rammen |

Felder je Stufe: `PursuerCount`, `SpeedMultiplier`, `bBoxIn`, `bRoadblock`,
`bHelicopter`, `RamAggression`.

## Orchestrierung: `UWiesbadenPoliceSubsystem`

Ein `UWorldSubsystem` (eine Instanz je Welt), besitzt den `FWiesbadenWantedState`
und die aktive Verfolger-Liste. Je Tick:
1. **Sicht bestimmen:** `bSeenByPolice` = mindestens ein Verfolger hat Sichtlinie
   (Raycast, gleicher Kanal wie Fahrzeug-Bodenprobe) und ist < SichtRadius.
2. **WantedState.Tick(...)** aufrufen.
3. **Verfolger-Roster an die Stufe angleichen:** fehlen Verfolger, an
   Straßen-Spawnpunkten außerhalb der Sicht des Spielers (aber in Reichweite)
   `AWiesbadenPoliceCar` spawnen und Ziel = Spieler setzen; sind zu viele (Stufe
   gesunken/Flucht), überzählige entfernen.
4. **Heli** ab 4★ spawnen/halten (Autopilot verfolgt `LastKnownPlayerLocation`).
5. **Festnahme:** ist der Spieler > T Sekunden von >=2 Cops in < R umringt UND
   nahezu im Stillstand -> Festnahme (Bildschirm + Reset des Zustands). Minimal
   in v1 (Fade + Respawn, Fahndung 0).
6. **De-Eskalation:** `Heat == 0` -> Sternchen 0, alle Verfolger + Heli entfernen.

Dev-Exec `WbWanted <0-5>` erzwingt eine Stufe (Test/Debug, wie die übrigen WbDev-
Befehle).

## Verfolgungs-KI: `UWiesbadenPolicePursuitComponent`

UActorComponent am Streifenwagen, **spiegelbildlich zum Heli-Autopiloten**: treibt
den `AWiesbadenPoliceCar` ausschließlich über `SetExternalControl(FWiesbadenCarControl)`
- der Fahrzeug-Code bleibt frei von KI.

Navigation (Straßennetz, gieriger Pfad zum bewegten Ziel):
1. Nächste eigene Spur und nächste Spieler-Spur im `FRoadNetwork` bestimmen.
2. **An jeder Kreuzung** die Folgespur wählen, deren Richtung die Distanz zum
   Spieler am meisten verringert (mit leichter Hysterese gegen Pendeln).
3. Entlang der aktuellen Spur-Centerline zum nächsten Knoten steuern: `Steering`
   Richtung Spur-/Zielpunkt, `Throttle` proportional zur freien Strecke
   (Kollisions-Sweep des Autos bremst vor Hindernissen), gekappt per
   `SpeedMultiplier` der Stufe.
4. **Nahbereich / Sichtlinie:** direktes Rammen - Beeline-Steuerung auf den
   Spieler + Vollgas, Intensität aus `RamAggression`.
5. Meldet dem Subsystem Sichtlinie/Distanz (für `bSeenByPolice` und Festnahme).

Wiederverwendung: dieselbe Steuer-Naht wie `WbDrive`/Test-Harness; dieselbe
Komponenten-KI-Struktur wie der Autopilot.

## Fahrbarer Actor: `AWiesbadenPoliceCar`

Ableitung von `AWiesbadenCar` (bereits `SetExternalControl`-fähig): Polizei-
Lackierung (Material), Blaulicht über die vorhandene `UWiesbadenCarLightsComponent`,
Sirene über die vorhandene Audio-Komponente. Vom Subsystem gespawnt/entfernt.

## Crime-Detektion (dünner Glue)

- **Fahrzeug-Kollision** (Hook in `AWiesbadenCar::SweepVehicle`/Hit): getroffener
  Actor ist Fußgänger -> `PedestrianHit`; ist Streifenwagen -> `PoliceRam`. Meldet
  ans Subsystem, das `RegisterCrime` ruft.
- **RecklessNearPolice:** Subsystem prüft Spieler-Tempo x Nähe zu Polizei je Tick.

Keine Detektion im datenreinen Kern - der bekommt nur `RegisterCrime`-Aufrufe.

## HUD

`WiesbadenVehicleHUD` (und Fuß-HUD): N Sternchen (gefüllt/leer) oben rechts,
blinkend während der Flucht (Heat verfällt). Kleine Flucht-Fortschrittsanzeige
optional (Zeit-bis-frei).

## Performance (kritisch, ehrlich)

Verfolger sind **schwergewichtige Actors** - das Spiel ist bereits spiel-strang-
gebunden (~10 FPS, siehe Streaming-Wurzel). Deshalb:
- **Harte Obergrenze** ~6 Verfolger (5★), strenge Entfern-Disziplin bei Flucht.
- Empfehlung: **nach dem WP-Streaming-Fix** bauen ODER mit strikter Kappung, damit
  die Verfolger nicht auf ein ohnehin überlastetes Bild draufsatteln.
- Der Polizeiheli nutzt den vorhandenen Autopiloten (eine Instanz).

## Test-Strategie

- **`FWiesbadenWantedState`:** C++-Automation-Test (+ optional Node-Port) - Crime
  -> Heat -> Sternchen, Verfall nach Grace, Eskalations-Schwellen. Das ist der
  Großteil der testbaren Logik (datenrein, deterministisch).
- **Verfolgungs-KI:** Rauchtest-Stil - `WbWanted 3` erzwingen, per Log nachweisen,
  dass ein Streifenwagen die Distanz zum Spieler über die Zeit verringert.
- Eskalations-Tabelle: Test, dass Verfolgerzahl je Stufe stimmt.
- Ziel: die 138 bestehenden Tests bleiben grün; neue Tests kommen dazu.

## Offene Fragen / Risiken

1. **Perf** - Actors auf ein 10-FPS-Bild. Kappung + WP-Fix zuerst.
2. **Pfad-Pendeln** an Kreuzungen (gieriges Ziel bewegt sich) - Hysterese, ggf.
   Neuplanung nur alle N Sekunden.
3. **Festnahme-UX** - v1 minimal (Fade + Respawn); ausbaufähig.
4. **Spieler zu Fuß** - Cops bleiben im Auto; erreicht der Spieler eine
   auto-unzugängliche Stelle, verlieren sie ihn (zählt als Flucht). Fuß-Cops sind
   Follow-up.
5. **Determinismus** des Kerns vs. Actor-Welt - der Kern bleibt rein; alle
   nicht-deterministischen Teile (Raycasts, Spawns) liegen im Subsystem/der KI.

## Umsetzungsreihenfolge (nach Abnahme, via writing-plans)

1. `FWiesbadenWantedState` + `EWiesbadenCrime` + Eskalationstabelle + Unit-Tests.
2. `AWiesbadenPoliceCar` + `UWiesbadenPolicePursuitComponent` (Straßen-Verfolgung).
3. `UWiesbadenPoliceSubsystem` (Roster, Sicht, De-Eskalation) + `WbWanted`-Exec.
4. Crime-Detektion-Hooks + HUD-Sternchen.
5. Heli-Eskalation (4★) über den Autopiloten.
6. Rauchtest-Nachweis + Perf-Kappung.
