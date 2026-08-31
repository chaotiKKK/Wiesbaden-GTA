# Prompt: Wiesbaden als spielbare Stadt in Unreal Engine 5.8

Kopiere den Abschnitt unter der Trennlinie als Auftrag an einen Coding-Agenten.
Er enthält Ziel, Datenlage, Architektur und — der wichtigste Teil — die Fallen,
die in der ersten Umsetzung Wochen gekostet haben, jeweils mit der Messung, die
sie aufgedeckt hat.

---

## Auftrag

Baue ein spielbares Open-World-Spiel im Stil von GTA, das die **reale Stadt
Wiesbaden** aus echten Geodaten erzeugt. Unreal Engine 5.8, C++ in mehreren
Dateien nach Zuständigkeit getrennt. Keine Platzhalter, keine TODO-Kommentare,
keine Attrappen: Was du meldest, muss laufen.

### Datenquellen

| Datei | Inhalt | Größe |
|---|---|---|
| `Data/Raw/OSM/wiesbaden.osm.json` | OpenStreetMap, 225.479 Ways, >400.000 Knoten | 163 MB |
| `Data/Raw/ALKIS/wiesbaden.alkis.json` | Amtliche Gebäudegrundrisse | 107 MB |
| SRTM-Höhenmodell | Gelände | — |

Das Land Hessen stellt weitere offene Karten bereit; sie sind nutzbar, aber
nicht Voraussetzung.

Georeferenz: Ursprung bei lon 8,24 / lat 50,0824 / 117 m. Umrechnung über ECEF,
**X = Ost, Y = SÜD** (nicht Nord), 1 Unreal-Einheit = 1 cm.

### Umfang, an dem du dich messen kannst

Die erste Umsetzung erreichte diese Größenordnungen — sie sind Richtwerte, keine
Vorgaben:

- 121.721 Straßensegmente, 111.262 Fahrspuren, 20.213 Kreuzungen
- 2.700 km Straßennetz, 1.393 Chunk-Actors in World Partition
- Gelände 4033 × 4033 Punkte, 781,2 cm je Rasterzelle
- 297 Flächen (Plätze, Fußgängerzonen), 101 davon benannt
- 11.479 Landnutzungsflächen, 432 Gewässer, 1.857 Fließgewässer

### Aufbau in Stufen

1. **OSM einlesen** → Ways, Knoten, Tags
2. **Gelände** aus dem Höhenmodell → Landscape
3. **Straßennetz**: Ways zu Segmenten, Kreuzungen an Knoten mit ≥3 Armen
4. **Gelände unter Straßen einebnen**
5. **Geometrie**: Fahrbahn, Kreuzungsflächen, Gehwege, Bordsteine, Böschungen
6. **Gebäude** aus ALKIS
7. **Ausstattung**: Schilder, Ampeln, Laternen, Bäume, Leitpfosten
8. **Aufteilen** in Chunks, als External Actors speichern
9. **Simulationen** zur Laufzeit: Verkehr, Fußgänger, Ampeln, Wetter

---

## Die Fallen

Jede davon hat in der ersten Umsetzung echte Zeit gekostet. Die Messung dahinter
ist wichtiger als die Regel.

### 1. Sichtbarkeit steht nicht in den Daten

Alle 20.213 Kreuzungsflächen waren unsichtbar. Geprüft und für gut befunden
wurden: Dreieckszahl, Höhenlage, Material, Chunk-Zuordnung, Verdeckung durch das
Gelände. Der Fehler war die **Umlaufrichtung der Dreiecke** — die Normalen
standen fest auf „oben", aber die Rückseitenentfernung richtet sich nach der
Reihenfolge der Indizes.

```
Kreuzungsplatten: 78158 Dreiecke mussten umgedreht werden.
```

**Regel:** Bestimme die Umlaufrichtung je Dreieck aus dem Kreuzprodukt, nie aus
einer Annahme über den Algorithmus, der die Punkte geliefert hat.

**Werkzeug, das es in drei Minuten zeigte:** Gelände ausblenden und von oben
schauen. Sieht man **Himmel**, fehlt die Fläche oder sie ist rückseitig
zugewandt. Sieht man **Gelände**, liegt sie darunter. Zwei völlig verschiedene
Ursachen, im normalen Bild nicht zu unterscheiden.

### 2. Die Kreuzung ist die Autorität

Fahrbahn, Gehweg und Bordstein enden an derselben Kreuzung. Lässt man jeden
seine Randpunkte selbst aus Mittellinie und Breite rechnen, sind das drei Wege
zum selben Ergebnis — und drei Gelegenheiten für Abweichungen.

**Regel:** Die Kreuzung legt je Arm zwei Randpunkte fest („Tore"), alle anderen
übernehmen sie. Baue den Kreuzungsumriss aus diesen Toren, nach Richtung
sortiert, und zerlege ihn als Fächer vom **Schwerpunkt der Torecken** — nicht
vom OSM-Knoten, der bei Gabelungen außerhalb liegen kann.

Benutze **keine konvexe Hülle**: Sie verschluckt die Ecken schmaler Arme
zwischen breiten, und dann hängt dieser Arm an nichts.

### 3. Seitenzuordnung nicht herleiten, sondern messen

`GetLeftNormal(D) = (D.Y, -D.X)` ist eine Drehung um **minus** 90 Grad. Der
Vektor heißt „links" und zeigt nach rechts. Wer daraus eine Umlaufreihenfolge
ableitet, baut einen Stern mit Zacken statt einer Kreuzungsfläche — und die
Flächenformel meldet durch Vorzeichen-Aufhebung 31 statt 45 m², was nach „etwas
kleiner" aussieht statt nach „kaputt".

**Regel:** Ordne Randpunkte über den **kürzesten Abstand** zu und miss den
Versatz mit. Ein Wert nahe null bestätigt die Zuordnung; ein Wert in
Fahrbahnbreite verrät vertauschte Seiten sofort.

### 4. Der Rückschnitt an Kreuzungen

Die Trimmweite ist `halbe Breite des Querarms / |sin(Winkel)|`. Bei einer
**geradeaus durchlaufenden** Straße ist der Winkel zum Gegenarm 180°, der Sinus
also null. Ein Divisor-Minimum fängt die Division ab — und macht aus dem
harmlosesten Fall den teuersten:

```
vorher:  884 cm Rueckschnitt je Arm, Plattenabdeckung 42 %
nachher: 414 cm, 82 %
```

**Regel:** Gegenarme über 150° brauchen **keinen** Rückschnitt; die Bänder liegen
auf einer Linie. Spitze Winkel auf das Doppelte der halben Breite begrenzen.

### 5. Ein Platz ist keine Straße

297 Ways sind Flächen (`area=yes`, oder geschlossener Ring mit
`highway=pedestrian/footway`). Als Band gebaut ergibt ein Platz einen Pfad **um
sich selbst herum**, mit Wiese in der Mitte.

Geschlossene `service`-Wege sind dagegen meist Ringstraßen auf Parkplätzen —
also echte Fahrwege, keine Flächen.

### 6. Einebnen: streckenweise, nicht je Stützpunkt

Nimmt man je Rasterzelle die Höhe des **nächstgelegenen Stützpunkts**, greifen
Nachbarzellen bei 7,81 m Rasterweite auf Punkte zu, die entlang der Straße weit
auseinanderliegen — eine Treppe. Projiziere die Zelle stattdessen auf den
Streckenabschnitt und interpoliere dort.

**Brücken und Tunnel dürfen das Gelände nicht mitziehen.** Eine Brücke 13 m über
Grund hebt sonst das Gelände auf Brückenhöhe und begräbt die Straße darunter:

```
vorher:  73.180 von 1.972.278 Punkten unter dem Gelaende, schlimmster Fall 1352 cm
nachher:  2.645 (0,1 %), schlimmster Fall 209 cm
```

**Miss immer beide Seiten.** Eine Verbesserung der Verdeckung, die nur eine
Richtung betrachtet, erkauft sich die Zahl mit schwebenden Fahrbahnrändern.

### 7. Modelle: Ursprung und Blickrichtung

- Ursprung **zwischen die Füße** bzw. auf die Radaufstandsfläche, nicht in die
  Mitte. Sonst steckt die Figur zur Hälfte im Asphalt.
- Blickrichtung **+X**. Eine Figur mit 66 cm in X und 29,5 cm in Y schaut nach
  +Y und läuft seitwärts.
- Der Spawner darf den Höhenversatz **nicht fest annehmen**, sondern muss ihn aus
  den tatsächlichen Mesh-Grenzen rechnen — sonst schwebt jedes Modell, dessen
  Ursprung anders sitzt als das ursprüngliche.
- Blender exportiert mit `apply_unit_scale` bereits in Zentimetern. Ein
  zusätzlicher Faktor 100 beim Import ergibt eine 175 **Meter** hohe Figur.

### 8. Instanzen lassen sich nicht per Skelett animieren

Dutzende Fußgänger als Instanzen kosten fast nichts, teilen sich aber ein Mesh.
Lösung: vier Gangphasen als getrennte Meshes, je ein Instanzenpool, und jede
Figur landet im Pool ihrer Schrittphase. Beim Modellieren die **Füße** auf den
Boden setzen, nicht die Hüfte festhalten — sonst heben sich die Füße beim
Schwenken.

### 9. Verkehr

- Die Abstandsregelung darf **nicht an der Kantengrenze enden**. Ein Fahrzeug am
  Spurende sieht sonst das Fahrzeug auf der Kreuzungsverbindung davor nicht,
  fährt hinein, und wird erst im nächsten Schritt mit negativem Abstand auf null
  geklemmt — sichtbar als ineinander steckende Fahrzeuge.
- **Beschleunigung begrenzen, Bremsen nicht.** Die Abstandsregel löst exakt auf;
  ein gedeckeltes Bremsen heißt Auffahren.
- Fußgänger folgen der **gekürzten** Mittellinie, sonst stehen sie mitten auf der
  Kreuzungsfläche.

### 10. Fahrphysik

- Der Lenkbefehl darf **nie direkt** ins Giermodell. Der Einschlag ist ein
  Zustand, der der Eingabe mit begrenzter Geschwindigkeit folgt (rund 0,8 s von
  Anschlag zu Anschlag).
- Reifen haben **ein** Kraftbudget: `quer = mu·g · sqrt(1 - (längs/(mu·g))²)`.
  Ohne diese Kopplung lenkt es sich unter Vollbremsung wie ohne.
- `FMath::Sign(0)` ist 0 und weicht damit von **jedem** Ziel ab. Eine Prüfung
  „Vorzeichen verschieden, also Rückstellung" stuft das Einlenken aus der Mitte
  falsch ein.

### 11. Leistung

Gemessen an der ersten Umsetzung, je fünf Minuten Spielbetrieb:

```
Renderer         75-196 ms   <- Engpass
Spiel-Strang     10-45 ms
Ampeln/Verkehr/Fussgaenger  0,0 / 1,6 / 0,1 ms
Aussetzer        156 von 156 Bildern
```

- **Lumen kostet rund 23 %.** Sein Oberflächen-Zwischenspeicher legt Karten für
  jeden der 10.325 prozeduralen Mesh-Abschnitte an, ein Teil davon im
  Spiel-Strang. Für eine prozedural erzeugte Stadt das falsche Verfahren.
- `UProceduralMeshComponent` kocht Kollision **synchron**. `bUseAsyncCooking`
  setzen.
- **Halte die Zahl der Mesh-Abschnitte klein.** 10.325 Abschnitte sind 10.325
  Zeichenaufrufe. Plane die Chunk-Aufteilung von Anfang an daraufhin.

---

## Arbeitsweise

Das ist der Teil, der am meisten Zeit spart.

**Miss, bevor du eine Ursache benennst.** In der ersten Umsetzung waren
nacheinander falsch: fehlende Geometrie, Triangulierung, Kachelauflösung,
Deckel-Radius, Verdeckung durch Gelände. Jede Vermutung war plausibel, jede
kostete einen Aufbau von 40 Minuten. Die eigentliche Ursache — die
Umlaufrichtung — war keine Eigenschaft der Daten und wurde von keiner
Datenprüfung gefunden.

**Prüfe, was deine Zahl misst.** Diese vier Messungen sahen wie Aussagen aus und
waren keine:

- Ein Zähler, der **nach** der zählenden Schleife zurückgesetzt wird → „0 von 0",
  was nicht „nichts" heißt, sondern „nicht gemessen"
- Eine Verdeckungsstatistik, die **Böschungen mitzählt** — die laufen bauartbedingt
  bis auf Geländehöhe und gelten dann alle als verdeckt (8–14 % statt 4 %)
- Messläufe mit `taskkill /F` beendet → das Log wird nie geschrieben
- Eine Einstellung über `-ExecCmds` abschalten wollen, während die INI sie
  vorher setzt → beide Vergleichsläufe messen dasselbe

**Bau dir früh diese Werkzeuge**, sie zahlen sich zehnfach zurück:

| Werkzeug | Beantwortet |
|---|---|
| Gelände ausblenden | fehlt die Fläche, oder liegt sie darunter? |
| Materialien in Leuchtfarben | welche Fläche sehe ich hier eigentlich? |
| Kamera an jede Kreuzung, fünf Blickwinkel | aus einem Winkel ist nichts zu beurteilen |
| Kamera per **Straßenname** | „bei X ist etwas kaputt" nachgehen können |
| Selbstabbruch nach N Sekunden | Messläufe automatisieren, ohne das Log zu verlieren |
| Bildzeit alle 15 s, getrennt nach Strang | wo geht die Zeit hin |

**Schau hin, nicht nur auf Zahlen.** Der verschränkte Kreuzungsumriss meldete
31 statt 45 m² — das sah nach „etwas kleiner" aus. Das Bild zeigte einen Stern
mit Zacken.

**Schreibe jede Fehlerklasse auf, sobald du sie gefunden hast**, mitsamt der
Messung. Was du nicht aufschreibst, findest du ein zweites Mal.

---

## Offene Punkte der ersten Umsetzung

Ehrlichkeitshalber, damit du sie nicht für gelöst hältst:

- **Ruckeln**: 8 Bilder/s nach Abschalten von Lumen. Der Renderer bleibt der
  Engpass, die 10.325 Zeichenaufrufe sind der nächste Verdacht.
- **Ladezeit**: über vier Minuten.
- **Brücken** schweben bis 28 m über Grund.
- **Landnutzung und Gewässer** werden nicht gezeichnet — 11.479 Flächen, 432
  Gewässer, 1.857 Fließgewässer liegen ungenutzt in den Daten. Alles zwischen
  den Straßen ist deshalb Wiese.
- **Zweiarm-Knoten**: 75 % der Bandenden liegen dort; ob sie exakt aneinander
  stoßen, ist nie gemessen worden.
