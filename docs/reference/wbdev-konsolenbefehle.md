# Referenz: WbDev-Konsolenbefehle

Vollstaendige Referenz der `WbDev`-Entwicklerbefehle von WiesbadenReal. Alle
Befehle sind `UFUNCTION(Exec)` auf `AWiesbadenPlayerController`
(`Source/WiesbadenReal/Core/WiesbadenPlayerController.cpp`) und dienen dem
manuellen wie automatisierten Testen (Rauchtest, Automation).

Dies ist eine reine Nachschlage-Referenz. Eine schrittweise Einfuehrung
(Tutorial) oder aufgabenbezogene Anleitungen (How-to) sind nicht Teil dieses
Dokuments.

## Aufruf

- **In-Game-Konsole:** Konsole oeffnen und den Befehl mit Argumenten tippen,
  z. B. `WbCam 2`.
- **Kommandozeile:** `-ExecCmds="Befehl1 Args,Befehl2 Args"`. Die Befehle sind
  per **Komma** getrennt (nicht Semikolon); ein Semikolon verschmilzt alles zu
  einem Befehl. Beispiel:
  `-ExecCmds="WbHeli,WbHeliGoto 200 0 50,WbHeliHover"`.
- **Erreichbarkeit:** `-ExecCmds` durchlaeuft nur die `ULocalPlayer::Exec`-Kette
  (PlayerController -> Pawn -> HUD -> GameMode -> ...), daher liegen diese
  Befehle bewusst auf dem PlayerController und nicht in einem Subsystem.
- **Fehlende Argumente** fuellt die Engine mit `0`. Ganzzahl-Parameter sind
  bewusst `int32`; Kommazahlen sind nicht vorgesehen.

## Log-Auswertung

Jeder Befehl schreibt in die Kategorie **`LogWbCore`**. Konvention:

- **`Log`** (Info) = Befehl ausgefuehrt, mit gemessenem Vorher/Nachher-Nachweis.
- **`Warning`** = Vorbedingung fehlte (kein Pawn/Heli/Fahrzeug/Subsystem) **oder**
  ein Argument wurde geklemmt. Der Befehl bricht bei fehlender Vorbedingung ohne
  Wirkung ab.

Alle Log-Zeilen beginnen mit dem Praefix `WbDev:`. Die unten zitierten Texte
sind woertlich (Platzhalter wie `%d`/`%.0f` durch die Laufzeitwerte ersetzt).

## Uebersicht

| Befehl | Signatur | Voraussetzung | Wirkung (kurz) |
| --- | --- | --- | --- |
| `WbTeleport` | `WbTeleport <0-2>` | besessener Pawn | Pawn an festen Spawn 0/1/2 setzen |
| `WbResetVehicle` | `WbResetVehicle` | besessener Pawn | Nick/Roll auf 0 (aufrichten), Yaw bleibt |
| `WbTraffic` | `WbTraffic <0/1>` | City-Subsystem | Verkehrsdichte 0.0 (aus) oder 0.5 (an) |
| `WbHealth` | `WbHealth` | City-Subsystem | Maschinenlesbaren Gesundheitsbericht (JSON) ausgeben + speichern |
| `WbCam` | `WbCam <0-2>` | Fahrzeug mit Kamera | Kameramodus Follow/Orbit/Cockpit |
| `WbHeli` | `WbHeli` | Helikopter in der Welt | ersten Helikopter uebernehmen (Possess) |
| `WbNudge` | `WbNudge <nick> <roll>` | besessener Pawn | Nick/Roll relativ um Grad kippen |
| `WbHeliYaw` | `WbHeliYaw <s>` | Helikopter besessen | Gierprobe (Test-Harness) fuer s Sekunden |
| `WbHeliFly` | `WbHeliFly <s>` | Helikopter besessen | Flugprofil (Test-Harness) fuer s Sekunden |
| `WbDrive` | `WbDrive <s>` | Fahrzeug besessen | Fahrprofil (Test-Harness) fuer s Sekunden |
| `WbHeliGoto` | `WbHeliGoto <dx> <dy> <dz>` | Helikopter besessen | Autopilot fliegt dx/dy/dz m relativ, haelt |
| `WbHeliHover` | `WbHeliHover` | Helikopter besessen | Autopilot haelt aktuelle Position |
| `WbHeliOff` | `WbHeliOff` | Helikopter besessen | Autopilot aus, Steuerung zurueck an Eingabe |

---

## WbTeleport

- **Signatur:** `WbTeleport <Ziel:int 0-2>`
- **Wirkung:** Setzt den besessenen Pawn per `TeleportPhysics` an einen der drei
  festen Spawn-Punkte (`FWiesbadenDevActions::TeleportSpawnCm`). Werte ausserhalb
  0-2 werden auf den Bereich geklemmt. Die Kernlogik (Zielkoordinaten) liegt
  datenrein in `FWiesbadenDevActions` und wird mit dem Pause-Menue geteilt.
- **Voraussetzung:** ein besessener Pawn.
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbTeleport <Ziel> ausgefuehrt: von (x,y,z) nach (x,y,z), Distanz <d> cm.`
  - Geklemmt (Warning): `WbDev: WbTeleport <roh> ausserhalb 0-2 - auf Ziel <geklemmt> begrenzt.`
  - Kein Pawn (Warning): `WbDev: WbTeleport <Ziel> erkannt, aber kein besessener Pawn.`

## WbResetVehicle

- **Signatur:** `WbResetVehicle`
- **Wirkung:** Richtet den besessenen Pawn auf: Nick und Roll auf 0, der Yaw
  (Blickrichtung) bleibt erhalten. Nutzt `FWiesbadenDevActions::UprightTransform`
  und `TeleportPhysics`. Fuer einen ueberschlagenen Wagen gedacht.
- **Voraussetzung:** ein besessener Pawn.
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbResetVehicle ausgefuehrt: Nick/Roll vorher (p/r) -> nachher (p/r), Yaw <y> bleibt.`
  - Kein Pawn (Warning): `WbDev: WbResetVehicle erkannt, aber kein besessener Pawn.`

## WbTraffic

- **Signatur:** `WbTraffic <An:int>`
- **Wirkung:** Schaltet die Verkehrssimulation. `0` setzt die Dichte
  (`TrafficSimulation.Settings.TrafficDensity`) auf `0.0` (aus), jeder andere
  Wert auf `0.5` (an). Kein Zwischenwert einstellbar.
- **Voraussetzung:** ein `UWiesbadenCitySubsystem` in der Welt.
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbTraffic <An> ausgefuehrt: Dichte <vorher> -> <nachher>.`
  - Kein Subsystem (Warning): `WbDev: WbTraffic <An> erkannt, aber kein City-Subsystem.`

## WbHealth

- **Signatur:** `WbHealth [MaxWaitSeconds:float]`
- **Wirkung:** Sammelt die Laufzeit-Selbstdiagnosen (Streaming, Verkehr, Ampeln,
  Fussgaenger, Gebaeude-Kollision) in einen `FWiesbadenHealthReport`, gibt ihn als
  maschinenlesbare JSON-Zeile ins Log und speichert ihn nach
  `Saved/Logs/WbHealth.json`. Warnungen sind EVIDENZ-gewichtet (z. B. Ampel nur bei
  vielen Anfahrten ohne Halten) und stehen im JSON-Feld `warnings` samt `healthy`.
  Interpretation/JSON liegen entkoppelt in `FWiesbadenHealthReport`; das
  CitySubsystem liefert nur die Rohzahlen (`BuildHealthReport`).
- **Argument (optional):** ohne Wert (bzw. `<=0`) schreibt der Befehl sofort den
  Ist-Zustand. Mit `MaxWaitSeconds > 0` laeuft er im **Gate-Modus**: er wartet auf
  den geladenen Zustand (Stadt geladen UND World-Partition-Streaming fertig),
  hoechstens `MaxWaitSeconds` Sekunden, und schreibt den Bericht erst dann. So
  spiegelt das JSON den geladenen Zustand statt eines Startup-Transienten - genutzt
  vom Rauchtest als Gesundheits-Gate nach dem Laden.
- **Voraussetzung:** ein `UWiesbadenCitySubsystem` in der Welt.
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbHealth: <json>` gefolgt von `WbDev: WbHealth - Bericht nach <pfad> geschrieben.`
  - Gate-Modus (Info): `WbDev: WbHealth wartet auf geladenen Zustand (bis <n> s).`
  - Kein Subsystem (Warning): `WbDev: WbHealth erkannt, aber kein City-Subsystem.`

## WbCam

- **Signatur:** `WbCam <Modus:int 0-2>`
- **Wirkung:** Setzt den Kameramodus des besessenen Fahrzeugs:
  `0=Follow`, `1=Orbit`, `2=Cockpit` (`UWiesbadenVehicleCameraComponent`). Werte
  ausserhalb 0-2 werden geklemmt.
- **Voraussetzung:** ein besessener Pawn mit `UWiesbadenVehicleCameraComponent`.
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbCam <Modus> gesetzt (0=Follow,1=Orbit,2=Cockpit).`
  - Geklemmt (Warning): `WbDev: WbCam <roh> ausserhalb 0-2 - auf <geklemmt> begrenzt.`
  - Keine Kamera (Warning): `WbDev: WbCam <Modus> erkannt, aber kein Fahrzeug mit Kamera.`

## WbHeli

- **Signatur:** `WbHeli`
- **Wirkung:** Uebernimmt (`Possess`) den ERSTEN `AWiesbadenHelicopter` in der
  Welt. Voraussetzung fuer alle `WbHeli*`-Befehle. Achtung: entlaedt damit ein
  zuvor besessenes Fahrzeug (relevant fuer den Rauchtest, der Fahr- und
  Heli-Tests deshalb in zwei getrennten Sitzungen laeuft).
- **Voraussetzung:** mindestens ein Helikopter in der Welt.
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbHeli - Helikopter <Name> uebernommen.`
  - Keiner vorhanden (Warning): `WbDev: WbHeli - kein Helikopter in der Welt.`

## WbNudge

- **Signatur:** `WbNudge <NickGrad:int> <RollGrad:int>`
- **Wirkung:** Kippt den besessenen Pawn RELATIV um die angegebenen Grad in Nick
  und Roll (per `TeleportPhysics`); der Yaw bleibt. Diagnosehilfe fuer
  Lage-/Auftriebstests. Hinweis: Rotator-Werte werden normalisiert, sehr grosse
  Winkel (z. B. 720) wirken damit wie 0.
- **Voraussetzung:** ein besessener Pawn.
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbNudge <nick>/<roll>: Nick/Roll <p>/<r> -> <p>/<r>.`
  - Kein Pawn (Warning): `WbDev: WbNudge erkannt, aber kein besessener Pawn.`

## WbHeliYaw

- **Signatur:** `WbHeliYaw <Sekunden:int>`
- **Wirkung:** Startet auf dem besessenen Helikopter eine Gierprobe des
  Test-Harness (`StartYawProbe`) fuer die angegebene Dauer. Der Harness wird bei
  Bedarf on-demand am Pawn erzeugt (`GetOrAddHarness`), damit die Fahrzeugklasse
  im Normalspiel frei von Testcode bleibt.
- **Voraussetzung:** ein besessener `AWiesbadenHelicopter` (zuvor `WbHeli`).
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbHeliYaw - Gierprobe fuer <s> s gestartet.`
  - Kein Heli (Warning): `WbDev: WbHeliYaw erkannt, aber kein Helikopter besessen (erst WbHeli).`

## WbHeliFly

- **Signatur:** `WbHeliFly <Sekunden:int>`
- **Wirkung:** Startet auf dem besessenen Helikopter ein Flugprofil des
  Test-Harness (`StartFlightProfile`) fuer die angegebene Dauer. Harness wird bei
  Bedarf on-demand erzeugt.
- **Voraussetzung:** ein besessener `AWiesbadenHelicopter` (zuvor `WbHeli`).
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbHeliFly - Flugprofil fuer <s> s gestartet.`
  - Kein Heli (Warning): `WbDev: WbHeliFly erkannt, aber kein Helikopter besessen (erst WbHeli).`

## WbDrive

- **Signatur:** `WbDrive <Sekunden:int>`
- **Wirkung:** Startet auf dem besessenen Fahrzeug ein Fahrprofil des
  Test-Harness (`StartDriveProfile`) fuer die angegebene Dauer. Harness wird bei
  Bedarf on-demand erzeugt. Der Rauchtest prueft damit Tempo (>20 km/h) und
  Kursaenderung (>15 Grad) am Standard-Kaefer.
- **Voraussetzung:** ein besessener `AWiesbadenCar`.
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbDrive - Fahrprofil fuer <s> s gestartet.`
  - Kein Fahrzeug (Warning): `WbDev: WbDrive erkannt, aber kein Fahrzeug besessen.`

## WbHeliGoto

- **Signatur:** `WbHeliGoto <dx:int m> <dy:int m> <dz:int m>`
- **Wirkung:** Der Autopilot (`UWiesbadenHelicopterAutopilot.FlyTo`) fliegt zu
  einem Ziel RELATIV zur aktuellen Heli-Position (dx/dy/dz in Metern, intern
  * 100 in cm) und haelt es. Der Autopilot wird on-demand am Heli erzeugt
  (`GetOrAddAutopilot`). Fortschritt loggt der Autopilot selbst
  (`WbDev Autopilot t=N: Abstand X m ... Modus Anflug/Halten`, `Wegpunkt erreicht`).
- **Voraussetzung:** ein besessener `AWiesbadenHelicopter` (zuvor `WbHeli`).
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbHeliGoto - Autopilot fliegt zu (<dx>, <dy>, <dz>) m relativ.`
  - Kein Heli (Warning): `WbDev: WbHeliGoto erkannt, aber kein Helikopter besessen (erst WbHeli).`

## WbHeliHover

- **Signatur:** `WbHeliHover`
- **Wirkung:** Der Autopilot haelt die AKTUELLE Position
  (`UWiesbadenHelicopterAutopilot.HoldPosition`). Autopilot wird on-demand
  erzeugt.
- **Voraussetzung:** ein besessener `AWiesbadenHelicopter` (zuvor `WbHeli`).
- **Log-Nachweis:**
  - Erfolg: `WbDev: WbHeliHover - Autopilot haelt die Position.`
  - Kein Heli (Warning): `WbDev: WbHeliHover erkannt, aber kein Helikopter besessen (erst WbHeli).`

## WbHeliOff

- **Signatur:** `WbHeliOff`
- **Wirkung:** Schaltet einen VORHANDENEN Autopiloten ab (`Disengage`) und gibt
  die Steuerung an Tastatur/Gamepad zurueck - auch mitten im Flug. Legt bewusst
  keinen Autopiloten an, wenn keiner existiert.
- **Voraussetzung:** ein besessener `AWiesbadenHelicopter`.
- **Log-Nachweis:**
  - Autopilot war aktiv: `WbDev: WbHeliOff - Autopilot aus, Steuerung zurueck an Tastatur/Gamepad.`
  - Keiner aktiv: `WbDev: WbHeliOff - kein Autopilot aktiv.`
  - Kein Heli (Warning): `WbDev: WbHeliOff erkannt, aber kein Helikopter besessen.`

---

## Typische Abfolgen

- **Helikopter-Autopilot testen:** `WbHeli,WbHeliGoto 200 0 50,WbHeliHover` -
  Heli uebernehmen, 200 m nach vorn und 50 m hoch fliegen, dann halten.
- **Fahrtest (Rauchtest-Muster):** `WbDrive 10` am besessenen Kaefer.
- **Ueberschlagenen Wagen bergen:** `WbResetVehicle`.

## Verwandtes

- `Source/WiesbadenReal/Core/WiesbadenPlayerController.cpp` - Implementierung.
- `Source/WiesbadenReal/Core/WiesbadenDevActions.*` - datenreine Teleport-/
  Aufrichte-Logik (geteilt mit dem Pause-Menue).
- `Tools/smoke_test.cmd` / `smoke_test.ps1` - automatisierter Rauchtest, der
  diese Befehle feuert und das Log auswertet.
- `Tools/check_wbdev_docs.ps1` - statische Drift-Pruefung: haelt diese Referenz
  mit dem Code synchron (jeder Befehl existiert, jede Log-Zeile stimmt woertlich).
  Laeuft als erste Pruefung im Rauchtest. Wer hier eine Log-Zeile oder einen
  Befehl aendert, muss den Code mitziehen - sonst faellt die Pruefung durch.
