# Platzierungsregeln für Ausstattung und Bewuchs — Design

**Datum:** 26.09.2026 · **Status:** Entwurf zur Abnahme
**Teilprojekt 1 von 3** der Linie „akurate, maßstabsgetreue Stadt" (2 = Straßen-/Gebäudedetailtreue, 3 = Wasserflächen und Schwimmen).

## 1. Warum

Die Stadt wird aus echten Daten gebaut (OSM für Straßen/Regionen/Ausstattung, ALKIS für
Grundrisse, LoD2 für Höhen, DGM1 für das Gelände). Die **Platzierung** der Ausstattung folgt
diesen Daten aber nur ungefähr: Objekte wandern bei Bedarf bis zu 9 m weit, Schilder stehen
teils auf dem Belag, und der Bewuchs kennt nur Straßen — nicht Gebäude, nicht die Bahn.

Belegte Zahlen aus dem Bake-Lauf vom 26.09.2026 (Ziel `Alkis32`, abgebrochen; dieselbe
Regel wie in der gespielten Karte `Alkis31`):

| Messwert | Zahl |
|---|---|
| Bestand | 52.690 Schilder, 187.076 Leitpfosten, 72.433 Laternen (dazu 112.472 Markierungen, vom Sweep ausgenommen) |
| „an den Rand versetzt" | **130.059** der 312.199 gesweepten Objekte (42 %), nur **412** entfernt |
| Suchraum dieser Verschiebung | 8 Richtungen, 1-m-Schritte, **bis 9 m** — auch längs der Straße |
| OSM-Knotenschilder | **kein** Seitenversatz (nur Z), Blickrichtung aus der OSM-Zeichenrichtung |
| Straßenmöbel im Bake | **3.607** `wb:furniture`-Knoten fehlen: gebacken wurde mit `wiesbaden.osm.forest.json` |
| Bewuchs-Freihaltung | nur aus Straßensegmenten, 150 cm Zuschlag — kein Grundriss, kein Bahnkorridor; Bäume stehen dadurch im SebboTower-Portal und im Nerobergbahn-Wagen |

## 2. Ziel und Erfolgskriterien

Ziel: Ausstattung und Bewuchs stehen **dort, wo die Karte sie vorsieht** — und wenn sie dort
nicht stehen können, wandern sie so wenig wie möglich, immer quer zur Straße, nie auf die
Fahrbahn.

Fertig ist dieses Teilprojekt, wenn

1. alle Prüfungen aus Abschnitt 7 grün sind (vorher rot),
2. der Audit-Lauf (Abschnitt 8) **0 Verstöße** meldet,
3. der Bake auf einer neuen Karte dieselben Bauzahlen wie `Alkis31` zeigt (Chunk-Zahl,
   Vertices, Dreiecke) — die Regeln ändern nur Ausstattung und Bewuchs,
4. die Bildpaare an den Belegstellen vorliegen (Abschnitt 8).

`Alkis31` bleibt bis dahin die gespielte Karte; das Umschalten des Defaults ist ein eigener,
ausdrücklich abgenommener Schritt.

## 3. Nicht-Ziele (bewusst außen vor)

- **Kein Laufzeitfilter.** Die Entscheidung „Generator-only" bleibt: die Regeln laufen im
  Daten-Pass, nicht im Spiel.
- Keine Änderung an Fahrbahnmarkierungen (sie gehören auf die Fahrbahn).
- Keine Schildergruppen an einem Mast (zwei Zeichen bleiben zwei Masten).
- Keine Sonderregel für Einbahnstraßen — die Seitenregel (R1) deckt sie mit ab.
- Kein Umbau der Spawner: `RoadFurnitureSpawnerComponent` und `RegionAssetSpawnerComponent`
  konsumieren weiter nur die Layouts.

## 4. Bausteine

Alles neu Gebaute liegt im GIS-Modul und ist **datenrein** (kein Welt-/Komponenten-Zugriff),
damit es wie die übrige Pipeline auf dem Worker-Thread laufen kann.

### 4.1 `FWiesbadenPlacementMask` (neu)

Kennt drei Sperrarten und beantwortet zwei Fragen:

- `bool IsBlocked(const FVector2D& Point, EPlacementKind Kind) const`
- `bool ResolveCarriagewayFreeSpot(const FVector& Wish, double MaxOffsetCm, FVector& Out) const`

| Sperrart | Quelle | Zuschlag |
|---|---|---|
| `Carriageway` | `FRoadNetwork` — Erschließungswege (`highway=service`) sind über `IsDrivable` bereits im Netz | 20 cm (wie heute) |
| `Building` | `TArray<FGeneratedBuilding>` (`FootprintCenterCm`, `FootprintExtentCm`, Rotation) **plus** eine kurze Liste eigener Sonderbauten aus Projektkonstanten (SebboTower-Sockel, beide Bahnhofshallen) | 0 (exakt am Grundriss) |
| `Rail` | `FWiesbadenRailAxes` | ±200 cm um die Achse |

Die Sonderbauten gehören dazu, weil ihre Geometrie aus Projektkonstanten stammt und nicht
aus ALKIS/OSM: ohne sie bliebe der gemeldete Fall „Bäume im SebboTower-Portal" auch nach dem
Umbau stehen.

Aufbau wie die vorhandenen Freihaltenetze: Zellgitter (`FCellGrid`, 50-m-Zellen), in das je
Sperrart die überdeckten Zellen mit Objekt-Indizes eingetragen werden. Die Gebäude-Prüfung
`IsInsideFootprint` und das Gitter existieren bereits im Möbel-Pass und werden
wiederverwendet, nicht neu erfunden.

**Nächster freier Platz — analytisch, nicht gesucht.** Der Aufrufer übergibt die Wunschposition;
die Maske nimmt die nächste Achse, rechnet die Querkoordinate des Punktes und verschiebt
**ausschließlich quer** um `nötiger Abstand − vorhandener Abstand`. Reicht das nicht (weil ein
zweites Hindernis im Weg liegt), folgt eine beschränkte Nachbesserung in 25-cm-Schritten
entlang derselben Querrichtung. Harte Grenze: `MaxOffsetCm` (2,50 m für Ausstattung). Findet
die Maske nichts, meldet sie das — der Aufrufer entscheidet (heute: Objekt fällt weg und wird
gezählt).

`ResolveCarriagewayFreeSpot` löst **nur** die Fahrbahn-Sperre auf; das gefundene Ziel muss zusätzlich
frei von `Building` und `Rail` sein (kein Schild wird ins Haus geschoben). Für den Bewuchs
gibt es kein Auflösen: er fragt nur `IsBlocked` und weicht erst gar nicht aus.

### 4.2 `FWiesbadenRailAxes` (neu, reine Daten)

Die rohen Achspolylinien beider Bahnen in Lon/Lat, also genau die Daten, die heute fest in
`World/WiesbadenNerobergbahn.cpp` und `World/WiesbadenNerotalbahn.cpp` stehen. Beide
World-Actoren lesen ihre Achsen künftig aus dieser Quelle, die Maske liest dieselbe Liste.
Damit gibt es eine Wahrheit für „wo liegt die Bahn".

Prüfstein für den Umzug: die gebaute Geometrie muss unverändert bleiben — Trassenlänge
A 43.439 cm / B 43.075 cm und die geloggten Hallenkoordinaten Tal/Berg müssen identisch sein.

## 5. Die sechs Regeln

Für alle gilt: Positionen bleiben in Weltkoordinaten (cm), Höhen weiter über den
`IHeightSampler`, Zählungen wandern in den Report.

**R1 — Knotenschilder** (OSM `traffic_sign`). Standort bleibt der kartierte Punkt. Liegt er
auf dem Belag, wandert das Schild quer auf Bordsteinkante + `SignLateralOffsetCm` (50 cm),
höchstens 2,50 m. Blickrichtung aus der Seite: steht das Schild **rechts** der Achsrichtung,
schaut es gegen den Verkehr dieser Richtung (Yaw = Achsrichtung + 180°), steht es **links**,
mit der Gegenrichtung. Der Spawner belegt die Konvention: die Tafel-Normale ist
`Sign.Rotation.Vector()` (`MakeFromZY(Facing, Up)`) — die Blickrichtung.

**R2 — Kreuzungsschilder** (Stopp/Vorfahrt/Verkehrszeicheninsel). Standort wie heute
(Fahrbahnende + Backset + Seite), die Blickrichtung kommt aus derselben Regel wie R1. Ein
Stopp-Schild schaut damit auch bei verdrehter OSM-Zeichenrichtung in den ankommenden Verkehr.

**R3 — Tempo- und Zonenschilder.** Nur **ein** Schild je Zeichen und Knoten: Entdopplung über
(Zeichen-Id, ±3 m), bevorzugt die Instanz mit korrekter Blickrichtung. Heute setzt jeder
Segmentanfang am selben Knoten sein eigenes Schild.

**R4 — Sweep „Fahrbahn frei".** Wie heute ein gemeinsamer Durchgang für Knoten-,
Kreuzungs- und Tempolimitschilder, Leitpfosten und Laternen — aber ausschließlich quer, mit
2,50-m-Grenze statt acht Richtungen bis 9 m. Markierungen bleiben unangetastet,
Straßenmöbel bleiben ausgenommen (ihr eigener Pass setzt sie bereits am Rand).

**R5 — Straßenmöbel.** Statt „bis in die Randzone andocken": liegt der kartierte Ort außerhalb
der Fahrbahn, bleibt das Möbel dort stehen. Nur wer auf dem Belag steht, wandert quer auf den
Rand (≤ 2,50 m). Das ist der aus der Messung vorgeschlagene Weg (Gesamtstand 3.3): heute
verschiebt das Andocken die Möbel im Median ~2,1 m von ihrem kartierten Ort.

**R6 — Bewuchs.** Der Grün-Pass benutzt dieselbe Maske: kein Baum und kein Busch auf
Fahrbahn/Zufahrt, nicht in Gebäuden, nicht im Bahnschlauch. Ufer- und Industrie-Objekte
prüfen zusätzlich gegen Gebäude; ihre bisherige Fahrbahn-Prüfung bleibt.

## 6. Datenfluss und Bake

**Maske in der Pipeline** (`WiesbadenCityPipeline::BuildCityData`), zweimal aufgebaut:

1. **Vor dem Grün-Pass:** Fahrbahn (inkl. Erschließungswege) + Bahn. Damit der Grün-Pass die
   Gebäude-Grundrisse kennt, wandert die Region-Asset-Streuung **hinter** den Gebäude-Pass
   (heute 66 %, Gebäude 68 %); die Regionen-Zuordnung der Gebäude bleibt bei 62 % davor.
   `GenerateClearOfRoads` bekommt denselben optionalen `Buildings`-Zeiger, den der Möbel-Pass
   bereits hat.
2. **Vor dem Möbel-Pass** (92 %): dieselbe Maske plus Gebäude — dort ist alles vorhanden.

Beide Pfade (Editor-Bake und Laufzeit-`GameInstance`) rufen dieselbe datenreine Kette auf und
verhalten sich damit gleich.

**Bahnachsen:** World-Actoren und Maske lesen `FWiesbadenRailAxes` (Abschnitt 4.2).

**OSM-Datensatz:** Der nächste Bake läuft mit `Data/Raw/OSM/wiesbaden.osm.moebel.json`.
Nachgezählt: gegen die heute benutzte `wiesbaden.osm.forest.json` hat sie **dieselben 1.434
Relationen** (der Wald bleibt also vollständig) und 225.511 Ways sowie zusätzlich **3.607
`wb:furniture`-Knoten**. Kein Merge nötig — nur der Pfad im Bake-Aufruf.

**Neue Karte:** Der Bake schreibt eine neue Karte (Arbeitsname `Alkis32`). Die Platzierungen
liegen in den externen Chunk-Actor-Paketen; Regeln ändern heißt neu backen. `Alkis31` bleibt
Default, bis Zahlen und Bilder abgenommen sind.

**Unverändert:** Straßen-, Gebäude- und Geländegeometrie. Kontrollzahl: gleiche Chunk-Zahl und
gleiche Vertex-/Dreieckszahlen wie `Alkis31`.

## 7. Prüfungen (datenrein, vorher rot / nachher grün)

| Nr. | Regel | Prüfung |
|---|---|---|
| 1 | R1 | Knotenschild auf dem Belag → steht danach auf der Bordsteinkante, Versatz ≤ 2,50 m, kein Schild innerhalb der Fahrbahn |
| 2 | R1 | Achse in +X, Schild rechts → Blickrichtung −X; Schild links → +X |
| 3 | R3 | drei Segmente, gleicher Startknoten, gleiches Limit → genau ein Schild; zwei verschiedene Limits → zwei |
| 4 | R4 | Objekt in der Mitte einer 7-m-Straße → wandert nur quer, ≤ 2,50 m; Objekt mitten im Kreisverkehr → fällt weg und wird gezählt |
| 5 | R5 | Bank auf dem Gehweg bleibt (< 5 cm Versatz); Bank auf dem Belag wandert quer auf den Rand |
| 6 | R6 | kein Baum im Gebäude-Grundriss, keiner im Bahnschlauch, aber einer 3 m daneben |
| 7 | alle | zwei Läufe ergeben identische Layouts (Hash über die Positionen) |
| 8 | Reihenfolge | die Umsortierung ändert die Regionen-Zuordnung der Gebäude nicht (RegionMap vor/nach gleich) |

Die Prüfungen liegen bei den bestehenden Test-Suiten `RoadFurnitureGeneratorTest`,
`RegionAssetTest` und in einer neuen `PlacementMaskTest`. Fällt eine Regel durch, bleibt der
Report rot — es gibt keinen „grünen" Sonderpfad.

## 8. Belege: Vorher-Nachher an denselben Stellen

Ein **Audit-Lauf auf dem Ist-Stand** (derselbe Generator, nur zählend, ohne Verhaltensänderung)
schreibt `placement_report.json`: Verstoßzahl je Regel plus bis zu 20 Beispielkoordinaten.
Diese Koordinaten sind die Fotostellen. Der Audit läuft am Ende des Daten-Passes (nach den
Gebäuden, wo die Grundrisse vorliegen) und wird über einen Schalter im Bake-Aufruf
(`-WbPlacementAudit`) ausgelöst; dieselbe Funktion ist aus den Tests aufrufbar.

Nach der Umsetzung:

- derselbe Audit-Lauf → Verstoßzahl **0**,
- derselbe Bake → Log-Zeile („versetzt/entfernt"), Schilderbestand, Bake-Zeit,
- dieselben Koordinaten per Posen-Datei aufgenommen (`Saved/Diagnose/…`, Kamera- und
  Blickparameter identisch), abgelegt als Paar vorher/nachher.

Ehrliche Grenze: die Bildpaare stammen aus **zwei Läufen**. Sie sind Beleg der Platzierung an
dieser Stelle, kein Pixelvergleich — Bilddifferenzen über Läufe sind in diesem Projekt
nachweislich wertlos (Verkehr, Wetter, fremde Threads).

## 9. Fehlerverhalten und Grenzen

- **Leere Maske** (kein Netz): Der Pass läuft wie heute, ohne zu verschieben; der Report trägt
  ein Flag (`bMaskAvailable = false`), damit die Zahl im Log als „ohne Maske" lesbar ist.
- **Fehlende oder unprojizierbare Bahnachsen** (kein Geo-Bezug): nur Fahrbahn + Gebäude, im
  Report gezählt.
- **Kein Absturz, kein stiller Fehlschlag:** jeder Verwurf und jede Nicht-Auflösung landet in
  einer Zählung.
- **Laufzeit:** Maske und Analyse sind Zellgitter wie die vorhandenen Netze; die Bake-Zeit wird
  gemessen und mit dem Vorlauf verglichen (Ziel ≤ 10 % länger).

## 10. Restunsicherheit

Die Standseite eines OSM-Knotenschilds kennt nur der kartierte Punkt. Liegt er exakt auf der
Achse (selten), entscheidet die Regel „nach rechts". Die Blickrichtung folgt derselben Seite —
sie ist damit so gut wie die Karte, nicht besser.

## 11. Was danach kommt

Teilprojekt 2 (Straßen- und Gebäudedetailtreue: Markierungen P3–P7, LoD2-Dächer, Fassaden) und
Teilprojekt 3 (Wasserflächen aus den Regionen, Pegel, Uferkante, Schwimmen im `WiesbadenFootPawn`)
bekommen eigene Specs. Die Platzierungsmaske aus diesem Entwurf ist die gemeinsame Grundlage:
Wasserflächen brauchen später dieselbe Antwort auf „ist hier frei?" und dieselben Achsendaten.
