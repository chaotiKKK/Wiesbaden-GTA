# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Baut das Nerobergbahn-Ensemble parametrisch: zwei Wagen (identisches Mesh),
# Tal- und Bergstation im Genzmer-Historismus und den fuenfbogigen
# Backstein-Viadukt. Alles wird als getrennte FBX exportiert und ein Manifest
# (nerobergbahn.json) beschreibt je Materialslot Grundfarbe und ART, damit der
# Unreal-Import (Tools/import_nerobergbahn.py) weiss, ob ein Slot einen
# Volltonlack oder eine gekachelte AAA-Backstein-/Putz-Textur bekommt.
#
# Reale Vorlage (Frame-Auswertung 2026, Quellen/nerobergbahn-video/):
#   - Wasserballast-Standseilbahn von 1888, Oberbau 1962 erneuert.
#   - Lackierung: Narzissengelb / Goldgelb mit BLAUER Liniengraphik (Dachkanten-
#     band, Fensterumrandungen, Zierbänder) und blauem Schriftzug "Nerobergbahn".
#     Dachflaeche gelb-ocker, Untersicht creme.
#   - KEIN Stufenwagen: der Wagenkasten ist ein gerader Kasten PARALLEL zur
#     Trasse (im Standbild liegt die Fensterbanklinie exakt in der Neigung der
#     Strecke, waehrend Mauerkrone und Strassenkante waagerecht bleiben).
#     Der Wagen hat offene Endbuehnen, ein schwarzes Untergestell mit
#     CREMEFARBENEN Streben und oxidroten Radsaetzen, Zahnrad + Bremstrommeln
#     auf der Wagenmitte, seitlich die Wasserstandsskala (10/20/30/40).
#   - Strecke 438,5 m, Spurweite 1000 mm, unten der Backstein-Viadukt,
#     Stationen aus Holz, Gusseisen und Backstein.
#   - Typenschild "Wagen 2 / Leergewicht 8100 kg / Hauptuntersuchung 03.2025".
#
# Siehe Quellen/nerobergbahn-video/NACHBAU-REFERENZ.md (Frame-Katalog,
# Farbtabelle, Maße) - dort stehen die Belegframes zu jeder Angabe.
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
from mathutils import Matrix, Vector

OUT_DIR_DEFAULT = ("C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/"
                   "Data/Raw/Nerobergbahn")

# Schriftzug und Wasserstandsskala als PNG (Tools/make_nerobergbahn_textures.py).
# Wird nur fuer den Kontrollrender gebraucht: ohne das Bild zeigt der Render
# bloss die blaue Grundfarbe des Slots, und gerade der Schriftzug muss geprueft
# werden (aufrecht, nicht gespiegelt, an der richtigen Stelle).
TEX_DIR_DEFAULT = ("C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/"
                   "Content/Nerobergbahn/Textures/Source")


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
    "NbBlau":      ((0.015, 0.110, 0.380), "paint"),
    "NbGelb":      ((0.850, 0.400, 0.020), "paint"),
    "NbCreme":     ((0.895, 0.870, 0.780), "paint"),
    # Wagen-Ergaenzungen aus der Frame-Auswertung (NACHBAU-REFERENZ.md, §3/§8):
    "NbDachOcker": ((0.700, 0.350, 0.023), "paint"),   # Dachflaeche gelb-ocker
    "NbOxidrot":   ((0.200, 0.050, 0.030), "paint"),   # Radsaetze, Bremstrommeln
    "NbBodenRot":  ((0.160, 0.025, 0.030), "paint"),   # Innenboden (oxblood)
    "NbBank":      ((0.760, 0.680, 0.550), "timber"),  # helle Holzbaenke
    # Schriftzug und Wasserstandsskala sind KEINE Lackflaechen, sondern
    # aufgemalte Graphik: der Unreal-Import baut daraus ein maskiertes,
    # zweiseitiges Material, damit der gelbe Kasten zwischen den Buchstaben
    # sichtbar bleibt (sonst waere es ein gelber Aufkleber mit blauem Rand).
    "NbSchrift":   ((0.015, 0.110, 0.380), "decal"),
    "NbSkala":     ((0.015, 0.110, 0.380), "decal"),
    # Innenraum (NACHBAU-REFERENZ.md §4): der Buehnenboden ist im Vorbild
    # dunkles Riffelblech/gummiert (key/840, key/313) - der helle Stahl von
    # NbMetall waere dort falsch.
    "NbBuehne":    ((0.055, 0.058, 0.062), "metal"),   # anthrazit Buehnenboden
    # Tachoscheibe und Riffelmuster sind wie Schriftzug und Skala aufgemalte
    # Graphik (maskiert, zweiseitig): die runde Tachoscheibe entsteht so ohne
    # Kreismodell, das Bodenmuster liegt als duenne Flaeche auf dem Blech.
    "NbTacho":     ((0.030, 0.032, 0.035), "decal"),
    "NbBuehneMuster": ((0.090, 0.094, 0.100), "decal"),
    "NbRot":       ((0.500, 0.010, 0.010), "paint"),   # Signallampe am Wagenende
    # Zeiger der Geschwindigkeitsanzeige: im Vorbild heller Stahl, deutlich
    # heller als die dunkle Tachoscheibe (key/884).
    "NbZeiger":    ((0.780, 0.790, 0.800), "metal"),
    "NbGlas":      ((0.030, 0.055, 0.080), "glass"),
    "NbSchwarz":   ((0.022, 0.022, 0.024), "dark"),
    "NbMetall":    ((0.520, 0.530, 0.560), "metal"),
    "NbBackstein": ((0.420, 0.200, 0.150), "brick"),
    "NbPutz":      ((0.795, 0.760, 0.660), "plaster"),
    "NbHolz":      ((0.230, 0.135, 0.070), "timber"),
    "NbDach":      ((0.400, 0.140, 0.100), "roof"),
    "NbStein":     ((0.615, 0.575, 0.485), "stone"),
    # Trasse (Abschnitt 4): Schienenstahl, oxidrote Zahnstange, dunkler
    # Seilkanal-Trog, verzinkter Rost, Schotter.
    "NbSchiene":     ((0.285, 0.296, 0.310), "metal"),
    "NbZahnstange":  ((0.225, 0.115, 0.075), "metal"),
    "NbSeilkanal":   ((0.110, 0.110, 0.120), "metal"),
    "NbRost":        ((0.330, 0.335, 0.345), "metal"),
    "NbSchotter":    ((0.255, 0.240, 0.215), "gravel"),
    # Bahnsteighalle (Abschnitt 2): oxidrot lackiertes Gusseisen mit Halsringen
    # (RAL 3009/8012, §6.2), dunkle Holzbinder, weisse Balustrade. Rautengitter
    # und Rostabdeckung sind ausgeschnittene Graphik wie Schriftzug und Skala -
    # ein Rautengitter als Geometrie waere ein Vielfaches an Dreiecken, und als
    # geschlossene duenne Platte bleibt die Windung des Objekts eindeutig.
    "NbGusseisen":      ((0.290, 0.075, 0.050), "metal"),
    "NbHolzDunkel":     ((0.105, 0.070, 0.050), "timber"),
    "NbWeiss":          ((0.855, 0.850, 0.840), "paint"),
    "NbRautengitter":   ((0.855, 0.850, 0.840), "decal"),
    "NbGitterrost":     ((0.330, 0.335, 0.345), "decal"),
}
MATERIAL_ORDER = list(MATERIALS.keys())

# Texturen fuer die Decal-Slots (Schriftzug, Skala). Die Dateien erzeugt
# Tools/make_nerobergbahn_textures.py nach
# WiesbadenReal/Content/Nerobergbahn/Textures/Source/; der UE-Import zieht sie
# von dort nach /Game/Nerobergbahn/Textures. Bildoberseite = Oberkante der
# Flaeche, also von aussen gelesen aufrecht.
TEXTURES = {
    "NbSchrift": "T_WbNbSchrift.png",
    "NbSkala":   "T_WbNbSkala.png",
    "NbTacho":   "T_WbNbTacho.png",
    "NbBuehneMuster": "T_WbNbBuehne.png",
    "NbRautengitter": "T_WbNbRautengitter.png",
    "NbGitterrost":   "T_WbNbGitterrost.png",
}

# Metall/Rauheit je Decal-Slot (Vorgabe 0,05/0,45). Die Tachoscheibe sitzt
# unter einer Glasscheibe, das Riffelmuster ist gummiert-rau.
# Decal-Slots mit MUSTER (wiederholen sich ueber die Flaeche) statt einer
# einzelnen Graphik - siehe bind_decal_texture.
TILED_DECALS = {"NbRautengitter", "NbGitterrost"}

DECAL_PBR = {
    "NbTacho":        (0.05, 0.25),
    "NbBuehneMuster": (0.15, 0.72),
    # Weiss lackiertes Gitter, verzinkter Laufrost.
    "NbRautengitter": (0.00, 0.55),
    "NbGitterrost":   (0.55, 0.42),
}

# Mittlere Steigung der Trasse: 26 % in der Streckenmitte, 15 % am Viadukt,
# im Mittel 19,5 % (ESWE/Wikipedia). Der Wagen liegt im Vorbild PARALLEL zur
# Trasse; weil der Actor im Spiel nur giert, steckt diese Neigung im Mesh.
GRADE = 0.195

# ---------------------------------------------------------------------------
# Bezugsmass des Wagenkastens und Ankerpunkte der BEWEGLICHEN Teile.
#
# Diese Zahlen standen frueher als lokale Groessen in build_wagen() und als
# Kommentare daneben ("# 1.95: Kastenende") - der Kommentar war falsch und
# eine Pruefung, die ihn abschrieb, damit ebenfalls. Hier stehen sie einmal,
# und Pruefskript, Actor und Bauteile lesen sie von hier.
#
# Koordinaten im BAU-System des Wagens (ungekippt): X laengs (Bergende +X),
# Y quer, Z hoch, Ursprung auf der Schienenkontaktlinie in Wagenmitte.
# ---------------------------------------------------------------------------
WAGEN_L = 5.40                       # Gesamtlaenge ueber Puffer
WAGEN_PERRON = 0.85                  # offene Endbuehne
WAGEN_X_KAB = WAGEN_L * 0.5 - WAGEN_PERRON   # 1.85 Kastenende
WAGEN_X_ENDE = WAGEN_L * 0.5                 # 2.70 Wagenende

# Geschwindigkeitsanzeige: Mitte der schraeg stehenden Platte im Gang.
TACHO_MITTE = (1.70, 0.06, 1.62)
# Wasserstandsschauglas: Bahn des Schwimmers von der Skalenmarke 10 (unten)
# bis 40 (oben), gemessen an der Glasblende (key/840). Das X ist die MITTE
# des Glasrohrs - Blende und Wand liegen dahinter (siehe build_wagen).
#
# Die Hoehe liegt ganz im FREIEN Wandband innen (1,34..2,56 m): darunter
# verkleidet die Innenwand (NbBank) die Stirnwand 6 cm stark, ein Decal oder
# Rohr waere dort in der Verkleidung verschwunden (erster Anlauf: Rohr ab
# 1,16 m, das untere Drittel steckte in der Verkleidung).
SCHWIMMER_UNTEN = (1.751, 0.68, 1.40)
SCHWIMMER_OBEN = (1.751, 0.68, 1.90)
# Handkurbel fuer Wasserschieber und Bremse: Lagerzapfen (Drehachse quer zum
# Wagen, deshalb bleibt die Achse bei der Neigung unveraendert Y).
KURBEL_ACHSE = (1.72, -0.63, 1.22)
KURBEL_LAENGE = 0.30                 # Arm bis zum roten Griff

# Die drei Teile werden vom Actor bewegt (siehe WiesbadenNerobergbahn.cpp) und
# liegen deshalb als EIGENE Meshes mit dem Drehpunkt im Ursprung vor. Der
# Actor setzt sie mit derselben Neigung wie das Wagen-Mesh an den Anker.
#
# ACHTUNG: diese Werte stehen auch im C++ (WiesbadenNerobergbahn.cpp,
# CarTachoAnchor/CarSchwimmerUnten/CarSchwimmerOben/CarKurbelachse) - wer hier
# dreht, muss dort nachziehen. Der Pruefer vergleicht beide Seiten.
DETAIL_ANKER = {
    "SM_WbNbTachoZeiger": TACHO_MITTE,
    "SM_WbNbSchwimmer": ((SCHWIMMER_UNTEN[0], SCHWIMMER_UNTEN[1],
                          (SCHWIMMER_UNTEN[2] + SCHWIMMER_OBEN[2]) * 0.5)),
    "SM_WbNbKurbel": KURBEL_ACHSE,
}


def bind_decal_texture(mat, nt, bsdf, mname):
    """Haengt die PNG des Schriftzugs/der Skala an den Kontrollrender.

    Unreal maskiert dieselbe Datei (Slot `NbSchrift`/`NbSkala` im Manifest);
    hier wird sie als Grundfarbe UND Deckkraft angehaengt, damit die Schrift
    im Vorschaubild genauso auf dem gelben Kasten steht. Fehlt die Datei,
    laeuft der Bau trotzdem durch - nur das Vorschaubild zeigt dann keine
    Schrift (der Slot bleibt die blaue Grundfarbe).
    """
    datei = TEXTURES.get(mname)
    pfad = os.path.join(TEX_DIR_DEFAULT, datei) if datei else ""
    if not datei or not os.path.exists(pfad):
        log("Hinweis: %s fehlt - %s ohne Schriftzug im Kontrollrender "
            "(erst Tools/make_nerobergbahn_textures.py laufen lassen)."
            % (pfad or "TEXTURES[%s]" % mname, mname))
        return
    img = bpy.data.images.load(pfad)
    img.colorspace_settings.name = "sRGB"
    node = nt.nodes.new("ShaderNodeTexImage")
    node.image = img
    node.location = (-380, 150)
    # Schriftzug und Skala liegen EINMAL auf ihrer Flaeche: CLIP, damit nichts
    # ueber den Rand hinaus wiederholt wird (EXTEND wuerde die Randpixel der
    # Schrift ziehen). Rautengitter und Rost sind dagegen MUSTER, die sich ueber
    # die Flaeche wiederholen - mit CLIP war nur die erste Kachel zu sehen, der
    # Rest des Feldes blieb leer (im Kontrollbild aufgefallen, nicht im Spiel:
    # der UE-Sampler wiederholt die Textur von sich aus).
    node.extension = "REPEAT" if mname in TILED_DECALS else "CLIP"
    nt.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
    if "Alpha" in bsdf.inputs:
        nt.links.new(node.outputs["Alpha"], bsdf.inputs["Alpha"])
    # Der Grund der Datei ist durchsichtig - der gelbe Kasten muss zwischen
    # den Buchstaben durchscheinen, sonst waere es ein gelber Aufkleber.
    for attr, val in (("surface_render_method", "DITHERED"),
                      ("blend_method", "BLEND")):
        try:
            setattr(mat, attr, val)
        except Exception:
            pass


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

    def _face(self, punkte, mat, uvs):
        """Eine Flaeche eintragen - immer ein VIERECK.

        to_object() rechnet damit, dass jede Flaeche vier Ecken und vier UVs
        hat (Flaeche i belegt uvs[i*4 .. i*4+3]); prism_x/prism_y uebergeben
        Dreiecke deshalb als entartete Vierecke.
        """
        base = len(self.verts)
        for v in punkte:
            self.verts.append((v.x, v.y, v.z))
        self.faces.append((base, base + 1, base + 2, base + 3))
        self.face_mat.append(self._mat_index(mat))
        self.uvs.extend(uvs)

    def quad_tex(self, p0, p1, p2, p3, mat):
        """Viereck mit der ganzen Textur als UV 0..1 (Schriftzug, Skala).

        Die vier Ecken werden so uebergeben, wie man sie VON AUSSEN liest:
        unten links, unten rechts, oben rechts, oben links. Damit zeigt die
        Normale nach aussen (anders als bei box(), siehe README) und das Bild
        steht aufrecht.

        Die UV-Zuordnung muss man dabei EINMAL richtig herum aufschreiben:
        (0,0) ist die UNTERE linke Bildecke. Vertauscht man oben und unten,
        steht die Schrift auf dem Kopf (am Kontrollrender gesehen und
        korrigiert) - die vier UV-Werte unten gehoeren deshalb genau in
        dieser Reihenfolge zu unten-links/unten-rechts/oben-rechts/oben-links.

        Warum nicht quad(): dessen UV ist planar im Weltmass (1 Kachel je
        Meter), fuer einen Schriftzug also unbrauchbar - der waere nach dem
        ersten Meter abgeschnitten.
        """
        self._face([Vector(p0), Vector(p1), Vector(p2), Vector(p3)], mat,
                   [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)])

    def quad_tex_towards(self, p0, p1, p2, p3, mat, towards):
        """quad_tex mit vorgegebener Normalenrichtung.

        Fuer aufgemalte Flaechen, deren Ecken nicht aus `aufgemalt()` kommen -
        z. B. das Riffelmuster auf dem Buehnenboden. Zeigt die Windung der
        uebergebenen Ecken nicht in Richtung `towards`, wird die Reihenfolge
        gedreht; die UV-Werte wandern mit ihrer Ecke mit, das Bild bleibt also
        aufrecht (anders als bei einer Spiegelung).
        """
        p = [Vector(p0), Vector(p1), Vector(p2), Vector(p3)]
        uv = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
        if (p[1] - p[0]).cross(p[2] - p[0]).dot(Vector(towards)) < 0:
            p.reverse()
            uv.reverse()
        self._face(p, mat, uv)

    def quad(self, p0, p1, p2, p3, mat, tpm=1.0):
        p = [Vector(p0), Vector(p1), Vector(p2), Vector(p3)]
        uvs = []

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
            uvs.append((v[au] * tpm, v[av] * tpm))
        self._face(p, mat, uvs)

    def quad_towards(self, p0, p1, p2, p3, mat, towards, tpm=1.0):
        """Viereck, dessen Normale in Richtung `towards` zeigt.

        Fuer Flaechen, die NICHT aus box() stammen (Schotter-Schultern,
        Trapezstirn). Die Windung wird gegen die gewuenschte Richtung geprueft
        und notfalls gedreht - dieselbe Idee wie in prism_y, nur mit
        vorgegebener Zielrichtung statt radial.
        """
        p = [Vector(p0), Vector(p1), Vector(p2), Vector(p3)]
        n = (p[1] - p[0]).cross(p[2] - p[0])
        if n.dot(Vector(towards)) < 0:
            p.reverse()
        self.quad(*p, mat, tpm)

    def orient_outward(self):
        """Dreht alle Flaechen so, dass die Normalen nach AUSSEN zeigen.

        box() legt seine sechs Flaechen mit der Normalen nach innen an (siehe
        Tools/Blender/README.md). Fuer den Wagen war das hinnehmbar, fuer die
        Trasse mit ihren tausenden sichtbaren Einzelflaechen ist es das nicht:
        Unreal-Materialien sind einseitig, von aussen gewickelt ist die
        sichere Seite.

        Geprueft wird ueber das vorzeichenbehaftete Volumen (Summe der
        Tetraeder ueber den Ursprung): positiv = nach aussen. Bei einem Set
        getrennter Kaesten ist die Summe der Einzelvolumina eindeutig, solange
        ALLE Flaechen derselben Konvention folgen - deshalb wird diese Funktion
        nur fuer Bauteile benutzt, die ausschliesslich aus box() bestehen.
        """
        vol = 0.0
        for face in self.faces:
            pts = [Vector(self.verts[i]) for i in face]
            for k in range(1, len(pts) - 1):
                vol += pts[0].dot(pts[k].cross(pts[k + 1])) / 6.0
        if vol >= 0.0:
            return vol
        for i, face in enumerate(self.faces):
            self.faces[i] = tuple(reversed(face))
            u = self.uvs[i * 4:i * 4 + 4]
            self.uvs[i * 4:i * 4 + 4] = list(reversed(u))
        return -vol

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

    def prism_y(self, y0, y1, section, mat, tpm=1.0):
        """Prisma entlang Y mit Querschnitt in der XZ-Ebene (Liste von (x,z)).

        Fuer Radsaetze, Zahnrad und Bremstrommeln - Scheiben quer zur
        Fahrtrichtung. Die Windung wird je Flaeche gegen den Prismenmittelpunkt
        geprueft und auf die Konvention von box() gedreht (dort zeigen die
        Normalen nach INNEN, siehe Tools/Blender/README.md) - ein einzelnes
        Bauteil mit umgekehrter Windung wuerde sonst je nach FBX-Import anders
        beleuchtet als der Rest des Wagens.
        """
        n = len(section)
        a = [(x, y0, z) for (x, z) in section]
        c = [(x, y1, z) for (x, z) in section]
        mitte = Vector((sum(p[0] for p in section) / n,
                        (y0 + y1) * 0.5,
                        sum(p[1] for p in section) / n))

        def dreh(pts, soll):
            """Dreht das Viereck so, dass seine Normale in Richtung soll zeigt.

            Deckel und Mantel brauchen unterschiedliche Sollrichtungen: der
            Mantel zeigt radial nach innen (Richtung Prismenmitte), die Deckel
            liegen in der Ebene y = const, ihre Normale ist also +-Y.
            """
            p0, p1, p2 = Vector(pts[0]), Vector(pts[1]), Vector(pts[2])
            if (p1 - p0).cross(p2 - p0).dot(soll) < 0:
                return list(reversed(pts))
            return pts

        for i in range(n):
            j = (i + 1) % n
            quad = [a[i], c[i], c[j], a[j]]
            p0, p1, p2 = Vector(quad[0]), Vector(quad[1]), Vector(quad[2])
            radial = (p0 + p1 + p2 + Vector(quad[3])) * 0.25 - mitte
            self.quad(*dreh(quad, -radial), mat, tpm)
        for i in range(1, n - 1):
            self.quad(*dreh([a[0], a[i], a[i + 1], a[i]], Vector((0, 1, 0))), mat, tpm)
            self.quad(*dreh([c[0], c[i + 1], c[i], c[i]], Vector((0, -1, 0))), mat, tpm)

    def prism_z(self, z0, z1, section, mat, tpm=1.0):
        """Prisma entlang Z mit Querschnitt in der XY-Ebene (Liste von (x,y)).

        Fuer senkrecht stehende Rundkoerper - der Schwimmer im Schauglas ist
        im Vorbild ein Ball, der im Glasrohr nach oben und unten wandert, und
        die Kurbelwelle steht quer. Windung wie prism_y und box(): nach INNEN,
        damit orient_outward() das ganze Bauteil einheitlich drehen kann.
        """
        n = len(section)
        a = [(x, y, z0) for (x, y) in section]
        c = [(x, y, z1) for (x, y) in section]
        mitte = Vector((sum(p[0] for p in section) / n,
                        sum(p[1] for p in section) / n,
                        (z0 + z1) * 0.5))

        def dreh(pts, soll):
            p0, p1, p2 = Vector(pts[0]), Vector(pts[1]), Vector(pts[2])
            if (p1 - p0).cross(p2 - p0).dot(soll) < 0:
                return list(reversed(pts))
            return pts

        for i in range(n):
            j = (i + 1) % n
            quad = [a[i], c[i], c[j], a[j]]
            p0, p1, p2 = Vector(quad[0]), Vector(quad[1]), Vector(quad[2])
            radial = (p0 + p1 + p2 + Vector(quad[3])) * 0.25 - mitte
            self.quad(*dreh(quad, -radial), mat, tpm)
        for i in range(1, n - 1):
            self.quad(*dreh([a[0], a[i], a[i + 1], a[i]], Vector((0, 0, -1))), mat, tpm)
            self.quad(*dreh([c[0], c[i + 1], c[i], c[i]], Vector((0, 0, 1))), mat, tpm)

    def tilt_grade(self, grade):
        """Dreht alle Punkte um die Y-Achse, sodass die Schienenkontaktlinie
        der Steigung folgt (Vorbild: der Wagenkasten liegt parallel zur
        Trasse). Der Actor giert nur, kippt nie - die Neigung MUSS also im
        Mesh stecken, sonst schwebt der Wagen ueber der Strecke.

        Die UVs werden nicht nachgefuehrt: der Wagen besteht nur aus
        Volltonfarben, planare UVs bleiben dafuer gueltig.
        """
        theta = math.atan(grade)
        ca, sa = math.cos(theta), math.sin(theta)
        for i, (x, y, z) in enumerate(self.verts):
            self.verts[i] = (x * ca - z * sa, y, x * sa + z * ca)

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
# 1. Wagen
# ---------------------------------------------------------------------------

def build_wagen():
    b = MeshBuilder()

    # -- Hauptmaße (Belege: NACHBAU-REFERENZ.md §3, key/806, key/851) -------
    L = WAGEN_L              # Gesamtlaenge ueber Puffer
    W = 2.10                 # Kastenbreite (Vorbild ~ 2 x Spurweite 1000 mm)
    hw = W * 0.5
    perron = WAGEN_PERRON    # offene Endbuehne
    x_kab = WAGEN_X_KAB      # 1.85: Kastenende / Anfang der Buehne
    x_ende = WAGEN_X_ENDE    # 2.70: Wagenende

    floor = 0.85             # Bodenhoehe ueber Schienenkontakt
    bruest = floor + 0.55    # 1.40 Fensterbank / Oberkante Bruestung
    f_top = bruest + 1.10    # 2.50 Oberkante Fensterband
    traufe = f_top + 0.22    # 2.72 Dachunterkante

    steg = 0.16              # gelber Steg zwischen zwei Fenstern
    n_win = 4                # vier Fenster je Seite (zwei Paare)
    win_w = (2.0 * x_kab - (n_win + 1) * steg) / n_win

    def wand(ys, x0, x1, z0, z1, mat, versatz=0.0, tiefe=0.05):
        """Wandstueck auf der Seite ys (+1/-1): aussen bei hw-versatz."""
        ya = ys * (hw - versatz)
        yb = ys * (hw - versatz - tiefe)
        b.box(x0, x1, min(ya, yb), max(ya, yb), z0, z1, mat)

    def rahmen_blau(x0, x1, z0, z1, y_aussen, br=0.06):
        """Blauer Fensterrahmen, flach vor der Aussenwand."""
        y0, y1 = y_aussen, y_aussen + 0.012
        b.box(x0, x1, min(y0, y1), max(y0, y1), z0, z0 + br, "NbBlau")
        b.box(x0, x1, min(y0, y1), max(y0, y1), z1 - br, z1, "NbBlau")
        b.box(x0, x0 + br, min(y0, y1), max(y0, y1), z0, z1, "NbBlau")
        b.box(x1 - br, x1, min(y0, y1), max(y0, y1), z0, z1, "NbBlau")

    def kreis(cx, cz, r, n=16):
        return [(cx + r * math.cos(2.0 * math.pi * k / n),
                 cz + r * math.sin(2.0 * math.pi * k / n)) for k in range(n)]

    def aufgemalt(ys, x0, x1, z0, z1, mat):
        """Aufgemalte Graphik flach auf der Aussenwand (Textur-Slot).

        1 cm vor der Wand, damit die blaue Schrift sauber davor liegt; die
        Flaeche selbst ist maskiert (transparent bis auf die Graphik), der
        gelbe Kasten bleibt also ringsum sichtbar. Die Ecken werden je Seite
        so geordnet, dass die Schrift auf BEIDEN Seiten aufrecht steht.
        """
        y = ys * (hw + 0.010)
        if ys > 0:
            b.quad_tex((x1, y, z0), (x0, y, z0), (x0, y, z1), (x1, y, z1), mat)
        else:
            b.quad_tex((x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1), mat)

    # -- Untergestell: schwarzer Rahmen, cremefarbene Streben ---------------
    uf_x, uf_y = 2.60, 0.95
    for ys in (-1, 1):
        b.box(-uf_x, uf_x, ys * (uf_y - 0.08), ys * uf_y, 0.55, floor, "NbSchwarz")
    for xs in (-1, 1):
        b.box(xs * (uf_x - 0.12), xs * uf_x, -uf_y, uf_y, 0.55, floor, "NbSchwarz")
    for cx in (-1.20, 0.0, 1.20):
        b.box(cx - 0.06, cx + 0.06, -uf_y, uf_y, 0.60, floor - 0.03, "NbSchwarz")
    # Die hellbeigen Laengstraeger/Stehbleche sind im Bild der auffaelligste
    # Teil des Untergestells (key/950).
    for ys in (-1, 1):
        b.box(-1.35, 1.35, ys * (uf_y - 0.10), ys * (uf_y - 0.01), 0.36, 0.50, "NbCreme")
    for ax in (-1.62, 1.62):
        for ys in (-1, 1):
            b.box(ax - 0.05, ax + 0.05, ys * (uf_y - 0.11), ys * uf_y, 0.40, floor, "NbCreme")

    # -- Radsaetze (Spurweite 1000 mm), Zahnrad + Bremstrommeln -------------
    for ax in (-1.62, 1.62):
        b.box(ax - 0.05, ax + 0.05, -0.78, 0.78, 0.26, 0.36, "NbSchwarz")
        for ys in (-1, 1):
            wy = ys * 0.50
            b.prism_y(wy - 0.04, wy + 0.04, kreis(ax, 0.31, 0.31), "NbOxidrot")
            b.prism_y(wy - ys * 0.075, wy - ys * 0.04,
                      kreis(ax, 0.31, 0.345), "NbOxidrot")      # Spurkranz innen
    b.prism_y(-0.055, 0.055, kreis(0.0, 0.31, 0.27), "NbSchwarz")       # Zahnrad
    for dx in (-0.42, 0.42):
        b.prism_y(-0.05, 0.05, kreis(dx, 0.31, 0.22), "NbOxidrot")      # Bremstrommeln
    # Seilanschluss am Bergende - dort haengt der Wagen am Zugseil
    b.box(uf_x, uf_x + 0.14, -0.18, 0.18, 0.45, 0.62, "NbMetall")

    # -- Geschlossener Kasten: Bruestung, Fensterband, Sturz -----------------
    for ys in (-1, 1):
        b.box(-x_kab, x_kab, min(ys * hw, ys * (hw - 0.05)),
              max(ys * hw, ys * (hw - 0.05)), floor, floor + 0.12, "NbBlau")
        wand(ys, -x_kab, x_kab, floor + 0.12, bruest - 0.05, "NbGelb")
        wand(ys, -x_kab, x_kab, bruest - 0.05, bruest, "NbBlau")
        wand(ys, -x_kab, x_kab, f_top, traufe, "NbGelb")
        # Fenster + gelbe Stege; jedes Fenster mit blauer Umrandung
        wx = -x_kab
        for i in range(n_win):
            wand(ys, wx, wx + steg, bruest, f_top, "NbGelb")
            wx0, wx1 = wx + steg, wx + steg + win_w
            wand(ys, wx0, wx1, bruest, f_top, "NbGlas", versatz=0.02, tiefe=0.02)
            rahmen_blau(wx0, wx1, bruest, f_top, ys * (hw + 0.004))
            wx = wx1
        wand(ys, wx, x_kab, bruest, f_top, "NbGelb")

        # -- Schriftzug "Nerobergbahn" (key/851) --------------------------
        # Blaue Serifenschrift auf der unteren gelben Wandhälfte, links davon
        # ein kleines blaues Zier-Emblem. Hoehe der Versalien aus dem Frame
        # abgeschätzt (Kastenhoehe 3,61 m ≙ Framehoehe): ~0,17 m, das Wort
        # spannt rund 2,2 m. Die Flaeche ist 1 cm vor der Wand.
        aufgemalt(ys, -1.15, 1.15, floor + 0.15, floor + 0.45, "NbSchrift")

        # -- Wasserstandsskala 10/20/30/40 (key/840) ----------------------
        # Blaue Zahlen mit Teilstrichen an einer duennen senkrechten Linie,
        # direkt neben dem Eckpfosten am Wagenende: die Zahlen stehen von
        # unten nach oben auf 10 / 20 / 30 / 40 (Wasserstand = Personen-
        # einheiten). Der gelbe Steg neben dem letzten Fenster ist 0,16 m
        # breit - dort liegt die Skala.
        aufgemalt(ys, x_kab - 0.17, x_kab - 0.01, floor + 0.14, floor + 0.48,
                  "NbSkala")

    # -- Stirnwaende des Kastens (gelb mit einem Fenster + Schild) ----------
    for xs in (-1, 1):
        xw0, xw1 = xs * x_kab - 0.03, xs * x_kab + 0.03
        wl, wr = -0.42, 0.42
        b.box(xw0, xw1, -hw, hw, floor, bruest, "NbGelb")
        b.box(xw0, xw1, -hw, hw, f_top, traufe, "NbGelb")
        b.box(xw0, xw1, -hw, wl, bruest, f_top, "NbGelb")
        b.box(xw0, xw1, wr, hw, bruest, f_top, "NbGelb")
        b.box(xw0, xw1, wl, wr, bruest + 0.02, f_top - 0.02, "NbGlas")
        xf = xs * (x_kab + 0.032)
        b.box(min(xf, xf + xs * 0.012), max(xf, xf + xs * 0.012), wl, wr,
              bruest, bruest + 0.06, "NbBlau")
        b.box(min(xf, xf + xs * 0.012), max(xf, xf + xs * 0.012), wl, wr,
              f_top - 0.06, f_top, "NbBlau")
        b.box(min(xf, xf + xs * 0.012), max(xf, xf + xs * 0.012), wl, wl + 0.06,
              bruest, f_top, "NbBlau")
        b.box(min(xf, xf + xs * 0.012), max(xf, xf + xs * 0.012), wr - 0.06, wr,
              bruest, f_top, "NbBlau")
        # Schwarzes Typenschild ("Wagen 2 / Leergewicht 8100 kg / ...")
        b.box(min(xf, xf + xs * 0.014), max(xf, xf + xs * 0.014), -0.22, 0.22,
              floor + 0.16, floor + 0.34, "NbSchwarz")

    # -- Dach: ockerfarbene Flaeche, blaues Kantenband, creme Untersicht -----
    dx_, dy_ = x_kab + 0.11, hw + 0.13
    for ys in (-1, 1):
        b.box(-dx_, dx_, min(ys * dy_, ys * (dy_ - 0.07)),
              max(ys * dy_, ys * (dy_ - 0.07)), traufe, traufe + 0.06, "NbBlau")
    for xs in (-1, 1):
        b.box(min(xs * dx_, xs * (dx_ - 0.07)), max(xs * dx_, xs * (dx_ - 0.07)),
              -dy_, dy_, traufe, traufe + 0.06, "NbBlau")
    b.box(-dx_, dx_, -dy_, dy_, traufe + 0.06, traufe + 0.10, "NbDachOcker")
    b.box(-dx_ + 0.03, dx_ - 0.03, -dy_ + 0.03, dy_ - 0.03,
          traufe - 0.02, traufe, "NbCreme")
    # rote Signalleuchte an beiden Dachkanten (key/806)
    for xs in (-1, 1):
        lx = xs * (dx_ - 0.05)
        b.box(lx - 0.05, lx + 0.05, -0.05, 0.05, traufe + 0.10, traufe + 0.22, "NbRot")

    # -- Offene Endbuehnen: Boden, Gelaender, Einstiegs-Oeffnung ------------
    for xs in (-1, 1):
        def px(u):
            return xs * (x_kab + u * perron)

        def pbox(u0, u1, y0, y1, z0, z1, mat):
            b.box(min(px(u0), px(u1)), max(px(u0), px(u1)), y0, y1, z0, z1, mat)

        # Bühnenboden: dunkles Riffelblech (key/840) - das Muster liegt als
        # dünne Fläche 1 cm über dem Blech (dieselbe maskierte Texturtechnik
        # wie Schriftzug und Skala).
        pbox(0.0, 1.0, -1.00, 1.00, floor - 0.02, floor + 0.02, "NbBuehne")
        b.quad_tex_towards((px(0.02), 0.98, floor + 0.03), (px(0.02), -0.98, floor + 0.03),
                           (px(0.98), -0.98, floor + 0.03), (px(0.98), 0.98, floor + 0.03),
                           "NbBuehneMuster", (0.0, 0.0, 1.0))
        pbox(0.0, 1.0, -1.02, 1.02, 0.70, floor - 0.02, "NbSchwarz")
        z0, z1 = floor + 0.02, floor + 1.00
        # Laengsgelaender: im inneren Teil offen (Einstieg), aussen zugebaut
        for ys in (-1, 1):
            for u in (0.0, 0.55, 1.0):
                pbox(u - 0.03, u + 0.03, ys * 0.96, ys * 1.02, z0, z1 + 0.06, "NbGelb")
            pbox(0.0, 1.0, ys * 0.96, ys * 1.02, z1 - 0.06, z1, "NbGelb")
            pbox(0.55, 1.0, ys * 0.97, ys * 1.01, floor + 0.56, floor + 0.62, "NbGelb")
            pbox(0.55, 1.0, ys * 0.98, ys * 1.00, z0 + 0.04, z1 - 0.02, "NbSchwarz")
        # Stirngelaender am Wagenende
        for hy in (-1.00, -0.33, 0.33, 1.00):
            b.box(min(px(1.0), px(1.0) - xs * 0.06), max(px(1.0), px(1.0) - xs * 0.06),
                  min(hy - 0.03, hy + 0.03), max(hy - 0.03, hy + 0.03),
                  z0, z1 + 0.06, "NbGelb")
        for zz0, zz1 in ((z1 - 0.06, z1), (floor + 0.56, floor + 0.62)):
            b.box(min(px(1.0), px(1.0) - xs * 0.06), max(px(1.0), px(1.0) - xs * 0.06),
                  -1.00, 1.00, zz0, zz1, "NbGelb")
        b.box(min(px(1.0), px(1.0) - xs * 0.03), max(px(1.0), px(1.0) - xs * 0.03),
              -0.98, 0.98, z0 + 0.04, z1 - 0.02, "NbSchwarz")
        # Scheinwerferpaar unter der Buehne
        for ys in (-1, 1):
            hy = ys * 0.55
            b.box(min(xs * (x_ende - 0.14), xs * x_ende),
                  max(xs * (x_ende - 0.14), xs * x_ende),
                  hy - 0.09, hy + 0.09, 0.60, 0.78, "NbMetall")
            b.box(min(xs * x_ende, xs * (x_ende + 0.03)),
                  max(xs * x_ende, xs * (x_ende + 0.03)),
                  hy - 0.075, hy + 0.075, 0.63, 0.75, "NbGlas")

    # -- Innenraum ---------------------------------------------------------
    # Bis hierher war das Innere nur als Silhouette hinter den Fenstern da
    # (Boden + Bankklötze). Im Mitfahrmodus sitzt der Spieler IM Wagen und
    # schaut sich darin um - die Kabine braucht deshalb echte Waende, Bänke
    # und Bedienelemente.
    #
    # Belege: key/298 (Fahrgastraum: helle Querbänke aus Holzlatten, helle
    # Wände), key/840 (Bühnenboden dunkel gemustert, Skala auf der Wand),
    # key/884 (Führerstand: Tacho mit grünem/rotem Sektor, Kurbel mit rotem
    # Griff), key/491 (dunkelroter Dielenboden).
    #
    # Bezugshoehen aus §4.1: Sitzfläche 0,45 m über dem Wagenboden, Oberkante
    # Lehne 0,85 m. Die Wand-Innenfläche liegt bei y = 1,00 m (Außenwand
    # hw = 1,05 m mit 5 cm Stärke), Verkleidung und Fensterfries 3 cm davor.
    iy = 1.00                # Innenflaeche der Aussenwand
    vk = 0.03                # Stärke der Innenverkleidung
    seat = floor + 0.45      # Sitzhoehe (§4.1)
    back = floor + 0.85      # Oberkante der Lehne (§4.1)

    # Boden: dunkelrot gestrichene Holzdielen (key/491)
    b.box(-x_kab + 0.02, x_kab - 0.02, -0.98, 0.98, floor - 0.03, floor + 0.01,
          "NbBodenRot")

    # Sitzbänke: Querbänke vor den Wandflächen, Sitzfläche aus drei Latten
    # mit sichtbaren Fugen (key/298 zeigt die Latten durch das Fenster).
    for bx in (-1.42, -0.50, 0.50, 1.42):
        for ys in (-1, 1):
            latten = (0.40, 0.59, 0.78)
            for ly in latten:
                b.box(bx - 0.28, bx + 0.28, ys * ly, ys * (ly + 0.16),
                      seat - 0.035, seat, "NbBank")
            # Wangen an beiden Enden, damit die Bank nicht schwebt
            for ex in (-0.28, 0.28):
                b.box(bx + ex - 0.02, bx + ex + 0.02, ys * 0.40, ys * 0.94,
                      floor + 0.02, seat - 0.035, "NbSchwarz")
            # Lehne: zwei Latten aufrecht an der Wand
            for lz in (seat + 0.06, seat + 0.24):
                b.box(bx - 0.28, bx + 0.28, ys * (iy - vk), ys * (iy - vk - 0.04),
                      lz, lz + 0.16, "NbBank")

    # Innenverkleidung: helle Holzflächen an den Wänden (Brüstung unter dem
    # Fensterband und Sturz darüber) - im Vorbild hell, nicht gelb (key/298).
    for ys in (-1, 1):
        b.box(-x_kab + 0.02, x_kab - 0.02, ys * iy, ys * (iy - vk),
              floor + 0.01, bruest - 0.06, "NbBank")
        b.box(-x_kab + 0.02, x_kab - 0.02, ys * iy, ys * (iy - vk),
              f_top + 0.06, traufe - 0.02, "NbBank")
        # Fensterfries: gelbe Rahmen um jede Öffnung (Innenseite, §4.1)
        wx = -x_kab
        for i in range(n_win):
            wx0, wx1 = wx + steg, wx + steg + win_w
            for xa, xb in ((wx0 - 0.05, wx0), (wx1, wx1 + 0.05)):
                b.box(xa, xb, ys * iy, ys * (iy - vk), bruest - 0.06,
                      f_top + 0.06, "NbGelb")
            for za, zb in ((bruest - 0.06, bruest), (f_top, f_top + 0.06)):
                b.box(wx0 - 0.05, wx1 + 0.05, ys * iy, ys * (iy - vk), za, zb,
                      "NbGelb")
            wx = wx1
        # schmaler blauer Streifen an der Deckenkante (§4.1)
        b.box(-x_kab + 0.03, x_kab - 0.03, ys * (iy + 0.01), ys * (iy - 0.01),
              traufe - 0.16, traufe - 0.06, "NbBlau")

    # Stirnwände innen ebenfalls hell verkleiden (das Vorbild ist innen hell,
    # nur die Fensterrahmen sind gelb) - unten unter dem Fenster, oben darüber.
    for xs in (-1, 1):
        ex = xs * x_kab
        for za, zb in ((floor + 0.01, bruest - 0.06), (f_top + 0.06, traufe - 0.02)):
            b.box(min(ex, ex - xs * 0.06), max(ex, ex - xs * 0.06), -0.97, 0.97,
                  za, zb, "NbBank")
        for xa, xb in ((-0.47, -0.42), (0.42, 0.47)):
            b.box(min(ex, ex - xs * 0.06), max(ex, ex - xs * 0.06), xa, xb,
                  bruest - 0.06, f_top + 0.06, "NbGelb")
        for za, zb in ((bruest - 0.06, bruest), (f_top, f_top + 0.06)):
            b.box(min(ex, ex - xs * 0.06), max(ex, ex - xs * 0.06), -0.47, 0.47,
                  za, zb, "NbGelb")

    # Decke: helle Fläche mit sichtbaren Holzrippen
    b.box(-x_kab + 0.02, x_kab - 0.02, -iy, iy, traufe - 0.06, traufe - 0.02,
          "NbCreme")
    rx = -x_kab + 0.35
    while rx < x_kab - 0.2:
        b.box(rx - 0.035, rx + 0.035, -iy, iy, traufe - 0.10, traufe - 0.06,
              "NbBank")
        rx += 0.65

    # -- Führerstand am Bergende (key/884, key/840) -------------------------
    # Der Bediener steht am bergseitigen Wagenende. An der Stirnwand hängen
    # Armaturenplatte mit Tacho, daneben das Schauglas mit rotem Schwimmer,
    # darunter die Handkurbel für Wasserschieber und Bremse. Die Fläche des
    # Tachos zeigt nach INNEN (-X), damit der Mitfahrer sie vom Gang aus sieht.
    sw = x_kab - 0.03        # Innenfläche der bergseitigen Stirnwand (1,82 m)
    # Die Stirnwand hat ein Fenster (|y| < 0,42). Die Bedienteile sitzen
    # deshalb auf dem festen Wandteil daneben, der Tacho auf einer schrägen
    # Platte im Gang - im Vorbild (key/884) liegt die Tachoplatte ebenfalls
    # schräg vor dem Bediener, nicht senkrecht an der Wand.

    # Tachoscheibe: grüner Sektor = zulässig, roter = zu schnell, Zeiger an
    # der Grenze. Die runde Form UND die schwarze Platte kommen aus dem Alpha
    # der Textur - das spart ein Kreismodell und eine eigene Platte.
    c = Vector(TACHO_MITTE)                        # Plattenmitte (Anker)
    n = Vector((-1.0, 0.0, 1.0)).normalized()      # Plattennormale (schräg)
    u = Vector((1.0, 0.0, 1.0)).normalized()      # Bildoben
    r = Vector((0.0, -1.0, 0.0))                  # Bildrechts
    # Mitte so weit vor der Stirnwand, dass die schraeg stehende Platte sie
    # nicht durchstoesst (halbe Diagonale 0,19 m -> 0,134 m in X).
    hr = hu = 0.19
    b.quad_tex_towards(c - r * hr - u * hu, c + r * hr - u * hu,
                       c + r * hr + u * hu, c - r * hr + u * hu,
                       "NbTacho", n)
    # Halterung unter der Platte, damit sie nicht schwebt
    b.box(1.68, 1.74, c.y - 0.05, c.y + 0.05, floor + 0.02, c.z - 0.15, "NbSchwarz")

    # Wasserstandsschauglas mit rotem Schwimmer (key/840): Glasrohr in einer
    # Metallfassung, daneben die Skala 10/20/30/40 (dieselbe Textur wie
    # außen - dort ist sie auf die Wand gemalt).
    # Der Aufbau muss von der Kabine aus nach VORNE durchsichtig sein: Glasrohr
    # vor der Blende, Schwimmer IM Rohr. In der ersten Fassung lagen Glas und
    # Schwimmer INNERHALB der Blende (gleicher x-Bereich) - der rote Schwimmer
    # war damit von der Kabine aus nie zu sehen (im Kontrollbild aufgefallen,
    # nicht im Spiel). Reihenfolge jetzt, von der Wand zur Kabine:
    #   Wand (gelb, ~1,85) -> Blende 1,775..1,79 -> Glasrohr 1,727..1,775.
    gx = SCHWIMMER_UNTEN[1]                         # rechts neben dem Fenster
    gr = SCHWIMMER_UNTEN[0]                         # Mitte des Glasrohrs
    b.box(gr - 0.024, sw - 0.03, gx - 0.055, gx + 0.055, 1.34, 1.40, "NbMetall")
    b.box(gr - 0.024, sw - 0.03, gx - 0.055, gx + 0.055, 1.90, 1.96, "NbMetall")
    b.box(sw - 0.045, sw - 0.03, gx - 0.075, gx + 0.075, 1.36, 1.94, "NbCreme")
    # Das Glasrohr ist eine SCHALE: Rueckwand und zwei Seitenholme, die Front
    # bleibt offen. Ein geschlossener Glaskasten verdeckt seinen Inhalt -
    # die Glas-Materialien sind wie die Fenster undurchsichtig, das rote
    # Baellchen war darin von der Kabine aus unsichtbar (im Kontrollbild
    # gesehen). So steht der Schwimmer zwischen den Holmen vor der Rueckwand,
    # genau wie im Vorbild durch das Glas gesehen.
    b.box(gr + 0.019, gr + 0.024, gx - 0.024, gx + 0.024,
          SCHWIMMER_UNTEN[2], SCHWIMMER_OBEN[2], "NbGlas")
    for ys in (-1, 1):
        b.box(gr - 0.024, gr + 0.024, gx + ys * 0.042, gx + ys * 0.024,
              SCHWIMMER_UNTEN[2], SCHWIMMER_OBEN[2], "NbGlas")
    # Der rote Schwimmer ist ein EIGENES Mesh (SM_WbNbSchwimmer): er wandert im
    # Spiel mit dem Fuellstand (Anker SCHWIMMER_UNTEN/OBEN).
    # Die Skala steht auf der gelben Wand NEBEN dem Rohr - genau wie im
    # Vorbild gemalt (key/840), nicht auf der Blende. Die Innenflaeche der
    # Stirnwand liegt bei sw = x_kab - 0,03 (der Wandkasten ist 6 cm stark,
    # siehe "Stirnwaende des Kastens"), das Bild also 5 mm davor. Ein Decal,
    # das in der Wand steckt, ist unsichtbar.
    wand = sw - 0.005
    b.quad_tex_towards((wand, gx - 0.09, 1.38), (wand, gx - 0.20, 1.38),
                       (wand, gx - 0.20, 1.94), (wand, gx - 0.09, 1.94),
                       "NbSkala", (-1.0, 0.0, 0.0))

    # Handkurbel ("Multitool" für Wasserschieber und Bremse, TON 13:13).
    # Nur noch der LAGERBOCK bleibt im Wagen; Arm und roter Griff sind ein
    # eigenes Mesh (SM_WbNbKurbel) und schwenken im Spiel um KURBEL_ACHSE,
    # wenn der Fahrgast den Wasserschieber bedient.
    b.box(sw - 0.06, sw, -0.70, -0.56, KURBEL_ACHSE[2] - 0.05,
          KURBEL_ACHSE[2] + 0.05, "NbMetall")

    # Der Kasten liegt parallel zur Trasse: Neigung ins Mesh, weil der Actor
    # nur giert (WiesbadenNerobergbahn.cpp, PlaceCar).
    b.tilt_grade(GRADE)

    obj = b.to_object("SM_WbNbWagen")
    return obj, b.mat_names, (L, W)


# ---------------------------------------------------------------------------
# 2. Bahnsteighalle - Tal- UND Bergstation, gleicher Bautyp (NACHBAU-REFERENZ §7.1)
#
# Vorlagen: station/vorlage_3_talstation.png (Innenansicht), key/933 (Trog von
# oben), key/199 + station/vorlage_1 (Aussenansicht). Die alte Fassung war ein
# geschlossenes Stationsgebaeude mit Backsteinsockel und Fenstern - im Vorbild
# steht eine OFFENE HALLE ueber dem Gleistrog (keine Waende, kein Raum).
#
# Bezug wie beim Gleis: Ursprung auf der SCHIENENOBERKANTE in Trassenmitte
# zwischen BEIDEN Gleisen (die Halle ueberspannt das Gleispaar, §5.1a), X
# laengs, Y quer, Z hoch. Damit stellt der Actor sie ohne Zuschlag auf die
# aufgeloeste Trasse (PlaceStructures). Die Bergstation wird um 180 Grad
# gedreht gesetzt: der Wagen steht dort am anderen Hallenende, und die offene
# Balustradenseite muss mitwandern.
#
# Querschnitt aus dem Gleisaufbau (§5.1a): Schwellen 2,40 m breit -> lichte
# Trogweite 4,00 m, Trogsohle = Bettsohle -0,60 m. Bahnsteighoehe 0,80 m =
# Wagenboden 0,85 m weniger eine Stufe; der Film nennt den Einstieg "fast
# niveaugleich" (§4.1). Stuetzen, Binder und Rautengitter nach key/933: oxidrot
# lackierte Gusseisenstuetzen mit Halsringen und Kapitell, dunkle Holzbinder
# mit gedrehten Haengezapfen (Docken), weisse Rautengitter-Balustraden.
# ---------------------------------------------------------------------------
HALLE_L = 12.00            # Laenge der Halle entlang der Trasse
HALLE_JOCHE = 4.00         # Stuetzenabstand laengs (3 Joche)
TROG_HALB = 2.00           # halbe lichte Weite des Gleistrogs
TROG_SOHLE = -0.60         # Trogsohle (wie Bettsohle des Gleises)
TROG_BODEN = 0.12          # Sohlplatte
BAHNSTEIG_H = 0.80         # Bahnsteigoberkante ueber Schienenoberkante
BAHNSTEIG_B = 2.60         # Bahnsteigbreite bis zur Aussenkante
BAHNSTEIG_PLATTE = 0.06    # Betonplatte auf dem Bahnsteigkoerper
STUETZE_Y = 2.35           # Stuetzenachse quer (0,35 m von der Trogkante)
STUETZE_H = 3.70           # Stuetze: Bahnsteig bis Stuetzenkopf
BINDER_H = 0.26            # Binderhoehe (Kehlbalken)
DACH_NEIGUNG = 0.30        # Firsthoehe je Meter Querabstand
DACH_UEBERSTAND = 0.80     # Dachueberstand ueber die Bahnsteigkante
DACH_UEBERSTAND_X = 0.60   # Dachueberstand am Giebel
BALUSTER_H = 1.10          # Rautengitter-Balustrade ueber Bahnsteig
ROST_B = 0.80              # Breite der Rostabdeckung neben dem Gleis
# Der Wagen steht im Bahnsteig am TIEFEN Hallenende (Talstation: S = 0), dort
# steigt man ein - deshalb bleibt das erste Joch balustradenfrei.
BALUSTER_FREI = HALLE_JOCHE


def balken(b, p0, p1, breit, hoehe, mat, quer=None, tpm=1.0):
    """Rechteckbalken von p0 nach p1 (auch schraeg - fuer Sparren und Kopfbänder).

    Die sechs Flaechen werden mit DERSELBEN Eckfolge wie box() angelegt, nur
    durch die Balkenachsen abgebildet: sonst haette ein schraeger Balken eine
    andere Windung als der Rest des Bauteils, und orient_outward() dreht das
    ganze Objekt falsch herum.
    """
    p0, p1 = Vector(p0), Vector(p1)
    d = p1 - p0
    laenge = d.length
    if laenge < 1e-6:
        return
    d = d / laenge
    if quer is None:
        quer = Vector((0.0, 0.0, 1.0)) if abs(d.z) < 0.9 else Vector((0.0, 1.0, 0.0))
    q = Vector(quer) - d * Vector(quer).dot(d)
    if q.length < 1e-6:
        q = Vector((0.0, 1.0, 0.0)) - d * d.y
    q.normalize()
    n = d.cross(q).normalized()

    def W(x, y, z):
        return p0 + d * x + q * y + n * z

    hb, hh = breit * 0.5, hoehe * 0.5
    b.quad(W(0, -hb, -hh), W(laenge, -hb, -hh), W(laenge, hb, -hh), W(0, hb, -hh), mat, tpm)
    b.quad(W(0, -hb, hh), W(0, hb, hh), W(laenge, hb, hh), W(laenge, -hb, hh), mat, tpm)
    b.quad(W(0, -hb, -hh), W(0, -hb, hh), W(laenge, -hb, hh), W(laenge, -hb, -hh), mat, tpm)
    b.quad(W(0, hb, -hh), W(laenge, hb, -hh), W(laenge, hb, hh), W(0, hb, hh), mat, tpm)
    b.quad(W(0, -hb, -hh), W(0, hb, -hh), W(0, hb, hh), W(0, -hb, hh), mat, tpm)
    b.quad(W(laenge, -hb, -hh), W(laenge, -hb, hh), W(laenge, hb, hh),
           W(laenge, hb, -hh), mat, tpm)


def docke(b, x, y, z_top, mat, laenge=0.62, tpm=1.0):
    """Gedrehter Haengezapfen (Docke) unter den Binderenden und dem Haengewerk.

    Im Vorbild sind das die auffaelligen Zierkoepfe der Halle (key/933): eine
    Reihe von Ringen und Wulsten, die in eine Spitze auslaufen. Nachgebaut als
    Folge senkrechter Rundkoerper - eine Drehbank hat das Modell nicht.
    """
    radien = (0.075, 0.050, 0.090, 0.115, 0.075, 0.045, 0.075, 0.032)
    teile = (0.05, 0.05, 0.06, 0.09, 0.06, 0.05, 0.17, 0.09)
    faktor = laenge / sum(teile)
    z = z_top
    for radius, hoehe in zip(radien, teile):
        hh = hoehe * faktor
        b.prism_z(z - hh, z, kreis(radius, 10, x, y), mat, tpm)
        z -= hh


def build_bahnsteighalle():
    b = MeshBuilder()
    x0, x1 = -HALLE_L * 0.5, HALLE_L * 0.5
    y_trog = TROG_HALB
    y_aussen = TROG_HALB + BAHNSTEIG_B
    y_traufe = y_aussen + DACH_UEBERSTAND
    z_kopf = BAHNSTEIG_H + STUETZE_H                 # Stuetzenkopf 4,50 m
    z_traufe = z_kopf + BINDER_H                     # Binderoberkante 4,76 m
    z_first = z_traufe + y_traufe * DACH_NEIGUNG     # First 6,38 m

    def z_dach(y):
        """Dachhaut an der Stelle |y| (gerader Sparren vom First zur Traufe)."""
        return z_first - (abs(y) / y_traufe) * (z_first - z_traufe)

    # -- Gleistrog ---------------------------------------------------------
    # Sohle dunkel ("Sohle dunkel", §6.2), Waende sind die Bahnsteigkoerper.
    b.box(x0, x1, -y_trog, y_trog, TROG_SOHLE, TROG_SOHLE + TROG_BODEN, "NbBuehne")
    # Rostabdeckungen neben dem Gleis (Aussenkante der Schwellen 1,20 m;
    # ROST_B breit bis zur Trogwand) - als geschlossene Platte mit aufgelegtem
    # Gitter, damit keine einzelne Flaeche die Windung des Objekts kippt.
    for ys in (-1, 1):
        y_innen = ys * (TROG_HALB - ROST_B)
        y_rand = ys * TROG_HALB
        z_rost = TROG_SOHLE + TROG_BODEN
        b.box(x0, x1, min(y_innen, y_rand), max(y_innen, y_rand),
              z_rost, z_rost + 0.05, "NbRost", tpm=0.8)
        b.box(x0, x1, min(y_innen, y_rand), max(y_innen, y_rand),
              z_rost + 0.05, z_rost + 0.065, "NbGitterrost", tpm=1.0)

    # -- Bahnsteige --------------------------------------------------------
    for ys in (-1, 1):
        y_kante = ys * y_trog
        y_rand = ys * y_aussen
        z_platte = BAHNSTEIG_H - BAHNSTEIG_PLATTE
        # Koerper (Putz) bis unter die Platte
        b.box(x0, x1, min(y_kante, y_rand), max(y_kante, y_rand),
              TROG_SOHLE, z_platte, "NbPutz", tpm=0.6)
        # Gehboden aus Betonplatten, gelbe Sicherheitslinie buendig an der Kante
        b.box(x0, x1, min(ys * (TROG_HALB + 0.40), y_rand),
              max(ys * (TROG_HALB + 0.40), y_rand), z_platte, BAHNSTEIG_H,
              "NbStein", tpm=0.7)
        b.box(x0, x1, min(y_kante, ys * (TROG_HALB + 0.40)),
              max(y_kante, ys * (TROG_HALB + 0.40)), z_platte, BAHNSTEIG_H,
              "NbGelb", tpm=1.0)

    # -- Gusseisenstuetzen (oxidrot, mit Halsringen und Kapitell) ----------
    stuetz_x = [x0 + i * HALLE_JOCHE for i in range(int(HALLE_L / HALLE_JOCHE) + 1)]
    for xs in stuetz_x:
        for ys in (-1, 1):
            y = ys * STUETZE_Y
            b.box(xs - 0.24, xs + 0.24, y - 0.24, y + 0.24,
                  BAHNSTEIG_H, BAHNSTEIG_H + 0.05, "NbGusseisen")      # Sockelplatte
            b.prism_z(BAHNSTEIG_H + 0.05, BAHNSTEIG_H + 0.34,
                      kreis(0.19, 12, xs, y), "NbGusseisen")          # Fuss
            b.prism_z(BAHNSTEIG_H + 0.34, z_kopf - 0.22,
                      kreis(0.145, 12, xs, y), "NbGusseisen")         # Schaft
            for z_ring in (BAHNSTEIG_H + 0.72, z_kopf - 0.62):
                b.prism_z(z_ring, z_ring + 0.10,
                          kreis(0.175, 12, xs, y), "NbGusseisen")     # Halsring
            b.prism_z(z_kopf - 0.28, z_kopf - 0.06,
                      kreis(0.20, 12, xs, y), "NbGusseisen")          # Kapitell
            # Die Kapitellplatte endet UNTER dem Binder (z_kopf ist dessen
            # Unterkante) - darueber steckte sie im Binder und war unsichtbar.
            b.box(xs - 0.21, xs + 0.21, y - 0.21, y + 0.21,
                  z_kopf - 0.06, z_kopf, "NbGusseisen")               # Kapitellplatte

    # -- Binder (dunkle Holzkonstruktion mit Haengezapfen) ----------------
    for xs in stuetz_x:
        b.box(xs - 0.11, xs + 0.11, -y_traufe, y_traufe,
              z_traufe - BINDER_H, z_traufe, "NbHolzDunkel")          # Kehlbalken
        balken(b, (xs, -y_traufe, z_traufe), (xs, 0.0, z_first),
               0.20, 0.17, "NbHolzDunkel", quer=(1.0, 0.0, 0.0))       # Sparren
        balken(b, (xs, y_traufe, z_traufe), (xs, 0.0, z_first),
               0.20, 0.17, "NbHolzDunkel", quer=(1.0, 0.0, 0.0))
        for ys in (-1, 1):
            balken(b, (xs, ys * STUETZE_Y, z_traufe - BINDER_H),
                   (xs, ys * 1.05, z_first - 0.42),
                   0.14, 0.12, "NbHolzDunkel", quer=(1.0, 0.0, 0.0))   # Kopfband
        b.box(xs - 0.09, xs + 0.09, -0.09, 0.09,
              z_traufe - BINDER_H, z_first - 0.14, "NbHolzDunkel")     # Haengewerk
        for y_docke in (0.0, -y_traufe, y_traufe):
            docke(b, xs, y_docke, z_traufe - BINDER_H, "NbHolzDunkel")

    # -- Pfetten (laengs) und Dachhaut ------------------------------------
    for y_pf in (0.0, 2.70, 5.20):
        for ys in ((1,) if y_pf == 0.0 else (-1, 1)):
            y = ys * y_pf
            z = z_dach(y)
            b.box(x0 - DACH_UEBERSTAND_X, x1 + DACH_UEBERSTAND_X, y - 0.075, y + 0.075,
                  z - 0.18, z, "NbHolzDunkel")

    x_d0, x_d1 = x0 - DACH_UEBERSTAND_X, x1 + DACH_UEBERSTAND_X
    for ys in (-1, 1):
        # Zwei Platten uebereinander: unten die helle Dachschalung, die man
        # von der Halle aus sieht (§6.2), darueber die dunkle Dachhaut.
        balken(b, (0.0, 0.0, z_first - 0.05), (0.0, ys * (y_traufe + 0.10),
                                               z_dach(y_traufe + 0.10) - 0.05),
               HALLE_L + 2 * DACH_UEBERSTAND_X, 0.08, "NbCreme",
               quer=(1.0, 0.0, 0.0), tpm=0.5)
        balken(b, (0.0, 0.0, z_first + 0.03), (0.0, ys * (y_traufe + 0.10),
                                               z_dach(y_traufe + 0.10) + 0.03),
               HALLE_L + 2 * DACH_UEBERSTAND_X, 0.06, "NbDach",
               quer=(1.0, 0.0, 0.0), tpm=0.7)
        # Traufbohle (dunkel) an der Dachkante
        balken(b, (x_d0, ys * (y_traufe + 0.06), z_traufe - 0.10),
               (x_d1, ys * (y_traufe + 0.06), z_traufe - 0.10),
               0.06, 0.34, "NbHolzDunkel", quer=(0.0, 0.0, 1.0))
        # Giebelschmuckbohle (Ortgang) an beiden Hallenenden
        for xs in (x_d0, x_d1):
            balken(b, (xs, 0.0, z_first - 0.02), (xs, ys * (y_traufe + 0.06),
                                                  z_traufe - 0.02),
                   0.06, 0.22, "NbHolzDunkel", quer=(1.0, 0.0, 0.0))

    # -- Rautengitter-Balustraden -----------------------------------------
    # Weisse Felder zwischen den Stuetzen an der Bahnsteig-Aussenkante, mit
    # Rahmen und Handlauf; das Joch am tiefen Hallenende bleibt frei (Einstieg).
    y_bal = y_aussen - 0.06
    panel = 2.00
    n_panel = int((HALLE_L - BALUSTER_FREI) / panel)
    for ys in (-1, 1):
        y = ys * y_bal
        for i in range(n_panel + 1):
            xp = x0 + BALUSTER_FREI + i * panel
            b.box(xp - 0.06, xp + 0.06, y - 0.06, y + 0.06,
                  BAHNSTEIG_H, BAHNSTEIG_H + BALUSTER_H, "NbWeiss")  # Pfosten
        for i in range(n_panel):
            xa = x0 + BALUSTER_FREI + i * panel
            xb = xa + panel
            b.box(xa, xb, y - 0.04, y + 0.04,
                  BAHNSTEIG_H + BALUSTER_H - 0.07, BAHNSTEIG_H + BALUSTER_H,
                  "NbWeiss")                                          # Handlauf
            b.box(xa, xb, y - 0.03, y + 0.03, BAHNSTEIG_H + 0.06,
                  BAHNSTEIG_H + 0.12, "NbWeiss")                      # Fussriegel
            # Gitterfeld als duenne Platte (Rautenmuster aus dem Alpha);
            # Rautenteilung 0,28 m wie im Vorbild (tpm = 1/0,28).
            b.box(xa + 0.07, xb - 0.07, y - 0.006, y + 0.006,
                  BAHNSTEIG_H + 0.12, BAHNSTEIG_H + BALUSTER_H - 0.07,
                  "NbRautengitter", tpm=3.6)

    laenge_x = HALLE_L + 2 * DACH_UEBERSTAND_X
    obj = b.to_object("SM_WbNbBahnsteighalle")
    return obj, b.mat_names, (laenge_x, 2 * y_traufe, z_first)


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
# 4. Trasse: das Gleis als Satz wiederverwendbarer Bauteile
# ---------------------------------------------------------------------------
#
# Das Vorbild hat keine zwei getrennten Gleise, sondern EINE gemeinsame
# Trasse: drei Laufschienen mit gemeinsamer Mittelschiene, dazu zwei
# Riggenbach-Zahnstangen, einen Seilkanal mit Rostabdeckung in der Mitte und
# das Schotterbett.
#
# Beleg (nachgemessen ueber die OSM-Streckenpunkte des Actors): die beiden
# Gleislinien liegen auf 385 von 434 m genau 1,00 m auseinander, nur in der
# Ausweiche (s = 199..234 m) und am Talstationsende laufen sie auf 3,7-4,15 m
# auseinander. 1,00 m ist aber die SPURWEITE - die Linien sind also die
# WAGENMITTE-Linien. Daraus folgt der Querschnitt von selbst:
#
#   Wagenmitte -0,50 m | Schiene -1,00 m (aussen) und 0,00 m (Mitte)
#   Wagenmitte +0,50 m | Schiene  0,00 m (Mitte) und +1,00 m (aussen)
#
# also DREI Schienen, deren mittlere beiden zusammenfallen; in der Ausweiche
# bleiben vier Schienen und die Mittelschiene entfaellt. Die Zahnstangen
# liegen in den Wagenmitten (+-0,50 m), wo auch das Zahnrad des Wagens sitzt.
#
# Alle Bauteile: X laeuft die Strecke entlang, Y quer, Z nach oben.
# Der Ursprung liegt auf der SCHIENENOBERKANTE in Trassenmitte (Z = 0), damit
# der Platzer im Actor die Teile nur noch um die Schienenoberkante heben muss
# und alle Hoehen zueinander stimmen:
#
#   Schienenkopf        0,00
#   Schienenfuss       -0,14   (= Schwellenoberkante = Oberkante Seilkanal)
#   Schwellenunterseite -0,30   (= Oberkante Schotterbett)
#   Schotterbett unten  -0,60

SCHIENE_STAB = 6.0      # Laenge eines Schienenstabs
TEIL_STAB = 2.0         # Laenge von Zahnstange, Seilkanal und Bettsegment
SPUR = 1.0              # Spurweite - Schiene 0,50 m neben der Wagenmitte
SCHWELLE_HALB = 1.20     # Schwellenlaenge 2,40 m
BETT_HALB = 1.30         # Bettkrone 2,60 m breit
RAIL_BOTTOM = -0.14
SLEEPER_BOTTOM = -0.30


def build_schiene():
    """Ein Schienenstab (S20-Profil) - dreimal je Querschnitt, 1000-mm-Spur."""
    b = MeshBuilder()
    h = SCHIENE_STAB * 0.5
    # Fuss, Steg, Kopf als drei Kaesten statt als I-Profil aus einem Zug: ein
    # I-Querschnitt ist nicht konvex, die Faecherdeckel von prism_x wuerden
    # sich ueberkreuzen.
    b.box(-h, h, -0.070, 0.070, -0.140, -0.120, "NbSchiene")
    b.box(-h, h, -0.017, 0.017, -0.120, -0.048, "NbSchiene")
    b.box(-h, h, -0.035, 0.035, -0.048, -0.008, "NbSchiene")
    b.box(-h, h, -0.030, 0.030, -0.008, 0.000, "NbSchiene")
    b.orient_outward()
    return (b.to_object("SM_WbNbSchiene"), b.mat_names,
            (SCHIENE_STAB, 0.140, 0.140))


def build_zahnstange():
    """Ein Riggenbach-Zahnstangenstab: zwei Seitenbleche + Sprossen.

    Beim Riggenbach-System SIND die Sprossen die Zaehne - das Zahnrad greift
    zwischen den beiden Seitenblechen in die Querstaebe. 110 mm breit,
    100 mm Teilung (NACHBAU-REFERENZ.md, Belegframes der Strecke).
    """
    b = MeshBuilder()
    h = TEIL_STAB * 0.5
    pitch = 0.10
    # Die Sprossen stehen als Zaehne ueber die Bleche hinaus, die Bleche
    # selbst sind der durchlaufende Traeger auf der Schwelle.
    b.box(-h, h, -0.055, -0.030, RAIL_BOTTOM, -0.055, "NbZahnstange")
    b.box(-h, h, 0.030, 0.055, RAIL_BOTTOM, -0.055, "NbZahnstange")
    for i in range(int(TEIL_STAB / pitch) + 1):
        x = -h + i * pitch
        b.box(x - 0.017, x + 0.017, -0.030, 0.030, RAIL_BOTTOM, -0.085,
              "NbZahnstange")
    b.orient_outward()
    return (b.to_object("SM_WbNbZahnstange"), b.mat_names,
            (TEIL_STAB, 0.110, 0.055))


def build_seilkanal():
    """Seilkanal mit Rostabdeckung - liegt in Trassenmitte unter der
    Mittelschiene (die Schienenunterkante ist zugleich die Kanalkrone).

    Aufbau wie im Film: 0,56 m aussen, 0,30 m tief, in der Mitte der Sockel
    fuer die Mittelschiene, links und rechts davon die Rostfelder.
    """
    b = MeshBuilder()
    h = TEIL_STAB * 0.5
    b.box(-h, h, -0.280, 0.280, -0.300, -0.270, "NbSeilkanal")   # Boden
    b.box(-h, h, -0.280, -0.250, -0.270, RAIL_BOTTOM, "NbSeilkanal")
    b.box(-h, h, 0.250, 0.280, -0.270, RAIL_BOTTOM, "NbSeilkanal")
    b.box(-h, h, -0.075, 0.075, -0.270, RAIL_BOTTOM, "NbSeilkanal")
    rod = 0.05                                                   # Roststaebe
    for (y0, y1) in ((-0.250, -0.075), (0.075, 0.250)):
        b.box(-h, h, y0, y0 + 0.015, -0.155, RAIL_BOTTOM, "NbRost")
        b.box(-h, h, y1 - 0.015, y1, -0.155, RAIL_BOTTOM, "NbRost")
        for i in range(int(TEIL_STAB / rod) + 1):
            x = -h + i * rod
            b.box(x - 0.006, x + 0.006, y0 + 0.015, y1 - 0.015, -0.153, -0.142,
                  "NbRost")
    b.orient_outward()
    return (b.to_object("SM_WbNbSeilkanal"), b.mat_names,
            (TEIL_STAB, 0.560, 0.300))


def build_schwelle():
    """Eine Holzschwelle 2,40 x 0,24 x 0,16 m.

    In der Ausweiche wird sie im Spiel in Y gestreckt, damit sie beide Gleise
    traegt - deshalb hier KEINE aufgeschraubten Klemmplatten an festen
    Schienenlagen: die wuerden beim Strecken mitwandern.
    """
    b = MeshBuilder()
    b.box(-0.120, 0.120, -SCHWELLE_HALB, SCHWELLE_HALB,
          SLEEPER_BOTTOM, RAIL_BOTTOM, "NbHolz")
    b.orient_outward()
    return (b.to_object("SM_WbNbSchwelle"), b.mat_names,
            (0.240, 2 * SCHWELLE_HALB, 0.160))


def build_schotterbett():
    """Ein Schotterbettsegment: Krone 2,60 m, Schultern 1:1,5, 0,30 m stark."""
    b = MeshBuilder()
    h = TEIL_STAB * 0.5
    top, bot = SLEEPER_BOTTOM, SLEEPER_BOTTOM - 0.30
    breit = 0.32                                        # 0,30 m hoch, 1:1,5
    # Nur Flaechen, die man sieht - plus die Unterseite, damit das Volumen
    # eindeutig bleibt. Die Windung gibt quad_towards vor.
    b.quad_towards((-h, -BETT_HALB, top), (h, -BETT_HALB, top),
                   (h, BETT_HALB, top), (-h, BETT_HALB, top), "NbSchotter",
                   (0, 0, 1))
    b.quad_towards((-h, -BETT_HALB, top), (-h, -BETT_HALB - breit, bot),
                   (h, -BETT_HALB - breit, bot), (h, -BETT_HALB, top),
                   "NbSchotter", (0, -1, -1))
    b.quad_towards((h, BETT_HALB, top), (h, BETT_HALB + breit, bot),
                   (-h, BETT_HALB + breit, bot), (-h, BETT_HALB, top),
                   "NbSchotter", (0, 1, -1))
    for sx in (-h, h):
        b.quad_towards((sx, -BETT_HALB, top), (sx, -BETT_HALB - breit, bot),
                       (sx, BETT_HALB + breit, bot), (sx, BETT_HALB, top),
                       "NbSchotter", (sx > 0, 0, 0))
    b.quad_towards((-h, -BETT_HALB - breit, bot), (-h, BETT_HALB + breit, bot),
                   (h, BETT_HALB + breit, bot), (h, -BETT_HALB - breit, bot),
                   "NbSchotter", (0, 0, -1))
    return (b.to_object("SM_WbNbSchotterbett"), b.mat_names,
            (TEIL_STAB, 2 * (BETT_HALB + breit), 0.30))


# ---------------------------------------------------------------------------
# 3. Bewegliche Teile des Wagens - eigene Meshes mit dem DREHPUNKT IM URSPRUNG
#
# Zeiger, Schwimmer und Kurbelarme gehoeren nicht mehr ins Wagen-Mesh, weil
# der Actor sie im Spiel bewegt: die Nadel zeigt die Geschwindigkeit, der
# Schwimmer den Wasserstand, der Kurbelarm schwenkt beim Bedienen. Jedes Teil
# bringt seinen Drehpunkt im Ursprung mit; der Actor setzt es mit DERSELBEN
# Neigung an den Anker (TACHO_MITTE / SCHWIMMER_UNTEN..OBEN / KURBEL_ACHSE),
# die auch das Wagen-Mesh traegt (tilt_grade).
# ---------------------------------------------------------------------------

def kreis(radius, n=16, cx=0.0, cz=0.0):
    """Querschnitt eines Rundkoerpers als Liste von (x,z) bzw. (x,y)."""
    return [(cx + radius * math.cos(2.0 * math.pi * k / n),
             cz + radius * math.sin(2.0 * math.pi * k / n)) for k in range(n)]


def build_tacho_zeiger():
    """Zeiger der Geschwindigkeitsanzeige (key/884).

    Flach in der XY-Ebene, Drehpunkt im Ursprung, Spitze nach +Y (= im Bild
    oben, weil die Platte "von aussen gelesen" aufgebaut ist). Lokal ist +Z
    die Plattennormale. Das Blatt liegt 8 mm vor der Scheibe, damit es die
    aufgemalte Skala nicht schneidet.
    """
    b = MeshBuilder()
    b.box(-0.0075, 0.0075, -0.026, 0.112, 0.008, 0.013, "NbZeiger")   # Blatt
    b.box(-0.016, 0.016, -0.016, 0.016, 0.004, 0.017, "NbZeiger")     # Nabe
    b.orient_outward()
    return (b.to_object("SM_WbNbTachoZeiger"), b.mat_names, (0.032, 0.138, 0.017))


def build_schwimmer():
    """Roter Schwimmer im Wasserstandsschauglas (key/840).

    Senkrechter Zylinder, Drehpunkt (und Schwerpunkt) im Ursprung: der Actor
    schiebt ihn entlang der Glasachse. Im Vorbild ist es ein rotes Baellchen.
    Der Durchmesser 4,8 cm ist die lichte Weite des Glasrohrs: genau so breit
    wie das Glas in Y und schmaler als die weisse Blende dahinter (13 cm),
    die Schauglaskugel bleibt also im Gehaeuse und ist nur durch das Glas zu
    sehen - waere sie breiter, quoll sie neben dem Glas heraus.
    """
    b = MeshBuilder()
    b.prism_z(-0.024, 0.024, kreis(0.024), "NbRot")
    b.orient_outward()
    return (b.to_object("SM_WbNbSchwimmer"), b.mat_names, (0.048, 0.048, 0.048))


def build_kurbel():
    """Handkurbel fuer Wasserschieber und Bremse (TON 13:13, key/884).

    Welle quer (Y), Arm nach oben, roter Griff am Ende. Drehpunkt im
    Ursprung = Lagerzapfen (KURBEL_ACHSE). Weil die Welle in Y liegt und die
    Neigung des Wagens eine Drehung um Y ist, bleibt die Drehachse beim
    Einbau unveraendert - der Actor schwenkt den Arm einfach um Y.
    """
    b = MeshBuilder()
    b.prism_y(-0.055, 0.055, kreis(0.018), "NbMetall")               # Welle
    b.box(-0.022, 0.022, -0.018, 0.018, 0.0, KURBEL_LAENGE, "NbMetall")
    griff = [(x, KURBEL_LAENGE + z) for (x, z) in kreis(0.026)]
    b.prism_y(-0.065, 0.065, griff, "NbRot")                        # roter Griff
    b.orient_outward()
    return (b.to_object("SM_WbNbKurbel"), b.mat_names,
            (0.052, 0.13, KURBEL_LAENGE + 0.026))


# ---------------------------------------------------------------------------
# Export + Kontrollrender
# ---------------------------------------------------------------------------

def center_origin_xy(obj, keep_z=False, keep_xy=False):
    """Ursprung in die XY-Mitte. z-Minimum auf 0 (Bauwerke stehen auf dem
    Boden) - ausser keep_z: dann bleibt der modellierte z=0 erhalten, beim
    Wagen die Schienenkontaktlinie in Wagenmitte.

    keep_xy misst nur und verschiebt nicht: der Wagen ist in seiner eigenen
    Trassenlage schon symmetrisch, und ein Verschieben um die Bounding-Box
    wuerde die Schienenkontaktlinie aus dem Ursprung ziehen (das gekippte Mesh
    ist in X nicht mehr symmetrisch).
    """
    me = obj.data
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    if not keep_xy:
        zshift = 0.0 if keep_z else min(zs)
        shift = Vector(((min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5, zshift))
        for v in me.vertices:
            v.co -= shift
        me.update()
        xs = [c - shift.x for c in xs]
        ys = [c - shift.y for c in ys]
        zs = [c - shift.z for c in zs]
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
        eintrag = {"name": nm, "base_color": [round(c, 4) for c in rgb],
                   "kind": kind}
        if nm in TEXTURES:
            eintrag["texture"] = TEXTURES[nm]
        if nm in DECAL_PBR:
            # Tachoscheibe und Riffelmuster sind keine Lackflaechen: das
            # Unreal-Material braucht dafuer eigene Metall-/Rauheitswerte.
            eintrag["metallic"], eintrag["roughness"] = DECAL_PBR[nm]
        out.append(eintrag)
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
    if tag == "wagen":
        # Der Wagen traegt Schriftzug und Wasserstandsskala auf BEIDEN Seiten,
        # und die Ecken jeder Flaeche werden je Seite anders geordnet (damit
        # die Schrift beidseitig aufrecht steht) - beide Seiten muessen also
        # einzeln im Kontrollbild stehen.
        views["seite_gegen"] = Vector((0.2 * diag, 2.0 * diag, 0.7 * diag))

    def shoot(vname, cam_pos, ziel, breite=960):
        cam.location = cam_pos
        cam.rotation_euler = (ziel - cam.location).to_track_quat("-Z", "Y").to_euler()
        sc.render.resolution_x = breite
        sc.render.resolution_y = int(breite * 2.0 / 3.0)
        sc.render.filepath = os.path.join(out_dir, "vorschau_%s_%s.png" % (tag, vname))
        bpy.ops.render.render(write_still=True)

    for vname, loc in views.items():
        shoot(vname, centre + loc, centre)

    if tag == "bahnsteighalle":
        # Innenansicht wie die Vorlage (station/vorlage_3): Blick aus dem Trog
        # die Halle entlang. In EINEM Bild haengen daran Stuetzen mit
        # Halsringen, Binder mit Haengezapfen, Pfetten, Balustraden und die
        # Bahnsteigkanten - die Gesamtansicht von aussen zeigt davon nichts.
        shoot("innen", Vector((-HALLE_L * 0.5 + 1.0, 0.0, 1.00)),
              Vector((HALLE_L * 0.5, 0.0, 2.30)), breite=1600)
        # Stuetze und Balustrade von nahem: daran haengen die Halsringe, das
        # Kapitell unter dem Binder und die Rautenfelder.
        shoot("stuetze", Vector((-HALLE_L * 0.5 + 3.4, -3.9, 1.40)),
              Vector((-HALLE_L * 0.5 + 2.0, -2.35, 2.60)), breite=1600)
        # Trog von oben (key/933): Roste neben dem Gleis, Trogwaende,
        # gelbe Sicherheitslinie auf dem Bahnsteig.
        shoot("trog", Vector((-1.0, 0.0, 4.00)), Vector((1.6, 0.0, -0.60)),
              breite=1600)

    if tag == "wagen":
        # Die beweglichen Teile (Zeiger, Schwimmer, Kurbelarm) sind eigene
        # Meshes und liegen im Ursprung - hier an ihre Einbaulage setzen,
        # sonst zeigt das Kontrollbild beim Fuehrerstand eine leere Platte und
        # ein leeres Schauglas. Dieselbe Neigung wie das Wagen-Mesh
        # (tilt_grade), nur als Objekt-Transformation.
        theta = math.atan(GRADE)
        dreh = Matrix.Rotation(-theta, 4, "Y")
        for name, anker in DETAIL_ANKER.items():
            teil = bpy.data.objects.get(name)
            if teil is None:
                log("Warnung: %s fehlt - Kontrollbild ohne dieses Teil" % name)
                continue
            teil.location = dreh @ Vector(anker)
            teil.rotation_euler = (0.0, -theta, 0.0)
            teil.hide_render = False

        # Nahansichten der Aussenwand, gerade von der Seite. Schriftzug
        # (0,17 m Versalhoehe) und Wasserstandsskala (0,16 m breit) sind in
        # der Gesamtansicht nur wenige Pixel breit; ohne diese zwei Bilder
        # laesst sich nicht pruefen, ob die Schrift aufrecht steht und die
        # Ziffern 10/20/30/40 in der richtigen Reihenfolge liegen. Ziel ist
        # die Wand unterhalb der Fenster (0,31 der Wagenhoehe), leicht nach
        # +X versetzt, damit der Schriftzug UND die Skala am Wagenende in
        # einem Bild liegen.
        ziel = Vector((0.40, 0.0, size[2] * 0.31))
        # Doppelte Aufloesung: die Ziffern der Wasserstandsskala sind rund
        # 0,10 m hoch und die Skala nur 0,16 m breit - in 960 px Bildbreite
        # verschwinden Striche und Ziffern im Bildrauschen.
        for vname, ys in (("wand", -1.0), ("wand_gegen", 1.0)):
            shoot(vname, Vector((ziel.x, ys * 6.5, ziel.z)), ziel, breite=1920)

        # Innenraum: genau die Ansicht, die der Mitfahrer im Spiel hat -
        # Kamera im Gang auf Augenhoehe (Wagenboden 0,85 m + 1,60 m = 2,45 m
        # ueber dem Ursprung), Blick zum Fuehrerstand am Bergende.
        # Ohne diese Bilder ist die Inneneinrichtung nur im Spiel zu pruefen,
        # und dort kostet jeder Versuch einen Spielstart.
        shoot("innen", Vector((0.10, 0.0, 2.45)), Vector((1.95, 0.0, 1.60)),
              breite=1920)
        # Blick zur Seite über Bank und Fenster (Sitzplatzsicht)
        shoot("innen_seite", Vector((-1.30, -0.30, 2.40)), Vector((0.60, 0.95, 1.45)),
              breite=1920)
        # Fuehrerstand von nahem: Tacho, Schauglas mit Schwimmer, Kurbel
        shoot("fuehrerstand", Vector((0.30, 0.05, 2.35)), Vector((1.90, 0.10, 1.62)),
              breite=1920)
    bpy.data.objects.remove(cam, do_unlink=True)


def main():
    out_dir = os.path.abspath(arg_value("--out", OUT_DIR_DEFAULT))
    os.makedirs(out_dir, exist_ok=True)

    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    setup_world()

    manifest = {"assets": []}

    # Die beweglichen Teile stehen VOR dem Wagen in der Liste: sie werden
    # gleich an ihre Einbaulage gesetzt und erscheinen so auf den
    # Kontrollbildern des Wagens (der Wagen allein haette sonst weder Zeiger
    # noch Schwimmer noch Kurbelarm).
    builders = [
        ("tacho_zeiger", lambda: build_tacho_zeiger()),
        ("schwimmer", lambda: build_schwimmer()),
        ("kurbel", lambda: build_kurbel()),
        ("wagen", lambda: build_wagen()),
        # EIN Hallen-Asset fuer beide Stationen: das Vorbild hat denselben
        # Bautyp an beiden Enden (§7.1); die Bergstation wird im Actor um
        # 180 Grad gedreht gesetzt.
        ("bahnsteighalle", lambda: build_bahnsteighalle()),
        ("viadukt", lambda: build_viadukt()),
        ("schiene", lambda: build_schiene()),
        ("zahnstange", lambda: build_zahnstange()),
        ("seilkanal", lambda: build_seilkanal()),
        ("schwelle", lambda: build_schwelle()),
        ("schotterbett", lambda: build_schotterbett()),
    ]

    # Bauteile mit festgelegtem Ursprung: der Wagen auf der Schienenkontaktlinie
    # in Wagenmitte, die Trassenteile auf der Schienenoberkante in Trassenmitte,
    # die beweglichen Teile auf ihrem Drehpunkt (TACHO_MITTE, SCHWIMMER_*,
    # KURBEL_ACHSE). Wer sie XY-zentriert, zieht die Bezugslinie weg.
    FIXER_URSPRUNG = {"wagen", "schiene", "zahnstange", "seilkanal",
                      "schwelle", "schotterbett", "tacho_zeiger",
                      "schwimmer", "kurbel", "bahnsteighalle"}
    # Diese drei werden nicht einzeln gerendert (5-14 cm grosse Teile sagen in
    # der Gesamtansicht nichts) - sie stehen auf den Wagenbildern.
    KEIN_EINZELBILD = {"tacho_zeiger", "schwimmer", "kurbel"}

    for tag, fn in builders:
        obj, mat_names, _dims = fn()
        fest = tag in FIXER_URSPRUNG
        size = center_origin_xy(obj, keep_z=fest, keep_xy=fest)
        path = os.path.join(out_dir, obj.name + ".fbx")
        # Export OHNE Objekt-Transformation: die beweglichen Teile werden
        # gleich zum Rendern an ihren Anker gesetzt, im Spiel setzt sie der
        # Actor - im FBX muss ihr Ursprung auf dem Drehpunkt liegen.
        export_fbx(obj, path)
        if tag not in KEIN_EINZELBILD:
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
        # naechsten Einzelrender ausblenden. Nach dem Wagen werden auch die
        # beweglichen Teile wieder ausgeblendet (render_views hat sie fuer die
        # Wagenbilder an ihren Anker gesetzt).
        obj.hide_render = True
        if tag == "wagen":
            for name in DETAIL_ANKER:
                teil = bpy.data.objects.get(name)
                if teil is not None:
                    teil.hide_render = True

    with open(os.path.join(out_dir, "nerobergbahn.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    log("FERTIG: %d Assets nach %s" % (len(manifest["assets"]), out_dir))


main()
