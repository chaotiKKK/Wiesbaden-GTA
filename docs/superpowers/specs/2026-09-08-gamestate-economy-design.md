# Spielzustand/Ökonomie — persistentes Guthaben + HUD (Design)

**Datum:** 2026-09-08
**Teilprojekt:** 2, Stück 1 von 3 (Fortschritt/Meta)
**Status:** freigegeben (Design), Spec zur Review

## Kontext & Ziel

Teilprojekt 1 (Missions-Rückgrat) vergibt bei Abschluss ein Guthaben, das aber
in einer lokalen Zahl im Missions-Subsystem versickert — kein persistenter
Kontostand, keine Anzeige, kein Sinn. Dieses Stück gibt der Belohnung einen
echten Besitzer: ein persistentes Guthaben-Konto, an dem später auch
Fahndung/Freischaltungen andocken.

### Erfolgskriterium (Definition of Done)

Beim Missionsabschluss steigt ein **persistentes** Guthaben, das HUD zeigt
„Guthaben: X €", und der Stand **überlebt einen Neustart** (SaveGame). Missionen
schreiben nicht mehr in eine eigene Zahl, sondern gutschreiben zentral.

## Architektur (Ansatz A)

Isolierter Besitzer des Spielzustands, getrennt vom Level (überlebt Level-Wechsel
via GameInstance) und von der Sitzung (SaveGame auf Platte).

### Komponenten

- **`UWiesbadenGameStateSubsystem`** (UGameInstanceSubsystem):
  - Zustand: `int32 Guthaben = 0`.
  - API: `int32 GetGuthaben() const`; `void AddGuthaben(int32 Delta)`;
    `bool SpendGuthaben(int32 Kosten)` (nur wenn gedeckt; Guthaben nie < 0).
  - `Initialize()`: lädt den Stand aus dem SaveGame (fehlt einer → 0).
  - Jede wertändernde Operation schreibt den SaveGame (Missionsabschlüsse sind
    selten; Save-je-Änderung ist unkritisch). Delegate `OnGuthabenChanged` für HUD.
- **`UWiesbadenSaveGame`** (USaveGame):
  - `UPROPERTY() int32 Guthaben = 0;`
  - `UPROPERTY() int32 SaveVersion = 1;` (Vorwärtskompatibilität).
  - Slot „WiesbadenReal", Nutzer-Index 0, via `UGameplayStatics::SaveGameToSlot`
    / `LoadGameFromSlot`.
- **Missions-Subsystem** (Änderung): lokales `Guthaben`/`GetGuthaben` **entfällt**;
  bei Abschluss `GameState->AddGuthaben(Reward.Guthaben)`, Log „gesamt" aus dem
  GameState geholt. Zugriff über `GetGameInstance()->GetSubsystem<...>()`.
- **HUD** (`WiesbadenVehicleHUD`): kleine Anzeige „Guthaben: X €" oben rechts,
  nahe der Minimap; liest `GetGuthaben()` vom Subsystem.

### Dateien

```
Source/WiesbadenReal/Core/WiesbadenSaveGame.h
Source/WiesbadenReal/Core/WiesbadenGameStateSubsystem.h
Source/WiesbadenReal/Core/WiesbadenGameStateSubsystem.cpp
Source/WiesbadenReal/Tests/GameStateTest.cpp
```
HUD-Anzeige erweitert `WiesbadenVehicleHUD.cpp`; das Missions-Subsystem wird
angepasst (kein neuer Ort).

## Ablauf

1. GameInstance-Start → Subsystem `Initialize` → SaveGame laden (oder 0).
2. Mission erfüllt → Missions-Subsystem ruft `AddGuthaben(Reward)` → Subsystem
   erhöht, speichert, feuert `OnGuthabenChanged`.
3. HUD zeichnet je Frame „Guthaben: X €".
4. Neustart → `Initialize` lädt den gespeicherten Stand → HUD zeigt ihn.

## Fehlerbehandlung

- Kein/kaputter SaveGame-Slot → Start mit 0, kein Crash.
- Kein GameState-Subsystem erreichbar (z. B. im Test/headless ohne GameInstance)
  → Missions-Subsystem loggt eine Warnung und überspringt die Gutschrift, ohne
  zu crashen (Mission gilt trotzdem als erfüllt).
- `SpendGuthaben` bei Unterdeckung → false, Guthaben unverändert.

## Tests + Nachweis

- **Unit** (`GameStateTest`): `AddGuthaben` summiert; `SpendGuthaben` bucht nur
  bei Deckung ab und hält Guthaben ≥ 0 (die datenreine Arithmetik, ohne
  Save/Engine). Falls das Subsystem headless schwer zu instanziieren ist, wird
  die Kontostand-Logik in eine pure Hilfsfunktion gezogen und die getestet.
- **Laufzeit-Nachweis** (Log): Mission abschließen → „Guthaben gutgeschrieben:
  +250 (gesamt 250)"; Spiel neu starten → „Guthaben geladen: 250". Das belegt
  Persistenz end-to-end.

## Bewusst nicht in v1 (YAGNI → spätere Stücke/Teilprojekte)

- Fahndung/Wanted-Level (Stück 2), Freischaltungen (Stück 3).
- Ausgeben-UI/Shops, mehrere Speicherstände, Cloud-Save, Auto-Save-Debounce.
- Nur: Guthaben verdienen (aus Missionen), anzeigen, über Sitzungen persistieren.
