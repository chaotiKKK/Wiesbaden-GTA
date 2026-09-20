"""Strassenmoebel als echte Meshes: Baenke mit Holzlatten, gelber Briefkasten,
roter Hydrant - statt der Engine-Wuerfel und -Zylinder.

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background \
        --python Tools/Blender/make_street_furniture.py -- --out Data/Raw/Moebel

Ergebnis: je Art (und Variante) eine FBX plus `street_furniture.json` mit den
Materialslots. Den Import nach Unreal macht `Tools/import_street_furniture.py`.

MASSE SIND VERTRAG: Sie muessen zu `FStreetFurnitureDimensions`
(Source/WiesbadenReal/World/StreetFurnitureShapes.h) passen. Der Bake-Pass
stellt die Moebel auf Gehweghoehe und dreht sie zur Fahrbahn; ein Mesh, das
anders hoch ist als die Struktur sagt, steht im Boden oder schwebt.

ACHSEN: +X ist die Blickrichtung des Moebels (die Bank schaut von der Strasse
weg), +Y quer nach links, +Z nach oben, Ursprung im Fusspunkt. Genau so liest
der Spawner die Drehung aus dem Bake-Pass.

FALLSTRICKE (aus Tools/Blender/README.md, hier tatsaechlich relevant):
* `bpy.ops.object.origin_set` wird im Hintergrundmodus verworfen - der Ursprung
  wird ueber die Vertexlage gesetzt, nicht per Operator.
* Blenders FBX-Export schreibt Zentimeter und UE liest Zentimeter. Hier wird in
  METERN gebaut; aus 1,8 m Bank werden im Import 180 cm. Kein Skalierungsfaktor
  beim Import.
* Flaechen muessen nach AUSSEN zeigen: Unreal-Materialien sind einseitig, der
  Blender-Render cullt nicht und zeigt den Fehler nie. Jeder Builder ruft am
  Ende `orient_outward()`.
"""
import json
import math
import os
import sys

import bpy


# -- Argumente ---------------------------------------------------------------

def arg_value(name, default):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if name in argv:
        i = argv.index(name)
        if i + 1 < len(argv):
            return argv[i + 1]
    return default


def log(msg):
    print("[moebel] %s" % msg, flush=True)


# -- Materialien -------------------------------------------------------------
#
# Farben sind lineare Basisfarben. Der Unreal-Import macht daraus je eine
# Material-Instanz am Lack-Master (siehe Tools/import_street_furniture.py).

MATERIALS = {
    # Holzlatten der Baenke und Tische - warmes Laerchenbraun, kein Rotholz.
    "WbMoebelHolz":   ((0.240, 0.130, 0.055), 0.0, 0.70),
    # Gusseiserne Wangen, Poller, Beine, Standrohre: dunkelgrau, matt lackiert.
    "WbMoebelMetall": ((0.045, 0.048, 0.052), 0.9, 0.42),
    # Verzinkter Korpus (Abfallkorb, Deckel) - heller als der Lack.
    "WbMoebelZink":   ((0.330, 0.340, 0.355), 0.9, 0.38),
    # Deutsche Post: RAL 1021 Rapsgelb.
    "WbMoebelGelb":   ((0.800, 0.520, 0.020), 0.0, 0.35),
    # Hydrant: RAL 3000 Feuerrot.
    "WbMoebelRot":    ((0.520, 0.035, 0.025), 0.0, 0.38),
    # Altglas-Container.
    "WbMoebelGruen":  ((0.045, 0.180, 0.075), 0.0, 0.45),
    # Automatenfront.
    "WbMoebelBlau":   ((0.030, 0.090, 0.260), 0.0, 0.30),
}


# -- Mesh-Bau ----------------------------------------------------------------
#
# Bewusst ein eigener, schlanker Builder statt des Nerobergbahn-Builders: der
# bringt planare Texturkachelung, Steigungsneigung und eine Materialtabelle mit
# 40 Eintraegen mit, von denen hier nichts gebraucht wird. Gemeinsam ist die
# WINDUNGS-Konvention - sie ist die, die im Spiel wehtut.

class MeshBuilder:
    """Sammelt Vierecke mit Materialslot und baut daraus ein Blender-Mesh."""

    def __init__(self):
        self.verts = []
        self.faces = []
        self.face_mat = []
        self.mat_names = []
        # Summe der Koerper-Volumina. Stimmt sie nicht mit dem gemessenen
        # vorzeichenbehafteten Volumen ueberein, windet mindestens ein Koerper
        # falsch - genau der Fehler, der die Lehnenlatten unsichtbar machte.
        self.soll_volumen = 0.0

    def _mat(self, name):
        if name not in self.mat_names:
            self.mat_names.append(name)
        return self.mat_names.index(name)

    def quad(self, p0, p1, p2, p3, mat):
        i = len(self.verts)
        self.verts += [p0, p1, p2, p3]
        self.faces.append((i, i + 1, i + 2, i + 3))
        self.face_mat.append(self._mat(mat))

    def box(self, x0, x1, y0, y1, z0, z1, mat):
        """Quader aus sechs Vierecken (Windung richtet orient_outward())."""
        a = (x0, y0, z0); b = (x1, y0, z0); c = (x1, y1, z0); d = (x0, y1, z0)
        e = (x0, y0, z1); f = (x1, y0, z1); g = (x1, y1, z1); h = (x0, y1, z1)
        # Reihenfolge so, dass JEDE Flaeche nach aussen zeigt (gegen den
        # Uhrzeigersinn von aussen gesehen). Die naheliegende Schreibweise
        # a,b,c,d / a,b,f,e / a,d,h,e dreht drei der sechs Flaechen nach INNEN;
        # in Unreal sind Materialien einseitig, und die Bank verlor dadurch von
        # der Strassenseite aus ihre Lehne - im Blender-Render war nichts zu
        # sehen, weil EEVEE nicht cullt.
        self.soll_volumen += abs((x1-x0) * (y1-y0) * (z1-z0))
        self.quad(a, d, c, b, mat)   # unten (-Z)
        self.quad(e, f, g, h, mat)   # oben  (+Z)
        self.quad(a, b, f, e, mat)   # -Y
        self.quad(d, h, g, c, mat)   # +Y
        self.quad(a, e, h, d, mat)   # -X
        self.quad(b, c, g, f, mat)   # +X

    def zylinder(self, cx, cy, z0, z1, radius, mat, seiten=12, radius_oben=None):
        """Stehender Zylinder oder Kegelstumpf, mit Deckel und Boden."""
        r_oben = radius if radius_oben is None else radius_oben
        # Kegelstumpf-Volumen (fuer den Zylinder faellt es auf pi*r^2*h zusammen).
        self.soll_volumen += (math.pi * abs(z1 - z0) / 3.0
                              * (radius*radius + radius*r_oben + r_oben*r_oben))
        ring = [(cx + radius * math.cos(2*math.pi*i/seiten),
                 cy + radius * math.sin(2*math.pi*i/seiten)) for i in range(seiten)]
        ring_o = [(cx + r_oben * math.cos(2*math.pi*i/seiten),
                   cy + r_oben * math.sin(2*math.pi*i/seiten)) for i in range(seiten)]
        for i in range(seiten):
            j = (i + 1) % seiten
            self.quad((ring[i][0], ring[i][1], z0), (ring[j][0], ring[j][1], z0),
                      (ring_o[j][0], ring_o[j][1], z1), (ring_o[i][0], ring_o[i][1], z1), mat)
            # Deckel und Boden als Faecher um die Mitte (entartete Vierecke).
            self.quad((cx, cy, z1), (ring_o[i][0], ring_o[i][1], z1),
                      (ring_o[j][0], ring_o[j][1], z1), (cx, cy, z1), mat)
            # Boden ANDERSHERUM als der Deckel - sonst zeigt er nach innen.
            self.quad((cx, cy, z0), (ring[j][0], ring[j][1], z0),
                      (ring[i][0], ring[i][1], z0), (cx, cy, z0), mat)

    def orient_outward(self):
        """Dreht alle Flaechen, wenn das vorzeichenbehaftete Volumen negativ ist.

        Unreal-Materialien sind einseitig; EEVEE cullt nicht, der Fehler faellt
        also erst im Spiel auf. Darum misst das hier statt zu vertrauen.
        """
        volumen = 0.0
        for f in self.faces:
            p = [self.verts[i] for i in f]
            for a, b, c in ((p[0], p[1], p[2]), (p[0], p[2], p[3])):
                volumen += (a[0]*(b[1]*c[2]-b[2]*c[1])
                            - a[1]*(b[0]*c[2]-b[2]*c[0])
                            + a[2]*(b[0]*c[1]-b[1]*c[0])) / 6.0
        if volumen < 0.0:
            self.faces = [tuple(reversed(f)) for f in self.faces]
            volumen = -volumen
        return volumen

    def windung_pruefen(self, name):
        """Gemessenes Volumen gegen die Summe der Koerper - Windungsprobe.

        Eine Flaeche mit falscher Windung ist in Unreal schlicht unsichtbar,
        und im Blender-Render sieht man nichts davon (EEVEE cullt nicht). Das
        vorzeichenbehaftete Volumen verraet sie: es faellt kleiner aus als die
        Summe der Koerper. Ein Zylinder wird als Vieleck gebaut und liegt
        darum systematisch ein paar Prozent unter der Kreisformel - erst ab
        15 % Abweichung ist es ein Windungsfehler.
        """
        gemessen = abs(self.orient_outward())
        if self.soll_volumen <= 0.0:
            return True
        abweichung = abs(gemessen - self.soll_volumen) / self.soll_volumen
        if abweichung > 0.15:
            log("FEHLER %s: Volumen %.4f m3 statt %.4f m3 (%.0f %% daneben) - "
                "Flaechenwindung pruefen" % (name, gemessen, self.soll_volumen,
                                             abweichung * 100.0))
            return False
        return True

    def to_object(self, name):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(self.verts, [], [f for f in self.faces])
        mesh.validate(verbose=False)
        for slot_name in self.mat_names:
            mat = bpy.data.materials.get(slot_name) or bpy.data.materials.new(slot_name)
            rgb, metallic, rough = MATERIALS[slot_name]
            mat.use_nodes = True
            bsdf = mat.node_tree.nodes.get("Principled BSDF")
            if bsdf:
                bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
                bsdf.inputs["Metallic"].default_value = metallic
                bsdf.inputs["Roughness"].default_value = rough
            mesh.materials.append(mat)
        for i, mat_index in enumerate(self.face_mat):
            mesh.polygons[i].material_index = mat_index
        mesh.polygons.foreach_set("use_smooth", [False] * len(mesh.polygons))
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        return obj


# -- Die acht Arten ----------------------------------------------------------
#
# Masse in METERN, identisch zu FStreetFurnitureDimensions.

LATTE = 0.045          # Staerke einer Holzlatte
FUGE = 0.012           # Luft zwischen den Latten


def latten(b, x0, x1, y_halb, z0, z1, mat, anzahl):
    """Reihe von Latten quer zur Blickrichtung - das Erkennungsmerkmal.

    Eine Bank ist im Vorbild nicht eine Platte, sondern fuenf schmale Bretter
    mit Luft dazwischen; genau diese Fugen machen sie auf zehn Meter als Bank
    erkennbar.
    """
    spanne = (x1 - x0)
    breite = (spanne - (anzahl - 1) * FUGE) / anzahl
    for i in range(anzahl):
        a = x0 + i * (breite + FUGE)
        b.box(a, a + breite, -y_halb, y_halb, z0, z1, mat)


def build_bench(mit_lehne=True):
    """Bank: Sitzlatten quer, Lehnenlatten waagerecht, Gusseisen-Wangen."""
    b = MeshBuilder()
    laenge, tiefe, sitz, lehne = 1.80, 0.45, 0.45, 0.40
    latten(b, -tiefe/2, tiefe/2, laenge/2, sitz - LATTE, sitz, "WbMoebelHolz", 5)
    for seite in (1.0, -1.0):
        y = seite * (laenge/2 - 0.04)
        b.box(-tiefe/2, tiefe/2, y - 0.03, y + 0.03, 0.0, sitz, "WbMoebelMetall")
        b.box(-tiefe/2 - 0.02, tiefe/2 + 0.02, y - 0.06, y + 0.06, 0.0, 0.03,
              "WbMoebelMetall")
    if mit_lehne:
        rueck_x = -tiefe/2 + LATTE
        hoehe = lehne - 2 * FUGE
        brett = (hoehe - 2 * FUGE) / 3.0
        for i in range(3):
            z0 = sitz + 0.05 + i * (brett + FUGE)
            b.box(rueck_x - LATTE/2, rueck_x + LATTE/2, -laenge/2, laenge/2,
                  z0, z0 + brett, "WbMoebelHolz")
        # Lehnenholme, die die Latten tragen.
        for seite in (1.0, -1.0):
            y = seite * (laenge/2 - 0.04)
            b.box(rueck_x - 0.025, rueck_x + 0.025, y - 0.03, y + 0.03,
                  sitz, sitz + lehne, "WbMoebelMetall")
    return b


def build_bollard(mit_ring=False):
    b = MeshBuilder()
    hoehe, r = 0.90, 0.09
    b.zylinder(0, 0, 0.0, hoehe - 0.06, r, "WbMoebelMetall", seiten=12)
    # Abgesetzte Kuppe - ein Poller ist oben nie plan abgeschnitten.
    b.zylinder(0, 0, hoehe - 0.06, hoehe, r, "WbMoebelMetall", seiten=12,
               radius_oben=r * 0.55)
    if mit_ring:
        b.zylinder(0, 0, hoehe - 0.20, hoehe - 0.14, r + 0.006, "WbMoebelZink", seiten=12)
    return b


def build_waste_basket():
    b = MeshBuilder()
    oberkante, hoehe, r = 0.95, 0.50, 0.20
    b.zylinder(0, 0, oberkante - hoehe, oberkante, r, "WbMoebelZink", seiten=12)
    # Einwurfrand, etwas ueberstehend.
    b.zylinder(0, 0, oberkante, oberkante + 0.03, r + 0.02, "WbMoebelMetall", seiten=12)
    # Standrohr bis zum Boden.
    b.zylinder(0, 0, 0.0, oberkante - hoehe + 0.02, 0.04, "WbMoebelMetall", seiten=8)
    return b


def build_fire_hydrant():
    b = MeshBuilder()
    hoehe, r = 0.80, 0.11
    b.zylinder(0, 0, 0.06, hoehe, r, "WbMoebelRot", seiten=12)
    # Fussflansch und Haube - daran erkennt man einen Ueberflurhydranten.
    b.zylinder(0, 0, 0.0, 0.06, r + 0.05, "WbMoebelMetall", seiten=12)
    b.zylinder(0, 0, hoehe, hoehe + 0.07, r + 0.02, "WbMoebelMetall", seiten=12,
               radius_oben=r * 0.5)
    # Zwei seitliche Abgaenge (B-Kupplungen) quer zur Blickrichtung.
    for seite in (1.0, -1.0):
        b.box(-0.05, 0.05, seite * r, seite * (r + 0.10), 0.45, 0.58, "WbMoebelMetall")
    return b


def build_post_box():
    b = MeshBuilder()
    breite, tiefe, hoehe, stand = 0.40, 0.30, 0.55, 0.75
    b.box(-tiefe/2, tiefe/2, -breite/2, breite/2, stand, stand + hoehe, "WbMoebelGelb")
    # Leicht geneigtes Dach - Regen soll ablaufen.
    b.box(-tiefe/2 - 0.02, tiefe/2 + 0.02, -breite/2 - 0.02, breite/2 + 0.02,
          stand + hoehe, stand + hoehe + 0.04, "WbMoebelGelb")
    # Einwurfschlitz als dunkle Nische in der Front (+X).
    b.box(tiefe/2 - 0.015, tiefe/2 + 0.002, -breite/2 + 0.06, breite/2 - 0.06,
          stand + hoehe - 0.16, stand + hoehe - 0.10, "WbMoebelMetall")
    b.zylinder(0, 0, 0.0, stand + 0.02, 0.05, "WbMoebelMetall", seiten=8)
    return b


def build_vending_machine():
    b = MeshBuilder()
    breite, tiefe, hoehe = 0.80, 0.40, 1.70
    b.box(-tiefe/2, tiefe/2, -breite/2, breite/2, 0.10, hoehe, "WbMoebelMetall")
    # Bedienfront auf der Blickseite.
    b.box(tiefe/2 - 0.02, tiefe/2 + 0.01, -breite/2 + 0.05, breite/2 - 0.05,
          hoehe * 0.35, hoehe - 0.10, "WbMoebelBlau")
    # Sockel, damit der Kasten nicht auf der Kante steht.
    b.box(-tiefe/2 + 0.03, tiefe/2 - 0.03, -breite/2 + 0.03, breite/2 - 0.03,
          0.0, 0.10, "WbMoebelMetall")
    return b


def build_recycling():
    b = MeshBuilder()
    breite, tiefe, hoehe = 1.20, 1.20, 1.40
    b.box(-tiefe/2, tiefe/2, -breite/2, breite/2, 0.05, hoehe, "WbMoebelGruen")
    b.box(-tiefe/2 - 0.03, tiefe/2 + 0.03, -breite/2 - 0.03, breite/2 + 0.03,
          hoehe, hoehe + 0.05, "WbMoebelZink")
    # Zwei Einwurfoeffnungen auf der Blickseite.
    for seite in (1.0, -1.0):
        y = seite * breite * 0.22
        b.zylinder(tiefe/2 - 0.04, y, hoehe - 0.35, hoehe - 0.15, 0.09,
                   "WbMoebelMetall", seiten=8)
    b.box(-tiefe/2 + 0.05, tiefe/2 - 0.05, -breite/2 + 0.05, breite/2 - 0.05,
          0.0, 0.05, "WbMoebelMetall")
    return b


def build_picnic_table():
    b = MeshBuilder()
    laenge, tisch_breite, tisch_hoehe, sitz_hoehe = 2.00, 0.80, 0.75, 0.45
    latten(b, -tisch_breite/2, tisch_breite/2, laenge/2,
           tisch_hoehe - LATTE, tisch_hoehe, "WbMoebelHolz", 4)
    for seite in (1.0, -1.0):
        x = seite * (tisch_breite/2 + 0.25)
        latten(b, x - 0.15, x + 0.15, laenge/2,
               sitz_hoehe - LATTE, sitz_hoehe, "WbMoebelHolz", 2)
    # Zwei A-Boecke statt vier Einzelbeinen - so steht ein Picknicktisch.
    for y in (-laenge/2 + 0.25, laenge/2 - 0.25):
        for seite in (1.0, -1.0):
            x = seite * (tisch_breite/2 + 0.25)
            b.box(x - 0.04, x + 0.04, y - 0.04, y + 0.04, 0.0, sitz_hoehe,
                  "WbMoebelMetall")
        b.box(-tisch_breite/2 + 0.05, tisch_breite/2 - 0.05, y - 0.04, y + 0.04,
              sitz_hoehe, tisch_hoehe, "WbMoebelMetall")
        b.box(-tisch_breite/2 - 0.25, tisch_breite/2 + 0.25, y - 0.03, y + 0.03,
              sitz_hoehe - 0.06, sitz_hoehe, "WbMoebelMetall")
    return b


BAUPLAN = [
    ("SM_WbFurn_Bench",          lambda: build_bench(True)),
    ("SM_WbFurn_Bench_v1",       lambda: build_bench(False)),
    ("SM_WbFurn_Bollard",        lambda: build_bollard(False)),
    ("SM_WbFurn_Bollard_v1",     lambda: build_bollard(True)),
    ("SM_WbFurn_WasteBasket",    build_waste_basket),
    ("SM_WbFurn_VendingMachine", build_vending_machine),
    ("SM_WbFurn_Recycling",      build_recycling),
    ("SM_WbFurn_FireHydrant",    build_fire_hydrant),
    ("SM_WbFurn_PostBox",        build_post_box),
    ("SM_WbFurn_PicnicTable",    build_picnic_table),
]


# -- Export ------------------------------------------------------------------

def export_fbx(obj, pfad):
    for o in bpy.context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(
        filepath=pfad,
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


def masse(obj):
    xs = [v.co.x for v in obj.data.vertices]
    ys = [v.co.y for v in obj.data.vertices]
    zs = [v.co.z for v in obj.data.vertices]
    return {"laenge_y_m": round(max(ys) - min(ys), 3),
            "tiefe_x_m": round(max(xs) - min(xs), 3),
            "hoehe_m": round(max(zs), 3),
            "unterkante_m": round(min(zs), 3)}


def main():
    out_dir = arg_value("--out", "Data/Raw/Moebel")
    if not os.path.isabs(out_dir):
        wurzel = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        out_dir = os.path.join(wurzel, out_dir)
    os.makedirs(out_dir, exist_ok=True)

    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)

    manifest = {"meshes": [], "materials": {}}
    fehler = 0
    for name, bauer in BAUPLAN:
        b = bauer()
        if not b.windung_pruefen(name):
            fehler += 1
        volumen = abs(b.orient_outward())
        obj = b.to_object(name)
        bpy.context.view_layer.update()
        m = masse(obj)
        if m["unterkante_m"] < -0.001:
            log("FEHLER %s steht %.3f m im Boden" % (name, -m["unterkante_m"]))
            fehler += 1
        pfad = os.path.join(out_dir, name + ".fbx")
        export_fbx(obj, pfad)
        manifest["meshes"].append({
            "name": name, "fbx": name + ".fbx",
            "slots": list(b.mat_names), "volumen_m3": round(abs(volumen), 4), **m})
        log("%-26s %5d Flaechen, %d Slots, %.2f m hoch -> %s"
            % (name, len(b.faces), len(b.mat_names), m["hoehe_m"], os.path.basename(pfad)))

    for name, (rgb, metallic, rough) in MATERIALS.items():
        manifest["materials"][name] = {
            "base_color": [round(c, 4) for c in rgb],
            "metallic": metallic, "roughness": rough}

    mpfad = os.path.join(out_dir, "street_furniture.json")
    with open(mpfad, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    log("Manifest: %s (%d Meshes, %d Materialien, %d Fehler)"
        % (mpfad, len(manifest["meshes"]), len(manifest["materials"]), fehler))
    return fehler


if __name__ == "__main__":
    raise SystemExit(main())
