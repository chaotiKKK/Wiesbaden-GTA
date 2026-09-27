# SebboTower AAA-Ausbau — Übergabedoku (27.09.2026)

Stand- und Übergabedoku der Ausbaustufen **Dachaufbauten** (Slice 1),
**Aufzug + Treppenhaus** (Slice 2) und **Innenausbau** (Slice 3) des
SebboTower (`AWiesbadenSebboHq` / `SebboHqShape`), plus dem Querschnittsthema
**Quit-Watchdog**. Jeder Abschnitt endet mit seinen Nachweisen (Log-Zeilen,
Test-Pfade, Bild-Dateien). Am Ende stehen die offenen Baustellen und die
Fallstricke für die Weiterarbeit. Ergänzende Regeln und Erkenntnisse stehen in
`AGENTS.md` (Repo-Wurzel) — dort auch die Umgebungs-Besonderheiten dieser
Maschine.

**Zeitraum:** 25.–27.09.2026 · **Karte:** `WiesbadenCity_Alkis31` ·
**Turmfuß:** (-113514, -125729, 10970) cm, Heading 120°, 15 Geschosse, 60 m.

---

## 1. Auf einen Blick

| Baustein | Stand | Beweis |
| --- | --- | --- |
| Slice 1 — Dachaufbauten (Schüssel, Mast, SEBBO-Logo) | fertig | Test `World.SebboHq.Dachaufbauten`, Import-Kontrolle „3 Meshes in cm korrekt" |
| Slice 2 — Aufzug | fertig | „Tower-Aufzug-Probe: Etage 14 erreicht, Türen offen" |
| Slice 2 — Treppenhaus (inkl. Handlauf) | fertig | „WbFigurProbe Treppe: OBEN – alle 57 Wegpunkte" |
| Slice 3 — Innenausbau (Möbel, Türen, Licht) | gebaut, Sichtprüfung offen | 1647 Bauteile (vorher 1062), Test `World.SebboHq.Innenausbau` |
| Querschnitt — Quit-Watchdog (Zombie-Schutz) | fertig, beweist | 2× Live-Kill Exit-Code 44, Unit-Test + Gesamt-Suite grün |
| Sichtprüfung Innenraum/Nacht-Bilder | **offen** | siehe Baustelle 2 |
| „Innenlicht auf Etage"-Nachweis | **offen** | siehe Baustelle 1 |

---

## 2. Slice 1 — Dachaufbauten (Blender → FBX → UE)

**Was:** Parametrische Dachaufbauten in Blender (`Tools/Blender/make_sebbo_dach.py`):
Satellitenschüssel (`SM_WbSeboDachSchuessel`), Antennenmast
(`SM_WbSeboDachMast`) und die Dachreklame mit der SEBBO-Wortmarke
(`SM_WbSeboDachLogo`, aufgemalte/ maskierte Logo-Textur). Export als drei
getrennte FBX + Manifest `Data/Raw/SebboTower/sebbo_dach.json` — je
Materialslot Grundfarbe und ART (Volltonlack oder maskierte Logo-Textur),
damit der Import weiß, welcher Slot was bekommt.

**Kette:** `make_sebbo_dach.py` (baut + exportiert) → `Tools/import_sebbo_dach.cmd`
(`import_sebbo_dach.py`, Editor-Lauf) → `Tools/Blender/check_sebbo_dach.py`
(Kontrolle). Runtime: `SebboHq::BuildDachaufbauten` (SebboHqShape) erzeugt die
Mesh-Posen, der Actor lädt die Meshes über `MeshPfad` aus dem Manifest
(`WiesbadenSebboHq.cpp`, Zeilen 461–464).

**Lage:** Krone des Turms (60 m), Weltkoordinaten aus Fußpunkt + Heading 120°
gerechnet; Posendatei `Saved/Diagnose/poses_sebbo_dach.txt`, Bilder der Serie
als `Saved/Diagnose/WbSeries_*.png`.

**Nachweise:**
- Import-Kontrolle: „3 Meshes in cm korrekt, Textur + Master da, Sentinel gesetzt".
- Test `WiesbadenReal.World.SebboHq.Dachaufbauten` → `Result={Success}`.
- 6 Pose-Bilder gesichtet (Gesamtansicht); die **Zoom-Details wurden nie
  abschließend gesichtet** (Preview-Webview lief damals keine Frames) →
  Baustelle 2.

**Achtung:** Die Asset-Namen im Manifest heißen `SM_WbSebo…` (ein „b") — nicht
mit den `Sebbo…`-Klassen verwechseln.

---

## 3. Slice 2 — Aufzug + Treppenhaus

### Aufzug
- Logik-Kern `Source/WiesbadenReal/World/SebboHqElevator.{h,cpp}`: Phasen,
  gekoppelte Kabinen-/Schachttüren, Etagenwahl.
- Actor `WiesbadenSebboHqElevator.{h,cpp}`: Kabine, Mitfahr-Logik
  (Spieler fährt mit), Ziffern-Etagenwahl, Ruf-Taster, Sonde `-WbLiftProbe`.
- Die Schacht-Öffnung der Kernwände wird von den Aufzugs-Schiebetüren belegt
  (LandingLeft/Right) — eigene Türflügel gehören deshalb nur an die
  **Treppenhaus-Öffnung** (−X-Wand, Y −171…−61).

### Treppenhaus
- `BuildVerticalCore` (SebboHqShape): Podeste, Läufe, Türen, Dach-Austritt.
  Lauf-Mitte ±50 cm Kapselfreiheit; `GetStairWalk` liefert die 57 Wegpunkte der
  Figurprobe.
- **Der gesamte Treppenweg liegt im Kern** (X ±385, Y −314…−115) — der
  Innenausbau außerhalb davon ist konfliktfrei (Vertrag mit der Figurprobe:
  Steckenbleiben zählt als Fehlschlag).
- AAA-Details: schräger **Handlauf** (dafür bekam `FHqPart` eine `Rotation`),
  Kabinen-AAA (Bedienfeld, Licht) — beides bewusst nicht kollidierend.

### Probe-Technik
- Wegpunkt-Tracking der Treppenprobe ist **ecken-schneidend** (Projektion auf
  das Wegstück statt exakter 4-cm-Punkte), Fix in `WiesbadenGameMode.cpp`.
  Vorher: „HAENGT" an der Laufspitze, obwohl die Figur korrekt stieg.
- Wrapper `Tools/run_innen_probe.cmd [Karte] [Logname] [Sekunden]`:
  `-WbGoto` (Zelle streamt, Turm baut), **`-WbZuFuss=20`** (ohne Ausstieg
  startet die Figurprobe nie — der Guard „erst aussteigen" blockiert sonst),
  `-abslog` (die `-stdout`-Umleitung verschluckt Projekt-Logzeilen).

**Nachweise** (`Saved/Logs/wb_innen_probe4_full.log`, 27.09. 01:44–01:56):
```
Sebbo-Hauptsitz gebaut … 1062 Bauteile, 15 Geschosse
-WbZuFuss: nach 20.0 s ausgestiegen.
Tower-Aufzug-Probe: Etage 14 erreicht, Kabine Z=5645 cm, Tueren offen.
WbFigurProbe Treppe: OBEN - alle 57 Wegpunkte, Fuesse 59.44 m ueber dem Start.
```
Bilder: `Saved/Diagnose/figur_treppe_{lauf1,mitte,oben}.png`.

---

## 4. Slice 3 — Innenausbau der 15 Geschosse

**Was:** `SebboHq::BuildInnenausbau` (SebboHqShape) richtet jedes Geschoss ein:
Deckenleuchte, Türflügel an der Treppenhaus-Öffnung (Türblatt + Klinke),
Polstermöbel. **Vertrag: Der Kern bleibt frei** — Treppenweg und Aufzugsschacht
bekommen nichts Kollidierendes; Möbel kollidieren nur außerhalb |x|, |y| <
500 cm.

**Materialien:** neue `EHqMaterial`-Werte mit festen Pfaden in
`WiesbadenSebboHq.cpp` (`MaterialPath`):
`Wood` → `MI_Nb_NbHolz`, `Fabric` → `MI_Nb_NbCreme`, `Lamp` → `M_WbLmWhite`
(plus bestehend Marking/Metal/Concrete/Glass).
**Fallstrick:** `MaterialPath` ist eine Pfadliste mit `default` = weiß — jeder
neue Enum-Wert braucht einen expliziten Fall, sonst weiße Möbel.

**Innenlicht:** drei Punktlichter, die dem **Blickpunkt** folgen (Version 1
folgte dem Spieler → bei Posen saß der im Auto, die Innenräume blieben dunkel).
Zwei Sicht-Funde aus den Nacht-Läufen wurden behoben:
- Lampen-Panel zeigte nachts riesige cyanfarbene Spiegel
  (`M_WbStreetLampGlass` ist ein Glas-/Reflex-Material) → umgestellt auf
  `M_WbLmWhite`.
- 6000 cd + Volumetric-Streuung 2.0 waren für die Ego-Kamera der Figurprobe ein
  weißer Ball → Intensität für Innenräume reduziert.

**Nachweise** (Kombi-Nachtlauf v4, `Saved/Logs/wb_nacht_innen_full.log`,
27.09. 04:35–04:44 — Figurprobe + Aufzug + Nacht-Posen in EINER Engine-Runde):
```
Sebbo-Hauptsitz gebaut bei (-113514, -125729, 10970): 1647 Bauteile, 15 Geschosse, 60 m hoch, Landeplatz auf 60 m.
-WbZuFuss: nach 20.0 s ausgestiegen.
Tower-Aufzug-Probe: Etage 14 erreicht, Kabine Z=5645 cm, Tueren offen.
WbFigurProbe Treppe: OBEN - alle 57 Wegpunkte, Fuesse 59.44 m ueber dem Start.
Messlauf beendet nach 420 Sekunden (-WbQuitAfter).
```
- 1647 Bauteile = 1062 + 585 (Möbel/Türen/Licht) — der Innenausbau ist im
  Build enthalten.
- „OBEN" ist zugleich die **Regression**: Möblierung verengt den Treppenweg nicht.
- Test `WiesbadenReal.World.SebboHq.Innenausbau` → `Result={Success}`.
- Nacht-Posen v2: `Saved/Diagnose/poses_innen_nacht.txt` (Yaws −60/30/240/−30
  korrigiert; Pose 2 landet auf der Fahrbahn), Bilder `WbSeries_000-003`
  (1920×1080).

**Werkzeuge:** `Tools/run_innen_nacht.cmd` (Kombi-Lauf ~7 min),
`Tools/shot_innen_nacht.cmd` (reine Bilder), `Tools/ps_prozesse.ps1`
(Prozess-Lagebild inkl. Kommandozeilen).

---

## 5. Querschnitt — Quit-Watchdog (GPU-Hang → Zombie-Editoren)

**Problem (aus Messläufen belegt):** Zwei Hänge-Ketten ließen Messläufe als
Zombie zurück, die Engine-Run-Lock und die gebaute DLL hielten:
1. **GPU-Stall** mitten im Lauf (Probe3, 26.09.: D3D12
   „GPU timeout: A payload … has not completed", Scene-Pass; Spiel-Strang
   347 s eingefroren, erholte sich dann). Weil die Spielzeit stillstand, wirkte
   der Selbstabbruch „hängend".
2. **Exit-Hang** nach „LogExit: Preparing to exit" (Minuten, DDC-Wartung).

**Fix:** `FWiesbadenQuitWatchdog` (`Core/WiesbadenQuitWatchdog.{h,cpp}`) —
eigener, losgelöster Thread (überlebt eingefrorene Spiel-/Render-Threads),
Poll alle 0,5 s, aktiv **nur** bei `-WbQuitAfter`-Messläufen (interaktive
Editor-Spiele werden nie gekillt):
- Warnung ab 45 s ohne Tick, hartes Ende ab 420 s ohne Tick (Exit-Code 43) —
  bewusst über der beobachteten 347-s-Erholung;
- hartes Ende, wenn 120 s nach Exit-Anfrage kein Ende kam (Exit-Code 44);
- „hart" = `RequestExitWithStatus(true)`: **das Log wird vorher gespült**, die
  Beweiszeilen bleiben erhalten;
- Fristen: `-WbWatchdogWarnSec=` / `-WbWatchdogStallSec=` / `-WbWatchdogExitSec=`.

Verdrahtung in `WiesbadenCitySubsystem.cpp`: `NotifyTick` im
`-WbQuitAfter`-Block, `NotifyExitRequested` an beiden Exit-Pfaden
(`quit`-Kommando und `ScreenshotQuitDelay`).

**Nachweise:**
- Unit-Test `WiesbadenReal.Core.QuitWatchdog` → `Result={Success}` (u. a.
  „Tick-Stopp im Shutdown ist kein Stall", Exit-Frist-Vorrang).
- Gesamt-Suite: `TEST COMPLETE. EXIT CODE: 0`.
- Live ×2 (45-s-Messlauf, Exit-Frist absichtlich 0,5 s):
  `WbWatchdog: Exit haengt seit 0.9 s nach Anfrage - erzwinge Beenden
  (Zombie-Schutz, Exit-Code 44).` + Prozessende `EXITCODE 44`, Log vollständig.

---

## 6. Testlandschaft

Familie `WiesbadenReal.World.SebboHq.*` (13 Stück, alle grün am 27.09.):
`ArrivalLayout`, `Dachaufbauten`, `EineZufahrtsbucht`, `ElevatorTravel`,
`HelipadApproach`, `Innenausbau`, `PlatterAccess`, `PortalDurchgang`,
`PortalSchwelle`, `Schwellenrampe`, `Shell`, `TreppeBegehbar`, `VerticalCore`;
dazu `WiesbadenReal.Core.QuitWatchdog`. Ausführung:
`Tools\run_automation_test.cmd WiesbadenReal.Core.QuitWatchdog <name>`
(Einzeltest) bzw. `… WiesbadenReal <name>` (Gesamtlauf; Ergebnis-Ende im
`Saved/Logs/wb_test_<name>.log`: `TEST COMPLETE. EXIT CODE: 0`).

---

## 7. Reproduktion in 30 Sekunden

| Ziel | Befehl |
| --- | --- |
| Innen-Proben (Aufzug + Treppe) | `Tools\run_innen_probe.cmd WiesbadenCity_Alkis31 innen 720` |
| Kombi Nacht-Innenraum (Probe + Bilder) | `Tools\run_innen_nacht.cmd` |
| Reine Pose-Bilder | `Tools\shot_innen_nacht.cmd` |
| Dach-Assets neu bauen/importieren | `python Tools\Blender\make_sebbo_dach.py` → `Tools\import_sebbo_dach.cmd` |
| Tests | `Tools\run_automation_test.cmd WiesbadenReal core` |
| Prozess-Lage (fremde Läufe?) | `powershell -ExecutionPolicy Bypass -File Tools\ps_prozesse.ps1` |

Beweiszeilen immer im `-abslog`-Log greifen (`Saved/Logs/<name>_full.log`) —
die `-stdout`-Umlenkung verschluckt Projekt-Logzeilen.

---

## 8. Offene Baustellen

1. **„Innenlicht auf Etage" loggt nie.** Die Floor-Erkennung der Innenlichter
   (Blickpunkt → Etage) meldet sich in keinem Lauf (0 Zeilen in v4 und
   nacht_v4), obwohl der Build die Logzeile enthält. Zuletzt von
   `InverseTransformPosition` (Root am Ursprung = Unsinn) auf Weltkoordinaten
   (`BuiltBase + Heading`) umgestellt — der Beweis fehlt. *Nächster Schritt:*
   Ursache in `UpdateInteriorLights` finden, Beweiszeile in einem kurzen Lauf
   provozieren (jede beleuchtete Etage muss einmal geloggt werden).
2. **Sichtprüfung Innenausbau unvollständig.** (a) Dach-Pose-Zoom-Details nie
   abgeschlossen; (b) Nacht-Bilder 1/2 (Glasfassade von außen) waren
   schwarz-schwarz — das Glas ist nachts nahezu opak, die Innenräume damit von
   außen kaum sichtbar; (c) die Intensitäts-Reduktion der Innenlichter wurde
   nicht erneut gesichtet. *Nächster Schritt:* Kombi v5 fahren, Galerie
   (root `make_gallery.py` → `shot_gallery.html`) sichten, ggf. Glas-/
   Innenlicht-Abstimmung nachlegen (z. B. Interieur-Eigenleuchten oder
   Glas-Opazität A/B).
3. **Watchdog-Stall-Zweig (Exit-Code 43) ohne Live-Beweis.** Code 44 ist
   zweimal bewiesen, 43 nur über den Unit-Test. *Nächster Schritt:* optional
   einen Freeze-Schalter (z. B. `-WbWatchdogProbeFreezeSec=`) einbauen und den
   Kill live belegen.
4. **GPU-Stall-Ursache (Scene-Pass) unbekannt.** Der Watchdog zähmt das Symptom
   (Zombie-Editoren), die Ursache ist unklar. *Nächster Schritt:* A/B-Serie mit
   `r.MotionBlurQuality 0` über mehrere Messläufe (der Compute-Queue-Verdächtige
   in den GPU-Breadcrumbs war MotionBlur).
5. **Türflügel sind Deko.** Keine Öffnungs-Logik, keine Kollision am Flügel —
   für den Innenausbau akzeptabel, aber offen, falls Interaktion gewünscht wird.
6. **Uncommitted-Stand** (nichts davon committet — bewusst): gestaged
   `Core/WiesbadenQuitWatchdog.{h,cpp}`, `Tests/WiesbadenQuitWatchdogTest.cpp`;
   modifiziert `World/WiesbadenCitySubsystem.cpp` (Watchdog-Verdrahtung).
   **Fremdes WIP** in denselben Verzeichnissen: `Tools/vor_dem_commit.py`,
   `Tools/test_vor_dem_commit.py` — nicht anfassen. Beim Committen nur eigene
   Hunks (AGENTS.md „Shell quirks").
7. **Meilenstein-Seite pflegen:** `Docs/meilensteine.md`, Eintrag **#10
   „Sebbo-Hauptsitz — Innenräume offen"** ist veraltet (Innenräume stehen,
   Aufzug/Treppe laufen). Aktualisieren + Bilder aus
   `Saved/Diagnose/figur_treppe_*.png` / `WbSeries_*.png` anhängen.

---

## 9. Fallstricke für die Weiterarbeit

- **Kartenname vollständig:** `WiesbadenCity_Alkis31` — `Alkis31` existiert
  nicht („map could not be found", Lauf sofort tot). Vor jedem Lauf
  `Config/DefaultEngine.ini` / `ls Content/Maps/*.umap` prüfen, die
  Map-Landschaft ändert sich schnell.
- **`-WbZuFuss=20` ist Pflicht** für die Figurprobe (Guard „erst aussteigen"),
  sonst läuft der Messlauf ohne Nachweis durch.
- **`-abslog` statt `-stdout`** für Beweiszeilen.
- **Engine-Run-Lock** (`Tools/engine_run_lock.cmd`) respektieren: die
  Push-Wächter-Kette (`waechter4.ps1`) belegt den Lock ~alle 10 min für ~12 min;
  fremde Läufe nie gewaltsam beenden (der Wrapper verweigert sich korrekt).
- **Neue Dateien `git add`-en** (Adaptive-Non-Unity-Build überspringt
  unversionierte Dateien).
- **Neue `EHqMaterial`-Werte** brauchen einen expliziten `MaterialPath`-Fall
  (sonst weiß).
- **Bildvergleiche nur A/B im selben Build** (über Lauf hinweg bis 65 % Pixel
  unterschiedlich).
- Vor einem vorschnellen Taskkill immer `Messlauf beendet` / `Log file closed`
  im Log abwarten — der Watchdog räumt auf, falls die Erholung nie kommt.
