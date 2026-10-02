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

- **20 %** — der Wächter meldet (Hinweis im Gate-Log, kein Gate). Seit
  30.09.2026 räumt bei dieser Zahl auch jeder **Engine-Start** selbst: liegt
  `engine_run_lock.ps1` unter 20 % frei, räumt er VOR dem Lock die
  Cache-Klasse (`platten_waechter.py --reinigen`, nur `cache`) und misst
  danach neu — erst dann wartet er oder bricht ab. Die dritte Antwort neben
  Warten und Abbrechen, und die einzige, die die Lage ändert. Einmal je
  Lauf, fail-open (fehlt der Wächter: eine Zeile im Log), abschaltbar mit
  `-PlattenReinigungsGrenze 0`; `Status` und `Freigeben` räumen nie.
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
- **Der Engine-Start (automatisch):** unter 20 % nur `cache`, VOR dem
  Lock, einmal je Lauf (`engine_run_lock.ps1`). Dieselbe Klasse wie
  Stufe 1, nur früher — dort ist Warten noch billig und ein Abbruch
  noch fern. Der Lock ist für die Caches keine Schranke (sie sind nichts,
  woran ein laufender Editor hängt); für die `ausgabe`-Klasse bleibt er
  eine.
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
  keine Reinigung auf. Der Befund zur möglichen automatisierten
  **Stufe 3** (dasselbe Warnmuster) steht unten.
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

## Befund: Stufe 3 (dev-builds) hinter demselben Warnmuster

Geprüft am 30.09.2026, zunächst bewusst NICHT implementiert; nach
Eingang der Zustimmung am 02.10.2026 umgesetzt (s. u.). Der Befund:

- **Sinn:** Die Hebel-Reihenfolge misst die Platte selbst (30.09.2026,
  14,5 % frei, 0,5 Punkte über der Schwelle): Stufe 1 (cache) holt
  ~2 GB, Stufe 2 (ausgabe) ~17 GB, die dev-builds-Altlasten lagen bei
  **35,35 GB in 9 Bäumen** (7 bugtank-Builds des 29.09. à ~3,2 GB,
  `2026-09-28` 9,98 GB, `-video` 6,20 GB). Stufe 3 ist damit der
  größte Hebel an Diagnose-Tagen — und der einzige ohne Bauzeit-Preis.
- **Reihenfolge 1 → 2 → 3:** aufsteigend nach Irreversibilität. Cache
  rechnet sich neu, Ausgabe kostet Zeit, dev-builds sind Belege, die
  kein zweites Mal entstehen. Darum nur als LETZTE Stufe: erst wenn
  der Cache die 14 % nicht gerettet hat und die Ausgabe-Klasse
  geräumt wäre. Existiert Stufe 3, verliert der Stufe-2-Endpunkt sein
  „dev-builds?“ — dann bleibt nur noch „Nutzerdaten“ unerreichbar.
- **Was vom Warnmuster trägt:** Warnung VOR dem Eingriff im Gate-Log,
  Engine-Lock-Stop, fail-open, Löschprotokoll, nur unter 14 %, nur
  nachdem die Vorgänger-Stufen nicht reichten, Keep-N (letzte 2) und
  12-h-Burst-Schutz unverändert.
- **Was anders ist — der Warninhalt:** Stufe 2 hält eine
  ZEITKOSTEN-Erwartung fest (VOLLBUILD). Stufe 3 hält die ABMACHUNG
  fest: „die Altlasten des fremden Threads (älter als 12 h, außerhalb
  der letzten 2) werden geräumt; die letzten 2 und alles Jüngere
  bleiben (Zustimmung vom TT.MM.)“.
- **Aktivierung per Zustimmungs-Artefakt, nicht per Code:** Die
  Zustimmung ist ein Ding, das der FREMDE Thread selbst schreibt —
  vorgesehen: `Saved/Diagnose/devbuilds_stufe3_zustimmung.txt` mit
  Datum und Scope (Keep-N 2, 12 h). Der Auto-Lauf prüft die Datei;
  fehlt sie, gibt es Stufe 3 nicht (Meldung „uebersprungen — keine
  Zustimmung“, nie still). Datei gelöscht = widerrufen. Bis zum
  Zustimmungsdatum gehörte kein Stufe-3-Code in den Arbeitsstand.
- **Umgesetzt am 02.10.2026** — `platten_auto_reinigen` fährt jetzt drei
  Stufen: Stufe 3 startet hinter cache/ausgabe, nur wenn beide nicht
  reichten und die Platte weiter unter 14 % liegt — hinter der
  ABMACHUNG-Zeile im Gate-Log (Keep-N 2, älter als 12 h, „Zustimmung
  vom 30.09.2026 11:29“), nie unter Engine-Lock, fail-open,
  Löschprotokoll wie jede Klasse. Die Aktivierung liest das Artefakt
  (`stufe3_zustimmung()` in `Tools/platten_waechter.py`); fehlt es,
  meldet der Lauf „uebersprungen — keine Zustimmung“ mit Dateinamen.
  Der Stufe-2-Endpunkt nennt nur noch „Nutzerdaten“ — das
  „dev-builds?“ hat Stufe 3 übernommen. Manuell bleibt
  `--auch-devbuilds` unverändert; Widerruf bleibt: Datei löschen.
