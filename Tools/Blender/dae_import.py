"""
COLLADA-Import fuer Blender 5.x, das den nativen Importer nicht mehr mitbringt.

Beschraenkt sich bewusst auf die Struktur, die Assimp erzeugt und die dieses
Modell verwendet:
  - <polylist> mit ausschliesslich Dreiecken (vcount == 3)
  - alle <input> mit offset="0", also ein gemeinsamer Indexstrom fuer
    Position/Normale/UV (Assimp schreibt die Daten unverschweisst)
Andere COLLADA-Varianten (mehrere Offsets, Polygone mit mehr als 3 Ecken)
werden ausdruecklich abgelehnt statt still falsch interpretiert - ein
lautloser Fehler haette hier eine verzerrte Karosserie zur Folge.
"""
import bpy
import math
import json
import sys
import xml.etree.ElementTree as ET
from mathutils import Matrix, Vector

NS = {"c": "http://www.collada.org/2005/11/COLLADASchema"}


def parse_floats(text):
    return [float(v) for v in text.split()]


def parse_ints(text):
    return [int(v) for v in text.split()]


def load_sources(mesh_el):
    """Liefert id -> (werte, stride)."""
    out = {}
    for src in mesh_el.findall("c:source", NS):
        arr = src.find("c:float_array", NS)
        if arr is None or not arr.text:
            continue
        acc = src.find("c:technique_common/c:accessor", NS)
        stride = int(acc.get("stride", "1")) if acc is not None else 1
        out[src.get("id")] = (parse_floats(arr.text), stride)
    return out


def read_geometries(root):
    geometries = {}
    for geo in root.findall(".//c:library_geometries/c:geometry", NS):
        mesh_el = geo.find("c:mesh", NS)
        if mesh_el is None:
            continue

        sources = load_sources(mesh_el)

        # <vertices> leitet POSITION auf eine Source um.
        vert_map = {}
        vsel = mesh_el.find("c:vertices", NS)
        if vsel is not None:
            for inp in vsel.findall("c:input", NS):
                vert_map[vsel.get("id")] = inp.get("source")[1:]

        prim = mesh_el.find("c:polylist", NS)
        if prim is None:
            prim = mesh_el.find("c:triangles", NS)
        if prim is None:
            continue

        inputs = {}
        max_off = 0
        for inp in prim.findall("c:input", NS):
            sem = inp.get("semantic")
            src = inp.get("source")[1:]
            off = int(inp.get("offset", "0"))
            max_off = max(max_off, off)
            if sem == "VERTEX":
                src = vert_map.get(src, src)
                sem = "POSITION"
            inputs[sem] = (src, off)

        if max_off != 0:
            raise RuntimeError(
                "Geometrie %s nutzt mehrere Index-Offsets - vom Parser nicht "
                "unterstuetzt." % geo.get("id"))

        vc = prim.find("c:vcount", NS)
        if vc is not None and vc.text:
            if any(c != 3 for c in parse_ints(vc.text)):
                raise RuntimeError(
                    "Geometrie %s enthaelt Polygone mit mehr als 3 Ecken."
                    % geo.get("id"))

        p_el = prim.find("c:p", NS)
        indices = parse_ints(p_el.text) if (p_el is not None and p_el.text) else []

        geometries[geo.get("id")] = {
            "sources": sources,
            "inputs": inputs,
            "indices": indices,
        }
    return geometries


def build(dae_path, unit_scale, blend_out, report_out):
    root = ET.parse(dae_path).getroot()
    geometries = read_geometries(root)

    # Y_UP -> Z_UP (Blender/UE) und Modelleinheit -> Meter in einer Matrix.
    conv = Matrix.Scale(unit_scale, 4) @ Matrix.Rotation(math.radians(90.0), 4, "X")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    created = []

    scene_nodes = root.findall(".//c:library_visual_scenes/c:visual_scene/c:node", NS)

    for node in scene_nodes:
        node_name = node.get("name") or node.get("id") or "Node"

        m_el = node.find("c:matrix", NS)
        if m_el is not None and m_el.text:
            v = parse_floats(m_el.text)
            node_m = Matrix((v[0:4], v[4:8], v[8:12], v[12:16]))
        else:
            node_m = Matrix.Identity(4)

        total = conv @ node_m

        for inst in node.findall("c:instance_geometry", NS):
            gid = inst.get("url")[1:]
            geo = geometries.get(gid)
            if geo is None:
                continue

            mat_el = inst.find(".//c:instance_material", NS)
            mat_name = mat_el.get("target")[1:] if mat_el is not None else "default"

            pos_src = geo["inputs"].get("POSITION", (None, 0))[0]
            if pos_src is None or pos_src not in geo["sources"]:
                continue
            pos_vals, pos_stride = geo["sources"][pos_src]

            uv_src = geo["inputs"].get("TEXCOORD", (None, 0))[0]
            uv_vals, uv_stride = geo["sources"].get(uv_src, ([], 2)) if uv_src else ([], 2)

            nverts = len(pos_vals) // pos_stride
            coords = []
            for i in range(nverts):
                b = i * pos_stride
                p = total @ Vector((pos_vals[b], pos_vals[b + 1], pos_vals[b + 2]))
                coords.append((p.x, p.y, p.z))

            idx = geo["indices"]
            faces = []
            for i in range(0, len(idx) - 2, 3):
                tri = (idx[i], idx[i + 1], idx[i + 2])
                # Entartete Dreiecke (doppelter Index) lehnt Blender ab.
                if len(set(tri)) == 3:
                    faces.append(tri)

            if not faces:
                continue

            me = bpy.data.meshes.new("%s_%s" % (node_name, gid))
            me.from_pydata(coords, [], faces)
            me.validate(verbose=False)

            if uv_vals and uv_stride >= 2:
                uvl = me.uv_layers.new(name="UVMap")
                for loop in me.loops:
                    b = loop.vertex_index * uv_stride
                    if b + 1 < len(uv_vals):
                        uvl.data[loop.index].uv = (uv_vals[b], uv_vals[b + 1])

            mat = bpy.data.materials.get(mat_name) or bpy.data.materials.new(mat_name)
            me.materials.append(mat)

            ob = bpy.data.objects.new("%s_%s" % (node_name, gid), me)
            bpy.context.scene.collection.objects.link(ob)
            created.append(ob)

    report = {"objects": []}
    for ob in created:
        xs, ys, zs = [], [], []
        for c in ob.bound_box:
            w = ob.matrix_world @ Vector(c)
            xs.append(w.x)
            ys.append(w.y)
            zs.append(w.z)
        report["objects"].append({
            "name": ob.name,
            "verts": len(ob.data.vertices),
            "tris": len(ob.data.polygons),
            "material": ob.data.materials[0].name if ob.data.materials else "",
            "min": [round(min(xs), 4), round(min(ys), 4), round(min(zs), 4)],
            "max": [round(max(xs), 4), round(max(ys), 4), round(max(zs), 4)],
        })

    report["count"] = len(created)
    report["total_tris"] = sum(o["tris"] for o in report["objects"])

    bpy.ops.wm.save_as_mainfile(filepath=blend_out)
    with open(report_out, "w") as f:
        json.dump(report, f, indent=1)

    print("###OK### %d Objekte, %d Dreiecke" % (report["count"], report["total_tris"]))


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:]
    build(argv[0], float(argv[1]), argv[2], argv[3])
