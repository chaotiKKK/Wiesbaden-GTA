# Ampeln sichtbar & realistisch - Design (T1)

- Datum: 2026-09-16
- Status: Entwurf (im Brainstorming freigegeben; wartet auf Spec-Review)
- Testkarte: Alkis4 (reines Laufzeit-System, unabhaengig vom kaputten Bake)
- Teil von: Strassen-/Stadt-System (T1 Ampeln; T2 Kreuzungen/Zebra; T3 OEPNV-Bus)

## 1. Problem & Kontext
`FWiesbadenTrafficLightSystem` ist datenrein, deterministisch und an die
Verkehrs-Sim gekoppelt (`Initialize` + `SetTrafficLightSystem` + `Tick` -
verifiziert in WiesbadenCitySubsystem.cpp); Autos halten nachweislich bei Rot.
ABER: das System ist UNSICHTBAR (kein Actor/Mesh rendert Masten/Koepfe) und
VEREINFACHT (nur gruen/rot; keine Gelb-Phase, keine Fussgaenger-Signale, keine
Abbiegepfeile, keine Koordination). Tick-Kosten ~0,0 ms/Frame - der Aufwand
liegt allein im Rendern potenziell tausender Signalkoepfe.

## 2. Ziele / Nicht-Ziele
Ziele (Umfang "Voll"):
- Sichtbare deutsche Ampeln je signalisierter Zufahrt.
- Gelb-Phase: Rot -> Rot-Gelb (1 s) -> Gruen -> Gelb (3 s) -> Rot, mit Allrot-Raeumzeit (~2 s).
- Fussgaenger-Ampeln mit eigener Phase.
- Geschuetzte Abbiegepfeile (Links; Rechts optional).
- Gruene Welle entlang benannter Hauptachsen.
Nicht-Ziele:
- Reparatur des leeren Bakes (Alkis10/11) - separater Strang.
- Volle Fussgaenger-Querungslogik / Zebra-Geometrie (T2; hier nur die Nahtstelle).
- Verkehrsabhaengige/angeforderte Steuerung - Festzeit bleibt.

## 3. Ansatz A - datenreiner Kern + instanzierte Sicht-Schicht
Drei Schichten, eine Wahrheit:
- Kern (datenrein): Kern liefert je (Kreuzung, Gruppe, Art) einen Signalbegriff.
- Sicht-Schicht (neu): baut Platzierungen aus Netz+Kern, rendert instanziert,
  treibt die leuchtende Linse per-Instance-Custom-Data.
- Kopplung: Sim und Sicht rufen dieselbe Kern-Funktion -> sichtbar == geschaltet.

## 4. Phasen-/Signalbegriff-Modell (Kern-Erweiterung)
- Neuer Aspekt-Enum je Gruppe: Rot, RotGelb, Gruen, Gelb (+ Pfeil- und Fussgaenger-Varianten).
- Zustandslos aus `ElapsedSeconds + Offset` gerechnet (kein gespeicherter Zustand -> deterministisch/testbar).
- Zwei Achsen-Gruppen wie heute (0/180 vs 90/270), Slots OHNE tote Zeit:
  Zyklus = 2 x (RotGelb + Gruen + Gelb + Allrot).
- "Fahren erlaubt" (Sim) = Aspekt == Gruen. Gelb/RotGelb sind sichtbar, die Sim
  haelt (konservativ, real genug). `IsConnectionGreen` bleibt und delegiert auf Gruen.
- Fussgaenger-Aspekt je Ueberweg: Gruen waehrend die QUERENDE Kfz-Achse Rot ist,
  mit eigener Raeumzeit vor Rot. Datenquelle: `bHasPedestrianCrossing`/Arme.
- Abbiegepfeile (geschuetzt): Verbindungen mit `TurnType::Left` bekommen einen
  Pfeil-Aspekt und ein vorgezogenes Links-Fenster am Anfang der Achsen-Gruenzeit;
  Gegen-Durch bleibt in diesem Fenster rot.
- Gruene Welle: statt Zufalls-Offset werden Ampeln entlang einer benannten
  Hauptachse (`FRoadNetwork::SegmentsByName`) nach Position sortiert; jede
  Folge-Kreuzung startet ihr Achsen-Gruen um Abstand/Auslegungstempo (50 km/h)
  spaeter. Nebenstrassen behalten den deterministischen Zufalls-Offset.
- Default-Timings (tunebar): Cycle 30 s, Gruen ~15 s, Gelb 3 s, RotGelb 1 s,
  Allrot 2 s, Wave 50 km/h.

## 5. Sicht-Schicht
- Geometrie (prozedural, MeshBuilder): Mast ~3 m (`TrafficLightHeightCm`),
  Kfz-Kopf 3 Linsen, Fussgaenger-Kopf 2 Linsen, Pfeil-Kopf (Linkspfeil). Wenige
  statische Varianten, als Instanzen wiederverwendet.
- Platzierung je Zufahrt: Primaersignal RECHTS an der Haltlinie (aus
  `FIntersectionArm`-Gate + Bordstein-Offset), Kopf zeigt `-OutwardDirection`
  (ankommender Verkehr); Hoehe ueber die vorhandene Furniture-Z-Logik
  (Terrain + RoadSurfaceOffset + KerbHeight). Fussgaenger-Koepfe an
  Ueberweg-Armen; Pfeil-Kopf nur wo geschuetzte Links-Verbindung existiert.
  "Frei von der Fahrbahn"-Logik der Strassenausstattung wiederverwenden.
- Rendering: je Mesh-Variante ein HISM. Leuchtende Linse via
  per-Instance-Custom-Data-Float = BITMASKE (welche Linsen leuchten; RotGelb =
  zwei Bits). Je Linse eine ID im Mesh (Vertexfarbe/UV); Material leuchtet die
  Linse mit gesetztem Bit. Pro Frame nur `SetCustomDataValue` je SICHTBAREM Kopf
  (Radius um Kamera/Spieler) - kein Material je Kopf, kein Rebuild.

## 6. Integration & Korrektheit
- Eine Wahrheit: `GetAspect(...)` fuer Sim UND Sicht. "Fahren erlaubt" = Gruen;
  Fussgaenger-Sim = Fussgaenger-Aspekt. Bestehende Kopplung bleibt (kein neues
  Verdrahtungs-Risiko). Sichtbar == geschaltet ist strukturell garantiert.

## 7. Fehler & Randfaelle
- Zyklus geklemmt (existiert). Nicht-ueberlappende Slots => nie zwei
  Konfliktgruppen gleichzeitig frei (auch Gelb). Links-Fenster ueberlappt
  Gegen-Durch-Gruen nicht (Konstruktion + Test). Gruene Welle bei
  Einbahn/Verzweigung -> Rueckfall auf Einzel-Offset (Korridor per Projektion
  auf Achsen-Polyline robust ordnen). Fehlende Ueberweg-/Pfeildaten -> Kopf
  entfaellt (graceful). Keine `TrafficSignals`-Kreuzung -> null Koepfe.

## 8. Tests
Datenrein (erweitert `TrafficLightTest`):
- Aspekt-Sequenz & Dauern ueber einen Zyklus.
- Nie zwei Konfliktgruppen frei (inkl. Gelb-Ueberlappung).
- Links-Pfeil gruen => Gegen-Durch rot.
- Fussgaenger gruen => querende Kfz-Achse rot.
- Gruene Welle: virtuelles Auto bei 50 km/h trifft Folge-Ampeln auf Gruen.
- Platzierung: Kopf auf Randstreifen, korrekte Blickrichtung, einer je Zufahrt.
Sicht-Pruefung: Foto-Lauf auf Alkis4 an einer Ampelkreuzung ueber mehrere
Phasen -> Linsen wechseln rot<->gruen, Autos halten/fahren passend.

## 9. Umsetzungsreihenfolge (je Phase testbar)
1. Aspektmodell + Gelb + Allrot (Kern + Tests) - korrekt, noch unsichtbar.
2. Meshes + Platzierung + instanziertes Rendern + Custom-Data-Treiber -> SICHTBAR (Foto-Test Alkis4).
3. Fussgaenger-Ampeln (+ Ped-Sim-Hook / T2-Nahtstelle).
4. Geschuetzte Abbiegepfeile.
5. Gruene Welle.

## 10. Offene Punkte / Nahtstellen
- T2-Nahtstelle: modelliert die Fussgaenger-Sim Querungen? Wenn nein, hier der Anknuepfpunkt.
- Fern-Repeater/Ausleger vertagt (spaeter als Toggle).
- Exakte Zahl signalisierter Kreuzungen noch messen (Rendering-Budget bestaetigen).

## 11. Erfolgskriterien
- Unit-Tests gruen (Sequenz/Konflikt/Links/Fussgaenger/Welle/Platzierung).
- Foto-Lauf Alkis4: Linsen wechseln ueber den Zyklus, Autos verhalten sich passend, Masten auf dem Randstreifen.
- Perf: Ampel-Tick + Sicht-Update << 1 ms; keine FPS-Regression.