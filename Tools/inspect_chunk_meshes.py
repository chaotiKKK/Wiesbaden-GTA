# Read-only-Diagnose fuer den "Stadt unsichtbar"-Bug: laedt eine Stichprobe der
# gebackenen Chunk-Static-Meshes (Content/Generated/Chunks) und prueft die zwei
# staerksten Verdaechtigen fuer nicht-renderndes Geometrie:
#   1) Material-Slots leer/None -> rendert unsichtbar
#   2) Bounds/Vertices degeneriert (0-Extent oder absurde Z) -> nichts zu sehen
# Ausgabe geht in die UE-Log als ###CHUNKDIAG### (headless via -ExecCmds="py ...").
import unreal


def log(msg):
    unreal.log("###CHUNKDIAG### %s" % msg)


def sample_paths(prefix, limit):
    all_assets = unreal.EditorAssetLibrary.list_assets("/Game/Generated/Chunks", recursive=False, include_folder=False)
    hits = [a for a in all_assets if ("/" + prefix) in a or a.split(".")[-1].startswith(prefix)]
    # list_assets liefert "/Game/.../SM_Building_x_y.SM_Building_x_y"
    hits = [a for a in all_assets if prefix in a]
    return hits[:limit]


def inspect(prefix, limit):
    paths = sample_paths(prefix, limit)
    log("%s: %d Assets gesamt-gefunden, pruefe %d" % (prefix, len(paths), min(limit, len(paths))))
    null_mat = 0
    zero_bounds = 0
    ok = 0
    zmins = []
    zmaxs = []
    for p in paths:
        m = unreal.load_asset(p)
        if not m or not isinstance(m, unreal.StaticMesh):
            continue
        # Material-Slots
        mats = m.static_materials
        n_null = sum(1 for sm in mats if sm.material_interface is None)
        if n_null > 0:
            null_mat += 1
        # Bounds
        b = m.get_bounds()  # FBoxSphereBounds
        ext = b.box_extent
        org = b.origin
        if ext.x < 1.0 and ext.y < 1.0 and ext.z < 1.0:
            zero_bounds += 1
        else:
            ok += 1
        zmins.append(org.z - ext.z)
        zmaxs.append(org.z + ext.z)
        if paths.index(p) < 4:
            mat0 = mats[0].material_interface.get_name() if (mats and mats[0].material_interface) else "None"
            log("  %s: slots=%d nullMat=%d ext=(%.0f,%.0f,%.0f) originZ=%.0f mat0=%s"
                % (p.split(".")[-1], len(mats), n_null, ext.x, ext.y, ext.z, org.z, mat0))
    log("%s ZUSAMMEN: geprueft %d | mit Null-Material %d | 0-Bounds %d | ok %d"
        % (prefix, len(paths), null_mat, zero_bounds, ok))
    if zmins:
        log("%s Z-Bereich der Bounds: min %.0f .. max %.0f (Weltkoordinaten cm)"
            % (prefix, min(zmins), max(zmaxs)))


log("=== START Chunk-Mesh-Diagnose ===")
inspect("SM_Building", 30)
inspect("SM_Road", 30)
log("=== ENDE Chunk-Mesh-Diagnose ===")
