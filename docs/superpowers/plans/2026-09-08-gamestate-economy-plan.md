# Implementierungsplan — Spielzustand/Ökonomie (TP2 Stück 1)

**Spec:** `docs/superpowers/specs/2026-09-08-gamestate-economy-design.md`
**Vorgehen:** phasenweise, testgetrieben wo datenrein; Save/Load per Laufzeit-Log
belegt. Je Phase Build; Commit erst nach Freigabe je Schritt.

## Phase 0 — Gerüst
1. `WiesbadenSaveGame.h`: `UWiesbadenSaveGame : USaveGame` mit `UPROPERTY int32
   Guthaben`, `int32 SaveVersion`.
2. `WiesbadenGameStateSubsystem.h/.cpp`: `UGameInstanceSubsystem`-Skelett mit
   `int32 Guthaben`, leeren API-Methoden + `FOnGuthabenChanged`-Delegate.
3. Build (kompiliert).

## Phase 1 — Kontostand-Logik (TDD)
1. `Tests/GameStateTest.cpp`: `AddGuthaben` summiert; `SpendGuthaben` bucht nur
   bei Deckung ab, Guthaben nie < 0; Unterdeckung → false, unverändert. **RED.**
2. `AddGuthaben`/`SpendGuthaben` implementieren (bei headless-Instanziierungs-
   problemen: pure Hilfsfunktion `ApplyDelta`/`TrySpend` extrahieren und die
   testen). **GREEN.**
3. Build + Test grün.

## Phase 2 — Persistenz (Save/Load)
1. `Initialize()`: `UGameplayStatics::LoadGameFromSlot("WiesbadenReal", 0)` →
   Guthaben setzen (oder 0). Log „Guthaben geladen: %d".
2. Nach jeder Wertänderung: `SaveGameToSlot`. Log „Guthaben gespeichert: %d".
3. Build. (Save/Load nicht unit-testbar → Nachweis in Phase 5.)

## Phase 3 — Missions-Integration
1. `WiesbadenMissionSubsystem`: lokales `Guthaben`/`GetGuthaben()` entfernen; bei
   Abschluss `GetGameInstance()->GetSubsystem<UWiesbadenGameStateSubsystem>()
   ->AddGuthaben(Reward.Guthaben)`. Fehlt das Subsystem → Warnung, Mission bleibt
   erfüllt. Abschluss-Log „gesamt" aus dem GameState.
2. Build. Bestehende Missions-Unit-Tests bleiben grün (betreffen Runner/Loader/
   IsComplete, nicht das Guthaben-Feld).

## Phase 4 — HUD-Anzeige
1. `WiesbadenVehicleHUD`: `DrawGuthaben()` (oder Zeile in DrawHUD) — „Guthaben:
   X €" oben rechts, nahe der Minimap; Wert vom GameState-Subsystem.
2. Build.

## Phase 5 — Laufzeit-Nachweis (Persistenz)
1. Lauf 1 (Alkis10, temporärer Demo-Treiber wie beim Missions-Loop, um den
   Abschluss zu erzwingen): Log zeigt „Guthaben gutgeschrieben: +250 (gesamt
   250)" + „gespeichert".
2. Lauf 2 (frischer Start, kein Demo): Log zeigt „Guthaben geladen: 250" → HUD
   zeigt den Stand. Persistenz end-to-end belegt.
3. Demo-Gerüst restlos entfernen; nur echter Code bleibt.

## Definition of Done
- Unit-Test Kontostand-Logik grün; bestehende Tests weiter grün.
- Missionsabschluss erhöht das zentrale Guthaben, HUD zeigt „Guthaben: X €".
- Stand überlebt Neustart (Log-Beleg über zwei Läufe).
- Fehlende/kaputte Save-Datei bricht das Spiel nicht.

## Commit-Schnitt
Ein Commit je Phase (SaveGame+Subsystem-Gerüst / Kontostand-Logik+Test /
Persistenz / Missions-Integration / HUD). Kein Commit des Demo-Gerüsts.
