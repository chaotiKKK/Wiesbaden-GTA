# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Macht aus dem Patina-Kaefer einen Herbie: Rennstreifen in Rot-Weiss-Blau
# ueber die Mitte und die Startnummer 53 auf Haube und Tueren.
#
# Aufruf:
#   blender.exe -b -P make_herbie.py -- --out <Ordner>
#
# Der Weg fuehrt ueber BACKEN, nicht ueber Malen im Bild: Die Albedo-Texturen
# des Kaefers sind vier UV-Kacheln mit wild verteilten Inseln - wo im Bild die
# Motorhaube liegt, weiss niemand. Gebacken wird deshalb in 3D: ein Material
# rechnet die Streifen aus der OBJEKTPOSITION, Cycles schreibt das Ergebnis
# durch die vorhandene UV-Abwicklung in neue Bilder.
#
# Der Lack bleibt Patina. Die Streifen legen sich nur auf LACK: eine
# Farbmaske haelt Glas (blaugrau: b > r), Rost (dunkel, gesaettigt) und
# Anbauteile heraus. Ueber Rostflecken duennt der Streifen aus - wie bei
# einem echten alten Rennwagen.

import bpy
import math
import os
import sys
from mathutils import Vector

FBX = "C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Data/Raw/Beetle/SM_VWBeetle1969_Body.fbx"
TEXDIR = "C:/freebuff/WiesbadenReal_Sicherung/Quellen/vw-beetle-1969/textures"
TILES = ["1001", "1002", "1003", "1004"]
BAKE_SIZE = 2048

# Streifen ueber die Mitte, in Metern seitlich der Laengsachse.
# Blickrichtung des Wagens ist +X; links ist +Y.
STRIPES = [
    (+0.195, +0.065, (0.6, 0.04, 0.05)),   # links:  Rot
    (+0.065, -0.065, (0.85, 0.83, 0.80)),  # mitte:  Weiss
    (-0.065, -0.195, (0.05, 0.10, 0.45)),  # rechts: Blau
]

# Startnummern-Plaketten: Mittelpunkt im Objektraum, Halbgroesse, Projektion.
#   axes: welche zwei Objektachsen auf die Plaketten-UV abgebildet werden
DECALS = [
    {"centre": (1.30, 0.0, 1.02), "half": 0.26, "axes": "xy"},   # Fronthaube
    {"centre": (0.05, +0.78, 0.72), "half": 0.24, "axes": "xz"}, # Tuer links
    # KEINE eigene Plakette fuer die rechte Tuer: beide Tueren teilen sich
    # dieselben, gespiegelten UV-Texel. Zwei Plaketten schrieben in dieselben
    # Bildpunkte und zerlegten sich gegenseitig zu Buchstabensalat. Mit nur
    # der linken erscheint rechts automatisch dieselbe Plakette - seitenverkehrt,
    # der uebliche Preis gespiegelter Abwicklungen.
]


def log(msg):
    print("[Herbie] %s" % msg)


def arg_value(name, default):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if name in argv:
        return argv[argv.index(name) + 1]
    return default


# ---------------------------------------------------------------------------
# 1. Die 53-Plakette rendern
# ---------------------------------------------------------------------------

def render_roundel(out_path):
    """Weisser Kreis, schwarzer Rand, schwarze 53 - orthografisch gerendert.

    Von Hand Bildpunkte zu setzen ergaebe Siebensegment-Ziffern; Blenders
    Textobjekt liefert echte Glyphen umsonst.
    """
    sc = bpy.data.scenes.new("Roundel")
    old = bpy.context.window.scene
    bpy.context.window.scene = sc

    # Kreis: grosse weisse Scheibe, darunter etwas groessere schwarze.
    def disc(name, radius, z, colour):
        mesh = bpy.data.meshes.new(name)
        steps = 96
        verts = [(0.0, 0.0, z)]
        verts += [(radius * math.cos(2 * math.pi * i / steps),
                   radius * math.sin(2 * math.pi * i / steps), z)
                  for i in range(steps)]
        faces = [(0, 1 + i, 1 + (i + 1) % steps) for i in range(steps)]
        mesh.from_pydata(verts, [], faces)
        obj = bpy.data.objects.new(name, mesh)
        sc.collection.objects.link(obj)
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Base Color"].default_value = (*colour, 1.0)
        bsdf.inputs["Roughness"].default_value = 1.0
        # WORKBENCH im MATERIAL-Modus liest die VIEWPORT-Farbe, nicht den
        # Knotenbaum - ohne diese Zeile ist alles dasselbe Grau.
        mat.diffuse_color = (*colour, 1.0)
        mesh.materials.append(mat)
        return obj

    disc("Rand", 1.0, 0.0, (0.02, 0.02, 0.02))
    disc("Scheibe", 0.92, 0.01, (0.92, 0.90, 0.86))

    txt_data = bpy.data.curves.new("Zahl", type='FONT')
    txt_data.body = "53"
    txt_data.size = 1.15
    txt_data.align_x = 'CENTER'
    txt_data.align_y = 'CENTER'
    txt = bpy.data.objects.new("Zahl", txt_data)
    txt.location = (0.0, 0.0, 0.02)
    sc.collection.objects.link(txt)
    mat = bpy.data.materials.new("ZahlSchwarz")
    mat.use_nodes = True
    mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.02, 0.02, 0.02, 1.0)
    mat.diffuse_color = (0.02, 0.02, 0.02, 1.0)
    txt_data.materials.append(mat)

    cam_data = bpy.data.cameras.new("RoundelCam")
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = 2.0
    cam = bpy.data.objects.new("RoundelCam", cam_data)
    cam.location = (0.0, 0.0, 5.0)
    sc.collection.objects.link(cam)
    sc.camera = cam

    sun = bpy.data.objects.new("RoundelSonne", bpy.data.lights.new("RoundelSonne", 'SUN'))
    sun.data.energy = 5.0
    sc.collection.objects.link(sun)

    # WORKBENCH zeichnet ohne diese Einstellung alles EINFARBIG grau - die
    # erste Plakette war eine leere graue Scheibe ohne Rand und ohne Zahl.
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'FLAT'
    sc.display.shading.color_type = 'MATERIAL'
    sc.render.resolution_x = 512
    sc.render.resolution_y = 512
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    sc.render.filepath = out_path
    bpy.ops.render.render(write_still=True)

    bpy.context.window.scene = old
    bpy.data.scenes.remove(sc)
    log("Plakette gerendert: %s" % os.path.basename(out_path))


# ---------------------------------------------------------------------------
# 2. Bake-Material
# ---------------------------------------------------------------------------

def stripe_and_decal_nodes(nt, albedo_node, roundel_img):
    """Streifen und Plaketten ueber die Original-Albedo mischen.

    Rueckgabe: der Farb-Ausgang, der gebacken wird.
    """
    nodes = nt.nodes
    links = nt.links

    geo = nodes.new("ShaderNodeNewGeometry")
    sep = nodes.new("ShaderNodeSeparateXYZ")
    links.new(geo.outputs["Position"], sep.inputs[0])

    alb_sep = nodes.new("ShaderNodeSeparateColor")
    links.new(albedo_node.outputs["Color"], alb_sep.inputs[0])

    # Lackmaske: hell UND r > b. Glas ist blaugrau (b >= r), Rost ist
    # dunkler - beides faellt heraus. Ueber Rost duennt der Streifen aus.
    lum = nodes.new("ShaderNodeVectorMath")   # als Mittelwert missbraucht
    # Einfacher: (r+g+b)/3 ueber zwei Mathe-Knoten.
    add_rg = nodes.new("ShaderNodeMath"); add_rg.operation = 'ADD'
    links.new(alb_sep.outputs[0], add_rg.inputs[0])
    links.new(alb_sep.outputs[1], add_rg.inputs[1])
    add_rgb = nodes.new("ShaderNodeMath"); add_rgb.operation = 'ADD'
    links.new(add_rg.outputs[0], add_rgb.inputs[0])
    links.new(alb_sep.outputs[2], add_rgb.inputs[1])
    mean = nodes.new("ShaderNodeMath"); mean.operation = 'DIVIDE'
    links.new(add_rgb.outputs[0], mean.inputs[0])
    mean.inputs[1].default_value = 3.0

    # Die Schwelle liegt bewusst NIEDRIG: Dach und Schweller dieses Kaefers
    # sind dunkelbraun verwittert, und mit einer Schwelle bei 0,42 endeten
    # die Streifen am Dachrand. Herausgehalten werden muss nur das Glas -
    # und das erledigt die Warm-Pruefung (r > b) darunter.
    bright = nodes.new("ShaderNodeMapRange")
    bright.inputs["From Min"].default_value = 0.16
    bright.inputs["From Max"].default_value = 0.26
    bright.clamp = True
    links.new(mean.outputs[0], bright.inputs["Value"])

    r_minus_b = nodes.new("ShaderNodeMath"); r_minus_b.operation = 'SUBTRACT'
    links.new(alb_sep.outputs[0], r_minus_b.inputs[0])
    links.new(alb_sep.outputs[2], r_minus_b.inputs[1])
    warmth = nodes.new("ShaderNodeMapRange")
    warmth.inputs["From Min"].default_value = 0.00
    warmth.inputs["From Max"].default_value = 0.05
    warmth.clamp = True
    links.new(r_minus_b.outputs[0], warmth.inputs["Value"])

    paint_mask = nodes.new("ShaderNodeMath"); paint_mask.operation = 'MULTIPLY'
    links.new(bright.outputs[0], paint_mask.inputs[0])
    links.new(warmth.outputs[0], paint_mask.inputs[1])

    def band(value_out, lo, hi, sharp=120.0):
        """Weiche 0/1-Maske fuer lo <= wert <= hi."""
        low = nodes.new("ShaderNodeMath"); low.operation = 'SUBTRACT'
        links.new(value_out, low.inputs[0])
        low.inputs[1].default_value = lo
        low_m = nodes.new("ShaderNodeMath"); low_m.operation = 'MULTIPLY'
        links.new(low.outputs[0], low_m.inputs[0])
        low_m.inputs[1].default_value = sharp
        low_c = nodes.new("ShaderNodeClamp")
        links.new(low_m.outputs[0], low_c.inputs[0])

        high = nodes.new("ShaderNodeMath"); high.operation = 'SUBTRACT'
        high.inputs[0].default_value = hi
        links.new(value_out, high.inputs[1])
        high_m = nodes.new("ShaderNodeMath"); high_m.operation = 'MULTIPLY'
        links.new(high.outputs[0], high_m.inputs[0])
        high_m.inputs[1].default_value = sharp
        high_c = nodes.new("ShaderNodeClamp")
        links.new(high_m.outputs[0], high_c.inputs[0])

        both = nodes.new("ShaderNodeMath"); both.operation = 'MULTIPLY'
        links.new(low_c.outputs[0], both.inputs[0])
        links.new(high_c.outputs[0], both.inputs[1])
        return both.outputs[0]

    colour_out = albedo_node.outputs["Color"]

    # Herbie-Weiss: die verwitterte Lackflaeche zu Cremeweiss aufhellen.
    #
    # Die Quell-Albedo ist ein schwerer Patina-Fotoscan (Rost, graue Flecken,
    # Radial-Artefakte) - der Wagen sah damit nach Schrottfund aus, nicht nach
    # Herbie. Hier werden die LACK-Flaechen zu Cremeweiss aufgehellt, BEVOR die
    # Streifen darueber kommen. Ausgenommen bleiben Glas (kuehl, b>r), dunkle
    # Trim-/Reifen-/Innenraum-Flaechen (niedrige Helligkeit -> bright=0) und
    # damit alles, was kein Blech ist. Teilstark, damit feine Struktur (Kratzer,
    # Blechkanten) durchscheint und der Lack nicht wie Plastik wirkt.
    not_glass = nodes.new("ShaderNodeMapRange")
    not_glass.inputs["From Min"].default_value = -0.05   # sehr kuehl = Glas -> 0
    not_glass.inputs["From Max"].default_value = -0.01
    not_glass.clamp = True
    links.new(r_minus_b.outputs[0], not_glass.inputs["Value"])

    whiten_mask = nodes.new("ShaderNodeMath"); whiten_mask.operation = 'MULTIPLY'
    links.new(bright.outputs[0], whiten_mask.inputs[0])
    links.new(not_glass.outputs[0], whiten_mask.inputs[1])
    whiten_str = nodes.new("ShaderNodeMath"); whiten_str.operation = 'MULTIPLY'
    links.new(whiten_mask.outputs[0], whiten_str.inputs[0])
    whiten_str.inputs[1].default_value = 0.82

    white_mix = nodes.new("ShaderNodeMix")
    white_mix.data_type = 'RGBA'
    links.new(whiten_str.outputs[0], white_mix.inputs["Factor"])
    links.new(colour_out, white_mix.inputs[6])
    white_mix.inputs[7].default_value = (0.88, 0.86, 0.82, 1.0)
    colour_out = white_mix.outputs[2]

    # Flaechennormale - der Schluessel gegen den lackierten INNENRAUM.
    #
    # Sitze, Bodenblech und Tuerinnenseiten liegen raeumlich mitten im
    # Streifenband und in den Plakettenkaesten. Der erste Wurf hat sie alle
    # mitlackiert; von aussen unsichtbar, aber durch jede Seitenscheibe als
    # Riesenziffern-Chaos zu bestaunen. Aussen erkennt man an der Normale:
    # Streifen nur auf nach OBEN weisenden Flaechen, Plaketten nur auf
    # Flaechen, die zur jeweiligen SEITE zeigen.
    nrm_sep = nodes.new("ShaderNodeSeparateXYZ")
    links.new(geo.outputs["True Normal"], nrm_sep.inputs[0])

    # Streifen: Oberseiten, aber NICHT der Innenraum.
    #
    # Zwei Zonen, per Maximum verodert:
    #   1. z > 0,95 - Dach und Guertellinie (haelt Sitze und Bodenblech raus)
    #   2. |x| > 1,05 UND z > 0,55 - Haube und Heckdeckel, die unter 0,95
    #      abfallen. Ein Tor NUR ueber z hat die Streifen dort amputiert:
    #      im Spiel endeten sie auf halber Haube. Der Innenraum liegt bei
    #      |x| < 1 und bleibt mit dieser Zone weiterhin unlackiert.
    # Beide Zonen verlangen eine nach oben weisende Flaeche.
    z_ok_band = band(sep.outputs[2], 0.95, 2.0, 30.0)

    abs_x = nodes.new("ShaderNodeMath"); abs_x.operation = 'ABSOLUTE'
    links.new(sep.outputs[0], abs_x.inputs[0])
    bonnet_x = band(abs_x.outputs[0], 1.05, 3.0, 20.0)
    bonnet_z = band(sep.outputs[2], 0.55, 2.0, 30.0)
    bonnet = nodes.new("ShaderNodeMath"); bonnet.operation = 'MULTIPLY'
    links.new(bonnet_x, bonnet.inputs[0])
    links.new(bonnet_z, bonnet.inputs[1])

    either = nodes.new("ShaderNodeMath"); either.operation = 'MAXIMUM'
    links.new(z_ok_band, either.inputs[0])
    links.new(bonnet.outputs[0], either.inputs[1])

    up_ok = band(nrm_sep.outputs[2], 0.25, 2.0, 8.0)
    z_ok_node = nodes.new("ShaderNodeMath"); z_ok_node.operation = 'MULTIPLY'
    links.new(either.outputs[0], z_ok_node.inputs[0])
    links.new(up_ok, z_ok_node.inputs[1])
    z_ok = z_ok_node.outputs[0]

    for hi_y, lo_y, rgb in STRIPES:
        in_band = band(sep.outputs[1], lo_y, hi_y)
        with_z = nodes.new("ShaderNodeMath"); with_z.operation = 'MULTIPLY'
        links.new(in_band, with_z.inputs[0])
        links.new(z_ok, with_z.inputs[1])
        with_paint = nodes.new("ShaderNodeMath"); with_paint.operation = 'MULTIPLY'
        links.new(with_z.outputs[0], with_paint.inputs[0])
        links.new(paint_mask.outputs[0], with_paint.inputs[1])
        # Leicht transparent, damit die Patina durchscheint.
        weaken = nodes.new("ShaderNodeMath"); weaken.operation = 'MULTIPLY'
        links.new(with_paint.outputs[0], weaken.inputs[0])
        weaken.inputs[1].default_value = 0.88

        mix = nodes.new("ShaderNodeMix")
        mix.data_type = 'RGBA'
        links.new(weaken.outputs[0], mix.inputs["Factor"])
        links.new(colour_out, mix.inputs[6])
        mix.inputs[7].default_value = (*rgb, 1.0)
        colour_out = mix.outputs[2]

    # KEINE Tuer-Plakette (xz) mehr - weder hier noch per Dreieck-Rastern.
    #
    # Die Tuer wickelt in ueber das ganze Tile VERSTREUTE und mit anderen
    # sichtbaren Blechen GETEILTE UV-Inseln ab (gemessen: 590 Tuer-Dreiecke,
    # UV-Spanne u[0,15..0,97] v[0,00..0,88] ueber die Kacheln 1002+1004). Wird
    # die 53 in diese Texel gebacken, erscheint sie zwangslaeufig auch auf den
    # Blechen, die sich dieselben Texel teilen (Kotfluegel/Seitenwand) - als
    # Geisterkreise und Radialstreifen ueber die ganze Seite. Das gilt fuer
    # BEIDE Backwege (Dreieck-Rastern wie Knotengraph, beide gemessen): ein
    # SAUBERES, gebackenes Tuer-Emblem ist auf dieser Abwicklung nicht moeglich.
    #
    # Die Herbie-Identitaet tragen die Mittelstreifen und die Hauben-53 (deren
    # UV-Insel ist kompakt und ungeteilt). Ein Tuer-53 gehoert - wenn ueberhaupt
    # - als projizierter Decal-Actor/-Component IN DER WELT auf die Tuer, nicht
    # in die geteilte Textur (Folgeaufgabe).
    for decal in []:
        cx, cy, cz = decal["centre"]
        half = decal["half"]
        axes = decal["axes"]

        first = sep.outputs[0]
        second = sep.outputs[1] if axes == "xy" else sep.outputs[2]
        first_c, second_c = (cx, cy) if axes == "xy" else (cx, cz)

        def to_uv(out, centre, flip=False):
            shift = nodes.new("ShaderNodeMath"); shift.operation = 'SUBTRACT'
            links.new(out, shift.inputs[0])
            shift.inputs[1].default_value = centre
            scale = nodes.new("ShaderNodeMath"); scale.operation = 'DIVIDE'
            links.new(shift.outputs[0], scale.inputs[0])
            scale.inputs[1].default_value = (-2.0 if flip else 2.0) * half
            centred = nodes.new("ShaderNodeMath"); centred.operation = 'ADD'
            links.new(scale.outputs[0], centred.inputs[0])
            centred.inputs[1].default_value = 0.5
            return centred.outputs[0]

        # Rechte Tuer spiegeln: die Projektion laeuft durch das Fahrzeug,
        # und von aussen betrachtet stuende die 53 dort seitenverkehrt.
        flip_u = axes == "xz" and cy < 0.0
        u = to_uv(first, first_c, flip=flip_u)
        v = to_uv(second, second_c)

        comb = nodes.new("ShaderNodeCombineXYZ")
        links.new(u, comb.inputs[0])
        links.new(v, comb.inputs[1])

        tex = nodes.new("ShaderNodeTexImage")
        tex.image = roundel_img
        tex.extension = 'CLIP'
        links.new(comb.outputs[0], tex.inputs["Vector"])

        # Nur die AUSSENHAUT: ueber ein ENGES Positionsband getrennt. Die
        # Tuerhaut liegt bei |y| >= 0,74, die Innenverkleidung weiter innen;
        # die Haubenoberseite oberhalb von z = 0,95, das Armaturenbrett
        # darunter. (Ein Normalen-Tor stand hier kurz und hat die Plaketten
        # KOMPLETT verschluckt - die Positionsbaender sind nachvollziehbar.)
        side_ok = None
        if axes == "xz":
            axis_pos = sep.outputs[1]
            if cy < 0:
                neg = nodes.new("ShaderNodeMath"); neg.operation = 'MULTIPLY'
                links.new(axis_pos, neg.inputs[0])
                neg.inputs[1].default_value = -1.0
                axis_pos = neg.outputs[0]
            # Band ab 0,5: die Tuerhaut selbst liegt bei etwa 0,74 - ein Band,
            # das erst DORT beginnt, laesst die Plakette auf der weichen
            # Kante verhungern (sie war eine Iteration lang unsichtbar).
            side_ok = band(axis_pos, 0.5, 2.0, 20.0)
        else:
            side_ok = band(sep.outputs[2], 0.95, 1.30, 25.0)

        fac = nodes.new("ShaderNodeMath"); fac.operation = 'MULTIPLY'
        links.new(tex.outputs["Alpha"], fac.inputs[0])
        links.new(side_ok, fac.inputs[1])
        fac2 = nodes.new("ShaderNodeMath"); fac2.operation = 'MULTIPLY'
        links.new(fac.outputs[0], fac2.inputs[0])
        links.new(paint_mask.outputs[0], fac2.inputs[1])

        mix = nodes.new("ShaderNodeMix")
        mix.data_type = 'RGBA'
        links.new(fac2.outputs[0], mix.inputs["Factor"])
        links.new(colour_out, mix.inputs[6])
        links.new(tex.outputs["Color"], mix.inputs[7])
        colour_out = mix.outputs[2]

    return colour_out


# ---------------------------------------------------------------------------
# 2b. Plaketten rastern
# ---------------------------------------------------------------------------

def apply_decals(body, slot_offset, bake_jobs, roundel_path):
    """Rastert die 53-Plaketten NACH dem Backen direkt in die Bilder.

    Dreieck fuer Dreieck: liegt ein Dreieck im Plakettenkasten und zeigt es
    nach aussen, wird sein UV-Dreieck im Bild gefuellt; je Bildpunkt wird
    die Plakette an der projizierten Stelle abgetastet und darueber gemischt.
    Alles in Python mit numpy - jede Zwischenzahl ist nachpruefbar, im
    Gegensatz zum Knotengraphen, der die Plaketten stumm verschluckt hat.
    """
    import numpy as np

    roundel = bpy.data.images.load(roundel_path, check_existing=True)
    rw, rh = roundel.size
    rpx = np.array(roundel.pixels[:], dtype=np.float32).reshape(rh, rw, 4)

    images = {}
    for tile, target in bake_jobs:
        w, h = target.size
        images[tile] = (target, np.array(target.pixels[:], dtype=np.float32).reshape(h, w, 4))

    me = body.data
    me.calc_loop_triangles()
    uvl = me.uv_layers["UVmap_0"].data
    mw = body.matrix_world
    nw = mw.to_3x3()

    tile_of_slot = {}
    for i, mat in enumerate(me.materials):
        for t in TILES:
            if t in mat.name:
                tile_of_slot[i] = t

    total_px = 0
    for decal in DECALS:
        # NUR die Haube (xy) rastert Python. Die Tuer (xz) laeuft ueber den
        # Knotengraphen (stripe_and_decal_nodes) - ihr geteiltes/verstreutes
        # UV liess das Dreieck-Rastern ueber die ganze Seite verschmieren.
        if decal["axes"] != "xy":
            continue
        cx, cy, cz = decal["centre"]
        half = decal["half"]
        axes = decal["axes"]
        tri_hits = 0

        for tri in me.loop_triangles:
            tile = tile_of_slot.get(tri.material_index)
            if tile is None:
                continue

            centre = mw @ tri.center
            normal = (nw @ tri.normal).normalized()

            # Kastentest + Aussenhaut-Test (endlich in einer Sprache, in der
            # man ihn AUSDRUCKEN kann).
            if axes == "xz":
                if abs(centre.x - cx) > half * 1.2 or abs(centre.z - cz) > half * 1.2:
                    continue
                # 0,705 trennt die TUERHAUT (gemessen y 0,71-0,75) von den
                # Innenlagen (0,50-0,70). Mit 0,45 schrieb auch die
                # Tuer-Innenstruktur eine VERSETZTE zweite Plakette in
                # dieselben geteilten Texel - das Ergebnis war ein
                # verschmierter Doppelkreis statt einer lesbaren 53.
                if cy > 0 and (centre.y < 0.705 or normal.y < 0.35):
                    continue
                if cy < 0 and (centre.y > -0.705 or normal.y > -0.35):
                    continue
            else:
                if abs(centre.x - cx) > half * 1.2 or abs(centre.y - cy) > half * 1.2:
                    continue
                if centre.z < 0.9 or normal.z < 0.3:
                    continue

            target, px = images[tile]
            h, w = px.shape[0], px.shape[1]

            # KEIN Kachelversatz mehr: main() hat die UVs schon vor dem
            # Backen in die Grundkachel geschoben. Ein zweiter Abzug hier
            # schob die Haube (Versatz 2) links aus dem Bild - die Tuer
            # (Versatz 0) funktionierte und verdeckte den Fehler.

            corners = []
            for li, vi in zip(tri.loops, tri.vertices):
                uv = uvl[li].uv
                pos = mw @ me.vertices[vi].co
                # Plaketten-UV aus der Objektposition.
                if axes == "xz":
                    du = (pos.x - cx) / (2.0 * half) + 0.5
                    # LINKS spiegeln, nicht rechts: von aussen betrachtet
                    # laeuft +X (Wagenfront) auf der linken Tuer nach LINKS.
                    # Das Kontrollbild zeigte dort eine seitenverkehrte 53.
                    if cy > 0.0:
                        du = 1.0 - du
                    dv = (pos.z - cz) / (2.0 * half) + 0.5
                else:
                    du = (pos.x - cx) / (2.0 * half) + 0.5
                    dv = (pos.y - cy) / (2.0 * half) + 0.5
                corners.append((uv.x * w, uv.y * h, du, dv))

            xs = [c[0] for c in corners]; ys = [c[1] for c in corners]
            x0 = max(0, int(min(xs))); x1 = min(w - 1, int(max(xs)) + 1)
            y0 = max(0, int(min(ys))); y1 = min(h - 1, int(max(ys)) + 1)
            if x1 <= x0 or y1 <= y0:
                continue

            (ax, ay, au, av), (bx, by, bu, bv), (ox, oy, ou, ov) = corners
            det = (bx - ax) * (oy - ay) - (ox - ax) * (by - ay)
            if abs(det) < 1e-6:
                continue

            tri_hits += 1
            for py_ in range(y0, y1 + 1):
                for px_ in range(x0, x1 + 1):
                    l1 = ((px_ - ax) * (oy - ay) - (ox - ax) * (py_ - ay)) / det
                    l2 = ((bx - ax) * (py_ - ay) - (px_ - ax) * (by - ay)) / det
                    l0 = 1.0 - l1 - l2
                    if l0 < -0.02 or l1 < -0.02 or l2 < -0.02:
                        continue
                    du = l0 * au + l1 * bu + l2 * ou
                    dv = l0 * av + l1 * bv + l2 * ov
                    if du < 0.0 or du >= 1.0 or dv < 0.0 or dv >= 1.0:
                        continue
                    rx = min(rw - 1, int(du * rw))
                    ry = min(rh - 1, int(dv * rh))
                    src = rpx[ry, rx]
                    alpha = float(src[3])
                    if alpha <= 0.02:
                        continue
                    dst = px[py_, px_]
                    px[py_, px_, 0] = dst[0] * (1 - alpha) + src[0] * alpha
                    px[py_, px_, 1] = dst[1] * (1 - alpha) + src[1] * alpha
                    px[py_, px_, 2] = dst[2] * (1 - alpha) + src[2] * alpha
                    total_px += 1

        log("Plakette %s/%s: %d Dreiecke getroffen" % (decal["axes"],
            "L" if decal.get("centre", (0, 0, 0))[1] > 0 else "M/R", tri_hits))

    for tile, target in bake_jobs:
        _, px = images[tile]
        target.pixels = px.reshape(-1).tolist()

    log("Plaketten gerastert: %d Bildpunkte geschrieben" % total_px)


# ---------------------------------------------------------------------------
# 3. Backen
# ---------------------------------------------------------------------------

def main():
    out_dir = os.path.abspath(arg_value(
        "--out", "C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Data/Raw/Beetle/Herbie"))
    os.makedirs(out_dir, exist_ok=True)

    roundel_path = os.path.join(out_dir, "T_Herbie_53.png")
    render_roundel(roundel_path)
    roundel_img = bpy.data.images.load(roundel_path)

    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=FBX)

    body = None
    for o in bpy.data.objects:
        if o.type == 'MESH':
            body = o
            break
    if body is None:
        raise RuntimeError("Karosserie nicht gefunden")
    log("Karosserie: %s, %d Materialschlitze" % (body.name, len(body.data.materials)))

    # Die UVs in die Grundkachel [0,1) schieben.
    #
    # Das Modell ist als UDIM abgewickelt: jeder Materialschlitz haelt seine
    # Inseln in einer EIGENEN u-Kachel (gemessen: 1001 in [1,2), 1002 in
    # [2,3), 1003 in [0,1), 1004 in [3,4)). Beim ZEICHNEN gleicht die
    # Wiederholungs-Abtastung das aus - beim BACKEN nicht: Cycles schreibt
    # nur Texel in [0,1), alles andere blieb schwarz. Der erste Durchlauf
    # lieferte deshalb drei schwarze und eine richtige Textur, und das Auto
    # im Kontrollbild war schwarz.
    #
    # Da jeder Schlitz genau eine Kachel belegt, ist das Schieben ein
    # konstanter Versatz je Schlitz - es kann nichts ueberlappen.
    me = body.data
    uvl = me.uv_layers["UVmap_0"].data
    slot_offset = {}
    for p in me.polygons:
        if p.material_index not in slot_offset:
            u = sum(uvl[li].uv.x for li in p.loop_indices) / len(p.loop_indices)
            slot_offset[p.material_index] = float(math.floor(u))
    for p in me.polygons:
        off = slot_offset.get(p.material_index, 0.0)
        if off != 0.0:
            for li in p.loop_indices:
                uvl[li].uv.x -= off
    log("UV-Kacheln verschoben: %s" % {
        me.materials[i].name: int(o) for i, o in sorted(slot_offset.items())})

    # Das FBX bringt Materialnamen MI_VWBeetle_<Kachel> mit - daraus folgt,
    # welche Albedo je Schlitz gilt.
    bake_jobs = []
    for slot_index, mat in enumerate(body.data.materials):
        tile = None
        for t in TILES:
            if t in mat.name:
                tile = t
                break
        if tile is None:
            log("Schlitz '%s' ohne Kachelnummer - unveraendert" % mat.name)
            continue

        albedo_path = os.path.join(TEXDIR, "vw_beetle_1969_mo_%s_albedo.jpeg" % tile)
        albedo = bpy.data.images.load(albedo_path, check_existing=True)

        new_mat = bpy.data.materials.new("Bake_%s" % tile)
        new_mat.use_nodes = True
        nt = new_mat.node_tree
        nt.nodes.clear()
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
        nt.links.new(bsdf.outputs[0], out.inputs[0])

        uv = nt.nodes.new("ShaderNodeUVMap")
        uv.uv_map = "UVmap_0"
        alb_node = nt.nodes.new("ShaderNodeTexImage")
        alb_node.image = albedo
        nt.links.new(uv.outputs[0], alb_node.inputs["Vector"])

        colour = stripe_and_decal_nodes(nt, alb_node, roundel_img)
        nt.links.new(colour, bsdf.inputs["Base Color"])

        target = bpy.data.images.new("T_Herbie_%s" % tile, BAKE_SIZE, BAKE_SIZE,
                                     alpha=False, float_buffer=False)
        target_node = nt.nodes.new("ShaderNodeTexImage")
        target_node.image = target
        target_node.select = True
        nt.nodes.active = target_node

        body.data.materials[slot_index] = new_mat
        bake_jobs.append((tile, target))

    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = 4
    sc.render.bake.margin = 12

    for o in bpy.context.selected_objects:
        o.select_set(False)
    body.select_set(True)
    bpy.context.view_layer.objects.active = body

    bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'}, use_clear=True, margin=12)

    # Die 53-Plaketten in die gebackenen Bilder rastern.
    apply_decals(body, slot_offset, bake_jobs, roundel_path)

    for tile, target in bake_jobs:
        path = os.path.join(out_dir, "T_Herbie_%s_albedo.png" % tile)
        target.filepath_raw = path
        target.file_format = 'PNG'
        target.save()
        log("gebacken: %s" % os.path.basename(path))

    # Kontrollbilder: gebackene Texturen aufs Modell, zwei Ansichten.
    for tile, target in bake_jobs:
        for slot_index, mat in enumerate(body.data.materials):
            if mat and mat.name == "Bake_%s" % tile:
                nt = mat.node_tree
                for n in list(nt.nodes):
                    nt.nodes.remove(n)
                out = nt.nodes.new("ShaderNodeOutputMaterial")
                bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
                nt.links.new(bsdf.outputs[0], out.inputs[0])
                uv = nt.nodes.new("ShaderNodeUVMap")
                uv.uv_map = "UVmap_0"
                img_node = nt.nodes.new("ShaderNodeTexImage")
                img_node.image = target
                nt.links.new(uv.outputs[0], img_node.inputs["Vector"])
                nt.links.new(img_node.outputs["Color"], bsdf.inputs["Base Color"])

    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x = 900
    sc.render.resolution_y = 600
    world = bpy.data.worlds.new("HerbieWelt")
    sc.world = world
    world.use_nodes = True
    nt = world.node_tree
    nt.nodes.clear()
    bg = nt.nodes.new("ShaderNodeBackground")
    wout = nt.nodes.new("ShaderNodeOutputWorld")
    nt.links.new(bg.outputs[0], wout.inputs[0])
    bg.inputs[0].default_value = (0.5, 0.55, 0.6, 1.0)
    bg.inputs[1].default_value = 1.5

    cam_data = bpy.data.cameras.new("Pruef")
    cam = bpy.data.objects.new("Pruef", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam

    centre = Vector((0.0, 0.0, 0.7))
    for name, loc in (("herbie_seite", Vector((0.5, -6.0, 1.4))),
                      ("herbie_front_oben", Vector((6.0, -2.5, 3.5)))):
        cam.location = loc
        direction = centre - cam.location
        cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = os.path.join(out_dir, "%s.png" % name)
        bpy.ops.render.render(write_still=True)
        log("Kontrollbild: %s.png" % name)

    log("FERTIG")


main()
