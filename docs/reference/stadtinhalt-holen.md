# Stadt-Inhalt in einem frischen Klon holen

Ein frischer Klon enthält **nicht** die gebackene Stadt: `Content/__ExternalActors__/`
und `Content/Generated/` sind per `.gitignore` ausgeschlossen, alle getrackten
`.umap`-Hüllen sind darum inhaltslos (je ~13 KB). Ein Start der Karte ohne den Inhalt
zeigt eine leere Welt — im Log steht dann `Stadt-Geometrie: KEIN Chunk-Actor geladen`,
0 Ampeln und ~1800 statt ~716.000 Instanzen.

Ein Kommando genügt:

```
Tools\fetch_city_content.cmd
```

Das Skript (`Tools/fetch_city_content.py`) lädt die zwei Release-Assets aus dem Release
`city-content-alkis16`, prüft je Archiv die sha256-Summe, entpackt sie in die Projektwurzel
und prüft jede einzelne Datei gegen die mitgelieferten `INHALT*.sha256`-Listen. Danach
startet `Wiesbaden_spielen.cmd` die Stadt.

Prüfen ohne zu laden (und ohne Netz): `Tools\fetch_city_content.cmd --check`.
Der Check ist absichtlich revisionsgenau: Liegt lokal noch ein anderer Bake (zum
Beispiel Alkis17) in `Content/Generated/Chunks`, meldet er diese Dateien als
abweichend. Das ist kein gueltiger Alkis16-Spielstand; in einem frischen Klon
zuerst ohne `--check` laden und entpacken.

## Was im Paket liegt

| Pfad | Inhalt | Dateien |
|---|---|---|
| `Content/__ExternalActors__/Maps/WiesbadenCity_Alkis16/**` | Externe Actor-Pakete der Karte: Straßenabschnitte, Gebäude, Ampeln, Schilder, Streuung | 2019 |
| `Content/Generated/Chunks/**` | Gebackene Kachel-Meshes (`SM_Road_*`, `SM_Building_*`, Kollision) | 4578 |
| `Content/Materials/AAA/**` | Bake-Materialien (Fassaden, Terrain, Asphalt) — die Nerobergbahn-Materialien `MI_Nb_brick/_plaster/_roof` referenzieren daraus `Textures/FacadeBrick_*` | 51 |

Ohne das dritte Paket meldet die Engine beim Start `Failed to find object 'Texture2D
/Game/Materials/AAA/Textures/FacadeBrick_*'` und die Nerobergbahn-Materialien verlieren ihre
Fassadentexturen — im Test gemessen (36 Zeilen, mit AAA dagegen 0).

Rohdaten (`Content/Data/Raw/`: ALKIS, LoD2, DGM1, OSM) sind **nicht** enthalten — sie
werden nur zum Backen gebraucht, nicht zum Spielen. Wer neu backen will, braucht die
Quellen lokal und fährt `rebake_alkis16.cmd` (siehe `AGENTS.md`).

## Warum kein Git LFS

Gemessen für diese eine Karte: **2,55 GB** in 6597 Dateien (plus 194 MB Bake-Materialien),
größte Einzeldatei **1,21 GB**.
Damit ist normales Git ausgeschlossen (Grenze 100 MB je Datei). Git LFS würde die Dateien
ungepackt und **gemessen** ablegen: im kostenlosen Kontingent stehen 1 GiB Speicher und
1 GiB Bandbreite pro Monat — ein einziger Klon braucht mehr als das Doppelte, und jeder
weitere Klon verbraucht erneut Bandbreite. Release-Assets haben unbegrenzte Bandbreite
und ein Limit von 2 GB je Datei; gepackt bleiben wir mit 0,95 GB klar darunter
(deflate Stufe 1, gemessener Faktor 3,7 — das Archiv entsteht in ~40 s).

Wer trotzdem LFS will (etwa mit Datenpaket), schaltet es in drei Zeilen um:

```
git lfs install
git lfs track "Content/__ExternalActors__/**" "Content/Generated/**"
git add .gitattributes Content/__ExternalActors__ Content/Generated && git commit
```

Danach ist der Inhalt Teil des Klons — bezahlt mit Speicher- und Bandbreitenkontingent.
Zu unterscheiden: die Bake-Materialien (194 MB, größte Datei 10,4 MB) wären auch als normale
Git-Objekte möglich, LFS bräuchte es dafür nicht. Die Actor-Pakete dagegen enthalten eine
1,21 GB große Datei und kämen **nur** über LFS ins Repo.

## Aktualisieren

Ein neuer Bake schreibt `Content/__ExternalActors__/Maps/WiesbadenCity_<neu>` und
`Content/Generated/Chunks` neu. Für eine weitere Stadt dasselbe Muster: Ordner packen,
Release-Asset hochladen, `Tools/fetch_city_content.py` auf das neue Asset umstellen (Liste `PAKETE`: `asset`,
`sha256`, `manifest`) — oder das vorhandene Paket mit dem neuen Bake neu erzeugen.
