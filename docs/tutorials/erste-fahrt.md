# Tutorial: Erste Fahrt durch Wiesbaden

Willkommen. In diesem Tutorial bringst du WiesbadenReal von einem frischen
Checkout zum Laufen, siehst die Stadt zum ersten Mal, faehrst mit dem Kaefer eine
Runde und laesst am Ende einen Helikopter ueber Wiesbaden fliegen. Kein
Vorwissen ueber die Codebasis noetig - folge einfach Schritt fuer Schritt, jeder
Schritt fuehrt zum naechsten.

Am Ende hast du das Projekt selbst gebaut, gestartet, gefahren und geflogen. Das
dauert insgesamt etwa 20 Minuten, das meiste davon Warten auf Build und Laden.

> Dies ist ein LERN-Einstieg. Das *Warum* hinter den Systemen steht in den
> Erklaerungen (`docs/explanation/`), die genauen Nachschlage-Details in der
> Referenz (`docs/reference/`). Hier geht es nur darum, dass es bei dir laeuft.

## Was du brauchst

- Den Projekt-Checkout unter `C:\freebuff\WiesbadenReal_Sicherung\`.
- Die Engine daneben unter `...\UE_5.8\` und einen installierten C++-Compiler
  (Visual Studio 2022). Beides ist auf diesem Rechner schon da.
- Ein PowerShell-Fenster.

## Schritt 1: Das Spiel bauen

Zuerst uebersetzen wir das Editor-Target - das bestaetigt, dass deine Werkzeugkette
steht. Kopiere das in PowerShell:

```powershell
$build = "C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Build\BatchFiles\Build.bat"
$proj  = "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject"
& $build WiesbadenRealEditor Win64 Development -project="$proj" -waitmutex
```

Beim ersten Mal dauert das einige Minuten. Du wartest auf die letzte Zeile:

```
Result: Succeeded
```

Steht dort `Succeeded`, ist alles bereit. (Erscheint stattdessen ein Fehler, ist
meist der Compiler nicht gefunden - dann fehlt Visual Studio.)

## Schritt 2: Wiesbaden zum ersten Mal sehen

Jetzt starten wir das Spiel in einem Fenster. Kopiere:

```powershell
$exe = "C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$proj = "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject"
Start-Process -FilePath $exe -ArgumentList @("`"$proj`"","-game","-windowed","-resx=1600","-resy=900")
```

Ein Fenster oeffnet sich und laedt - das Laden der Stadt dauert etwa **25-40
Sekunden** (World Partition streamt die Umgebung ein). Hab Geduld; wenn es kurz
haengt, laedt es noch. Dann siehst du: du sitzt in einem Kaefer irgendwo in
Wiesbaden, mit der Stadt rundherum. Das ist deine erste Fahrt-Ausgangslage.

## Schritt 3: Ans Steuer - fahr eine Runde

**Klicke einmal ins Fenster**, damit es die Tastatur annimmt. Dann faehrst du mit
den ueblichen Tasten:

| Taste | Wirkung |
| --- | --- |
| `W` | Gas / vorwaerts |
| `S` | Bremse / rueckwaerts halten |
| `A` / `D` | Lenken links / rechts |
| `Leertaste` | Handbremse |
| `R` | Rueckwaertsgang |
| `L` | Licht |
| `Q` / `E` | Blinker links / rechts |

Druecke `W` und halte es - der Kaefer beschleunigt, das Getriebe schaltet hoch.
Lenke mit `A` und `D` um eine Ecke. Fahr ruhig ein paar Strassen weit; du kannst
nichts kaputt machen. Wenn du das Gefuehl fuers Fahren hast, geht es weiter.

> `Esc` oeffnet das Pausenmenue, falls du eine Pause brauchst.

## Schritt 4: Aussteigen und zum Helikopter laufen

In der Naehe deines Startpunkts steht ein Helikopter. Dahin gehst du jetzt zu Fuss.

Druecke **`F`** - du steigst aus dem Kaefer aus und stehst als Figur daneben. Zu
Fuss steuerst du mit denselben Richtungstasten (`W`/`A`/`S`/`D`) und der Maus zum
Umschauen. Laufe zu dem Helikopter hinueber (der grosse Rotor ist gut zu sehen).

Stehst du direkt daneben, druecke wieder **`F`** - dieselbe Taste, mit der du aus-
gestiegen bist, laesst dich in das naechste Fahrzeug EINsteigen. Jetzt sitzt du im
Helikopter.

> `F` ist die eine Taste fuers Ein- und Aussteigen: im Fahrzeug steigst du aus, zu
> Fuss neben einem Fahrzeug steigst du ein. Findet sie kein eigenes Fahrzeug in
> der Naehe, uebernimmt sie sogar ein vorbeikommendes Verkehrsauto.

## Schritt 5: Dein erster Helikopterflug

Einen Koaxial-Hubschrauber von Hand ruhig zu fliegen ist knifflig - das lernst du
in Schritt 6. Damit dein erster Flug aber garantiert gelingt, lassen wir zuerst den
**Autopiloten** fliegen. Schliesse das Spielfenster und starte es so:

```powershell
$exe = "C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$proj = "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject"
Start-Process -FilePath $exe -ArgumentList @("`"$proj`"","-game","-windowed","-resx=1600","-resy=900","-ExecCmds=`"WbHeli,WbHeliGoto 100 0 40`"")
```

Der Zusatz `-ExecCmds="WbHeli,WbHeliGoto 100 0 40"` bedeutet: uebernimm den
Helikopter und flieg 100 m nach vorn und 40 m hoch. Nach dem Laden siehst du im
Fenster, wie der Heli anhebt, vorwaerts fliegt, steigt und dann seine Position
haelt. Das ist dein erster Helikopterflug ueber Wiesbaden - du hast ihn ausgeloest.

Der Flug dauert etwa **40 Sekunden** (bewusst gemaechlich). Wenn der Heli oben
ankommt und ruhig stehen bleibt, hat es geklappt.

## Schritt 6 (Kuer): Flieg selbst

Willst du selbst fliegen? Starte das Spiel wieder normal (wie in Schritt 2),
steig wie in Schritt 4 in den Helikopter, klicke ins Fenster und dann:

| Taste | Wirkung |
| --- | --- |
| `G` | Motor an/aus - **zuerst druecken**, sonst dreht der Rotor nicht |
| `Leertaste` / `Shift` | Kollektiv HOCH (steigen) |
| `Strg` | Kollektiv RUNTER (sinken) |
| `W` / `S` | Nase kippen vor / zurueck (vorwaerts/rueckwaerts) |
| `A` / `D` | zur Seite neigen links / rechts |
| `Q` / `E` | Gieren (Nase drehen) links / rechts |

Starte mit `G`, dann gib mit `Leertaste` etwas Kollektiv, bis der Heli sanft
abhebt - und nimm es sofort wieder etwas zurueck, um in der Schwebe zu bleiben.
Kleine Eingaben! Der Hubschrauber reagiert traeger, als man denkt. Wenn er
davonzudriften droht, hilft ein kurzer Gegen-Tipp mit `W`/`S`/`A`/`D`.

Kein Stress, wenn es wackelt - echtes Hubschrauberfliegen ist schwer. Du hast
mit dem Autopiloten aus Schritt 5 immer den sicheren Weg, den Heli fliegen zu
sehen.

## Was du geschafft hast

- Das Projekt selbst gebaut (`Result: Succeeded`).
- Wiesbaden gestartet und gesehen.
- Den Kaefer durch die Stadt gefahren.
- Zu Fuss zum Helikopter gewechselt.
- Deinen ersten Helikopterflug ueber Wiesbaden ausgeloest.

Das ist der komplette Grundkreis des Spiels - fahren, aussteigen, fliegen.

## Wie es weitergeht

- Willst du das Fahr- oder Flugverhalten gezielt PRUEFEN (statt nur zu erleben)?
  Die How-tos zeigen es: `docs/how-to/fahrverhalten-testen.md` und
  `docs/how-to/helikopter-autopilot-testen.md`.
- Willst du alle Entwickler-Befehle (wie `WbHeliGoto`) nachschlagen?
  `docs/reference/wbdev-konsolenbefehle.md`.
- Willst du VERSTEHEN, warum die Stadt so fluessig streamt?
  `docs/explanation/streaming-anker.md`.
