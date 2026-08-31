# Asset-Quellen & Integration (Verkehrszeichen, Fassaden, Straßenausstattung)

Dieses Dokument bündelt die geprüften, lizenz-kompatiblen Quellen für die
Aufwertung von „Wiesbaden Real“ und beschreibt, wie sie in das Projekt
eingebaut werden.

**Stand:** Die amtlichen Verkehrszeichen-Grafiken sind bereits **heruntergeladen**
und liegen als PNG unter `Content/Textures/TrafficSigns/` (66 Dateien,
`Sign_<VzKat>.png`). Die übrigen Binär-Assets (Fassaden/Dachtexturen,
3D-Straßenausstattung) sind weiterhin **Drop-in**: Sie müssen im Unreal-Editor
importiert werden. Fuer die Putz-Fassade und alle Stadt-Materialien gibt es
inzwischen einen automatisierten Lauf: `Tools/build_materials.py` (siehe
Abschnitt „Materialpipeline" am Ende). Das *Datenmodell*
ist in jedem Fall im Code verankert (`WiesbadenTrafficSignCatalog`,
`WiesbadenRoadMarkings`), der Lookup ist über
`AWiesbadenWorldBuilder::ResolveSignTexture/ResolveSignMaterial` verdrahtet.

## Lizenz-Überblick (kurz)

| Quelle | Lizenz | Kommerzielle Nutzung | Attribution |
|---|---|---|---|
| Amtliche Verkehrszeichen (BASt / StVO-Grafiken) | amtliches Werk (§5 UrhG) | ja | empfohlen (Quelle: BASt) |
| Spass mit Daten (Verkehrszeichen PNG/SVG) | aufbereitete amtliche Zeichen | ja | empfohlen |
| Openfootage Facade Texture Packs | CC BY 3.0 AT | ja | **erforderlich** (Openfootage) |
| Tony Textures (Fassaden) | kostenlos privat+kommerziell | ja | nach AGB prüfen |
| Architextures (Materialien) | kostenlos, Bibliothek-Weiterverteilung eingeschränkt | ja | nach AGB prüfen |
| mtextur (CAD/BIM-Texturen) | kostenloser Download | ja | nach AGB prüfen |
| OSM-Verkehrswende Traffic Sign Tool | OSM-Daten (ODbL) | ja | ODbL-Attribution |
| Sketchfab / Printables (3D-Modelle) | **pro Modell prüfen** (oft CC) | je Modell | je Modell |

> **Faustregel:** Amtliche Verkehrszeichen-Grafiken sind in Deutschland
> öffentliche Werke (kein Urheberrechtsschutz). Bei allen Foto-/Textur-Packs
> und Community-3D-Modellen **vor** der Nutzung die Lizenz der konkreten Datei
> prüfen und Attributionen in eine `ATTRIBUTION.md` aufnehmen.

## 1. Verkehrszeichen (höchste Priorität)

**Quellen:**
- BASt – „Verkehrszeichen und Symbole“: <https://www.bast.de> →
  Verkehrstechnik → V1 (offizielle Grafikpakete, 1:10 / 300 DPI).
- Wikimedia Commons – amtliche StVO-SVGs (Bildtafel, gemeinfrei als amtliche
  Werke): <https://commons.wikimedia.org> – **diese Quelle wurde für den
  Download genutzt** (SVG → transparentes PNG per Thumbnail-Rendering).
- Spass mit Daten – „Alle Verkehrszeichen der BRD als PNG“:
  <https://www.spassmitdaten.de> (SVG/PNG, skalierbar).

**Code-Integration (bereits vorhanden):**
- `GIS/WiesbadenTrafficSignCatalog` – kuratierter VzKat-Katalog +
  `traffic_sign=*`-Parser (`DE:206`, `DE:274-50`, `DE:274[30]`, mehrere via `;`).
- `GIS/WiesbadenRoadMarkings` – Platzierungs-/Markierungs-Maße (VwV-StVO/RMS).

**Asset-Einbau (Verkehrszeichen: ✅ erledigt):**
1. ✅ **Heruntergeladen:** 66 PNGs in `Content/Textures/TrafficSigns/` nach dem
   Namensschema `Sign_<VzKat>.png` - Punkte in der Nummer werden zu
   Bindestrichen (`325.1` → `Sign_325-1.png`, `330.1` → `Sign_330-1.png`),
   Bindestriche bleiben (`274-50` → `Sign_274-50.png`). Enthalten ist die
   komplette Tempolimit-Serie `Sign_274-<x>`/`Sign_278-<x>` (x = 5…130).
2. ✅ **Lookup verdrahtet:** `AWiesbadenWorldBuilder::ResolveSignTexture(SignId)`
   laedt `Sign_<Id>.png` aus `SignTextureFolder` (Default
   `/Game/Textures/TrafficSigns/`); `ResolveSignMaterial(SignId)` erzeugt ein
   Material-Instanz-Dynamic (`SignMaterial` + Parameter `SignTexture`).
3. ◻ **Import im Editor:** PNGs in UE importieren (werden zu `/Game/Textures/
   TrafficSigns/Sign_<VzKat>`) und als `sRGB` (Albedo) belassen.
4. ◻ **Darstellung:** `URoadFurnitureSpawnerComponent` platziert Schilder,
   Leitpfosten und Halt-/Wartelinien als InstancedStaticMesh aus
   `FRoadFurnitureLayout` (Tafel = Engine-Plane, Mast/Pfosten =
   Engine-Zylinder, Reflektor = Engine-Box, Linie = Engine-Plane). Je
   eindeutigem Zeichen teilt sich die Tafel eine Material-Instanz-Dynamic
   (`WiesbadenSignAssets::CreateMaterial` = `ResolveSignMaterial`); den
   Materialvertrag siehe unten.

**Katalog & OSM-Normalisierung:** Der Katalog liegt als JSON unter
`Content/Config/TrafficSignCatalog.json` (Id, Name, Kategorie, `aliases`,
`speedLimit`, optional `noTexture`) und wird zur Laufzeit geladen; die
Kurz-/Alt-Formen stecken im `aliases`-Feld des jeweiligen Eintrags: `DE:325`
→ `325.1`, `DE:330`/`DE:360-50` → `330.1`, `DE:605` → `620-40`, `DE:220` →
`220-20`, `DE:108` → `108-10`, `DE:136` → `136-10`, `DE:138` → `138-10`,
`DE:142` → `142-10`, `DE:241` → `241-30`. Neue Zeichen werden als JSON-Eintrag
ergänzt, ohne Neu-Kompilierung. Fehlt die Datei, greift ein Fallback (nur
274/278).

**Start-Validierung:** Beim Editor-Start (`FWiesbadenRealModule::StartupModule`)
prueft `FWiesbadenTrafficSignCatalog::ValidateTexturesAtStartup` jede
Katalog-Id gegen die Textur-Datei (`Sign_<Id>.png` bzw. `.uasset`) im Ordner
`Content/Textures/TrafficSigns` und warnt fehlende Zeichen in einem
Log-Eintrag. Tempolimit-Basen (`speedLimit`: 274/278) und bewusst grafiklose
Eintraege (`noTexture`: 600) werden uebersprungen. Der dazugehoerige
Automation-Test (`TrafficSigns.TextureValidation`) schlaegt fehl, wenn eine
Textur fehlt.

**Laufzeit-Erweiterung & Hot-Reload:** Der Katalog ist ein mutables Registry.
Blueprints ergaenzen/entfernen Eintraege ueber `UWiesbadenTrafficSignLibrary`
(Add/Remove/Get/Find/Reload); die `ReloadTrafficSignCatalog`-CallInEditor-
Kachel am `WiesbadenWorldBuilder` laedt die JSON neu, ohne den Editor neu zu
starten.

**Nicht geliefert:** `600` (Absperrpfosten) – dafür existiert kein amtliches
Flach-SVG auf Commons (physischer Pfosten); der Eintrag bleibt im Katalog
(mit `noTexture: true`), das Textur-Lookup liefert dafür `nullptr` (Warn-Log)
und die Start-Validierung ueberspringt ihn.

### Schilder-Spawner (Materialvertrag)

`URoadFurnitureSpawnerComponent` rendert jedes Zeichen als Tafel-Instanz mit
einer `UMaterialInstanceDynamic` ueber `WiesbadenSignAssets::CreateMaterial`
(= `ResolveSignMaterial`): Basismaterial `SignMaterial`, Textur unter dem
Parameter `SignTexture`. Tafeln mit gleichem Zeichen teilen sich einen ISM
und damit einen Draw-Call (ein ISM je eindeutigem Zeichen); Masten/Pfosten
und Linien sind je ein weiterer ISM.

Das zugewiesene `SignMaterial` muss:
- einen `TextureParameter` namens `SignTexture` (Default) sampeln,
- den Alpha-Kanal maskieren (die Zeichenform steckt im Alpha der PNGs),
- `Used with Instanced Static Meshes` aktivieren (Tafeln sind Instanzen).

Ohne `SignMaterial` rendern die Tafeln mit dem UE-Default-Material
(weiss, Warn-Log); ohne Textur-Import liefert das Lookup `nullptr` (Warn-Log).
Dasselbe gilt fuer `PoleMaterial`/`DelineatorMaterial`/`ReflectorMaterial`/
`MarkingMaterial` (jeweils Default-Material, falls nicht zugewiesen).

## 2. Straßenausstattung (Schilder-Masten, Leitpfosten, Baken)

**Quellen:**
- Sketchfab – Suche „German Traffic Signs“ (CC-Packs):
  <https://sketchfab.com/search?q=german+traffic+signs>
- Printables / MakerWorld – „Verkehrsschilder“ (STL/OBJ):
  <https://www.printables.com/search?q=verkehrsschilder>

**Integration:** OBJ/FBX in `Content/Meshes/RoadFurniture/` importieren;
Maße/Abstände aus `WiesbadenRoadMarkings` (Leitpfosten 50 m auf Geraden,
Schildhöhe 2,2 m über Gehweg, Seitenabstand 0,5 m).

## 3. Fassaden, Dächer, Oberflächen

**Quellen:**
- Openfootage Facade Texture Packs (CC BY 3.0 AT):
  <https://www.openfootage.net/facades-texture-pack/>
- Tony Textures: <https://www.tonytextures.de>
- Architextures: <https://architextures.org>
- mtextur: <https://www.mtextur.com>

**Integration:** kachelbare Albedo (+ Normal/Roughness, falls vorhanden) nach
`Content/Textures/Facades/` bzw. `Content/Textures/Roofs/` importieren und als
Material-Parameter des `UBuildingGenerator`-Fassadensystems verdrahten
(Slot je Gebäudetyp: Wohnen/Gewerbe/Industrie/Dach).

**Geladen (CC0):** `PutzFassade_Color.jpg` (kachelbare Putz-Fassade
"Plaster005" von ambientCG, 1K) liegt samt `PutzFassade_NormalGL.jpg` und
`PutzFassade_Roughness.jpg` in `Content/Textures/Facades/` — CC0, daher ohne
Namensnennung nutzbar (Quelle siehe `ATTRIBUTION.md`).

**Per-Adress-Override (Mainzer Strasse 129):** Ein konkretes Gebäude bekommt
über `AddressFacadeMaterials` (Schlüssel = Adresse aus `addr:street` +
`addr:housenumber`, z. B. `Mainzer Strasse 129`, Abgleich case-insensitive)
ein eigenes Fassaden-Material, **ohne** die Varianten-Pipeline anzufassen:
Der `BuildingGenerator` vergibt weiterhin nur die Standardvariante (0-5);
passt die Adresse in `BuildingSettings.FacadeOverrideAddresses`, landet das
Gebäude zusätzlich in einem eigenen Mesh-Abschnitt (`FacadeOverrideKey`), und
`ResolveBuildingMaterial` wählt dort das Material direkt aus
`AddressFacadeMaterials[Adresse]`. Die Textur (z. B. `PutzFassade_*`) wird
importiert und im zugewiesenen Material verwendet — ein Schritt, kein
Varianten-Index nötig.

## Rohordner-Struktur

```
Content/
  Config/
    TrafficSignCatalog.json   # ✅ VzKat-Katalog + Aliases (geladen)
    WiesbadenRoadTypes.json   # ✅ Strassentypen RASt 06/RAA (geladen)
  Textures/
    TrafficSigns/      # ✅ 66 amtliche Zeichen als Sign_<VzKat>.png (geladen)
    Facades/           # ✅ PutzFassade_* (Plaster005, CC0) + Normal/Roughness
    Roofs/             # ◻ Dachtexturen (noch offen)
    RoadMarkings/      # ◻ Zebra, Haltlinie, Pfeile (noch offen)
  Meshes/
    RoadFurniture/     # ◻ Schildermasten, Leitpfosten, Baken (noch offen)
```

## Import-Schritte (im Unreal-Editor)

1. Ordnerstruktur anlegen und Dateien per Drag&Drop importieren.
2. Texturen ggf. als `sRGB` (Albedo) bzw. `Linear` (Normal/Roughness) setzen.
3. Materialien je Slot anlegen und im `WiesbadenWorldBuilder`/`BuildingGenerator`
   zuweisen (Straßen, Gebäude, Terrain) bzw. im Sign-Lookup verdrahten.
4. Attributionen aus `ATTRIBUTION.md` in Credits/About anzeigen (CC BY Pflicht).

## Offene Punkte / nicht automatisiert

- ✅ Verkehrszeichen-Grafiken sind **heruntergeladen und eingecheckt**
  (`Content/Textures/TrafficSigns/`, gemeinfreie amtliche Werke). Es fehlt nur
  noch der **UE-Import** (PNG → UTexture2D) beim ersten Editor-Start.
- ◻ Fassaden-, Dach- und Markierungs-Texturen sowie 3D-Straßenausstattung sind
  weiterhin **Drop-in** (Download bewusst NICHT eingecheckt: lizenz-abhängige
  Prüfung pro Datei, kein UE-Import-Lauf hier).
- ◻ Der **Ausstattungs-Spawner** (`URoadFurnitureSpawnerComponent`, ISM +
  Material-Instanz-Dynamic je Zeichen) ist im Code verankert und deckt
  Schilder (Mast + Tafel), Leitpfosten (Pfosten + Reflektor) und
  Halt-/Wartelinien ab; er benoetigt die zugewiesenen Materialien
  (`SignMaterial` etc., siehe Materialvertrag oben) und den UE-Import der
  PNGs.

## Materialpipeline (`Tools/build_materials.py`)

Bis dahin hatte das Projekt **keine** Stadt-Materialien: alle Material-Slots am
`AWiesbadenWorldBuilder` standen auf `nullptr`, und
`UProceduralMeshComponent` rendert ohne Material kommentarlos mit dem
Default-Material (graues Schachbrett). Die fertig gebaute Stadt sah dadurch
"leer" aus, obwohl Geometrie und Streaming einwandfrei waren.

Der Lauf legt alles unter `/Game/Materials/City` an und wird so gestartet:

```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" WiesbadenReal.uproject -run=pythonscript -script="Tools/build_materials.py" -unattended -nosplash
```

| Material | Verwendung | Besonderheit |
|---|---|---|
| `M_WbRoad` | Fahrbahn, Kreuzungsflaeche | Vertexfarbe R = Verschleiss, G = Griffigkeit |
| `M_WbLaneMarking` | Markierung, Zebrastreifen | ISM-Flag; weiss statt Asphalt |
| `M_WbCycleway` | Radweg | roter Belag |
| `M_WbSidewalk`, `M_WbKerb` | Gehweg, Bordstein | Bordstein bewusst abgesetzt |
| `M_WbFacade_*` | 6 Bauweisen | Index = `FBuildingMeshSection::MaterialVariant` |
| `M_WbBuildingRoof` | Dach | weltbezogene UVs |
| `M_WbTerrain` | Landscape | **Landscape-Flag** |
| `M_WbSign`, `M_WbPole`, `M_WbDelineator`, `M_WbReflector` | Ausstattung | **ISM-Flag** |

Die Texturen `PutzFassade_*` werden im selben Lauf importiert (Normalmap auf
`TC_NORMALMAP`, Roughness auf `TC_GRAYSCALE`); die OpenGL-Konvention der
Normalmap wird im Material per Gruen-Flip korrigiert.

### Das Rauschen liegt in einer Textur, nicht im Shader

Siebzehn der Stadt-Materialien brechen ihre Flaechen mit Rauschen auf. Das lief
bis zum 26.08.2026 ueber `MaterialExpressionNoise` mit `levels = 3` - also
dreioktaviges Simplex-Rauschen, ausgewertet fuer JEDEN Bildpunkt. Gemessen:

| Lauf | Bildzeit | Spiel-Strang | Renderer |
|---|---|---|---|
| normale Materialien | 146 ms | 20-26 ms | 142-146 ms |
| Gelaende ausgeblendet | 138-185 ms | 201-223 ms | **7,6-7,9 ms** |
| alle Materialien einfarbig | 70-99 ms | 7,6-10,2 ms | **8,8 ms** |

Der Renderer faellt von 142 ms auf 8,8 ms, sobald die Materialien einfarbig
sind - die Shader waren rund die Haelfte der Bildzeit.

`Tools/make_noise_texture.py` erzeugt dieselbe Optik einmalig als
`Content/Assets/Source/T_WbNoise.png` (512x512, Graustufen, vier Oktaven mit
den Gitterbreiten 4/8/16/32). Die Textur **muss kachelbar** sein, sonst zeigt
jede Wiederholung eine Naht; erreicht wird das ueber ein periodisches
Wertgitter, dessen Zugriff modulo der Gitterbreite laeuft. Das Skript prueft
das selbst nach und meldet die Kantendifferenz gegen die mittlere
Nachbardifferenz.

`build_materials.py` importiert sie als `T_WbNoise` (`TC_GRAYSCALE`, `sRGB`
aus) und baut in `noise()` die Kette

    WorldPosition -> Maske RG -> Fmod(100000) -> Multiply -> TextureSample -> Maske R

Zwei Punkte daran sind nicht offensichtlich:

* **`Fmod` auf einen Kilometer.** Ohne die Faltung erreichen die
  Texturkoordinaten am Stadtrand sechsstellige Werte; 32-Bit-Gleitkomma hat
  dort keine Nachkommastellen mehr uebrig und das Rauschen zerfaellt zu
  Streifen. Sichtbar ist die Faltung nicht, weil die Textur kachelbar ist und
  ein Kilometer bei **jedem** benutzten Massstab ein ganzzahliges Vielfaches
  der Kachelweite ist - das ist eine Bedingung an neue `scale`-Werte:
  `250 * scale` muss ganzzahlig sein.
* **Maske R am Ende.** Ein `TextureSample` gibt als erste Ausgabe RGB aus, die
  Aufrufstellen erwarten aber einen Mischwert, also eine Zahl.

`scale` behaelt seine Richtung (groesser = feiner) und wird ueber
`NOISE_TILE_CM_AT_SCALE_1 = 400.0` in Kachelweiten umgerechnet. Der frueher
uebergebene `levels`-Parameter hat keine Wirkung mehr - die Oktaven stecken in
der Textur - und steht nur noch da, damit die Aufrufstellen unveraendert
bleiben.

`Tools/report_material_nodes.py` prueft ohne laufenden Editor nach, dass kein
`MaterialExpressionNoise` uebrig ist. Warum nicht ueber die Python-API der
Engine: In UE 5.8 ist die Knotenliste eines Materials von Python aus nicht
lesbar (`Property 'Expressions' ... is protected`), und
`MaterialEditingLibrary.get_statistics` liefert im Kommandozeilenlauf durchweg
Nullen, weil dort keine Vorschau-Shader uebersetzt werden. Beides ist geprueft.

**Zwei Fallstricke, die stumm zum Schachbrett fuehren:**

1. **Usage-Flags.** Ohne `bUsedWithLandscape` bzw.
   `bUsedWithInstancedStaticMeshes` kompiliert Unreal das Material fuer den
   jeweiligen Vertex-Factory-Typ nicht und zeichnet das Default-Material -
   obwohl die Zuweisung im Editor korrekt aussieht.
2. **Zuweisung nur beim Build.** Die Chunk-Actors speichern ausschliesslich
   ihre `UProceduralMeshComponent`; `FRoadMeshSection::Channel` ueberlebt das
   Backen nicht. Nach einem Materialwechsel muss die Stadt **neu gebaut**
   werden - `AWiesbadenWorldBuilder::EnsureDefaultMaterials()` fuellt die
   Slots dabei automatisch.

## Spielerfigur (`Tools/Blender/build_sebbo.py`, `Tools/import_sebbo.py`)

Der Spieler zu Fuss ist kein Bausatz aus Grundkoerpern mehr, sondern der
Fotogrammetrie-Scan **„Sebbo mit Kettensaege"**. Die Quelle liegt unter
`Downloads/Sebbo_Kettensaege1.blend` und ist ein reiner OBERKOERPER: an der
Huefte glatt abgeschnitten, darunter offen.

**Zwei Laeufe, keine Handarbeit:**

    blender.exe -b Sebbo_Kettensaege1.blend -P Tools/Blender/build_sebbo.py \
        -- --out Data/Raw/Sebbo

    UnrealEditor.exe WiesbadenReal.uproject \
        -ExecCmds="py Tools/import_sebbo.py, quit" -unattended -nosplash

Der erste Lauf braucht rund vier Minuten - fast alles davon geht auf das
Backen der Verschattung.

**Was `build_sebbo.py` tut:**

| Schritt | Warum |
|---|---|
| Verschweissen (0,1 mm) | Der Scan kommt in 2 853 losen Teilen an, entlang der UV-Naehte aufgetrennt. Erst verschweisst laesst er sich weich schattieren. |
| 20 Krumen loeschen | Fotorauschen, das als Geometrie ausgewertet wurde. |
| 392 164 → 80 000 Dreiecke | Kollabieren; erhaelt die UV-Inseln, und bei einem Fotoscan ist die Textur alles. |
| Huftquerschnitt MESSEN | Fuer jeden der 28 Ringwinkel der aeusserste Punkt des Torsos. Die Beine setzen daran an, statt an einer geratenen Ellipse. |
| Beine und Stiefel bauen | Ringprofil nach den Koerpermassen eines 1,80-m-Mannes: Schritt 0,83 m, Knie 0,48 m, Knoechel 0,07 m ueber dem Boden. |
| Hosenfarbe ABLESEN | Aus dem Scan selbst, am aussenliegenden Hosensaum, sRGB → linear umgerechnet. |
| Farbe, Rauheit, Normale backen | Unreal liest keine Blender-Knoten. Was nicht gebacken wird, kommt drueben grau an. |
| Vierteldrehung auf +X | Blenders X wird Unreals X. Ohne die Drehung laeuft die Figur seitwaerts. |
| Ursprung zwischen die Fuesse | Die Kapsel wird mit ihrem MITTELPUNKT gesetzt. |

**Ergebnis:** 1,777 m, 80 754 Dreiecke, drei Materialschlitze
(`Sebbo_Scan`, `Sebbo_Hose`, `Sebbo_Schuhe`), drei Detailstufen.

Das Grundmaterial ist `M_WbFigur`, **nicht** `M_WbSurface`. Jenes rechnet die
Texturkoordinaten ueber eine Kachelgroesse in Metern um - richtig fuer eine
Hauswand, falsch fuer eine Figur mit fertiger UV-Abwicklung. Mit ihm laege die
Fototextur des Gesichts irgendwo auf der Schulter.

`WiesbadenReal.People.Spielermodell` (Automationstest) prueft Groesse,
Blickrichtung, Ursprung, Materialschlitze und Detailstufen nach - genau die
fuenf Dinge, die beim Bau schiefgegangen sind, ohne dass irgendwo ein Fehler
erschienen waere.
