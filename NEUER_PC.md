# Wiesbaden Real auf einem neuen Rechner einrichten

Diese Sicherung enthaelt alles, was sich **nicht** wiederherstellen laesst.
Was fehlt, erzeugt Unreal beim ersten Start selbst.

## Was auf der Platte liegt

```
WiesbadenReal_Sicherung\
  WiesbadenReal\        das Unreal-Projekt (Quelltext, Inhalte, Rohdaten)
  UE_5.8\               Unreal Engine 5.8.1 - nur noch RUECKFALL (installiert ist 5.8.2)
  Epic\
    Launcher\           der Epic Games Launcher
    ProgramData_Epic\   die Manifeste, an denen er Installationen erkennt
  Blender\              Modelle und Texturen ausserhalb des Projekts
  Quellen\              wbnracing, vw-beetle-1969
  Claude\               Gedaechtnisdateien und Arbeitsskripte
  kopie.log             Protokoll des Kopierlaufs
```

## Voraussetzungen auf dem neuen Rechner

| | |
|---|---|
| Unreal Engine | **5.8** - auf diesem Rechner installiert als 5.8.2 unter `C:\Program Files\Epic Games\UE_5.8`; die 5.8.1 auf der Platte ist nur Rueckfall |
| Visual Studio | 2022 oder neuer, mit **Desktopentwicklung mit C++** und **Spieleentwicklung mit C++** |
| Windows SDK | 10.0.22621 oder neuer |
| Blender | 4.x, falls die Modelle bearbeitet werden sollen |

Visual Studio ist das Einzige, was wirklich geladen werden muss. Ohne die
beiden C++-Arbeitslasten laesst sich das Projekt nicht uebersetzen - Unreal
meldet dann nur, dass kein Compiler gefunden wurde.

## Einrichten

**1. Engine an ihren Platz kopieren**

`UE_5.8` von dieser Platte nach `C:\Program Files\Epic Games\UE_5.8`.

Auf diesem Rechner ist das bereits erledigt: Dort liegt die **5.8.2**
(Launcher-Installation, CL 56702186), und alle `.cmd`-Dateien/Tools zeigen
auf diesen Pfad. Die Plattenkopie `WiesbadenReal_Sicherung\UE_5.8` (5.8.1,
CL 56057345) ist nur noch Rueckfall - ein Wechsel zurueck erfordert einen
Rebuild, weil `Intermediate`/`Binaries` gegen 5.8.2 gebaut werden.

Genau dieser Pfad: Die Registrierung des Altrechners lautete

```
HKLM\SOFTWARE\EpicGames\Unreal Engine    INSTALLDIR = C:\Program Files\Epic Games\
```

**Zum Uebersetzen und Spielen ist gar keine Registrierung noetig.** Alle
`.cmd`-Dateien dieses Projekts rufen `Build.bat` und `UnrealEditor.exe` ueber
ihren vollen Pfad auf - so laeuft hier ohnehin schon alles.

Wer die Doppelklick-Verknuepfung fuer `.uproject` moechte, braucht den Epic
Games Launcher. Er liegt unter `Epic\Launcher` mit dabei, samt der Manifeste
unter `Epic\ProgramData_Epic` (gehoeren nach `C:\ProgramData\Epic`), an denen
er eine vorhandene Installation erkennt. Sauberer ist es, den Launcher frisch
zu installieren - er ist nur rund 1 GB - und die Manifeste danach zu
ergaenzen.

Achtung: `UnrealVersionSelector.exe` liegt **nicht** in der Engine, sondern
unter `Launcher\Engine\Binaries\Win64\`. Im Engine-Ordner sucht man vergebens.

**2. Projekt an seinen Platz kopieren**

Am besten wieder nach `C:\Users\<Name>\aivideo\WiesbadenReal`. Ein anderer
Pfad geht auch, aber dann muessen die absoluten Pfade in den `.cmd`-Dateien
und in `Tools\*.py` angepasst werden - sie stehen dort ausgeschrieben.

**3. Uebersetzen**

```
Build.bat WiesbadenRealEditor Win64 Development -project=<Pfad>\WiesbadenReal.uproject
```

`Build.bat` liegt unter
`C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\`.

Der erste Lauf dauert lange - er baut auch die Engine-Abhaengigkeiten. Danach
sind es rund 30 Sekunden je Aenderung.

**4. Pruefen**

```
run_tests.cmd
```

Erwartet werden **132 bestandene Tests, keine Fehlschlaege**. Weniger heisst,
dass etwas beim Kopieren verlorengegangen ist.

Achtung: Die Testergebnisse stehen **nicht** in `build_test.log`, sondern in
`Saved\Logs\WiesbadenReal.log`. Nach `Result={` suchen.

**5. Materialien anlegen**

Zwei Materialien werden im Editor erzeugt und liegen nicht als Quelltext vor:

```
run_materials.cmd
UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript ^
    -script=Tools\build_vertexcolor_material.py -unattended -nop4
```

Fehlen sie, wird die Stadt im **Standardmaterial** gezeichnet - sie sieht dann
komplett grau aus.

**6. Starten**

Die gebackene Karte liegt bereit:

| Karte | Stand |
|---|---|
| `WiesbadenCity_Alkis10` | aktuell (auch GameDefaultMap) |
| `WiesbadenCity_Alkis4` | der vorherige Stand, als Rueckfall |

## Die Stadt neu bauen

Nur noetig, wenn die Rohdaten sich aendern oder der Generator angefasst wurde:

```
rebuild_stadt.cmd
```

Baut aus `Data\Raw` (OSM, SRTM-Hoehenmodell, ALKIS-Grundrisse) eine neue
Karte. Dauer rund 10 Minuten auf acht Kernen, **speicherhungrig** - waehrend
des Laufs nichts Grosses parallel starten.

Wichtig: Der Bau braucht den **vollen Editor**, nicht `UnrealEditor-Cmd`. Ueber
den Kommandlet-Weg ist er an 42 GiB virtuellem Speicher gescheitert.

## Hoehenpruefung

```
check_hoehen.cmd
```

Vergleicht alle 22.227 Kreuzungen mit dem Gelaende darunter und schreibt
`hoehen_report.json`. Der Stand vom 31.08.2026:

| | |
|---|---|
| mittlere Abweichung | 35,9 cm |
| schlimmster Fall | 3,59 m |
| ueber 10 m daneben | 0 |

Deutlich schlechtere Werte bedeuten, dass die Gelaende-Hoehenskalierung wieder
klemmt - siehe `AGENTS.md`, Abschnitt "16 Bit Heightmap".

## Was NICHT mitkopiert wurde

`Intermediate`, `Binaries`, `Saved`, `DerivedDataCache` und die veraltete Karte
`WiesbadenCity_Alkis2`. Zusammen ueber 25 GB, die Unreal beim ersten Bau
ohnehin neu erzeugt.

## Weiterlesen

`AGENTS.md` im Projektstamm ist das Wichtigste: rund 1.950 Zeilen mit den
Fallen, die dieses Projekt schon gestellt hat - vom stillen Abschneiden des
Gelaendes ueber falsche Abtasttypen bis zu Exit-Codes, die luegen. Wer daran
weiterarbeitet, spart sich damit dieselben Tage.
