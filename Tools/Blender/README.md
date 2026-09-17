# Blender-Werkzeuge (Asset-Pipeline)

Skripte für den Weg vom Rohmodell zum einsatzfähigen Unreal-Asset. Alle laufen
im Hintergrundmodus:

```bash
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --python <skript>.py -- <argumente>
```

## Reihenfolge

| Skript | Zweck |
|---|---|
| `dae_import.py` | COLLADA (`.dae`) → `.blend`. Blender 5.2 hat den COLLADA-Importer entfernt, UE5 liest `.dae` nicht — daher ein eigener Parser. |
| `find_wheels.py` | Sucht radförmige Teile über Loose Parts. Diagnose, verändert nichts. |
| `split_wheels.py` | Trennt Räder von der Karosserie, exportiert beide FBX + `wheel_positions.json`. |
| `export_beetle.py` | Exportiert das Gesamtmodell (Verkehrsvariante, reduziert). |
| `verify_assembly.py` | Kontrollmontage mit den hartcodierten Radpositionen aus `AWiesbadenCar` + Rendering. |
| `make_nerobergbahn.py` | Wagen (außen **und innen**, §10d der Nachbau-Referenz), Tal-/Bergstation, Viadukt **und die fünf Gleisbauteile** der Trasse (Schiene, Zahnstange, Seilkanal, Schwelle, Schotterbett) → FBX + `nerobergbahn.json`. |
| `make_nerobergbahn_textures.py` (in `Tools/`, Pillow statt Blender) | Erzeugt Schriftzug, Wasserstandsskala, Geschwindigkeitsanzeige und Riffelmuster des Wagens als transparente PNG → `Content/Nerobergbahn/Textures/Source/`. Vor `make_nerobergbahn.py` laufen, damit die Kontrollbilder die Schrift zeigen. |
| `check_nerobergbahn_gleis.py` | Prüft die Gleisbauteile ohne Renderlauf: Maße, Höhenkette (Schienenfuß = Schwellenkrone = Seilkanalkrone, Bettkrone = Schwellenunterseite), Windung, Sprossen-/Rostteilung, Radspur gegen die Schienenlage. 0 Fehler = alles sitzt aufeinander. |
| `check_nerobergbahn_wagen.py` | Prüft den Wagen **und die Bahnsteighalle** ohne Renderlauf. Wagen: Maße, Neigung der Kontaktlinie (19,1 % gegen Soll 19,5 %), Windung, **und den Innenraum** (Sitzhöhe, Lehnenoberkante, Bankzahl, freier Mittelgang, Kopfreiheit über der Augenhöhe der Mitfahrkamera, Tacho/Schauglas/Kurbel innerhalb des Kastens). Halle: Trogsohle, Bahnsteighöhe (0,80 m), Stützenzahl/-kopf unter der Binderunterkante, Dachüberdeckung des Bahnsteigs, Balustradenhöhe, freies Einstiegsjoch, Windung von `balken()`/`docke()`. Meldet am Ende „Innenraum: N Fehler" und „Bahnsteighalle: N Fehler" — 0 = der C++-Code darf sich auf die Maße verlassen. |

## Fallstricke

Beide sind uns hier tatsächlich passiert und beide **melden keinen Fehler**:

- **`bpy.ops.object.origin_set` wird im `--background`-Modus als CANCELLED
  verworfen** (fehlender UI-Kontext). Der Pivot bleibt liegen, wo er war.
  Vertices stattdessen direkt verschieben.

- **`ob.bound_box` und `ob.matrix_world` sind gecacht.** Nach direkter
  Vertex-Manipulation bzw. nach dem Setzen von `.location` melden sie die alten
  Werte. Vor dem Messen `bpy.context.view_layer.update()` aufrufen oder über die
  Vertices iterieren. Das Rendering wertet den Depsgraph selbst aus — Bild und
  Messung können dadurch widersprüchlich sein, und das Bild hat recht.

- **Blenders FBX-Export schreibt Zentimeter**, UE liest FBX als Zentimeter. Ein
  `import_uniform_scale = 100` beim UE-Import (in der Annahme, Blender liefere
  Meter) ergibt ein 414 m langes Auto. Nach jedem Import die Maße ausgeben.

- **`MeshBuilder.box()` legt alle sechs Flächen mit der Normalen nach INNEN an.**
  Gemessen mit `check_nerobergbahn_wagen.py`: ein 2×2×2-Würfel ergibt als
  vorzeichenbehaftetes Volumen −8,000 statt +8,000. Der FBX-Export dreht das
  **nicht** um (Roundtrip-Test mit denselben Exporteinstellungen), und der
  Blender-Render zeigt es nicht, weil EEVEE nicht cullt — Unreal-Materialien
  sind aber einseitig. Wagen, Stationen und Viadukt folgen dieser Konvention,
  deshalb halten neue Bauteile an ihnen (z. B. `prism_y` für Radsätze) sie
  ebenfalls ein; sonst fällt genau ein Bauteil optisch aus der Reihe.

  **Die fünf Gleisbauteile (Abschnitt 4) tun das bewusst NICHT:** bei
  tausenden Einzelflächen entlang der Strecke ist „nach außen" die sichere
  Seite. Ihre Builder rufen am Ende `MeshBuilder.orient_outward()` auf — es
  misst das vorzeichenbehaftete Volumen und dreht bei negativem Ergebnis alle
  Flächen um (UVs mit, damit die planare Zuordnung bleibt). `prism_y`/`prism_x`
  prüfen ihre Windung selbst und sind davon nicht betroffen; die
  Schotterbett-Quads geben ihre Richtung über `quad_towards()` vor, damit keine
  Mischkonvention im selben Mesh entsteht. Sichtbar wird das nur im Spiel
  (`shot_nerobergbahn.cmd`, Trasse von oben), nicht im Blender-Render.

- **Der Nerobergbahn-Wagen steht parallel zur Trasse.** Die Neigung (19,5 %)
  steckt als Vorab-Drehung im Mesh (`GRADE`, `MeshBuilder.tilt_grade`), weil
  `AWiesbadenNerobergbahn::PlaceCar` nur giert. Wer den Actor zusaetzlich nicken
  laesst, legt den Wagen doppelt schief. Der Ursprung bleibt auf der
  Schienenkontaktlinie in Wagenmitte — deshalb wird der Wagen beim Export
  **nicht** XY-zentriert (`center_origin_xy(..., keep_xy=True)`).

- **Höhen am gekippten Wagen nie im Mesh-System messen.** `build_wagen()`
  dreht das ganze Mesh mit `tilt_grade()` in die Streckenneigung. Wer dort eine
  Höhe abliest, bekommt sie um `x · 19 %` verschoben — bei x = 1,70 m sind das
  32 cm, genug, um „Tacho auf Augenhöhe" und „Tacho unter Augenhöhe" zu
  vertauschen. `check_nerobergbahn_wagen.py` dreht Punkte und Normalen zuerst
  zurück und vergleicht erst dann mit den Werten, die der C++-Code setzt
  (Wagenboden 0,85 m, Augenhöhe 2,45 m).

- **Die Bahnsteighalle ist EIN Asset für beide Stationen** und trägt ihre
  Platzierungskonvention im Mesh: Länge entlang X, Ursprung = **Schienen-
  oberkante in der Mitte ZWISCHEN den beiden Gleisen** (nicht auf einer der
  Linien — sonst steht die Halle um eine halbe Spurweite daneben), und das
  **erste Joch (x0 … x0+4 m) hat keine Balustrade** — das ist die offene
  Einstiegsseite. An der Bergstation muss der Actor die Halle deshalb um 180°
  drehen, damit die Öffnung dort ebenfalls am stehenden Wagen liegt. Was am
  Bauwerk **nicht** hängt: Stützmauer, Geländer und Wimpel der Trasse
  (`BuildTrackMeshes`) — die liefen sonst mitten durch den Bahnsteig, siehe
  NACHBAU-REFERENZ §10e.

- **Kachel-Texturen aus dem Städte-Atlas wirken vor der Nase wie ein Muster.**
  `plaster`/`stone`/`roof` (TILED in `import_nerobergbahn.py`) ziehen
  `FacadePlaster_*` / `FacadeStone_*` / `RoofClay_*` — das sind Fassaden- und
  Dachatlanten der Stadt. Auf einer 12 m langen Bahnsteigwand mit Kachelung 1,0
  sieht man bei 1 m Abstand **eine Atlaszelle** als große Rechteckfelder statt
  Beton. Für Nahaufnahmen an Bahnsteigkante und Trog ist ein `paint`-Slot
  (Vollton mit Rauheit) die ehrlichere Wahl.

- **Maße aus dem Mesh ableiten, nicht aus Kommentaren abschreiben.** Ein
  Prüfband stand auf „Kastenende 1,95 m", weil ein Kommentar daneben das sagte —
  der Aufbau rechnet `5,40/2 − 0,85 = 1,85 m`. Solche Zahlen sind nach dem
  nächsten Umbau still falsch. Das Skript holt den Bezug jetzt aus der
  Innenboden-Geometrie und gibt ihn aus (`Kastenende (aus dem Innenboden
  abgeleitet): 1,850 m`).

- **UV-Reihenfolge entscheidet, ob Schrift aufrecht steht.** `quad_tex()`
  bekommt die Ecken so, wie man sie von außen liest (unten links, unten rechts,
  oben rechts, oben links); die UV-Werte müssen dazu
  `(0,0) (1,0) (1,1) (0,1)` lauten. Mit der zuerst verwendeten Reihenfolge
  `(0,1) … (0,0)` stand der Schriftzug **auf dem Kopf** (V-Achse und U-Achse
  vertauscht = 180°-Drehung). Blender zeigt den Fehler sofort, Unreal erst nach
  dem Import. Deshalb rendert der Wagen zwei **Nahansichten der Außenwand**
  (`vorschau_wagen_wand*.png` in 1920 px) und eine Gegenseitenansicht —
  Schriftzug (0,17 m Versalhöhe) und Skala (0,16 m breit) sind in der
  Gesamtansicht nur wenige Pixel breit und dort nicht prüfbar.

- **Deckkraft braucht das Bild, nicht nur die Farbe.** Die Slots `NbSchrift` /
  `NbSkala` sind aufgemalte Graphik: der PNG-Grund ist durchsichtig, erst
  Alpha→Opacity-Mask lässt den gelben Kasten zwischen den Buchstaben stehen.
  Für den Kontrollrender hängt `bind_decal_texture()` dieselbe Datei an
  Base Color UND Alpha (DITHERED); fehlt die Datei, läuft der Bau durch und
  meldet nur einen Hinweis — der Slot bleibt dann die blaue Grundfarbe.

## Quelle

`vw-beetle-1969` — Assimp-exportiertes COLLADA, Einheit Zoll (`meter="0.0254"`),
Hochachse Y_UP, Fahrtrichtung −Y. Alle drei Abweichungen korrigiert
`dae_import.py` bzw. `export_beetle.py`.
