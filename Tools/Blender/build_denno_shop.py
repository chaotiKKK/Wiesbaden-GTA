"""Dennos Laden am Sedanplatz 5: Cafe (Nordhaelfte) + Friseur (Suedhaelfte).

Alles prozedural, keine Fremd-Assets. Lokales System in METERN:
  X = entlang der Fassade nach NORDEN, +Y = nach AUSSEN (Strasse), Z = oben,
  Ursprung = Fassadenlinie Mitte, Hoehe Gehweg/Ladenboden.
Mit export_yup=True landet X->X, Y->Y, Z->Z in UE (Meter -> cm).

Ausgabe Data/Raw/Denno/denno_shop_{shell,glass,cafe,salon}.glb
(-> Tools/import_denno_shop.py). Die Glasscheiben liegen in einer eigenen
Datei, weil sie ein durchscheinendes Material brauchen.

Blender 5.2: blender -b -P Tools/Blender/build_denno_shop.py
"""
import bpy
import json
import math
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Data/Raw/Denno'
PREVIEW = ROOT.parent / '.planning/denno-shop'

# Masse aus der gemeinsamen Quelle Tools/denno_shop.json - dieselben Zahlen
# liest der Test WiesbadenReal.World.DennoShop.SharedDims gegen die C++-
# Konstanten (Ausschnitt, Denno-Platz) in WiesbadenDennoShop.h.
DIMS = json.loads((ROOT / 'Tools/denno_shop.json').read_text(encoding='utf-8'))['laden']
W2 = DIMS['halbe_breite_m']            # halbe Ladenbreite
DEPTH = DIMS['tiefe_m']                # Raumtiefe hinter der Fassade
CEIL = DIMS['decke_m']                 # Deckenhoehe
FASCIA0 = DIMS['schild_unten_m']       # Schildband (deckt den Rest
FASCIA1 = DIMS['schild_oben_m']        # des Erdgeschosses)
OVER = DIMS['front_ueberstand_m']      # Pfeiler/Schild ueber die Ladenbreite hinaus

bpy.ops.wm.read_factory_settings(use_empty=True)
MATS = {}


def mat(name, rgb, rough=0.6, metal=0.0, alpha=1.0, emit=None):
    if name in MATS:
        return MATS[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes['Principled BSDF']
    bsdf.inputs['Base Color'].default_value = (*rgb, 1)
    bsdf.inputs['Roughness'].default_value = rough
    bsdf.inputs['Metallic'].default_value = metal
    if alpha < 1.0:
        bsdf.inputs['Alpha'].default_value = alpha
        m.surface_render_method = 'BLENDED'
    if emit:
        bsdf.inputs['Emission Color'].default_value = (*emit, 1)
        bsdf.inputs['Emission Strength'].default_value = 3.0
    MATS[name] = m
    return m


# Farbwelt: Cafe warm (Salbei, Eiche, Messing), Friseur hell (Weiss, Terrazzo, Schwarz).
OAK = mat('M_Denno_Oak', (0.42, 0.26, 0.14), 0.55)
PARQUET = mat('M_Denno_Parquet', (0.50, 0.33, 0.19), 0.5)
TILE = mat('M_Denno_Tile', (0.80, 0.79, 0.76), 0.35)
SAGE = mat('M_Denno_Sage', (0.46, 0.55, 0.45), 0.8)
CREAM = mat('M_Denno_Cream', (0.93, 0.90, 0.83), 0.8)
WHITE = mat('M_Denno_White', (0.95, 0.95, 0.94), 0.7)
ANTHRA = mat('M_Denno_Anthracite', (0.10, 0.11, 0.12), 0.45, 0.3)
TEAL = mat('M_Denno_Teal', (0.05, 0.25, 0.27), 0.5)
BRASS = mat('M_Denno_Brass', (0.78, 0.60, 0.28), 0.3, 1.0)
CHROME = mat('M_Denno_Chrome', (0.85, 0.86, 0.88), 0.15, 1.0)
LEATHER = mat('M_Denno_Leather', (0.06, 0.06, 0.06), 0.35)
CHALK = mat('M_Denno_Chalk', (0.08, 0.10, 0.09), 0.9)
LETTER = mat('M_Denno_Letter', (0.96, 0.88, 0.62), 0.4, 0.4)
GREEN = mat('M_Denno_Plant', (0.16, 0.38, 0.14), 0.7)
TERRA = mat('M_Denno_Terracotta', (0.62, 0.30, 0.18), 0.8)
CAKE = mat('M_Denno_Cake', (0.84, 0.62, 0.42), 0.6)
MIRROR = mat('M_Denno_Mirror', (0.80, 0.84, 0.88), 0.03, 1.0)
LAMP = mat('M_Denno_LampGlow', (1.0, 0.85, 0.6), 0.4, 0.0, emit=(1.0, 0.8, 0.55))
GLASS = mat('M_Denno_Glass', (0.75, 0.85, 0.88), 0.05, 0.0, alpha=0.18)

GROUPS = {'shell': [], 'glass': [], 'cafe': [], 'salon': []}


def add(group, obj):
    GROUPS[group].append(obj)
    return obj


def box(group, x0, x1, y0, y1, z0, z1, material, bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
    o = bpy.context.object
    o.scale = (abs(x1 - x0), abs(y1 - y0), abs(z1 - z0))
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod = o.modifiers.new('bevel', 'BEVEL')
        mod.width = bevel
        mod.segments = 2
        bpy.ops.object.modifier_apply(modifier=mod.name)
    o.data.materials.append(material)
    return add(group, o)


def cyl(group, x, y, z0, z1, r, material, verts=24):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=z1 - z0,
                                        location=(x, y, (z0 + z1) / 2))
    o = bpy.context.object
    o.data.materials.append(material)
    for p in o.data.polygons:
        p.use_smooth = True
    return add(group, o)


def ball(group, x, y, z, r, material, squash=1.0):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=10, radius=r, location=(x, y, z))
    o = bpy.context.object
    o.scale.z = squash
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.data.materials.append(material)
    for p in o.data.polygons:
        p.use_smooth = True
    return add(group, o)


def text(group, body, x, z, size, material):
    """Schrift auf dem Schildband, von der Strasse (+Y) lesbar."""
    curve = bpy.data.curves.new('sign', 'FONT')
    curve.body = body
    curve.align_x = 'CENTER'
    curve.align_y = 'CENTER'
    curve.size = size
    curve.extrude = 0.012
    o = bpy.data.objects.new('sign ' + body, curve)
    bpy.context.collection.objects.link(o)
    # Textebene XY -> aufrecht in XZ, Vorderseite nach +Y (zur Strasse):
    # +90 Grad um X kippt die Schrift hoch, 180 Grad um Z dreht sie so,
    # dass sie von aussen (Blick nach -Y) nicht gespiegelt ist.
    o.rotation_euler = (math.radians(90), 0, math.radians(180))
    o.location = (x, 0.16, z)
    curve.materials.append(material)
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.convert(target='MESH')
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return add(group, bpy.context.object)


# ---------------------------------------------------------------- Raum
T = 0.12   # Wandstaerke der eigenen Innenwaende
box('shell', 0, W2, -DEPTH, 0.0, -0.10, 0.0, PARQUET)          # Cafe-Boden
box('shell', -W2, 0, -DEPTH, 0.0, -0.10, 0.0, TILE)            # Friseur-Boden
box('shell', -W2, W2, -DEPTH, 0.0, CEIL, CEIL + 0.10, WHITE)    # Decke
box('shell', 0, W2, -DEPTH - T, -DEPTH, 0, CEIL, SAGE)          # Rueckwand Cafe
box('shell', -W2, 0, -DEPTH - T, -DEPTH, 0, CEIL, CREAM)        # Rueckwand Friseur
box('shell', W2, W2 + T, -DEPTH, 0.0, 0, CEIL, SAGE)            # Nordwand
box('shell', -W2 - T, -W2, -DEPTH, 0.0, 0, CEIL, CREAM)         # Suedwand
# Trennwand mit Durchgang (y -4.6 .. -3.4)
box('shell', -T / 2, T / 2, -DEPTH, -4.6, 0, CEIL, CREAM)
box('shell', -T / 2, T / 2, -3.4, -0.35, 0, CEIL, CREAM)
box('shell', -T / 2, T / 2, -4.6, -3.4, 2.2, CEIL, CREAM)
# Sockelleisten
box('shell', 0.06, W2, -DEPTH + 0.005, -DEPTH + 0.03, 0, 0.1, OAK)
box('shell', -W2, -0.06, -DEPTH + 0.005, -DEPTH + 0.03, 0, 0.1, ANTHRA)

# ---------------------------------------------------------------- Schaufront
FY0, FY1 = -0.30, 0.10     # Tiefe der Ladenfront (steckt in der Leibung)
for x0, x1 in [(-W2 - OVER, -W2 + 0.35), (-0.30, 0.30), (W2 - 0.35, W2 + OVER)]:
    box('shell', x0, x1, FY0, FY1, 0.0, FASCIA0, ANTHRA)        # Pfeiler
box('shell', -W2 - OVER, W2 + OVER, FY0, FY1 + 0.06, FASCIA0, FASCIA1, TEAL)   # Schildband
box('shell', -W2 - OVER, W2 + OVER, FY1 + 0.06, FY1 + 0.10, FASCIA0 - 0.04, FASCIA0, BRASS)  # Messingkante
text('shell', "DENNO'S CAFE", W2 / 2, (FASCIA0 + FASCIA1) / 2, 0.46, LETTER)
text('shell', "DENNO'S FRISEUR", -W2 / 2, (FASCIA0 + FASCIA1) / 2, 0.46, LETTER)

DOOR = 1.15
# (Tuer-x0, Fenster-x0, Fenster-x1) je Laden - Tueren neben dem Mittelpfeiler.
for sign, door0, win0, win1 in [(1, 0.40, 1.75, W2 - 0.45), (-1, -0.40 - DOOR, -(W2 - 0.45), -1.75)]:
    d0, d1 = door0, door0 + DOOR
    # Brüstung unter dem Fenster
    box('shell', win0, win1, FY0 + 0.05, 0.02, 0.0, 0.45, ANTHRA)
    # Rahmen: Fenster
    box('shell', win0 - 0.05, win1 + 0.05, -0.08, 0.02, 0.45, 0.52, ANTHRA)
    box('shell', win0 - 0.05, win1 + 0.05, -0.08, 0.02, FASCIA0 - 0.08, FASCIA0, ANTHRA)
    for xm in (win0 - 0.05, (win0 + win1) / 2 - 0.025, win1):
        box('shell', xm, xm + 0.05, -0.08, 0.02, 0.45, FASCIA0, ANTHRA)
    # Zwischenraum Tuer <-> Fenster
    gx0, gx1 = (d1, win0 - 0.05) if sign > 0 else (win1 + 0.05, d0)
    box('shell', gx0, gx1, FY0, FY1, 0.0, FASCIA0, ANTHRA)
    # Tuerrahmen + Oberlicht-Kaempfer, Griff
    box('shell', d0, d1, -0.08, 0.02, 2.20, 2.26, ANTHRA)
    box('shell', d0 - 0.04, d0, -0.08, 0.02, 0.0, FASCIA0, ANTHRA)
    box('shell', d1, d1 + 0.04, -0.08, 0.02, 0.0, FASCIA0, ANTHRA)
    box('shell', d0 + 0.03, d1 - 0.03, -0.07, 0.01, 0.0, 0.10, ANTHRA)
    hx = d0 + 0.12 if sign > 0 else d1 - 0.12
    box('shell', hx - 0.015, hx + 0.015, 0.01, 0.05, 0.85, 1.35, BRASS)
    # Glas: Fenster (zwei Felder), Tuer, Oberlicht
    box('glass', win0, win1, -0.04, -0.02, 0.52, FASCIA0 - 0.08, GLASS)
    box('glass', d0 + 0.03, d1 - 0.03, -0.04, -0.02, 0.10, 2.20, GLASS)
    box('glass', d0, d1, -0.04, -0.02, 2.26, FASCIA0, GLASS)
    # Stufe/Schwelle
    box('shell', d0, d1, -0.10, 0.25, -0.02, 0.02, ANTHRA)


def pendant(group, x, y, shade=LAMP):
    cyl(group, x, y, 2.05, CEIL, 0.006, ANTHRA, 8)
    bpy.ops.mesh.primitive_cone_add(vertices=24, radius1=0.22, radius2=0.05, depth=0.22,
                                    location=(x, y, 1.95))
    o = bpy.context.object
    o.data.materials.append(BRASS if group == 'cafe' else ANTHRA)
    add(group, o)
    ball(group, x, y, 1.86, 0.06, shade)


def chair(group, x, y, facing):
    """Bistrostuhl; facing = Richtung der Lehne (+1 nach +Y, -1 nach -Y)."""
    box(group, x - 0.21, x + 0.21, y - 0.21, y + 0.21, 0.44, 0.48, OAK, 0.01)
    for dx in (-0.18, 0.18):
        for dy in (-0.18, 0.18):
            cyl(group, x + dx, y + dy, 0.0, 0.44, 0.015, ANTHRA, 8)
    by = y + facing * 0.19
    box(group, x - 0.20, x + 0.20, by - 0.02, by + 0.02, 0.62, 0.85, OAK, 0.01)
    for dx in (-0.18, 0.18):
        cyl(group, x + dx, by, 0.48, 0.85, 0.013, ANTHRA, 8)


def plant(group, x, y, h=1.3):
    cyl(group, x, y, 0.0, 0.42, 0.20, TERRA)
    for dx, dy, dz, r in [(0, 0, h * 0.62, 0.30), (0.12, 0.05, h * 0.78, 0.22),
                                          (-0.10, -0.06, h * 0.85, 0.20), (0.02, 0.10, h, 0.16)]:
        ball(group, x + dx, y + dy, dz, r, GREEN, 1.2)


# ---------------------------------------------------------------- Cafe (x > 0)
CY = -DEPTH + 0.35
box('cafe', 1.1, 5.6, CY - 0.30, CY + 0.35, 0.0, 1.00, OAK, 0.01)          # Tresen
box('cafe', 1.05, 5.65, CY - 0.33, CY + 0.40, 1.00, 1.06, CREAM, 0.005)    # Arbeitsplatte
box('cafe', 1.1, 5.6, CY + 0.35, CY + 0.37, 0.10, 0.95, BRASS)             # Messingblende
# Siebtraeger-Maschine
box('cafe', 1.5, 2.3, CY - 0.20, CY + 0.15, 1.06, 1.50, CHROME, 0.02)
box('cafe', 1.55, 2.25, CY + 0.15, CY + 0.18, 1.12, 1.44, ANTHRA)
for gx in (1.72, 2.08):
    cyl('cafe', gx, CY + 0.24, 1.18, 1.26, 0.035, ANTHRA, 12)
# Kuchenvitrine mit Torten
box('cafe', 3.6, 5.2, CY - 0.10, CY + 0.30, 1.06, 1.12, ANTHRA)
box('glass', 3.6, 5.2, CY - 0.10, CY + 0.30, 1.12, 1.52, GLASS)
for i, cx in enumerate((3.85, 4.25, 4.65, 5.0)):
    cyl('cafe', cx, CY + 0.10, 1.12, 1.22 + 0.02 * (i % 2), 0.14, CAKE)
# Tassenregal + Tafel an der Rueckwand
for z in (1.55, 1.95):
    box('cafe', 1.1, 5.6, -DEPTH, -DEPTH + 0.28, z, z + 0.04, OAK)
    for i in range(14):
        cx = 1.3 + i * 0.3
        cyl('cafe', cx, -DEPTH + 0.14, z + 0.04, z + 0.13, 0.045, WHITE, 12)
box('cafe', 2.0, 4.7, -DEPTH, -DEPTH + 0.03, 2.15, 2.85, CHALK)
box('cafe', 1.96, 4.74, -DEPTH, -DEPTH + 0.04, 2.11, 2.15, OAK)
box('cafe', 1.96, 4.74, -DEPTH, -DEPTH + 0.04, 2.85, 2.89, OAK)
# Bistrotische am Fenster, je zwei Stuehle
for tx in (2.2, 3.9, 5.6):
    ty = -1.35
    cyl('cafe', tx, ty, 0.0, 0.03, 0.22, ANTHRA)
    cyl('cafe', tx, ty, 0.03, 0.72, 0.03, ANTHRA, 12)
    cyl('cafe', tx, ty, 0.72, 0.76, 0.36, CREAM)
    cyl('cafe', tx - 0.08, ty + 0.05, 0.76, 0.84, 0.045, WHITE, 12)
    chair('cafe', tx, ty - 0.62, -1)
    chair('cafe', tx, ty + 0.62, 1)
    pendant('cafe', tx, ty)
# Sitzbank an der Nordwand
box('cafe', W2 - 0.55, W2, -4.8, -2.4, 0.0, 0.46, OAK, 0.01)
box('cafe', W2 - 0.12, W2, -4.8, -2.4, 0.46, 1.05, SAGE)
plant('cafe', W2 - 0.4, -0.9)
pendant('cafe', 2.4, CY + 0.6)
pendant('cafe', 4.4, CY + 0.6)

# ---------------------------------------------------------------- Friseur (x < 0)
# Zwei Bedienplaetze an der Suedwand: Spiegel, Ablage, Stuhl mit Blick zum Spiegel.
for sy in (-1.8, -3.9):
    box('salon', -W2, -W2 + 0.04, sy - 0.55, sy + 0.55, 1.05, 2.10, MIRROR)
    box('salon', -W2, -W2 + 0.06, sy - 0.60, sy + 0.60, 2.10, 2.16, ANTHRA)
    box('salon', -W2, -W2 + 0.35, sy - 0.60, sy + 0.60, 0.85, 0.90, WHITE, 0.005)
    for dz in (0.95, 1.00):
        cyl('salon', -W2 + 0.2, sy + 0.35, 0.90, dz + 0.12, 0.035, CHROME, 12)
    cx = -W2 + 1.15
    cyl('salon', cx, sy, 0.0, 0.04, 0.30, CHROME)
    cyl('salon', cx, sy, 0.04, 0.45, 0.05, CHROME, 12)
    box('salon', cx - 0.26, cx + 0.26, sy - 0.26, sy + 0.26, 0.45, 0.58, LEATHER, 0.03)
    box('salon', cx + 0.22, cx + 0.30, sy - 0.26, sy + 0.26, 0.58, 1.20, LEATHER, 0.03)
    for ay in (-0.28, 0.28):
        box('salon', cx - 0.20, cx + 0.22, sy + ay - 0.03, sy + ay + 0.03, 0.66, 0.70, CHROME)
    pendant('salon', cx - 0.2, sy)
# Waschplatz an der Rueckwand
WX = -2.2
box('salon', WX - 0.55, WX + 0.55, -DEPTH, -DEPTH + 0.55, 0.0, 0.85, WHITE, 0.01)
ball('salon', WX, -DEPTH + 0.35, 0.92, 0.26, WHITE, 0.45)
box('salon', WX - 0.30, WX + 0.30, -DEPTH + 0.65, -DEPTH + 1.35, 0.40, 0.55, LEATHER, 0.03)
box('salon', WX - 0.30, WX + 0.30, -DEPTH + 0.55, -DEPTH + 0.72, 0.55, 0.95, LEATHER, 0.03)
cyl('salon', WX, -DEPTH + 1.0, 0.0, 0.40, 0.06, CHROME, 12)
# Handtuchregal
for z in (1.2, 1.55):
    box('salon', WX - 1.6, WX - 0.8, -DEPTH, -DEPTH + 0.30, z, z + 0.03, WHITE)
    for i in range(4):
        box('salon', WX - 1.55 + i * 0.19, WX - 1.40 + i * 0.19, -DEPTH + 0.03, -DEPTH + 0.27,
            z + 0.03, z + 0.13, TEAL, 0.02)
# Empfang am Eingang
box('salon', -2.2, -0.9, -2.2, -1.7, 0.0, 1.05, ANTHRA, 0.01)
box('salon', -2.25, -0.85, -2.25, -1.65, 1.05, 1.09, BRASS)
# Wartebank an der Trennwand
box('salon', -0.55, -0.06, -3.2, -1.0, 0.0, 0.45, LEATHER, 0.03)
plant('salon', -W2 + 0.45, -DEPTH + 0.5)
pendant('salon', -1.5, -2.0)


# ---------------------------------------------------------------- Export
def export(group):
    objs = GROUPS[group]
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = 'SM_DennoShop_' + group.capitalize()
    path = OUT / ('denno_shop_%s.glb' % group)
    bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True,
                              export_apply=True, export_yup=True)
    print('###SHOP', group, 'tris', sum(len(p.vertices) - 2 for p in joined.data.polygons),
          'dims', tuple(round(v, 2) for v in joined.dimensions), 'mats', len(joined.data.materials))
    return joined


OUT.mkdir(parents=True, exist_ok=True)
joined = [export(g) for g in GROUPS]

# Vorschau: Blick von der Strasse (+Y) in den Laden, ohne Decke waere zu hell -
# Workbench zeigt die Materialfarben.
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.color_type = 'MATERIAL'
scene.display.shading.light = 'STUDIO'
scene.render.resolution_x = 1600
scene.render.resolution_y = 900
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
scene.collection.objects.link(cam)
scene.camera = cam
for name, loc, look in [('strasse', (0, 9.0, 1.7), (0, -2.0, 1.4)),
                        ('innen', (5.5, -0.6, 2.3), (-3.0, -4.5, 0.8))]:
    cam.location = loc
    cam.rotation_euler = (Vector(look) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(PREVIEW / ('shop_%s.png' % name))
    bpy.ops.render.render(write_still=True)
print('###SHOP done')
