# Missions-Rückgrat + erste Kurier-Mission — Design

**Datum:** 2026-09-08
**Teilprojekt:** 1 von 6 (Fundament für „WiesbadenReal wird ein Spiel")
**Status:** freigegeben (Design), Spec zur Review

## Kontext & Ziel

WiesbadenReal ist heute ein Sandkasten (Käfer fahren, Ka-52 fliegen, Fuß-Pawn
mit Waffen) ohne Zweck. Die Gesamtvision: ein breites Open-World-Spiel, das
mehrere Spielstile (Action, taktisch, City-Life, Arcade) auf **gemeinsamen
Fundamenten** trägt. Zerlegt in 6 Teilprojekte:

1. **Missions-/Ziel-Framework** ← *dieses Dokument, das Rückgrat*
2. Fortschritt/Spielzustand (Geld, Wanted, Freischaltungen)
3. Kampf-Tiefe + Gegner-KI
4. Fahrzeug-/Heli-Gameplay (Ein-/Aussteigen, Schaden, Heli-Waffen)
5. Weltsysteme (Tageszeit/Wetter, Distrikte, POIs)
6. Modus-Inhalt (Action-Missionen, Militär-Ops, City-Jobs, Arcade-Challenges)

Jedes weitere Teilprojekt steckt am Missions-Rückgrat an. Dieses Teilprojekt
liefert das Rückgrat **plus** eine erste durchgespielte **Kurier-Mission**
(Abholpunkt → Lieferpunkt → Belohnung) als vertikalen Schnitt — ohne
Gegner-KI, damit der Loop schnell und demofähig steht.

### Erfolgskriterium (Definition of Done für v1)

Beim Spielstart ist automatisch eine Kurier-Mission aktiv. Das HUD zeigt das
aktuelle Ziel („Fahre zur Abholung — 240 m") und die Minimap einen Marker.
Fährt der Spieler mit dem Käfer in den Abholradius, schaltet das Ziel auf
„Liefere die Sendung" um (Marker + HUD folgen). Erreicht er den Lieferpunkt,
gilt die Mission als erfüllt: Guthaben steigt um den Missionsbetrag und eine
Abschlussmeldung erscheint. Die Mission lässt sich wiederholen. Neue Aufträge
entstehen durch Editieren einer JSON-Datei, **ohne Neubau**.

## Architektur

Datengetrieben: **Runtime in C++**, **Missionsinhalt als JSON**. Klare Trennung
zwischen dem Missions-*Ablauf* (Code) und den Missions-*Daten* (Content).

### Komponenten

- **`WiesbadenMissionTypes.h`** — datenreine Typen, keine Engine-Abhängigkeit
  über `CoreMinimal` hinaus:
  - `enum class EObjectiveType : uint8 { ReachLocation }` (v1; später
    `Eliminate`, `Deliver`, `Survive` …).
  - `struct FMissionObjective { EObjectiveType Type; FVector Location;
    double RadiusCm; FString Label; }` mit **purer** Methode
    `bool IsComplete(const FMissionContext& Ctx) const`. Für `ReachLocation`:
    horizontale Distanz `Spieler↔Location` ≤ `RadiusCm`.
  - `struct FMissionContext { FVector PlayerLocation; }` (v1; wächst später um
    Kills/Zeit/Inventar).
  - `struct FMissionReward { int32 Guthaben = 0; }`.
  - `struct FMission { FName Id; FString Title; TArray<FMissionObjective>
    Objectives; FMissionReward Reward; }`.
- **`WiesbadenMissionLoader` (.h/.cpp)** — statische, datenreine Funktion
  `FMissionLoadResult ParseMissions(const FString& Json)` →
  `TArray<FMission>` + Fehlerliste. Muster wie `OSMDataParser` (FJsonSerializer).
  Unbekannte `type`-Werte werden übersprungen und geloggt, nicht als Absturz.
- **`UWiesbadenMissionSubsystem` (UTickableWorldSubsystem)** — der Runtime-Kern:
  - `Initialize()`: lädt die Missions-JSON von `FPaths::ProjectDir()/Data/
    Missions/missions.json` (wie die ALKIS-Rohdaten unter `Data/Raw/`), parst
    via Loader, hält `TArray<FMission> Missions`.
  - Zustand: `int32 ActiveMissionIndex = -1; int32 ActiveObjectiveIndex = 0;
    int32 Guthaben = 0;`.
  - API: `bool StartMission(FName Id)`, `const FMissionObjective*
    GetCurrentObjective() const`, `int32 GetGuthaben() const`,
    `FString GetActiveMissionTitle() const`.
  - `Tick(float Dt)`: throttled (~4–8 Hz genügt). Holt die Spielerposition
    (PlayerController→Pawn). Wenn `GetCurrentObjective()->IsComplete(Ctx)`:
    `ActiveObjectiveIndex++`; war es das letzte Ziel → Mission erfüllt:
    `Guthaben += Reward.Guthaben`, `OnMissionCompleted` feuern,
    `ActiveMissionIndex = -1` (bereit für Neustart).
  - Delegates: `FOnObjectiveChanged`, `FOnMissionCompleted` (multicast) für
    HUD/Sound/FX — die Anzeige pollt zusätzlich `GetCurrentObjective()` je Frame.
- **HUD-Erweiterung (`WiesbadenVehicleHUD`, Fuß-HUD):** ein schlankes
  Ziel-Panel — Missionstitel + Ziel-Label + Distanz zum Ziel in Metern.
- **Minimap-Erweiterung (`WiesbadenMinimap`):** ein Ziel-Marker (Diamant/Farbe)
  an der Zielposition, relativ zum Spieler geclippt wie die vorhandenen
  Vektor-Straßen.

### Dateistruktur (isoliert, je Datei ein Zweck)

```
Source/WiesbadenReal/Missions/WiesbadenMissionTypes.h
Source/WiesbadenReal/Missions/WiesbadenMissionLoader.h
Source/WiesbadenReal/Missions/WiesbadenMissionLoader.cpp
Source/WiesbadenReal/Missions/WiesbadenMissionSubsystem.h
Source/WiesbadenReal/Missions/WiesbadenMissionSubsystem.cpp
Source/WiesbadenReal/Tests/MissionTest.cpp
Data/Missions/missions.json
```
HUD/Minimap-Anzeige erweitert die bestehenden UI-Dateien (kein neuer Ort).

## Daten: JSON-Schema

```json
{
  "missions": [
    {
      "id": "kurier_platter_01",
      "title": "Kurierfahrt Platter Straße",
      "reward": { "guthaben": 250 },
      "objectives": [
        { "type": "reach_location", "label": "Fahre zur Abholung",
          "x": -121000.0, "y": -119000.0, "radius_cm": 800.0 },
        { "type": "reach_location", "label": "Liefere die Sendung",
          "x": -104083.0, "y": -137317.0, "radius_cm": 800.0 }
      ]
    }
  ]
}
```

- Koordinaten in **Welt-cm** (v1, simpel und mapunabhängig genug, da die
  Stadt an fester Weltposition liegt). Abholung nahe Platter-Spawn, Lieferung
  an der Nerobergbahn-Talstation → eine Fahrt quer über die Karte.
- `z` wird nicht vorgegeben; die Erfüllung misst **horizontal** (2D), damit
  Hang/Höhe nicht stören (wie bei der Bahn-Umgebung).
- Erweiterbar: weitere `type`-Werte fügen neue Ziel-Logik hinzu, ohne das
  Schema zu brechen.

## Ablauf (Lebenszyklus)

1. `Initialize()` → Missionen aus JSON laden. Fehlt/kaputt die Datei: Log-
   Warnung, keine Missionen — das Spiel bleibt als Sandkasten spielbar.
2. Beim ersten gültigen Tick mit vorhandenem Spieler-Pawn und `ActiveMissionIndex
   == -1`: `StartMission("kurier_platter_01")` (Auto-Angebot der ersten Mission).
3. `Tick` prüft das aktuelle Ziel; erfüllt → nächstes Ziel oder Missionsabschluss
   (Belohnung + Meldung + Event), danach Neustart möglich.
4. HUD/Minimap fragen `GetCurrentObjective()` je Frame und zeichnen Label,
   Distanz und Marker.

## Fehlerbehandlung

- Ungültiges/fehlendes JSON → Log, leere Missionsliste, Sandkasten bleibt heil.
- Kein Spieler-Pawn (z. B. während Streaming/Umstieg) → Erfüllungsprüfung
  übersprungen, kein Fortschritt, kein Crash.
- Objektiv-Index-Grenzen strikt geprüft (kein Zugriff außerhalb der Liste).
- Unbekannter `type` in der JSON → Ziel übersprungen + geloggt.

## Tests (TDD, `IMPLEMENT_SIMPLE_AUTOMATION_TEST`)

- **Ziel-Erfüllung:** `FMissionObjective::IsComplete` für `ReachLocation` —
  genau innerhalb/außerhalb des Radius, Höhe ignoriert.
- **Loader:** gültiges JSON → korrekte `FMission`-Struktur; kaputtes/leeres JSON
  → leer + Fehler ohne Crash; unbekannter `type` → übersprungen.
- **Fortschritt:** eine datenreine Fortschritts-Funktion (Ziel-Index vorrücken,
  Abschluss erkennen, Belohnung summieren) end-to-end durch die Kurier-Mission —
  Guthaben am Ende korrekt.

## Bewusst nicht in v1 (YAGNI → spätere Teilprojekte)

- Persistenz/Speichern, Freischaltungen, Wanted-Level (Teilprojekt 2).
- Gegner, Kampf-Ziele („ausschalten"), Trefferlogik (Teilprojekt 3).
- Missions-Geber-NPCs / Annahme-Dialog — v1 bietet die Mission automatisch an.
- Mehrere gleichzeitige Missionen, Verzweigungen, Zeitlimits — eine lineare
  Ziel-Kette genügt fürs Rückgrat.
- Welt-Beacon (Lichtsäule am Ziel): nur HUD + Minimap-Marker in v1; ein Beacon
  kann später risikolos ergänzt werden.
```
