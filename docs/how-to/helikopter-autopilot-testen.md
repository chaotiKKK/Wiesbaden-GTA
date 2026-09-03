# How-to: Den Helikopter-Autopiloten per WbDev-Befehlen testen

Diese Anleitung zeigt Schritt fuer Schritt, wie man den Anflug-, Halte- und
Abschalt-Weg des Helikopter-Autopiloten in einer Editor-Sitzung ausloest und am
Log nachweist, dass er funktioniert.

Sie setzt voraus, dass du das Editor-Target schon einmal gebaut hast und eine
Karte mit mindestens einem `AWiesbadenHelicopter` geladen wird. Die Befehle
selbst sind in `docs/reference/wbdev-konsolenbefehle.md` einzeln beschrieben;
hier geht es um den Ablauf und die Auswertung.

## Ueberblick

Der Test besteht aus drei nachweisbaren Phasen:

1. **Anflug** (`WbHeliGoto`) - der Heli fliegt zu einem relativen Ziel.
2. **Halten** - nach Ankunft haelt der Autopilot die Position von selbst
   (oder explizit per `WbHeliHover`).
3. **Abschalten** (`WbHeliOff`) - Steuerung zurueck an die Eingabe.

Der Autopilot loggt seinen Fortschritt im Sekundentakt selbst. Nachweis =
Abstand sinkt, "Wegpunkt erreicht" erscheint, danach bleibt der Abstand klein.

## Schritt 1: Sitzung mit Anflugbefehl starten

`-ExecCmds` feuert alle Befehle direkt beim Start (komma-getrennt). Fuer den
Anflug reicht **`WbHeli`** (Heli uebernehmen) gefolgt von **`WbHeliGoto`** - der
Autopilot fliegt danach eigenstaendig ueber mehrere Sekunden, waehrend das Spiel
laeuft. `WbHeliHover`/`WbHeliOff` gehoeren NICHT in dieselbe Kette (sie wuerden
sofort mitfeuern, bevor der Heli losfliegt) - fuer die nutzt du die Konsole
interaktiv (Schritt 5) oder eine eigene, spaeter gestartete Sitzung.

Beispiel (PowerShell; Pfade ggf. anpassen), Ziel 300 m nach vorn und 60 m hoch:

```powershell
$exe  = "C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$proj = "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject"
$log  = "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\WbAutopilot.log"
Start-Process -FilePath $exe -ArgumentList @(
    "`"$proj`"", "-game", "-windowed", "-resx=1280", "-resy=720",
    "-ExecCmds=`"WbHeli,WbHeliGoto 300 0 60`"", "-ABSLOG=$log")
```

`WbHeliGoto` nimmt die Argumente in **Metern relativ** zur aktuellen
Heli-Position (dx vorn, dy rechts, dz hoch).

## Schritt 2: Genug Laufzeit geben

Das Anflugtempo ist bewusst niedrig (~4-5 m/s horizontal), damit der Heli nicht
ueberschiesst - der Anflug dauert entsprechend lange und SKALIERT mit der
Distanz. Fuer das Beispielziel (300 m) sind es real **~80 s** bis "Wegpunkt
erreicht". Lass die Sitzung also grosszuegig **~90 s** laufen, bevor du das Log
auswertest, sonst fehlt die "Wegpunkt erreicht"-Zeile noch. Willst du es eilig
haben, nimm ein kleineres Ziel: `WbHeliGoto 100 0 40` kommt in ~25-35 s an. Beim
World-Partition-Streaming kann es zwischendurch haengen ("Gamethread hitch"); das
ist normal, warte auf die Log-Messpunkte, nicht auf ein bestimmtes sichtbares Ende.

## Schritt 3: Die Autopilot-Zeilen aus dem Log ziehen

Die Fortschrittszeilen laufen unter der Kategorie **`LogWbVehicles`** (nicht
`LogWbCore` - das sind nur die Befehls-Echos). So filterst du sie:

```bash
grep -aE "WbDev Autopilot" "Saved/Logs/WbAutopilot.log"
```

Erwartete Ausgabe (Werte beispielhaft):

```
LogWbVehicles: WbDev Autopilot t=0: Abstand 306 m (horiz 300 m, Hoehe +60 m), Tempo 0 km/h, Modus Anflug.
LogWbVehicles: WbDev Autopilot t=7: Abstand 287 m (horiz 285 m, Hoehe +32 m), Tempo 13 km/h, Modus Anflug.
...
LogWbVehicles: WbDev Autopilot t=80: Abstand 20 m (horiz 20 m, Hoehe +0 m), Tempo 14 km/h, Modus Anflug.
LogWbVehicles: WbDev Autopilot: Wegpunkt erreicht nach 82.0 s (Abstand 16.0 m) - halte Position.
LogWbVehicles: WbDev Autopilot t=83: Abstand 14 m (horiz 14 m, Hoehe +0 m), Tempo 13 km/h, Modus Anflug.
```

**Wichtig:** Der Modus bleibt auch NACH "Wegpunkt erreicht" auf `Anflug`. Das ist
korrekt - `WbHeliGoto` nutzt EIN Regelgesetz fuer Anflug und Schweben: bei Abstand
nahe 0 geht die Ziel-Geschwindigkeit gegen 0, der Heli haelt die Position, ohne je
den Modus zu wechseln. `Modus Halten` erscheint NUR nach explizitem `WbHeliHover`
(Schritt 5), nicht durch die Goto-Ankunft.

## Schritt 4: Den Anflug bewerten

Der Test ist **bestanden**, wenn:

- **Abstand faellt ueber die Zeit** (grob monoton) von Start Richtung 0.
- Die Zeile **`Wegpunkt erreicht`** genau einmal erscheint.
- Danach bleibt der **Abstand klein** (wenige Meter), driftet nicht wieder weg.
  Der Modus bleibt dabei `Anflug` - das ist KORREKT und KEIN Durchfall (das
  Goto-Gesetz haelt selbst; ein automatischer Wechsel auf `Halten` findet NICHT
  statt, siehe Schritt 3).

Bekannte, tolerierte Eigenheit: bei hoeherem Anflugtempo (naeheres/kleineres Ziel)
kann der Abstand kurz vor dem Ziel **ueberschwingen**, bevor er sich einpendelt -
die Bremsautoritaet nahe Schweben ist plant-bedingt schwach (siehe AGENTS.md,
Autopilot-Bullet). Ein Ueberschwinger ist also KEIN Durchfall, solange am Ende
"Wegpunkt erreicht" kommt und der Abstand danach klein bleibt.

**Durchgefallen**, wenn: keine `WbDev Autopilot`-Zeile (Heli nicht uebernommen,
siehe Fehlersuche), Abstand steigt dauerhaft, oder "Wegpunkt erreicht" fehlt nach
reichlich Laufzeit.

## Schritt 5: Halten und Abschalten interaktiv pruefen

Weil `WbHeliHover`/`WbHeliOff` zeitlich nach dem Anflug kommen muessen, testest
du sie ueber die **In-Game-Konsole** in einer laufenden Sitzung:

1. Konsole oeffnen und `WbHeli` tippen (falls noch nicht besessen).
2. `WbHeliGoto 200 0 40` - warten, bis das Log "Wegpunkt erreicht" zeigt.
3. `WbHeliHover` - im Log erscheint `WbDev: WbHeliHover - Autopilot haelt die
   Position.` (Kategorie `LogWbCore`); die Fortschrittszeilen bleiben auf
   `Modus Halten` mit kleinem Abstand.
4. `WbHeliOff` - im Log erscheint `WbDev: WbHeliOff - Autopilot aus, Steuerung
   zurueck an Tastatur/Gamepad.`; ab hier reagiert der Heli wieder auf die
   Eingabe und die Autopilot-Fortschrittszeilen hoeren auf.

## Fehlersuche

| Symptom im Log | Ursache / Abhilfe |
| --- | --- |
| Keine `WbDev Autopilot`-Zeile | Heli wurde nicht uebernommen. Pruefe auf `LogWbCore: WbDev: WbHeli - kein Helikopter in der Welt.` - dann fehlt der Heli auf der Karte. |
| `WbDev: WbHeliGoto erkannt, aber kein Helikopter besessen` | `WbHeli` lief nicht oder scheiterte. Reihenfolge in `-ExecCmds` pruefen (erst `WbHeli`, dann `WbHeliGoto`). |
| Zeitstempel wirken "in der Zukunft" | UE-Logzeitstempel sind **UTC**; lokal ist UTC+2. Kein Fehler. |
| Abstand pendelt lange um den Rand | Erwartetes plant-bedingtes Ueberschwingen; solange "Wegpunkt erreicht" folgt, bestanden. |
| `Wegpunkt erreicht` wiederholt sich | Sollte nicht vorkommen - `bArrivedLogged` wird nur von `FlyTo`/`HoldPosition` zurueckgesetzt. Tritt es doch auf, ist es ein Regressions-Hinweis. |

## Verwandtes

- `docs/reference/wbdev-konsolenbefehle.md` - Befehlsreferenz mit allen Log-Zeilen.
- `Source/WiesbadenReal/Vehicles/WiesbadenHelicopterAutopilot.cpp` - der Regler.
- `Tools/smoke_test.ps1` - automatisierter Rauchtest (feuert `WbHeliFly`/`WbHeliYaw`
  ueber den Test-Harness, nicht den Autopiloten - anderer Pfad).
