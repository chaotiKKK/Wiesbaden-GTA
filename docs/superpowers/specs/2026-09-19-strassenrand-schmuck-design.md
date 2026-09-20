# Straßenrand-Schmuck: Echte OSM-Straßenmöblierung in der Stadt

- **Datum:** 2026-09-19
- **Status:** Freigegeben (Design-Review in dieser Session)
- **Pfad:** Teilprojekt 1 von 4 der Initiative „Stadt detailreicher / AAA-Schliff"
  (Teilprojekte: 1 Straßenrand-Schmuck · 2 Licht & Stimmung · 3 Oberflächen/Materialien · 4 Lebendigkeit)

## Ziel

Die ~4500 in OpenStreetMap verzeichneten Straßenmöbel-Objekte Wiesbadens — Bänke, Poller,
Abfallkörbe, Hydranten, Briefkästen, Automaten, Recycling-Container, Picknick-Tische —
werden als echte 3D-Objekte an ihren echten OSM-Positionen in die Stadt gebacken.
Ziel ist die „dritte Ebene" der Detaildichte: der Straßenrand, auf dem man im
Fußgänger-Modus direkt herumläuft.

**Nicht-Ziele:** prozedurale Auffüllung über OSM hinaus (bewusst abgelehnt — wir
wollen authentische Orte, keine erfundenen); Straßenlampen (existieren bereits
prozedural); planter/street_cabinet (mit 8/7 OSM-Nodes zu dünn für eigene
Kategorien); Interaktion (Hinsetzen, Einwerfen) — nur Darstellung.

## Quelllage (vermessen am 2026-09-19 via Overpass, Gebiet Wiesbaden admin_level=6)

| Kategorie | OSM-Tag | Nodes |
|---|---|---|
| Bank | `amenity=bench` | 1804 |
| Poller | `barrier=bollard` | 920 |
| Abfallkorb | `amenity=waste_basket` | 546 |
| Automat | `amenity=vending_machine` | 357 |
| Recycling-Container | `amenity=recycling` | 291 |
| Hydrant | `emergency=fire_hydrant` | 248 |
| Briefkasten | `amenity=post_box` | 210 |
| Picknick-Tisch | `leisure=picnic_table` | 124 |
| **Summe** | | **4500** |

(Die Vierer-Gruppe Körbe/Hydranten/Poller/Bänke wurde separat per Overpass-Query mit
3518 bestätigt — deckungsgleich mit den Tabellenzeilen. Die exakte Zahl nach Filterung
zeigt die Bake-Statistik.)

## Architektur & Datenfluss

Die Pipeline folgt exakt dem existierenden Schild-Muster (Importer → GIS-Pass →
Bake → ISM-Rendering):

1. **Importer** (`Tools/alkis_extract.mjs`): Die Tag-Whitelist bekommt die acht
   Kategorien (Tabelle oben). Übernommene Nodes landen als neues Top-Level-Array
   `furniture` im City-JSON: `{id, kind, lat, lon}` plus optionale lokale
   Zusatz-Tags (`material`, `colour`) für spätere Variation. Unbekannte `kind`s
   werden verworfen und gezählt. Erwartetes Volumen ~0,5 MB.
2. **Bake-Pass** (neu, im GIS-Layer neben dem bestehenden
   Straßenausstattungs-Pass): Wandelt Nodes in Weltkoordinaten, samplet die Höhe
   über `IHeightSampler`, wendet die Platzierungsregeln (siehe unten) an und
   schreibt die Instanzen als `FFurnitureInstance`-Arrays (je Kategorie eines)
   in die `FRoadFurnitureLayout`-Erweiterung.
3. **Bake der Chunk-Pakete:** Die Instanzen werden wie die Schild-Instanzen in
   die Chunk-External-Actor-Pakete gebacken. Streaming-Latenz bleibt null;
   der bekannte Re-Bake-Aufwand (~2 h, detached mit Polling) gilt einmalig.
4. **Runtime** (`URoadFurnitureSpawnerComponent`): Erweiterung um je Kategorie
   einen ISM-Slot — „eine Instanz je Objekt, gleiche Art teilt Draw-Call",
   identisch zum Schild-Tafel-Muster. Kein neues Component, kein neues Subsystem.

## Komponenten & Meshes

**Meshes** (Blender, je Kategorie ein bis drei Varianten, 300–1200 Tri,
UE-Konvention wie die existierenden Exporte; UV/Materialslots kompatibel zu den
AAA-Materialien aus Teilprojekt 3):

| Objekt | Quellform | Varianten |
|---|---|---|
| Bank | Holzlatten + Gusseisen-Seitenwangen | 2 (mit/ohne Armlehne) |
| Poller | Zylinder, abgesetzt, Reflektorring | 1–2 |
| Abfallkorb | Gitterzylinder mit Einwurf | 1–2 |
| Hydrant | Rotationskörper (Überflur) | 1–2 (Unterflur prüfbar) |
| Briefkasten | Gelber Kasten auf Standrohr | 1 |
| Automat | Box + Frontpanel (Decal-UV) | 1–2 |
| Recycling | Container-Boxensatz Glas/Papier/Textil | 3 Farbvarianten |
| Picknick-Tisch | Planken + Balken | 1 |

Neue Export-Skripte unter `Tools/Blender/` (ein Skript, acht Mesh-Funktionen,
z. B. `make_street_furniture.py`), Import headless wie
`aaa_import_materials.py`. Content-`.uasset`s bleiben git-ignoriert; getrackt
werden die erzeugenden Skripte (Reproduzierbarkeit).

**C++-Seite:** `RoadFurnitureGenerator.h/.cpp` bekommt
`FFurnitureInstance {Kind, Location, Rotation, Variant}` + die Platzierungsregeln;
`RoadFurnitureSpawnerComponent` bekommt je Kategorie einen ISM-Slot (UPROPERTY,
wie die Schild-Tafeln). Mesh-Zuordnung als Config (DefaultGame.ini Soft-Ref, wie
die City-Materialien) — kein hartkodierter Asset-Pfad im C++. Variation über
1–3 Mesh-Varianten je Kategorie + Zufalls-Yaw, deterministisch über den
Seed-Mechanismus des Bakes.

## Platzierungsregeln (alle im Bake-Pass, deterministisch)

1. **Höhe:** Position via `IHeightSampler` auf Gehweg-/Geländehöhe, kleiner
   Z-Offset gegen Z-Fighting.
2. **Gebäude-Ausschluss:** Node im Gebäude-Footprint (liegt beim Bake vor) →
   verwerfen. Fängt die typischen OSM-Verortungsfehler ab.
3. **Fahrbahn-Ausschluss:** Instanz auf dem Kfz-Fahrbahnstreifen (außer Poller,
   die dort legitim stehen) → verwerfen; Kriterium ist der Abstand zur Achse der
   zugehörigen Straße aus dem existierenden Straßennetz.
4. **Andocken:** Bis 1,5 m auf den nächsten befestigten Rand (Gehwegkante/Borde)
   verschieben, sonst verwerfen — verhindert „in der Wiese schwebende" Bänke.
5. **Kategorie-Feinheiten:** Bänke + Picknick-Tische ausrichten (Rücken zur
   Straße, vor Grünfläche), Poller entlang der Fahrbahnkante,
   Recycling-Cluster zusammenziehen (Glas/Papier/Textil nebeneinander, wenn OSM
   sie einzeln verzeichnet), Automaten gegen Verkehrsrichtung.
6. **Statistik:** Verworfene Instanzen je Regel ins Bake-Log (Muster der
   Diagnose-Pässe).

Erwartung: von 4500 Nodes überleben je nach Regelstrenge grob 70–90 %; die
Bake-Statistik zeigt die tatsächlichen Zahlen beim ersten Lauf.

## Fehlerbehandlung

- Fehlendes Mesh (git-ignorierte uassets nach frischem Klon!): Warn-Log je
  Kategorie + Kategorie wird übersprungen — gleiche Degradation wie bei den
  Schild-Tafeln, kein Crash.
- Unbekannte OSM-`kind`s: beim Import verwerfen und zählen, nicht raten.
- Bake-Fail mitten im Schreiben: bestehende Sicherheitsregeln des Bake-Systems
  (Ziel wird erst nach erfolgreichem Bau geschrieben) greifen unverändert.

## Tests

- **C++-Automationstests** (Pass): synthetisches Straßennetz + Nodes →
  erwartete Filter-Ergebnisse (im Gebäude → verworfen; am Rand → angedockt;
  Poller auf Fahrbahn → erlaubt), plus Determinismus (zwei Läufe → identische
  Instanzen).
- **Importer-Selbsttest** (JS): Whitelist-Übernahme, `furniture`-Schema,
  unbekannte `kind`s.
- **`bake_abnahme.py`-Erweiterung:** Instanzen-Zählung je Kategorie +
  Chunk-Abdeckung als Abnahme-Kriterium.

## Ausrollung (Reihenfolge)

1. Importer + JSON-Schema (getrackt, sofort prüfbar)
2. Blender-Meshes + headless Import (8 Kategorien, 12–15 Varianten gesamt)
3. C++: Bake-Pass + Spawner-Erweiterung + Tests, DLL-Build
4. Abnahme-Lauf (`bake_abnahme.py`): Platzierungen visuell + Statistik prüfen
5. Voll-Re-Bake (~2 h, detached mit Polling) + Streaming-Diagnose

## Offene Punkte für die Planung

- Genauer Pass-Anbindungspunkt im GIS-Layer (Datei/Call-Site entscheidet die
  Implementierungsplanung, nicht das Spec).
- Ob die Hydranten-Unterflur-Variante (Halm/Triangle) neben der Überflur-Variante
  sinnvoll ist — entscheidet die Sichten-Abnahme.
- Recycling-Cluster-Erkennung: Schwelle für „zusammenziehen" (Abstand in m) wird
  bei der Implementierung an der Bake-Statistik kalibriert.

## Umsetzungsnotiz (2026-09-20, Schritt 1 erledigt)

Zwei Annahmen des Specs haben der Wirklichkeit nicht standgehalten:

1. **Der Importer ist nicht `Tools/alkis_extract.mjs`.** Das Skript wandelt
   ALKIS-XML in OSM-aehnliches JSON fuer die GEBAEUDE. Die Strassenmoebel
   kommen aus der OSM-Datei, die der C++-Parser (`OSMDataParser`) direkt liest -
   es gibt kein dazwischenliegendes City-JSON mit einem Top-Level-Array
   `furniture`. Der Nachzug gehoert darum auf dieselbe Ebene wie der
   Wald-Nachzug: ein Python-Werkzeug, das die fehlenden Knoten per Overpass
   holt und in eine KOPIE der OSM-Datei mischt.
2. **Die Daten waren gar nicht da.** `wiesbaden.osm.forest.json` enthielt zwar
   Baenke, aber praktisch keine der uebrigen sieben Kategorien (0 post_box,
   je 1 waste_basket/recycling/vending_machine/picnic_table) - die
   urspruengliche Overpass-Abfrage in `OSMDataParser.cpp` fragt sie nicht ab.
   Ohne den Nachzug haette der Bake-Pass ins Leere gegriffen.

**Umgesetzt:** `Tools/fetch_street_furniture.py` (+ Selbsttest
`Tools/test_street_furniture.py`, 11 Faelle) holt die acht Kategorien im
Stadtrechteck (~6 km um den Ursprung, dasselbe wie beim Wald) und mischt sie
nach `Data/Raw/OSM/wiesbaden.osm.moebel.json`. Die Art steht an EINEM Tag
`wb:furniture`, damit der C++-Pass nicht acht Tag-Kombinationen nachbauen muss.
Vorhandene Knoten werden ERGAENZT, nicht dupliziert (ein zweiter Eintrag mit
derselben Id ueberschriebe im Parser den ersten und verbeulte die Ways).

**Gemessen (2026-09-20):** 3.607 Knoten statt der im Spec genannten 4.500 - die
4.500 gelten fuer das ganze Stadtgebiet (admin_level=6), 3.607 fuer das
Rechteck, das auch gebacken wird. Verteilung: 1.448 Baenke, 790 Poller, 438
Abfallkoerbe, 285 Automaten, 220 Recycling, 180 Briefkaesten, 167 Hydranten,
79 Picknick-Tische. Davon 3.219 neu in der Datei, 388 vorhandene Knoten nur um
die Tags ergaenzt.

**Naechster Schritt:** Bake-Pass (`RoadFurnitureGenerator`) mit den
Platzierungsregeln, dann Meshes und ISM-Slots.

