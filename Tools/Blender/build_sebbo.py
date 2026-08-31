# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Macht aus dem Fotoscan "Sebbo mit Kettensaege" ein Spielermodell.
#
# Aufruf:
#   blender.exe -b <Quelle>.blend -P build_sebbo.py -- --out <Ordner>
#
# Der Scan ist ein Oberkoerper, an der Huefte glatt abgeschnitten und darunter
# offen. Jemand hatte sechs weisse Zylinder als Beine daruntergehaengt: nicht
# angeschlossen, ohne Textur, und mit 0,63 m viel zu kurz - die Figur war
# insgesamt nur 1,40 m hoch. Dieses Werkzeug ersetzt sie durch Beine, die am
# gemessenen Huftquerschnitt ansetzen, und bemalt sie mit den Farben, die im
# Scan selbst am Hosensaum stehen.

import bpy
import bmesh
import math
import os
import sys
from mathutils import Matrix, Vector

# ---------------------------------------------------------------------------
# Vorgaben
# ---------------------------------------------------------------------------

SCAN_NAME = "Sebbo_Kettensaege"

# Zielzahl der Dreiecke fuer den Scan. Der Rohscan hat 392 401; das ist fuer
# eine Figur, die dauernd im Bild steht, zu viel. 80 000 halten die Falten der
# Jacke und die Zaehne der Kette, kosten aber ein Fuenftel.
TARGET_TRIS = 80000

# Winkel, ab dem eine Kante hart bleibt. Ohne das wirkt der geschweisste Scan
# facettiert, weil er in 236 449 Ecken zerlegt geliefert wurde.
SMOOTH_ANGLE_DEG = 35.0

# Ecken je Beinring.
RING_SEGMENTS = 28

# Ueberlappung der beiden Beininnenseiten in Metern.
#
# Beide Beine flachen zur Mittellinie hin ab. Laegen die Waende exakt in
# derselben Ebene, kaempften sie um jedes Pixel; ein Spalt dagegen liesse
# zwischen den Beinen hindurchsehen. Deshalb greifen sie um 2 mm ineinander.
CROTCH_OVERLAP = 0.002

# Beinprofil, gemessen von der Schnittebene an der Huefte (z = 0) nach unten.
#
#   z      dx     cy      rx     ry     f     w     hs    toe
#
# dx  Abstand der Beinmitte von der Koerpermitte
# cy  Versatz nach vorn (die Figur schaut nach -Y) bzw. hinten
# rx  halbe Breite, ry halbe Tiefe des Rings
# f   Abflachung zur Mittellinie (1 = senkrechte Wand, 0 = rund)
# w   Anteil des GEMESSENEN Huftumrisses gegenueber der Ellipse
# hs  Massstab auf diesen Umriss
# toe Verjuengung nach vorn - nur fuer den Schuh
#
# Die Hoehen folgen den ueblichen Koerpermassen eines 1,80-m-Mannes, bezogen
# auf den Schnitt in Huefthoehe (rund 1,00 m ueber dem Boden): Schritt 0,83,
# Knie 0,48, Knoechel 0,07.
#
# Der oberste Ring liegt bewusst ueber der Schnittebene und ist auf 84 Prozent
# eingezogen. Der erste Anlauf hatte ihn in voller Huftbreite 12 cm hochgezogen:
# der Torso wird nach oben schmaler, also ragte der Hosenbund als glatter
# Zylinder aus dem Mantel heraus - die Figur trug ein Fass.
LEG_RINGS = [
    ( 0.060, 0.000,  0.000, 0.170, 0.136, 1.00, 1.00, 0.84, 0.0),  # im Torso
    ( 0.005, 0.000,  0.000, 0.170, 0.136, 1.00, 1.00, 1.00, 0.0),  # Schnittebene
    (-0.080, 0.020,  0.000, 0.150, 0.132, 0.92, 0.55, 1.00, 0.0),
    (-0.170, 0.072, -0.004, 0.112, 0.118, 0.55, 0.15, 1.00, 0.0),  # Schritt
    (-0.280, 0.090, -0.008, 0.098, 0.102, 0.18, 0.00, 1.00, 0.0),
    (-0.400, 0.094, -0.010, 0.088, 0.092, 0.02, 0.00, 1.00, 0.0),
    (-0.520, 0.096, -0.012, 0.078, 0.083, 0.00, 0.00, 1.00, 0.0),  # Knie
    (-0.620, 0.096, -0.004, 0.080, 0.088, 0.00, 0.00, 1.00, 0.0),  # Wade
    (-0.750, 0.096,  0.002, 0.070, 0.076, 0.00, 0.00, 1.00, 0.0),
    (-0.860, 0.096,  0.008, 0.060, 0.065, 0.00, 0.00, 1.00, 0.0),
    (-0.885, 0.096,  0.008, 0.059, 0.064, 0.00, 0.00, 1.00, 0.0),  # Hosensaum
]

# Arbeitsstiefel. Der Schaft beginnt oberhalb des Hosensaums und steckt darin.
SHOE_RINGS = [
    (-0.855, 0.096,  0.010, 0.053, 0.058, 0.00, 0.00, 1.00, 0.00),  # Schaft
    (-0.918, 0.096,  0.006, 0.058, 0.066, 0.00, 0.00, 1.00, 0.05),  # Knoechel
    (-0.948, 0.096, -0.024, 0.063, 0.098, 0.00, 0.00, 1.00, 0.25),  # Rist
    (-0.976, 0.096, -0.058, 0.066, 0.132, 0.00, 0.00, 1.00, 0.38),
    (-0.993, 0.096, -0.068, 0.065, 0.142, 0.00, 0.00, 1.00, 0.42),  # Sohlenrand
    (-1.000, 0.096, -0.068, 0.058, 0.136, 0.00, 0.00, 1.00, 0.42),  # Sohle
]

# Hoehe der Sohle unter der Schnittebene - daraus folgt die Gesamtgroesse.
SOLE_Z = -1.000


def log(msg):
    print("[Sebbo] %s" % msg)


def arg_value(name, default):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if name in argv:
        return argv[argv.index(name) + 1]
    return default


# ---------------------------------------------------------------------------
# 1. Aufraeumen
# ---------------------------------------------------------------------------

def remove_placeholder_legs():
    """Die sechs weissen Grundkoerper loeschen."""
    doomed = [o for o in bpy.data.objects
              if o.type == 'MESH' and o.name != SCAN_NAME]
    for o in doomed:
        log("entferne Platzhalter: %s" % o.name)
        bpy.data.objects.remove(o, do_unlink=True)

    # Ihre Materialien mit. Bleiben sie liegen, heisst das neue Schuhmaterial
    # "Sebbo_Schuhe.001" - und mit ihm jede daraus gebackene Bilddatei.
    keep = set()
    for o in bpy.data.objects:
        if o.type == 'MESH':
            keep.update(m.name for m in o.data.materials if m)
    for m in list(bpy.data.materials):
        if m.name not in keep:
            m.use_fake_user = False
            bpy.data.materials.remove(m)


def freeze_transform(obj):
    """Objektverschiebung und -drehung in die Ecken schreiben.

    Der Scan steht gedreht und versetzt in der Datei. Solange das in der
    Objektmatrix steckt, meint "z" bei einer Ecke etwas anderes als bei einer
    Weltkoordinate - der erste Durchlauf hat die Figur genau deshalb auf 0,70 m
    Hoehe zusammengestaucht statt sie auf 1,78 m zu stellen.
    """
    for o in bpy.context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.select_set(False)


def clean_scan(obj):
    """Verschweissen, Scanmuell entfernen, Normalen richten, reduzieren."""
    me = obj.data
    verts_before = len(me.vertices)

    bm = bmesh.new()
    bm.from_mesh(me)

    # Der Scan kommt in 2853 losen Teilen an, weil er entlang der UV-Naehte
    # aufgetrennt geliefert wurde. Verschweissen macht daraus eine Flaeche -
    # erst dann laesst sich ueberhaupt weich schattieren oder reduzieren.
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0001)

    # Lose Krumen: Staub, den die Fotoauswertung als Geometrie missdeutet hat.
    bm.verts.ensure_lookup_table()
    visited = set()
    parts = []
    for v in bm.verts:
        if v.index in visited:
            continue
        stack = [v]
        comp = []
        while stack:
            cur = stack.pop()
            if cur.index in visited:
                continue
            visited.add(cur.index)
            comp.append(cur)
            for e in cur.link_edges:
                other = e.other_vert(cur)
                if other.index not in visited:
                    stack.append(other)
        parts.append(comp)
    parts.sort(key=len, reverse=True)
    crumb_parts = [c for c in parts[1:] if len(c) < 400]
    crumbs = [v for comp in crumb_parts for v in comp]
    if crumbs:
        bmesh.ops.delete(bm, geom=crumbs, context='VERTS')
        log("Scanmuell entfernt: %d lose Teile, %d Ecken"
            % (len(crumb_parts), len(crumbs)))

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    me.update()

    log("Scan verschweisst: %d -> %d Ecken" % (verts_before, len(me.vertices)))

    # Reduzieren. Kollabieren erhaelt die UV-Inseln besser als jedes andere
    # Verfahren; bei einem Fotoscan ist die Textur alles.
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    if tris > TARGET_TRIS:
        mod = obj.modifiers.new("Reduzieren", 'DECIMATE')
        mod.decimate_type = 'COLLAPSE'
        mod.ratio = float(TARGET_TRIS) / float(tris)
        mod.use_collapse_triangulate = True
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=mod.name)
        log("Scan reduziert: %d -> %d Dreiecke"
            % (tris, sum(len(p.vertices) - 2 for p in obj.data.polygons)))

    shade_smooth(obj)


def shade_smooth(obj):
    for p in obj.data.polygons:
        p.use_smooth = True
    for o in bpy.context.selected_objects:
        o.select_set(False)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(SMOOTH_ANGLE_DEG))
    except (AttributeError, RuntimeError, TypeError):
        # Aeltere Blender-Fassungen: der Winkelmodifikator fehlt, dann bleibt
        # es bei durchgehend weicher Schattierung.
        bpy.ops.object.shade_smooth()
    obj.select_set(False)


# ---------------------------------------------------------------------------
# 2. Huftquerschnitt messen
# ---------------------------------------------------------------------------

def measure_hip(obj, band=0.030):
    """Mittelpunkt und Umriss des Torsos an seiner Schnittkante.

    Die Beine ANALYTISCH anzusetzen und zu hoffen, dass sie passen, hat beim
    Platzhalter zu einer klaffenden Luecke gefuehrt. Stattdessen wird der
    tatsaechliche Umriss abgetastet: fuer jeden Ringwinkel der aeusserste
    Punkt des Torsos. Daran koennen die Beine buendig anschliessen.
    """
    mw = obj.matrix_world
    pts = [mw @ v.co for v in obj.data.vertices]
    zmin = min(p.z for p in pts)
    ring = [p for p in pts if p.z < zmin + band]

    cx = (min(p.x for p in ring) + max(p.x for p in ring)) * 0.5
    cy = (min(p.y for p in ring) + max(p.y for p in ring)) * 0.5
    centre = Vector((cx, cy, zmin))

    # Groesster Radius je Winkelfach - so bleibt kein Zipfel des Torsos
    # unbedeckt.
    radius = [0.0] * RING_SEGMENTS
    for p in ring:
        dx = p.x - cx
        dy = p.y - cy
        a = math.atan2(dy, dx) % (2.0 * math.pi)
        i = int(a / (2.0 * math.pi) * RING_SEGMENTS) % RING_SEGMENTS
        radius[i] = max(radius[i], math.hypot(dx, dy))

    # Leere Faecher aus den Nachbarn fuellen.
    for i in range(RING_SEGMENTS):
        if radius[i] <= 0.0:
            prev = next((radius[(i - k) % RING_SEGMENTS] for k in range(1, RING_SEGMENTS)
                         if radius[(i - k) % RING_SEGMENTS] > 0.0), 0.15)
            nxt = next((radius[(i + k) % RING_SEGMENTS] for k in range(1, RING_SEGMENTS)
                        if radius[(i + k) % RING_SEGMENTS] > 0.0), 0.15)
            radius[i] = (prev + nxt) * 0.5

    profile = []
    for i in range(RING_SEGMENTS):
        a = 2.0 * math.pi * (i + 0.5) / RING_SEGMENTS
        r = radius[i] * 1.01   # ein Prozent Zugabe: lieber Hosenbund als Loch
        profile.append((r * math.cos(a), r * math.sin(a)))

    log("Huefte: Mitte (%.3f, %.3f) bei z=%.3f, Breite %.3f m, Tiefe %.3f m"
        % (cx, cy, zmin,
           max(p[0] for p in profile) - min(p[0] for p in profile),
           max(p[1] for p in profile) - min(p[1] for p in profile)))
    return centre, profile


# ---------------------------------------------------------------------------
# 3. Beine bauen
# ---------------------------------------------------------------------------

def ring_points(spec, side, hip_profile):
    z, dx, cy, rx, ry, f, w, hs, toe = spec
    pts = []
    for i in range(RING_SEGMENTS):
        a = 2.0 * math.pi * (i + 0.5) / RING_SEGMENTS
        ex_e = rx * math.cos(a)
        ey_e = ry * math.sin(a)
        ex_h, ey_h = hip_profile[i][0] * hs, hip_profile[i][1] * hs
        ex = ex_e * (1.0 - w) + ex_h * w
        ey = ey_e * (1.0 - w) + ey_h * w

        if toe > 0.0:
            # Vorn ist -Y: dort laeuft der Schuh schmal zu.
            narrow = max(0.0, -math.sin(a)) ** 1.5
            ex *= (1.0 - toe * narrow)

        x = side * dx + ex
        # Innenseite gegen die Mittellinie abflachen; die beiden Beine greifen
        # dabei um CROTCH_OVERLAP ineinander.
        inward = max(0.0, -side * x - CROTCH_OVERLAP)
        x += side * inward * f

        pts.append(Vector((x, cy + ey, z)))
    return pts


def build_legs(hip_centre, hip_profile):
    """Beine und Schuhe als ein Netz mit zwei Materialschlitzen."""
    verts = []
    faces = []
    mat_index = []
    uvs = []          # je Flaeche eine Liste von UV-Paaren

    def add_tube(rings, side, mat, v0, v1, cap_top, cap_bottom):
        base = len(verts)
        n = RING_SEGMENTS
        for spec in rings:
            for p in ring_points(spec, side, hip_profile):
                verts.append(p + hip_centre)
        for r in range(len(rings) - 1):
            for i in range(n):
                j = (i + 1) % n
                a = base + r * n + i
                b = base + r * n + j
                c = base + (r + 1) * n + j
                d = base + (r + 1) * n + i
                faces.append((a, b, c, d))
                mat_index.append(mat)
                u0 = i / float(n)
                u1 = (i + 1) / float(n)
                t0 = v0 + (v1 - v0) * (r / float(len(rings) - 1))
                t1 = v0 + (v1 - v0) * ((r + 1) / float(len(rings) - 1))
                uvs.append([(u0, t0), (u1, t0), (u1, t1), (u0, t1)])
        disc = [(0.5 + 0.45 * math.cos(2.0 * math.pi * i / n),
                 0.5 + 0.45 * math.sin(2.0 * math.pi * i / n)) for i in range(n)]
        if cap_top:
            faces.append(tuple(range(base, base + n)))
            mat_index.append(mat)
            uvs.append(list(disc))
        if cap_bottom:
            last = base + (len(rings) - 1) * n
            faces.append(tuple(range(last + n - 1, last - 1, -1)))
            mat_index.append(mat)
            uvs.append(list(reversed(disc)))

    # Links und rechts in getrennte UV-Haelften, damit das Backen sie nicht
    # uebereinanderschreibt.
    add_tube(LEG_RINGS, +1.0, 0, 0.510, 0.995, cap_top=True, cap_bottom=True)
    add_tube(LEG_RINGS, -1.0, 0, 0.005, 0.490, cap_top=True, cap_bottom=True)
    add_tube(SHOE_RINGS, +1.0, 1, 0.510, 0.995, cap_top=False, cap_bottom=True)
    add_tube(SHOE_RINGS, -1.0, 1, 0.005, 0.490, cap_top=False, cap_bottom=True)

    me = bpy.data.meshes.new("Sebbo_Beine")
    me.from_pydata([tuple(v) for v in verts], [], faces)
    me.update()

    uvlayer = me.uv_layers.new(name="UVMap")
    for pi, poly in enumerate(me.polygons):
        poly.material_index = mat_index[pi]
        poly.use_smooth = True
        for k, li in enumerate(poly.loop_indices):
            uvlayer.data[li].uv = uvs[pi][k]

    obj = bpy.data.objects.new("Sebbo_Beine", me)
    bpy.context.scene.collection.objects.link(obj)
    log("Beine gebaut: %d Ecken, %d Flaechen" % (len(me.vertices), len(me.polygons)))
    return obj


# ---------------------------------------------------------------------------
# 4. Anmalen
# ---------------------------------------------------------------------------

def scan_image(obj):
    mat = obj.data.materials[0] if obj.data.materials else None
    if mat and mat.node_tree:
        for n in mat.node_tree.nodes:
            if n.type == 'TEX_IMAGE' and n.image:
                return n.image
    return None


def srgb_to_linear(c):
    """Bildpunkt in Rechenfarbe umwandeln.

    `Image.pixels` liefert bei einem 8-Bit-Bild den ROHWERT aus dem Speicher,
    nicht die farbverwaltete Helligkeit. Wer ihn direkt als Grundfarbe
    einsetzt, malt rund dreimal zu hell: der erste Durchlauf gab dem Mann eine
    hellgraue Hose, obwohl der Scan an derselben Stelle fast schwarz ist.
    """
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def sample_trouser_colour(obj):
    """Hosenfarbe aus dem Scan selbst holen.

    Eine geratene Farbe faellt sofort auf, weil der Uebergang zur gescannten
    Hose genau auf Augenhoehe des Betrachters liegt. Deshalb wird der
    Bildpunktdurchschnitt am untersten Rand des Scans ausgelesen - und zwar
    nur an nach AUSSEN gerichteten Flaechen. Die nach innen zeigenden liegen
    im Schatten des Mantels und wuerden die Hose schwarz faerben.
    """
    me = obj.data
    img = scan_image(obj)
    if img is None or not me.uv_layers:
        log("keine Scantextur gefunden - Ersatzfarbe")
        return (0.020, 0.017, 0.014)

    w, h = img.size
    px = img.pixels[:]
    mw = obj.matrix_world
    verts = [mw @ v.co for v in me.vertices]
    zmin = min(p.z for p in verts)
    band = [p for p in verts if p.z < zmin + 0.06]
    cx = (min(p.x for p in band) + max(p.x for p in band)) * 0.5
    cy = (min(p.y for p in band) + max(p.y for p in band)) * 0.5
    uvl = me.uv_layers[0].data

    acc = [0.0, 0.0, 0.0]
    n = 0
    for poly in me.polygons:
        centre = Vector((0.0, 0.0, 0.0))
        for vi in poly.vertices:
            centre += verts[vi]
        centre /= len(poly.vertices)
        if centre.z > zmin + 0.06:
            continue
        outward = Vector((centre.x - cx, centre.y - cy, 0.0))
        if outward.length < 1e-5:
            continue
        normal = mw.to_3x3() @ poly.normal
        if normal.xy.dot(outward.xy.normalized()) < 0.25:
            continue
        for loop in poly.loop_indices:
            u, v = uvl[loop].uv
            x = int((u % 1.0) * (w - 1))
            y = int((v % 1.0) * (h - 1))
            i = (y * w + x) * 4
            acc[0] += srgb_to_linear(px[i])
            acc[1] += srgb_to_linear(px[i + 1])
            acc[2] += srgb_to_linear(px[i + 2])
            n += 1
    if n == 0:
        log("kein aussenliegender Hosensaum getroffen - Ersatzfarbe")
        return (0.020, 0.017, 0.014)
    col = tuple(c / n for c in acc)
    log("Hosenfarbe aus dem Scan: linear (%.4f, %.4f, %.4f) aus %d Bildpunkten"
        % (col + (n,)))
    return col


def make_fabric_material(name, base_rgb, roughness, weave_scale, weave_strength,
                         dirt=0.0, bump_strength=0.35):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()

    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs[0], out.inputs[0])

    # Gewebe: feines Rauschen, das die Farbe leicht aufhellt und abdunkelt.
    tex = nt.nodes.new("ShaderNodeTexNoise")
    tex.inputs['Scale'].default_value = weave_scale
    tex.inputs['Detail'].default_value = 8.0
    tex.inputs['Roughness'].default_value = 0.65

    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[1].position = 0.65
    lo = [c * (1.0 - weave_strength) for c in base_rgb]
    hi = [min(1.0, c * (1.0 + weave_strength)) for c in base_rgb]
    ramp.color_ramp.elements[0].color = (lo[0], lo[1], lo[2], 1.0)
    ramp.color_ramp.elements[1].color = (hi[0], hi[1], hi[2], 1.0)
    nt.links.new(tex.outputs['Fac'], ramp.inputs['Fac'])

    colour_out = ramp.outputs['Color']

    if dirt > 0.0:
        # Staub, der von unten hochzieht - Hosenbeine und Schuhe sind unten
        # heller als oben. Ohne das wirkt die Hose wie lackiert.
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        sep = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(geo.outputs['Position'], sep.inputs[0])
        # Die Beine stehen beim Backen noch huftbezogen: die Sohle liegt bei
        # z = -1,00, nicht bei 0. Ein Bereich von 0 bis 0,55 laege komplett
        # UNTER der Figur - dann bekaeme jeder Punkt den vollen Staubanteil
        # und die Hose waere gleichmaessig ausgeblichen.
        maprange = nt.nodes.new("ShaderNodeMapRange")
        maprange.inputs['From Min'].default_value = -1.00
        maprange.inputs['From Max'].default_value = -0.45
        maprange.inputs['To Min'].default_value = dirt
        maprange.inputs['To Max'].default_value = 0.0
        maprange.clamp = True
        nt.links.new(sep.outputs['Z'], maprange.inputs['Value'])

        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = 'RGBA'
        mix.blend_type = 'MIX'
        nt.links.new(maprange.outputs[0], mix.inputs['Factor'])
        nt.links.new(colour_out, mix.inputs[6])
        mix.inputs[7].default_value = (0.055, 0.048, 0.038, 1.0)   # Strassenstaub
        colour_out = mix.outputs[2]

    # Verschattung mit einbacken.
    #
    # Die Fototextur des Scans TRAEGT ihre Verschattung bereits in sich - der
    # Mantel ist dort dunkel, weil er beim Fotografieren im Schatten lag. Eine
    # rechnerisch saubere, aber schattenfreie Hose sitzt daneben wie
    # angeleuchtet: an der Huefte klaffte ein sichtbarer Helligkeitssprung.
    # Deshalb bekommen auch die Beine ihre Eigenverschattung ins Bild.
    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.samples = 8
    # Nicht nur das eigene Netz abtasten: der Torso steht beim Backen noch
    # daneben und soll seinen Schatten auf Huefte und Schritt werfen.
    ao.only_local = False
    ao.inputs['Distance'].default_value = 0.25

    ao_shape = nt.nodes.new("ShaderNodeMapRange")
    ao_shape.inputs['From Min'].default_value = 0.0
    ao_shape.inputs['From Max'].default_value = 1.0
    ao_shape.inputs['To Min'].default_value = 0.45   # voll verschattet
    ao_shape.inputs['To Max'].default_value = 1.0
    ao_shape.clamp = True
    nt.links.new(ao.outputs['AO'], ao_shape.inputs['Value'])

    shade = nt.nodes.new("ShaderNodeMix")
    shade.data_type = 'RGBA'
    shade.blend_type = 'MULTIPLY'
    shade.inputs['Factor'].default_value = 1.0
    nt.links.new(colour_out, shade.inputs[6])
    nt.links.new(ao_shape.outputs[0], shade.inputs[7])
    colour_out = shade.outputs[2]

    nt.links.new(colour_out, bsdf.inputs['Base Color'])
    bsdf.inputs['Metallic'].default_value = 0.0

    # Rauheit mitschwanken lassen. Bliebe sie konstant, waere die gebackene
    # Karte ein einziger Grauwert und die Hose spiegelte wie lackiert.
    rough_ramp = nt.nodes.new("ShaderNodeMapRange")
    rough_ramp.inputs['From Min'].default_value = 0.0
    rough_ramp.inputs['From Max'].default_value = 1.0
    rough_ramp.inputs['To Min'].default_value = max(0.05, roughness - 0.16)
    rough_ramp.inputs['To Max'].default_value = min(1.0, roughness + 0.10)
    rough_ramp.clamp = True
    nt.links.new(tex.outputs['Fac'], rough_ramp.inputs['Value'])
    nt.links.new(rough_ramp.outputs[0], bsdf.inputs['Roughness'])

    # Struktur als Relief.
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs['Strength'].default_value = bump_strength
    bump.inputs['Distance'].default_value = 0.004
    nt.links.new(tex.outputs['Fac'], bump.inputs['Height'])
    nt.links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])

    return mat


def paint_legs(legs, trouser_rgb):
    """Hose und Schuhe einfaerben."""
    # Die gemessene Farbe stammt vom untersten Rand des Scans und traegt dort
    # den Schatten des Mantels als eingebrannte Verdunklung. Ein Aufschlag holt
    # sie auf das Niveau der freistehenden Hosenbeine.
    tr = tuple(min(1.0, c * 1.2) for c in trouser_rgb)
    hose = make_fabric_material("Sebbo_Hose", tr,
                                roughness=0.86, weave_scale=180.0,
                                weave_strength=0.22, dirt=0.20,
                                bump_strength=0.4)
    # Rauheit 0,42 hat den Stiefel HELLER erscheinen lassen als die Hose,
    # obwohl seine Grundfarbe fast schwarz ist: eine glatte dunkle Flaeche
    # spiegelt den Himmel. Mattes Leder gibt das gewuenschte Schwarz.
    schuh = make_fabric_material("Sebbo_Schuhe", (0.018, 0.016, 0.014),
                                 roughness=0.58, weave_scale=90.0,
                                 weave_strength=0.55, dirt=0.14,
                                 bump_strength=0.6)
    legs.data.materials.append(hose)
    legs.data.materials.append(schuh)
    log("Hose: (%.3f, %.3f, %.3f), Schuhe: Leder schwarz" % tr)
    return hose, schuh


def bake_legs(legs, out_dir, size=1024):
    """Die berechneten Oberflaechen in Bilddateien brennen.

    Unreal kann Blenders Knotennetze nicht lesen. Was hier nicht in eine
    Textur gebacken wird, kommt drueben als graue Flaeche an - genau der
    Zustand, in dem die Platzhalterbeine waren.
    """
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 16
    scene.cycles.use_denoising = False
    scene.render.bake.use_selected_to_active = False
    scene.render.bake.margin = 16

    for o in bpy.context.selected_objects:
        o.select_set(False)
    legs.select_set(True)
    bpy.context.view_layer.objects.active = legs

    written = {}
    passes = [
        ("Color", 'DIFFUSE', {'COLOR'}, False),
        ("Roughness", 'ROUGHNESS', set(), True),
        ("Normal", 'NORMAL', set(), True),
    ]

    for pass_name, bake_type, filt, non_color in passes:
        targets = []
        for mat in legs.data.materials:
            nt = mat.node_tree
            img = bpy.data.images.new(
                "%s_%s" % (mat.name, pass_name), size, size,
                alpha=False, float_buffer=False,
                is_data=non_color)
            node = nt.nodes.new("ShaderNodeTexImage")
            node.image = img
            node.select = True
            nt.nodes.active = node
            targets.append((mat, node, img))

        kwargs = dict(type=bake_type, use_clear=True, margin=16)
        if filt:
            kwargs["pass_filter"] = filt
        bpy.ops.object.bake(**kwargs)

        for mat, node, img in targets:
            path = os.path.join(out_dir, "T_%s_%s.png" % (mat.name, pass_name))
            img.filepath_raw = path
            img.file_format = 'PNG'
            img.save()
            written.setdefault(mat.name, {})[pass_name] = path
            mat.node_tree.nodes.remove(node)
            log("gebacken: %s" % os.path.basename(path))

    scene.render.engine = 'BLENDER_EEVEE'
    return written


def wire_baked_materials(legs, written):
    """Die gebackenen Bilder wieder als Material anhaengen.

    Danach zeigt Blender genau das, was Unreal spaeter sieht - und nicht mehr
    das Knotennetz, das dort niemand lesen kann.
    """
    for mat in legs.data.materials:
        maps = written.get(mat.name, {})
        nt = mat.node_tree
        nt.nodes.clear()
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
        nt.links.new(bsdf.outputs[0], out.inputs[0])

        def load(name, non_color):
            path = maps.get(name)
            if not path or not os.path.exists(path):
                return None
            img = bpy.data.images.load(path, check_existing=True)
            if non_color:
                img.colorspace_settings.name = 'Non-Color'
            node = nt.nodes.new("ShaderNodeTexImage")
            node.image = img
            return node

        col = load("Color", False)
        if col:
            nt.links.new(col.outputs['Color'], bsdf.inputs['Base Color'])
        rough = load("Roughness", True)
        if rough:
            nt.links.new(rough.outputs['Color'], bsdf.inputs['Roughness'])
        nrm = load("Normal", True)
        if nrm:
            nmap = nt.nodes.new("ShaderNodeNormalMap")
            nt.links.new(nrm.outputs['Color'], nmap.inputs['Color'])
            nt.links.new(nmap.outputs['Normal'], bsdf.inputs['Normal'])


# ---------------------------------------------------------------------------
# 5. Zusammenfuegen, ausrichten, ausgeben
# ---------------------------------------------------------------------------

def join_and_place(scan, legs, hip_centre):
    """Ein Objekt, Ursprung zwischen den Fuessen, Sohlen auf z = 0."""
    for o in bpy.context.selected_objects:
        o.select_set(False)
    scan.select_set(True)
    legs.select_set(True)
    bpy.context.view_layer.objects.active = scan
    bpy.ops.object.join()
    figure = bpy.context.view_layer.objects.active
    figure.name = "Sebbo"
    figure.data.name = "Sebbo"

    # Ursprung zwischen die Fuesse: waagerecht in die Koerpermitte, senkrecht
    # auf die Sohle. Unreal setzt die Kapsel mit ihrem MITTELPUNKT; ein
    # Modell, dessen Nullpunkt in der Huefte liegt, steckt dort bis zum Bauch
    # im Asphalt.
    shift = Matrix.Translation(
        Vector((-hip_centre.x, -hip_centre.y, -(hip_centre.z + SOLE_Z))))

    # Vierteldrehung in die Blickrichtung von Unreal.
    #
    # Der Scan schaut in Blender nach -Y. Ueber den FBX-Weg wird daraus in
    # Unreal NICHT die Vorwaertsachse: Blenders X landet auf Unreals X, also
    # stand die Figur quer - der Import meldete 41,3 cm in Blickrichtung und
    # 70,3 cm in die Breite, wo die Kettensaege nach vorn zeigen sollte.
    # Nach dieser Drehung schaut sie in Blender nach +X und in Unreal nach
    # vorn.
    turn = Matrix.Rotation(math.radians(90.0), 4, 'Z')

    me = figure.data
    me.transform(turn @ shift)
    me.update()

    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    log("Figur steht: Sohle z=%.4f, Scheitel z=%.4f, Hoehe %.3f m" %
        (min(zs), max(zs), max(zs) - min(zs)))
    log("Grundflaeche: %.3f m in Blickrichtung, %.3f m breit" %
        (max(xs) - min(xs), max(ys) - min(ys)))
    return figure


def export(figure, out_dir):
    img = None
    for mat in figure.data.materials:
        if mat.node_tree:
            for n in mat.node_tree.nodes:
                if n.type == 'TEX_IMAGE' and n.image and n.image.size[0] >= 2048:
                    img = n.image
                    break
    if img is not None:
        path = os.path.join(out_dir, "T_Sebbo_Scan_Color.png")
        img.filepath_raw = path
        img.file_format = 'PNG'
        img.save()
        log("Scantextur gesichert: %s (%dx%d)" % (os.path.basename(path), *img.size))

    blend = os.path.join(out_dir, "Sebbo_Spielermodell.blend")
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    log("Blend gesichert: %s" % blend)

    for o in bpy.context.selected_objects:
        o.select_set(False)
    figure.select_set(True)
    bpy.context.view_layer.objects.active = figure

    fbx = os.path.join(out_dir, "SM_Sebbo.fbx")
    bpy.ops.export_scene.fbx(
        filepath=fbx,
        use_selection=True,
        apply_unit_scale=True,        # Meter -> Zentimeter, wie Unreal es will
        global_scale=1.0,
        apply_scale_options='FBX_SCALE_NONE',
        object_types={'MESH'},
        mesh_smooth_type='FACE',
        use_mesh_modifiers=True,
        bake_space_transform=False,
        axis_forward='-Z',
        axis_up='Y',
        path_mode='COPY',
        embed_textures=False)
    log("FBX geschrieben: %s" % fbx)
    return fbx


def main():
    out_dir = arg_value("--out", os.path.join(os.path.dirname(bpy.data.filepath), "Sebbo"))
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    log("Ausgabe nach %s" % out_dir)

    scan = bpy.data.objects.get(SCAN_NAME)
    if scan is None:
        raise RuntimeError("Objekt '%s' fehlt in der Quelldatei" % SCAN_NAME)

    remove_placeholder_legs()
    # Das Scanmaterial heisst in der Quelldatei "Material.001". Unter diesem
    # Namen taucht es drueben als Materialschlitz auf, und niemand erkennt,
    # wozu er gehoert.
    if scan.data.materials and scan.data.materials[0]:
        scan.data.materials[0].name = "Sebbo_Scan"
    freeze_transform(scan)
    clean_scan(scan)

    trouser = sample_trouser_colour(scan)
    hip_centre, hip_profile = measure_hip(scan)

    legs = build_legs(hip_centre, hip_profile)
    paint_legs(legs, trouser)
    written = bake_legs(legs, out_dir)
    wire_baked_materials(legs, written)

    figure = join_and_place(scan, legs, hip_centre)
    export(figure, out_dir)
    log("FERTIG")


main()
