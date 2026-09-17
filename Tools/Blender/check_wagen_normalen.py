"""Prueft am exportierten FBX, wohin die Normalen zeigen.

Warum das die wichtigste Frage fuer den Innenraum ist: Sitzt der Fahrgast IM
Wagen, sieht er nur Flaechen, deren Normale zu ihm zeigt. Zeigt die
Wagenhaut nach aussen (Normalkonvention "outward"), ist die Innenseite eines
jeden Wand-/Boden-BALKENS ebenfalls sichtbar (er ist ein geschlossener
Koerper) - der Innenraum ist dann einfach da. Zeigt sie nach innen, sieht man
vom Kabineninneren nur die Rueckseiten und damit nichts.

Gemessen wird am FBX (also an dem, was Unreal importiert), nicht am
Blender-Mesh - der Importeur koennte die Windung drehen.

Aufruf:
  blender.exe -b -P check_wagen_normalen.py -- <pfad/zum/fbx>
"""

import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
PFAD = argv[0] if argv else ("C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/"
                             "Data/Raw/Nerobergbahn/SM_WbNbWagen.fbx")


def log(msg):
    print("###NORM### %s" % msg)


for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)

bpy.ops.import_scene.fbx(filepath=PFAD)
obj = [o for o in bpy.data.objects if o.type == "MESH"][0]
me = obj.data
log("Mesh %s: %d Flaechen" % (obj.name, len(me.polygons)))

# 1. Aeusserste Seitenwand: die Flaeche mit dem groessten Y.
poly = max(me.polygons, key=lambda p: p.center.y)
log("Aussenwand max Y: center=(%.2f, %.2f, %.2f) Normale=(%.2f, %.2f, %.2f)"
    % (poly.center.x, poly.center.y, poly.center.z,
       poly.normal.x, poly.normal.y, poly.normal.z))

# 2. Boden: groesste waagerechte Flaeche um die Wagenmitte.
kandidaten = [p for p in me.polygons
              if abs(p.normal.z) > 0.9 and abs(p.center.x) < 1.0
              and abs(p.center.y) < 0.6]
if kandidaten:
    boden = max(kandidaten, key=lambda p: p.area)
    log("Bodenflaeche: center z=%.2f Normale z=%+.2f  (Vorder- oder Rueckseite "
        "des Bodenbalkens)" % (boden.center.z, boden.normal.z))

# 3. Anteil der Flaechen, deren Normale vom Wagenzentrum WEG zeigt (outward).
zentrum = Vector((0.0, 0.0, 1.6))
raus = 0
rein = 0
for p in me.polygons:
    d = p.center - zentrum
    if d.length < 0.05:
        continue
    if p.normal.dot(d.normalized()) > 0:
        raus += 1
    else:
        rein += 1
log("Flaechen mit Normale VOM Zentrum weg: %d, zum Zentrum hin: %d" % (raus, rein))
log("(geschlossene Balken: 'zum Zentrum hin' sind die dem Blick zugewandten "
    "Innenflaechen der Waende - sie MUESSEN existieren, sonst ist der "
    "Innenraum durchsichtig)")
