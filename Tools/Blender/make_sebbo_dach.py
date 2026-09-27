# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Baut die Dachaufbauten des SebboTower parametrisch: Satellitenschuessel,
# Antennenmast und die Dachreklame, dazu der Magazinstaender und die
# Topfpflanze. Alles wird als getrennte FBX exportiert und ein Manifest
# (sebbo_dach.json) beschreibt je Materialslot Grundfarbe und ART, damit der
# Unreal-Import (Tools/import_sebbo_dach.py) weiss, ob ein Slot einen Volltonlack
# oder eine maskierte Bildtextur bekommt.
#
# Lage im Turm (lokale Bauteil-Koordinaten, cm - siehe SebboHqShape.cpp,
# BuildDachaufbauten): Schuessel und Masten stehen auf der Dachflaeche
# (Oberkante Attika) HINTER dem Kern, damit der Anflugkorridor des
# Landeplatzes (+X) frei bleibt. Das Logo-Schild steht auf der Krone und zeigt
# mit +X zur Platter Strasse. Magazinstaender und Topfpflanzen stehen am
# Dachrand (siehe SebboHq::BuildDachaufbauten).
#
# Die Bildvorlagen liegen in Data/Raw/SebboTower/quellen/:
#   sebbo_ag_logo.jpg       -> T_WbSeboLogoAG.png  (linkes Schildfeld)# sebbo_magazin_cover.jpg  -> T_WbSeboMagazin.png (Plakattafel)
#   sebbo_pflanze_01..03.jpg -> Geometrie von build_pflanze()
# Die Texturen erzeugt Tools/make_sebbo_dach_textures.py (Pillow).
#
# Modelliert wird in METERN; der FBX-Export liefert Zentimeter, Unreal
# importiert mit Skalierung 1,0. Die Meshes haben z = 0 am Standfuss.
#
# Aufruf:
#   blender.exe -b -P Tools/Blender/make_sebbo_dach.py -- --out <Ordner>
#
# Headless-Fallen (siehe Tools/Blender/README.md): kein origin_set, keine
# bpy.ops fuer Geometrie - alles ueber direkte Vertexlisten, damit im
# --background-Modus nichts stumm als CANCELLED verworfen wird.

import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

OUT_DIR_DEFAULT = (r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
                   r"\Data\Raw\SebboTower")

# Logo-Texturen (Tools/make_sebbo_dach_textures.py). Werden fuer den
# Kontrollrender gebraucht: ohne das Bild zeigt der Render bloss die dunkle
# Platte, und gerade die Schrift muss geprueft werden (aufrecht, nicht
# gespiegelt).
TEX_DIR_DEFAULT = (r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
                   r"\Content\SebboTower\Textures\Source")


def arg_value(name, default):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if name in argv:
        return argv[argv.index(name) + 1]
    return default


def log(msg):
    print("###WBSD### %s" % msg)


def srgb(hexwert):
    """sRGB-Hex -> lineares RGB.

    Unreal-LinearColor erwartet LINEARE Werte, die Basisfarben im Manifest
    werden unveraendert durchgereicht. Das marineblaue Schildfeld muss exakt
    zum Grundton des Logo-Fotos passen (063b6d), sonst steht ein heller Rand
    um die freigestellte Wortmarke - darum wird der Ton hier gerechnet und
    nicht von Hand eingetragen.
    """
    kanale = [int(hexwert[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]

    def linear(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    return tuple(linear(c) for c in kanale)


# ---------------------------------------------------------------------------
# Materialtabelle: Name -> (Grundfarbe RGB, ART).
#
# ART steuert den Unreal-Import: "paint"/"metal"/"dark" werden zu farbigen
# Materialinstanzen (Metall/Rauheit passend), "foliage" zum ZWEISEITIGEN
# Blattmaterial (Blattflaechen sind einzelne Streifen - Unreal wuerde die
# Rueckseite wegcullen), "decal" bekommt die maskierte Bildtextur.
# ---------------------------------------------------------------------------
MATERIALS = {
    "SbMetall":   ((0.52, 0.53, 0.56), "metal"),    # Mast, Schuesselruecken
    "SbWeiss":    ((0.86, 0.86, 0.84), "paint"),    # Schuesselschale
    "SbAntrazit": ((0.09, 0.09, 0.10), "dark"),     # Schildplatte
    "SbRot":      ((0.62, 0.05, 0.04), "paint"),    # Spitze, Befeuerung
    # Marineblaues Feld des AG-Logos: exakt der Grundton des Fotos.
    "SbNavy":     (srgb("063b6d"), "paint"),
    # Wortmarke und AG-Logo: KEINE Lackflaeche, sondern aufgemalte Graphik -
    # der Unreal-Import baut daraus maskierte, zweiseitige Materialien, damit
    # das Schildfeld zwischen Buchstaben und Formen sichtbar bleibt.
    "SbLogo":     ((1.0, 1.0, 1.0), "decal"),
    "SbLogoAG":   ((1.0, 1.0, 1.0), "decal"),
    # Magazinstaender
    "MgMetall":   ((0.26, 0.27, 0.29), "metal"),    # Rahmen der Tafel
    "MgCover":    ((1.0, 1.0, 1.0), "decal"),       # Titelbild "SeBBo"
    # Topfpflanze
    "PbKuebel":   ((0.34, 0.33, 0.30), "paint"),    # Betonkuebel
    "PbErde":     ((0.045, 0.030, 0.020), "dark"),  # Substrat
    "PbBlatt":    ((0.085, 0.170, 0.040), "foliage"),   # Fächerblaetter
    "PbBlattHell": ((0.230, 0.330, 0.085), "foliage"),  # junge Blaetter
    "PbReif":     ((0.560, 0.600, 0.450), "foliage"),   # Harzreif der Bluete
}

TEXTURES = {"SbLogo": "T_WbSeboLogo.png",
            "SbLogoAG": "T_WbSeboLogoAG.png",
            "MgCover": "T_WbSeboMagazin.png"}


def bind_decal_texture(mat, nt, bsdf, mname):
    """Haengt die Bild-PNG an den Kontrollrender.

    Unreal maskiert dieselbe Datei (Slot `SbLogo`/`SbLogoAG`/`MgCover` im
    Manifest); hier wird sie als Grundfarbe UND Deckkraft angehaengt, damit
    Schrift und Logo im Vorschaubild genauso auf der Platte stehen. Fehlt die
    Datei, laeuft der Bau trotzdem durch - nur das Vorschaubild zeigt dann
    keine Schrift.
    """
    datei = TEXTURES.get(mname)
    pfad = os.path.join(TEX_DIR_DEFAULT, datei) if datei else ""
    if not datei or not os.path.exists(pfad):
        log("Hinweis: %s fehlt - %s ohne Bild im Kontrollrender "
            "(erst Tools/make_sebbo_dach_textures.py laufen lassen)."
            % (pfad or "TEXTURES[%s]" % mname, mname))
        return
    img = bpy.data.images.load(pfad)
    img.colorspace_settings.name = "sRGB"
    node = nt.nodes.new("ShaderNodeTexImage")
    node.image = img
    node.location = (-380, 150)
    # CLIP: das Bild liegt EINMAL auf seiner Flaeche - REPEAT wuerde es
    # ueber den Rand hinaus wiederholen.
    node.extension = "CLIP"
    nt.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
    if "Alpha" in bsdf.inputs:
        nt.links.new(node.outputs["Alpha"], bsdf.inputs["Alpha"])
    for attr, val in (("surface_render_method", "DITHERED"),
                      ("blend_method", "BLEND")):
        try:
            setattr(mat, attr, val)
        except Exception:
            pass


# ---------------------------------------------------------------------------
# MeshBuilder: Vierecke mit Materialslot und planarer UV.
#
# ANDERS als bei make_nerobergbahn.py (dort legt box() die Flaechen nach
# INNEN, Konvention des Wagen-Meshes) sind diese Bauteile EINZELSTEHENDE
# Props: alle Flaechen zeigen nach AUSSEN, damit Unreal sie nicht wegcullt.
# box() wickelt direkt nach aussen; offene Flaechen (Schuesselschale,
# Logo-Flaeche) bekommen ihre Richtung ueber quad_towards().
# ---------------------------------------------------------------------------
class MeshBuilder:
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

    def _face(self, punkte, mat, uvs):
        """Eine Flaeche eintragen - immer ein VIERECK.

        to_object() rechnet damit, dass jede Flaeche vier Ecken und vier UVs
        hat (Flaeche i belegt uvs[i*4 .. i*4+3]); Dreiecke werden als
        entartete Vierecke uebergeben.
        """
        base = len(self.verts)
        for v in punkte:
            self.verts.append((v[0], v[1], v[2]))
        self.faces.append((base, base + 1, base + 2, base + 3))
        self.face_mat.append(self._mat_index(mat))
        self.uvs.extend(uvs)

    def _planar_uv(self, punkte, tpm):
        """Planare UV aus den zwei groessten Achsen der Flaeche mal Kachel-
        dichte (Kacheln je Meter) - unabhaengig von der Flaechengroesse."""
        p = [Vector(v) for v in punkte]
        n = (p[1] - p[0]).cross(p[2] - p[0])
        laengen = (abs(n.x), abs(n.y), abs(n.z))
        dominante = laengen.index(max(laengen))
        if dominante == 0:
            uvs = [(v[1] * tpm, v[2] * tpm) for v in punkte]
        elif dominante == 1:
            uvs = [(v[0] * tpm, v[2] * tpm) for v in punkte]
        else:
            uvs = [(v[0] * tpm, v[1] * tpm) for v in punkte]
        return uvs

    def quad(self, p0, p1, p2, p3, mat, tpm=1.0):
        punkte = (p0, p1, p2, p3)
        self._face(punkte, mat, self._planar_uv(punkte, tpm))

    def quad_towards(self, p0, p1, p2, p3, mat, towards, tpm=1.0):
        """Viereck, gedreht bis die Normale zu `towards` zeigt.

        Fuer offene Flaechen wie die Schuesselschale: die Windung ergibt sich
        aus der Geometrie nicht, die Blickrichtung aber schon.
        """
        punkte = [Vector(v) for v in (p0, p1, p2, p3)]
        n = (punkte[1] - punkte[0]).cross(punkte[2] - punkte[0])
        if n.dot(Vector(towards)) < 0.0:
            punkte = [punkte[0], punkte[3], punkte[2], punkte[1]]
        self._face([tuple(v) for v in punkte], mat,
                   self._planar_uv([tuple(v) for v in punkte], tpm))

    def quad_tex(self, p0, p1, p2, p3, mat):
        """Viereck mit der ganzen Textur als UV 0..1 (Wortmarke, Cover).

        Die vier Ecken werden so uebergeben, wie man sie VON AUSSEN liest:
        unten links, unten rechts, oben rechts, oben links. Die UV-Werte
        gehoeren genau in dieser Reihenfolge - vertauscht man oben und unten,
        steht die Schrift auf dem Kopf (Merksatz aus dem Wagen-Projekt:
        Blender-UV und UE-UV stimmen ueberein, KEIN V-Flip noetig).
        """
        self._face((p0, p1, p2, p3), mat,
                   [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)])

    def box(self, x0, x1, y0, y1, z0, z1, mat, tpm=1.0):
        """Quader, Flaechen NACH AUSSEN (Eigenkonvention, siehe oben)."""
        a = (x0, y0, z0)
        b = (x1, y0, z0)
        c = (x1, y1, z0)
        d = (x0, y1, z0)
        e = (x0, y0, z1)
        f = (x1, y0, z1)
        g = (x1, y1, z1)
        h = (x0, y1, z1)
        self.quad(a, d, c, b, mat, tpm)     # unten, Normal -Z
        self.quad(e, f, g, h, mat, tpm)     # oben, Normal +Z
        self.quad(a, b, f, e, mat, tpm)     # -Y
        self.quad(c, d, h, g, mat, tpm)     # +Y
        self.quad(b, c, g, f, mat, tpm)     # +X
        self.quad(d, a, e, h, mat, tpm)     # -X

    def lathe(self, profil, segmente, mat, mitte=(0.0, 0.0), hinweis=None,
              tpm=1.0):
        """Profil (r, z) um die Z-Achse gedreht.

        r = 0 an den Enden erzeugt die Deckel als Faecher. Die Windung wird
        je Flaeche geprueft: ohne `hinweis` zeigen die Normale nach AUSSEN
        (vom Weg weg), mit `hinweis` in die Richtung des Vektors - fuer die
        Schuesselschale, die nach oben offen ist.
        """
        ringe = []
        for (r, z) in profil:
            ring = []
            for s in range(segmente):
                w = 2.0 * math.pi * s / segmente
                ring.append(Vector((mitte[0] + r * math.cos(w),
                                    mitte[1] + r * math.sin(w), z)))
            ringe.append(ring)
        for i in range(len(profil) - 1):
            for s in range(segmente):
                s2 = (s + 1) % segmente
                p0, p1 = ringe[i][s], ringe[i][s2]
                p2, p3 = ringe[i + 1][s2], ringe[i + 1][s]
                mitte_v = (p0 + p1 + p2 + p3) * 0.25
                if hinweis is not None:
                    ziel = Vector(hinweis)
                else:
                    ziel = mitte_v - Vector(
                        (mitte[0], mitte[1], mitte_v.z))
                    if ziel.length < 1e-9:
                        ziel = Vector((0.0, 0.0, 1.0))
                self.quad_towards(tuple(p0), tuple(p1), tuple(p2), tuple(p3),
                                  mat, ziel, tpm)

    def balken(self, p0, p1, breit, hoehe, mat, quer=(0.0, 0.0, 1.0)):
        """Balken von p0 nach p1 mit rechteckigem Querschnitt.

        `quer` ist der "oben"-Hinweis; faellt er mit der Achse zusammen,
        weicht der Balken auf Y aus.
        """
        p0, p1 = Vector(p0), Vector(p1)
        d = p1 - p0
        if d.length < 1e-9:
            return
        d.normalize()
        q = Vector(quer)
        if abs(d.dot(q)) > 0.99:
            q = Vector((0.0, 1.0, 0.0))
        if abs(d.dot(q)) > 0.99:
            q = Vector((1.0, 0.0, 0.0))
        oben = (q - d * d.dot(q)).normalized()
        seite = d.cross(oben).normalized()
        ecken = []
        for p in (p0, p1):
            for sv, ov in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                ecken.append(p + seite * (sv * breit * 0.5)
                             + oben * (ov * hoehe * 0.5))
        mitte_ges = (p0 + p1) * 0.5

        def flaeche(i0, i1, i2, i3):
            punkte = [ecken[i0], ecken[i1], ecken[i2], ecken[i3]]
            fm = sum(punkte, Vector((0, 0, 0))) * 0.25
            self.quad_towards(tuple(punkte[0]), tuple(punkte[1]),
                              tuple(punkte[2]), tuple(punkte[3]), mat,
                              fm - mitte_ges)

        flaeche(0, 1, 2, 3)      # p0-Seite
        flaeche(7, 6, 5, 4)      # p1-Seite
        flaeche(0, 4, 5, 1)      # unten
        flaeche(2, 6, 7, 3)      # oben
        flaeche(1, 5, 6, 2)      # +seite
        flaeche(3, 7, 4, 0)      # -seite

    def verschmelzen(self, anderer, matrix=None):
        """Ein anderes Mesh mit optionaler Transformation einfuegen."""
        versatz = len(self.verts)
        for v in anderer.verts:
            vv = Vector(v)
            if matrix is not None:
                vv = matrix @ vv
            self.verts.append(tuple(vv))
        for f, fm, uvs in zip(anderer.faces, anderer.face_mat, self._uv_gruppen(anderer)):
            self.faces.append(tuple(i + versatz for i in f))
            self.face_mat.append(fm)
            self.uvs.extend(uvs)
        for name in anderer.mat_names:
            self._mat_index(name)

    def _uv_gruppen(self, anderer):
        for i in range(len(anderer.faces)):
            yield anderer.uvs[i * 4:i * 4 + 4]

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
                rgb, kind = MATERIALS[mname]
                mat = bpy.data.materials.new(mname)
                mat.use_nodes = True
                bsdf = mat.node_tree.nodes.get("Principled BSDF")
                if bsdf is not None:
                    bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
                mat.diffuse_color = (*rgb, 1.0)
                if kind == "decal":
                    bind_decal_texture(mat, mat.node_tree, bsdf, mname)
            me.materials.append(mat)
        obj = bpy.data.objects.new(name, me)
        bpy.context.collection.objects.link(obj)
        me.update()
        return obj


# ---------------------------------------------------------------------------
# Blattwerk (Bildvorlage Data/Raw/SebboTower/quellen/sebbo_pflanze_0*.jpg)
# ---------------------------------------------------------------------------

def blatt_kanten(ursprung, azimut, elevation, laenge, breite, segmente=10,
                 zaehne=5, wicklung=0.35):
    """Die beiden Randkurven eines Blatts und seine Blattnormale.

    Ein Blatt ist ein Streifen aus `segmente` Vierecken: die Mittelrippe laeuft
    ab `ursprung` in Richtung (azimut, elevation), die Halbbreite folgt
    sin(pi * t^0,6) - schmal am Stiel, breit in der Mitte, spitz an der
    Spitze. Genau das ist die Form der Fotos: schmale suessende Spitzen,
    breite Mitte. `wicklung` hebt die Mittelrippe nach +Normale (mit t^2,
    damit die Spitze staerker ausbogen ist als der Ansatz) - die Blaetter
    stehen dadurch nicht flach, sondern gewellt. `zaehne` alterniert die
    Breite und erzeugt so den gezaegelten Rand.
    """
    a = math.radians(azimut)
    e = math.radians(elevation)
    d = Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a),
                math.sin(e)))
    w = Vector((-math.sin(a), math.cos(a), 0.0))
    n = d.cross(w).normalized()
    if n.z < 0.0:
        n = -n                       # Blattoberseite zeigt nach oben
    links, rechts = [], []
    for i in range(segmente + 1):
        t = i / float(segmente)
        p = (Vector(ursprung) + d * (laenge * t)
             + n * (laenge * wicklung * t * t))
        hw = 0.5 * breite * math.sin(math.pi * (t ** 0.6)) * (1.0 - 0.10 * t)
        if zaehne:
            hw *= 1.0 + 0.11 * math.cos(2.0 * math.pi * zaehne * t)
        links.append(tuple(p - w * hw))
        rechts.append(tuple(p + w * hw))
    return links, rechts, n


def blatt(b, mat, **kwargs):
    """Ein Blatt als Streifen; die Normalen zeigen nach oben."""
    links, rechts, n = blatt_kanten(**kwargs)
    for i in range(len(links) - 1):
        b.quad_towards(links[i], rechts[i], rechts[i + 1], links[i + 1],
                       mat, n)
    return n


def faecherblatt(b, mat, ursprung, azimut, laenge, breite=0.055, anzahl=9):
    """Fächerblatt: `anzahl` schmale Blättchen aus einem Punkt.

    Die Fotos zeigen die Hanf-Fächerform - mehrere fingerartige Blaettchen,
    die in der Mitte am laengsten sind. Jedes Blaettchen bekommt eigene
    Spreizung und eigene Neigung, sonst waere der Faecher eine flache
    Scheibe.
    """
    mitte = (anzahl - 1) * 0.5
    for k in range(anzahl):
        t = (k - mitte) / mitte                      # -1 .. +1
        blatt(b, mat,
              ursprung=ursprung,
              azimut=azimut + 35.0 * t,
              elevation=26.0 - 30.0 * abs(t) ** 1.4,  # Seitenblaettchen sinken
              laenge=laenge * (1.0 - 0.30 * abs(t) ** 1.6),
              breite=breite, segmente=7, zaehne=4, wicklung=0.22)


def suessblatt(b, mat, ursprung, azimut, elevation, laenge, breite=0.038):
    """Ein Bluetenblatt (Sugar Leaf): schmal, stark gewellt, nach oben."""
    return blatt(b, mat, ursprung=ursprung, azimut=azimut,
                 elevation=elevation, laenge=laenge, breite=breite,
                 segmente=6, zaehne=3, wicklung=0.55)


# ---------------------------------------------------------------------------
# Bauteile
# ---------------------------------------------------------------------------

# Satellitenschuessel: Oeffnung zum lokalen -X (Talseite im Baukoordinaten-
# system) in rund 30 Grad Elevation - so steht eine deutsche Empfangsschuessel
# (Suedost, flacher Winkel). Die Achse ist ins MESH gebaut, der Actor braucht
# nur Yaw 0.
SCHUESSEL_D = Vector((-math.cos(math.radians(30.0)), 0.0,
                      math.sin(math.radians(30.0))))
SCHUESSEL_R = 0.60                       # Radius der Schale
SCHUESSEL_TIEFE = 0.18                   # Tiefe der Parabel
SCHUESSEL_FOKUS = SCHUESSEL_R * SCHUESSEL_R / (4.0 * SCHUESSEL_TIEFE)
SCHUESSEL_URSPRUNG = Vector((-0.30, 0.0, 1.30))   # Scheitel der Schale


def build_schuessel():
    b = MeshBuilder()

    # Sockel und Ausleger (stehen senkrecht auf der Dachflaeche).
    b.lathe([(0.0, 0.0), (0.24, 0.0), (0.24, 0.10), (0.19, 0.14),
             (0.19, 0.90), (0.0, 0.90)], 20, "SbMetall")
    b.balken((0.0, 0.0, 0.72), tuple(SCHUESSEL_URSPRUNG), 0.10, 0.10,
             "SbMetall")

    # Schale im EIGENEN Builder, dann gedreht an den Ausleger gesetzt.
    schale = MeshBuilder()
    profil = []
    for i in range(7):
        r = SCHUESSEL_R * i / 6.0
        z = r * r / (4.0 * SCHUESSEL_FOKUS)
        profil.append((r, z))
    schale.lathe(profil, 28, "SbWeiss", hinweis=(0.0, 0.0, 1.0))

    # Brennpunkthalter: drei Streben vom Schalenrand zum Feed.
    for s in range(3):
        w = 2.0 * math.pi * s / 3.0 + math.pi / 2.0
        rand = Vector((SCHUESSEL_R * math.cos(w), SCHUESSEL_R * math.sin(w),
                       SCHUESSEL_R * SCHUESSEL_R
                       / (4.0 * SCHUESSEL_FOKUS)))
        schale.balken(tuple(rand), (0.0, 0.0, SCHUESSEL_FOKUS), 0.025, 0.025,
                      "SbMetall")
    # Feed: kleiner Kasten am Brennpunkt.
    f = SCHUESSEL_FOKUS
    schale.box(-0.05, 0.05, -0.05, 0.05, f - 0.08, f + 0.08, "SbMetall")

    dreh = Vector((0.0, 0.0, 1.0)).rotation_difference(SCHUESSEL_D).to_matrix()
    b.verschmelzen(schale, Matrix.Translation(SCHUESSEL_URSPRUNG) @ dreh.to_4x4())
    return b.to_object("SM_WbSeboDachSchuessel"), ["SbMetall", "SbWeiss"]


def build_mast():
    b = MeshBuilder()
    # Sockelplatte, Rohr.
    b.box(-0.28, 0.28, -0.28, 0.28, 0.0, 0.14, "SbMetall")
    b.lathe([(0.0, 0.14), (0.055, 0.14), (0.055, 3.60), (0.0, 3.60)], 14,
            "SbMetall")
    # Drei Querstangen mit Dipolen - Richtfunk-/Fernsehantenne.
    for z, l in ((2.30, 1.30), (2.75, 1.05), (3.20, 0.80)):
        b.box(-0.025, 0.025, -l * 0.5, l * 0.5, z - 0.025, z + 0.025,
              "SbMetall")
        for s in (-1.0, 1.0):
            b.box(-0.02, 0.02, s * l * 0.5 - 0.02, s * l * 0.5 + 0.02,
                  z - 0.22, z + 0.22, "SbMetall")
    # Spitze: rote Tonne mit Befeuerungskasten.
    b.lathe([(0.0, 3.60), (0.045, 3.60), (0.045, 3.85), (0.0, 3.85)], 12,
            "SbRot")
    b.box(-0.06, 0.06, -0.06, 0.06, 3.85, 3.97, "SbRot")
    return b.to_object("SM_WbSeboDachMast"), ["SbMetall", "SbRot"]


# Schildfeld-Masse (Meter, im Mesh y von -2,40 bis 2,40 und z von 0,55 bis
# 1,90). Die Seitenverhaeltnisse muessen zu den Texturmaessen in
# Tools/make_sebbo_dach_textures.py passen (2048x843 = 2,4296 und
# 1200x1087 = 1,1039) - sonst steht die Schrift verzerrt auf dem Schild.
LOGO_FELD_H = 1.35
LOGO_FELD_LINKS = -2.40          # Aussenrand des AG-Feldes
LOGO_FELD_TRENNER = -0.91        # Feldgrenze
LOGO_FELD_RECHTS = 2.40


def build_logo():
    b = MeshBuilder()
    # Zwei Beine, auf denen die Platte auf der Krone steht.
    for s in (-1.0, 1.0):
        b.box(-0.06, 0.06, s * 1.60 - 0.06, s * 1.60 + 0.06, 0.0, 0.55,
              "SbMetall")
    # Platte: 4,80 breit, 1,35 hoch, 0,12 tief. Rueckseite x = 0.
    b.box(0.0, 0.12, -2.40, 2.40, 0.55, 1.90, "SbAntrazit")

    # Linkes Feld: marineblaues Schild 1,49 m breit, 4 cm vor der Platte
    # stehend, weil die freigestellte Wortmarke dort keinen Hintergrund
    # mitbringt (Tools/make_sebbo_dach_textures.py macht nur sie transparent).
    b.box(0.12, 0.16, LOGO_FELD_LINKS, LOGO_FELD_TRENNER, 0.55, 1.90,
          "SbNavy")
    # Wortmarke 1 cm vor der Plattenfront (x = 0,13), von AUSSEN gelesen:
    # unten links, unten rechts, oben rechts, oben links.
    b.quad_tex((0.13, LOGO_FELD_TRENNER + 0.03, 0.55),
               (0.13, LOGO_FELD_RECHTS, 0.55),
               (0.13, LOGO_FELD_RECHTS, 1.90),
               (0.13, LOGO_FELD_TRENNER + 0.03, 1.90), "SbLogo")
    # AG-Logo 1 cm vor dem marineblauen Feld (x = 0,17).
    b.quad_tex((0.17, LOGO_FELD_LINKS, 0.55),
               (0.17, LOGO_FELD_TRENNER, 0.55),
               (0.17, LOGO_FELD_TRENNER, 1.90),
               (0.17, LOGO_FELD_LINKS, 1.90), "SbLogoAG")
    return b.to_object("SM_WbSeboDachLogo"), ["SbMetall", "SbAntrazit", "SbNavy",
                                             "SbLogo", "SbLogoAG"]


# Magazinstaender: Plakattafel mit dem Titelbild des Magazins "SeBBo"
# (T_WbSeboMagazin.png, 650x800 = 0,8125). Die Tafel ist 1,30 m breit und
# 1,60 m hoch - genau der Seitenverhaeltnis des Coverbildes, sonst waere das
# Cover gestaucht. Der Fuss steht nach -X, das Cover zeigt nach +X zur
# Strasse.
MAG_BREITE = 1.30
MAG_HOEHE = 1.60
MAG_Z0 = 0.35


def build_magazin():
    b = MeshBuilder()
    # Grundplatte und zwei Pfosten.
    b.box(-0.06, 0.06, -0.75, 0.75, 0.0, 0.07, "MgMetall")
    for s in (-1.0, 1.0):
        b.box(-0.04, 0.04, s * 0.67 - 0.04, s * 0.67 + 0.04, 0.07,
              MAG_Z0 + MAG_HOEHE, "MgMetall")
    # Tafel, Vorderseite bei x = 0,10.
    b.box(0.0, 0.10, -MAG_BREITE * 0.5, MAG_BREITE * 0.5, MAG_Z0,
          MAG_Z0 + MAG_HOEHE, "MgMetall")
    # Cover 1 cm vor der Tafel, von AUSSEN gelesen.
    b.quad_tex((0.11, -MAG_BREITE * 0.5, MAG_Z0),
               (0.11, MAG_BREITE * 0.5, MAG_Z0),
               (0.11, MAG_BREITE * 0.5, MAG_Z0 + MAG_HOEHE),
               (0.11, -MAG_BREITE * 0.5, MAG_Z0 + MAG_HOEHE), "MgCover")
    # Strebe nach hinten, damit die Tafel nicht kippt.
    b.balken((0.02, 0.0, MAG_Z0 + MAG_HOEHE - 0.15), (-0.50, 0.0, 0.05),
             0.05, 0.05, "MgMetall")
    return b.to_object("SM_WbSebboMagazin"), ["MgMetall", "MgCover"]


# Topfpflanze nach den drei Makro-Fotos: Betonkuebel mit Substrat, Fuenf
# Faechertblaetter aus den unteren Knoten, drei kleinere darueber und die
# Hauptbluete als spindelfoermiger Kern mit zwei Windungen suessender
# Bluetenblaetter. Der Harzreif ist ein eigener heller Slot - er ist auf
# jedem der drei Fotos die hellste Stelle der Bluete.
PFL_BLUETE_Z0 = 0.74
PFL_BLUETE_Z1 = 1.16


def build_pflanze():
    b = MeshBuilder()
    # Kuebel: Boden, konischer Mantel, umlaufender Rand, Innenwand.
    b.lathe([(0.0, 0.0), (0.170, 0.0), (0.175, 0.02), (0.230, 0.40),
             (0.245, 0.40), (0.245, 0.44), (0.210, 0.44), (0.200, 0.41),
             (0.190, 0.38)], 20, "PbKuebel")
    # Substrat als geschlossener Fladen: von unten her waere eine einseitige
    # Scheibe im Kuebel zwar unsichtbar, aber der Import macht daraus eine
    # Konvexhuelle, und die soll keine offene Figur sein.
    b.lathe([(0.0, 0.355), (0.185, 0.370), (0.190, 0.400), (0.0, 0.400)],
            20, "PbErde")
    # Stamm.
    b.lathe([(0.0, 0.40), (0.020, 0.42), (0.016, 0.72), (0.012, 1.02),
             (0.008, 1.14), (0.0, PFL_BLUETE_Z1)], 8, "PbBlatt")

    # Fuenf Faechertblaetter: die grossen, handfoermigen mit sieben bis neun
    # Blaehttchen, wie im linken Bild.
    for az, z, la in ((15.0, 0.47, 0.36), (105.0, 0.50, 0.33),
                      (190.0, 0.47, 0.35), (275.0, 0.52, 0.31),
                      (340.0, 0.49, 0.34)):
        faecherblatt(b, "PbBlatt", (0.0, 0.0, z), az, la)
    # Drei kleinere, hellere Blaetter an den oberen Knoten.
    for az, z, la in ((60.0, 0.62, 0.24), (230.0, 0.66, 0.21),
                      (320.0, 0.63, 0.23)):
        faecherblatt(b, "PbBlattHell", (0.0, 0.0, z), az, la, anzahl=7,
                     breite=0.045)

    # Hauptbluete: spindelfoermiger Kern, drumher zwei Windungen
    # suessender Blaetter, nach oben zum Reif hin kleiner werdend.
    spannen = 8
    spindel = []
    for i in range(spannen + 1):
        t = i / float(spannen)
        spindel.append((0.052 * math.sin(math.pi * t) ** 0.75,
                        PFL_BLUETE_Z0 + (PFL_BLUETE_Z1 - PFL_BLUETE_Z0) * t))
    b.lathe(spindel, 12, "PbReif")
    for i in range(18):
        f = i / 18.0
        a = f * 720.0                       # zwei Windungen
        az = math.radians(a)
        r = 0.052 * math.sin(math.pi * f) ** 0.75
        z = PFL_BLUETE_Z0 + (PFL_BLUETE_Z1 - PFL_BLUETE_Z0) * f
        suessblatt(b, "PbBlattHell",
                   (r * math.cos(az), r * math.sin(az), z),
                   azimut=a, elevation=68.0 - 22.0 * f,
                   laenge=0.115 * (0.45 + 0.55 * math.sin(math.pi * f)))
    return b.to_object("SM_WbSebboPflanze"), ["PbKuebel", "PbErde", "PbBlatt",
                                             "PbBlattHell", "PbReif"]


# ---------------------------------------------------------------------------
# Export + Kontrollrender
# ---------------------------------------------------------------------------

def boden_auf_null(obj):
    """z = 0 an die Unterkante: alle Bauteile stehen mit dem Ursprung
    auf ihrer Standflaeche (Dach oder Krone), so setzt sie der Actor."""
    zs = [v.co.z for v in obj.data.vertices]
    schiebe = min(zs)
    if abs(schiebe) > 1e-9:
        for v in obj.data.vertices:
            v.co.z -= schiebe
        obj.data.update()
    return (max(v.co.x for v in obj.data.vertices)
            - min(v.co.x for v in obj.data.vertices),
            max(v.co.y for v in obj.data.vertices)
            - min(v.co.y for v in obj.data.vertices),
            max(v.co.z for v in obj.data.vertices))


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
        eintrag = {"name": nm, "base_color": [round(c, 4) for c in rgb],
                   "kind": kind}
        if nm in TEXTURES:
            eintrag["texture"] = TEXTURES[nm]
        out.append(eintrag)
    return out


def setup_world():
    world = bpy.data.worlds.new("SbWelt")
    bpy.context.scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    nt.nodes.clear()
    bg = nt.nodes.new("ShaderNodeBackground")
    out = nt.nodes.new("ShaderNodeOutputWorld")
    nt.links.new(bg.outputs[0], out.inputs[0])
    bg.inputs[0].default_value = (0.52, 0.60, 0.72, 1.0)
    bg.inputs[1].default_value = 0.9
    sun = bpy.data.objects.new("SbSonne", bpy.data.lights.new("SbSonne", "SUN"))
    # Die Sonne kommt bewusst von +X her: Schild, Magazintafel und
    # Satellitenschuessel zeigen alle nach +X, und mit der alten Drehung
    # (12 Grad um Y, -40 Grad um Z) lief das Licht an ihnen vorbei - das
    # Schild stand im Bild bei 37,58,96 statt 6,59,109, die gelbe Flamme
    # wurde zu (181,158,84) statt (255,208,92). Ein Kontrollrender, in dem
    # die aufgemalte Grafik matt ist, taugt nicht zur Pruefung.
    #
    # 58 Grad um X kippt den Lichtweg auf -X, 38 Grad um Z schiebt ihn
    # leicht nach -Y (die Seite, von der die Kamera steht).
    sun.data.energy = 2.4
    sun.rotation_euler = (math.radians(58), 0.0, math.radians(38))
    bpy.context.collection.objects.link(sun)


def render_views(obj, size, out_dir, tag):
    sc = bpy.context.scene
    try:
        sc.render.engine = "BLENDER_EEVEE_NEXT"
    except Exception:
        sc.render.engine = "BLENDER_EEVEE"
    # STANDARD statt der Vorgabe AgX: AgX entsaettigt kräftige Farben, und
    # genau um die geht es hier - das Gelb der Flamme, das Rosa des Herzens,
    # das Gelb des Magazin-Feldes "PAGE-20". AgX macht daraus drei graue
    # Flecken, und die Flaechenvergleiche in Tools/check_sebbo_dach_bilder.py
    # koennen dann nichts mehr unterscheiden.
    for attr, wert in (("view_transform", "Standard"), ("look", "None")):
        try:
            setattr(sc.view_settings, attr, wert)
        except Exception:
            pass
    sc.view_settings.exposure = 0.0
    sc.render.resolution_x = 960
    sc.render.resolution_y = 640
    diag = max(size)
    cam_data = bpy.data.cameras.new("SbCam_%s" % tag)
    cam = bpy.data.objects.new("SbCam_%s" % tag, cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    centre = Vector((0.0, 0.0, size[2] * 0.45))
    views = {
        "seite": Vector((0.2 * diag, -2.0 * diag, 0.7 * diag)),
        "drei_viertel": Vector((1.4 * diag, -1.6 * diag, 0.9 * diag)),
    }
    if tag == "schuessel":
        # Stirnansicht in die Schale: die Oeffnung muss zum -X zeigen und die
        # Parabel tief genug sein - von der Seite sieht man beides nicht.
        views["schale"] = Vector((-1.8 * diag, 0.0, 1.0 * diag))
    if tag == "logo":
        # Die Wortmarke ist der Punkt des Bauteils: GERADE von vorn, sonst
        # bleibt unklar, ob die Schrift aufrecht und ungespiegelt steht
        # (180-Grad-Fehler des Wagen-Projekts, dort erst im Spiel sichtbar).
        views["front"] = Vector((2.2 * diag, 0.0, size[2] * 0.5))
    if tag == "magazin":
        # Dasselbe wie beim Logo: das Cover muss aufrecht und ungespiegelt
        # lesbar sein - 1920 px, sonst ist die Titelschrift nur Pixelbrei.
        views["front"] = Vector((2.6 * diag, 0.0, size[2] * 0.5))
    if tag == "pflanze":
        # Nahaussicht auf die Bluete: die Bluetenblaetter sind im
        # Gesamtbild drei Pixel breit.
        views["bluete"] = Vector((0.9 * diag, -1.1 * diag, 0.55 * diag))

    def shoot(vname, cam_pos, ziel, breite=960):
        cam.location = cam_pos
        cam.rotation_euler = (ziel - cam.location).to_track_quat("-Z", "Y").to_euler()
        sc.render.resolution_x = breite
        sc.render.resolution_y = int(breite * 2.0 / 3.0)
        sc.render.filepath = os.path.join(out_dir,
                                          "vorschau_sebo_dach_%s_%s.png"
                                          % (tag, vname))
        bpy.ops.render.render(write_still=True)

    for vname, loc in views.items():
        if vname == "front":
            ziel = (Vector((0.14, 0.0, 1.22)) if tag == "logo"
                    else Vector((0.10, 0.0, size[2] * 0.5)))
            shoot(vname, Vector(loc), ziel, breite=1920)
        elif vname == "bluete":
            shoot(vname, Vector((0.55, -0.70, 1.00)),
                  Vector((0.0, 0.0, 0.98)), breite=1400)
        else:
            shoot(vname, centre + loc, centre)
    bpy.data.objects.remove(cam, do_unlink=True)


def main():
    out_dir = os.path.abspath(arg_value("--out", OUT_DIR_DEFAULT))
    os.makedirs(out_dir, exist_ok=True)

    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    setup_world()

    manifest = {"assets": []}

    builders = [
        ("schuessel", lambda: build_schuessel()),
        ("mast", lambda: build_mast()),
        ("logo", lambda: build_logo()),
        ("magazin", lambda: build_magazin()),
        ("pflanze", lambda: build_pflanze()),
    ]

    for tag, fn in builders:
        obj, mat_names = fn()
        size = boden_auf_null(obj)
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
        log("%-10s %-24s %s m  Flaechen %d  Slots %s"
            % (tag, obj.name, [round(s, 2) for s in size],
               len(obj.data.polygons), mat_names))
        obj.hide_render = True

    with open(os.path.join(out_dir, "sebbo_dach.json"), "w",
              encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    log("FERTIG: %d Assets nach %s" % (len(manifest["assets"]), out_dir))


main()
