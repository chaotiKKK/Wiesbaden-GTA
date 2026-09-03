# Erklaerung: Warum der Streaming-Anker noetig ist

Dieses Dokument erklaert den GRUND hinter `AWiesbadenCityChunk::AnchorStreamingBounds`
und `URegionAssetSpawnerComponent::AnchorEmptyInstanceComponents` - warum es sie
ueberhaupt geben muss, welches Problem sie loesen und warum die Loesung subtiler
ist, als sie aussieht. Es ist keine Schritt-Anleitung und keine Referenz, sondern
Hintergrund: wer die Anker-Funktionen liest und sich fragt "wozu dieser Aufwand,
das schiebt doch nur unsichtbare Komponenten herum?", findet hier die Antwort.

## Wie World-Partition entscheidet, was geladen wird

Wiesbaden ist als World-Partition-Karte (WP) gebacken. WP haelt nicht die ganze
Stadt im Speicher, sondern teilt die Welt in ein Gitter aus Zellen und laedt nur
die Zellen, die eine **Streaming-Quelle** (der Spieler) raeumlich erreicht -
Zellen innerhalb eines Radius werden geladen, der Rest entladen. So bleibt nur die
Nachbarschaft resident, und die Stadt streamt beim Fahren mit.

Der springende Punkt: WP ordnet jeden Actor beim Backen einer Zelle zu, und zwar
**nach seinen Bounds** - dem achsenparallelen Kasten (`GetComponentsBoundingBox`),
der alle seine Primitive-Komponenten umschliesst. Ein Actor mit kompakten Bounds
landet sauber in EINER Zelle und wird nur geladen, wenn der Spieler nahe genug
ist. Ein Actor mit riesigen Bounds ueberspannt viele Zellen - WP kann ihn nicht
mehr raeumlich eingrenzen und muss ihn faktisch immer geladen halten.

Damit haengt alles an einer Frage: **Sind die Bounds jedes Chunk-Actors eng?**

## Das Problem: Punkt-Bounds leerer Komponenten am Ursprung

Hier kommt eine unscheinbare UE-5.8-Eigenheit ins Spiel. Eine Komponente OHNE
Inhalt - ein `ProceduralMeshComponent` ohne Sections, ein HISM mit null Instanzen -
hat keine echte Ausdehnung. UE gibt ihr deshalb **Punkt-Bounds an ihrer eigenen
Position** (`SceneComponent.cpp`): ein Kasten der Groesse null an dem einen Punkt,
wo die Komponente sitzt.

Das waere harmlos - wenn die leeren Komponenten nicht alle am **Ursprung** saessen.
Und genau das tun sie, aus zwei Richtungen:

- Die Chunk-Actors werden bei `FTransform::Identity` gespawnt, also am Weltursprung
  (0,0,0); ihre Mesh-Geometrie steckt in WELT-Koordinaten, nicht relativ. Eine
  Zelle irgendwo in Wiesbaden hat also einen Actor, dessen Wurzel am Ursprung
  steht, waehrend die eigentliche Geometrie kilometerweit entfernt liegt.
- Auf einer Zelle ohne Baeume/Baenke sind die serialisierten Varianten-HISMs
  (`Trees_01..06`, `Bushes_01..06`) leer, und ein Gebaeude-Mesh ohne Gebaeude ist
  leer - alle mit Punkt-Bounds an ihrer Komponentenposition, die ohne Zutun der
  Actor-Ursprung ist.

Jetzt rechnet WP die Actor-Bounds: die Vereinigung aus der echten Geometrie
(kilometerweit draussen, beim Zellmittelpunkt) UND den Punkt-Bounds der leeren
Komponenten (am Ursprung). Das Ergebnis ist ein Kasten, der vom Ursprung bis zum
Zellmittelpunkt reicht - in Wiesbaden **Kilometer** gross. Jeder solche Chunk
ueberspannt das halbe Stadtgebiet, WP kann ihn nicht raeumlich trennen, und die
Folge ist genau das gemessene Bild: statt der Nachbarschaft ist praktisch die
ganze Stadt resident, das Laden dauert, und beim Fahren haengt es (mehr Zellen
streamen gleichzeitig ein).

Der Fehler ist tueckisch, weil an der Geometrie NICHTS fehlt - die leeren
Komponenten zeichnen nichts, sie kosten kein sichtbares Dreieck. Sie verschieben
nur unsichtbar eine Zahl, die WP fuer die Zellzuordnung liest.

## Die Loesung: Ankern statt am Ursprung lassen

Der Anker macht genau eine Sache: er sorgt dafuer, dass die Punkt-Bounds einer
leeren Komponente NICHT am Ursprung liegen, sondern beim **Inhalt ihrer Zelle**.

- Leere Komponenten (leeres Mesh, 0-Instanz-HISM) werden per `SetWorldLocation` an
  den **Mittelpunkt des Zell-Inhalts** gesetzt (Anchor). Ihre Punkt-Bounds liegen
  dann dort, wo auch die echte Geometrie ist - die Actor-Bounds bleiben eng, WP
  ordnet den Chunk der richtigen Zelle zu.
- Nicht-leere Komponenten bleiben an der Actor-Position (Ursprung, Identity): ihre
  Vertices tragen bereits Weltkoordinaten, die Komponente gehoert deshalb an die
  Identitaet, sonst wuerde die Geometrie doppelt verschoben rendern.

Der Anchor selbst ist der Schwerpunkt des Zellinhalts: bevorzugt aus der
Mesh-Geometrie (deren Welt-Box), sonst aus den Positionen der Regions-Assets.

## Warum die Loesung subtiler ist, als sie klingt

Das Umsetzen dieser einfachen Idee traegt mehrere nicht-offensichtliche Fallen,
und das erklaert, warum der Code so aussieht, wie er aussieht:

**Package-Schmutz und der Re-Bake.** Die Zellzuordnung ist in die
External-Actor-Packages der Chunks GEBACKEN, nicht zur Laufzeit berechnet. Der Fix
muss also beim Backen (Commandlet, `anchor_chunk_bounds.py`) in die Packages
geschrieben werden. Aber: ein programmatisches `SetWorldLocation` ruft - anders
als ein Gizmo-Zug im Editor - kein `PostEditMove`, markiert das Package also nicht
als schmutzig. Ohne ein ausdrueckliches `Modify(true)` (mit `bAlwaysMarkDirty`,
das auch im Commandlet ohne Undo-Transaktion greift) haette `save_dirty_packages`
NICHTS zu speichern - der Re-Bake liefe scheinbar "erfolgreich" und schriebe doch
nichts. Deshalb steht `Modify(true)` vor den Transform-Aenderungen.

**Klassen-Sweep statt Laufzeit-Arrays.** Man koennte meinen, es reiche, die beim
Bau gemerkten Varianten-Komponenten (`VariedInstances`) umzuhaengen. Tut es nicht:
dieses Array wird nur beim Bauen gefuellt. Auf einer GELADENEN, leeren Zelle
existieren die serialisierten Varianten-HISMs, aber `BeginPlay` ruft bei null
Regionsobjekten `SpawnRegionAssets` gar nicht erst - das Array bleibt leer,
waehrend die Komponenten sehr wohl am Ursprung stehen. Deshalb sweept der Anker
KLASSENWEIT ueber alle HISM-Komponenten des Besitzers (`GetComponents`), nicht
ueber ein Array.

**Der 2C-Fallstrick beim Build-Pfad.** Der Anker muss den Zellmittelpunkt aus der
Mesh-Geometrie per `CalcBounds` lesen - aber mit `FTransform::Identity`, nicht mit
dem aktuellen Komponententransform. Grund: im Build-Pfad laeuft ein erster
Anker-Aufruf (aus `SetRegionAssets`) VOR dem Section-Aufbau und zieht das noch
leere Mesh bereits an den Zellmittelpunkt C. Wuerde der zweite Aufruf danach
`CalcBounds(GetComponentTransform())` nehmen, saehe er C (Komponentenposition) plus
C (die schon in den Vertices steckenden Weltkoordinaten) = 2C und ankerte alle
leeren Komponenten faelschlich an 2C - die Bounds waeren wieder ueber Kilometer
aufgeblaeht, nur verschoben. `CalcBounds(Identity)` zaehlt den transienten
Komponentenversatz nicht doppelt.

**Voellig leere Zellen duerfen den Ursprung behalten.** Hat eine Zelle weder
Mesh-Sections noch Regions-Assets, gibt es keinen Inhalt, an den man ankern
koennte - der Anker kehrt frueh zurueck und laesst die (dann ausschliesslich
leeren) Komponenten am Ursprung. Das ist kein Loch: ein Actor, der NUR Punkt-Bounds
am Ursprung traegt, hat winzige Bounds am Ursprung - die sind eng, WP ordnet ihn
der Ursprungszelle zu. Das Problem entstand nur aus der MISCHUNG von echter
Geometrie (weit draussen) und Ursprungs-Punkten; fehlt die Geometrie, fehlt die
Spanne.

## Verhaeltnis zum Streaming-Radius

Anker und Streaming-Radius sind zwei verschiedene Hebel am selben Ziel und werden
oft verwechselt:

- Der **Anker** macht die Chunks ueberhaupt erst raeumlich TRENNBAR - ohne ihn
  ueberspannt jeder Chunk die Stadt und WP kann gar nicht auswaehlen.
- Der **Streaming-Radius** (siehe `WiesbadenStreamingSource`, hoehenadaptiv)
  bestimmt, WIE VIEL der trennbaren Nachbarschaft die Quelle laedt.

Erst beide zusammen ergeben das gewuenschte Verhalten: enge Bounds (Anker) +
passender Radius = nur die Umgebung resident, hohe Bildrate am Boden, volle Sicht
aus der Luft.

## Warum nicht anders?

Zwei naheliegende Alternativen scheiden aus, und das schaerft das Verstaendnis:

- **Die leeren Komponenten loeschen?** Sie sind serialisierte Varianten, die auf
  GEFUELLTEN Zellen gebraucht werden - man kann sie nicht generell entfernen, nur
  dort, wo sie leer sind, korrekt platzieren. Das tut der Anker.
- **Einfach die Sichtweite reduzieren?** Der Engpass ist nicht das ZEICHNEN,
  sondern die Zahl gleichzeitig RESIDENTER/einstreamender Zellen. Eine A/B-Messung
  (siehe AGENTS.md-Perf-Notiz) zeigte, dass die Instanzenzahl bei grossem vs.
  kleinem Radius fast gleich bleibt, die Bildrate aber von ~6 auf ~76 FPS springt -
  die Kosten haengen an der verwalteten Menge und den Streaming-Hitches, nicht an
  der Sichtweite. Sichtweite zu senken haette am eigentlichen Problem nichts
  geaendert.

## Verwandtes

- `Source/WiesbadenReal/World/WiesbadenCityChunk.cpp` - `AnchorStreamingBounds`.
- `Source/WiesbadenReal/World/RegionAssetSpawnerComponent.cpp` -
  `AnchorEmptyInstanceComponents`.
- `Source/WiesbadenReal/World/WiesbadenStreamingSource.cpp` - der hoehenadaptive
  Streaming-Radius (der andere Hebel).
- `AGENTS.md` - Abschnitt zu World-Partition-Bounds und der A/B-Perf-Messung.
