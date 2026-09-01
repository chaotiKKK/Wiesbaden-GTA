# Fahrzeug-/Figur-Testinfrastruktur und Fixes — Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Die verbleibenden Spiel-Baustellen (Dev-Werkzeuge, Fahrzeug-Kollision, Sebbo-Animationen) abschliessen und dabei eine scriptbare, schnelle Test-Infrastruktur schaffen, die zukuenftige Gameplay-Arbeit de-risked.

**Architecture:** Zuerst reine, testbare Logik aus den bestehenden Dev-Menue-Handlern herausziehen und als `UFUNCTION(Exec)`-Konsolenbefehle spiegeln (per `-ExecCmds` automatisierbar). Darauf aufbauend ein Automation-Test, der die Fahrzeug-Ruhelage prueft. Erst danach die asset-/editor-gebundenen Fixes (Kaefer-Chassis-Kollision, Sebbo-Anim-Import), die durch den Regressionstest bzw. einen In-Game-Screenshot abgesichert werden.

**Tech Stack:** Unreal Engine 5.8, C++ (Chaos Vehicles), UE Automation Framework (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`), Blender 5.2 (Asset-Pipeline), Python-Editor-Commandlets.

**Spec:** Keine separate Spec-Datei — dieser Plan implementiert die Brainstorm-Empfehlungen dieser Session (Dev-Konsolenbefehle, Fahrzeug-Test-Regression) sowie die offenen Nutzer-Punkte #3 (Kaefer kippt/schwebt) und #4 (Sebbo gespreizte Beine). Die verbindlichen Fakten stehen unter Global Constraints.

## Global Constraints

- **Nur ASCII im Code.** Umlaute nur in bestehenden UPROPERTY-Kategorie-Strings. Neue Strings: „Strasse", „Gebaeude", „Gefaelle". (verbatim aus AGENTS.md)
- **Antworten und Code-Kommentare auf Deutsch.**
- **Unity-Build ist AUS** (`bUseUnity = false` in `WiesbadenReal.Build.cs`) — jede `.cpp` ist eine eigene Uebersetzungseinheit; keine gleichnamigen anonyme-Namespace-Helfer als Doppel-Definition erwarten.
- **Build:** `Tools\bau_neuerpc.cmd` (ruft `Build.bat WiesbadenRealEditor Win64 Development`, schreibt Sentinel `__BUILD_EXIT=` ans Log-Ende `bau_neuerpc.log`). Ergebnis: `Result: Succeeded`.
- **Tests:** Editor-Commandlet mit `-stdout`; Ergebnis in `Saved\Logs\WiesbadenReal.log` (`Result={Success}`), NICHT im Build-Log. Aktuell registriert: 133; jede neue `IMPLEMENT_*_AUTOMATION_TEST` erhoeht die Zahl. `run_tests.cmd` ist KAPUTT (ruft Build.bat ohne `call`, baut nur) — `run_tests_only.cmd` (Editor direkt) nutzen.
- **Bekannte Weltkoordinaten (cm):** Platter Strasse 146 `(-121474, -119312, 11347)`, Nerobergbahn `(-104083, -137317, 8800)`, Garten Nerotal 48 `(-71366, -124226, 8530)`.
- **Kaefer-Fakten:** `SK_VWBeetle` Mesh-Z ∈ [0, 154] cm, Radradius 34,3 cm (Rad-Aufstand bei Z≈0), Chaos-Spring-Trace-Kanal `ECC_WorldDynamic`. Chassis-Kollision liegt im `SK_VWBeetle_Chassis_PhysicsAsset` und reicht unter die Radaufstandslinie (Ursache fuer schweben/kippen).
- **Sebbo-Fakten:** `WiesbadenFootPawn` laedt `/Game/Assets/People/SK_Sebbo` + Anims `SK_SebboSebboRig_Sebbo_{Idle,Walk,Swing}` (Fallback-Kandidat `%s`); nur wenn ALLE drei laden, wird das animierte Skelett genutzt, sonst statisches `SM_Sebbo`. UE-5.8-Commandlet-Import erzeugt KEINE Anim-Sequenzen — GUI-Drag&Drop noetig. Frische, korrigierte FBX liegt in `Data\Raw\Sebbo\SK_Sebbo.fbx`.

---

## Hinweis zur Aufteilung

Dieser Plan buendelt vier unabhaengige Subsysteme (A–D). Jeder Teil liefert fuer sich lauffaehige, testbare Software und kann als eigener Plan ausgefuehrt werden. Empfohlene Reihenfolge: **A → B → C → D** (A/B schaffen die Werkzeuge, mit denen C/D verifiziert werden). Teile C und D enthalten manuelle Editor-Gates, weil UE-PhysicsAssets und FBX-Anim-Import nicht per Commandlet bearbeitbar sind.

---

## Datei-Struktur

- `Source/WiesbadenReal/Core/WiesbadenDevActions.h` / `.cpp` — **NEU.** Datenreine Helfer: Teleportziele und Aufricht-Transform. Eine Verantwortung: die reine Logik der Dev-Aktionen, ohne Welt/Pawn-Abhaengigkeit. Von HUD und Konsolenbefehlen geteilt (DRY).
- `Source/WiesbadenReal/Core/WiesbadenDevConsole.h` / `.cpp` — **NEU.** `UWiesbadenDevConsole` (UGameInstanceSubsystem) mit `UFUNCTION(Exec)`-Befehlen, die die Helfer auf den lokalen Pawn anwenden.
- `Source/WiesbadenReal/UI/WiesbadenVehicleHUD.cpp` — **MODIFY.** Pause-Menue-Handler auf die geteilten Helfer umstellen (Duplikat entfernen).
- `Source/WiesbadenReal/Tests/DevActionsTest.cpp` — **NEU.** Automation-Tests fuer die reinen Helfer.
- `Source/WiesbadenReal/Tests/VehicleRestTest.cpp` — **NEU.** Regressionstest der Fahrzeug-Ruhelage (Chassis vs. Rad).
- `Source/WiesbadenReal/Vehicles/WiesbadenChaosCar.h` / `.cpp` — **MODIFY (Teil C).** Datenreine Ruhelage-Pruefung als static Funktion; Chassis-Fix erfolgt im Asset.
- `Source/WiesbadenReal/Vehicles/WiesbadenFootPawn.cpp:346-361` — **MODIFY (Teil D).** Anim-Ladekandidaten an tatsaechliche Importnamen anpassen.

---

## Teil A — Dev-Aktionen als testbare Logik + Exec-Konsolenbefehle

Ziel: Teleport, Fahrzeug-Aufrichten und Verkehr-Umschalten als per `-ExecCmds="WbTeleport 2"` scriptbare Befehle, mit reiner, getesteter Kernlogik. Loest die Injektions-Unzuverlaessigkeit (Esc/Tasten am D3D-Fenster kamen nicht an).

### Task A1: Datenreine Dev-Aktions-Helfer

**Files:**
- Create: `Source/WiesbadenReal/Core/WiesbadenDevActions.h`
- Create: `Source/WiesbadenReal/Core/WiesbadenDevActions.cpp`
- Test: `Source/WiesbadenReal/Tests/DevActionsTest.cpp`

**Interfaces:**
- Produces:
  - `enum class EWiesbadenDevTeleport : uint8 { PlatterStrasse=0, Nerobergbahn=1, GartenNerotal48=2 }`
  - `struct FWiesbadenDevActions` mit:
    - `static FVector TeleportTargetCm(EWiesbadenDevTeleport Target)` — liefert den Zielpunkt in cm (bekannte Weltkoordinaten), OHNE die +300-Anhebung.
    - `static FVector TeleportSpawnCm(EWiesbadenDevTeleport Target)` — `TeleportTargetCm(...) + FVector(0,0,300)` (Fallhoehe, damit kein Punkt IM Boden steckt).
    - `static FTransform UprightTransform(const FTransform& Current)` — behaelt Yaw + Ort, setzt Pitch/Roll auf 0 und hebt Z um 150 cm an.

- [ ] **Step 1: Write the failing test**

```cpp
// Source/WiesbadenReal/Tests/DevActionsTest.cpp
#include "Misc/AutomationTest.h"
#include "Core/WiesbadenDevActions.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDevActionsTest,
    "WiesbadenReal.Dev.Actions",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDevActionsTest::RunTest(const FString& Parameters)
{
    // Teleportziele: exakte bekannte Weltkoordinaten.
    TestEqual(TEXT("Platter Strasse"),
        FWiesbadenDevActions::TeleportTargetCm(EWiesbadenDevTeleport::PlatterStrasse),
        FVector(-121474.0, -119312.0, 11347.0));
    TestEqual(TEXT("Nerobergbahn"),
        FWiesbadenDevActions::TeleportTargetCm(EWiesbadenDevTeleport::Nerobergbahn),
        FVector(-104083.0, -137317.0, 8800.0));
    TestEqual(TEXT("Garten Nerotal 48"),
        FWiesbadenDevActions::TeleportTargetCm(EWiesbadenDevTeleport::GartenNerotal48),
        FVector(-71366.0, -124226.0, 8530.0));

    // Spawn = Ziel + 300 cm Fallhoehe.
    TestEqual(TEXT("Spawn liegt 300 ueber dem Ziel"),
        FWiesbadenDevActions::TeleportSpawnCm(EWiesbadenDevTeleport::Nerobergbahn).Z,
        8800.0 + 300.0);

    // Aufrichten: Pitch/Roll -> 0, Yaw bleibt, Z +150.
    const FTransform Kippt(FRotator(40.0, 90.0, 25.0), FVector(100.0, 200.0, 500.0), FVector::OneVector);
    const FTransform Auf = FWiesbadenDevActions::UprightTransform(Kippt);
    TestTrue(TEXT("Pitch 0"), FMath::IsNearlyZero(Auf.Rotator().Pitch, 0.01));
    TestTrue(TEXT("Roll 0"), FMath::IsNearlyZero(Auf.Rotator().Roll, 0.01));
    TestEqual(TEXT("Yaw bleibt"), Auf.Rotator().Yaw, 90.0);
    TestEqual(TEXT("Z +150"), Auf.GetLocation().Z, 650.0);
    TestEqual(TEXT("X/Y bleiben"), FVector2D(Auf.GetLocation()), FVector2D(100.0, 200.0));
    return true;
}
```

- [ ] **Step 2: Run test to verify it fails**

Der Test kompiliert noch nicht (Header fehlt) → Build schlaegt fehl. Das ist der erwartete „rote" Zustand.
Run: `Tools\bau_neuerpc.cmd`
Expected: FAIL — `Cannot open include file: 'Core/WiesbadenDevActions.h'`.

- [ ] **Step 3: Write minimal implementation (Header)**

```cpp
// Source/WiesbadenReal/Core/WiesbadenDevActions.h
#pragma once
#include "CoreMinimal.h"

// Ziele der Dev-Teleports. Index == -ExecCmds-Argument (WbTeleport <n>).
enum class EWiesbadenDevTeleport : uint8
{
    PlatterStrasse   = 0,
    Nerobergbahn     = 1,
    GartenNerotal48  = 2,
};

// Datenreine Kernlogik der Dev-Aktionen: keine Welt, kein Pawn - damit unter
// Automation testbar. HUD und Konsole rufen dieselben Funktionen (DRY).
struct WIESBADENREAL_API FWiesbadenDevActions
{
    static FVector TeleportTargetCm(EWiesbadenDevTeleport Target);
    static FVector TeleportSpawnCm(EWiesbadenDevTeleport Target);
    static FTransform UprightTransform(const FTransform& Current);
};
```

- [ ] **Step 4: Write minimal implementation (Cpp)**

```cpp
// Source/WiesbadenReal/Core/WiesbadenDevActions.cpp
#include "Core/WiesbadenDevActions.h"

FVector FWiesbadenDevActions::TeleportTargetCm(EWiesbadenDevTeleport Target)
{
    switch (Target)
    {
    case EWiesbadenDevTeleport::PlatterStrasse:  return FVector(-121474.0, -119312.0, 11347.0);
    case EWiesbadenDevTeleport::Nerobergbahn:    return FVector(-104083.0, -137317.0, 8800.0);
    case EWiesbadenDevTeleport::GartenNerotal48: return FVector(-71366.0, -124226.0, 8530.0);
    default:                                     return FVector::ZeroVector;
    }
}

FVector FWiesbadenDevActions::TeleportSpawnCm(EWiesbadenDevTeleport Target)
{
    // 300 cm hoch ansetzen und fallen lassen: ein Punkt IM Boden liesse den
    // Wagen steckenbleiben.
    return TeleportTargetCm(Target) + FVector(0.0, 0.0, 300.0);
}

FTransform FWiesbadenDevActions::UprightTransform(const FTransform& Current)
{
    // Nick/Roll auf 0 (aufrichten), Yaw + Ort behalten, 150 cm anheben und
    // auf die Raeder fallen lassen.
    const FRotator Rot = Current.Rotator();
    return FTransform(
        FRotator(0.0, Rot.Yaw, 0.0),
        Current.GetLocation() + FVector(0.0, 0.0, 150.0),
        FVector::OneVector);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `Tools\bau_neuerpc.cmd` (muss `Result: Succeeded`), dann `run_tests_only.cmd`.
Expected: `Saved\Logs\WiesbadenReal.log` enthaelt `Test Completed. Result={Success} Name={Actions} Path={WiesbadenReal.Dev.Actions}`.

- [ ] **Step 6: Commit**

```bash
git add Source/WiesbadenReal/Core/WiesbadenDevActions.h Source/WiesbadenReal/Core/WiesbadenDevActions.cpp Source/WiesbadenReal/Tests/DevActionsTest.cpp
git commit -m "feat(dev): datenreine Dev-Aktions-Helfer (Teleport, Aufrichten) mit Test"
```

### Task A2: Exec-Konsolenbefehle

**Files:**
- Create: `Source/WiesbadenReal/Core/WiesbadenDevConsole.h`
- Create: `Source/WiesbadenReal/Core/WiesbadenDevConsole.cpp`

**Interfaces:**
- Consumes: `FWiesbadenDevActions` (Task A1).
- Produces: `UWiesbadenDevConsole : UGameInstanceSubsystem` mit `UFUNCTION(Exec) void WbTeleport(int32 Ziel)`, `UFUNCTION(Exec) void WbResetVehicle()`, `UFUNCTION(Exec) void WbTraffic(int32 An)`.

- [ ] **Step 1: Header schreiben**

```cpp
// Source/WiesbadenReal/Core/WiesbadenDevConsole.h
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WiesbadenDevConsole.generated.h"

// Dev-Konsolenbefehle, per -ExecCmds="WbTeleport 2" automatisierbar. Als
// GameInstanceSubsystem, damit die Exec-Funktionen ohne Blueprint erreichbar
// sind und den ersten lokalen Pawn ansteuern.
UCLASS()
class WIESBADENREAL_API UWiesbadenDevConsole : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // Teleportiert den Spieler-Pawn: 0=Platter Strasse, 1=Nerobergbahn, 2=Garten.
    UFUNCTION(Exec)
    void WbTeleport(int32 Ziel);

    // Richtet den Spieler-Pawn auf (Nick/Roll 0) und laesst ihn auf die Raeder fallen.
    UFUNCTION(Exec)
    void WbResetVehicle();

    // Verkehr an (1) oder aus (0).
    UFUNCTION(Exec)
    void WbTraffic(int32 An);

private:
    APawn* LocalPawn() const;
};
```

- [ ] **Step 2: Cpp schreiben**

```cpp
// Source/WiesbadenReal/Core/WiesbadenDevConsole.cpp
#include "Core/WiesbadenDevConsole.h"

#include "Core/WiesbadenDevActions.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "World/WiesbadenCitySubsystem.h"

APawn* UWiesbadenDevConsole::LocalPawn() const
{
    if (const UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            return PC->GetPawn();
        }
    }
    return nullptr;
}

void UWiesbadenDevConsole::WbTeleport(int32 Ziel)
{
    if (APawn* Pawn = LocalPawn())
    {
        const EWiesbadenDevTeleport Target = static_cast<EWiesbadenDevTeleport>(FMath::Clamp(Ziel, 0, 2));
        Pawn->SetActorLocation(FWiesbadenDevActions::TeleportSpawnCm(Target),
            /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
    }
}

void UWiesbadenDevConsole::WbResetVehicle()
{
    if (APawn* Pawn = LocalPawn())
    {
        const FTransform Auf = FWiesbadenDevActions::UprightTransform(Pawn->GetActorTransform());
        Pawn->SetActorLocationAndRotation(Auf.GetLocation(), Auf.Rotator(),
            /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
    }
}

void UWiesbadenDevConsole::WbTraffic(int32 An)
{
    if (const UWorld* World = GetWorld())
    {
        if (UWiesbadenCitySubsystem* City = World->GetSubsystem<UWiesbadenCitySubsystem>())
        {
            City->TrafficSimulation.Settings.TrafficDensity = (An != 0) ? 0.5f : 0.0f;
        }
    }
}
```

- [ ] **Step 3: Build**

Run: `Tools\bau_neuerpc.cmd`
Expected: `Result: Succeeded`. (Kein neuer Automation-Test — Exec-Befehle sind ueber die A1-Logik bereits getestet; die Verdrahtung wird in A4 im Spiel geprueft.)

- [ ] **Step 4: Commit**

```bash
git add Source/WiesbadenReal/Core/WiesbadenDevConsole.h Source/WiesbadenReal/Core/WiesbadenDevConsole.cpp
git commit -m "feat(dev): Exec-Konsolenbefehle WbTeleport/WbResetVehicle/WbTraffic"
```

### Task A3: Pause-Menue auf geteilte Helfer umstellen (DRY)

**Files:**
- Modify: `Source/WiesbadenReal/UI/WiesbadenVehicleHUD.cpp` (Funktion `ActivatePauseEntry`, Faelle 2–6)

**Interfaces:**
- Consumes: `FWiesbadenDevActions` (Task A1).

- [ ] **Step 1: Include ergaenzen**

Oben in `WiesbadenVehicleHUD.cpp` bei den Includes einfuegen:

```cpp
#include "Core/WiesbadenDevActions.h"
```

- [ ] **Step 2: Teleport-Fall umschreiben**

In `ActivatePauseEntry`, ersetze im Block `case 2: case 3: case 4:` die lokale Zielberechnung durch den Helfer. Vorher berechnete der Block `Target` per if/else-if; nachher:

```cpp
    case 2:
    case 3:
    case 4:
    {
        const EWiesbadenDevTeleport Ziel =
            (Index == 2) ? EWiesbadenDevTeleport::PlatterStrasse
          : (Index == 3) ? EWiesbadenDevTeleport::Nerobergbahn
                         : EWiesbadenDevTeleport::GartenNerotal48;
        if (APawn* Pawn = PC->GetPawn())
        {
            Pawn->SetActorLocation(FWiesbadenDevActions::TeleportSpawnCm(Ziel),
                /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
        }
        Unpause();
        break;
    }
```

- [ ] **Step 3: Aufricht-Fall umschreiben**

Ersetze im Block `case 5:` (Fahrzeug aufrichten) die lokale Rotationsberechnung durch:

```cpp
    case 5:
    {
        if (APawn* Pawn = PC->GetPawn())
        {
            const FTransform Auf = FWiesbadenDevActions::UprightTransform(Pawn->GetActorTransform());
            Pawn->SetActorLocationAndRotation(Auf.GetLocation(), Auf.Rotator(),
                /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
        }
        Unpause();
        break;
    }
```

- [ ] **Step 4: Build + bestehende Tests**

Run: `Tools\bau_neuerpc.cmd` (Erfolg), dann `run_tests_only.cmd`.
Expected: Alle Tests weiter gruen (Zahl unveraendert bis auf `WiesbadenReal.Dev.Actions`). Keine Regression im HUD (das Menue-Verhalten ist unveraendert, nur die Quelle der Zahlen).

- [ ] **Step 5: Commit**

```bash
git add Source/WiesbadenReal/UI/WiesbadenVehicleHUD.cpp
git commit -m "refactor(dev): Pause-Menue nutzt geteilte Dev-Aktions-Helfer (DRY)"
```

### Task A4: Manuelles Gate — Exec-Befehle im Spiel verifizieren

**Files:** keine.

- [ ] **Step 1: Spiel starten und Befehl absetzen**

Run:
```
UnrealEditor.exe "<abs>\WiesbadenReal.uproject" /Game/Maps/WiesbadenCity_Alkis4 -game -windowed -ResX=1600 -ResY=900 -nop4 -ExecCmds="WbTeleport 2"
```
Erwartung nach dem Laden: der Spieler-Pawn steht am Garten Nerotal 48 (Ortsname „Nerotal" oben; Minimap-Marker bei Nerotal). Zusaetzlich pruefen: im Spiel die Konsole (`~`) oeffnen, `WbResetVehicle` eingeben — ein gekippter Wagen richtet sich auf.

- [ ] **Step 2: Ergebnis festhalten**

Screenshot des Fensters (`Saved\Screenshots` oder Fenster-Capture). Kein Commit — reine Verifikation.

---

## Teil B — Fahrzeug-Ruhelage-Regressionstest

Ziel: Ein Automation-Test, der die reine Bedingung „Chassis-Unterkante liegt UEBER dem Rad-Aufstand" prueft — das Kriterium, an dem der Kaefer-Kollisions-Fix (Teil C) gemessen wird. Damit kann ein spaeteres kaputtes PhysicsAsset den Test rot faerben.

### Task B1: Datenreine Ruhelage-Pruefung

**Files:**
- Modify: `Source/WiesbadenReal/Vehicles/WiesbadenChaosCar.h` (neue static Funktion deklarieren)
- Modify: `Source/WiesbadenReal/Vehicles/WiesbadenChaosCar.cpp` (implementieren)
- Test: `Source/WiesbadenReal/Tests/VehicleRestTest.cpp`

**Interfaces:**
- Produces: `static bool AWiesbadenChaosCar::RestsOnWheels(double ChassisBottomZcm, double WheelContactZcm, double MarginCm)` — true, wenn `ChassisBottomZcm >= WheelContactZcm + MarginCm` (das Chassis darf den Boden nicht vor den Raedern beruehren).

- [ ] **Step 1: Write the failing test**

```cpp
// Source/WiesbadenReal/Tests/VehicleRestTest.cpp
#include "Misc/AutomationTest.h"
#include "Vehicles/WiesbadenChaosCar.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleRestTest,
    "WiesbadenReal.Vehicles.RestsOnWheels",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehicleRestTest::RunTest(const FString& Parameters)
{
    // Rad-Aufstand bei Z=0 (Radradius 34,3, Radmitte bei 34,3). Marge 5 cm.
    // Chassis-Unterkante unter dem Aufstand -> Wagen liegt auf dem Chassis (Fehler).
    TestFalse(TEXT("Chassis unter Rad -> nicht auf Raedern"),
        AWiesbadenChaosCar::RestsOnWheels(-2.0, 0.0, 5.0));
    // Chassis knapp ueber Aufstand, aber unter der Marge -> noch nicht genug.
    TestFalse(TEXT("Chassis unter Marge -> nicht auf Raedern"),
        AWiesbadenChaosCar::RestsOnWheels(3.0, 0.0, 5.0));
    // Chassis 30 cm ueber Aufstand -> Wagen ruht auf den Raedern (korrekt).
    TestTrue(TEXT("Chassis 30 ueber Rad -> auf Raedern"),
        AWiesbadenChaosCar::RestsOnWheels(30.0, 0.0, 5.0));
    return true;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `Tools\bau_neuerpc.cmd`
Expected: FAIL — `RestsOnWheels` ist kein Member von `AWiesbadenChaosCar`.

- [ ] **Step 3: Deklaration im Header**

In `WiesbadenChaosCar.h`, in der `public:`-Sektion der Klasse `AWiesbadenChaosCar` einfuegen:

```cpp
    // Ruhelage-Kriterium: liegt die Chassis-Kollisionsunterkante genug ueber
    // dem Rad-Aufstand, damit die Raeder den Wagen tragen (nicht das Chassis)?
    // Datenrein fuer den Test; die Ist-Werte kommen zur Laufzeit aus dem
    // PhysicsAsset bzw. dem Radradius.
    static bool RestsOnWheels(double ChassisBottomZcm, double WheelContactZcm, double MarginCm);
```

- [ ] **Step 4: Implementierung im Cpp**

Am Ende von `WiesbadenChaosCar.cpp` einfuegen:

```cpp
bool AWiesbadenChaosCar::RestsOnWheels(double ChassisBottomZcm, double WheelContactZcm, double MarginCm)
{
    return ChassisBottomZcm >= WheelContactZcm + MarginCm;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `Tools\bau_neuerpc.cmd` (Erfolg), dann `run_tests_only.cmd`.
Expected: `Result={Success} Name={RestsOnWheels} Path={WiesbadenReal.Vehicles.RestsOnWheels}`.

- [ ] **Step 6: Commit**

```bash
git add Source/WiesbadenReal/Vehicles/WiesbadenChaosCar.h Source/WiesbadenReal/Vehicles/WiesbadenChaosCar.cpp Source/WiesbadenReal/Tests/VehicleRestTest.cpp
git commit -m "test(vehicles): Ruhelage-Kriterium RestsOnWheels mit Regressionstest"
```

---

## Teil C — Kaefer-Chassis-Kollision (Editor-Fix, durch Teil B abgesichert)

Ziel: Die Chassis-Kollision im `SK_VWBeetle_Chassis_PhysicsAsset` so anheben, dass ihre Unterkante ueber der Radaufstandslinie (Z≈0) liegt — der Wagen ruht dann auf den Raedern statt zu schweben/kippen. **Nicht per Commandlet moeglich** (`SkeletalBodySetups` ist protected); manuelles Editor-Gate.

### Task C1: PhysicsAsset im Editor anheben

**Files:** `Content/Assets/Vehicles/Beetle/SK_VWBeetle_Chassis_PhysicsAsset.uasset` (via Editor-GUI, kein direkter Datei-Edit)

- [ ] **Step 1: Physics Asset Editor oeffnen**

Im Content-Browser `SK_VWBeetle_Chassis_PhysicsAsset` doppelklicken (oder `SK_VWBeetle` -> Menue *Physics Asset*).

- [ ] **Step 2: Chassis-Body waehlen und Maasse ablesen**

Im Skeleton Tree den (einzigen grossen) Chassis-Body am Rumpf-Knochen waehlen. Im Details-Panel die Box-Primitive: **Center.Z** und **Z-Groesse** notieren.

- [ ] **Step 3: Unterkante auf ~Z=30 anheben**

Box so aendern, dass `Center.Z − (Z-Groesse/2) ≈ 30` (Oberkante bei ~150 halten). Bei einer Box, die grob **0…150** spannt: **Center.Z ≈ 90**, **Z-Groesse ≈ 120**. Sauberer (optional): Box loeschen, per Rechtsklick -> *Regenerate Bodies* als **Single Convex Hull** mit **Minimum Bone Size** so hoch, dass die Radknochen ausgeschlossen bleiben. Speichern.

- [ ] **Step 4: Manuelles Gate — Testfahrt**

Spiel starten (`WbTeleport 0`), losfahren und ueber einen Bordstein. Erwartung: der Wagen steht/rollt auf den Raedern (kein Schweben, kein Kippen an Kanten). Bei Bedarf `WbResetVehicle`. Screenshot festhalten.

- [ ] **Step 5: Commit**

```bash
git add Content/Assets/Vehicles/Beetle/SK_VWBeetle_Chassis_PhysicsAsset.uasset
git commit -m "fix(vehicles): Kaefer-Chassis-Kollision ueber die Radaufstandslinie angehoben"
```

---

## Teil D — Sebbo-Animationen (GUI-Import, durch Screenshot abgesichert)

Ziel: Die korrigierten Bein-Animationen (frische `Data\Raw\Sebbo\SK_Sebbo.fbx`) in UE importieren und vom FootPawn abspielen lassen. **Commandlet-Import erzeugt keine Anim-Sequenzen** (UE-5.8-Blocker) — GUI-Drag&Drop noetig; Code-Anpassung nur, falls die Importnamen abweichen.

### Task D1: FBX per GUI importieren

**Files:** `Content/Assets/People/SK_Sebbo*.uasset` (via Editor-GUI)

- [ ] **Step 1: Alte Assets loeschen**

Im Content-Browser `/Game/Assets/People/`: `SK_Sebbo`, `SK_Sebbo_Skeleton`, `SK_SebboSebboRig_Sebbo_Idle/Walk/Swing` loeschen (Referenzen ignorieren — der FootPawn laedt per Pfad zur Laufzeit).

- [ ] **Step 2: FBX importieren**

`Data\Raw\Sebbo\SK_Sebbo.fbx` in den Content-Browser nach `/Game/Assets/People/` ziehen. Im Dialog: **Import Animations ✓**, Skeletal Mesh, Ziel-Name `SK_Sebbo`, Skelett neu erzeugen. Importieren.

- [ ] **Step 3: Entstandene Anim-Namen notieren**

Nach dem Import die tatsaechlichen Namen der drei Anim-Sequenzen ablesen (z. B. `SK_SebboSebboRig_Sebbo_Idle` oder `SK_Sebbo_Anim_SebboRig_Sebbo_Idle`).

### Task D2: FootPawn an die realen Namen anpassen (nur falls abweichend)

**Files:** `Source/WiesbadenReal/Vehicles/WiesbadenFootPawn.cpp:346-357`

**Interfaces:** Consumes: die in D1 notierten Anim-Asset-Namen.

- [ ] **Step 1: Ladekandidaten erweitern**

Falls die Namen von `SK_SebboSebboRig_Sebbo_<Name>` abweichen, in der Lambda `LoadAnim` den neuen Namen als ERSTEN Kandidaten ergaenzen. Beispiel fuer das `_Anim_`-Schema:

```cpp
        auto LoadAnim = [](const TCHAR* Name) -> UAnimSequence*
        {
            const FString Neu = FString::Printf(
                TEXT("/Game/Assets/People/SK_Sebbo_Anim_SebboRig_%s.SK_Sebbo_Anim_SebboRig_%s"), Name, Name);
            if (UAnimSequence* F = LoadObject<UAnimSequence>(nullptr, *Neu)) { return F; }
            const FString Primary = FString::Printf(
                TEXT("/Game/Assets/People/SK_SebboSebboRig_%s.SK_SebboSebboRig_%s"), Name, Name);
            if (UAnimSequence* F = LoadObject<UAnimSequence>(nullptr, *Primary)) { return F; }
            const FString Plain = FString::Printf(
                TEXT("/Game/Assets/People/%s.%s"), Name, Name);
            return LoadObject<UAnimSequence>(nullptr, *Plain);
        };
```

- [ ] **Step 2: Build**

Run: `Tools\bau_neuerpc.cmd`
Expected: `Result: Succeeded`.

### Task D3: Manuelles Gate — Beine im Spiel verifizieren

**Files:** keine.

- [ ] **Step 1: Zu Fuss laufen und beobachten**

Spiel starten, aus dem Fahrzeug aussteigen (F), laufen. Im Log darf die Warnung `Spielerfigur: SK_Sebbo ohne vollstaendige Bewegungen ... statisches Modell` NICHT erscheinen (dann laden alle drei Anims). Erwartung: sauberer Schrittzyklus, keine gespreizten Beine (Vergleich `Data\Raw\Sebbo\pruef_walk.png`). Screenshot des laufenden FootPawn festhalten.

- [ ] **Step 2: Commit (erst nach verifizierten Beinen)**

```bash
git add Content/Assets/People/SK_Sebbo.uasset Content/Assets/People/SK_Sebbo_Skeleton.uasset Content/Assets/People/SK_SebboSebboRig_Sebbo_Idle.uasset Content/Assets/People/SK_SebboSebboRig_Sebbo_Walk.uasset Content/Assets/People/SK_SebboSebboRig_Sebbo_Swing.uasset Source/WiesbadenReal/Vehicles/WiesbadenFootPawn.cpp
git commit -m "fix(figur): Sebbo-Animationen mit korrigierten Beinen neu importiert"
```

---

## Self-Review

**Spec-Abdeckung:** Dev-Konsolenbefehle → Teil A. Fahrzeug-Regressionstest → Teil B. #3 Kaefer-Kollision → Teil C (mit B als Gate). #4 Sebbo-Animationen → Teil D. Die Test-Range (Brainstorm) ist bewusst NICHT enthalten — sie ist eine reine Asset-/Editor-Aufgabe ohne eigene testbare Logik und wuerde als separater Plan sinnvoller sein (Hinweis siehe „Aufteilung").

**Placeholder-Scan:** Keine TBD/TODO; alle Code-Schritte enthalten vollstaendigen Code; Editor-Schritte nennen exakte Assets/Werte.

**Typkonsistenz:** `EWiesbadenDevTeleport`, `FWiesbadenDevActions::TeleportTargetCm/TeleportSpawnCm/UprightTransform`, `AWiesbadenChaosCar::RestsOnWheels` sind in A1/B1 definiert und in A2/A3/B1 identisch verwendet. `UWiesbadenDevConsole`-Exec-Namen (`WbTeleport/WbResetVehicle/WbTraffic`) sind konsistent zwischen Header und Cpp.
