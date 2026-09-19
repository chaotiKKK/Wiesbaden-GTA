# WiesbadenReal - Dokumentation

Diese Sammlung ist nach dem [Diataxis](https://diataxis.fr/)-Rahmen geordnet.
Vier Sorten Dokument, nach dem, was du gerade brauchst:

| Wenn du willst... | Kategorie | Charakter |
| --- | --- | --- |
| Etwas Schritt fuer Schritt LERNEN | **Tutorial** | lernorientiert, an der Hand |
| Ein konkretes Ziel ERREICHEN | **How-to** | aufgabenorientiert, du kannst schon etwas |
| Genau NACHSCHLAGEN | **Reference** | informationsorientiert, trocken, vollstaendig |
| Etwas VERSTEHEN (warum) | **Explanation** | verstaendnisorientiert, Hintergrund |

---

## Tutorials

*Lernorientierte, durchgehende Einfuehrungen fuer den ersten Kontakt.*

- [Erste Fahrt durch Wiesbaden](tutorials/erste-fahrt.md)
  - Vom frischen Checkout an der Hand: bauen, die Stadt starten, den Kaefer
    fahren, aussteigen, zum Helikopter laufen und den ersten Flug ausloesen.

## How-to-Anleitungen

*Rezepte fuer ein konkretes Ziel; setzen Grundvertrautheit voraus.*

- [Den Helikopter-Autopiloten per WbDev-Befehlen testen](how-to/helikopter-autopilot-testen.md)
  - Anflug ausloesen, Laufzeit abwarten, Log auswerten (Bestehen/Durchfall),
    Halten/Abschalten interaktiv, Fehlersuche.
- [Das Fahrverhalten per WbDrive testen](how-to/fahrverhalten-testen.md)
  - Fahrprofil ausloesen, Log auswerten (Tempo/Laengsdynamik, Kursaenderung/
    Lenkung, Gangwechsel), Schwellen, Fehlersuche.

## Reference

*Trockene, vollstaendige Nachschlagewerke - Struktur folgt dem Code, nicht einer
Lernreise.*

- [WbDev-Konsolenbefehle](reference/wbdev-konsolenbefehle.md)
  - Alle 12 `WbDev`-Execs: Signatur, Wirkung, Voraussetzung, woertlicher
    Log-Nachweis; Aufruf- und Log-Konventionen.
- [Stadt-Inhalt in einem frischen Klon holen](reference/stadtinhalt-holen.md)
  - Release-Assets holen, SHA-256 pruefen, entpacken und den gebackenen
    Stadtstand fuer einen frischen Klon verifizieren.

## Explanation

*Hintergrund und Designbegruendung - warum etwas so gebaut ist.*

- [Warum der Streaming-Anker noetig ist](explanation/streaming-anker.md)
  - World-Partition-Bounds, Punkt-Bounds leerer Komponenten am Ursprung, wie das
    Anchoring sie aufloest, die Fallen (Modify/Re-Bake, Klassen-Sweep, 2C), und
    das Verhaeltnis zum Streaming-Radius.
- [Rotor-Physik](explanation/rotor-physik.md)
  - Das reine, deterministische Drehfluegler-Modell: Governor, Lift, Zyklik,
    Heckrotor, Koaxial-Rotoren, Blattspitzenverlust, Autorotation.
- [Flugsound (Helikopter)](explanation/flugsound.md)
  - Prozeduraler Helikopter-Klang aus dem Flugzustand (assetfrei) plus optionale
    Asset-Wiedergabe.
- [Weltinhalte - Beschilderung, Ausstattung, Wetter, Verkehr](explanation/weltinhalte.md)
  - Die GIS-Inhalts-Systeme: Beschilderung, Strassenklassen, Fassaden, Markierung/
    Ausstattung, City-Prompt, Wetter (System + FX), Verkehrs-KI, Stadt-Regionen.

Die Designbegruendung des Fahndungssystems steckt weiterhin in der Spec unten
(`superpowers/specs/`).

---

## Plaene & Spezifikationen (Prozess, ausserhalb Diataxis)

*Arbeitsartefakte des Superpowers-Ablaufs - Design-Entwuerfe und
Implementierungsplaene. Sie beschreiben geplante/laufende Arbeit, nicht das
fertige Produkt, und altern mit ihr; Datum im Dateinamen.*

- [Fahrzeug-Steuernaht vereinheitlichen (IWiesbadenVehicleControl) - Design](superpowers/specs/2026-09-03-fahrzeug-steuernaht-vereinheitlichen.md)
  (Spec, 2026-09-03) - Car und ChaosCar hinter EIN Interface (SetExternalControl),
  damit Harness/Autopilot/Tests/HUD beide identisch fahren. Kein Code, nur Plan.
- [Fahndungs-/Polizei-System - Design](superpowers/specs/2026-09-02-fahndung-polizei-design.md)
  (Spec, 2026-09-02) - GTA-artiges Fahndungssystem: Sternchen-Level 0-5,
  Streifenwagen-Verfolgung, Festnahme. Enthaelt zugleich die Designbegruendung.
- [Fahndungs-/Polizei-System - Implementierungsplan](superpowers/plans/2026-09-02-fahndung-polizei-system.md)
  (Plan, 2026-09-02) - Umsetzung der Spec: datenreiner `FWiesbadenWantedState`,
  `UWiesbadenPoliceSubsystem`, Verfolgung ueber die `SetExternalControl`-Naht.
- [Fahrzeug-/Figur-Testinfrastruktur und Fixes - Implementierungsplan](superpowers/plans/2026-09-01-fahrzeug-figur-testinfra-und-fixes.md)
  (Plan, 2026-09-01) - scriptbare Dev-Execs (`-ExecCmds`), Fahrzeug-Ruhelage-
  Automation-Test, Kaefer-Kollision, Sebbo-Animationen.

---

## Ablage-Konvention

- `docs/tutorials/`, `docs/how-to/`, `docs/reference/`, `docs/explanation/` -
  je Diataxis-Kategorie.
- `docs/superpowers/specs/` und `docs/superpowers/plans/` - datierte
  Prozessartefakte (`YYYY-MM-DD-thema.md`).

Neues Dokument? Nach seinem ZWECK einordnen (obige Tabelle), nicht nach Thema -
und hier verlinken.
