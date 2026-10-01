# Stadt-Audiomischung: Wiesbaden soll klingen wie Wiesbaden

- **Datum:** 2026-09-27
- **Status:** Entwurf (Design-Review ausstehend)
- **Umfang:** 4 Teile, davon 3 ohne Karten-Nebake; Teil 3b ist ein eigener,
  freizugebender Abschnitt mit Bake-Gate
- **Karte:** `WiesbadenCity_Alkis31` (Live-Karte laut `Config/DefaultEngine.ini`)

## Ziel

Die Mischinfrastruktur ist fertig und geprueft. Was fehlt, ist der Inhalt: eine
ganze Stadt aus 14 Klang-Clips, und ein „Ambiente", das aus gefiltertem Rauschen
besteht. Jede Strasse in Wiesbaden klingt gleich tot.

Die vier Teile in Reihenfolge:

| Teil | Inhalt | Klangquelle | Nebake |
| --- | --- | --- | --- |
| 1 | Fussschritte nach Untergrund (Spieler + Passanten) | 2 Aufnahmen + 2 synthetisch | nein |
| 2 | Stadtgeraeusch und Stimmen | synthetisch, Aufnahmen loesen sie ab | nein |
| 3a | Zonenauswahl statt einheitlichem Ring | — | nein |
| 3b | Strassenklasse je Section, Quellen an echten Punkten | — | **ja, ~2 h** |

## Bestandsaufnahme (vermessen am 2026-09-27 am Quelltext)

| Baustein | Zustand | Fundstelle |
| --- | --- | --- |
| SoundClass-Busse | 7 Busse, SoundMix `SM_WbMaster` | `WiesbadenAudioSubsystem` |
| Hall | Submix `SBX_Reverb` + Preset `SFXP_Reverb`, 4 Raumklassen | `WiesbadenAudioPropagation` |
| Distanz | 3 Attenuationsklassen `ATT_Near/Mid/Far` mit Occlusion + Fern-Tiefpass | ebenda |
| Raumsonde | 3 Strahlen, 0,5 s Takt, Halle/Kirche/Tunnel erkannt | `WiesbadenAmbienceSubsystem::ProbeSpaceAndTime` |
| Tag/Nacht-Mischung | `DayBedGain` / `NightBedGain`, weich geblendet | `WiesbadenAudioPropagation` |
| Doppler | c/(c-v), auf [0,7; 1,4] begrenzt | ebenda |
| Motor, Hubschrauber | MetaSound `MS_EngineBoxer` + `S_*_Ka52`, 5 Lagen gemischt | `WiesbadenEngineAudio` |
| Waffen, Gore, Tueren | 14 Clips aus BigSoundBank | `/Game/Audio/Samples` |
| Ambience-Betten | 5 Stueck | `WiesbadenAmbienceSubsystem` |
| Fussschritte | **keine** — weder Spieler noch Passant | — |
| Strassenlaerm an Strassen | **keiner** (Ring um den Hoerer) | ebenda |
| Zonen-/Ortsbezug | **keiner** | ebenda |

## Der eine Befund, der alles entscheidet

**Die fuenf Ambience-Betten sind kein Klang, sondern ein Filter.**
`WbAudioAssetsCommandlet::BuildBed` verdrahtet `Noise` in einen
`One-Pole Low/High Pass Filter` und das Ergebnis auf `AudioOut`:

| Bett | Filter | Grenzfrequenz |
| --- | --- | --- |
| `MS_AmbWind` | Low Pass | 320 Hz |
| `MS_AmbCity` | Low Pass | 140 Hz |
| `MS_AmbRoom` | Low Pass | 90 Hz |
| `MS_AmbBirds` | High Pass | 1.800 Hz |
| `MS_AmbNight` | High Pass | 3.800 Hz |

Der Name sagt Ort, der Inhalt sagt „Rauschen mit Huebschband". Eine Innenstadt,
ein Wald und der Nordrand bekommen dieselbe Kurve, nur anders hell. Das ist
der Grund, warum die Stadt tot klingt — nicht die Mischung.

Der zweite Befund betrifft die Datenlage und entscheidet die Reihenfolge:

**Auf der gebackenen Karte gibt es keine `FWiesbadenCityData`.**
`UWiesbadenGameInstance::HasCityData()` ist auf `WiesbadenCity_Alkis31` false,
weil der Laufzeit-Build nur im Editor/PIE laeuft. Wer Ort → Klang uebersetzen
will, kann sich also nicht auf den Strassen- und Regionsgraphen der Pipeline
verlassen. Was aber **in** der Karte steht, ist `AWiesbadenCityChunk`:

- `RoadSectionChannels` (`TArray<uint8>`, serialisiert) — der `ERoadMeshChannel`
  je Section: Fahrbahn, Gehweg, Bordstein, Zebrastreifen, Boeschung …
- der Materialname der Section — daraus laesst sich die **Oberflaeche** lesen
- `RegionAssets` — Category (Tree/Waterfront/Industrial) + `RegionName`

Es fehlt genau eine Sache: `EOSMHighwayType`. Die Sektionen sind nach Kanal
**und Oberflaeche** gruppiert (`FRoadMeshSection::Channel` + `Surface`), und
`ResolveRoadMaterial(Channel, Surface)` kennt die Strassenklasse nicht — sie
geht in der Gruppierung verloren. Ohne sie kann die Strasse nicht lauter sein als
die Wohnstrasse.

Daraus folgt die Aufgabenstellung der vier Teile: **Teil 1 bis 3a kommen ohne
Nebake aus**, weil sie Material, Kanal und Region aus der gebackenen Karte lesen.
Erst 3b braucht das neue Feld.

## Synthese und Aufnahme als Stufenpaar

Auf der Platte liegen 22 BigSoundBank-Dateien in `C:\Users\HP\Downloads` — und
**alle 22 sind Fahrzeug, Waffe oder Gore**. Kein Fussschritt, kein Strassenlaerm,
keine Stimme. Der vorhandene Bestand kann Teil 1 also nicht bedienen.

Deshalb das Stufenpaar: Jede Klangquelle, die es als Aufnahme gibt, wird zuerst
**synthetisch** gebaut (MetaSound-Graph, wie der Boxer) und geht sofort. Eine
Aufnahme ersetzt spaeter die synthetische Lage derselben Quelle. Das Spiel ist nie
stumm, und jede spaetere Aufnahme ist eine Verbesserung an genau einer Stelle.

Synthetisch heisst hier konkret: ein Rauschimpuls durch einen Bandpass, dessen
Frequenz das Material bestimmt (Asphalt hell und kurz, Wiese dumpf und weich),
darueber ein kurzer Hüllkurven-Impuls. Die Materialunterscheidung ist damit
parametrisch und im Code pruefbar, statt in einer Datei vergraben.

## Architektur

Eine neue Schicht, drei Konsumenten:

```
AWiesbadenCityChunk  ──►  UWiesbadenAudioZonesSubsystem  ──►  Klang
  (serialisiert:          (UTickableWorldSubsystem)
   Section-Kanal,           ·  ZONE des Hoerers
   Section-Material,        ·  Strassenklasse -> Grundpegel
   RegionAssets,            ·  Attenuation je Zone
   NEU 3b: Section-Klasse)  ·  Bett-Auswahl, Quellen setzen
                                   ▲
             UWiesbadenAmbienceSubsystem ─┘   (liest die Zone, statt zu raten)
                                   ▲
             AWiesbadenFootPawn / Passanten-Schrittpool
                                   ▲
             WiesbadenAudioZones   (reine Mathematik, headless testbar)
```

Drei Regeln, die den Bau bestimmen:

**Reine Mathematik getrennt von der Engine-Kopplung.** `WiesbadenAudioZones`
ist ein Namespace mit statischen Funktionen, ohne jeden UObject-Zugriff — genau
nach dem Muster von `WiesbadenAudioPropagation`. Er ist damit headless testbar,
was bei 3 von 4 Testfaellen den Unterschied zwischen einem Test und einem
Hören macht.

**Ein Ort, eine Wahrheit.** Nichts ausser `UWiesbadenAudioZonesSubsystem` liest
den Chunk. Der Fuss-Pawn, die Passanten und der Ambience-Subsystem fragen nur
dieses eine Subsystem. Wenn die Zonentabelle sich aendert, aendert sie genau
einmal etwas.

**Fehlende Assets bleiben still.** Alle drei Teile folgen der bestehenden
Konvention: kein Bett, kein Import, keine Karte → das Subsystem loggt einmal
und rechnet weiter. Genau das haelt die vorhandenen Subsysteme robust, und es
kostet nichts, es einzuhalten.

## Teil 1 — Fussschritte

### Das Muster, das Teil 1 aufbricht

Die Spielerfigur ist ein `AActor` (`AWiesbadenFootPawn`) und kann eine
`UAudioComponent` bekommen. **Die Passanten nicht.** Sie sind keine Actors,
sondern ISM-Instanzen aus `FPlacedPedestrian` — der Spawner fuettert sie in
`UPedestrianSpawnerComponent::UpdateInstances`, und `WiesbadenCityActor` liest
`CollectPlaced()` aus der Simulation. Ein `UAudioComponent` je Passant gibt es
nicht und kann es nicht geben.

Dafuer gibt es die vorhandene Schrittphase. `FPlacedPedestrian::StridePhase`
laeuft 0..1 und steuert die Auf- und Abbewegung — sie ist exakt das, was ein
Schritt-Zyklus fuer Klang braucht, und sie ist bereits da. Der Passanten-Schritt
ist deshalb ein **Pool** von wenigen `UAudioComponent` am Hoerer: die Simulation
meldet je Bild, welche Passanten ihre Phase gerade ueberschritten haben, der Pool
legt genau dort einen Schritt ab.

Begrenzung, die die Regel setzt: ein Schritt hoechstens alle 8 m je Passant und
hoechstens 6 gleichzeitige Schritte. Eine belebte Innenstadt darf nicht wie
Feuerwerk klingen.

### Untergrund aufloesen — ohne Rebake

`FWbFootstepSurface` (Asphalt, Pflaster, Wiese, Innenraum) und
`SurfaceFromMaterialName()`. Das ist dasselbe Muster wie das bestehende
`AWiesbadenCityChunk::BuildingUseFromMaterialName`, das die Nutzungsart
(`EWbBuildingUse`) aus dem Fassadenmaterial-Namen liest — datenrein, statisch,
testbar, und es funktioniert auf der gebackenen Karte, weil der Materialname
serialisiert ist. Falscher Name → `Pflaster` als Default, kein Fehler.

Bodenabfrage: Strahl nach unten vom Fusspunkt, wie `FollowGround` ihn bereits
tut. Kein +200 cm Versatz wie bei der Bodenabfrage der Figur (`FootPawn`-Lektion
aus AGENTS.md) — der Schritt soll am Fuss klingen, nicht auf Kniehoehe.

### Nachweis

- Test `WiesbadenReal.Audio.Footsteps`: Materialzuordnung je Materialname,
  leere und unbekannte Namen, `MAX`-Enum
- Test `WiesbadenReal.Audio.FootstepPool`: Pool-Grenzen, Mindestabstand,
  Zaehlung der abgelegten Schritte
- Laufzeile je Schritt: Material, Abstand zum Boden, Poolgroesse — nach
  `Saved/Logs/`, nicht stdout
- Bildbeleg: A/B im selben Build mit `-WbNoFootsteps`

## Teil 2 — Stadtgeraeusch und Stimmen

### Die Betten werden neu gebaut

Die fuenf Rausch-Betten werden nicht weggeworfen, sondern **gestuft aufgebaut**:
`MS_AmbCity` z. B. bekommt drei Lagen statt einer — Grundrauschen (heute), ein
gefiltertes mittleres Band fuer Strassenverkehr, ein schmales hohes Band fuer
einzelne Fahrzeuge. Die Filterfrequenz jeder Lage kommt aus der Zonentabelle
(Teil 3a) statt aus einer Konstanten im Commandlet.

Jede Lage ist einzeln abschaltbar. Eine synthetische Lage, die taub klingt, wird
abgeschaltet, ohne dass die anderen mitfallen.

### Regeln, die Teil 2 aufstellt

**Kein Ortswissen im Klang.** Stimmen, Glocken, Anzeiger und Bahnhof gehoeren an
Orte mit Bedeutung. Die Orte liefert der Zonen-Teil, nicht der Audio-Code.

**Aufnahmen loesen Synthese ab, Lage fuer Lage.** Eine neue Aufnahme ersetzt die
synthetische Lage mit derselben Rolle. Der Austausch ist eine Zeile in der
Lagenliste, kein Umbau.

**Pegel sind gemessen, nicht geschaetzt.** Die Belegpflicht des Projekts gilt
auch hier: der Boxer-Mix wurde am 24.09. um 13 dB korrigiert, weil die
Rauschlagen 25–31 dB unter dem Zuendpuls lagen. Die Lagen werden vor der
Abnahme ueber `Tools/render_engineboxer.py` gerendert und gehoert.

## Teil 3a — Zonenauswahl

Heute liegen die Betten in einem Ring von 18 m um den Hoerer und werden bei
3.500 cm Bewegung neu gesaet (`LocalBedRadiusCm`, `ReseedDistanceCm`). Jede
Position klingt gleich.

Neu: `EWbAudioZone` mit `Quiet` (Wald, Nordrand), `Residential`,
`Commercial` (Innenstadt, Kurpark), `Industrial`. Jede Zone hat einen Satz Betten,
eine Grundlautstaerke und eine Strassen-Grundlautstaerke. Die Zonenbestimmung
liest die geladenen Chunks:

- `RegionAssets` mit Category `Tree` → `Quiet` (eine Strasse durch Wald)
- Kategorie `Industrial` → `Industrial`
- Sonst nach Strassenlage und Bebauungsdichte → `Residential` / `Commercial`

Der Ring bleibt als **Fallback**, wenn keine Chunks geladen sind. Damit bleibt
der Lauf ohne Bake am Ton — im Editor/PIE mit aktiver Zonen-Bestimmung, auf der
gebackenen Karte mit RegionAssets, und im Extremfall mit dem Ring.

Nachweis: gemessener Zonenwechsel an drei festen Punkten (Nordrand,
Innenstadt, Kurpark) mit protokollierten Weltkoordinaten. Kein „klingt besser",
sondern „Zone wechselte hier".

## Teil 3b — Ortsbezug (eigenes Freigabe-Gate)

Erst wenn 1, 2 und 3a stehen und gemessen ist, welche Strassenklasse der Klang
tatsaechlich braucht, entscheidet sich, ob 3b den Bake wert ist.

**Neues Feld an `AWiesbadenCityChunk`:**

```
UPROPERTY()                        // OHNE Transient - sonst ueberlebt
TArray<uint8> RoadSectionClasses; //   der Bake den Bau nicht
```

`ERoadMeshChannel`-Wert je Section, parallel zu `RoadSectionChannels`. Beide
Felder zusammen ergeben die Strassenklasse. Anwendungsregel aus den Learning-
Einträgen wortgetreu: in `PostRegisterAllComponents` und `BeginPlay` anwenden,
niemals in `OnRegister` — das ist ein `USceneComponent`-Haken und existiert auf
dem Actor nicht.

**Kosten:** `anchor_bounds.cmd` laeuft ueber ~2.060 Chunks und rund 2 Stunden,
und es nimmt den Engine-Lock. Der Bake ist deshalb ein eigener, gebuchter
Schritt, kein Nebeneffekt einer C++-Aenderung.

**Abbruchbedingung:** Wenn 3a zeigt, dass der Unterschied zwischen einer
Wohnstrasse und einer Landstrasse hoerbar nichts beiträgt, wird 3b verworfen und
stattdessen `RoadSectionClasses` als reine Diagnose behalten (kostet dann immer
noch den Bake — deshalb diese Entscheidung erst nach den Messungen aus 3a).

## Fehlerbehandlung

| Fall | Verhalten |
| --- | --- |
| Materialname unbekannt oder leer | `Pflaster` als Default, Debug-Log, kein Fehler |
| Kein Chunks geladen (PIE, Lauf ohne Bake) | Ring-Fallback aus 3a |
| MetaSound-Bett fehlt (Import nie gelaufen) | still, `bWarnedMissingBeds` einmal |
| Kein Sendepegel vom Hoerer vorhanden | keine Schritte, kein Fehler |
| Passanten-Schritt ohne Bodenabfrage | kein Schritt, Zaehler meldet 0 |
| Abschnitt ohne Klasse (3b, altes Feld leer) | `Residential` als Default |

## Teststrategie

Die Discipline des Projekts: **Sabotage-Gegenproben MUESSEN anschlagen.** In
dieser Session hat eine Sabotageprobe zunächst gruen geblieben, weil der
RCodezweig sie nie erreichte. Deshalb: jede neue Funktion wird mit kaputtem Input
geprueft — leerer Name, unbekannter Kanal, `MAX`-Enum, leeres Array — und der
Test schlaegt an, wenn der Code nicht sauber faellt.

| Ebene | Werkzeug | Beispiele |
| --- | --- | --- |
| Reine Mathematik | `IMPLEMENT_SIMPLE_AUTOMATION_TEST`, headless | Material, Zonentabelle, Strassenklasse → Pegel, Pool-Grenzen |
| Bauteil | Automation-Test mit Test-World | Zonenwechsel, Pool-Auffuellen |
| Karte | Log aus `Saved/Logs/`, Probe-Flag | Schrittzeilen, Zonenwechsel, Poolgroesse |
| Bild | A/B im selben Build | `-WbNoFootsteps` gegen Default |

Bildvergleiche ueber **verschiedene** Laeufe sind nach AGENTS.md wertlos (bis
65 % Pixel Unterschied durch Verkehr, Wetter, fremde Threads) — deshalb immer
der Schalter im selben Build.

## Reihenfolge und Aufwand

| Teil | Rebake | Aufwand | Ergebnis |
| --- | --- | --- | --- |
| 1 Fussschritte | nein | 1 Tag | Spieler und Passanten treffen den Boden richtig |
| 2 Stadtgeraeusch | nein | 1–2 Tage | Innenstadt, Wald und Nacht unterscheiden sich |
| 3a Zonen | nein | 1 Tag | Der Ort bestimmt den Klang, nicht der Ring |
| 3b Ortsbezug | ja, ~2 h | 2 Tage | Quellen stehen an echten Strassen |

Die Reihenfolge ist Absicht: 1, 2 und 3a kosten **keinen** Bake. Wenn 3b am Ende
verworfen wird, ist kein Baucycle verloren — nur die zwei Stunden, die dann nie
ausgegeben wurden.

## Was dieser Entwurf nicht loest

- **Sprachausgabe.** Eine sprechende Figur braechte Lippen-Sync auf einem
  statischen Mesh, das die Passanten heute nicht sind. Eigenes Thema.
- **Musik.** `SC_Music` ist ein Bus mit Regler, aber kein Inhalt. Eigenes Thema.
- **Fahrzeug-Interaktionsklang im Detail.** Motoren sind gebaut; Tuerauftritt,
  Motorhebel, Kupplung sind es nicht.
- **Reverb pro Gebaeude.** Die Raumsonde erkennt vier Klassen, waehlt aber nicht
  das Gebaeude. Innenraumhall je Raum wuerste eigene Sonden.
