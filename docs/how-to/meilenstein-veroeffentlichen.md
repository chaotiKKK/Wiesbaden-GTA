# How-to: Ein fertiges Thema als Meilenstein veroeffentlichen

Ein Befehl macht aus einem fertigen, gepushten Thema den ganzen Meilenstein:
Clip aus dem Spiel, GIF und Foto, Eintrag auf `docs/meilensteine.md` samt
README-Block, Doku-PR nach `main` (gemergt), GitHub-Release und das oeffentliche
Schaufenster.

```
python Tools/meilenstein.py thema.json --trocken   # erst ansehen: nur lokal
python Tools/meilenstein.py thema.json             # dann veroeffentlichen
```

Aufgenommen wird mit dem Build DIESES Ordners oder dem von `--spiel <Ordner>`.
Er braucht den Clip-Modus `-WbClip` (siehe [clip-aufnehmen.md](clip-aufnehmen.md))
UND den Themen-Commit - sonst bricht die Vorpruefung ab, denn der Clip zeigte
einen Stand ohne das Thema (am 27.09.2026 so passiert: der zweite Hubschrauber
fehlte im Bild, alles andere lief durch). Vorher bauen: `Tools\build_gate1.cmd`.
Das Spielfenster darf verdeckt sein, aber nicht minimiert.

## 1. Themen-Datei schreiben

```json
{
  "stamm": "zweiter-hubschrauber",
  "titel": "Zweiter Hubschrauber mit Cockpit",
  "zeitraum": "27.09.2026",
  "stand": "fertig",
  "commit": "dac241e",
  "text": "Was man sieht, in zwei, drei Saetzen.\n\nZweiter Absatz.",
  "clip": {
    "goto": "-117159,-118324",
    "pose": "3, -117159, -118324, 206, 0, 13, 26, -6",
    "at": 30, "sekunden": 6, "fps": 25,
    "gif": {"von": 0.5, "bis": 5.5, "fps": 10},
    "gif_alt": "Der zweite Hubschrauber am Garagenhof",
    "foto_alt": "Der zweite Hubschrauber von der Seite"
  }
}
```

| Schluessel | Bedeutung | Vorgabe |
| --- | --- | --- |
| `stamm` | Tag- und Dateiname (`meilenstein-15-<stamm>`, `15-<stamm>.gif`), nur a-z 0-9 - | Pflicht |
| `titel`, `zeitraum`, `stand`, `text` | Ueberschrift, Tabellenspalten, Markdown des Abschnitts | Pflicht |
| `commit` | Ziel-Commit des Releases, muss auf GitHub liegen | Pflicht |
| `nr` | Meilensteinnummer | hoechste auf main + 1 |
| `clip.goto` / `clip.pose` | Zielort (`-WbGoto`) / Kamera (Posenzeile wie `-WbShotPoseFile`) | Spielkamera |
| `clip.at`, `delay`, `sekunden`, `fps`, `tempo` | wie `-WbClipAt/-Delay/-Sekunden/-Fps/-Tempo` | 0, 5, 8, 25, 1 |
| `clip.uhrzeit` | `-WbTime` | 13 |
| `clip.ohne_hud`, `clip.schalter` | HUD aus; weitere Spielschalter (z. B. `-WbZuFuss=20`) | an, - |
| `clip.gif` | `von`, `bis` (s), `breite`, `fps`, `farben`, `crop` | 0, Ende, 480, 12, 96, - |
| `clip.foto_bei` | Sekunde des Clips fuer das Foto (1280 px) | Mitte |

Unbekannte Schluessel brechen ab - ein Tippfehler ergibt nie still eine Vorgabe.

## 2. Was der Befehl tut

1. **Vorpruefung** - Aufnahme-Build hat Clip-Modus und Themen-Commit, `gh` angemeldet, Themen-Commit auf GitHub, Schaufenster-Klon
   sauber und mit noreply-Adresse. Erst danach startet das Spiel.
2. **Aufnahme** unter dem Engine-Lock nach `Saved/Clips/meilenstein-<nr>-<stamm>/`.
   Liegt der Clip schon vollstaendig vor, wird nicht neu aufgenommen
   (`--neu-aufnehmen` erzwingt es).
3. **GIF und Foto** nach `Saved/Meilensteine/<nr>-<stamm>/`. Ein GIF ueber 3 MB
   bricht ab: kuerzer schneiden oder `gif.fps`/`farben`/`breite` senken.
4. **Seite und README** auf dem Stand von `origin/main`: Zeile oben in die
   Tabelle, Abschnitt vor den bisher neuesten, im README Anzahl und die drei
   neuesten GIFs. Vorschau: `Saved/Meilensteine/<nr>-<stamm>/meilensteine.md`.
5. **Doku-PR**: Commit direkt auf `origin/main` (ohne Checkout - fremde Arbeit
   im Arbeitsbaum bleibt unberuehrt), Push aus dem Hauptordner durch das
   Push-Gate, PR `docs/meilenstein-<nr>-<stamm>`, Squash-Merge, Zweig loeschen.
6. **Release** `meilenstein-<nr>-<stamm>` auf den Themen-Commit, "Latest",
   Bilder ueber `blob/main` eingebettet (das Repo ist privat) und angehaengt.
7. **Schaufenster**: `schaufenster.py` von `origin/main` erzeugt den
   oeffentlichen Klon neu, Commit + Push (`--ohne-schaufenster` laesst es weg).

`--trocken` endet nach Schritt 4 mit einem Commit-OBJEKT (`git show <sha>`),
ohne Push, PR, Release oder Schaufenster.

## 3. Nach einem Abbruch

Denselben Befehl noch einmal aufrufen. Er nimmt den vorhandenen Clip, erkennt
einen schon gemergten Seiteneintrag (gleiche Nummer, gleicher Titel) und ein
schon angelegtes Release und macht beim ersten offenen Schritt weiter. Ist das
Push-Gate rot, ist noch nichts gemergt oder veroeffentlicht.
