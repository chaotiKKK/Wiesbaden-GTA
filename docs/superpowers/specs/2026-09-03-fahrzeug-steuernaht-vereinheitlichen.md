# Design: Fahrzeug-Steuernaht vereinheitlichen (IWiesbadenVehicleControl)

Status: ENTWURF zur Abnahme. Kein Code, nur Plan.
Datum: 2026-09-03

## Ziel
Harness, Autopilot, Dev-Execs, HUD und Tests sollen **beide** Fahrzeuge -
`AWiesbadenCar` (kinematisch) und `AWiesbadenChaosCar` (Chaos-Physik) - ueber
**dieselbe Naht** ansteuern und auslesen. Heute ist alles hart auf `AWiesbadenCar`
verdrahtet; `-WbChaosCar` liefert "kein Fahrzeug besessen", und der ganze
Handling-Schwerpunkt (CoM/Reibung/Federung) ist damit nicht verifizierbar.

## Ist-Zustand (die Gabelung)
- `AWiesbadenCar : public APawn` - eigene kinematische Physik (`FWiesbadenVehicle-
  Physics`). Naht: inline `SetExternalControl(FWiesbadenCarControl)` /
  `ClearExternalControl()`, angewandt im Input-Tick (interpoliert). Readouts
  `GetSpeedKmh()`, `GetGear()`.
- `AWiesbadenChaosCar : public AWheeledVehiclePawn` - echte Chaos-Physik. Eingabe
  per `Movement->SetThrottleInput/SetBrakeInput/SetSteeringInput/SetHandbrakeInput`
  (liest heute nur Tastatur). Readouts `GetSpeedKmh()`, `GetCurrentGear()`.
- Konsumenten casten hart auf `AWiesbadenCar`: `WiesbadenVehicleTestHarness`
  (`Cast<AWiesbadenCar>` + `SetExternalControl`), `WbDrive` (`Cast<AWiesbadenCar>`),
  `WiesbadenVehicleHUD` (Tacho/Gang/RPM/Kontrollleuchten). ChaosCar faellt durch
  jeden Cast.

**Warum kein gemeinsamer Basistyp:** ChaosCar MUSS von `AWheeledVehiclePawn`
erben (Chaos), Car ist ein `APawn`. Ein gemeinsamer Pawn-Basistyp ist unmoeglich.
Die Vereinheitlichung MUSS ueber ein **Interface** laufen (Dependency Inversion),
nicht ueber Vererbung.

## Die Naht: FWiesbadenCarControl + IWiesbadenVehicleControl

1. **`FWiesbadenCarControl`** (Throttle 0..1, Brake 0..1, Steering -1..1,
   bHandbrake, bReverse) aus `WiesbadenCar.h` in einen neutralen Header ziehen
   (`Vehicles/WiesbadenVehicleControl.h`) - so kann ChaosCar die Naht nutzen,
   ohne `WiesbadenCar.h` zu inkludieren. Bleibt das gemeinsame Steuer-Vokabular.

2. **`UWiesbadenVehicleControl` / `IWiesbadenVehicleControl`** (C++ UINTERFACE) -
   der Vertrag, den beide Fahrzeuge erfuellen:
   - `void SetExternalControl(const FWiesbadenCarControl&)`
   - `void ClearExternalControl()`
   - `bool IsExternalControlActive() const`
   - `float GetSpeedKmh() const`
   - `int32 GetGear() const`  (ChaosCar mappt `GetCurrentGear` -> `GetGear`)

   Bewusst SCHLANK: nur Steuerung + die zwei Readouts, die Harness/Tests/HUD-Basis
   brauchen. Reiche fahrzeugspezifische Anzeigen (RPM-Band, Kontrollleuchten,
   Cockpit) bleiben vorerst Car-spezifisch (siehe HUD unten).

## Aenderungen je Klasse
- **`AWiesbadenCar`**: deklariert, dass es `IWiesbadenVehicleControl` implementiert.
  Methoden existieren bereits (nur Interface-Anbindung, ~keine Verhaltensaenderung).
- **`AWiesbadenChaosCar`**: implementiert das Interface. `SetExternalControl`
  speichert Control + Aktiv-Flag; im `Tick` wird bei aktivem externem Control der
  gespeicherte Befehl statt der Tastatur an `Movement->Set*Input` gelegt.
  Uebersetzung: Throttle/Brake/Steering direkt; `bHandbrake` -> `SetHandbrakeInput`;
  `bReverse` -> Rueckwaertsgang (`SetTargetGear(-1, true)`, sonst automatisch).
  `GetGear()` = `GetCurrentGear()`.

## Konsumenten umstellen (Cast auf Interface statt Klasse)
- **Test-Harness** (`WiesbadenVehicleTestHarness`): der Fahr-Pfad haelt den Pawn
  (`AActor*`) UND `IWiesbadenVehicleControl* = Cast<...>(Owner)`. `StartDriveProfile`
  nutzt `Ctrl->SetExternalControl` + `Ctrl->GetSpeedKmh/GetGear`; Kursaenderung
  weiter aus `Pawn->GetActorRotation().Yaw` (Actor-Ebene). `GetOrAddHarness` bleibt.
- **`WbDrive`**: `Cast<IWiesbadenVehicleControl>(GetPawn())` statt `AWiesbadenCar` -
  damit greift es auf BEIDE Fahrzeuge. Warnung nur, wenn kein Fahrzeug das
  Interface implementiert.
- **HUD** (`WiesbadenVehicleHUD`): fuer die Basis-Anzeige (Tempo/Gang) ueber das
  Interface gehen; die reichen Car-Instrumente vorerst nur zeichnen, wenn der Pawn
  ein `AWiesbadenCar` ist (ChaosCar bekommt Tempo/Gang-Minimalanzeige). Optionaler
  Ausbau spaeter: RPM/Telltales ins Interface heben.
- **Tests**: ein neuer Automation-/Rauchtest faehrt BEIDE Fahrzeuge ueber das
  Interface (Tempo>20, Kursaenderung>15) - `-WbChaosCar` schaltet um.

## Voraussetzungen fuers ECHTE Fahren des ChaosCar (ausserhalb dieser Code-Naht)
Die Naht allein laesst den ChaosCar noch nicht fahren:
1. **In der Karte vorhanden/besetzbar** - der `-WbChaosCar`-Zweig im GameMode
   iteriert vorhandene `AWiesbadenChaosCar`; es muss einer platziert oder gespawnt
   werden (GameMode, Code) - sonst "kein Fahrzeug besessen".
2. **Bauch-Kollision fixen** - der ChaosCar sitzt auf dem Bauch (steht nicht auf
   Raedern, faehrt nicht vorwaerts). Fix am **PhysicsAsset** im Editor
   (`SkeletalBodySetups`, laut AGENTS.md NICHT per Python skriptbar).
Erst danach ist die End-to-End-Verifikation ueber die Naht moeglich.

## Heli-Parallele
Der Helikopter nutzt bereits dieselbe Idee mit `FWiesbadenHeliControl` +
`SetExternalControl`. Optionaler spaeterer Schritt: eine Interface-Familie
(`IWiesbadenExternalControl<TControl>`-Muster) fuer Car UND Heli - der Autopilot
und der Harness haetten dann EINEN Zugriffspfad. Nicht Teil dieses Entwurfs
(Fokus: die zwei Autos), aber das Interface hier so benennen, dass es passt.

## Sequenzierung
1. `FWiesbadenCarControl` in neutralen Header ziehen + `IWiesbadenVehicleControl`
   anlegen. (S)
2. `AWiesbadenCar` an das Interface anbinden (Verhalten unveraendert). (XS)
3. `AWiesbadenChaosCar` implementieren + Uebersetzung + Apply-im-Tick. (M)
4. Konsumenten (Harness, WbDrive, HUD-Basis) auf das Interface umstellen. (M)
5. ChaosCar in Karte + PhysicsAsset-Bauch-Fix (Editor, separat). (Editor)
6. Gemeinsamer Fahr-Test ueber das Interface fuer beide Fahrzeuge. (S-M)

Schritte 1-4 sind reine, sichere Code-Arbeit (meine Dateien; `WiesbadenVehicle-
Physics` bleibt unangetastet). Schritt 5 ist der Editor-Blocker fuer die
End-to-End-Verifikation.

## Aufwand / Risiko / Tradeoffs
- **Aufwand:** Code mittel (1-4); Editor-Vorbedingung separat (5).
- **Risiko:** niedrig fuer die Naht (additiv; der bestehende Car-Pfad bleibt
  verhaltensgleich). Mittel fuer die ChaosCar-Uebersetzung (Rueckwaerts/Handbremse,
  Apply-Timing) - erst nach dem Bauch-Fix verifizierbar.
- **Tradeoff:** eine Indirektionsebene (Interface); zwei Steuer-Vokabulare (Car vs
  Heli), solange nicht zusammengefuehrt. Gewinn: EIN verifizierbarer Fahrzeugpfad -
  entsperrt den ganzen Handling-Tuning-Track (CoM/Reibung/Federung) mit
  headless-Verifikation, sobald der ChaosCar faehrt.

## Verifikation (nach Umsetzung)
- `WbDrive` treibt den Standard-Kaefer wie bisher (Regression: Rauchtest gruen).
- `-WbChaosCar` + `WbDrive` treibt den ChaosCar ueber DIESELBE Naht (nach
  Bauch-Fix): Tempo>20 km/h + Kursaenderung>15 Grad aus dem Log.
- Der neue Zwei-Fahrzeug-Test besteht fuer beide.

## Explizit ausserhalb
- Handling-Tuning-Werte (CoM/Reibung/Federung) - eigener Track, nach der Naht.
- PhysicsAsset-/Modellarbeit (Editor).
- `WiesbadenVehiclePhysics` (Parallel-Worker-WIP) wird nicht angefasst.
