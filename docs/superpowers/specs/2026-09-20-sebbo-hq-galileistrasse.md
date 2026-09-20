# Sebbos International Mega Company — Hauptsitz an der Galileistraße

- **Datum:** 2026-09-20
- **Status:** ENTWURF — zwei Punkte brauchen eine Entscheidung (Abschnitt „Offen")
- **Quelle:** Bildschirmaufnahme Street View „39 Galileistraße" (81 s, 13 Bilder
  ausgewertet unter `Saved/Diagnose/galilei_video/`) + OSM-Bestand

## Zwei Befunde, die den Zuschnitt bestimmen

**1. Galileistraße 39 gibt es in den Daten nicht.** Die Hausnummern der
Galileistraße im OSM-Bestand sind 16–24, 28, 30, 32, 33, 35 — dann bricht es ab.
Auch 37 fehlt. Street View kennt die Adresse, OSM nicht; die Stadt im Spiel wird
aus OSM gebaut, dort ist an dieser Stelle also kein Gebäude mit dieser Nummer.
Die Nummerierung der ungeraden Seite wächst nach Westen (17 → 19 → 21 → 23 →
33 → 35), 39 läge also westlich von 35 — dort, wo die Galileistraße auf die
Platter Straße (K651) trifft. Genau diese Kreuzung zeigt das Video.

**2. Es gibt im Projekt keine Tür, keinen Schalter, keinen Aufzug.** Gesucht
wurde nach door/tuer/elevator/fahrstuhl/lichtschalter im gesamten Quellbaum:
kein Treffer. Was es gibt und was als Muster taugt:

| Vorhanden | Taugt als Muster für |
|---|---|
| `WiesbadenBusInterior` (datenreine Kastenbeschreibung → Mesh, mit Test) | begehbarer Innenraum aus Kästen |
| `RoadFurnitureSpawnerComponent` (HISM je Mesh/Material, Sichtweiten) | Instanzierung der Innenausstattung |
| `Tools/Blender/make_street_furniture.py` + Import | Möbel, Türblätter, Aufzugskabine als echte Meshes |
| Laternen-Lichtpool (`MaxActiveLampLights`, nächste N leuchten) | Deckenlicht ohne VRAM-Sprengung |
| `AWiesbadenHelicopter` + Standstück-Prüfung | Landeplatz, Anflug |
| `Tools/moebel_nachweis.py` | Sichtnachweis je Bauabschnitt |

Der interaktive Teil der Anforderung (**funktionierende Türen, Lichtschalter,
Fahrstühle**) ist damit kein Ausbau, sondern ein neues System. Das ist der
größere Teil der Arbeit, nicht die Geometrie.

## Was das Video über die Umgebung sagt

Ecke Galileistraße / Platter Straße (K651), Aufnahme April 2022:

- **Bebauung:** drei- bis fünfgeschossige Wohnblöcke, heller Putz (weiß bis
  creme), Flachdächer und flache Satteldächer, Balkone zur Straße, Garagenreihen
  mit weißen Schwingtoren an der Grundstückskante.
- **Straßenraum:** zweispurige Kreisstraße mit Mittelstreifen-Markierung und
  aufgemaltem „K651", Gehweg beidseitig, Bordsteinkante, Fußgängerüberweg an der
  Einmündung.
- **Grün:** alte Straßenbäume in Reihe (Linden/Ahorn, Kronen über die Fahrbahn
  reichend), geschnittene Hecken an den Vorgärten, niedrige Sandsteinmauer mit
  Metallgeländer, auf der Gegenseite ein Parkstreifen mit dichtem Baumbestand
  (Nerotal).
- **Kleinkram:** Mülltonnen am Bordstein, Fahrradbügel, Hinweisschilder,
  parkende Fahrzeuge längs.

Vieles davon **baut die Pipeline bereits**: Straße mit Markierungen, Gehwege,
Bordsteine, Bäume aus OSM, Straßenmöbel (seit heute mit echten Meshes),
Laternen, Schilder. Neu wären Garagentore, Hecken, die Sandsteinmauer mit
Geländer und Balkone — und die sind nicht ortsspezifisch, sondern gehören in die
Gebäude-/Ausstattungspipeline, sonst steht eine Vorzeigeecke in einer Stadt, die
sonst anders aussieht.

## Das Bauprogramm

Turm nach Vorbild „Stark Tower", also ein Hochhaus mit abgesetzter Krone, in
eine Straße mit vier- bis fünfgeschossiger Blockbebauung. Das ist ein bewusster
Fremdkörper — es sollte einer sein, aber die Höhe bestimmt, wie weit er die
Silhouette der Stadt verändert.

| Teil | Inhalt |
|---|---|
| Tiefgarage | Rampe von der Straße, zwei Ebenen, Stellplätze, Aufzugsvorraum |
| Erdgeschoss | Eingangshalle, Empfang, Aufzugsvorraum, Durchgang zum Treppenhaus |
| Regelgeschosse | Grundriss je Stockwerk, Flur, Aufzugsschacht, Treppenhaus |
| Dachgeschoss | Technik, Zugang zum Dach |
| Dach | Hubschrauberlandeplatz mit Markierung, Befeuerung, Windsack |
| Technik | Fahrstühle (fahren, Türen, Etagenwahl), Türen (öffnen/schließen), Lichtschalter, Beleuchtung je Raum |

## Ausrollung in Stufen

Jede Stufe endet mit einem Bild aus dem laufenden Spiel (`moebel_nachweis.py`
als Muster) und grüner Testbatterie.

1. **Hülle und Ort.** Turm als Mesh (Blender) am festgelegten Grundstück,
   Kollision, Nanite/LOD, Silhouette im Stadtbild geprüft. Kein Innenraum.
2. **Vertikale Erschließung.** Treppenhaus und Aufzugsschacht als begehbare
   Geometrie über alle Geschosse, Dachausstieg. Der Spieler kann zu Fuß hoch.
3. **Landeplatz.** Dachfläche mit H-Markierung, Randbefeuerung, Windsack;
   Anflug und Aufsetzen mit dem Ka-52 belegt.
4. **Eingangshalle und Tiefgarage.** Erdgeschoss begehbar, Rampe befahrbar.
5. **Technik: Türen.** Erstes Interaktionssystem — Türblatt, Angel, Auslöser,
   Zustand. Danach überall verwendbar, nicht nur hier.
6. **Technik: Licht und Schalter.** Leuchten je Raum am Lichtpool-Muster,
   Schalter schaltet die Gruppe.
7. **Technik: Fahrstühle.** Kabine fährt, Türen takten, Etagenwahl.
8. **Umgebung.** Garagentore, Hecken, Sandsteinmauer mit Geländer, Balkone —
   als allgemeine Bauteile, angewandt auf diese Ecke.

Stufe 1–4 ist Geometrie und geht nach dem bekannten Muster. Stufe 5–7 sind neue
Systeme; sie sind der eigentliche Umfang und der Grund, warum das nicht in einem
Zug geht.

## Offen — hierauf warte ich

1. **Welches Grundstück?** Nummer 39 gibt es in den Daten nicht. Drei
   Möglichkeiten: (a) die Lücke westlich von 35 an der Einmündung zur Platter
   Straße, wie das Video sie zeigt; (b) ein vorhandenes Gebäude ersetzen; (c)
   den Turm frei an einer benachbarten Ecke setzen.
2. **Wie hoch?** Das Stark-Tower-Vorbild ist ein Wolkenkratzer, die Nachbarschaft
   ist fünfgeschossig. 60 m, 120 m oder mehr verändern das Stadtbild
   unterschiedlich stark — und die Höhe entscheidet über Geschosszahl und damit
   über den Aufwand von Stufe 2 und 7.
