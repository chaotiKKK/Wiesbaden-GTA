# Licht & Stimmung: Die Stadt bei Nacht und über den Tag

- **Datum:** 2026-09-20
- **Status:** Entwurf (Design-Review ausstehend)
- **Pfad:** Teilprojekt 2 von 4 der Initiative „Stadt detailreicher / AAA-Schliff"
  (Teilprojekte: 1 Straßenrand-Schmuck · **2 Licht & Stimmung** · 3 Oberflächen/Materialien · 4 Lebendigkeit)

## Ziel

Die Stadt soll zu jeder Tageszeit aussehen, als sei sie beleuchtet — und nachts
soll man sehen, **woher** das Licht kommt. Heute ist beides nicht der Fall:
nachts hebt ein künstlicher Grundwert die ganze Szene gleichmäßig an, während
72.434 Laternenmasten unbeleuchtet danebenstehen.

Das ist kein Beleuchtungs-, sondern ein Sichtbarkeitsproblem. Diese
Unterscheidung trägt den ganzen Entwurf.

## Bestandsaufnahme (vermessen am 2026-09-20 am Quelltext und an den Bake-Logs)

| Baustein | Zustand | Fundstelle |
|---|---|---|
| Sonne + Himmelslicht | vorhanden, `Movable`, 10 lx | `WiesbadenWorldBuilder::EnsureLightingActors` |
| Tageszeit + Wetter | vollständig: Sonnenstand, Nebel, Wolken, Blitz | `WiesbadenWeatherSystem` / `WiesbadenWeatherFX` |
| Regen/Schnee | Post-Process-Overlay (kein Niagara) | `WeatherOverlay` |
| Fensterlicht | je Zelle, Materialparameter `FensterlichtStaerke`, Takt 2 s, **nach Tageszeit geschaltet** | `WiesbadenCityChunk::ApplyWindowLight` |
| Fahrzeuglichter | echte Spot- + Punktlichter (Scheinwerfer, Standlicht, Bremse, Blinker) | `WiesbadenCarLightsComponent` |
| Ampeln | eigenes Teilprojekt, Signalbilder stehen | `WiesbadenTrafficLights` |
| Laternenmasten | 72.434 Stück, **ein** ISM, ein Draw-Call | `RoadFurnitureSpawnerComponent::SpawnStreetLamps` |
| Laternenlicht | Pool aus 48 Punktlichtern, 26 m Radius, 40.000 cd, ohne Schatten | `CreateLampLightPool` / `UpdateLampLights` |
| Belichtung | Histogramm, hart geklemmt auf 0,6 … 1,6, Bias 0 | `EnsureLightingActors` |
| Farbgradierung | **keine** | — |
| Lumen / GI | **aus**, gemessen ~23 % schneller | `Config/DefaultEngine.ini` |
| Reflexionen | Screen Space (`r.ReflectionMethod=2`) | ebenda |

Der Bestand ist also besser, als „nachts wirkt es komisch" vermuten lässt. Es
fehlen nicht Systeme, sondern vier konkrete Dinge.

## Die Rechnung, die alles entscheidet

**72.434 Laternen. 48 Lichter. Das Verhältnis ist 1 zu 1.509.**

Die Zahl steht im Bake-Log vom 15.09.:
`Strassenlaternen ergaenzt: 69167 zusaetzlich (Sollabstand 30 m), insgesamt 72499`,
danach `Bestand: … 72434 Laternen`. Nur 3.332 davon sind in OSM kartiert; der
Rest entsteht alle 30 m am Straßennetz.

Daraus folgt unmittelbar, was **nicht** geht:

- **Kein Licht je Laterne.** 72.434 Punktlichter sind mit keinem Budget
  darstellbar, und das Bild läuft heute schon bei 31 ms (gemessen,
  Cranachstraße, TAA).
- **Kein Bounce-Light.** Lumen ist aus, und nicht aus Bequemlichkeit:
  Software-Lumen **sieht diese Stadt nicht**, weil die prozeduralen Gebäude
  nicht an der Distanzfeld-Erzeugung teilnehmen. Nur Hardware-Raytracing träfe
  echte Dreiecke — das liegt als `-WbLumenHW` bereit und ist ausdrücklich nicht
  der Default. Wer hier mit indirektem Licht plant, plant an der
  Engine-Einstellung vorbei.

Und daraus folgt, was der Entwurf stattdessen tut: **nicht die Stadt
beleuchten, sondern die Lichtquellen sichtbar machen.** Ein leuchtender
Laternenkopf kostet in einem ISM null zusätzliche Draw-Calls und macht aus
72.434 Masten einen Lichterteppich bis zum Horizont. Ein Punktlicht kostet je
Stück und wirkt 26 m weit. Das erste ist die Stimmung, das zweite der
Nahbereich.

## Vier Befunde, die den Entwurf tragen

### 1. Die Laterne hat keinen Kopf

`SpawnStreetLamps` setzt **einen** skalierten Engine-Zylinder je Laterne — den
Mast. Sonst nichts. Das Punktlicht schwebt bei `LampPostHeightCm` = 700 cm
darüber, ohne dass dort etwas leuchtet. Nachts sieht man einen Lichtkegel auf
dem Asphalt, dessen Quelle unsichtbar ist; aus 50 m Entfernung, wo das
Punktlicht längst nicht mehr reicht, ist die Laterne ein grauer Pfahl.

Das ist die größte Wirkung für den geringsten Aufwand im ganzen Teilprojekt.

### 2. Die 48 Lichter brennen mittags

`UpdateLampLights` ruft `Light->SetVisibility(true)` ohne jede Abfrage der
Tageszeit; es gibt keinen Zweig, der sie wieder ausschaltet. 48 Punktlichter à
40.000 cd liegen also auch um 12 Uhr auf sonnenbeschienener Fahrbahn — Kosten
ohne Nutzen und sichtbare Flecken bei Tag.

### 3. Die Auswahl sortiert 72.434 Einträge alle 0,5 Sekunden

`UpdateLampLights` baut ein Indexfeld über **alle** Standorte und sortiert es
vollständig nach Abstand, um die nächsten 48 zu finden. Der Kommentar daneben
rechnet mit „3.332 Standorte" — das war vor der Synthese richtig; seit dem
30-m-Raster sind es 72.434, also das **22-fache**. Eine volle Sortierung ist
für „finde die nächsten 48" ohnehin die falsche Form; gebraucht wird ein
Ortsgitter, wie es `FCellGrid` in `RoadFurnitureGenerator` bereits gibt.

Zusätzlich fehlt eine Hysterese: an der Grenze zweier gleich weit entfernter
Laternen springt eine Leuchte im Halbsekundentakt hin und her.

### 4. Die Schattenseiten sind unerklärt dunkel

In `DefaultEngine.ini` steht ein ehrlicher offener Punkt: die Vermutung, die
dunklen Fassaden kämen von fehlendem indirektem Licht, wurde **geprüft und
widerlegt** (Vergleich `Stadt00070` gegen `Stadt00071`). Die Putztextur ist mit
gemessen 0,382 linearer Albedo hell. Die Ursache ist offen.

Das gehört hierher, weil es die Stimmung bei Tag bestimmt — und es gehört als
**Messaufgabe** hinein, nicht als Schönheitskorrektur. Am Himmelslicht zu
drehen, bis es gefällt, hat den Befund schon einmal verdeckt.

## Aufbau

Nichts davon braucht einen Re-Bake. Das ist keine Annahme: `LampLocations` ist
ausdrücklich eine gespeicherte `UPROPERTY`, weil genau dieser Fehler schon
einmal auftrat („die Laternenmasten standen sichtbar da und trugen kein
einziges Licht"). Die Standorte überleben das Laden der gebackenen Karte, der
Lichter-Pool entsteht in `BeginPlay`. Ein leuchtender Kopf ist zusätzliche
Geometrie — er kommt als **zweiter ISM zur Laufzeit** aus denselben
`LampLocations`, nicht als Änderung an den gebackenen Instanzen.

```
LampLocations (UPROPERTY, überlebt das Laden)
    |
    +-- LampPostInstances   ISM, 72.434 Masten          (gebacken, unverändert)
    +-- LampHeadInstances   ISM, 72.434 Köpfe           (NEU, zur Laufzeit)
    |        ^ Material mit Parameter Leuchtstaerke 0..1
    |
    +-- LampLights          48 Punktlichter, Nahbereich (vorhanden, zu reparieren)
             ^ Auswahl über FCellGrid statt Vollsortierung

WeatherSystem.TimeOfDayHours
    |
    +--> LampSchedule (datenrein, testbar) -> Leuchtstaerke + Lichter an/aus
    +--> MoodGrading  (datenrein, testbar) -> Farbgradierung + Belichtung
    +--> ApplyWindowLight                  (vorhanden, unverändert)
```

Die beiden neuen Rechenteile sind **datenrein** und ohne Engine prüfbar —
dasselbe Muster wie `StreetFurnitureShapes`, `SebboHqShape` und
`WiesbadenBusInterior`. „Ab wann brennt die Laterne" und „wie warm ist das
Licht um 19 Uhr" sind Funktionen von einer Zahl auf eine Zahl; sie gehören
nicht in einen Actor.

## Stufen

### Stufe 1 — Die Laterne leuchtet (Kopf + Schaltzeit)

Zweiter ISM aus denselben Standorten: ein kleiner Körper auf Masthöhe mit
emissivem Material, ein Draw-Call für die ganze Stadt. `Leuchtstaerke` wird
zentral gesetzt, nicht je Instanz.

Dazu `LampSchedule`: eine Funktion `Stunde -> 0..1` mit weichem Ein- und
Ausblenden um Sonnenauf- und -untergang — nicht hart um 18 Uhr, denn die
Dämmerung ist genau der Moment, für den sich der Aufwand lohnt. Dieselbe
Funktion schaltet die 48 Punktlichter.

*Wirkung:* der Lichterteppich über die ganze Sichtweite, und mittags keine
Lichtflecken mehr.

### Stufe 2 — Der Lichter-Pool wird tragfähig

- Auswahl über `FCellGrid` statt Vollsortierung: nur die Zellen im Umkreis
  absuchen. Die Klasse ist vorhanden und wird bereits von den Möbeln geteilt.
- Hysterese: ein Slot wechselt die Laterne erst, wenn die neue spürbar näher
  ist — sonst flackert es an Gleichstand-Grenzen.
- Das Budget wird eine Einstellung mit Messung daneben, kein Literal. 48 ist
  heute eine Zahl ohne Begründung.

*Wirkung:* messbar weniger Spiel-Strang-Last, kein Springen.

### Stufe 3 — Stimmung über den Tag

- **Farbgradierung** je Tageszeit: kühler Morgen, neutraler Mittag, warmer
  Abend, blaue Nacht. Heute gibt es davon nur `NightBlueBoost` = 0,15 auf der
  Sonnenfarbe — an der Lichtquelle, nicht am Bild.
- **Belichtung**: das Fenster 0,6 … 1,6 ist für den Tag gesetzt. Nachts
  verhindert die Untergrenze, dass es dunkel werden *darf*; zusammen mit
  `NightSunFloor` = 0,03 ist die Nacht doppelt aufgehellt. Das Fenster wird
  tageszeitabhängig.
- **Bloom** maßvoll, damit Laternenköpfe und Fenster strahlen statt nur hell zu
  sein. Das ist der Teil, der aus „beleuchtet" „stimmungsvoll" macht.

*Wirkung:* der eigentliche AAA-Schliff dieses Teilprojekts.

### Stufe 4 — Die dunklen Schattenseiten aufklären

Eine Messung, kein Regler. Reihenfolge: eine Fassade im Spiel mit bekannter
Albedo gegen den `Buffer Visualization`-Wert halten, dann Himmelslicht-Anteil
und Schattenkanal einzeln isolieren. Erst wenn die Ursache benannt ist, wird
etwas geändert.

Diese Stufe darf **ohne Behebung enden** — dann steht der Befund im Spec und
niemand dreht mehr blind am Himmelslicht.

## Nicht-Ziele

- **Kein Lumen, kein Raytracing als Default.** Beides ist vermessen und
  abgelehnt; `-WbLumenHW` bleibt der Vergleichsschalter.
- **Keine schattenwerfenden Laternenlichter.** Die vorhandene Begründung
  („kosten mehr, als die Beleuchtung optisch einbringt") bleibt gültig.
- **Keine Innenraumbeleuchtung.** Fenster leuchten als Material, nicht als
  Lichtquelle — das ist bewusst so und bleibt.
- **Kein Umbau des Wettersystems.** Es ist vollständig; dieses Teilprojekt
  liest seine Tageszeit und mehr nicht.
- **Keine neuen Laternen-Standorte.** Das 30-m-Raster ist eine Entscheidung von
  Teilprojekt 1.

## Tests

Datenrein, ohne Engine:

- `LampSchedule`: aus um 12 Uhr, an um 23 Uhr, monoton in der Dämmerung, keine
  Sprungstelle, identisch für Punktlichter und Köpfe.
- `MoodGrading`: stetig über 24 h, kein Werteüberlauf, 0:00 und 24:00 gleich
  (Rundlauf).
- Auswahl über das Gitter: liefert **dieselben** 48 Laternen wie die
  Vollsortierung. Die heutige Fassung bleibt als Referenz im Test stehen, nicht
  im Spiel.
- Hysterese: bei Gleichstand kein Wechsel, bei klarer Annäherung Wechsel.

Im Spiel, mit dem vorhandenen Werkzeug: `Tools/moebel_nachweis.py` wählt
bereits einen Ort nach Objektart, setzt die Kamera auf Augenhöhe und legt Bild
plus Kennzahlen ab. Es bekommt einen Tageszeit-Parameter und wird damit zum
Abnahmewerkzeug — dieselbe Stelle um 12 und um 23 Uhr.

## Abnahme

1. Ein Nachtbild, auf dem die Laternenköpfe **bis zum Horizont** leuchten, und
   dasselbe Bild um 12 Uhr ohne einen einzigen Lichtfleck.
2. Die Zahlen aus dem Log: wie viele Köpfe, wie viele Lichter, wie weit die
   nächste Laterne — gegen die 72.434 gehalten.
3. Bildzeit vor und nach jeder Stufe, gleiche Strecke. Stufe 2 muss **schneller**
   sein als der Bestand, nicht nur gleich schnell.
4. Build grün, volle Batterie grün.
5. Für Stufe 4 genügt ein benannter Befund — auch „Ursache ist X, Behebung
   gehört in Teilprojekt 3" ist ein Ergebnis.

## Offene Punkte für die Planung

- **Leuchtet der Kopf über einen Materialparameter oder braucht es ein neues
  Master-Material?** Beides ist ein ISM; die Frage ist, ob die Stadt-Materialien
  einen freien Parameter haben.
- **Reicht ein Kopf, oder braucht es einen sichtbaren Lichtkegel?** Ohne GI
  trägt nichts das Licht vom Kopf zum Boden außer dem Punktlicht (26 m). Für den
  Fernbereich wäre ein zweites, sehr billiges Mittel denkbar — das ist eine
  Design-Entscheidung, keine technische.
- **Wieviel Budget hat die Farbgradierung?** Ein unbeschränktes
  Post-Process-Volume mit Gradierung ist nicht gratis, und 31 ms sind knapp.
- **Ist `MaxActiveLampLights` = 48 gemessen oder geraten?** Im Quelltext steht
  keine Messung daneben. Stufe 2 sollte sie nachliefern.
