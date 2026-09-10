# Arcade-Mobilitaet und Nordfriedhof Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Spieler kaufen den Helikopterhangar bei einem Nordfriedhof-Haendler, fahren Arcade-Pkw und -Helikopter per Xbox-Gamepad und erleben die Nerobergbahn als Panorama-Fahrt; anschliessend entsteht ein getestetes Windows-Paket mit Desktop-Verknuepfung.

**Architecture:** Bestehende Store-, Physik- und Rail-Subsysteme bleiben die alleinigen Quellen der Wahrheit. Neue Weltobjekte sind schmale Actors, die vorhandene APIs aufrufen; alle deterministischen Entscheidungen werden vor Actor-Integration in Automation-Tests abgesichert.

**Tech Stack:** Unreal Engine 5.8, C++20, Unreal Automation Tests, Blender/Unreal Editor, PowerShell Release-Pipeline.

**Spec:** `docs/superpowers/specs/2026-09-10-arcade-mobility-and-nordfriedhof-design.md`

## Global Constraints

- Code und Kommentare bleiben ASCII; sichtbare deutsche Texte verwenden bestehende Projektkonventionen.
- Neue Produktionslogik entsteht erst nach einem fehlenden Automation-Test.
- Bestehende uncommittete Aenderungen des vorherigen Agents bleiben getrennt und werden nicht pauschal bereinigt.
- Die Desktop-Verknuepfung zeigt auf `Saved/Package/Windows/WiesbadenReal.exe`, nie auf den Editor.

---

### Task 1: Nordfriedhof-Haendler und echter Hangar-Kauf

**Files:** Create `Source/WiesbadenReal/NPC/WiesbadenStoreMerchant.h/.cpp`, `Source/WiesbadenReal/Tests/StoreMerchantTest.cpp`; modify `Core/WiesbadenGameMode.cpp`, `UI/WiesbadenVehicleHUD.*`, `Store/WiesbadenStore.*`, `Tests/StoreTest.cpp`.

**Interfaces:** `AWiesbadenStoreMerchant::TryInteract(APawn*) -> bool` oeffnet ausschliesslich das bestehende `UWiesbadenStoreSubsystem`; `FWiesbadenStore::MayEnterHelicopter(bool)` ist nur bei freigeschaltetem Hangar wahr.

- [ ] Test schreiben: NPC akzeptiert nur einen Fuss-Pawn in Interaktionsreichweite; Kauf ohne Guthaben bleibt abgelehnt; erfolgreicher Kauf entsperrt den Heli.
- [ ] Test rot ausfuehren: `Automation RunTests WiesbadenReal.Store`.
- [ ] Merchant-Actor mit Reichweite, Hinweistext und deterministischer Nordfriedhof-Position implementieren; NPC/Stand aus lokalen Assets oder schlanken Engine-Primitives aufbauen.
- [ ] HUD-Panel an Merchant-Interaktion, Guthaben, Kaufstatus und Gamepad-Auswahl binden.
- [ ] Widerspruechliche freie Hangar-Logik entfernen; Tests, Build und Spielinteraktion pruefen.
- [ ] Commit: `feat: Nordfriedhof-Haendler und Hangar-Kauf`.

### Task 2: Arcade-Pkw-Fahrgefuehl

**Files:** Modify `Vehicles/WiesbadenVehiclePhysics.*`, `Vehicles/WiesbadenCar.*`, `Tests/VehiclePhysicsTest.cpp`.

**Interfaces:** `FWiesbadenVehiclePhysics::Tick(const FWiesbadenVehiclePhysicsInput&, float)` liefert weiterhin Vorwaerts-, Quer- und Schlupfgeschwindigkeit; `AWiesbadenCar` mappt LT/RT/Linksstick/A darauf.

- [ ] Test schreiben: Ausrollen ist nicht sofort null, Vollbremsung bleibt unter Reifenhaftung, Handbremse erzeugt kontrollierten Schlupf und Gegenlenken reduziert ihn.
- [ ] Test rot ausfuehren: `Automation RunTests WiesbadenReal.Vehicles.Physics`.
- [ ] Parameter fuer Lenkrate, Grip, seitliche D?mpfung, Handbremse und Bremse minimal anpassen; keine neue Parallelphysik bauen.
- [ ] Xbox-Achsen und HUD-Hinweis mit Tastatursteuerung paritaetisch halten.
- [ ] Tests, Editor-Build und Fahrt auf Asphalt/Hang pruefen.
- [ ] Commit: `feat: Arcade-Pkw-Handling`.

### Task 3: Arcade-Helikopter fuer Xbox-Gamepad

**Files:** Modify `Vehicles/WiesbadenHelicopter.*`, `UI/WiesbadenVehicleHUD.*`, `Tests/WeaponAndControlsTest.cpp`, `Tests/RotorPhysicsTest.cpp`.

**Interfaces:** RT/LT liefern Collective, linker Stick X liefert Yaw, rechter Stick liefert Pitch/Roll; `ComputeAutoLevel` arbeitet nur ausserhalb aktiver Pitch/Roll-Eingabe.

- [ ] Test schreiben: Trigger steigen/sinken gegensaetzlich, zentrierter rechter Stick richtet auf, aktive Eingabe unterdrueckt Assistenz.
- [ ] Test rot ausfuehren: `Automation RunTests WiesbadenReal.Vehicles.Helicopter`.
- [ ] Bestehende Stick-Shaping-, Axis- und Auto-Level-Helfer auf die vereinbarte Belegung abstimmen.
- [ ] HUD-Belegung beim Einsteigen anzeigen und Start/Landung/Schweben im Spiel pruefen.
- [ ] Commit: `feat: Arcade-Helikoptersteuerung`.

### Task 4: Nerobergbahn einbetten und Panorama-Fahrt absichern

**Files:** Modify `World/WiesbadenNerobergbahn.*`, `World/WiesbadenRailTransport.*`, `Vehicles/WiesbadenFootPawn.*`, `Tests/RailTransportTest.cpp`, `Tests/VehicleHUDTest.cpp`.

**Interfaces:** `FWiesbadenRideSession` bleibt Zustandsautomat `OnFoot -> Boarding -> Riding -> Exiting -> OnFoot`; `AWiesbadenNerobergbahn::ToggleBoarding()` bindet nur im Stationshalt und gibt den Pawn sicher frei.

- [ ] Test schreiben: Boarding ausserhalb Halt scheitert; gueltige Fahrt erreicht Riding; Ausstieg stellt OnFoot und Bewegung wieder her.
- [ ] Test rot ausfuehren: `Automation RunTests WiesbadenReal.World.RailTransport`.
- [ ] Terrain-Endpunkte, Bahnsteige, St?tzen, Wartezonen und Stationsrequisiten gegen die reale Hoehe ausrichten.
- [ ] Sitzkamera mit freiem Blick und Notausstieg fertigstellen; Tal/Berg-Fahrt im Spiel pruefen.
- [ ] Commit: `feat: Nerobergbahn Panorama-Fahrt`.

### Task 5: Release und Desktop-Verknuepfung

**Files:** Verify `Tools/build_release.cmd`, `Tools/build_release.ps1`, `package_game.cmd`; no new deployment path.

- [ ] `Tools/build_release.cmd -GatesOnly` ausfuehren und Compile-, Automation- und Rauchtest-Gates pruefen.
- [ ] Vollstaendige Pipeline starten; nur bei gruener Vorpruefung nach `Saved/Package/Windows` paketieren.
- [ ] Existenz und Ziel von `Wiesbaden Real (Paket).lnk` gegen `WiesbadenReal.exe` pruefen.
- [ ] Bei Fehlern vor Paketierung stoppen; bei fehlerhaftem Paket `Tools/build_release.cmd -Rollback` verwenden.
- [ ] Commit: `build: aktualisieren getestetes Standalone-Paket`.

## Selbstpruefung

- Spec-Abdeckung: Tasks 1 bis 5 decken Haendler, Hangar, Pkw, Helikopter, Nerobergbahn und Standalone ab.
- Keine Platzhalter: jeder Task benennt Dateien, Schnittstellen, Testlauf und Abschluss.
- Abhaengigkeiten: Store vor Helikopter; Fahrgefuehl und Helikopter getrennt; Bahn unabhaengig; Paketierung nur nach allen Tests.
