# AAA-Materialien, datengetrieben aus Tools/aaa_materials.json.
#
# Pro Manifest-Eintrag (Oberflaeche -> CC0-Asset-ID -> Ziel-Material + Kachelung):
#   1) CC0-PBR-Satz sicherstellen (aus Saved/_aaa_source oder von ambientCG laden)
#   2) Texturen importieren (NormalDX -> TC_NORMALMAP, Roughness/AO -> linear)
#   3) M_AAA_<surface> bauen: BaseColor/Normal/Roughness(/AO), Kachelung als
#      Skalar-Parameter "Tiling" (per MI feinjustierbar)
#   4) falls config_key gesetzt: DefaultGame.ini-Soft-Ref der City darauf umbiegen
#
# Content/*.uasset ist git-ignoriert -> es werden NUR neue Assets angelegt und die
# (getrackte) Config editiert; bestehende City-Materialien bleiben unangetastet.
# Reproduzierbar: erneuter Lauf liefert dasselbe Ergebnis (idempotent).
#
# Headless:
#   UnrealEditor-Cmd.exe <proj> -run=pythonscript -script="Tools/aaa_import_materials.py"
import unreal, os, json, zipfile, subprocess

ROOT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
MANIFEST = os.path.join(ROOT, "Tools", "aaa_materials.json")
SRC = os.path.join(ROOT, "Saved", "_aaa_source")
INI = os.path.join(ROOT, "Config", "DefaultGame.ini")

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def load_manifest():
    with open(MANIFEST, "r", encoding="utf-8") as f:
        return json.load(f)


def ensure_source(cc0, res):
    """Staged Satz zurueckgeben oder von ambientCG laden. -> (dir, prefix) | (None,None)."""
    prefix = "%s_%s-JPG" % (cc0, res)
    d = os.path.join(SRC, cc0)
    color = os.path.join(d, prefix + "_Color.jpg")
    if os.path.exists(color):
        return d, prefix
    os.makedirs(d, exist_ok=True)
    zpath = os.path.join(SRC, prefix + ".zip")
    url = "https://ambientcg.com/get?file=%s.zip" % prefix
    # curl (Windows-eigenes System32\curl.exe) statt urllib: im Editor-Prozess
    # scheitert urllib an Netz/SSL, curl laedt zuverlaessig.
    try:
        res = subprocess.run(["curl", "-sL", "--fail", "-m", "120", "-o", zpath, url],
                             capture_output=True, timeout=150)
        if res.returncode != 0 or not os.path.exists(zpath):
            unreal.log_warning("AAA: curl-Download fehlgeschlagen fuer %s (rc=%s)." % (cc0, res.returncode))
            return None, None
        with zipfile.ZipFile(zpath) as z:
            z.extractall(d)
    except Exception as e:
        unreal.log_warning("AAA: Download fehlgeschlagen fuer %s (%s) - Satz uebersprungen." % (cc0, e))
        return None, None
    return (d, prefix) if os.path.exists(color) else (None, None)


def import_tex(abspath, dest_path, name):
    if not os.path.exists(abspath):
        return None
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", abspath)
    task.set_editor_property("destination_path", dest_path)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    return unreal.load_asset(dest_path + "/" + name)


def build_material(surface, color, normal, rough, ao, tiling, mat_root):
    mat_name = "M_AAA_" + surface
    mat_path = mat_root + "/" + mat_name
    if EAL.does_asset_exist(mat_path):
        EAL.delete_asset(mat_path)
    mat = tools.create_asset(mat_name, mat_root, unreal.Material, unreal.MaterialFactoryNew())

    # Kachelung als benannter Skalar-Parameter -> pro MI justierbar.
    tile = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1300, 0)
    tile.set_editor_property("parameter_name", "Tiling")
    tile.set_editor_property("default_value", float(tiling))
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -900, 0)
    mel.connect_material_expressions(uv, "", mul, "A")
    mel.connect_material_expressions(tile, "", mul, "B")

    def sample(tex, y, sampler):
        s = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -600, y)
        s.set_editor_property("texture", tex)
        s.set_editor_property("sampler_type", sampler)
        mel.connect_material_expressions(mul, "", s, "UVs")
        return s

    cs = sample(color, -300, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    mel.connect_material_property(cs, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    ns = sample(normal, 0, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    mel.connect_material_property(ns, "RGB", unreal.MaterialProperty.MP_NORMAL)
    rs = sample(rough, 300, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    mel.connect_material_property(rs, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    if ao:
        aos = sample(ao, 600, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
        mel.connect_material_property(aos, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

    mel.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat_path


def set_ini_key(key, value):
    """DefaultGame.ini <key>=<value> setzen, wenn der Schluessel existiert."""
    with open(INI, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()
    changed = False
    for i, ln in enumerate(lines):
        if ln.strip().startswith(key + "="):
            new = "%s=%s" % (key, value)
            if lines[i] != new:
                lines[i] = new
                changed = True
            break
    if changed:
        with open(INI, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    return changed


def main():
    m = load_manifest()
    res = m.get("resolution", "2K")
    tex_root = m["texture_root"]
    mat_root = m["material_root"]
    done, wired = 0, 0
    for s in m["sets"]:
        surface, cc0 = s["surface"], s["cc0"]
        d, prefix = ensure_source(cc0, res)
        if not d:
            continue
        base = os.path.join(d, prefix)
        color = import_tex(base + "_Color.jpg", tex_root, surface + "_C")
        normal = import_tex(base + "_NormalDX.jpg", tex_root, surface + "_N")
        rough = import_tex(base + "_Roughness.jpg", tex_root, surface + "_R")
        ao = import_tex(base + "_AmbientOcclusion.jpg", tex_root, surface + "_AO") if s.get("ao") else None
        if not (color and normal and rough):
            unreal.log_warning("AAA: unvollstaendig, ueberspringe " + surface)
            continue
        normal.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        normal.set_editor_property("srgb", False)
        rough.set_editor_property("srgb", False)
        EAL.save_loaded_asset(normal); EAL.save_loaded_asset(rough)
        if ao:
            ao.set_editor_property("srgb", False); EAL.save_loaded_asset(ao)

        mat_path = build_material(surface, color, normal, rough, ao, s.get("tiling", 1.0), mat_root)
        done += 1
        unreal.log("AAA-Material fertig: %s (Kachelung %.1f)" % (mat_path, s.get("tiling", 1.0)))

        ck = s.get("config_key")
        if ck:
            ref = "/Game/Materials/AAA/M_AAA_%s.M_AAA_%s" % (surface, surface)
            if set_ini_key(ck, ref):
                wired += 1
                unreal.log("AAA-Verdrahtung: DefaultGame.ini %s -> %s" % (ck, ref))
    unreal.log("AAA fertig: %d Materialien gebaut, %d City-Refs verdrahtet." % (done, wired))


main()
