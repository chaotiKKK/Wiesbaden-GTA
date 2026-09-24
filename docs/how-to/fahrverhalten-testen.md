# How-to: Das Fahrverhalten per WbDrive im Editor testen

Diese Anleitung zeigt Schritt fuer Schritt, wie man das Fahrverhalten des
besessenen Fahrzeugs (Standard-Kaefer) ueber die echte Fahrphysik ausloest und am
Log nachweist, dass es **beschleunigt** (Laengsdynamik) und **lenkt**
(Kursaenderung) - ohne Tastatur, im gerenderten Unreal-Spielbetrieb.

Sie setzt voraus, dass das Editor-Target gebaut ist. Die Befehlsreferenz steht in
`docs/reference/wbdev-konsolenbefehle.md`; hier geht es um Ablauf und Auswertung.

## Ueberblick

`WbDrive <s>` faehrt den besessenen `AWiesbadenCar` ueber den Test-Harness per
`SetExternalControl` (das Physikmodell des Standard-Kaefers) in vier Phasen:

1. **Beschleunigen** (0-40 % der Dauer): Vollgas geradeaus -> Tempo steigt, Gang
   schaltet hoch.
2. **Rechts lenken** (40-60 %): Vollgas + Lenkeinschlag rechts -> Kursaenderung
   waechst ins Positive.
3. **Links lenken** (60-80 %): Lenkeinschlag links -> die Kursaenderung geht
   wieder zurueck.
4. **Bremsen** (80-100 %): Gas weg, volle Bremse.

Mit `-WbDriveReverse` beim Spielstart laeuft dasselbe Profil rueckwaerts.
Das ist nur ein Test-Harness-Schalter; die normale Fahrzeugsteuerung bleibt
unveraendert. `WbDrive 12` prueft Anfahren, Gegenlenken und Bremsen; Gang -1
und steigende Motordrehzahl muessen im Log stehen, nicht 400 U/min bei Fahrt.

Nachweis = das Log zeigt Tempo deutlich > 0 UND eine Kursaenderung deutlich != 0.

## Schritt 1: Sitzung starten

Der Standard-Kaefer ist bei Spielstart besessen - `WbDrive` wirkt sofort auf ihn,
**ohne** vorheriges `WbHeli`. Wichtig: `WbHeli` wuerde den Kaefer ENTLADEN, dann
liefe `WbDrive` ins Leere ("kein Fahrzeug besessen"). Fahr- und Heli-Nachweise
gehoeren daher in getrennte Sitzungen.

```powershell
$exe  = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$proj = "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject"
$log  = "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\WbDrive.log"
Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2
Remove-Item $log -ErrorAction SilentlyContinue
Start-Process -FilePath $exe -ArgumentList @(
    "`"$proj`"", "-game", "-windowed", "-resx=1280", "-resy=720",
    "-ExecCmds=`"WbDrive 10`"", "-ABSLOG=$log")
```

`10` ist die Fahrdauer in Sekunden. Kuerzer als ~6 s laesst kaum Zeit fuer die
Lenkphase - 8-10 s zeigen beide Phasen deutlich.

## Schritt 2: Genug Laufzeit geben

Das Laden dauert ~25-38 s, DANN laeuft `WbDrive` die angegebenen Sekunden. Warte
auf die Abschlusszeile `WbDev Fahren fertig` (also grob Ladezeit + Fahrdauer,
~50 s bei `WbDrive 10`), bevor du auswertest.

## Schritt 3: Die Fahr-Zeilen aus dem Log ziehen

Die Messpunkte laufen im Sekundentakt unter der Kategorie **`LogWbVehicles`**
(nicht `LogWbCore` - dort steht nur das Befehls-Echo `WbDev: WbDrive ... gestartet`).

```bash
grep -aE "WbDev Fahrt" "Saved/Logs/WbDrive.log"
```

Echte Ausgabe (`WbDrive 10`, Standard-Kaefer):

```
LogWbVehicles: WbDev Fahrt t=0: Tempo 0 km/h, Kursaenderung -0 Grad, Gang 1.
LogWbVehicles: WbDev Fahrt t=2: Tempo 16 km/h, Kursaenderung -0 Grad, Gang 1.
LogWbVehicles: WbDev Fahrt t=4: Tempo 37 km/h, Kursaenderung -0 Grad, Gang 2.
LogWbVehicles: WbDev Fahrt t=5: Tempo 43 km/h, Kursaenderung +15 Grad, Gang 2.
LogWbVehicles: WbDev Fahrt t=7: Tempo 57 km/h, Kursaenderung +73 Grad, Gang 2.
LogWbVehicles: WbDev Fahrt t=8: Tempo 63 km/h, Kursaenderung +66 Grad, Gang 3.
LogWbVehicles: WbDev Fahrt t=10: Tempo 70 km/h, Kursaenderung +20 Grad, Gang 3.
LogWbVehicles: WbDev Fahren fertig.
```

Lies die Zeilen so: bis t=4 baut sich das **Tempo** geradeaus auf (Kursaenderung
~0, Gang 1->2). Ab t=5 setzt der **Rechts**-Einschlag ein -> Kursaenderung waechst
(+15 -> +73). Ab ~t=8 lenkt der **Links**-Einschlag zurueck -> die Kursaenderung
faellt wieder (+66 -> +20). Der Spitzenwert (+73) ist der Lenk-Nachweis.

## Schritt 4: Das Fahrverhalten bewerten

Der Test ist **bestanden**, wenn zwei Dinge belegt sind (die Schwellen des
Rauchtests):

- **Laengsdynamik:** maximales Tempo **> 20 km/h**. Der Kaefer beschleunigt also
  wirklich (hier 70 km/h). Bleibt das Tempo bei ~0, greift die Fahrphysik nicht.
- **Lenkung:** maximaler Betrag der **Kursaenderung > 15 Grad**. Das Fahrzeug
  reagiert also auf Lenkbefehle (hier bis 73 Grad). Bleibt die Kursaenderung ~0,
  lenkt es nicht.

Als schnelle Auswertung Maxima ziehen:

```bash
grep -aoE "Tempo [0-9]+ km/h" "Saved/Logs/WbDrive.log" | grep -aoE "[0-9]+" | sort -n | tail -1
grep -aoE "Kursaenderung [+-][0-9]+" "Saved/Logs/WbDrive.log" | grep -aoE "[0-9]+" | sort -n | tail -1
```

Dass der **Gang** hochschaltet (1 -> 2 -> 3) ist ein Bonus-Beleg, dass Getriebe
und Drehzahl mitlaufen.

**Durchgefallen**, wenn: keine `WbDev Fahrt`-Zeile (Fahrzeug nicht besessen, siehe
Fehlersuche), Tempo bleibt ~0 (keine Laengsdynamik) oder Kursaenderung bleibt ~0
(keine Lenkung).

## Fehlersuche

| Symptom im Log | Ursache / Abhilfe |
| --- | --- |
| `LogWbCore: WbDev: WbDrive erkannt, aber kein Fahrzeug besessen.` | Ein `WbHeli` (auch in derselben `-ExecCmds`-Kette) hat den Kaefer entladen. WbDrive in eine eigene Sitzung OHNE WbHeli legen. |
| Keine `WbDev Fahrt`-Zeile, nur das Echo | Zu frueh gepollt - das Laden (~25-38 s) war noch nicht durch. Laenger warten (auf `WbDev Fahren fertig`). |
| Tempo baut auf, aber Kursaenderung ~0 | Fahrdauer zu kurz (<~6 s) - die Lenkphase (ab 45 %) wurde nie erreicht. `WbDrive 8`+ nehmen. |
| Zeitstempel wirken "in der Zukunft" | UE-Logzeitstempel sind **UTC** (lokal UTC+2). Kein Fehler. |

## Verwandtes

- `docs/reference/wbdev-konsolenbefehle.md` - Befehlsreferenz mit allen Log-Zeilen.
- `docs/how-to/helikopter-autopilot-testen.md` - das Gegenstueck fuer den Heli.
- `Source/WiesbadenReal/Vehicles/WiesbadenVehicleTestHarness.cpp` - das Fahrprofil
  (`StartDriveProfile`, Drei-Phasen-Sweep).
- `Tools/smoke_test.ps1` - automatisierter Rauchtest, der genau diese Pruefung
  (Tempo>20, Kursaenderung>15) buendelt.
