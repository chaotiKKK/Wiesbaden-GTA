# Blender-Werkzeuge (Asset-Pipeline)

Skripte für den Weg vom Rohmodell zum einsatzfähigen Unreal-Asset. Alle laufen
im Hintergrundmodus:

```bash
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --python <skript>.py -- <argumente>
```

## Reihenfolge

| Skript | Zweck |
|---|---|
| `dae_import.py` | COLLADA (`.dae`) → `.blend`. Blender 5.2 hat den COLLADA-Importer entfernt, UE5 liest `.dae` nicht — daher ein eigener Parser. |
| `find_wheels.py` | Sucht radförmige Teile über Loose Parts. Diagnose, verändert nichts. |
| `split_wheels.py` | Trennt Räder von der Karosserie, exportiert beide FBX + `wheel_positions.json`. |
| `export_beetle.py` | Exportiert das Gesamtmodell (Verkehrsvariante, reduziert). |
| `verify_assembly.py` | Kontrollmontage mit den hartcodierten Radpositionen aus `AWiesbadenCar` + Rendering. |

## Fallstricke

Beide sind uns hier tatsächlich passiert und beide **melden keinen Fehler**:

- **`bpy.ops.object.origin_set` wird im `--background`-Modus als CANCELLED
  verworfen** (fehlender UI-Kontext). Der Pivot bleibt liegen, wo er war.
  Vertices stattdessen direkt verschieben.

- **`ob.bound_box` und `ob.matrix_world` sind gecacht.** Nach direkter
  Vertex-Manipulation bzw. nach dem Setzen von `.location` melden sie die alten
  Werte. Vor dem Messen `bpy.context.view_layer.update()` aufrufen oder über die
  Vertices iterieren. Das Rendering wertet den Depsgraph selbst aus — Bild und
  Messung können dadurch widersprüchlich sein, und das Bild hat recht.

- **Blenders FBX-Export schreibt Zentimeter**, UE liest FBX als Zentimeter. Ein
  `import_uniform_scale = 100` beim UE-Import (in der Annahme, Blender liefere
  Meter) ergibt ein 414 m langes Auto. Nach jedem Import die Maße ausgeben.

## Quelle

`vw-beetle-1969` — Assimp-exportiertes COLLADA, Einheit Zoll (`meter="0.0254"`),
Hochachse Y_UP, Fahrtrichtung −Y. Alle drei Abweichungen korrigiert
`dae_import.py` bzw. `export_beetle.py`.
