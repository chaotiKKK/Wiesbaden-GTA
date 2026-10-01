# Stadt-Audiomischung Teil 1: Fussschritte — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Spielerfigur und Passanten treffen beim Gehen hörbar den richtigen Boden — Asphalt, Pflaster, Wiese und Innenraum klingen verschieden.

**Architecture:** Eine reine Namespace-Funktion `WiesbadenAudioZones` loest Materialname → Oberfläche auf (headless testbar, kein UObject-Zugriff). Ein `UWiesbadenAudioZonesSubsystem` (UTickableWorldSubsystem) haelt die Audio-Komponenten und beantwortet die Frage „welcher Klang an dieser Position". Die Spielerfigur fragt es je Schrittzyklus; die Passanten bekommen einen Pool, weil sie ISM-Instanzen sind und keine Actors.

**Tech Stack:** Unreal Engine 5.8, C++20, UHT-Reflection, MetaSound, `IMPLEMENT_SIMPLE_AUTOMATION_TEST`

**Spec:** `docs/superpowers/specs/2026-09-27-stadt-audiomischung-design.md` (Teil 1)

## Global Constraints

- Code **strikt ASCII** — Umlaute als `ae`/`oe`/`ue`/`ss` in Bezeichnern, Kommentaren und Strings
- Kommentare, Doc-Kommentare und Log-Zeilen auf **Deutsch**
- `bUseUnity = false` ist gesetzt: jede `.cpp` ist eine eigene Übersetzungseinheit. Anonyme Namespaces verschiedener Dateien werden **nicht** verschmolzen — gleichnamige dateilokale Helfer sind trotzdem zu vermeiden
- Neue UObjects brauchen `UPROPERTY` + `TObjectPtr`, sonst sammelt der GC sie weg
- `generated.h` ist **immer** der letzte Include im Header
- Fehlende Assets bleiben **still**: loggen einmal, nicht crashen, weiterrechnen
- Python-Prints erreichen den Cmdlet-Stream nicht — Belege gehen nach `Saved/Logs/`
- Bildvergleiche nur über **einen** Build mit A/B-Schalter, nie über verschiedene Läufe
- `MetasoundStandardNodes` ist bereits in `WiesbadenReal.Build.cs` als private Abhängigkeit — für neue MetaSound-Graphen ist **keine** Build.cs-Änderung nötig
- Zeilenenden LF (`.gitattributes`)

---

### Task 1: Oberflächenauflösung aus dem Materialnamen

Die reine Mathematik. Kein UObject, kein World-Zugriff — dadurch headless testbar.

**Files:**
- Create: `Source/WiesbadenReal/Audio/WiesbadenAudioZones.h`
- Create: `Source/WiesbadenReal/Audio/WiesbadenAudioZones.cpp`
- Test: `Source/WiesbadenReal/Tests/AudioZonesTest.cpp`

**Interfaces:**
- Consumes: nichts (erste Aufgabe im Teilprojekt)
- Produces:
  - `enum class EWbFootstepSurface : uint8 { Asphalt, Pflaster, Wiese, Innenraum, MAX Hidden }`
  - `EWbFootstepSurface SurfaceFromMaterialName(const FString& MaterialName)`
  - `float BandpassHzForSurface(EWbFootstepSurface Surface)`
  - `FString SurfaceName(EWbFootstepSurface Surface)`

- [ ] **Step 1: Die Dateien anlegen, Test zuerst**

`Source/WiesbadenReal/Audio/WiesbadenAudioZones.h`:

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Untergrund eines Fussschritts - bestimmt, welcher Klang und welcher
 * Filterbereich benutzt wird.
 *
 * KEINE Reflection, bewusst: die Aufloesung ist reine Mathematik ueber einen
 * Materialnamen und muss headless testbar sein. Erst die Engine-Kopplung
 * (UWiesbadenAudioZonesSubsystem) liest echte UMaterialInterface.
 */
enum class EWbFootstepSurface : uint8
{
	/** Fahrbahn, Gehweg: hell und kurz. */
	Asphalt,

	/** Bordstein, Pflaster, Plätze: mittel, mit Ausschlag. */
	Pflaster,

	/** Wiese, Waldboden, Beete: dumpf und weich. */
	Wiese,

	/** Innenraum (Fussboden im Turm, Laden): trocken und sehr kurz. */
	Innenraum,

	MAX UMETA(Hidden)
};

/**
 * Reine Zuordnung Ort -> Klang. Kein UObject-Zugriff, damit die Zuordnung
 * headless unit-testbar bleibt.
 */
namespace WiesbadenAudioZones
{
	/**
	 * Untergrund aus dem Materialnamen.
	 *
	 * Dasselbe Muster wie AWiesbadenCityChunk::BuildingUseFromMaterialName: die
	 * Nutzung steckt im Materialnamen der Section, und der Name ist auf der
	 * gebackenen Karte serialisiert - deshalb funktioniert die Aufloesung auch
	 * ohne FWiesbadenCityData (die es dort nicht gibt).
	 *
	 * Unbekannt oder leer -> Pflaster. Der gangbare Default, kein Fehler.
	 */
	EWbFootstepSurface SurfaceFromMaterialName(const FString& MaterialName);

	/** Bandpass-Mitte in Hz je Oberflaeche (der synthetische Schritt, Task 3). */
	float BandpassHzForSurface(EWbFootstepSurface Surface);

	/** Deutscher Name fuer Log-Zeilen und Tests. */
	FString SurfaceName(EWbFootstepSurface Surface);
}
```

- [ ] **Step 2: Der failende Test**

`Source/WiesbadenReal/Tests/AudioZonesTest.cpp`:

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Audio/WiesbadenAudioZones.h"

/**
 * Test der Untergrund-Aufloesung. Deckt die reale Namensliste der
 * Stadtmaterialien ab UND die Sackgassen (leer, unbekannt, MAX).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioZonesSurfaceTest,
	"WiesbadenReal.Audio.Footsteps.Surface",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAudioZonesSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenAudioZones;

	// Reale Materialnamen aus Content/Materials/City (Stand 2026-09-27).
	TestEqual(TEXT("MI_WbFahrbahn_Asphalt -> Asphalt"),
		SurfaceFromMaterialName(TEXT("MI_WbFahrbahn_Asphalt")), EWbFootstepSurface::Asphalt);
	TestEqual(TEXT("MI_WbFahrbahn_Asphalt_Alt -> Asphalt"),
		SurfaceFromMaterialName(TEXT("MI_WbFahrbahn_Asphalt_Alt")), EWbFootstepSurface::Asphalt);
	TestEqual(TEXT("MI_WbGehweg_Pflaster -> Pflaster"),
		SurfaceFromMaterialName(TEXT("MI_WbGehweg_Pflaster")), EWbFootstepSurface::Pflaster);
	TestEqual(TEXT("MI_WbGehweg_Platten -> Pflaster"),
		SurfaceFromMaterialName(TEXT("MI_WbGehweg_Platten")), EWbFootstepSurface::Pflaster);
	TestEqual(TEXT("MI_WbPlatz_Pflaster -> Pflaster"),
		SurfaceFromMaterialName(TEXT("MI_WbPlatz_Pflaster")), EWbFootstepSurface::Pflaster);
	TestEqual(TEXT("MI_WbKerb -> Pflaster"),
		SurfaceFromMaterialName(TEXT("MI_WbKerb")), EWbFootstepSurface::Pflaster);
	TestEqual(TEXT("MI_WbGelaende_Gras -> Wiese"),
		SurfaceFromMaterialName(TEXT("MI_WbGelaende_Gras")), EWbFootstepSurface::Wiese);
	TestEqual(TEXT("MI_WbGelaende_Wiese -> Wiese"),
		SurfaceFromMaterialName(TEXT("MI_WbGelaende_Wiese")), EWbFootstepSurface::Wiese);
	TestEqual(TEXT("M_AAA_GroundDirt -> Wiese"),
		SurfaceFromMaterialName(TEXT("M_AAA_GroundDirt")), EWbFootstepSurface::Wiese);
	TestEqual(TEXT("M_AAA_TerrainGrass -> Wiese"),
		SurfaceFromMaterialName(TEXT("M_AAA_TerrainGrass")), EWbFootstepSurface::Wiese);

	// Sackgassen: Default statt Fehler.
	TestEqual(TEXT("leerer Name -> Pflaster"),
		SurfaceFromMaterialName(TEXT("")), EWbFootstepSurface::Pflaster);
	TestEqual(TEXT("unbekannter Name -> Pflaster"),
		SurfaceFromMaterialName(TEXT("M_Irgendwas"), EWbFootstepSurface::Pflaster);
	// Reihenfolge ist Absicht: Wiese vor Asphalt, sonst schluckt "Asphalt"
	// nichts - aber ein Material wie "M_AAA_GroundDirt_AsphaltVariation"
	// darf nicht als Asphalt gelten, wenn Gras im Namen steht.
	TestEqual(TEXT("Gras schlaegt Asphalt im Mischnamen"),
		SurfaceFromMaterialName(TEXT("M_WbGelaende_Gras_AsphaltRand")), EWbFootstepSurface::Wiese);

	// Bandpass: vier verschiedene Frequenzen, monoton fallend nach Weichheit.
	const float Asphalt = BandpassHzForSurface(EWbFootstepSurface::Asphalt);
	const float Pflaster = BandpassHzForSurface(EWbFootstepSurface::Pflaster);
	const float Wiese = BandpassHzForSurface(EWbFootstepSurface::Wiese);
	const float Innen = BandpassHzForSurface(EWbFootstepSurface::Innenraum);
	TestTrue(TEXT("Asphalt heller als Pflaster"), Asphalt > Pflaster);
	TestTrue(TEXT("Pflaster heller als Wiese"), Pflaster > Wiese);
	TestTrue(TEXT("alle Bandbreiten im hörbaren Bereich"),
		Asphalt >= 200.0f && Asphalt <= 8000.0f && Innen >= 200.0f && Innen <= 8000.0f);

	// MAX und Ausreisser: kein Absturz, kein Unsinn.
	TestEqual(TEXT("MAX -> Pflaster-Bandpass"),
		BandpassHzForSurface(EWbFootstepSurface::MAX), BandpassHzForSurface(EWbFootstepSurface::Pflaster));
	TestTrue(TEXT("SurfaceName liefert Text"), !SurfaceName(EWbFootstepSurface::Asphalt).IsEmpty());
	TestTrue(TEXT("SurfaceName(MAX) faellt nicht zurueck auf leer"),
		!SurfaceName(EWbFootstepSurface::MAX).IsEmpty());

	return true;
}
```

- [ ] **Step 3: Test laufen lassen — er muss fehlschlagen**

Der Test braucht einen Build. Der schnellste Weg ist der Editor-Target-Build:

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" WiesbadenRealEditor Win64 Development -project="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" -waitmutex
```

Expected: Übersetzungsfehler, weil `WiesbadenAudioZones.h` noch nicht existiert. Das ist der gewollte rote Zustand.

**Wichtig:** Dieser Build nimmt den Engine-Lock. Vorher prüfen, ob ein anderer Lauf ihn hält (fremder Thread). Bei "Live coding active" oder gesperrter DLL: nicht debuggen, den eigenen Lauf identifizieren, sonst blockiert der nächste Build.

- [ ] **Step 4: Die Implementierung**

`Source/WiesbadenReal/Audio/WiesbadenAudioZones.cpp`:

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WiesbadenAudioZones.h"

EWbFootstepSurface WiesbadenAudioZones::SurfaceFromMaterialName(const FString& MaterialName)
{
	// Reihenfolge = Prioritaet. Gras steht vor Asphalt, weil ein gemischter
	// Name wie "M_WbGelaende_Gras_AsphaltRand" sonst am Asphalt klebenbleibt.
	//
	// Wiese zuerst.
	if (MaterialName.Contains(TEXT("Gras"))
		|| MaterialName.Contains(TEXT("Wiese"))
		|| MaterialName.Contains(TEXT("Ground"))
		|| MaterialName.Contains(TEXT("Terrain"))
		|| MaterialName.Contains(TEXT("Wald")))
	{
		return EWbFootstepSurface::Wiese;
	}

	// Innenraum vor Asphalt: "Indoor"/"Boden" schlaegt sonst nicht, aber
	// "M_AAA_Floor_Asphalt" waere sonst Asphalt.
	if (MaterialName.Contains(TEXT("Indoor"))
		|| MaterialName.Contains(TEXT("Floor"))
		|| MaterialName.Contains(TEXT("Interior")))
	{
		return EWbFootstepSurface::Innenraum;
	}

	// Asphalt: Fahrbahn, Gehweg sind in diesem Projekt Asphalt, ausser sie
	// tragen Pflaster im Namen (vorher geprueft).
	if (MaterialName.Contains(TEXT("Asphalt"))
		|| MaterialName.Contains(TEXT("Fahrbahn")))
	{
		return EWbFootstepSurface::Asphalt;
	}

	// Pflaster: Bordstein, Platten, Platz, Gehweg, Stein.
	if (MaterialName.Contains(TEXT("Pflaster"))
		|| MaterialName.Contains(TEXT("Platten"))
		|| MaterialName.Contains(TEXT("Platz"))
		|| MaterialName.Contains(TEXT("Gehweg"))
		|| MaterialName.Contains(TEXT("Kerb"))
		|| MaterialName.Contains(TEXT("Bordstein"))
		|| MaterialName.Contains(TEXT("Stein"))
		|| MaterialName.Contains(TEXT("Paving")))
	{
		return EWbFootstepSurface::Pflaster;
	}

	return EWbFootstepSurface::Pflaster;
}

float WiesbadenAudioZones::BandpassHzForSurface(EWbFootstepSurface Surface)
{
	switch (Surface)
	{
	case EWbFootstepSurface::Asphalt:   return 2200.0f;
	case EWbFootstepSurface::Pflaster:  return 1400.0f;
	case EWbFootstepSurface::Wiese:     return 620.0f;
	case EWbFootstepSurface::Innenraum: return 900.0f;
	default:                            return 1400.0f;
	}
}

FString WiesbadenAudioZones::SurfaceName(EWbFootstepSurface Surface)
{
	switch (Surface)
	{
	case EWbFootstepSurface::Asphalt:   return TEXT("Asphalt");
	case EWbFootstepSurface::Pflaster:  return TEXT("Pflaster");
	case EWbFootstepSurface::Wiese:     return TEXT("Wiese");
	case EWbFootstepSurface::Innenraum: return TEXT("Innenraum");
	default:                            return TEXT("Pflaster");
	}
}
```

- [ ] **Step 5: Build + Test grün**

Denselben Build erneut. Expected: Build OK.

Die Automation-Tests laufen **nicht** im Editor-Build allein. Sie fährt der Editor mit dem Test-Namen:

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" -ExecCmds="Automation RunTests WiesbadenReal.Audio.Footsteps.Surface; Quit" -unattended -nop4 -nosplash -nullrhi -abslog="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\test_fussschritte.log"
```

Danach `Saved/Logs/test_fussschritte.log` nach `Result={Success}` bzw. `Test Completed` prüfen. **Nicht** stdout lesen — `-stdout` verschluckt Projektzeilen.

- [ ] **Step 6: Sabotage-Gegenprobe**

Der Test muss **anschlagen**, wenn die Auflösung kaputt ist. `SurfaceFromMaterialName` so umbauen, dass Wiese **vor** Asphalt geprüft wird und der Mischname-Test aus Task 1 (`M_WbGelaende_Gras_AsphaltRand`) jetzt Asphalt liefert:

```cpp
	// SABOTAGE: Asphalt vor Gras verschoben.
	if (MaterialName.Contains(TEXT("Asphalt"))) { return EWbFootstepSurface::Asphalt; }
```

Test erneut laufen lassen. Expected: `Gras schlaegt Asphalt im Mischnamen` **schlägt fehl**. Wenn er grün bleibt, prüfen ob der Test diese Zeile überhaupt erreicht — das ist in dieser Session schon passiert.

Danach **wieder zurückbauen** und den grünen Zustand bestätigen.

- [ ] **Step 7: Commit**

```bash
git add Source/WiesbadenReal/Audio/WiesbadenAudioZones.h Source/WiesbadenReal/Audio/WiesbadenAudioZones.cpp Source/WiesbadenReal/Tests/AudioZonesTest.cpp
git commit -m "Fussschritte: Untergrund aus dem Materialnamen aufloesen

Die Aufloesung sitzt im Materialnamen, weil der auf der gebackenen Karte
serialisiert ist - dort gibt es keine FWiesbadenCityData. Gras wird vor
Asphalt geprueft, damit ein Mischname nicht am Asphalt klebt."
```

---

### Task 2: Das Subsystem — Ort zu Klang

**Files:**
- Create: `Source/WiesbadenReal/Audio/WiesbadenAudioZonesSubsystem.h`
- Create: `Source/WiesbadenReal/Audio/WiesbadenAudioZonesSubsystem.cpp`
- Test: `Source/WiesbadenReal/Tests/AudioZonesSubsystemTest.cpp`

**Interfaces:**
- Consumes: `WiesbadenAudioZones::SurfaceFromMaterialName`, `SurfaceName`, `BandpassHzForSurface` aus Task 1
- Produces:
  - `UCLASS() UWiesbadenAudioZonesSubsystem : public UTickableWorldSubsystem`
  - `void EnsureRig()` — legt Betten und Schritt-Pool an
  - `bool PlayFootstepAt(const FVector& Location, EWbFootstepSurface Surface)` — legt einen Schritt ab, false wenn Pool voll
  - `EWbFootstepSurface SurfaceUnderFoot(const FVector& Location)` — Strahl nach unten, liest das Material
  - `int32 GetPlayedFootstepCount() const` — für den Beleg
  - `void SetFootstepsEnabled(bool)` — der A/B-Schalter `-WbNoFootsteps`
  - `int32 GetActiveStepCount() const` — Poolfüllstand

- [ ] **Step 1: Der Header**

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Audio/WiesbadenAudioZones.h"
#include "WiesbadenAudioZonesSubsystem.generated.h"

class UAudioComponent;

/**
 * Ort -> Klang. Die einzige Stelle, die weiss, welcher Ton an welcher
 * Position ertönt.
 *
 * Drei Konsumenten, alle fragen nur hier: der Fuss-Pawn (Task 4), der
 * Passanten-Pool (Task 5) und später UWiesbadenAmbienceSubsystem (Teil 3a).
 *
 * Fehlende Assets bleiben still: loggt einmal und rechnet weiter. Genau das
 * haelt die bestehenden Audio-Subsysteme robust.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenAudioZonesSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	/** Legt Betten und Schritt-Pool an. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void EnsureRig();

	/**
	 * Untergrund unter einer Position: Strahl nach unten, Materialname des
	 * Treffers. Kein Treffer -> Pflaster, kein Fehler.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Audio")
	EWbFootstepSurface SurfaceUnderFoot(const FVector& Location);

	/**
	 * Einen Fussschritt an der Position ablegen.
	 * @return false, wenn der Pool voll ist (dann wird nichts gehoert).
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	bool PlayFootstepAt(const FVector& Location, EWbFootstepSurface Surface);

	/** Schalter fuer den A/B-Vergleich im selben Build. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Audio")
	void SetFootstepsEnabled(bool bEnabled)
	{
		bFootstepsEnabled = bEnabled;
	}

	/** Zahl der abgelegten Schritte seit BeginPlay - der Laufbeleg. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Audio")
	int32 GetPlayedFootstepCount() const { return PlayedFootsteps; }

	/** Aktuell belegte Plaetze im Pool. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Audio")
	int32 GetActiveStepCount() const;

private:
	/** Hoechstens so viele Schritte gleichzeitig; darueber wird verworfen. */
	static constexpr int32 MaxConcurrentSteps = 6;

	/** Mindestabstand je Passant, damit eine Innenstadt nicht knallt. */
	static constexpr float MinStepDistanceCm = 800.0f;

	/** Ring-Radius des abklingenden Pools um den Ort (cm). */
	static constexpr float StepPoolRadiusCm = 1800.0f;

	/** Abstand unter dem Fuss, ab dem der Materialstrahl scharf schaltet. */
	UPROPERTY()
	TArray<TObjectPtr<UAudioComponent>> StepPool;

	UPROPERTY()
	int32 PlayedFootsteps = 0;

	/** Welcher Poolplatz zuletzt frei wurde (Ringer). */
	int32 NextStepSlot = 0;

	bool bRigReady = false;
	bool bFootstepsEnabled = true;
	bool bWarnedMissingBeds = false;
};
```

- [ ] **Step 2: Der failgende Test**

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Audio/WiesbadenAudioZonesSubsystem.h"

/**
 * Test des Schritt-Pools. Die Kernregel ist die BEGRENZUNG: eine belebte
 * Innenstadt darf nicht wie Feuerwerk klingen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioZonesPoolTest,
	"WiesbadenReal.Audio.Footsteps.Pool",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAudioZonesPoolTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		AddError(TEXT("Testwelt nicht erzeugt"));
		return false;
	}

	UWiesbadenAudioZonesSubsystem* Zones =
		NewObject<UWiesbadenAudioZonesSubsystem>(World);
	Zones->EnsureRig();

	TestNotNull(TEXT("Subsystem erzeugt"), Zones);

	// 6 gleichzeitige Schritte: alle muessen durchkommen.
	int32 Played = 0;
	for (int32 i = 0; i < 6; ++i)
	{
		if (Zones->PlayFootstepAt(FVector(i * 100.0, 0.0, 0.0), EWbFootstepSurface::Asphalt))
		{
			++Played;
		}
	}
	TestEqual(TEXT("die ersten 6 Schritte kommen durch"), Played, 6);
	TestEqual(TEXT("Zaehler stimmt"), Zones->GetPlayedFootstepCount(), 6);
	TestTrue(TEXT("Pool meldet Belegung"), Zones->GetActiveStepCount() > 0);

	// 50 weitere: der Pool recycelt seine Plaetze, er waechst nicht.
	for (int32 i = 0; i < 50; ++i)
	{
		Zones->PlayFootstepAt(FVector(i * 10.0, 0.0, 0.0), EWbFootstepSurface::Pflaster);
	}
	TestTrue(TEXT("Pool waechst nicht unbegrenzt"),
		Zones->GetActiveStepCount() <= 6);
	// Ergaenzung: der Pool darf auch nicht SCHROMPFEN, wenn Schritte fallen.
	TestTrue(TEXT("Pool bleibt belegbar"),
		Zones->GetPlayedFootstepCount() >= 6);

	// Abschalter: danach darf nichts mehr gehoert werden.
	Zones->SetFootstepsEnabled(false);
	const int32 Before = Zones->GetPlayedFootstepCount();
	Zones->PlayFootstepAt(FVector::ZeroVector, EWbFootstepSurface::Asphalt);
	TestEqual(TEXT("ausgeschaltet -> kein Schritt"),
		Zones->GetPlayedFootstepCount(), Before);
	Zones->SetFootstepsEnabled(true);

	World->DestroyWorld(false);
	return true;
}
```

- [ ] **Step 3: Build — muss fehlschlagen**

Expected: `Unresolved external symbol` bzw. Fehler, weil die `.cpp` fehlt.

- [ ] **Step 4: Die Implementierung**

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WiesbadenAudioZonesSubsystem.h"

#include "Audio/WiesbadenAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbZones, Log, All);

void UWiesbadenAudioZonesSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogWbZones, Log, TEXT("Audio-Zonen-Subsystem bereit."));
}

void UWiesbadenAudioZonesSubsystem::Deinitialize()
{
	StepPool.Reset();
	bRigReady = false;
	Super::Deinitialize();
}

TStatId UWiesbadenAudioZonesSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWiesbadenAudioZonesSubsystem, STATGROUP_Tickables);
}

void UWiesbadenAudioZonesSubsystem::Tick(float DeltaSeconds)
{
	// Absichtlich leer: die Abfragen kommen von den Konsumenten, nicht aus
	// einem Polling. Task 4/5 rufen SurfaceUnderFoot/PlayFootstepAt.
}

void UWiesbadenAudioZonesSubsystem::EnsureRig()
{
	if (bRigReady)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Synthetischer Fussschritt je Oberflaeche. Fehlt das Asset (Import nie
	// gelaufen), bleibt der Pool still - das ist die Projektkonvention.
	int32 Created = 0;
	for (uint8 Index = 0; Index < static_cast<uint8>(EWbFootstepSurface::MAX); ++Index)
	{
		const EWbFootstepSurface Surface = static_cast<EWbFootstepSurface>(Index);
		const FString Name = FString::Printf(TEXT("/Game/Audio/Meta/MS_Step_%s"),
			*WiesbadenAudioZones::SurfaceName(Surface));
		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Name);
		if (!Sound)
		{
			continue;
		}

		UAudioComponent* Component = NewObject<UAudioComponent>(this);
		Component->bAutoActivate = false;
		Component->bAllowSpatialization = true;
		Component->SoundClassOverride =
			UWiesbadenAudioSubsystem::LoadBusSoundClass(EWbAudioBus::SFX);
		Component->SetSound(Sound);
		Component->RegisterComponent();
		if (Component->IsRegistered())
		{
			StepPool.Add(Component);
			++Created;
		}
	}

	bRigReady = true;

	if (Created == 0 && !bWarnedMissingBeds)
	{
		bWarnedMissingBeds = true;
		UE_LOG(LogWbZones, Log,
			TEXT("Keine Fussschritt-Assets gefunden (Tools/make_audio_assets.cmd). "
			     "Die Stadt bleibt bei ihren Schritten still - kein Fehler."));
	}
	UE_LOG(LogWbZones, Log, TEXT("Fussschritt-Pool: %d von %d Klangen geladen."),
		Created, MaxConcurrentSteps);
}

int32 UWiesbadenAudioZonesSubsystem::GetActiveStepCount() const
{
	return StepPool.Num();
}

EWbFootstepSurface UWiesbadenAudioZonesSubsystem::SurfaceUnderFoot(const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return EWbFootstepSurface::Pflaster;
	}

	// Strahl 30 cm unter dem Fuss nach unten. Bewusst KEIN Start 2 m ueber der
	// Mitte wie bei der Bodenabfrage der Figur: der Schritt soll am Fuss
	// klingen. 30 cm reichen, um den Boden zu treffen, ohne die Decke darueber
	// (der Strahl zeigt nach unten).
	FHitResult Hit;
	const FVector Start = Location + FVector(0.0, 0.0, 30.0);
	const FVector End = Location - FVector(0.0, 0.0, 200.0);
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility))
	{
		return EWbFootstepSurface::Pflaster;
	}

	// Materialname des Treffers lesen. Bei einer StaticMesh-Komponente steht
	// er am Slot, den der Strahl getroffen hat.
	UMeshComponent* Mesh = Hit.GetComponent();
	if (!Mesh)
	{
		return EWbFootstepSurface::Pflaster;
	}

	UMaterialInterface* Material = Mesh->GetMaterial(Hit.BoneItem != INDEX_NONE
		? Hit.BoneItem
		: Hit.Item);
	if (!Material)
	{
		return EWbFootstepSurface::Pflaster;
	}

	return WiesbadenAudioZones::SurfaceFromMaterialName(Material->GetName());
}

bool UWiesbadenAudioZonesSubsystem::PlayFootstepAt(
	const FVector& Location, EWbFootstepSurface Surface)
{
	if (!bFootstepsEnabled || !bRigReady || StepPool.IsEmpty())
	{
		return false;
	}

	// Ringer: der naechste Platz wird recycelt. So waechst der Pool nie, und
	// ein Schritt bricht den laufenden desselben Platzes ab, statt zu stapeln.
	UAudioComponent* Component = StepPool[NextStepSlot];
	NextStepSlot = (NextStepSlot + 1) % StepPool.Num();

	if (!Component)
	{
		return false;
	}

	Component->Stop();
	Component->SetWorldLocation(Location);
	Component->SetVolumeMultiplier(1.0f);
	Component->Play();

	++PlayedFootsteps;

	// Laufbeleg nach Saved/Logs (NIE stdout).
	UE_LOG(LogWbZones, Verbose,
		TEXT("Fussschritt #%d %s auf %s (Untergrund %s, Pool %d/%d)."),
		PlayedFootsteps, *WiesbadenAudioZones::SurfaceName(Surface),
		*Location.ToCompactString(), *WiesbadenAudioZones::SurfaceName(Surface),
		StepPool.Num(), MaxConcurrentSteps);

	return true;
}
```

- [ ] **Step 5: Build + Test grün**

- [ ] **Step 6: Sabotage-Gegenprobe**

`MaxConcurrentSteps` im Ringer auf `StepPool.Num() + 1` setzen (Modulo durch Null wäre die andere Richtung, also stattdessen den Guard entfernen):

```cpp
	// SABOTAGE: Guard entfernt.
	NextStepSlot = (NextStepSlot + 1) % StepPool.Num();
```

Expected: Der Pool-Test schlägt fehl oder der Build bricht. Danach zurückbauen.

- [ ] **Step 7: Commit**

```bash
git add Source/WiesbadenReal/Audio/WiesbadenAudioZonesSubsystem.h Source/WiesbadenReal/Audio/WiesbadenAudioZonesSubsystem.cpp Source/WiesbadenReal/Tests/AudioZonesSubsystemTest.cpp
git commit -m "Fussschritte: Subsystem als einzige Ort-zu-Klang-Stelle

Ring-Pool statt wachsender Liste, damit eine belebte Innenstadt nicht wie
Feuerwerk klingt. Fehlende Assets bleiben still - Projektkonvention."
```

---

### Task 3: Die synthetischen Schrittklänge

**Files:**
- Modify: `Source/WiesbadenReal/Audio/WbAudioAssetsCommandlet.cpp` (neue Methode neben `BuildBed`, Aufruf in `BuildAmbienceBeds`-Nachbarschaft)
- Test: Test `WiesbadenReal.Audio.Footsteps.Surface` aus Task 1 deckt die Bandpass-Werte ab; zusätzlich ein Beleg aus dem Commandlet-Log

**Interfaces:**
- Consumes: `WiesbadenAudioZones::SurfaceName`, `BandpassHzForSurface`
- Produces: 4 Assets `MS_Step_Asphalt`, `MS_Step_Pflaster`, `MS_Step_Wiese`, `MS_Step_Innenraum` unter `/Game/Audio/Meta`

- [ ] **Step 1: Vorher messen, nicht behaupten**

```bash
ls "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Content\Audio\Meta"
```

Expected: nur `MS_Amb*` und `MS_EngineBoxer`. Fehlen die `MS_Step_*`, ist das der rote Zustand für diesen Task.

- [ ] **Step 2: Die Methode im Commandlet**

Direkt nach `BuildBed` einfügen, damit die beiden Rausch-Bauer nebeneinander stehen:

```cpp
	/**
	 * Fussschritt: Rauschen -> Biquad-Bandpass (Material) -> Ausgang.
	 *
	 * Kein Sample, sondern parametrisch: die Bandpass-Mitte kommt aus
	 * WiesbadenAudioZones::BandpassHzForSurface, damit die Materialunterscheidung
	 * im Code steht und getestet ist statt in einer WAV-Datei.
	 *
	 * Node- und Pin-Namen sind am Engine-Quelltext geprueft
	 * (MetasoundStandardNodes/Private/MetasoundBasicFilters.cpp und
	 * MetasoundNoiseGenerator.cpp, UE 5.8):
	 *   Noise          -> Ausgang "Audio"      (Noise, Zeile 215)
	 *   Biquad Filter  -> Eingang "In", "Cutoff Frequency", "Type"
	 *                     (BasicFilters, Zeile 819ff; Pins 36/37/41)
	 * Es gibt KEINEN Node "Band Pass Filter" - der Filter heisst "Biquad
	 * Filter". Der bestehende Code nutzt "One-Pole Low/High Pass Filter",
	 * das sind eigene Nodes (Zeilen 529 und 664).
	 */
	void BuildFootsteps(TArray<UPackage*>& Packages)
	{
		for (uint8 Index = 0; Index < static_cast<uint8>(EWbFootstepSurface::MAX); ++Index)
		{
			const EWbFootstepSurface Surface = static_cast<EWbFootstepSurface>(Index);
			const FString SurfaceLabel = WiesbadenAudioZones::SurfaceName(Surface);
			const FString AssetName = FString::Printf(TEXT("MS_Step_%s"), *SurfaceLabel);
			FGraph Graph;
			if (!StartGraph(Graph, AssetName))
			{
				continue;
			}

			const FMetaSoundNodeHandle Noise =
				Graph.Node(FName(TEXT("Noise")), Metasound::StandardNodes::AudioVariant);
			const FMetaSoundNodeHandle Filter =
				Graph.Node(FName(TEXT("Biquad Filter")), Metasound::StandardNodes::AudioVariant);
			Graph.Wire(Noise, TEXT("Audio"), Filter, TEXT("In"));
			Graph.Input(TEXT("Cutoff Hz"), Filter, TEXT("Cutoff Frequency"),
				WiesbadenAudioZones::BandpassHzForSurface(Surface), true);
			Graph.ToAudioOut(Filter, TEXT("Out"));

			Graph.Finish(FString::Printf(TEXT("/Game/Audio/Meta/%s"), *AssetName),
				AssetName, Packages);
		}
	}
```

Der Aufruf kommt neben `BuildAmbienceBeds(Packages);` in der Hauptfunktion. Der
`Biquad Filter` hat als Default `Bandpass` gesetzt (Zeile 73 setzt
`Audio::EBiquadFilter::Bandpass` als Eintrag); falls der Default beim Bau nicht
greift, ist der `Type`-Pin explizit zu setzen. Der `Type`-Pin ist ein
Enum-Eingang und NICHT ueber `Graph.Input(float)` erreichbar - im Zweifel
ohne ihn bauen und das Ergebnis anhoeren, statt eine Enum-Verdrahtung zu
raten.

- [ ] **Step 3: Bauen**

```bash
C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\make_audio_assets.cmd
```

Erwartet: Build des Editor-Targets, dann der Commandlet-Lauf. **Nimmt den Engine-Lock** — vorher prüfen, ob ein fremder Thread läuft.

- [ ] **Step 4: Beleg lesen, nicht stdout**

```bash
tail -40 "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\make_audio_assets.log"
```

Expected: keine `MetaSound` Fehler, Exit 0. Dann die Assets prüfen:

```bash
ls -la "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Content\Audio\Meta"
```

Expected: vier neue `MS_Step_*.uasset`, jedes ~40 KB (gleiche Grössenordnung wie die Betten).

**Falls die Assets fehlen:** leere MetaSounds sind ~2 KB — genau die Falle aus den Learningseinträgen. Grösse prüfen, nicht nur Existenz.

- [ ] **Step 5: Gehört werden — der Abnahmeschritt**

Der Boxer-Mix musste am 24.09. um 13 dB korrigiert werden, weil die Lagen 25–31 dB unter dem Zuendpuls lagen. Dasselbe Risiko gilt hier: ein Schritt, der 40 dB zu leise ist, **gilt als gebaut** und ist unhörbar.

```bash
python "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\render_engineboxer.py" --help
```

Wenn das Skript nur den Boxer rendert: die vier `MS_Step_*` im Editor starten und die Ausgabe anhören, Pegel am Bus `SC_SFX` gegen einen Autotür-Klang (`A_CarDoor`) halten. Ergebnis als Zahl notieren, nicht als „klingt ok".

- [ ] **Step 6: Commit**

```bash
git add Source/WiesbadenReal/Audio/WbAudioAssetsCommandlet.cpp
git commit -m "Fussschritte: vier synthetische Klange, Material als Bandpass-Mitte

Die Materialunterscheidung steht damit im getesteten Code statt in einer
WAV-Datei. Aufnahmen koennen sie spaeter Lage fuer Lage ersetzen."
```

---

### Task 4: Der Spieler tritt auf

**Files:**
- Modify: `Source/WiesbadenReal/Vehicles/WiesbadenFootPawn.h` (zwei Felder, Tick-Haken)
- Modify: `Source/WiesbadenReal/Vehicles/WiesbadenFootPawn.cpp` (Schrittzyklus in `Tick`)
- Test: `Source/WiesbadenReal/Tests/FootstepSurfaceTest.cpp`

**Interfaces:**
- Consumes: `UWiesbadenAudioZonesSubsystem::SurfaceUnderFoot`, `PlayFootstepAt` aus Task 2
- Produces: kein neuer öffentlicher Name — der Pawn ist der Aufrufer

- [ ] **Step 1: Der failende Test — die Schrittweite zuerst**

Der Takt ist reine Mathematik und wird getestet, nicht die Audio-Ausgabe:

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Audio/WiesbadenAudioZones.h"

/**
 * Der Schrittzyklus des Spielers: Schrittlaenge je Tempo, Phase ueber
 * 0..1. Geht die Figur langsamer, sind die Schritte weiter auseinander.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFootstepCadenceTest,
	"WiesbadenReal.Audio.Footsteps.Cadence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFootstepCadenceTest::RunTest(const FString& Parameters)
{
	namespace
	{
		/** Schrittlaenge in cm: Gehen 75 cm, Rennen 125 cm (gemessener
		 *  Fussabstand einer 1,75-m-Figur). */
		float StrideCmForSpeed(float SpeedMps)
		{
			return FMath::Lerp(75.0f, 125.0f, FMath::Clamp(SpeedMps / 6.0f, 0.0f, 1.0f));
		}
	}

	const float Walk = StrideCmForSpeed(1.5f);
	const float Run = StrideCmForSpeed(6.0f);
	TestTrue(TEXT("Gehen kuerzere Schritte als Rennen"), Walk < Run);
	TestTrue(TEXT("Gehen in Schrittlaenge 50..110 cm"), Walk >= 50.0f && Walk <= 110.0f);

	// Stillstand erzeugt keinen Schrittzyklus.
	const float Idle = StrideCmForSpeed(0.0f);
	TestTrue(TEXT("Stillstand: kein Fortschritt"), Idle >= 0.0f);

	return true;
}
```

- [ ] **Step 2: Build — muss fehlschlagen**

- [ ] **Step 3: Zwei Felder in den Header**

In `AWiesbadenFootPawn.h`, nach `PreviousLocation`:

```cpp
	/** Seit dem letzten Schritt zurueckgelegte Strecke (cm). */
	float StepDistanceAccumulatedCm = 0.0f;

	/** Aktueller Untergrund unter den Fuessen (fuer den naechsten Schritt). */
	EWbFootstepSurface LastFootstepSurface = EWbFootstepSurface::Pflaster;
```

`EWbFootstepSurface` braucht den Include `#include "Audio/WiesbadenAudioZones.h"` in der `.cpp`; in der `.h` genuegt die Vorwaertsdeklaration als `enum class EWbFootstepSurface : uint8;` **nicht** — fuer ein Feld vom_enum-Typ reicht sie, aber der Default-Wert `= EWbFootstepSurface::Pflaster` braucht die vollstaendige Definition. Also den Include in die `.h`.

- [ ] **Step 4: Der Schrittzyklus im Tick**

In `AWiesbadenFootPawn::Tick`, unmittelbar nach `UpdateFigure(DeltaSeconds, SpeedMps);`:

```cpp
	UpdateFootsteps(DeltaSeconds, SpeedMps);
```

Und die Methode daneben:

```cpp
void AWiesbadenFootPawn::UpdateFootsteps(float DeltaSeconds, float SpeedMps)
{
	// Steht die Figur, laeuft kein Schrittzyklus. Ohne diese Klammer kaemen
	// im Leerlauf Schritte heraus, weil der Akkumulator stehen bliebe und
	// beim ersten Gehen sofort ueberschossen wird.
	if (SpeedMps < 0.1f)
	{
		StepDistanceAccumulatedCm = 0.0f;
		return;
	}

	// Gemessenes Tempo, nicht die Eingabe: wer gegen eine Wand laeuft, soll
	// keine Schritte hoeren.
	StepDistanceAccumulatedCm += SpeedMps * DeltaSeconds * 100.0f;

	const float StrideCm = FMath::Lerp(75.0f, 125.0f, FMath::Clamp(SpeedMps / 6.0f, 0.0f, 1.0f));
	if (StepDistanceAccumulatedCm < StrideCm)
	{
		return;
	}
	StepDistanceAccumulatedCm -= StrideCm;

	UWorld* World = GetWorld();
	UWiesbadenAudioZonesSubsystem* Zones = World
		? World->GetSubsystem<UWiesbadenAudioZonesSubsystem>() : nullptr;
	if (!Zones)
	{
		return;
	}

	// Am Fuss abtasten, nicht in der Mitte: die Figur ist 1,75 m hoch.
	const FVector FootLocation = GetActorLocation() - FVector(0.0, 0.0, 88.0);
	LastFootstepSurface = Zones->SurfaceUnderFoot(FootLocation);
	Zones->PlayFootstepAt(FootLocation, LastFootstepSurface);

	UE_LOG(LogWbFootPawn, Verbose,
		TEXT("WbSchritt: Tempo %.2f m/s, Schritt %.0f cm, Untergrund %s."),
		SpeedMps, StrideCm, *WiesbadenAudioZones::SurfaceName(LastFootstepSurface));
}
```

- [ ] **Step 5: Build + Tests grün**

- [ ] **Step 6: Laufbeleg**

```bash
powershell -NoProfile -Command 'Get-Content "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\WiesbadenReal.log" | Select-String "WbSchritt|Fussschritt #" | Select-Object -Last 20'
```

Expected: Zeilen mit steigendem Tempo und wechselndem Untergrund. **Steigt die Schrittzahl bei stehendem Tempo weiter, ist der Leerlauf-Zweig kaputt** — das ist die Sabotage, die man im Bild sonst nicht sieht.

- [ ] **Step 7: Commit**

```bash
git add Source/WiesbadenReal/Vehicles/WiesbadenFootPawn.h Source/WiesbadenReal/Vehicles/WiesbadenFootPawn.cpp Source/WiesbadenReal/Tests/FootstepSurfaceTest.cpp
git commit -m "Fussschritte: die Spielerfigur tritt auf dem Untergrund auf

Schrittlaenge aus dem gemessenen Tempo, nicht aus der Eingabe - wer gegen
eine Wand laeuft, soll keine Schritte hoeren. Der Takt haengt am
Akkumulator, damit im Leerlauf nichts laeuft."
```

---

### Task 5: Die Passanten treten auf

Der Teil, der die vorhandene Architektur zwingt: Passanten sind **keine Actors**, sie sind ISM-Instanzen. Ein `UAudioComponent` je Passant gibt es nicht.

**Files:**
- Create: `Source/WiesbadenReal/Audio/WiesbadenFootstepPool.h`
- Create: `Source/WiesbadenReal/Audio/WiesbadenFootstepPool.cpp`
- Modify: `Source/WiesbadenReal/World/WiesbadenCityActor.h` (Pool als Feld)
- Modify: `Source/WiesbadenReal/World/WiesbadenCityActor.cpp` (Aufruf in `UpdatePedestrians`)
- Test: `Source/WiesbadenReal/Tests/PedestrianFootstepTest.cpp`

**Interfaces:**
- Consumes: `FPlacedPedestrian::StridePhase` (0..1), `Seed`, `Location` aus `GIS/WiesbadenPedestrianSimulation.h`; `PlayFootstepAt` und `SurfaceUnderFoot` aus Task 2
- Produces:
  - `class UWiesbadenFootstepPool` (kein UObject — eine `UCLASS` waere hier Overhead ohne Nutzen, der Besitz haengt am Actor-Feld)
  - `void NotePedestrians(UWorld* World, const TArray<FPlacedPedestrian>& Placed, float DeltaSeconds)` — Phase je Seed merken, beim Sprung ueber die 0/1-Grenze melden
  - `int32 GetPedestrianStepsLastFrame() const`
  - `static constexpr int32 MaxStepsPerFrame = 6`

- [ ] **Step 1: Der failende Test**

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenPedestrianSimulation.h"
#include "Audio/WiesbadenFootstepPool.h"

/**
 * Passanten-Schritte aus der vorhandenen StridePhase.
 *
 * Kernpunkt: die Phase laeuft 0..1 und springt bei 1 -> 0. Genau dieser
 * Sprung ist der Schritt. Ohne ihn (etwa weil die Phase in zwei Schritten
 * weitergenannt wurde) schweigen alle Passanten.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPedestrianFootstepTest,
	"WiesbadenReal.Audio.Footsteps.Pedestrians",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPedestrianFootstepTest::RunTest(const FString& Parameters)
{
	namespace
	{
		TArray<FPlacedPedestrian> Make(int32 N, float Phase)
		{
			TArray<FPlacedPedestrian> Out;
			for (int32 i = 0; i < N; ++i)
			{
				FPlacedPedestrian P;
				P.Location = FVector(i * 150.0, 0.0, 0.0);
				P.Rotation = FRotator::ZeroRotator;
				P.StridePhase = Phase;
				P.ScaleFactor = FVector::OneVector;
				P.Seed = i;
				Out.Add(P);
			}
			return Out;
		}
	}

	UWiesbadenFootstepPool Pool;

	// Erstes Bild: Phase 0.4 -> noch kein Schritt (der Start zaehlt nicht).
	Pool.NotePedestrians(nullptr, Make(10, 0.4f), 1.0f / 60.0f);
	TestEqual(TEXT("Startbild erzeugt keinen Schritt"), Pool.GetPedestrianStepsLastFrame(), 0);

	// Zweites Bild: Phase springt ueber die 1->0-Grenze. 10 Passanten wollen
	// schreiten, das Budget laesst 6 zu - genau das ist die Regel, um die es
	// hier geht, deshalb wird 6 und nicht 10 erwartet.
	Pool.NotePedestrians(nullptr, Make(10, 0.05f), 1.0f / 60.0f);
	TestEqual(TEXT("Phasensprung erzeugt genau 6 Schritte (Budget)"),
		Pool.GetPedestrianStepsLastFrame(),
		UWiesbadenFootstepPool::MaxStepsPerFrame);

	// Unter dem Budget: 3 Passanten -> 3 Schritte.
	Pool.NotePedestrians(nullptr, Make(3, 0.05f), 1.0f / 60.0f);
	TestEqual(TEXT("unter dem Budget: jeder Passant ein Schritt"),
		Pool.GetPedestrianStepsLastFrame(), 3);

	// Drittes Bild: gleiche Phase -> nichts Neues.
	Pool.NotePedestrians(nullptr, Make(10, 0.05f), 1.0f / 60.0f);
	TestEqual(TEXT("gleiche Phase erzeugt nichts"),
		Pool.GetPedestrianStepsLastFrame(), 0);

	// Leerer Durchgang: nichts, kein Absturz.
	Pool.NotePedestrians(nullptr, TArray<FPlacedPedestrian>(), 1.0f / 60.0f);
	TestEqual(TEXT("leer -> kein Schritt"), Pool.GetPedestrianStepsLastFrame(), 0);

	// Begrenzung: 200 Passanten duerfen nicht 200 Schritte erzeugen.
	Pool.NotePedestrians(nullptr, Make(200, 0.05f), 1.0f / 60.0f);
	TestTrue(TEXT("Schrittzahl ist begrenzt"),
		Pool.GetPedestrianStepsLastFrame() <= UWiesbadenFootstepPool::MaxStepsPerFrame);

	return true;
}
```

- [ ] **Step 2: Build — muss fehlschlagen**

Expected: `WiesbadenFootstepPool.h` fehlt, der Test laesst sich nicht uebersetzen.

- [ ] **Step 3: Die Implementierung**

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

class UWorld;
struct FPlacedPedestrian;

/**
 * Fussschritte der Passanten — ohne einen Actor je Passant.
 *
 * Die Passanten sind ISM-Instanzen (FPlacedPedestrian), keine Actors. Ein
 * UAudioComponent je Figur waere dort unmoeglich. Stattdessen dient die
 * vorhandene StridePhase als Takt: sie laeuft 0..1 und springt bei 1 -> 0.
 * Dieser Sprung IST der Schritt.
 *
 * Eine belebte Innenstadt darf nicht wie Feuerwerk klingen, deshalb ist die
 * Zahl der Schritte je Bild hart begrenzt.
 */
class WIESBADENREAL_API UWiesbadenFootstepPool
{
public:
	/** Hoechstens so viele Passantenschritte je Bild. */
	static constexpr int32 MaxStepsPerFrame = 6;

	/**
	 * Phasen merken und bei Ueberschreitung melden.
	 * Aufgerufen mit dem Ergebnis der Simulation, sobald die Figuren stehen.
	 */
	void NotePedestrians(UWorld* World, const TArray<FPlacedPedestrian>& Placed, float DeltaSeconds);

	/** Schritte, die der letzte Aufruf ausgeloest hat. */
	int32 GetPedestrianStepsLastFrame() const { return StepsThisFrame; }

	/** Setzt den Zaehler zurueck (Aufruf nach der Auswertung). */
	void ResetFrame() { StepsThisFrame = 0; }

private:
	/** Vorige Phase je Passant-Seed (der Seed ist stabil, der Index nicht). */
	TMap<int32, float> LastPhaseBySeed;

	int32 StepsThisFrame = 0;
};
```

Die `.cpp`:

```cpp
// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WiesbadenFootstepPool.h"

#include "Audio/WiesbadenAudioZonesSubsystem.h"
#include "GIS/WiesbadenPedestrianSimulation.h"

void UWiesbadenFootstepPool::NotePedestrians(
	UWorld* World, const TArray<FPlacedPedestrian>& Placed, float DeltaSeconds)
{
	StepsThisFrame = 0;

	// nullptr-Welt ist der Normalfall im headless Test: dann zaehlt der Pool
	// nur, ohne zu spielen. Kein Absturz, kein Fehler.
	UWiesbadenAudioZonesSubsystem* Zones = World
		? World->GetSubsystem<UWiesbadenAudioZonesSubsystem>() : nullptr;

	// Mehr Schritte als das Budget erlaubt: die NAEHSTEN zuerst. Die Liste
	// kommt nach Sichtbarkeit sortiert bei den Figuren an, die man hoert.
	const int32 Budget = MaxStepsPerFrame;
	int32 Emitted = 0;

	for (const FPlacedPedestrian& P : Placed)
	{
		// FindOrAdd liefert eine Referenz, Find sagt, ob es den Seed schon gab.
		// Die Trennung ist noetig: ein neuer Passant darf im ersten Bild keinen
		// Schritt ausloesen, sonst schlaegt die ganze Strasse im ersten Frame.
		float& Last = LastPhaseBySeed.FindOrAdd(P.Seed, -1.0f);
		const bool bWasKnown = Last >= 0.0f;

		// Sprung ueber die 0/1-Grenze: 0.95 -> 0.05 heisst Schritt. Ein
		// Gleichstand ist keiner, und ein kleiner Sprung (0.2 -> 0.3, die
		// Phase lief nur ein Stueck weiter) auch nicht.
		const bool bCrossed = bWasKnown && (P.StridePhase < Last - 0.5f);

		Last = P.StridePhase;

		if (!bCrossed || Emitted >= Budget || !Zones)
		{
			continue;
		}

		Zones->PlayFootstepAt(P.Location,
			Zones->SurfaceUnderFoot(P.Location));
		++Emitted;
		++StepsThisFrame;
	}
}
```

Die Welt kommt als Parameter herein, nicht aus `GEngine` — sonst wuerde der Pool
im Test die falsche Welt greifen. `nullptr` ist der erlaubte Fall: dann zaehlt
der Pool, ohne zu spielen. Der Test bleibt dadurch headless.

- [ ] **Step 4: Aufrufer anschliessen**

Der Pool lebt in `AWiesbadenCityActor`, nicht im Spawner. Grund: `UpdatePedestrians`
ist die einzige Stelle, die die fertige `TArray<FPlacedPedestrian>` bekommt
(`WiesbadenCityActor.cpp:67`) und den `World` ohnehin hat. Ein
`UPedestrianSpawnerComponent` ist eine `USceneComponent` und muss den World erst
umstaendlich holen.

In `Source/WiesbadenReal/World/WiesbadenCityActor.h`, neben `PedestrianSpawner`:

```cpp
	/** Passanten-Fussschritte (Pool, weil Passanten ISM-Instanzen sind). */
	UPROPERTY()
	TObjectPtr<UWiesbadenFootstepPool> PedestrianSteps = nullptr;
```

Im Konstruktor nach `PedestrianSpawner->SetupAttachment(Root);`:

```cpp
	PedestrianSteps = NewObject<UWiesbadenFootstepPool>(this);
```

Und `AWiesbadenCityActor::UpdatePedestrians`:

```cpp
void AWiesbadenCityActor::UpdatePedestrians(const TArray<FPlacedPedestrian>& Placed)
{
	if (PedestrianSpawner)
	{
		PedestrianSpawner->UpdateInstances(Placed);
	}

	// Schritte aus der StridePhase. Der Spawner zeichnet die Figuren, der
	// Pool hoert sie - die Aufteilung ist genau die ISM-Realitaet: eine
	// AudioComponent je Figur gibt es nicht.
	if (PedestrianSteps)
	{
		PedestrianSteps->NotePedestrians(GetWorld(), Placed, 0.0f);
	}
}
```

Der `DeltaSeconds` wird hier mit 0 uebergeben, weil die Sprungerkennung in
`NotePedestrians` aus dem Phasenvergleich kommt und nicht aus der Zeit — der
Test in Task 5 laeuft ebenfalls mit einem beliebigen Delta und erhaelt die
richtige Zaehlung.

- [ ] **Step 5: Build + Tests grün**

- [ ] **Step 6: Sabotage-Gegenprobe**

Die Sprungbedingung auf „Phase hat sich veraendert" erweitern:

```cpp
	// SABOTAGE: jeder Phasenwechsel zaehlt.
	const bool bCrossed = bWasKnown && (P.StridePhase != Last);
```

Expected: `gleiche Phase erzeugt nichts` schlägt fehl. **Dieser Test ist der Grund fuer die Aufgabe** — ohne ihn haette die Aenderung im Bild niemand bemerkt.

- [ ] **Step 7: Der A/B-Beleg im selben Build**

```bash
C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\shot_innen_nacht.cmd
```

Derselbe Build zweimal, einmal mit dem A/B-Schalter. **Zwei verschiedene Laeufe sind nicht vergleichbar** (bis 65 % Pixel Unterschied durch Verkehr und Wetter).

- [ ] **Step 8: Commit**

```bash
git add Source/WiesbadenReal/Audio/WiesbadenFootstepPool.h Source/WiesbadenReal/Audio/WiesbadenFootstepPool.cpp Source/WiesbadenReal/World/WiesbadenCityActor.h Source/WiesbadenReal/World/WiesbadenCityActor.cpp Source/WiesbadenReal/Tests/PedestrianFootstepTest.cpp
git commit -m "Fussschritte: Passanten treten auf, ohne einen Actor je Figur

Die vorhandene StridePhase ist der Takt - ihr Sprung ueber die 0/1-Grenze IST
der Schritt. Der Pool haengt am CityActor, weil nur er die fertige Passanten-
liste und den World hat. Sechs Schritte je Bild als harte Grenze, sonst
klingt die Innenstadt wie Feuerwerk."
```

---

## Was nach diesem Plan gilt

**Fertig:** Spieler und Passanten treffen hörbar den richtigen Boden, ohne
Karten-Nebake. `WiesbadenAudioZones` und das Subsystem stehen so, dass Teil 2
(synthetische Stadtbetten) und Teil 3a (Zonenauswahl) nur noch die
Zonentabelle in `WiesbadenAudioZones` ergaenzen.

**Noch offen, bewusst:** Teil 2, 3a und 3b bekommen je einen eigenen Plan.
3b bleibt bis nach den Messungen aus 3a gesperrt — es kostet den
2-Stunden-NRebake ueber ~2060 Chunks.

**Bekannte Luecke:** Die `-WbNoFootsteps`-Schalter-Zeile in Task 2 ist im
Header gesetzt, aber noch an keine Kommandozeile gebunden. Das gehoert in
Task 4 oder 5, sonst schaltet der A/B-Vergleich ins Leere.
