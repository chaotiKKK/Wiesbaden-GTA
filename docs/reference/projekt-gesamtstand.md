# WiesbadenReal — Gesamtstand, offene Punkte und Vorschläge

- **Stand:** 2026-09-23, Quellstand `e9dcd96` auf `main` (Squash von PR #12)
- **Zweck:** Ein Dokument statt vieler Gesprächsfäden. Pro Thema: was läuft,
  was fehlt, was es blockiert.
- **Quellen:** Agenten-Gedächtnis (44 Dateien unter
  `~/.claude/projects/C--freebuff-WiesbadenReal-Sicherung/memory/`), `AGENTS.md`,
  die Spezifikationen und Pläne unter `docs/superpowers/`, die Git-Historie
  (342 Commits auf `main`) und Messungen am laufenden Spiel.
- **Abgrenzung:** `preview.html` (erzeugt von `Tools/uebersicht.py`) zählt
  Dateien, Zeilen und Tests. Dieses Dokument beantwortet die andere Frage:
  *was ist fertig, was nicht, und warum nicht.*

> **Lesehinweis zu Zahlen.** Jede Zahl unten stammt aus einer Messung, nicht aus
> einer Erinnerung. Wo eine ältere Notiz überholt ist, steht das ausdrücklich
> dabei — im Gedächtnis liegen mehrere Einträge, die der Code inzwischen
> widerlegt.

---

## 1. Auf einen Blick

| Größe | Wert | Gemessen wo |
|---|---|---|
| Quellcode | 181 Dateien / 74.681 Zeilen (Core, GIS, World, Vehicles, Weapons) | `Tools/uebersicht.py --zeigen` |
| Tests | 258 Automation (C++), 194 Python in 14 Suiten | ebenda |
| Gespielte Karte | `WiesbadenCity_Alkis16` | `Config/DefaultEngine.ini` |
| Straßennetz | 125.024 Segmente, 22.227 Kreuzungen, 117.351 Spuren | Bake-Protokoll |
| Gebäude | 104.458 (amtliche LoD2-Höhen) | Bake-Protokoll |
| Ausstattung | 52.689 Schilder, 187.191 Leitpfosten, 112.329 Markierungen, 72.434 Laternen | Laufzeit-Protokoll |
| Ampeln | 1.073 im Netz, 552 mit eigener Abbiegephase | Laufzeit-Protokoll |
| Region-Objekte | 1.432.801 (davon 1.207.828 Bäume) | Bake-Protokoll Alkis17 |
| Bildrate | 105–136 B/s, Ladezeit 19–21 s, RAM-Spitze 6,3 GB | Spiellauf |
| CI | **keine** — der Pre-Push-Hook ist der Nachweis | `gh pr checks` leer |

---

## 2. Stand je Thema

### 2.1 Welt, Daten und Bake

**Läuft.** Die Stadt entsteht aus echten offenen Daten: OSM (Straßen, Gebäude,
POIs), amtliches ALKIS mit LoD2-Höhen aus dem Hessen-INSPIRE-WFS, DGM1-Gelände
(1 m). Der Bake dauert rund 15 Minuten und schreibt eine World-Partition-Karte
mit ~2.010 Chunks und 1,84 GB externen Actors.

Die Wald-Lücke ist geschlossen: `wiesbaden.osm.json` enthielt **null**
Wald-Relationen (Overpass holte nur Ways), der Neroberg war kahl.
`Tools/fetch_osm_forest_relations.py` zieht 162 Relationen nach und schreibt
`wiesbaden.osm.forest.json` — daraus backen alle aktuellen Karten.

Es gibt nur noch **zwei** Karten; zwölf Altversionen wurden gelöscht (44,8 GB
frei). `Alkis16` ist live, `Alkis17` ist gleichwertig gemessen, aber nicht live
(siehe 3.1).

**Wichtig für jeden frischen Klon:** Der gebackene Inhalt ist gitignoriert. Ohne
`Tools\fetch_city_content.cmd` (Release `city-content-alkis16`) zeigt das Spiel
eine leere Welt.

### 2.2 Straßen, Spuren und Markierungen

**Läuft.** Fahrbahnbänder folgen dem Gelände, Bordsteine und Gehwege stehen,
Tempo-30-Zonen tragen die aufgemalte „30", Verkehrszeichen stehen an Randfällen
geprüft.

**Teilweise.** Die Initiative *Echte Fahrspuren & Beschriftungen*
(`docs/superpowers/specs/2026-09-17-authentic-road-markings-design.md`) ist in
sieben Phasen geschnitten. **P0 (Gate) bestanden, P1 committet (`39813e3`),
P2 committet (`67101eb`).** P3–P7 sind geschrieben, aber nicht gebaut —
siehe 3.2.

### 2.3 Straßenmöbel

**Fertig, aber unsichtbar.** Hier ist eine ältere Gedächtnisnotiz überholt: die
Meshes existieren. `Tools/Blender/make_street_furniture.py` baut zehn Meshes
(Bänke mit Gusseisen-Wangen, Briefkasten RAL 1021, Hydrant RAL 3000,
Abfallkörbe, Picknick-Tische, Poller), importiert nach
`Content/Assets/Furniture/`, und `RoadFurnitureSpawnerComponent` macht daraus
HISM-Instanzen (Commits `528110a`, `8ae00e9`). 3.607 OSM-Knoten sind
nachgezogen.

Was fehlt, ist der **Bake, der sie in die gespielte Karte bringt** — siehe 3.3.

### 2.4 Verkehr

**Läuft, und zwar gemessen.** Drei Defekte sind mit Zahlen belegt behoben:

- **Dichte je Spurkilometer** statt fester Stückzahl (`VehiclesPerLaneKm = 12`).
  Die Bildrate ist nicht die Grenze: 368 Fahrzeuge kosten 120 B/s gegenüber
  132 B/s bei 55.
- **Spurwechsel**: 53 → 308 je Messfenster. Der große Hebel war nicht die
  Lückengröße, sondern der zu späte Auslöser
  (`LaneChangeLookAheadSeconds = 4`).
- **Kreuzungskonflikte**: Fahrzeuge standen sichtbar ineinander. Mit
  `FindConnectionConflict` und Vorfahrt nach Straßenklasse fielen am
  Bahnhofsplatz 42,2 durchdringende Paare auf **0,5**.

Dazu eine Stau-Karte als Luftbild (`-WbStauKarte` und
`Tools/render_stau_karte.py`), gefärbt nach Tempo **geteilt durch Limit** —
nach absolutem Tempo wäre jede Tempo-30-Zone rot.

### 2.5 Ampeln

**Läuft.** Echte Signalprogramme mit Rot/Rot-Gelb/Grün/Gelb, eigenen
Richtungsgruppen für Links, Abbiegephasen an 552 von 1.073 Kreuzungen, grüner
Welle und Umlaufzeiten nach Kreuzungsgröße.

Der entscheidende Fund: Jahrelang fuhr **jedes Auto durch jedes Rot** — eine
Index-Verwechslung zwischen dem Laufindex über alle Kreuzungen und dem
*gefilterten* Ampel-Array. Nur 1.073 von ~20.213 Kreuzungen sind Ampeln, der
Index lag fast immer daneben und fiel auf Grün zurück. Daraus wurde eine
Testregel: bei jeder Zuordnung in ein gefiltertes Array muss das Testnetz sein
Element **abseits von Index 0** haben.

### 2.6 ÖPNV — Buslinie 6

**Fertig.** Fünf Busse fahren die echte ESWE-Linie 6 (10,2 km, 19 Wiesbadener
Halte) nach dem **echten Fahrplan** (103 Mo–Fr-Abfahrten, aus dem bildbasierten
ESWE-PDF per `pypdfium2` abgelesen). Mit Rechtsverkehr-Seitenversatz,
Haltebuchten, Ampelbeachtung, Mitfahren, Haltestellen-Monitor, Bus-Modell mit
Zielanzeige — und **gesprochenen Haltestellenansagen** (SAPI-Stimme Hedda,
19 OSM-Namen, eigener Voice-Bus mit Ducking).

### 2.7 Fußgänger

**Läuft.** 200 Personen im Umkreis (53,5 je km Gehweg statt vorher 18,7),
Ausdünnung nach außen bleibt. Kosten: 125 → 123 B/s, also praktisch nichts.

### 2.8 Spieler, Fahrzeuge, Waffen

**Läuft.** Käfer 1302 mit echter Drehmomentkurve (eine Spec, zwei Adapter:
kinematisches Modell und Chaos), Ka-52-Helikopter mit koaxialen Rotoren und
prozeduralem Turbinen-Sound, Fuß-Pawn mit Waffen, Innen- und Außenkameras für
alle steuerbaren Fahrzeuge (Taste C).

### 2.9 Spiel-Rahmen: Missionen, Ökonomie, Läden

**Läuft.** Missions-Rückgrat (Dispatcher, Runner, Loader, Deadline),
persistentes Guthaben mit HUD-Anzeige, Läden mit Händler-NPCs, die
Sylvia-Szene, Verfolger-NPCs (`WiesbadenPursuer`: Idle → Chasing → Caught).

### 2.10 Licht, Wetter, Ton

**Läuft.** Echte astronomische Sonne nach Systemzeit (NOAA, Ursprung
50,0824/8,24), `-WbTime=<h>` erzwingt eine Stunde. Nachts leuchten die Fenster
— das Fensterraster liegt im **Material**, nicht in der Geometrie. 48 wandernde
Punktlichter an den Laternen. Regen und Schnee fallen sichtbar als
**Post-Process-Overlay**, weil der Niagara-Weg blockiert ist (siehe 3.6).
Zentrales Audio-Mischpult mit sieben Bussen, dB-genauen Reglern im Pausemenü und
Ducking für Sprachansagen.

### 2.11 Wahrzeichen und Sonderbauten

**Läuft.** Marktkirche und Russisch-Orthodoxe Kirche als Laufzeit-Geometrie aus
Primitiven (kein Bake nötig), Kurpark-Anlagen, Space-Shuttle-Monument am
Jagdschloss Platte, die **Nerobergbahn** mit Viadukt, Wagen, Bahnsteighallen,
Mitfahr-Modus und aufgemalter Graphik.

**Neu in PR #12: der SebboTower** an der Galileistraße — 60 m, 15 Geschosse,
begehbares Treppenhaus, Aufzugsschacht, Dachlandeplatz, und drei gemessen freie
Ankunftswege (Auto, Fuß, Helikopter). Dabei fielen zwei Befunde an, die nur
durch Messen zu finden waren: der Turm stand mit **497 cm auf der Wolkenbruch**,
und die Straßenböschung lief **6,5 m ins Erdgeschoss** (im Portal wuchs Gras).
Beides behoben; ein Bauplateau (`FTerrainSitePad`) nennt jetzt seinen Grundriss,
und darin hebt die Straßen-Einebnung das Gelände nicht mehr an.

### 2.12 Werkzeuge und Prozess

**Läuft.** Release-Gates laufen vor jedem Commit (`Tools/git-hooks/`,
`core.hooksPath`): `pre-commit` prüft Engine-Pfade und kompiliert nur bei
vorgemerktem C++ (3–8 s), `pre-push` fährt zusätzlich alle Python-Suiten,
Unit-Tests und den Rauchtest (426 s) – seit 25.09.2026 in einem eigenen, sauberen
Worktree auf genau den zu pushenden Commits (`Tools/gate_worktree.py`,
`<Sicherung>\.gate-worktree\WiesbadenReal`; gebackene Stadt und Stadtkarten
verlinkt). Fremde laufende Arbeit im Arbeitsbaum blockiert den Push nicht mehr,
und Gate 1 wie Rauchtest beenden nur Editoren des eigenen Projektordners. Der Kartenname steht nur noch an **einer**
Stelle (`GameDefaultMap`), gelesen über `Tools/karte.cmd|py|ps1`, bewacht von
`Tools/pruefe_kartenname.py`.

---

## 3. Offen und nie umgesetzt

Sortiert nach Wirkung zu Aufwand, nicht nach Alter. Jeder Punkt nennt seinen
Blocker.

### 3.1 Alkis17 live schalten — *der billigste große Hebel*

**Stand:** Alkis17 ist gebacken, im Spiel abgenommen (31 Chunk-Actors, 0 ohne
Render-Geometrie, 1.073 Ampeln, 8,6 ms Bildzeit) und seit PR #12 auf `main`.
Der Default steht trotzdem auf Alkis16.

**Blocker:** Zu Alkis16 existiert als einziger Karte ein geprüftes Release-Paket
(`city-content-alkis16`). Ein frischer Klon holt darüber den gebackenen Inhalt.
Wer Alkis17 live schaltet, muss **vorher ein Release dazu veröffentlichen**,
sonst sieht jeder neue Klon eine leere Welt.

**Was daran hängt:** Auf Alkis16 steht der SebboTower 8 m neben seinem Plateau
(der Standort wurde in PR #12 verschoben), und die Straßenböschung läuft dort
weiter durch sein Erdgeschoss. Alkis16 kennt außerdem keine Straßenmöbel.

**Falle, die schon einmal beinahe die falsche Entscheidung getragen hätte:** Der
erste Lauf einer nie gespielten Karte misst den Cache, nicht die Karte. Alkis17
brauchte 138 s, dann 84 s, dann 21 s. Bei einem Trend messen, bis er flach ist.

### 3.2 Fahrspuren & Markierungen P3–P7

**Stand:** Spec und Umsetzungsplan liegen vor, P0–P2 sind erledigt. Offen sind
fünf Module, jedes mit eigenem Settings-Bool, datenreinen Tests, Re-Bake und
Foto:

| Phase | Inhalt | Datenquelle |
|---|---|---|
| P3 | Zebrastreifen | leerer `Crossing`-Kanal, neuer `BuildCrossingMesh` |
| P4 | Abbiegepfeile | OSM `turn:lanes` (Resolver steht schon) |
| P5 | Busspuren | OSM `bus:lanes` / `lanes:psv` / `busway`, ohne Farbe (dt. Praxis) |
| P6 | Wartelinien (Haifischzähne) | Vorfahrtregelung je Kreuzung |
| P7 | Radstreifen | OSM `cycleway*`, rote Fläche, neuer Kanal `BikeLaneSurface` |

**Blocker:** Kein technischer — jedes Modul braucht einen Re-Bake (~15 min) und
ein Belegfoto. Der Rückstand ist reine Arbeitszeit. **Auch das Bake-Foto von P2
steht noch aus.**

### 3.3 Straßenmöbel in die gespielte Karte backen

**Stand:** Meshes, Platzierungsregeln, Spawner und Tests sind fertig (siehe
2.3). Sichtbar ist trotzdem nichts.

**Blocker, gemessen:** Die Möbel entstehen im **Bake**
(`WiesbadenWorldBuilder.cpp:1974` ruft `SpawnFurniture`), nicht zur Laufzeit.
Alkis16 wurde am 18.09. gebacken, der Möbel-Pass kam am 20.09. Und der
Alkis17-Bake vom 21.09. lief laut Protokoll gegen
`Data/Raw/OSM/wiesbaden.osm.forest.json` — die 3.607 nachgezogenen Möbelknoten
liegen aber in `wiesbaden.osm.moebel.json`. Ein möbelvollständiger Bake braucht
also `WB_OSM_FILE=…/wiesbaden.osm.moebel.json`.

**Zusätzlich offen:** die Kalibrierung. 78 % der 1.464 Verwürfe stehen an
Fußwegen und Pfaden mit Gehwegbreite 0 in der `RoadTypeLibrary`. Vorschlag aus
der Messung: 2,5 m Reichweite und 2,5 m Bankett für diese Typen (59 % → 85 %
Übernahme), und im Bankett **stehen lassen** statt andocken — sonst wandern die
Möbel im Median 2,1 m von ihrem kartierten Ort weg.

### 3.4 Bewuchs-Freihaltung

**Stand:** Bäume und Büsche wachsen mitten im SebboTower-Portal, auf seiner
Zufahrt und im Nerobergbahn-Wagen.

**Blocker, gemessen:** `FWiesbadenRoadClearance` wird **ausschließlich aus
Straßensegmenten** gebaut (`WiesbadenRegionAssets.cpp`, 150 cm Zuschlag). Es
kennt weder Gebäudegrundrisse noch private Zufahrten noch den Bahnkorridor. Die
Behebung braucht ein zweites Freihaltenetz aus diesen Quellen **und einen
Re-Bake der Kacheln** — die Streuung liegt in den Chunk-Paketen.

### 3.5 Fahrzeuglampen: verschiedene Fahrzeugtypen

**Stand:** Bremslicht, Blinker, Scheinwerfer und Rückleuchten laufen als eigene
ISM-Gruppen mit datenreiner Logik (bei 270 Fahrzeugen: 540 Scheinwerfer,
150 Bremslichter = exakt 2× die 75 gemeldeten Steher).

**Blocker:** Es gibt genau **zwei** Fahrzeug-Meshes — `SM_VWBeetle1969_Traffic`
und `SM_Bus`. Der ganze Verkehr besteht aus Käfern. Ein Lieferwagen muss gebaut
werden; Blender 5.2 liegt vor, Vorlage ist der `MeshBuilder` aus
`Tools/Blender/make_nerobergbahn.py`.

**Ungeklärter Nebenbefund:** Warum ein per Python erzeugtes Emissiv-Material im
`-game`-Lauf dunkel rendert, ist bis heute offen. Es liegt **nicht** daran, dass
Python es erzeugt hat — sämtliche Fassadenmaterialien sind so gebaut und rendern
einwandfrei.

### 3.6 Regen- und Schnee-Partikel (Niagara)

**Stand:** Regen und Schnee fallen sichtbar — als Bildschirm-Overlay nach dem
Tonemapper. Das war der Ausweg, nicht das Ziel.

**Blocker, nachgemessen:** **Niagara ist aus Python nicht baubar.**
`NiagaraEditorLibrary` fehlt, `NiagaraSystemFactoryNew` legt nur ein leeres
System an, und `unreal.NiagaraSystem` gibt weder Emitter noch `fixed_bounds`
noch `exposed_parameters` heraus. Die fünf `NS_Weather*`-Assets müssen von Hand
im Niagara-Editor gebaut werden; die Anleitung dafür steht in
`docs/Wetter_Niagara_Anleitung.md`.

**Grenze des heutigen Ersatzes:** Bildschirm-Niederschlag hat keine Tiefe. Er
verschwindet nicht hinter Häusern und wird von Brücken nicht abgeschirmt.

### 3.7 Ruckler beim Fahren

**Stand:** 15–20 Aussetzer von 50–94 ms je zweiminütiger Fahrt, im Stand keine.
Ursache eingegrenzt: **Zell-Laden im Spiel-Strang.** Nicht unser Code (unser
Chunk-`BeginPlay` ist 0–4 ms davon), nicht die GPU (4–8 ms), nicht Shader, nicht
die Speicherbereinigung. Fünf Ursachen per A/B-Lauf einzeln ausgeschlossen.

**Blocker:** Die Startzeile sagt `Async Loading Thread: false`. UE schaltet den
Lade-Strang ab, solange `GIsEditor` gilt — und der Direktstart über
`UnrealEditor.exe` ist genau das. Per Schalter **nicht** erzwingbar
(`!GIsEditor` steht fest im Engine-Code). Die Gegenprobe braucht ein
**paketiertes Spiel** (`package_game.cmd`, dauert Stunden). Erst dort ist zu
sehen, wie viel davon bloßes Editor-Artefakt ist.

**Noch nicht geprüft:** Kollisionsaufbau je Zelle (Trimesh-Body je Chunk) und
die 17 Komponenten je Chunk. Beides wäre je-Zelle-fest und passt zum Befund.

### 3.8 Fußgängerdichte: die leere Straßenseite

**Stand:** Auf dem Bodenbild stehen die Fußgänger auf dem **gegenüberliegenden**
Gehweg; der direkt neben dem Wagen bleibt leer. Aus der Luft verteilen sie sich
sauber.

**Blocker:** Verdacht, nicht Befund — einzelnen Abschnitten fehlt vermutlich das
Gehweg-Kennzeichen (`SidewalkType`), dort spawnt nie jemand. Netzweit tragen
32.274 Segmente einen Gehweg. Das ist mit einer Messung zu entscheiden, nicht
mit einer Vermutung.

### 3.9 Fahndung / Polizei — **nie umgesetzt**

**Stand:** Die Spezifikation liegt seit dem **2026-09-02** vor
(`docs/superpowers/specs/2026-09-02-fahndung-polizei-design.md`, Status
„ENTWURF zur Abnahme"): Sternchen-Level 0–5, Vergehen erhöhen es,
Polizeifahrzeuge jagen über das Straßennetz, Entkommen durch Sichtverlust.

**Gebaut ist davon nichts.** Im ganzen Quellbaum gibt es kein `WantedLevel`. Der
vorhandene `WiesbadenPursuer` ist ein einfacher Verfolger (Idle → Chasing →
Caught) für die Sylvia-Szene, kein Fahndungssystem.

**Blocker:** Die Abnahme des Entwurfs steht aus. Das ist die größte ungebaute
Einzelfunktion des Projekts.

### 3.10 AAA-Initiative: Teilprojekte 2–4

Die Initiative *Stadt detailreicher* ist in vier Teilprojekte geschnitten.
Teilprojekt 1 (Straßenrand-Schmuck) ist gebaut (siehe 3.3).

| Nr. | Thema | Stand |
|---|---|---|
| 2 | Licht & Stimmung | Entwurf vom 20.09., **Design-Review ausstehend** |
| 3 | Oberflächen / Materialien | **nicht spezifiziert** |
| 4 | Lebendigkeit | **nicht spezifiziert** |

Zum Entwurf von Teilprojekt 2 eine Richtigstellung: Er nennt „72.434
Laternenmasten unbeleuchtet". Gemessen sind es **48 gleichzeitig brennende**
Punktlichter, die mit dem Spieler wandern (`MaxActiveLampLights = 48`, Radius
26 m, 40.000 cd, warmweiß). Die Masten stehen alle als ISM; nur die Lichter sind
knapp. Der Entwurf zielt richtig, seine Ausgangsbeschreibung ist zu scharf.

### 3.11 Kleinere offene Punkte

- **Chaos-Käfer fährt nicht.** Der Chassis-Kollisionskörper reicht 2 cm tiefer
  als der Radaufstand — der Wagen liegt auf dem Bauch. **Blocker:**
  Editor-Python kann den Körper nicht anheben (`SkeletalBodySetups` ist
  protected, keine Geometrie-Accessoren). Es braucht ein Re-Rig in Blender.
- **Verkehrsverteilung.** Der Spawn verteilt round-robin **gleich** auf alle
  Spuren; weil Wohnstraßen die Hauptstraßen zahlenmäßig übertreffen, landet der
  Zuwachs in Seitenstraßen. Die passende Gewichtung liegt fertig da und wird
  nirgends gelesen: `FRoadTypeDefinition::TrafficDensityFactor` wird nur
  *gesetzt*, nie *benutzt*.
- **Nerotalbahn** ist nur Gleisgeometrie — kein fahrbares Fahrzeug.
- **Nerobergbahn-Optik:** Viadukt-Bögen sollen zweifarbig sein (rote Ziegelringe
  und helle Zwickel), das Deckgeländer schlank und metallisch. Aus der Doku
  verifiziert, noch nicht umgesetzt.
- **Bordkante der Wolkenbruch** ist durch die neue Geländeregel von 27 auf 35 cm
  gewachsen — Folge des 5-m-Rasters der Landscape.
- **Garagen-Innenkante** des SebboTower hat dieselbe 15-cm-Stufe, die im Portal
  behoben ist; sie liegt hinter dem Ankunftsziel und wurde bewusst nicht
  angefasst.
- **Fußgängerampeln** gibt es nicht — Fußgänger queren nie, deshalb war
  Fußgängergrün bisher sinnlos.
- **Echter ESWE-Fahrplan für Samstag und Sonntag** ist nachtragbar (gleiches
  PDF, Seite 3 ff.).
- **`-WbAtStreet=` und `-WbAerial=`** greifen in Läufen mit `-WbShotWhenReady`
  nicht; `-WbGoto=` ist der verlässliche Ersatz.
- **`rebuild_city.py` stellt am Ende selbsttätig die Default-Karte um.** Nach
  jedem Bake `git status Config/` prüfen.
- **Die Sonden haben keinen einzigen Test.** `ProbeArrival` ist eine
  660-Zeilen-Funktion, die Messung und JSON-Ausgabe zugleich besitzt; jede
  Abnahmeentscheidung ruht darauf.

---

## 4. Eigene Vorschläge, priorisiert

Die Reihenfolge folgt **Wirkung geteilt durch Aufwand**, nicht Interesse.

### Rang 1 — Alkis17 veröffentlichen und live schalten

*Aufwand: ein Bake, ein Release-Upload, ein Abnahmelauf. Wirkung: drei offene
Punkte auf einmal.*

Damit werden der korrekt stehende SebboTower, die Geländeregel und — nach einem
möbelvollständigen Bake — 3.607 Straßenmöbel auf einen Schlag sichtbar. Es ist
die einzige Maßnahme in dieser Liste, die fertige, bereits bezahlte Arbeit aus
der Schublade holt, statt neue zu beginnen. **Reihenfolge:** erst den Bake mit
`WB_OSM_FILE=…moebel.json` wiederholen, dann das Release, dann umschalten.

### Rang 2 — Ein zweites Fahrzeug-Mesh

*Aufwand: ein Blender-Tag. Wirkung: der auffälligste Realismusbruch.*

Der gesamte Verkehr besteht aus 1969er Käfern. Ein Lieferwagen und ein moderner
Kompaktwagen verändern den Eindruck der Stadt stärker als jede weitere
Markierungsphase — und die Lampen-, Blinker- und Bremslichtlogik ist bereits
fahrzeugunabhängig gebaut.

### Rang 3 — Das Fahndungssystem bauen

*Aufwand: groß. Wirkung: aus dem Sandkasten wird ein Spiel.*

Missionen, Ökonomie, Läden, Waffen und Verfolger-NPCs stehen alle schon. Was
fehlt, ist die Schleife, die sie verbindet: etwas tun → verfolgt werden →
entkommen. Die Spezifikation liegt seit drei Wochen fertig da. Der `Pursuer`
lässt sich als Polizeifahrzeug wiederverwenden, das Straßennetz-Routing
existiert, und das Guthaben ist der natürliche Ort für Strafen.

### Rang 4 — Die Sonden testbar machen

*Aufwand: mittel. Wirkung: Vertrauen in jede künftige Abnahme.*

`ProbeArrival` trägt 660 Zeilen ohne einen Test, baut ihr JSON von Hand und hat
allein in einer Sitzung zwei Bearbeitungsunfälle verursacht. Messung und
Berichtsform gehören getrennt, und die Berichtsform muss ohne Welt prüfbar sein.
Solange das so bleibt, ist jede Zahl in einer Abnahmetabelle nur so gut wie der
letzte Blick darauf.

### Rang 5 — Den Ruckler endgültig entscheiden

*Aufwand: ein paketierter Build (Stunden Rechenzeit, wenig Handarbeit).*

Es ist die einzige offene Frage, die sich **nicht** durch Nachdenken klären
lässt: Solange nur im Editor gemessen wird, ist unbekannt, wie viel vom Ruckler
Editor-Artefakt ist. Ein einziger paketierter Lauf beantwortet das — und
verhindert, dass weiter an einer womöglich gar nicht existierenden Ursache
optimiert wird.

### Rang 6 — Verkehr nach Straßenklasse gewichten

*Aufwand: klein. Wirkung: Hauptstraßen wirken wie Hauptstraßen.*

`TrafficDensityFactor` ist bereits befüllt und wird nirgends gelesen; in der Sim
existiert mit `GetSuccessorWeight` schon eine Rangfolge je Straßenklasse
(Motorway 20 … LivingStreet 0,6). Die Verdrahtung ist ein überschaubarer
Eingriff mit sichtbarer Wirkung.

### Rang 7 — Von P3–P7 zuerst die Zebrastreifen

*Aufwand: eine Phase. Wirkung: die sichtbarste der fünf.*

Zebrastreifen sind das, was einem Fußgänger sofort auffällt, und der
`Crossing`-Kanal steht leer bereit. P4 (Abbiegepfeile) danach, weil der Resolver
`turn:lanes` bereits auflöst.

### Was ich *nicht* empfehle

- **Niagara-Partikel von Hand bauen.** Das Post-Process-Overlay liefert das
  Ergebnis ohne Editor-Handarbeit. Die Tiefenwirkung lohnt den Aufwand erst,
  wenn die Stadt sonst fertig ist.
- **Den Chaos-Käfer reparieren.** Das kinematische Modell fährt, ist getestet
  und ist das, was ausgeliefert wird. Das Re-Rig kostet einen Blender-Tag für
  ein Opt-in hinter `-WbChaosCar`.
- **Teilprojekte 3 und 4 der AAA-Initiative spezifizieren**, bevor 1 und 2
  sichtbar in der gespielten Karte stehen. Sonst wächst der Vorrat an fertiger,
  unsichtbarer Arbeit weiter.

---

## 5. Nächste Durchgänge (vom Nutzer gesetzt)

- **Marktstraße-Overlay** — eigener Durchgang.
- **Optionsmenü** — eigener Durchgang. Das Pausemenü trägt bereits ein
  Ton-Unterfenster mit sieben dB-genauen Reglern; dort ist anzudocken.

---

## 6. Wie dieses Dokument aktuell bleibt

| Frage | Befehl |
|---|---|
| Zahlen zu Code, Tests, Karte | `python Tools/uebersicht.py --zeigen` |
| Welche Karte ist live? | `grep GameDefaultMap Config/DefaultEngine.ini` |
| Zeigt ein Skript auf eine tote Karte? | `python Tools/pruefe_kartenname.py` |
| Stimmt der gebackene Inhalt? | `Tools\fetch_city_content.cmd --check` |
| Sind die Gates grün? | `python Tools/vor_dem_commit.py voll` |
| Ist die Stadt heil gebacken? | Log nach `Chunk-Actors OHNE Render-Geometrie` durchsuchen |

Die ausführlichen Begründungen hinter den Befunden stehen in `AGENTS.md`
(Umgebung, Fallen, Werkzeug-Eigenheiten) und in den Spezifikationen unter
`docs/superpowers/specs/`.
