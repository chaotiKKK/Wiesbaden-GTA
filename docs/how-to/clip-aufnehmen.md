# How-to: Einen Clip direkt aus dem Spiel aufnehmen (-WbClip)

Fuer Meilenstein-GIFs und kurze Videos. Das Spiel schreibt jedes Bild selbst
aus seinem Renderer - **kein Bildschirmfilm, kein freies Fenster noetig**. Du
kannst waehrend der Aufnahme weiterarbeiten; nur **minimieren** darfst du das
Spielfenster nicht (ein minimiertes Fenster rendert nicht).

## 1. Aufnehmen

Startschalter an das Spiel haengen (z. B. per `WBARGS` wie bei `messlauf.cmd`):

```
-WbKeinIntro -WbGoto=-91581,-153781 -WbClip=nerobergbahn
-WbClipPoseFile=C:\pfad\pose.txt -WbClipAt=85 -WbClipSekunden=20 -WbClipFps=25
```

| Schalter | Bedeutung | Vorgabe |
| --- | --- | --- |
| `-WbClip=<Name>` | schaltet den Modus ein; Ordner `Saved/Clips/<Name>/` | - |
| `-WbClipSekunden=<s>` | Laenge in Spielzeit | 8 |
| `-WbClipFps=<n>` | Bilder je Sekunde (1..60) | 30 |
| `-WbClipTempo=<x>` | Spielzeit je Clip-Sekunde; 4 = Zeitraffer (bis 8) | 1 |
| `-WbClipDelay=<s>` | Vorlauf nach "Stadt bereit" | 5 |
| `-WbClipAt=<s>` | fruehestens bei dieser Weltzeit starten | 0 |
| `-WbClipPoseFile=<Pfad>` | erste Posenzeile setzt die Kamera (Format wie `-WbShotPoseFile`: `Hoehe_m, AtX_cm, AtY_cm, Yaw, Pitch, Vorwaerts_m, LookYaw, LookPitch`); ohne: die Spielkamera | - |
| `-WbClipPng` | PNG statt JPG | JPG (Qualitaet 92) |
| `-WbClipOhneHud` | HUD waehrend der Aufnahme aus | an |
| `-WbClipNoQuit` | danach weiterspielen | Spiel beendet sich |

Die Pose wird schon bei "Stadt bereit" gesetzt - der Vorlauf ist zugleich die
Zeit, in der Streaming und Belichtung am Zielort einschwingen.

Im laufenden Spiel geht es auch per Konsole: `WbClip [Sekunden] [Fps] [Name]`
(aktuelle Kamera, Spiel laeuft danach weiter).

## 2. Pruefen

Im Log (`-abslog`) steht am Ende:

```
WbClip: fertig - 500 von 500 Bildern (1280x720), 0.0400 s Spielzeit je Bild (Soll 0.0400), 19.9 s Echtzeit, nach ...\Saved\Clips\nerobergbahn
```

Daneben liegt `clip.json` mit `"vollstaendig": true` und
`spielzeit_je_bild_gemessen` = `spielzeit_je_bild_soll`. Steht dort `false`
oder im Log `UNVOLLSTAENDIG` / `seit 30 s kein Bild mehr`, war das Fenster
minimiert oder der Lauf wurde beendet.

Gemessen am 27.09.2026 (1280x720):

| Lauf | Bilder | Spielzeit je Bild (Soll/Ist) | Echtzeit | Platz |
| --- | --- | --- | --- | --- |
| Nerobergbahn, Pose, JPG, 25 fps, 20 s | 500/500 | 0,0400 / 0,0400 | 19,9 s | 231 MB |
| Sebbo, Spielkamera, PNG, 30 fps, 12 s | 360/360 | 0,0333 / 0,0333 | 13,1 s | 468 MB |
| Nerobergbahn, Fenster die GANZE Zeit verdeckt, 30 fps, 6 s | 180/180 | 0,0333 / 0,0333 | 7,1 s | - |

Beim verdeckten Lauf lag ein rotes Fenster (400x300, immer oben) mitten ueber
dem Spiel: in den Bildern zeigt die Mitte Baeume, 0 % rote Pixel.

## 3. GIF oder MP4 bauen

```
python Tools/medien.py clip Saved/Clips/nerobergbahn x.gif --von 2 --bis 10 --crop 960:400:0:320 --breite 480 --fps 12
python Tools/medien.py clip Saved/Clips/nerobergbahn x.mp4 --breite 1280
```

`--von/--bis` sind Sekunden auf der Zeitachse des Clips, `--crop` schneidet
aus dem vollen Bild (z. B. die Minikarte rechts weg). Fuer die
Meilenstein-Seite GIFs um 1 MB halten.

## Warum der Clip fluessig ist, auch wenn das Spiel stockt

Waehrend der Aufnahme laeuft die Spielzeit mit **festem Zeitschritt**
(`FApp::SetUseFixedTimeStep`, 1/fps bzw. Tempo/fps). Jedes gerenderte Bild ist
genau ein Schritt Spielzeit - dass das Auslesen jedes Bildes das Spiel bremst,
sieht man dem Clip nicht an. Die Echtzeit der Aufnahme ist deshalb laenger als
der Clip (siehe `echtzeit_s` in `clip.json`).

## Fallen

- Der Screenshot-Delegate der Engine ist **global**: waehrend einer Aufnahme
  schreibt `HighResShot`/`shot` keine Datei. Nicht mit `-WbShotWhenReady`
  in denselben Zeitraum legen.
- Ein neuer Lauf mit demselben Namen **loescht** den alten Ordner zuerst.
- Die Klang-Ausgabe laeuft nicht mit (nur Bilder).
