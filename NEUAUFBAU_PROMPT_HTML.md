# Prompt: Wiesbaden als fahrbare Stadt in EINER HTML-Datei

Kopiere den Abschnitt unter der Trennlinie als Auftrag an einen Coding-Agenten.

---

## Auftrag

Baue ein fahrbares Open-World-Spiel der **realen Stadt Wiesbaden** als **eine
einzige HTML-Datei**. Kein Bauschritt, kein Server, keine externen Dateien: Die
Datei wird doppelgeklickt und läuft im Browser. Alles inline — CSS, JavaScript,
Shader als Template-Strings.

Keine Platzhalter, keine TODO-Kommentare. Was du meldest, muss laufen.

### Technischer Rahmen

- **WebGL2**, handgeschrieben. Keine Bibliothek von einem CDN — das wäre eine
  externe Abhängigkeit und widerspricht „eine Datei". Du brauchst ohnehin nur
  Matrizen, einen Shader und Vertex-Buffer.
- **Typed Arrays** für alle Geometrie. Kein Objekt je Vertex.
- Ziel: **60 Bilder/s** auf einem Mittelklasse-Notebook.

### Woher die Stadtdaten kommen

Die vollständigen OSM-Daten Wiesbadens sind 163 MB — nicht einbettbar. Zwei
Wege, baue beide:

1. **Overpass-API zur Laufzeit.** Beim ersten Start eine Abfrage für den
   gewählten Ausschnitt, Ergebnis in `localStorage` oder `IndexedDB` legen.
   Beispielabfrage für das Stadtzentrum:

   ```
   [out:json][timeout:60];
   (way["highway"](50.06,8.20,50.10,8.27););
   (._;>;);
   out body;
   ```

2. **Eingebetteter Rückfall.** Ein stark reduzierter Ausschnitt (nur Straßen der
   Klassen motorway…residential, Koordinaten auf 5 Nachkommastellen gerundet,
   als kompaktes Array-Format statt GeoJSON) direkt in der Datei. Damit läuft
   das Spiel auch ohne Netz. Rechne mit 200–500 KB für die Innenstadt.

**Höhendaten** sind im Browser aufwendig zu beschaffen. Zulässige Entscheidung:
flaches Gelände in der ersten Fassung, und die Straßen liegen alle auf z = 0.
Wenn du Höhe willst, hole SRTM-Kacheln als PNG und lies sie über ein Canvas aus.
Sag klar, welchen Weg du gehst.

### Georeferenz

Wähle einen Ursprung (z. B. lon 8,24 / lat 50,0824) und rechne alles in Meter
um:

```js
const R = 6378137;
const x = (lon - lon0) * Math.PI/180 * R * Math.cos(lat0 * Math.PI/180);
const y = (lat - lat0) * Math.PI/180 * R;
```

Lege **eine** Achsenkonvention fest und schreibe sie an genau einer Stelle auf.
Die Hälfte aller Geometriefehler entsteht daraus, dass zwei Codestellen
verschiedene Annahmen haben.

### Was das Spiel können muss

- **Fahren** mit einem Auto: Gas, Bremse, Lenken, Rückwärtsgang, Handbremse.
  Tastatur **und** Gamepad (`navigator.getGamepads()`).
- **Echtes Straßennetz** aus den Daten: Fahrbahnen mit richtiger Breite je
  Straßenklasse, Kreuzungsflächen, Gehwege, Mittelstreifen.
- **Gebäude** als extrudierte Grundrisse aus `building`-Ways, Höhe aus
  `building:levels` × 3 m, sonst 3 Geschosse.
- **Minikarte** und **Straßenname**, auf der man gerade fährt.
- **Verkehr**: Fahrzeuge, die dem Spurnetz folgen und Abstand halten.

---

## Straßengeometrie — hier liegen die Fallen

Diese Punkte stammen aus einer vollständigen Umsetzung desselben Vorhabens. Jede
Falle hat dort echte Zeit gekostet.

### 1. Die Kreuzung ist die Autorität

Fahrbahn, Gehweg und Bordstein enden an derselben Kreuzung. Lässt du jeden seine
Randpunkte selbst aus Mittellinie und Breite rechnen, sind das drei Wege zum
selben Ergebnis — und drei Gelegenheiten für Abweichungen von wenigen
Zentimetern. Genau diese Fugen sieht man im Spiel.

**Vorgehen:**

1. Kreuzung = Knoten, an dem **drei oder mehr** Ways enden. Zweiarm-Knoten sind
   Fortsetzungen (dort wechselt nur ein Tag) und dürfen **nicht** getrimmt
   werden — sonst zerfällt jede Straße in Stücke.
2. Je Arm zwei Randpunkte festlegen („Tore"), einmal.
3. Kreuzungsumriss = Tore, nach Richtung sortiert, für jeden Arm beide Ecken.
4. Fahrbahnband zwischen den Toren seiner beiden Kreuzungen aufspannen und
   **genau diese Punkte** benutzen.

Nimm **keine konvexe Hülle** für den Umriss: Sie verschluckt die Ecken schmaler
Arme zwischen breiten, und dann hängt dieser Arm an nichts.

Zerlege den Umriss als **Fächer vom Schwerpunkt der Torecken**. Nicht vom
OSM-Knoten — der liegt bei Gabelungen außerhalb, und dann zeigen die Dreiecke in
verschiedene Richtungen.

### 2. Rückschnitt: der harmloseste Fall ist nicht der teuerste

Die Trimmweite ist `halbe Breite des Querarms / |sin(Winkel)|`. Bei einer
**geradeaus durchlaufenden** Straße ist der Winkel zum Gegenarm 180°, der Sinus
also null. Wer die Division mit einem Minimum abfängt, macht daraus den größten
Wert überhaupt.

In der Referenzumsetzung: **884 cm Rückschnitt an jedem Arm jeder der 20.213
Kreuzungen**, die Kreuzungsfläche deckte davon 42 % ab. Der Rest war Loch.

**Richtig:** Gegenarme über 150° brauchen **keinen** Rückschnitt. Spitze Winkel
auf das Doppelte der halben Breite begrenzen. Ergebnis dort: 414 cm, 82 %.

### 3. Umlaufrichtung aus der Geometrie, nie aus einer Annahme

In WebGL entfernt `gl.cullFace` Dreiecke nach der Reihenfolge ihrer Vertices.
Normalen steuern nur die Beleuchtung.

In der Referenzumsetzung waren **alle 20.213 Kreuzungsflächen unsichtbar** —
Dreieckszahl, Höhe, Material und Zuordnung waren dabei durchweg in Ordnung. Der
Umlaufsinn kam aus dem Hüllen-Algorithmus.

**Regel:** Für jedes Dreieck das Kreuzprodukt bilden und die Reihenfolge
notfalls tauschen:

```js
const nz = (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
if (nz < 0) { /* zwei Indizes tauschen */ }
```

Das gilt genauso für OSM-Ringe: Ihr Umlaufsinn ist **nicht** festgelegt. Fest
angenommen wäre die Hälfte aller Gebäude und Plätze rückseitig zugewandt.

### 4. Links und rechts nicht herleiten, sondern messen

Die Normale `(y, -x)` ist eine Drehung um **minus** 90°, `(-y, x)` um plus 90°.
Welche du „links" nennst, ist Geschmack — aber jede Codestelle, die daraus eine
Reihenfolge ableitet, ist eine Fehlerquelle.

In der Referenzumsetzung entstand daraus ein **Stern mit Zacken** statt einer
Kreuzungsfläche, und die Flächenformel meldete durch Vorzeichen-Aufhebung 31
statt 45 m². Das sah nach „etwas kleiner" aus, nicht nach „kaputt".

**Regel:** Ordne Randpunkte über den **kürzesten Abstand** zu und protokolliere
den Versatz. Nahe null bestätigt die Zuordnung; ein Wert in Fahrbahnbreite
verrät vertauschte Seiten sofort.

### 5. Ein Platz ist keine Straße

Ways mit `area=yes` oder geschlossene Ringe mit `highway=pedestrian|footway`
sind **Flächen**. Als Band gebaut ergibt ein Platz einen Pfad um sich selbst
herum, innen leer.

Geschlossene `service`-Ways sind dagegen meist Ringstraßen auf Parkplätzen —
also echte Fahrwege.

### 6. Straßenbreiten

Nimm sie aus der Klasse, nicht geraten:

| `highway` | Fahrbahn |
|---|---|
| motorway | 2 × 3,75 m je Richtung |
| primary | 7,0 m |
| secondary | 6,5 m |
| residential | 5,5 m |
| living_street | 4,5 m |
| service | 3,5 m |

`lanes` und `width` aus den Tags haben Vorrang. Bordstein 12 cm, Gehweg 1,8 m.

---

## Leistung — plane sie von Anfang an ein

Die Referenzumsetzung erreichte **8 Bilder/s**, und die Ursache war strukturell:
**10.325 getrennte Mesh-Abschnitte = 10.325 Zeichenaufrufe.** Das lässt sich
hinterher nicht mehr billig reparieren.

**Für den Browser heißt das:**

- **Ein Buffer je Material, nicht je Straße.** Baue die ganze Stadt in wenige
  große Vertex-Buffer: Fahrbahn, Gehweg, Gebäude, Gelände. Ziel: **unter 50**
  Zeichenaufrufe je Bild.
- **Kacheln für die Sichtbarkeitsprüfung.** Teile die Stadt in Kacheln von
  200–500 m, halte je Kachel einen Indexbereich im gemeinsamen Buffer und
  zeichne nur Kacheln im Sichtfeld. Das braucht keinen eigenen Buffer je Kachel.
- **Baue die Geometrie einmal**, nicht je Bild. Der Aufbau darf zwei Sekunden
  dauern; zeige solange einen Fortschrittsbalken.
- Miss die Bildzeit im Spiel und zeige sie an. Ohne Zahl ist „es ruckelt" keine
  Aussage, mit der man arbeiten kann.

---

## Arbeitsweise

Das ist der Teil, der am meisten Zeit spart.

**Miss, bevor du eine Ursache benennst.** In der Referenzumsetzung waren
nacheinander falsch: fehlende Geometrie, Triangulierung, Auflösung,
Verdeckung. Jede Vermutung war plausibel. Die tatsächliche Ursache — die
Umlaufrichtung — ist keine Eigenschaft der Daten und wurde von keiner
Datenprüfung gefunden.

**Prüfe, was deine Zahl misst.** Diese Messungen sahen wie Aussagen aus und
waren keine:

- Ein Zähler, der **nach** der zählenden Schleife zurückgesetzt wird
- Eine Verdeckungsstatistik, die Böschungen mitzählt — die liegen bauartbedingt
  am Boden und gelten dann alle als verdeckt
- Eine Einstellung abschalten wollen, die an anderer Stelle vorher gesetzt wird
  → beide Vergleichsläufe messen dasselbe

**Bau dir früh diese Werkzeuge in die Seite ein**, hinter Tastenkürzeln:

| Taste | Werkzeug | Beantwortet |
|---|---|---|
| F1 | Bildzeit, Zeichenaufrufe, Dreiecke | wo geht die Zeit hin |
| F2 | Draufsicht auf die ganze Stadt | ist das Netz vollständig |
| F3 | jede Flächenart in einer Leuchtfarbe | welche Fläche sehe ich hier |
| F4 | Drahtgitter | wo überlappt was |
| F5 | Gelände ausblenden | fehlt die Fläche, oder liegt sie darunter |
| F6 | Kamera zu einer Straße per Name | „bei X ist etwas kaputt" nachgehen |

Der Unterschied zwischen **Himmel** und **Gelände** an einer fehlenden Stelle ist
der zwischen „nicht gezeichnet" und „verdeckt" — zwei völlig verschiedene
Ursachen, im normalen Bild nicht zu unterscheiden.

**Schau hin, nicht nur auf Zahlen.** Der verschränkte Kreuzungsumriss meldete
31 statt 45 m². Das Bild zeigte einen Stern mit Zacken.

**Halte die Datei lesbar.** Eine HTML-Datei mit 5.000 Zeilen ist in Ordnung,
wenn sie in benannte Abschnitte gegliedert ist: Daten laden, Geometrie bauen,
Renderer, Fahrphysik, Verkehr, Anzeige. Schreibe zu jeder nicht offensichtlichen
Entscheidung **warum**, nicht was.

---

## Reihenfolge des Vorgehens

1. HTML-Gerüst, WebGL2-Kontext, Kamera, ein Dreieck auf dem Schirm
2. OSM laden (Overpass + eingebetteter Rückfall), Koordinaten umrechnen
3. Straßen als Bänder — noch ohne Kreuzungen, nur um Daten und Achsen zu prüfen
4. **Draufsicht (F2) einbauen** und das Netz gegen eine echte Karte vergleichen
5. Kreuzungen mit Toren, Rückschnitt, Fächer
6. Gehwege und Bordsteine an dieselben Tore
7. Plätze als Flächen
8. Gebäude extrudieren
9. Fahrphysik und Kamera
10. Verkehr auf dem Spurnetz
11. Minikarte, Straßenname, Bildzeit-Anzeige

Nach jedem Schritt **hinschauen und messen**, nicht erst am Ende.
