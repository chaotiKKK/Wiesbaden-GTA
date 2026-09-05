# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Baut das Nerobergbahn-Ensemble parametrisch: zwei Wagen (identisches Mesh),
# Tal- und Bergstation im Genzmer-Historismus und den fuenfbogigen
# Backstein-Viadukt. Alles wird als getrennte FBX exportiert und ein Manifest
# (nerobergbahn.json) beschreibt je Materialslot Grundfarbe und ART, damit der
# Unreal-Import (Tools/import_nerobergbahn.py) weiss, ob ein Slot einen
# Volltonlack oder eine gekachelte AAA-Backstein-/Putz-Textur bekommt.
#
# Reale Vorlage (Recherche 2026, ESWE Verkehr / Stadtlexikon Wiesbaden):
#   - Wasserballast-Standseilbahn von 1888, Oberbau 1962 erneuert.
#   - Lackierung Blau + Narzissengelb, cremefarbenes Dach.
#   - Stufenwagen: der Boden bleibt waagerecht, die Trasse steigt darunter -
#     im Wagen steigt man von Abteil zu Abteil eine Stufe hoch.
#   - Strecke 438,5 m, unten ein fuenfbogiger Backstein-Viadukt, Stationen aus
#     Holz und Backstein.
#
# Der Wagen zeigt mit +X bergwaerts (die Actor-Tangente zeigt bergwaerts, und
# WiesbadenNerobergbahn giert nur, kippt nie). Modelliert wird in METERN; der
# FBX-Export liefert Zentimeter, Unreal importiert mit Skalierung 1,0.
#
# Aufruf:
#   blender.exe -b -P Tools/Blender/make_nerobergbahn.py -- --out <Ordner>
#
# Headless-Fallen (siehe Tools/Blender/README.md): kein origin_set, keine
# bpy.ops fuer Geometrie - alles ueber direkte Vertexlisten, damit im
# --background-Modus nichts stumm als CANCELLED verworfen wird.

import json
import math
import os
import sys

import bpy
from mathutils import Vector

OUT_DIR_DEFAULT = ("C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/"
                   "Data/Raw/Nerobergbahn")


def arg_value(name, default):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if name in argv:
        return argv[argv.index(name) + 1]
    return default


def log(msg):
    print("###WBNB### %s" % msg)


# ---------------------------------------------------------------------------
# Materialtabelle: Name -> (Grundfarbe RGB, ART).
#
# ART steuert den Unreal-Import: "paint"/"glass"/"metal"/"roof"/"dark" werden
# zu farbigen Materialinstanzen (Metall/Rauheit passend), "brick"/"plaster"/
# "stone" bekommen die gekachelten AAA-Texturen.
# ---------------------------------------------------------------------------
MATERIALS = {
    "NbBlau":      ((0.045, 0.110, 0.360), "paint"),
    "NbGelb":      ((0.930, 0.740, 0.090), "paint"),
    "NbCreme":     ((0.895, 0.870, 0.780), "paint"),
    "NbGlas":      ((0.030, 0.055, 0.080), "glass"),
    "NbSchwarz":   ((0.022, 0.022, 0.024), "dark"),
    "NbMetall":    ((0.520, 0.530, 0.560), "metal"),
    "NbBackstein": ((0.420, 0.200, 0.150), "brick"),
    "NbPutz":      ((0.795, 0.760, 0.660), "plaster"),
    "NbHolz":      ((0.230, 0.135, 0.070), "timber"),
    "NbDach":      ((0.400, 0.140, 0.100), "roof"),
    "NbStein":     ((0.615, 0.575, 0.485), "stone"),
}
MATERIAL_ORDER = list(MATERIALS.keys())


class MeshBuilder:
    """Sammelt Vierecke mit Materialslot und planarer UV, baut ein Mesh.

    Planare UV je Viereck aus den zwei groessten Weltachsen der Flaeche mal
    Kacheldichte (Kacheln je Meter) - so kacheln die Backstein-Texturen im
    Weltmass, unabhaengig von der Flaechengroesse.
    """

    def __init__(self):
        self.verts = []
        self.faces = []
        self.face_mat = []
        self.uvs = []          # je Flaecheneck ein (u, v)
        self.mat_names = []    # Slotreihenfolge

    def _mat_index(self, name):
        if name not in self.mat_names:
            self.mat_names.append(name)
        return self.mat_names.index(name)

    def quad(self, p0, p1, p2, p3, mat, tpm=1.0):
        p = [Vector(p0), Vector(p1), Vector(p2), Vector(p3)]
        base = len(self.verts)
        for v in p:
            self.verts.append((v.x, v.y, v.z))
        self.faces.append((base, base + 1, base + 2, base + 3))
        self.face_mat.append(self._mat_index(mat))

        # Dominante Normalenachse bestimmen -> die zwei anderen Achsen sind U,V.
        n = (p[1] - p[0]).cross(p[2] - p[0])
        nx, ny, nz = abs(n.x), abs(n.y), abs(n.z)
        if nx >= ny and nx >= nz:
            au, av = 1, 2       # YZ
        elif ny >= nx and ny >= nz:
            au, av = 0, 2       # XZ
        else:
            au, av = 0, 1       # XY
        for v in p:
            self.uvs.append((v[au] * tpm, v[av] * tpm))

    def box(self, x0, x1, y0, y1, z0, z1, mat, tpm=1.0):
        x0, x1 = min(x0, x1), max(x0, x1)
        y0, y1 = min(y0, y1), max(y0, y1)
        z0, z1 = min(z0, z1), max(z0, z1)
        # -Z, +Z
        self.quad((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), mat, tpm)
        self.quad((x0, y0, z1), (x0, y1, z1), (x1, y1, z1), (x1, y0, z1), mat, tpm)
        # -Y, +Y
        self.quad((x0, y0, z0), (x0, y0, z1), (x1, y0, z1), (x1, y0, z0), mat, tpm)
        self.quad((x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1), mat, tpm)
        # -X, +X
        self.quad((x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1), mat, tpm)
        self.quad((x1, y0, z0), (x1, y0, z1), (x1, y1, z1), (x1, y1, z0), mat, tpm)

    def prism_x(self, x0, x1, section, mat, tpm=1.0):
        """Prisma entlang X mit gemeinsamem Querschnitt (Liste von (y,z))."""
        n = len(section)
        a = [(x0, y, z) for (y, z) in section]
        b = [(x1, y, z) for (y, z) in section]
        # Mantel
        for i in range(n):
            j = (i + 1) % n
            self.quad(a[i], a[j], b[j], b[i], mat, tpm)
        # Zwei Deckel als Dreiecksfaecher (als entartete Vierecke)
        for i in range(1, n - 1):
            self.quad(a[0], a[i], a[i + 1], a[i + 1], mat, tpm)
            self.quad(b[0], b[i + 1], b[i], b[i], mat, tpm)

    def to_object(self, name):
        me = bpy.data.meshes.new(name)
        me.from_pydata(self.verts, [], [list(f) for f in self.faces])
        me.update()
        for i, poly in enumerate(me.polygons):
            poly.material_index = self.face_mat[i]
        uvl = me.uv_layers.new(name="UVmap")
        # Jede Flaeche ist ein Viereck mit genau vier UVs in Reihenfolge des
        # Aufbaus - Flaeche i belegt self.uvs[i*4 .. i*4+3].
        for i, poly in enumerate(me.polygons):
            for k, li in enumerate(poly.loop_indices):
                uvl.data[li].uv = self.uvs[i * 4 + k]
        for mname in self.mat_names:
            mat = bpy.data.materials.get(mname)
            if mat is None:
                rgb, _kind = MATERIALS[mname]
                mat = bpy.data.materials.new(mname)
                mat.use_nodes = True
                bsdf = mat.node_tree.nodes.get("Principled BSDF")
                if bsdf is not None:
                    bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
                mat.diffuse_color = (*rgb, 1.0)
            me.materials.append(mat)
        obj = bpy.data.objects.new(name, me)
        bpy.context.collection.objects.link(obj)
        me.update()
        return obj


# ---------------------------------------------------------------------------
# 1. Wagen
# ---------------------------------------------------------------------------

def build_wagen():
    b = MeshBuilder()

    L = 5.2
    W = 2.2
    hw = W * 0.5
    x_m = L * 0.5            # Bergende +X
    x_v = -L * 0.5           # Talende -X

    # Stufenwagen: drei Abteile steigen bergwaerts, dazwischen Plattformen.
    plat = 0.62
    comp = (L - 2.0 * plat) / 3.0
    step = 0.26
    floor0 = 0.60            # Bodenhoehe Talabteil ueber Ursprung (Schienenkontakt)
    sill = 0.50
    win = 0.58
    header = 0.12
    wall_h = sill + win + header
    roof_t = 0.13

    body_hw = hw - 0.05
    glass_hw = body_hw - 0.02
    pillar_w = 0.10

    comp_x = []
    x = x_v + plat
    for i in range(3):
        comp_x.append((x, x + comp, floor0 + i * step))
        x += comp

    # -- Unterrahmen: schwarzer Keil, Boden folgt der Steigung (~19,5 %) ------
    grade = 0.195
    fb_top = floor0 - 0.02
    fb_bz_v = -grade * (L * 0.5)     # Talende tiefer
    fb_bz_m = +grade * (L * 0.5)     # Bergende hoeher
    uf_hw = hw - 0.08
    # Prisma entlang X mit trapezfoermigem Querschnitt geht nicht (Boden
    # kippt entlang X), daher zwei sich verjuengende Endquerschnitte von Hand.
    for (yy0, yy1) in [(-uf_hw, uf_hw)]:
        # Boden (schraeg), Deckel (eben) und vier Seiten.
        pv0 = (x_v, yy0, fb_bz_v); pv1 = (x_v, yy1, fb_bz_v)
        pm0 = (x_m, yy0, fb_bz_m); pm1 = (x_m, yy1, fb_bz_m)
        tv0 = (x_v, yy0, fb_top); tv1 = (x_v, yy1, fb_top)
        tm0 = (x_m, yy0, fb_top); tm1 = (x_m, yy1, fb_top)
        b.quad(pv0, pm0, pm1, pv1, "NbSchwarz")     # Boden
        b.quad(tv0, tv1, tm1, tm0, "NbSchwarz")     # Deckel
        b.quad(pv0, pv1, tv1, tv0, "NbSchwarz")     # Talstirn
        b.quad(pm0, tm0, tm1, pm1, "NbSchwarz")     # Bergstirn
        b.quad(pv0, tv0, tm0, pm0, "NbSchwarz")     # Seite -Y
        b.quad(pv1, pm1, tm1, tv1, "NbSchwarz")     # Seite +Y

    # -- Abteile -------------------------------------------------------------
    for (cx0, cx1, fz) in comp_x:
        # Blaue Bruestung
        b.box(cx0, cx1, -body_hw, body_hw, fz, fz + sill, "NbBlau")
        # Glasband (leicht eingerueckt), darueber gelber Sturz
        b.box(cx0, cx1, -glass_hw, glass_hw, fz + sill, fz + sill + win, "NbGlas")
        b.box(cx0, cx1, -body_hw, body_hw, fz + sill + win, fz + wall_h, "NbGelb")
        # Gelbe Fensterpfosten (Ecken + Mitte) beidseitig, vor dem Glas
        posts = [cx0, (cx0 + cx1) * 0.5, cx1 - pillar_w]
        for px in posts:
            for yy in (-body_hw, body_hw - 0.02):
                b.box(px, px + pillar_w, yy, yy + 0.02, fz + sill, fz + sill + win, "NbGelb")
        # Cremefarbenes Dach mit leichtem Ueberstand
        b.box(cx0 - 0.03, cx1 + 0.03, -body_hw - 0.05, body_hw + 0.05,
              fz + wall_h, fz + wall_h + roof_t, "NbCreme")
        # Gelbe Traufleiste
        for yy in (-body_hw - 0.05, body_hw + 0.03):
            b.box(cx0 - 0.03, cx1 + 0.03, yy, yy + 0.02, fz + wall_h - 0.04,
                  fz + wall_h, "NbGelb")

    # Stufen-Riser zwischen den Abteilen (blau) + Bergstirn/Talstirn
    for i in range(len(comp_x) - 1):
        x_edge = comp_x[i][1]
        z_lo = comp_x[i][2]
        z_hi = comp_x[i + 1][2]
        b.box(x_edge - 0.02, x_edge + 0.02, -body_hw, body_hw,
              z_lo, z_hi + wall_h, "NbBlau")
    # Stirnwaende (blau unten, Glas oben) am Tal- und Bergabteil
    for (cx, fz, nx) in [(comp_x[0][0], comp_x[0][2], -1), (comp_x[-1][1], comp_x[-1][2], +1)]:
        xw = cx
        b.box(xw - 0.03, xw + 0.03, -body_hw, body_hw, fz, fz + sill, "NbBlau")
        b.box(xw - 0.03, xw + 0.03, -glass_hw, glass_hw, fz + sill, fz + wall_h, "NbGlas")

    # -- Endplattformen mit Gelaender ---------------------------------------
    for (px0, px1, fz) in [(x_v, x_v + plat, comp_x[0][2]),
                           (x_m - plat, x_m, comp_x[-1][2])]:
        b.box(px0, px1, -body_hw, body_hw, fz - 0.06, fz, "NbSchwarz")   # Plattformboden
        rail_z = fz + 0.95
        for yy in (-body_hw, body_hw - 0.04):
            b.box(px0, px1, yy, yy + 0.04, rail_z - 0.04, rail_z, "NbMetall")  # Handlauf
            for pxp in (px0, (px0 + px1) * 0.5, px1 - 0.05):
                b.box(pxp, pxp + 0.05, yy, yy + 0.04, fz, rail_z, "NbMetall")  # Pfosten
        # Stirngelaender
        xend = px0 if px0 == x_v else px1
        b.box(xend, xend + 0.04 if xend == x_v else xend - 0.04, -body_hw, body_hw,
              rail_z - 0.04, rail_z, "NbMetall")

    obj = b.to_object("SM_WbNbWagen")
    return obj, b.mat_names, (L, W)


# ---------------------------------------------------------------------------
# 2. Station (Tal / Berg) - Backsteinsockel, verputzte Wand mit Fachwerk,
#    Satteldach, Perronvordach ueber dem Gleis.
# ---------------------------------------------------------------------------

def build_station(name, width, depth):
    b = MeshBuilder()
    w = width
    d = depth
    hw = w * 0.5
    x0, x1 = -hw, hw
    y0, y1 = -d * 0.5, d * 0.5

    socle = 0.9
    wall = 3.2
    eave = socle + wall
    ridge = eave + 1.9

    # Backsteinsockel
    b.box(x0, x1, y0, y1, 0.0, socle, "NbBackstein", tpm=1.2)
    # Verputzte Wand
    b.box(x0, x1, y0, y1, socle, eave, "NbPutz", tpm=0.6)
    # Fachwerk: Eckpfosten + Riegel (Holz)
    post = 0.16
    for (px, py) in [(x0, y0), (x1 - post, y0), (x0, y1 - post), (x1 - post, y1 - post)]:
        b.box(px, px + post, py, py + post, socle, eave, "NbHolz")
    for zz in (socle + wall * 0.5,):
        b.box(x0, x1, y0, y0 + 0.10, zz - 0.08, zz + 0.08, "NbHolz")
        b.box(x0, x1, y1 - 0.10, y1, zz - 0.08, zz + 0.08, "NbHolz")
    # Fenster (Glas + Holzrahmen) an den Laengsseiten
    win_w, win_h = 0.9, 1.5
    win_z0 = socle + 0.7
    n_win = max(2, int(w // 2.2))
    for i in range(n_win):
        cx = x0 + (i + 0.5) * (w / n_win)
        for (yy, yo) in [(y0, -0.02), (y1 - 0.02, 0.02)]:
            b.box(cx - win_w * 0.5, cx + win_w * 0.5, yy + yo, yy + yo + 0.04,
                  win_z0, win_z0 + win_h, "NbGlas")
            for fx in (cx - win_w * 0.5, cx + win_w * 0.5 - 0.06):
                b.box(fx, fx + 0.06, yy + yo, yy + yo + 0.05, win_z0, win_z0 + win_h, "NbHolz")
    # Tuer an der Bergseite (Holz)
    b.box(-0.55, 0.55, y1 - 0.03, y1 + 0.03, socle, socle + 2.1, "NbHolz")

    # Satteldach (First laengs X), zwei geneigte Flaechen + zwei Giebel
    over = 0.35
    rx0, rx1 = x0 - over, x1 + over
    ry0, ry1 = y0 - over, y1 + over
    ridge_y = 0.0
    # Dachflaechen
    b.quad((rx0, ry0, eave), (rx1, ry0, eave), (rx1, ridge_y, ridge),
           (rx0, ridge_y, ridge), "NbDach", tpm=0.7)
    b.quad((rx0, ry1, eave), (rx0, ridge_y, ridge), (rx1, ridge_y, ridge),
           (rx1, ry1, eave), "NbDach", tpm=0.7)
    # Giebel (Putz) an beiden X-Enden
    for gx in (x0, x1):
        b.quad((gx, y0, eave), (gx, y1, eave), (gx, ridge_y, ridge),
               (gx, ridge_y, ridge), "NbPutz")

    # Perronvordach ueber dem Gleis (auskragend nach -Y), auf Pfosten
    can_y = y0 - 2.6
    can_z = socle + 2.4
    for cx in (x0 + 0.4, 0.0, x1 - 0.4):
        b.box(cx - 0.08, cx + 0.08, can_y, can_y + 0.16, 0.0, can_z, "NbHolz")
    b.quad((x0, y0, can_z + 0.35), (x1, y0, can_z + 0.35),
           (x1, can_y, can_z), (x0, can_y, can_z), "NbDach", tpm=0.7)

    obj = b.to_object(name)
    return obj, b.mat_names, (w, d)


# ---------------------------------------------------------------------------
# 3. Viadukt - fuenf halbrunde Backsteinboegen
# ---------------------------------------------------------------------------

def build_viadukt():
    b = MeshBuilder()

    n_arch = 5
    clear = 6.0
    pier = 1.5
    depth = 5.2
    y0, y1 = -depth * 0.5, depth * 0.5
    springing = 3.6
    r = clear * 0.5
    crown = springing + r
    deck_bot = crown + 0.5
    deck_top = deck_bot + 0.6
    parapet = deck_top + 0.9
    tpm = 1.1

    pitch = clear + pier
    length = n_arch * clear + (n_arch + 1) * pier
    x_start = -length * 0.5

    # Pfeiler (inkl. der beiden Widerlager) vom Boden bis Deckunterkante
    px = x_start
    piers_x = []
    for i in range(n_arch + 1):
        b.box(px, px + pier, y0, y1, 0.0, deck_bot, "NbBackstein", tpm=tpm)
        piers_x.append((px, px + pier))
        px += pier + clear

    # Boegen: glatte Zwickelfuellung zwischen Deckunterkante und Bogenlinie
    K = 18
    for i in range(n_arch):
        a = piers_x[i][1]
        c = piers_x[i + 1][0]
        xc = (a + c) * 0.5
        rr = (c - a) * 0.5
        prev = None
        for k in range(K + 1):
            xk = a + (c - a) * k / K
            dz = rr * rr - (xk - xc) * (xk - xc)
            zk = springing + math.sqrt(dz) if dz > 0 else springing
            cur = (xk, zk)
            if prev is not None:
                (xp, zp) = prev
                # Zwickel-Viereck (Vorder-/Rueckseite) + Bogenlaibung
                b.quad((xp, y0, zp), (xk, y0, zk), (xk, y0, deck_bot),
                       (xp, y0, deck_bot), "NbBackstein", tpm=tpm)
                b.quad((xp, y1, zp), (xp, y1, deck_bot), (xk, y1, deck_bot),
                       (xk, y1, zk), "NbBackstein", tpm=tpm)
                b.quad((xp, y0, zp), (xp, y1, zp), (xk, y1, zk),
                       (xk, y0, zk), "NbBackstein", tpm=tpm)   # Laibung
            prev = cur

    # Deckplatte (Stein) + Bruestung (Backstein) beidseitig
    b.box(x_start, x_start + length, y0 - 0.15, y1 + 0.15, deck_bot, deck_top,
          "NbStein", tpm=tpm)
    for (yy0, yy1) in [(y0 - 0.15, y0 + 0.15), (y1 - 0.15, y1 + 0.15)]:
        b.box(x_start, x_start + length, yy0, yy1, deck_top, parapet, "NbBackstein", tpm=tpm)

    obj = b.to_object("SM_WbNbViadukt")
    return obj, b.mat_names, (length, depth, parapet)


# ---------------------------------------------------------------------------
# Export + Kontrollrender
# ---------------------------------------------------------------------------

def center_origin_xy(obj, keep_z=False):
    """Ursprung in die XY-Mitte. z-Minimum auf 0 (Bauwerke stehen auf dem
    Boden) - ausser keep_z: dann bleibt der modellierte z=0 erhalten, beim
    Wagen die Schienenkontaktlinie in Wagenmitte."""
    me = obj.data
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    zshift = 0.0 if keep_z else min(zs)
    shift = Vector(((min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5, zshift))
    for v in me.vertices:
        v.co -= shift
    me.update()
    return (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))


def export_fbx(obj, path):
    for o in bpy.context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        apply_unit_scale=True,
        global_scale=1.0,
        apply_scale_options="FBX_SCALE_NONE",
        object_types={"MESH"},
        mesh_smooth_type="FACE",
        use_mesh_modifiers=True,
        add_leaf_bones=False,
        bake_anim=False,
        axis_forward="-Z",
        axis_up="Y",
    )


def slot_manifest(mat_names):
    out = []
    for nm in mat_names:
        rgb, kind = MATERIALS[nm]
        out.append({"name": nm, "base_color": [round(c, 4) for c in rgb], "kind": kind})
    return out


def setup_world():
    world = bpy.data.worlds.new("NbWelt")
    bpy.context.scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    nt.nodes.clear()
    bg = nt.nodes.new("ShaderNodeBackground")
    out = nt.nodes.new("ShaderNodeOutputWorld")
    nt.links.new(bg.outputs[0], out.inputs[0])
    bg.inputs[0].default_value = (0.52, 0.60, 0.72, 1.0)
    bg.inputs[1].default_value = 1.2
    sun = bpy.data.objects.new("NbSonne", bpy.data.lights.new("NbSonne", "SUN"))
    sun.data.energy = 4.0
    sun.rotation_euler = (math.radians(52), math.radians(12), math.radians(-40))
    bpy.context.collection.objects.link(sun)


def render_views(obj, size, out_dir, tag):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in \
        [e.bl_idname if hasattr(e, "bl_idname") else e for e in []] else "BLENDER_EEVEE"
    try:
        sc.render.engine = "BLENDER_EEVEE_NEXT"
    except Exception:
        sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x = 960
    sc.render.resolution_y = 640
    diag = max(size)
    cam_data = bpy.data.cameras.new("NbCam_%s" % tag)
    cam = bpy.data.objects.new("NbCam_%s" % tag, cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    centre = Vector((0.0, 0.0, size[2] * 0.45))
    views = {
        "seite": Vector((0.2 * diag, -2.0 * diag, 0.7 * diag)),
        "drei_viertel": Vector((1.4 * diag, -1.6 * diag, 0.9 * diag)),
    }
    for vname, loc in views.items():
        cam.location = centre + loc
        d = centre - cam.location
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(out_dir, "vorschau_%s_%s.png" % (tag, vname))
        bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)


def main():
    out_dir = os.path.abspath(arg_value("--out", OUT_DIR_DEFAULT))
    os.makedirs(out_dir, exist_ok=True)

    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    setup_world()

    manifest = {"assets": []}

    builders = [
        ("wagen", lambda: build_wagen()),
        ("talstation", lambda: build_station("SM_WbNbTalstation", 7.0, 5.0)),
        ("bergstation", lambda: build_station("SM_WbNbBergstation", 8.4, 6.0)),
        ("viadukt", lambda: build_viadukt()),
    ]

    for tag, fn in builders:
        obj, mat_names, _dims = fn()
        size = center_origin_xy(obj, keep_z=(tag == "wagen"))
        path = os.path.join(out_dir, obj.name + ".fbx")
        export_fbx(obj, path)
        render_views(obj, size, out_dir, tag)
        manifest["assets"].append({
            "tag": tag,
            "name": obj.name,
            "fbx": os.path.basename(path),
            "size_m": [round(s, 3) for s in size],
            "materials": slot_manifest(mat_names),
        })
        log("%-14s %-20s %s m  Slots %s"
            % (tag, obj.name, [round(s, 2) for s in size], mat_names))
        # Objekt behalten, damit ein Gesamtbild moeglich waere - aber fuer den
        # naechsten Einzelrender ausblenden.
        obj.hide_render = True

    with open(os.path.join(out_dir, "nerobergbahn.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    log("FERTIG: %d Assets nach %s" % (len(manifest["assets"]), out_dir))


main()
