"""Phase-1-Beweissammlung: prueft die Bounds der gebackenen Chunk-StaticMeshes
auf NaN/Inf. Der Renderer stuerzt bei NaN-Welt-Bounds ab (RendererScene.cpp
ContainsNaN). Sagt uns, OB und WIE VIELE Assets betroffen sind und liefert
Beispiele - ohne zu raten.

Aufruf:
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/check_chunk_bounds.py" -unattended -nosplash -nullrhi
"""

import math

import unreal

CHUNKS = "/Game/Generated/Chunks"
EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log("###BOUNDS### %s" % m)


def bad(v):
    return math.isnan(v) or math.isinf(v)


paths = EAL.list_assets(CHUNKS, recursive=True, include_folder=False)
checked = 0
nan_assets = 0
examples = []

for p in paths:
    short = p.split("/")[-1].split(".")[0]
    if not (short.startswith("SM_Road_") or short.startswith("SM_Building_")
            or short.startswith("SM_RoadCol_")):
        continue
    asset = EAL.load_asset(p)
    if not isinstance(asset, unreal.StaticMesh):
        continue
    checked += 1
    try:
        # Asset-lokale Bounds (BoxExtent + Origin).
        b = asset.get_bounds()  # FBoxSphereBounds
        o = b.origin
        e = b.box_extent
        r = b.sphere_radius
        vals = [o.x, o.y, o.z, e.x, e.y, e.z, r]
    except Exception as exc:
        log("Fehler bei %s: %s" % (short, exc))
        continue
    if any(bad(v) for v in vals):
        nan_assets += 1
        if len(examples) < 8:
            examples.append("%s origin=(%s) extent=(%s) r=%s"
                            % (short, (o.x, o.y, o.z), (e.x, e.y, e.z), r))

log("Geprueft: %d Assets, davon %d mit NaN/Inf-Bounds." % (checked, nan_assets))
for ex in examples:
    log("  BEISPIEL: %s" % ex)

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
