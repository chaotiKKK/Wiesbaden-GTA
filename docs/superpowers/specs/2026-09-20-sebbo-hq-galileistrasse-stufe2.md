# Stufe 2 — Vertikale Erschließung: Treppenhaus + Aufzugsschacht über 15 Stockwerke

**Datum:** 2026-09-20
**Status:** GEBAUT — hohl ausgefuehrt, Test WiesbadenReal.World.SebboHq.VerticalCore gruen
**Vorbedingung:** Stufe 1 (Hülle am Grundstück A, 60 m, 15 Geschosse) ist fertig

## Was Stufe 2 liefert

- Begehbares Treppenhaus, das je Geschoss eine Etage hat und alle 15
  Stockwerke durchzieht.
- Aufzugsschacht mit Öffnungen je Stock und einem Kabinenraum, der später
  (Stufe 7) die Aufzugskabine und -technik aufnimmt. Der Schacht ist jetzt
  schon als Aufzugsschacht definiert, nicht nur als Treppenschacht.
- Dachausstieg über den Schacht (Zugang zum Dach, Vorbereitung für Stufe 3
  Landeplatz).
- Kein Innenausbau (Türen, Licht, Schalter, Fahrstuhl-Antrieb) — das sind
  Stufen 5–7.

## Struktur des Schachts und Treppenhauses

Die Maßstäbe orientieren sich an der bestehenden Hülle und an der 15-Geschoss-
Zahl aus Stufe 1.

| Element | Größe (cm) | Lage im Turm |
|---|---|---|
| Aufzugsschacht | 600 × 500, quer zur Gebäudetiefe, durchziehend | neben dem Tueroffnungsbereich nach innen |
| Öffnung je Stock zum Schacht | 150 × 250 (Türöffnung), angeordnet an der Schachtwand | je Stockwerk auf der Innenseite, im Kabinenzugang |
| Kabinenraum im Schacht | 400 × 400, zwischen den Öffnungen, auf dem Boden der Etage | Schachtinneres, Kabinenhaltung für Stufe 7 |
| Treppenhaus | 500 tief × 300 breit, neben dem Schacht | auf der Innenseite des nach außen weisenden Raums, Zugang zum Schacht |
| Treppe je Geschoss | ca. 250 × 180, 30 cm hoch je Stufe, 15 Stufen je Stufe vorgeschlagen (horizontale Fläche je Etage) | im Treppenhaus, je Geschoss |
| Boden/Decke je Etage | 500 × 600 (Schacht + Treppenhaus ebene Fläche, 400 cm hoch) | je Stock, untere Etage = Boden des Schachts, obere Etage = Dachausgang |
| Dachausstieg | über den Schacht auf dem Dachgeschoss, Zugang zum Dachgelände (diese Stufe nur der Zugang, Landeplatz in Stufe 3) | Dach, 500 × 50 cm Grenzstreifen je Seite (Wandschutz) |

Die Öffnungen je Stock sind so beschaffen, dass der Schacht über alle 15
Stockwerke durchzieht — die Öffnung einer Etage führt zu der nächsten, ohne
Drehung/Bruch; das Muster aus BusInterior ( je Kasten = je Etage) gilt hier.

## Von Stufe 2 bis Stufe 7

Stufe 2 definiert den Schacht und das Treppenhaus als **begehbare Geometrie**:
der Spieler kann frei zu Fuß die volle Höhe erklimmen. Stufe 7 fügt dann das
Antriebssystem hinzu: Kabine fährt, Türen tickern, Etagenwahl. Der Schacht
bleibt dafür in Stufe 2 schon in der richtigen Form vorbereitet — die Öffnung
je Stock und der Kabinenraum sind dort.

## Nachweis

Wie Stufe 1: Bild aus dem laufenden Spiel + Testbatterie grün. Der Test
prüft vor allem, dass der Schacht alle 15 Stockwerke durchzieht, die Öffnungen
je Stock im Schacht stehen, und das Treppenhaus je Geschoss begehbar ist.

## Offen / Blockierend

Keine offenen Punkte mehr — Entscheidungen aus der Rückfrage sind eingebaut.

## Korrektur am 2026-09-20: hohl statt massiv

Die Tabelle oben beschreibt den ersten Entwurf. Gebaut wurde er nicht so, und
zwar aus vier nachgerechneten Gruenden:

1. **Die Darstellung ist rein additiv.** „Oeffnung" und „Kabinenraum" waren als
   solide Kaesten angelegt. Ein Vollkoerper ist kein Raum - im Spiel waere es
   ein Betonklotz gewesen, obwohl Spec und Kommentar „begehbar" sagten. Jetzt
   entsteht der Raum wie beim Bus-Innenraum aus WAENDEN um eine Leere, und die
   Tueroeffnung ist die LUECKE zwischen zwei Wandstuecken.
2. **Der Schacht ragte 3 m aus dem Turm.** `SchachtCenterX = CoreCm +
   SchachtBreite*0,5 + HalbeSchachtBreite` ergibt 1500 cm - genau die
   Fassadenkante bei einem 3000er Grundriss; der 600 cm breite Schacht lief
   damit von 1200 bis 1800. Der Kern sitzt jetzt mittig.
3. **Der Dachausstieg schwebte.** Er sass auf Z 6200 bis 6600 bei einer
   Attika-Oberkante von 6045 - 155 cm Luft darunter. Er sitzt jetzt auf der
   Attika auf.
4. **Zwei konkurrierende Kerne.** Die Huelle baute bereits einen massiven
   900er Kern in der Mitte, der neue Schacht stand daneben. Der Kern hat jetzt
   EINEN Besitzer: BuildVerticalCore. BuildShell kennt nur noch seine
   Oberkante, weil Krone und Mast darauf sitzen.

**Gebaut ist damit:** je Geschoss vier Aussenwaende, eine Mittelwand, zwei
Tueroeffnungen als Luecken mit Sturz, ein Treppenpodest und ein Lauf mit
25-cm-Stufen. Die +Y-Haelfte ist der Aufzugsschacht und bleibt ohne Boeden -
dort faehrt ab Stufe 7 die Kabine. Die -Y-Haelfte ist das begehbare
Treppenhaus.

**Was der Test festhaelt:** nichts ragt aus der Fassade, der Kern steht auf dem
Fusspunkt und reicht ueber die Attika, jedes der 15 Geschosse hat Bauteile,
keine Hoehenluecke groesser als 50 cm, kein Bauteil fuellt den Kernquerschnitt,
je Geschoss genau ein Treppenpodest, der Schacht null Boeden, und keine Stufe
hoeher als 45 cm (darueber kommt Unreals Spielfigur nicht hinauf).

NICHT GEPRUEFT: ob man im Spiel wirklich hinauflaeuft. Das ist ein Lauf mit
dem Nachweis-Werkzeug und steht noch aus.

