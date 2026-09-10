# Arcade-Mobilitaet, Nordfriedhof-Haendler und Nerobergbahn — Gesamtkontext

## Stand zurücksetzen
- Repository: `C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal`
- HEAD: `6d64fb7` (committet), letzter gelieferter Stand des vorherigen Threads.
- Uncommittete Änderungen: keine (nach reread).
- Kein separater task_plan.md / progress.md / findings.md vorhanden — dieser Kontext ersetzt die vorherigen Thread-Notizen.

## Was committet ist
6d64fb7 enthält die Bereiche, in die der vorherige Thread eingetreten war:
- Store: `HelikopterHangarId`, `TryPurchase`, Freischaltungslogik (`WiesbadenStore.*`, `WiesbadenStoreSubsystem.*`, `unlocks.json`)
- Helikopter-Hangar-Gate in `WiesbadenGameMode::TogglePlayerVehicle`
- Spielstartkarte: `Wiesbaden_spielen.cmd` auf `WiesbadenCity_Alkis9` umgestellt
- Minimap-Vektor-Schwelle (Texture->Vector): Tuning von `WorldMapVectorSwitchMagnification` auf 1.4f mit passenden Tests
- Fahrzeug-HUD: Testaktualisierungen, Steuerungslegende, Fuß-Prompt / Nerobergbahn-Hinweis

## Was bereits existiert und nachvollzogen wurde
### Store / Ökonomie
- `FWiesbadenStore` ist datenrein, `UWiesbadenStoreSubsystem` lädt `Data/Store/unlocks.json` und delegiert Kauf/Status an `TryPurchase`.
- Persistenter Guthaben-Zustand ist bereits vorhanden (`UWiesbadenGameStateSubsystem`).

### Fahrzeugsteuerung
- Beide Fahrzeuge nutzen `WiesbadenVehicleControl.h` / `WiesbadenVehicleControl`-Schnittstelle.
- Auto: `WiesbadenCar` polls Tastatur + Gamepad analog.
- Helikopter: `WiesbadenHelicopter` hat bereits per-Stick-Shaping, per-Achse-Folgen, und externe Steuerung.

### Nerobergbahn
- `WiesbadenNerobergbahn` baut Trasse aus OSM-Koordinaten, löst Höhen nach, platziert Stations-/Wagenkomponenten, besitzt `FWiesbadenRideSession` (OnFoot -> Boarding -> Riding -> Exiting -> OnFoot).
- Stations-/Wagenmobs existieren in `Content/Nerobergbahn/Meshes`.
- Tests: `RailTransportTest.cpp` (Profil, Endpunkte, Zustandsübergänge, Bewegung).

### Szene / Map
- Es gibt Alkis3, Alkis4, Alkis9 (Proc/SM Varianten).
- Standardstartkarte nach 6d64fb7: `WiesbadenCity_Alkis9`.
- Nordfriedhof-Szene und NPC-Szene fehlen noch so as a gameplay-Element – das ist
  die offene Aufgabe aus dem letzten Thread.

## Geplante Umsetzung
Die Arbeit bleibt in der in der Spec festgelegten Teilreihenfolge, aber jetzt in einem
zusammengeführten, ausführlicheren Plan:
1. Nordfriedhof-Händler + echter Hangar-Kauf (Store-Gate reaktivieren, NPC + Szenerie)
2. Arcade-Pkw-Handling (Tuning, ohne neues Parallelsystem)
3. Arcade-Helikopter für Xbox-Gamepad
4. Nerobergbahn als begeh-/befahrbares Panorama-Erlebnis (Szenerie, Stationen, Sitzkamera)
5. Geprüftes Standalone-Paket mit Desktop-Verknüpfung

Spezifikation: `docs/superpowers/specs/2026-09-10-arcade-mobility-and-nordfriedhof-design.md`
Plan-Stub: `docs/superpowers/plans/2026-09-10-arcade-mobilitaet-nordfriedhof.md`

## Nächster Schritt
Test-first, klein: Store-Gate wieder "nur mit gekauftem Hangar", Test `MayEnterHelicopter`
passt, dann NPC/StorePanel-Interaktion, dann Händler-Szenerie auf Alkis9, dann arcade
Tuning. Danach Helikopter-Belegung, dann Nerobergbahn-Szenerie / Bahnsteige, dann Release.
