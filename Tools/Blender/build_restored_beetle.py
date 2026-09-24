"""Build a clean, wheel-free 1969 Beetle shell for the drivable player car.

The scanned source shell has holes and severe texture bleed.  This replacement
keeps its measured wheelbase/pivot and uses four separately animated old wheels.
Blender 5.2: blender -b -P Tools/Blender/build_restored_beetle.py
"""

import bpy
import math
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Data/Raw/Beetle/Herbie/restored_beetle_body.glb'

bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, rgb, roughness=.36, metal=0):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*rgb, 1)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes['Principled BSDF']
    bsdf.inputs['Base Color'].default_value = (*rgb, 1)
    bsdf.inputs['Roughness'].default_value = roughness
    bsdf.inputs['Metallic'].default_value = metal
    return mat


PAINT = material('WB_Beetle_CreamPaint', (.82, .80, .72), .31)
RED = material('WB_Beetle_RedStripe', (.58, .025, .032), .34)
BLUE = material('WB_Beetle_BlueStripe', (.018, .047, .28), .34)
WHITE = material('WB_Beetle_WhiteStripe', (.91, .89, .82), .33)
CHROME = material('WB_Beetle_Chrome', (.64, .69, .71), .18, .85)
RUBBER = material('WB_Beetle_Trim', (.018, .020, .022), .82)
GLASS = material('WB_Beetle_Glass', (.045, .082, .105), .16, .15)
LENS = material('WB_Beetle_HeadLens', (.88, .91, .82), .17)
TAIL = material('WB_Beetle_TailLens', (.48, .008, .013), .18)
AMBER = material('WB_Beetle_AmberLens', (.84, .32, .025), .21)
BLACK = material('WB_Beetle_Number', (.015, .016, .018), .55)


def mesh(name, verts, faces, mat):
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    data.update()
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    data.materials.append(mat)
    for face in data.polygons:
        face.use_smooth = True
    return obj


def cube(name, loc, scale, mat, bevel=0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod = obj.modifiers.new('rounded pressings', 'BEVEL')
        mod.width = bevel
        mod.segments = 3
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=mod.name)
    obj.data.materials.append(mat)
    return obj


def ball(name, loc, scale, mat):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, location=loc)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    obj.data.materials.append(mat)
    for face in obj.data.polygons:
        face.use_smooth = True
    return obj


def rod(name, points, radius, mat):
    curve = bpy.data.curves.new(name, 'CURVE')
    curve.dimensions = '3D'
    curve.resolution_u = 12
    curve.bevel_depth = radius
    curve.bevel_resolution = 3
    spline = curve.splines.new('POLY')
    spline.points.add(len(points) - 1)
    for point, coords in zip(spline.points, points):
        point.co = (*coords, 1)
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    curve.materials.append(mat)
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.convert(target='MESH')
    return bpy.context.object


# X forward, Y to car's right, Z up; units in metres, origin at tire contact.
RINGS = [
    (-2.05, .67, .33, .44), (-1.94, .77, .51, .47),
    (-1.70, .89, .67, .49), (-1.30, 1.05, .75, .53),
    (-.95, 1.28, .73, .58), (-.58, 1.47, .71, .64),
    (-.20, 1.52, .71, .67), (.25, 1.49, .71, .65),
    (.62, 1.36, .72, .60), (.89, 1.16, .73, .54),
    (1.23, .97, .73, .50), (1.58, .86, .69, .47),
    (1.92, .76, .52, .44), (2.08, .64, .27, .43),
]


def profile(x):
    for left, right in zip(RINGS, RINGS[1:]):
        if x <= right[0]:
            t = max(0, min(1, (x - left[0]) / (right[0] - left[0])))
            return tuple(a * (1 - t) + b * t for a, b in zip(left[1:], right[1:]))
    return RINGS[-1][1:]


def top_z(x, y):
    top, width, edge = profile(x)
    return edge + (top - edge) * max(0, 1 - (y / width)**2)**.48


# Single manifold shell with a rounded roof, bonnet, and rear deck.
verts, faces = [], []
ring_count = 17 + 4
for x, top, width, edge in RINGS:
    for i in range(17):
        y = width * (i / 8 - 1)
        verts.append((x, y, top_z(x, y)))
    verts.extend([(x, width, .40), (x, width * .88, .32),
                  (x, -width * .88, .32), (x, -width, .40)])
for i in range(len(RINGS) - 1):
    for j in range(ring_count):
        a = i * ring_count + j
        b = i * ring_count + (j + 1) % ring_count
        c = (i + 1) * ring_count + (j + 1) % ring_count
        d = (i + 1) * ring_count + j
        faces.append((a, b, c, d))
faces.append(tuple(reversed(range(ring_count))))
faces.append(tuple((len(RINGS) - 1) * ring_count + j for j in range(ring_count)))
body = mesh('Clean pressed Beetle shell', verts, faces, PAINT)

# Open both wheel arches through the lower shell so tires are visible.
for axle, x in [('rear', -1.12), ('front', 1.30)]:
    bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=.425, depth=2.6,
                                        location=(x, 0, .33),
                                        rotation=(math.pi / 2, 0, 0))
    cutter = bpy.context.object
    cutter.name = 'arch cutter ' + axle
    mod = body.modifiers.new('wheel arch ' + axle, 'BOOLEAN')
    mod.operation = 'DIFFERENCE'
    mod.solver = 'EXACT'
    mod.object = cutter
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)

# Pressed fender crowns and dark wheel-arch lips.
for axle, x in [('rear', -1.12), ('front', 1.30)]:
    for side in (-1, 1):
        for inner, outer, mat, label in [(.415, .55, PAINT, 'fender'),
                                          (.395, .417, RUBBER, 'arch lip')]:
            v, f = [], []
            for step in range(25):
                angle = math.pi * step / 24
                for radius, y in [(inner, side * .77), (outer, side * .82)]:
                    v.append((x + radius * math.cos(angle), y,
                              .34 + radius * math.sin(angle)))
            for step in range(24):
                a = 2 * step
                f.append((a, a + 1, a + 3, a + 2))
            mesh('%s %s %s' % (axle, side, label), v, f, mat)

# Tinted glass panels and thin chrome borders. The arched roof remains a
# solid surface, keeping the shell closed and free from scanned holes.
def skin(points):
    """Lay a glass pane directly on the curved shell, without hovering."""
    return [(x, y, top_z(x, y) + .012) for x, y in points]


def panel(name, verts):
    # A single quad cuts through the curved roof in its middle. Tessellate
    # the patch against the same shell equation used by the body rings.
    xy = [(point[0], point[1]) for point in verts]
    v, f = [], []
    for u in range(13):
        fu = u / 12
        for w in range(9):
            fw = w / 8
            a = Vector(xy[0]).lerp(Vector(xy[1]), fu)
            b = Vector(xy[3]).lerp(Vector(xy[2]), fu)
            x, y = a.lerp(b, fw)
            v.append((x, y, top_z(x, y) + .022))
    for u in range(12):
        for w in range(8):
            a = u * 9 + w
            f.append((a, a + 9, a + 10, a + 1))
    mesh(name, v, f, GLASS)
    outline = []
    for start, end in zip(xy, xy[1:] + xy[:1]):
        for step in range(8):
            x, y = Vector(start).lerp(Vector(end), step / 8)
            outline.append((x, y, top_z(x, y) + .03))
    rod(name + ' seal', outline + [outline[0]], .011, RUBBER)
    rod(name + ' trim', [(x, y + (.009 if y > 0 else -.009), z)
                         for x, y, z in outline + [outline[0]]], .005, CHROME)


for side in (-1, 1):
    panel('front side glass %s' % side,
          skin([(.04, side*.70), (.75, side*.69),
                (.46, side*.49), (.02, side*.49)]))
    panel('rear side glass %s' % side,
          skin([(-.83, side*.70), (-.05, side*.70),
                (-.06, side*.49), (-.47, side*.50)]))
    rod('door gap %s' % side,
        [(.02, side*.756, .95), (.02, side*.754, .42)], .006, RUBBER)
    rod('door chrome %s' % side,
        [(.45, side*.749, .97), (.63, side*.744, .97)], .012, CHROME)
    # Spiegel am Tuerrahmen mit Arm - frueher schwebte die Kugel 14 cm neben
    # der Scheibe ohne Verbindung zur Karosserie.
    rod('side mirror arm %s' % side,
        [(.70, side*.71, .76), (.68, side*.78, .86), (.66, side*.81, .90)], .012, CHROME)
    ball('side mirror %s' % side, (.66, side*.83, .92), (.05, .06, .045), CHROME)

panel('windscreen', skin([(.91,-.60), (.91,.60),
                          (.47,.49), (.47,-.49)]))
panel('rear window', skin([(-1.10,.58), (-1.10,-.58),
                           (-.50,-.49), (-.50,.49)]))

# Painted Herbie tricolour follows bonnet, roof, and rear deck; the glass
# interruptions are intentional rather than stripes pasted onto windows.
for lo, hi in [(-2.02,-1.12), (-.49,.45), (.93,2.03)]:
    for label, y0, y1, mat in [('red', .055, .19, RED),
                               ('white', -.055, .055, WHITE),
                               ('blue', -.19, -.055, BLUE)]:
        v, f = [], []
        for i in range(33):
            x = lo + (hi - lo) * i / 32
            for y in (y0, y1):
                v.append((x,y,top_z(x,y)+.011))
        for i in range(32):
            a=2*i
            f.append((a,a+1,a+3,a+2))
        mesh('painted %s stripe %.2f' % (label,lo), v, f, mat)


def circle(name, center, radius, mat, side):
    x,y,z=center
    v=[center]
    for i in range(48):
        a=math.tau*i/48
        v.append((x+radius*math.cos(a),y,z+radius*math.sin(a)))
    f=[(0, i+1, (i+1)%48+1) for i in range(48)]
    obj=mesh(name,v,f,mat)
    if side>0:
        # Material is two sided in Unreal; winding still face the viewer.
        for poly in obj.data.polygons:
            poly.flip()
    return obj


def side_number(side):
    y=side*.771
    circle('black 53 roundel %s' % side, (-.12,y,.78),.245,BLACK,side)
    circle('white 53 disc %s' % side, (-.12,y+side*.003,.78),.224,WHITE,side)
    font=bpy.data.curves.new('Herbie 53','FONT')
    font.body='53'
    font.align_x='CENTER'
    font.align_y='CENTER'
    font.size=.285
    font.extrude=.001
    ob=bpy.data.objects.new('door number 53 %s' % side,font)
    bpy.context.collection.objects.link(ob)
    # Local text X points toward the front from either outside view.
    local_x=Vector((-side,0,0))
    local_y=Vector((0,0,1))
    local_z=local_x.cross(local_y)
    ob.rotation_euler=Matrix((local_x,local_y,local_z)).transposed().to_euler()
    ob.location=(-.12,y+side*.007,.695)
    font.materials.append(BLACK)
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active=ob
    bpy.ops.object.convert(target='MESH')


for side in (-1,1):
    side_number(side)
    # Lampen SITZEN auf den Kotfluegeln: frueher lag der Scheinwerfer 20 cm
    # ueber der Bugflaeche (Karosserie bei x=1,875/y=0,52 nur 0,54 m hoch) und
    # der Blinker schwebte seitlich neben dem Bug. Die Lackbeulen darunter
    # verbinden Lampentopf und Kotfluegel wie beim echten Kaefer.
    # Positionen MUESSEN zu UWiesbadenCarLightsComponent passen (cm-Offsets).
    ball('headlamp fender pod %s' % side,(1.60,side*.50,.74),(.22,.17,.13),PAINT)
    ball('front chrome lamp rim %s' % side,(1.70,side*.50,.80),(.11,.18,.18),CHROME)
    ball('headlamp glass %s' % side,(1.789,side*.50,.80),(.045,.145,.145),LENS)
    ball('tail lamp fender pod %s' % side,(-1.72,side*.50,.70),(.20,.16,.12),PAINT)
    ball('rear chrome lamp rim %s' % side,(-1.80,side*.50,.74),(.16,.15,.17),CHROME)
    ball('red tail/brake lens %s' % side,(-1.903,side*.50,.74),(.067,.118,.13),TAIL)
    ball('amber rear blinker %s' % side,(-1.905,side*.50,.87),(.03,.07,.035),AMBER)
    ball('amber front blinker %s' % side,(1.45,side*.66,.67),(.09,.05,.045),AMBER)
    rod('bumper overrider front %s' % side,
        [(2.075,side*.52,.33),(2.095,side*.52,.55)],.025,CHROME)
    rod('bumper overrider rear %s' % side,
        [(-2.085,side*.52,.33),(-2.105,side*.52,.55)],.025,CHROME)

for x,label in [(2.08,'front'),(-2.08,'rear')]:
    rod(label+' chrome bumper',[(x,-.69,.43),(x+(.045 if x>0 else -.045),0,.43),
                                (x,.69,.43)],.027,CHROME)

rod('left running board',[(.90,-.777,.36),(-.77,-.777,.36)],.04,RUBBER)
rod('right running board',[(.90,.777,.36),(-.77,.777,.36)],.04,RUBBER)
rod('front bonnet seam',[(1.03,-.63,.96),(1.23,-.68,.93),(1.93,-.40,.75),
                          (2.03,0,.67),(1.93,.40,.75),(1.23,.68,.93),
                          (1.03,.63,.96)],.006,RUBBER)
rod('rear engine lid seam',[(-1.13,-.63,1.09),(-1.62,-.64,.89),
                            (-2.0,-.30,.68),(-2.0,.30,.68),
                            (-1.62,.64,.89),(-1.13,.63,1.09)],.006,RUBBER)

# Export a single mesh; materials remain separate named slots for Unreal.
objects=[obj for obj in bpy.context.scene.objects if obj.type=='MESH']
bpy.ops.object.select_all(action='DESELECT')
for obj in objects:
    obj.select_set(True)
bpy.context.view_layer.objects.active=body
bpy.ops.object.join()
body.name='SM_VWBeetle1969_Restored'
OUT.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.export_scene.gltf(filepath=str(OUT),export_format='GLB',
                          use_selection=True,export_apply=True,export_yup=True)
print('###RESTORED_BEETLE###',OUT,'vertices',len(body.data.vertices),
      'materials',len(body.data.materials),'dimensions',tuple(round(x,3) for x in body.dimensions),flush=True)
