# Implementierungsplan — Missions-Rückgrat + Kurier-Mission (Teilprojekt 1)

**Spec:** `docs/superpowers/specs/2026-09-08-missions-framework-design.md`
**Vorgehen:** phasenweise, testgetrieben (RED→GREEN), nach jeder Phase Build.
Automatisierte Tests via `IMPLEMENT_SIMPLE_AUTOMATION_TEST` + `build_only.cmd`;
Abschluss-Nachweis per Spiel-Lauf (`Wiesbaden_spielen.cmd WiesbadenCity_Alkis10`).
Jede Phase ist eigenständig baubar und lässt den Sandkasten heil.

## Phase 0 — Gerüst (kein Verhalten)
1. Ordner `Source/WiesbadenReal/Missions/` anlegen.
2. `WiesbadenMissionTypes.h`: `EObjectiveType{ReachLocation}`, `FMissionContext`,
   `FMissionObjective`, `FMissionReward`, `FMission` (USTRUCTs/POD, nur
   `CoreMinimal`). `IsComplete` zunächst deklariert.
3. Build (nur Kompilierbarkeit der Typen).

## Phase 1 — Ziel-Erfüllung (TDD)
1. `Tests/MissionTest.cpp`: `FMissionObjectiveReachLocationTest` — Ziel bei
   Punkt P, Radius R; Spieler knapp innerhalb → true, knapp außerhalb → false,
   große Höhendifferenz bei gleichem XY → true (horizontal gemessen). **RED.**
2. `FMissionObjective::IsComplete` implementieren (2D-Distanz ≤ RadiusCm). **GREEN.**
3. Build + Test grün.

## Phase 2 — Loader (TDD) + Inhalt
1. `WiesbadenMissionLoader.h/.cpp`: statisch
   `FMissionLoadResult ParseMissions(const FString& Json)` (FJsonSerializer,
   Muster wie `OSMDataParser`), unbekannte `type` überspringen + sammeln.
2. `MissionTest.cpp`: gültiges JSON → 1 Mission, 2 Ziele, korrekte Werte;
   kaputtes JSON → leer + Fehler, kein Crash; unbekannter `type` → übersprungen.
   **RED → GREEN.**
3. `Data/Missions/missions.json` mit der Kurier-Mission anlegen (Koordinaten aus
   der Spec: Abholung nahe Platter, Lieferung Nerobergbahn-Talstation).
4. Build + Tests grün.

## Phase 3 — Fortschritt + Subsystem (TDD)
1. Fortschritts-Logik datenrein extrahieren: eine Funktion/kleine Struktur, die
   aus (Mission, aktuellem Ziel-Index, Kontext) den nächsten Zustand + evtl.
   „erfüllt"/Belohnung liefert — **ohne** Welt/Tick.
2. `MissionTest.cpp`: Kurier-Mission Schritt für Schritt durchlaufen
   (Position an Ziel 0 → Index 1; Position an Ziel 1 → erfüllt, Guthaben +250).
   **RED → GREEN.**
3. `UWiesbadenMissionSubsystem` (UTickableWorldSubsystem): `Initialize` (JSON
   laden), `StartMission`, `Tick` (Spielerposition holen, Fortschritts-Logik
   anwenden, bei Abschluss `Guthaben +=`, Event feuern, Neustart ermöglichen),
   `GetCurrentObjective`, `GetGuthaben`, `GetActiveMissionTitle`, Delegates.
4. Build (Subsystem kompiliert, Tests grün).

## Phase 4 — Anzeige (HUD + Minimap)
1. `WiesbadenVehicleHUD` (und Fuß-HUD): schlankes Ziel-Panel — Missionstitel,
   Ziel-Label, Distanz in Metern. Aktives Ziel vom Subsystem holen.
2. `WiesbadenMinimap`: Ziel-Marker (Diamant) an der Zielposition, relativ zum
   Spieler geclippt (vorhandene Vektor-Straßen-Projektion nachnutzen).
3. Build. (UI-Änderungen sind schwer unit-zu-testen; Nachweis per Bild in Phase 5.)

## Phase 5 — Integration + Spiel-Nachweis
1. Auto-Angebot: beim ersten gültigen Tick mit Spieler-Pawn und ohne aktive
   Mission `StartMission("kurier_platter_01")`.
2. Build.
3. `Wiesbaden_spielen.cmd WiesbadenCity_Alkis10` starten und end-to-end prüfen:
   Ziel-Panel + Marker sichtbar → Käfer zur Abholung → umschalten → zur Lieferung
   → Abschlussmeldung + Guthaben +250. Screenshot-Beleg (HighResShot/Pose oder
   `-WbShot`).
4. Regressions-Check: gesamte Testsuite grün; Sandkasten ohne Mission-JSON weiter
   spielbar (Fehlerpfad).

## Definition of Done
- Alle Unit-Tests grün (Ziel-Erfüllung, Loader, Fortschritt).
- Kurier-Mission im Spiel durchspielbar mit HUD/Marker/Belohnung (Bild-Beleg).
- Neue Aufträge per JSON ohne Neubau möglich.
- Keine Regressionen; fehlende/kaputte JSON bricht das Spiel nicht.

## Commit-Schnitt (Vorschlag)
Ein Commit je Phase (Typen+Test / Loader+JSON / Subsystem / Anzeige /
Integration), damit die Historie den vertikalen Aufbau zeigt. Commit erst nach
deiner Freigabe je Schritt (Projekt-Konvention).
