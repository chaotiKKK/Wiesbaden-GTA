# Plattenstrategie

Wer darf was löschen, bei welcher Zahl — und was passiert automatisch.
Gemessen an dieser Platte: 953 GB, Warnung bei 20 %, Abbruch bei 10 %.

## Die vier Klassen

| Klasse | Was | Löschregeln |
|---|---|---|
| `cache` | Abgeleitete Kopien, rechnen sich neu: Zen-DDC, Shader-DDC, npm/pip/gradle/D3DS-Caches | `platten_waechter.py --reinigen` (automatisch möglich) |
| `dev-builds` | Zeitgestempelte Diagnose-Builds des fremden Threads — nur ALT (außerhalb der letzten 2 und älter als 12 h) | nur `--auch-devbuilds`, default trocken; Abstimmung offen |
| `ausgabe` | Regenerierbar, aber teuer: `Saved/StagedBuilds`, `Saved/Cooked`, `Intermediate/Build`, Gate-Worktree-`Intermediate` | nur `--reinigen --auch-ausgabe`, bewusst manuell |
| `eingabe` | Von Werkzeugen gelesen, nie Müll: `Data/Raw`, `Saved/Diagnose`, `Saved/_aaa_source` | niemals |
| `geschützt` | Projektinhalt, Release, Fremd- und Nutzerdateien | niemals, außer der Mensch entscheidet |

Die Klasse steht bei jedem Pfad in `KANDIDATEN` (Tools/platten_waechter.py)
**mit Begründung** — eine Liste ohne Warum löscht irgendwann das Falsche.
Jeder Löschlauf schreibt sein Protokoll VOR dem Eingriff nach
`Saved/Diagnose/loeschprotokoll.jsonl` (Phase „absicht", dann „ergebnis").

## Die drei Schwellen

- **20 %** — der Wächter meldet (Hinweis im Gate-Log, kein Gate).
- **14 %** — der Platten-Hinweis in vor_dem_commit räumt **automatisch
  in zwei Stufen** (fail-open, abschaltbar mit
  `WB_PLATTEN_AUTO_REINIGUNG=0`; die Testsuite schaltet so ab):
  **Stufe 1** die Cache-Klasse; reicht das nicht, **Stufe 2** die
  Ausgabe-Klasse — hinter einer Zeitkosten-Warnung im Gate-Log, die die
  Erwartung festhält: **der nächste Build wird ein Vollbuild** (gemessen
  27.09.2026: Gate 1 im Gate-Worktree 104 s → 181 s). Ein Engine-Lock
  stoppt Stufe 2 (Objektdateien unter dem Compiler erzeugen halbe
  Builds); leert auch sie nichts, verweist der Lauf auf diese Doku —
  nicht-regenerierbare Daten (dev-builds, Nutzerdaten) entscheidet kein
  Gate allein.
- **10 %** — `engine_run_lock.ps1` bricht JEDEN Engine-Start ab (Exit 4).
  Notausgang `-PlattenTrotz`, nur wenn der Editor zum Aufräumen gebraucht
  wird.

`ausgabe` bleibt manuell über `--auch-ausgabe` möglich — der Auto-Lauf
erreicht sie als Stufe 2 nur in der Ausnahmelage (Cache hat die Schwelle
nicht gerettet), und selbst dann mit Warnung und Engine-Lock-Stop. Ihr
Weggang kostet Zeit: Ohne `Intermediate/Build` wird der nächste Build
ein Vollbuild (im Gate-Worktree gemessen: Gate 1 von 104 s auf 181 s);
StagedBuilds und Gate-Intermediate baut Cook bzw. nächster Gate-Lauf neu.

## Wer was freigeben darf

- **Der Wächter (automatisch):** Stufe 1 nur `cache`, Stufe 2 nur
  `ausgabe` — beide nur unter 14 %, nur mit Protokoll; Stufe 2 nur nach
  der Zeitkosten-Warnung und nie unter Engine-Lock. Fehlt der Wächter
  oder bricht er, ist das eine Zeile im Log — niemals ein rotes Gate.
- **Der Bediener (manuell):** `--auch-ausgabe` nach Abwägung der
  Bauzeit-Kosten, im Fehlerfall mit der Protokoll-Datei als Beleg.
- **Niemand allein — aber vorbereitet:** `Saved/Package/dev-builds`
  (Stand 29.09.2026: 13 zeitgestempelte Diagnose-Builds, 49 GB, ~32 GB
  davon an einem Tag) bleibt als `geschützt` eingetragen, weil es die
  **Beweise des fremden Threads** trägt. Der Wächter implementiert seit
  dem 29.09.2026 den Keep-N-Kompromiss als eigene Klasse `dev-builds`:
  die letzten 2 Build-Bäume und alles jüngere als 12 h bleiben, Älteres
  erscheint im Bericht unter „zur Entsorgung vorgemerkt“ und wird erst
  mit `--reinigen --trocken --auch-devbuilds` (Endtasche: ohne
  `--trocken`) gelöscht — protokolliert wie jede Klasse. Erster Befund
  bei Einführung: 3 Altlasten (27./28.09.) mit 16,3 GB; alle 10 bugtank-
  Builds des 29.09. blieben geschützt. **Offen ist die Zustimmung des
  fremden Threads** — bis dahin ist der Lauf ein Vorschlag mit Beleg,
  kein Eingriff. Bei ~30 GB/Tag an Diagnose-Tagen holt ohne diese Klasse
  keine Reinigung auf.
- **Nur der Nutzer (außerhalb der Projektreviere):** `pagefile.sys`
  (96 GB, systemverwaltet — feste Größe wäre ein Einmaleins-Gewinn),
  `hiberfil.sys` (`powercfg /h off`, falls Ruhezustand ungenutzt),
  `C:\Games`, `Downloads`, `.lmstudio`.

## Gemessene Wirkungen

- 29.09.2026, 10,2 % frei (0 % Luft): manuelle cache-only-Reinigung
  holte 11 GB zurück und rettete den nächsten Push — dieser Lauf ist
  seitdem automatisiert (Schwelle 14 %, Live-Nachweis: „4 Cache-Ordner
  geraeumt").
- Ein Leerlauf von `%TEMP%` als Ganzes ist bewusst KEINE Kandidaten-
  klasse: dort liegen die Arbeitsdaten laufender Werkzeuge; nur
  einzelne Einträge entscheidet der Mensch.
