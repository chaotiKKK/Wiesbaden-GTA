"""Gemeinsamer Rig-Baukasten fuer Tripo-Figuren (Iris, Denno).

Eine Tripo-Figur kommt als GLB in T-Pose, in mehreren losen Teilen, ohne
Skelett. TripoRig macht daraus eine animierbare Spielfigur:

  1. Teile zusammenfuegen, Materialien/Texturen sauber benennen und auf
     hoechstens 512 px verkleinern (PNG nach tex_dir), dezimieren.
  2. Arme finden: T-Pose -> per Gewichtsfeld senken (wie build_denno_figure.py);
     haengende Arme (arms='unten') -> Schicht fuer Schicht an der Luecke
     zwischen Arm und Rumpf erkennen, nichts verbiegen; am Koerper angeformte
     Arme (arms='starr', keine Luecke) -> die Arme bleiben Teil des Rumpfs,
     die Armknochen sind da, bewegen aber nichts.
  3. Schritt finden, Beinfeld bestimmen.
  4. Masstab, Blick +X, seitlich Y (+Y = links), Boden Z = 0.
  5. Skelett aus der Geometrie (Gelenke aus Arm-/Beinecken) und Gewichte
     NACH BAUPLAN - Waermediffusion ("automatic weights") scheitert an den
     losen Tripo-Inseln (Arme, Kopf, Schuhe).
  6. Optional ein Augenlid-Knochen (eyelid='R'): er staucht die Augenpartie
     senkrecht - zum Zwinkern, ohne Gesichtsrig.

Dazu make_action (Schluesselbilder aus einer pose_at-Funktion), render
(Kontrollbilder - Vorzeichen SEHEN statt herleiten) und export (FBX wie
rig_sebbo.py: axis_forward -Z, up Y, bringt Blick +X unverdreht nach Unreal).

Knochenachsen (align_roll, rig_sebbo.py): Rumpfknochen zeigen nach oben -
lokal Y = Hochachse, X = Querachse (neigen), Z = Blickachse nach hinten
(seitlich kippen). Arm- und Beinknochen zeigen nach unten - X = Querachse
(vor/zurueck schwingen), Z = seitlich heben.
"""
import math
import os
import bpy
from mathutils import Vector
from mathutils.kdtree import KDTree

FPS = 30
# Vorzeichen aus den Kontrollbildern (Iris, 25.09.2026): +X am Bein-/Armknochen
# schwingt hier nach HINTEN (bei Sebbo nach vorn - die Rollung entscheidet).
FWD = -1.0
KNEE = -FWD          # Knie beugt nach hinten
ABDUCT_R = +1.0      # Drehung um Z hebt den RECHTEN Arm seitlich nach aussen
ABDUCT_L = -ABDUCT_R
BREATH_WIDTH, BREATH_RISE = 0.012, 0.004   # wie Denno im Cafe (ComputeDennoIdle)


def smoothstep(a, b, x):
    if b <= a:
        return 1.0 if x >= b else 0.0
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def _mean(points):
    points = list(points)
    return sum(points, Vector()) / max(len(points), 1)


class TripoRig:
    def __init__(self, src, name, height_m, target_tris, tex_dir, preview_dir,
                 arm_outward_deg=10.0, texture_max=512, texture_min=128, eyelid=None,
                 crotch_frac=None, preview_exposure=0.0, arms='T', facing='-Y'):
        """crotch_frac: Schritthoehe als Anteil der Figurhoehe VORGEBEN statt suchen -
        bei eng anliegenden Hosen beruehren sich die Oberschenkel weit unter dem
        Schritt, und die Suche fand diese Stelle (Denno: Huefte 0,63 statt 0,87 m).
        Dann kommt auch die Beinmitte aus den Beinen statt aus der ganzen Figur
        (eine Umhaengetasche verschiebt den Schwerpunkt)."""
        self.name = name
        self.H = height_m
        self.preview_dir = preview_dir
        self.crotch_frac = crotch_frac
        self.arms = arms
        # Tripo blickt ueblicherweise nach -Y; manche Figuren nach +Y (Mira) -
        # dann wird um 180 Grad gedreht und links/rechts getauscht.
        self.flip = facing == '+Y'
        self.preview_exposure = preview_exposure
        self.tag = '###%s' % name.upper()
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.render.fps = FPS
        self._load(src)
        self._textures(tex_dir, texture_max, texture_min)
        # Haengende Arme auf dem DICHTEN Netz erkennen: nach dem Dezimieren liegen
        # die Ecken 1-2 cm auseinander, und jede Schicht zerfiel in Scheinluecken
        # (Mira: die Beine wurden bis Kniehoehe als Arme erkannt).
        dense = self._hanging_arms_dense() if arms == 'unten' else None
        self._decimate(target_tris)
        if arms == 'T':
            self._lower_arms(arm_outward_deg)
        elif arms == 'unten':
            self._find_hanging_arms(dense)
        else:
            # Starr: keine Armecken. Die Schulterhoehe schaetzt die Proportion
            # (Achsel ~0,75, Schultergelenk ~0,82 der Hoehe).
            self.arm_weight = [0.0] * len(self.verts)
            self.arm_z = min(v.co.z for v in self.verts) + 0.82 * self.height
        self._legs()
        self._to_final()
        self._joints()
        self._armature(eyelid)
        self._weights(eyelid)
        self.anim_bones = [b.name for b in self.rig.data.bones if b.name != 'Root']

    def log(self, msg):
        print('%s %s' % (self.tag, msg))

    # -- 1. Figur ------------------------------------------------------------
    def _load(self, src):
        bpy.ops.import_scene.gltf(filepath=str(src))
        parts = [o for o in bpy.context.scene.objects if o.type == 'MESH']
        for o in parts:
            o.select_set(True)
        bpy.context.view_layer.objects.active = parts[0]
        bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        bpy.ops.object.join()
        body = bpy.context.view_layer.objects.active
        body.name = 'SK_%s' % self.name
        body.data.name = 'SK_%s' % self.name
        for o in list(bpy.context.scene.objects):
            if o != body:
                bpy.data.objects.remove(o, do_unlink=True)
        self.body = body

    def _textures(self, tex_dir, texture_max, texture_min):
        # Saubere Namen statt der Tripo-Vorgabe ("..._tripo_part_0_basecolor"):
        # Materialschlitz M_<Name>_PartN, Textur T_<Name>_PartN.png - der Import
        # haengt die Texturen ueber diesen Namen an die Materialien.
        os.makedirs(tex_dir, exist_ok=True)
        for mat in self.body.data.materials:
            part = mat.name.split('tripo_part_')[-1].split('_')[0]
            mat.name = 'M_%s_Part%s' % (self.name, part)
            for node in mat.node_tree.nodes:
                if node.type == 'TEX_IMAGE' and node.image:
                    img = node.image
                    img.name = 'T_%s_Part%s' % (self.name, part)
                    w, h = img.size
                    edge = max(w, h)
                    if edge > texture_max:
                        f = max(texture_max / edge, texture_min / min(w, h))
                        img.scale(max(texture_min, round(w * f)), max(texture_min, round(h * f)))
                    img.filepath_raw = os.path.join(str(tex_dir), 'T_%s_Part%s.png' % (self.name, part))
                    img.file_format = 'PNG'
                    img.save()
                    self.log('texture %s %s -> %s' % (img.name, (w, h), tuple(img.size)))

    def _decimate(self, target_tris):
        body = self.body
        tris = sum(len(p.vertices) - 2 for p in body.data.polygons)
        mod = body.modifiers.new('decimate', 'DECIMATE')
        mod.ratio = min(1.0, target_tris / max(tris, 1))
        bpy.ops.object.modifier_apply(modifier=mod.name)
        self.log('decimate %d -> %d' % (tris, len(body.data.polygons)))
        self.verts = body.data.vertices
        zs = [v.co.z for v in self.verts]
        self.height = max(zs) - min(zs)

    # -- 2. Arme senken (Tripo-Rahmen: Blick -Y, seitlich X) ------------------
    def _lower_arms(self, arm_outward_deg):
        verts, height = self.verts, self.height
        span = max(abs(v.co.x) for v in verts)
        far = [v.co for v in verts if abs(v.co.x) > 0.55 * span]
        arm_z = sum(p.z for p in far) / len(far)
        band = [abs(v.co.x) for v in verts if arm_z - 0.16 * height < v.co.z < arm_z - 0.09 * height]
        shoulder_x = sorted(band)[int(len(band) * 0.97)]
        self.log('arm_z %.3f shoulder_x %.3f height %.3f' % (arm_z, shoulder_x, height))
        theta = math.radians(90.0 - arm_outward_deg)
        self.arm_weight = [0.0] * len(verts)
        for v in verts:
            p = v.co
            w = smoothstep(shoulder_x - 0.015 * height, shoulder_x + 0.05 * height, abs(p.x)) \
                * smoothstep(arm_z - 0.11 * height, arm_z - 0.05 * height, p.z)
            self.arm_weight[v.index] = w
            if w <= 0.0:
                continue
            side = 1.0 if p.x > 0 else -1.0
            t = theta * side
            px, pz = p.x - side * shoulder_x, p.z - arm_z
            rx = px * math.cos(t) + pz * math.sin(t)
            rz = -px * math.sin(t) + pz * math.cos(t)
            v.co = p.lerp(Vector((rx + side * shoulder_x, p.y, rz + arm_z)), w)
        self.arm_z = arm_z

    def _hanging_arms_dense(self):
        """Arme, die schon haengen (A-Pose, Arme am Koerper), auf dem dichten
        Netz: in waagerechten Schichten zwischen Knie- und Schulterhoehe sind die
        Arme die AEUSSERSTEN, schmalen Stuecke links und rechts, durch eine Luecke
        vom Rest getrennt - und das naechste Stueck nach innen ist Rumpf oder das
        eigene Bein (liegt es ganz auf der anderen Seite, sind es zwei Beine).
        Liefert (Punkte, Armgewichte, Achselhoehe, Rumpf-Halbbreite, tiefste
        Armschicht je Seite) im Tripo-Rahmen."""
        coords = [v.co.copy() for v in self.body.data.vertices]
        zs = [c.z for c in coords]
        z0, height = min(zs), max(zs) - min(zs)
        low = [c.x for c in coords if c.z < z0 + 0.25 * height]
        x_mid = (min(low) + max(low)) / 2
        slice_h, gap, narrow = 0.01 * height, 0.006 * height, 0.075 * height
        n = len(coords)
        weights = [0.0] * n
        order = sorted(range(n), key=lambda i: zs[i])
        lowest, highest, torso_edge = {}, {}, {}
        k, z = 0, z0 + 0.25 * height
        while z < z0 + 0.80 * height:
            while k < n and zs[order[k]] < z:
                k += 1
            j = k
            while j < n and zs[order[j]] < z + slice_h:
                j += 1
            layer = sorted(order[k:j], key=lambda i: coords[i].x)
            if len(layer) > 50:
                groups, cur = [], [layer[0]]
                for a, b in zip(layer, layer[1:]):
                    if coords[b].x - coords[a].x > gap:
                        groups.append(cur)
                        cur = []
                    cur.append(b)
                groups.append(cur)
                if len(groups) > 1:
                    for side, group, inner in (('R', groups[0], groups[1]), ('L', groups[-1], groups[-2])):
                        right = side == 'R'
                        width = coords[group[-1]].x - coords[group[0]].x
                        outside = all((coords[i].x < x_mid) if right else (coords[i].x > x_mid) for i in group)
                        other_side = all((coords[i].x > x_mid) if right else (coords[i].x < x_mid) for i in inner)
                        if outside and width < narrow and not other_side:
                            for i in group:
                                weights[i] = 1.0
                            lowest[side] = min(lowest.get(side, z), z)
                            highest[side] = max(highest.get(side, z), z)
                            edge = coords[inner[-1]].x if right else coords[inner[0]].x
                            torso_edge[side] = abs(edge - x_mid)
            z += slice_h
        if 'L' not in highest or 'R' not in highest:
            raise RuntimeError('Haengende Arme nicht gefunden (Luecke Arm-Rumpf fehlt)')
        self.log('dicht: %d Ecken, Arme in %d Ecken, Achsel %.3f, tiefste Armschicht L %.3f R %.3f' % (
            n, sum(1 for w in weights if w > 0), min(highest.values()) - z0, lowest['L'] - z0, lowest['R'] - z0))
        return coords, weights, x_mid, min(highest.values()), max(torso_edge.values()), lowest

    def _find_hanging_arms(self, dense):
        """Die dicht erkannten Arme aufs dezimierte Netz uebertragen (naechster
        dichter Punkt), dann Achsel bis Schulter weich dazunehmen und Luecken
        (Hand am Oberschenkel) unterhalb der tiefsten erkannten Schicht fuellen."""
        coords, dense_w, x_mid, armpit, shoulder_x, lowest = dense
        verts, height = self.verts, self.height
        tree = KDTree(len(coords))
        for i, c in enumerate(coords):
            tree.insert(c, i)
        tree.balance()
        n = len(verts)
        self.arm_weight = [dense_w[tree.find(v.co)[1]] for v in verts]
        for v in verts:
            p = v.co
            if p.z < armpit or p.z > armpit + 0.10 * height:
                continue
            w = smoothstep(shoulder_x - 0.01 * height, shoulder_x + 0.03 * height, abs(p.x - x_mid)) \
                * (1.0 - smoothstep(armpit + 0.04 * height, armpit + 0.10 * height, p.z))
            self.arm_weight[v.index] = max(self.arm_weight[v.index], w)
        for side, sign in (('L', 1.0), ('R', -1.0)):
            arm_x = [verts[i].co.x for i in range(n) if self.arm_weight[i] >= 1.0
                     and (verts[i].co.x - x_mid) * sign > 0 and verts[i].co.z < lowest[side] + 0.05 * height]
            if not arm_x:
                continue
            reach = min(abs(x - x_mid) for x in arm_x)
            for v in verts:
                if lowest[side] - 0.05 * height < v.co.z < lowest[side] and (v.co.x - x_mid) * sign >= reach:
                    self.arm_weight[v.index] = 1.0
        self.arm_z = armpit + 0.07 * height
        self.log('haengende Arme: Achsel %.3f, Rumpf halb %.3f, %d Arm-Ecken' % (
            armpit - min(v.co.z for v in verts), shoulder_x, sum(1 for w in self.arm_weight if w > 0.5)))

    # -- 3. Schritt und Beinfeld ----------------------------------------------
    def _legs(self):
        verts, height = self.verts, self.height
        z0 = min(v.co.z for v in verts)
        if self.crotch_frac is not None:
            crotch_z = z0 + self.crotch_frac * height
            legs = [v.co.x for v in verts if v.co.z < crotch_z and self.arm_weight[v.index] <= 0.0]
            x_mid = (min(legs) + max(legs)) / 2
        else:
            x_mid = sum(v.co.x for v in verts) / len(verts)
            gap = 0.012 * height
            crotch_z = 0.46 * height
            z = 0.15 * height
            while z < 0.65 * height:
                if any(abs(v.co.x - x_mid) < gap and z <= v.co.z < z + 0.01 * height for v in verts):
                    crotch_z = z
                    break
                z += 0.005 * height
        self.leg_weight = [0.0] * len(verts)
        for v in verts:
            if self.arm_weight[v.index] > 0.0:
                continue   # die gesenkten Haende reichen bis zur Huefte - sie gehoeren dem Arm
            self.leg_weight[v.index] = (1.0 - smoothstep(crotch_z - 0.03 * height, crotch_z + 0.06 * height, v.co.z)) \
                * smoothstep(0.0, 0.025 * height, abs(v.co.x - x_mid))
        # Tripo +X = Figur links (bei Blick +Y: rechts).
        self.side_of = [(1.0 if v.co.x > x_mid else -1.0) * (-1.0 if self.flip else 1.0) for v in verts]
        self.crotch_z = crotch_z

    # -- 4. Masstab, Blick +X, Boden 0 -----------------------------------------
    def _to_final(self):
        verts = self.verts
        scale = self.H / self.height
        lo = Vector((min(v.co.x for v in verts), min(v.co.y for v in verts), min(v.co.z for v in verts)))
        hi = Vector((max(v.co.x for v in verts), max(v.co.y for v in verts), max(v.co.z for v in verts)))
        centre = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
        for v in verts:
            q = (v.co - centre) * scale
            if self.flip:
                q = Vector((-q.x, -q.y, q.z))
            v.co = Vector((-q.y, q.x, q.z))
        for poly in self.body.data.polygons:
            poly.use_smooth = True
        self.hip_z = (self.crotch_z - lo.z) * scale + 0.06 * self.H
        self.shoulder_z = (self.arm_z - lo.z) * scale
        self.log('Huefte %.3f m, Schulter %.3f m' % (self.hip_z, self.shoulder_z))

    def _band_mean(self, indices, z, half=0.02):
        verts = self.verts
        pts = [verts[i].co for i in indices if abs(verts[i].co.z - z) < half]
        if not pts:
            pts = sorted((verts[i].co for i in indices), key=lambda c: abs(c.z - z))[:20]
        return _mean(pts)

    def _joints(self):
        verts, H = self.verts, self.H
        self.knee_z, self.ankle_z = 0.28 * H, 0.045 * H
        self.joints = {}
        for s, tag in ((1.0, 'L'), (-1.0, 'R')):
            leg = [i for i in range(len(verts)) if self.leg_weight[i] > 0.9 and self.side_of[i] == s]
            arm = [i for i in range(len(verts)) if self.arm_weight[i] > 0.9 and self.side_of[i] == s]
            hip = self._band_mean(leg, 0.40 * H, 0.03)
            if arm:
                tip_z = min(verts[i].co.z for i in arm)
                reach = self.shoulder_z - tip_z
                elbow_z, wrist_z = self.shoulder_z - 0.44 * reach, self.shoulder_z - 0.76 * reach
                tip = self._band_mean(arm, tip_z + 0.01, 0.01)
                shoulder = self._band_mean(arm, self.shoulder_z - 0.04, 0.02) + Vector((0, 0, 0.04))
                elbow, wrist = self._band_mean(arm, elbow_z), self._band_mean(arm, wrist_z)
                tip = Vector((tip.x, tip.y, tip_z))
            else:
                # Starre Arme: Knochen nach Proportion (sie tragen keine Ecken).
                shoulder = Vector((0.0, s * 0.11 * H, self.shoulder_z))
                elbow_z, wrist_z = self.shoulder_z - 0.19 * H, self.shoulder_z - 0.33 * H
                elbow = Vector((0.0, s * 0.12 * H, elbow_z))
                wrist = Vector((0.0, s * 0.12 * H, wrist_z))
                tip = Vector((0.0, s * 0.12 * H, self.shoulder_z - 0.44 * H))
            self.joints[tag] = {
                'hip': Vector((hip.x, hip.y, self.hip_z)),
                'knee': self._band_mean(leg, self.knee_z),
                'ankle': self._band_mean(leg, self.ankle_z, 0.015),
                'shoulder': shoulder, 'elbow': elbow, 'wrist': wrist, 'tip': tip,
                'elbow_z': elbow_z, 'wrist_z': wrist_z,
            }
            j = self.joints[tag]
            self.log('%s Schulter %s Ellbogen %s Handgelenk %s Knie %s' % (
                tag, tuple(round(c, 3) for c in j['shoulder']), tuple(round(c, 3) for c in j['elbow']),
                tuple(round(c, 3) for c in j['wrist']), tuple(round(c, 3) for c in j['knee'])))
        self.torso_x = _mean(v.co for v in verts if abs(v.co.y) < 0.08 and 0.6 * H < v.co.z < 0.8 * H).x
        self.chest_z, self.neck_z = 0.72 * H, 0.84 * H

    # -- 5. Skelett ------------------------------------------------------------
    def _find_eye(self, side):
        """Auge aus der Geometrie: die Nasenspitze ist der vorderste Punkt des
        Kopfes nahe der Mitte; die Augen liegen 3,5 cm darueber, 3,2 cm seitlich."""
        verts, H = self.verts, self.H
        head = [v.co for v in verts if v.co.z > 0.87 * H and self.arm_weight[v.index] <= 0.0]
        mid_y = _mean(head).y
        nose = max((c for c in head if abs(c.y - mid_y) < 0.02 and c.z < 0.97 * H), key=lambda c: c.x)
        ez = nose.z + 0.035
        ey = mid_y + (0.032 if side == 'L' else -0.032)
        front = [c for c in head if abs(c.z - ez) < 0.012 and abs(c.y - ey) < 0.012]
        ex = max(c.x for c in front) if front else nose.x - 0.02
        self.log('Auge %s bei (%.3f, %.3f, %.3f), Nasenspitze (%.3f, %.3f)' % (side, ex, ey, ez, nose.x, nose.z))
        return Vector((ex - 0.004, ey, ez))

    def _armature(self, eyelid):
        arm_data = bpy.data.armatures.new('%sSkelett' % self.name)
        rig = bpy.data.objects.new('%sRig' % self.name, arm_data)
        bpy.context.scene.collection.objects.link(rig)
        bpy.context.view_layer.objects.active = rig
        rig.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        eb = arm_data.edit_bones

        def bone(name, head, tail, parent=None, connect=False):
            b = eb.new(name)
            b.head, b.tail = Vector(head), Vector(tail)
            if parent:
                b.parent = eb[parent]
                b.use_connect = connect
            return b

        tx, H = self.torso_x, self.H
        bone('Root', (0, 0, 0), (0.25, 0, 0))
        bone('Hips', (tx, 0, self.hip_z), (tx, 0, self.hip_z + 0.08), 'Root')
        bone('Spine', (tx, 0, self.hip_z + 0.08), (tx, 0, self.chest_z), 'Hips', True)
        bone('Chest', (tx, 0, self.chest_z), (tx, 0, self.neck_z), 'Spine', True)
        bone('Head', (tx, 0, self.neck_z), (tx, 0, H), 'Chest', True)
        for tag in ('L', 'R'):
            j = self.joints[tag]
            bone('Thigh_' + tag, j['hip'], j['knee'], 'Hips')
            bone('Shin_' + tag, j['knee'], j['ankle'], 'Thigh_' + tag, True)
            bone('Foot_' + tag, j['ankle'], j['ankle'] + Vector((0.14, 0, -0.03)), 'Shin_' + tag, True)
            bone('UpperArm_' + tag, j['shoulder'], j['elbow'], 'Chest')
            bone('LowerArm_' + tag, j['elbow'], j['wrist'], 'UpperArm_' + tag, True)
            bone('Hand_' + tag, j['wrist'], j['tip'], 'LowerArm_' + tag, True)
        self.eye = None
        if eyelid:
            self.eye = self._find_eye(eyelid)
            bone('Eye_' + eyelid, self.eye, self.eye + Vector((0.02, 0, 0)), 'Head')
        back = Vector((-1.0, 0.0, 0.0))
        up = Vector((0.0, 0.0, 1.0))
        for b in eb:
            if b.name == 'Root':
                continue
            # Fuss und Augenlid zeigen nach vorn: ihre Z-Achse nach oben (fuer das
            # Augenlid heisst das: Skalierung lokal Z = senkrecht stauchen).
            b.align_roll(up if b.name.startswith(('Foot', 'Eye')) else back)
        bpy.ops.object.mode_set(mode='OBJECT')
        for name in ('Thigh_L', 'UpperArm_R', 'Spine'):
            m = arm_data.bones[name].matrix_local.to_3x3()
            self.log('%-10s X=%s Z=%s' % (name, tuple(round(c, 2) for c in m.col[0]), tuple(round(c, 2) for c in m.col[2])))
        self.log('Skelett: %d Knochen' % len(arm_data.bones))
        self.rig = rig

    # -- 5b. Gewichte nach Bauplan ------------------------------------------
    def _weights(self, eyelid):
        body, verts, H = self.body, self.verts, self.H
        names = [b.name for b in self.rig.data.bones if b.name != 'Root']
        groups = {n: body.vertex_groups.new(name=n) for n in names}
        for v in verts:
            i, p = v.index, v.co
            tag = 'L' if self.side_of[i] > 0 else 'R'
            w = {}
            s_spine = smoothstep(self.hip_z + 0.02, self.hip_z + 0.12, p.z)
            s_chest = smoothstep(0.66 * H, 0.74 * H, p.z)
            s_head = smoothstep(0.83 * H, 0.87 * H, p.z)
            torso = {'Hips': 1 - s_spine, 'Spine': s_spine * (1 - s_chest),
                     'Chest': s_chest * (1 - s_head), 'Head': s_head}
            wa, wl = self.arm_weight[i], self.leg_weight[i]
            j = self.joints[tag]
            if wa > 0.0:
                e = smoothstep(j['elbow_z'] - 0.02 * H, j['elbow_z'] + 0.02 * H, p.z)
                h = smoothstep(j['wrist_z'] - 0.015 * H, j['wrist_z'] + 0.015 * H, p.z)
                limb = {'UpperArm_' + tag: e, 'LowerArm_' + tag: (1 - e) * h, 'Hand_' + tag: 1 - h}
                torso = {'Chest': 1.0}   # der Armanteil geht an die Brust
                share = wa
            elif wl > 0.0:
                k = smoothstep(self.knee_z - 0.025 * H, self.knee_z + 0.025 * H, p.z)
                a = smoothstep(self.ankle_z - 0.015 * H, self.ankle_z + 0.015 * H, p.z)
                limb = {'Thigh_' + tag: k, 'Shin_' + tag: (1 - k) * a, 'Foot_' + tag: 1 - a}
                share = wl
            else:
                limb, share = {}, 0.0
            for n, x in limb.items():
                w[n] = w.get(n, 0.0) + share * x
            for n, x in torso.items():
                w[n] = w.get(n, 0.0) + (1 - share) * x
            if eyelid and w.get('Head', 0.0) > 0.0:
                # Augenpartie: Ellipse 3,0 x 1,8 cm um das Auge, nur die Vorderseite.
                # Weicher Rand (ab 40 %): mit 2,4 x 1,6 cm und hartem Rand stand am
                # aeusseren Augenwinkel eine dunkle Kerbe.
                d = math.hypot((p.y - self.eye.y) / 0.030, (p.z - self.eye.z) / 0.018)
                if p.x > self.eye.x - 0.02 and d < 1.0:
                    e_w = w['Head'] * (1.0 - smoothstep(0.4, 1.0, d))
                    w['Head'] -= e_w
                    w['Eye_' + eyelid] = e_w
            total = sum(w.values())
            for n, x in w.items():
                if x > 1e-4:
                    groups[n].add([i], x / total, 'REPLACE')
        mod = body.modifiers.new('Skelett', 'ARMATURE')
        mod.object = self.rig
        body.parent = self.rig
        self.log('Gewichte vergeben (%d Ecken)' % len(verts))

    # -- 6. Bewegungen ---------------------------------------------------------
    def make_action(self, name, frames, pose_at, key_step=2):
        """pose_at(frame) -> {bone: (euler_deg, loc[, scale])}; fehlende Knochen: Ruhe."""
        rig = self.rig
        action = bpy.data.actions.new(name)
        rig.animation_data_create()
        rig.animation_data.action = action
        for pb in rig.pose.bones:
            pb.rotation_mode = 'XYZ'
        keyed = list(range(0, frames + 1, key_step))
        if keyed[-1] != frames:
            keyed.append(frames)
        for f in keyed:
            pose = pose_at(f)
            for bn in self.anim_bones:
                pb = rig.pose.bones[bn]
                entry = pose.get(bn, ((0, 0, 0), (0, 0, 0)))
                pb.rotation_euler = tuple(math.radians(a) for a in entry[0])
                pb.location = Vector(entry[1])
                pb.scale = Vector(entry[2] if len(entry) > 2 else (1, 1, 1))
                pb.keyframe_insert('rotation_euler', frame=f)
                pb.keyframe_insert('location', frame=f)
                pb.keyframe_insert('scale', frame=f)
        action.use_frame_range = True
        action.frame_start, action.frame_end = 0, frames
        action.use_fake_user = True
        self.log('Bewegung %s: %d Bilder (%.2f s)' % (name, frames, frames / FPS))
        return action

    # -- 7. Kontrollbilder ---------------------------------------------------
    def render(self, action, frame, view, name, distance=4.2, look_z=0.52, resolution=(600, 800)):
        scene = bpy.context.scene
        if not hasattr(self, '_cam'):
            scene.render.engine = 'BLENDER_WORKBENCH'
            scene.display.shading.color_type = 'TEXTURE'
            scene.display.shading.light = 'STUDIO'
            scene.view_settings.exposure = self.preview_exposure
            self._cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
            scene.collection.objects.link(self._cam)
            scene.camera = self._cam
            os.makedirs(str(self.preview_dir), exist_ok=True)
        scene.render.resolution_x, scene.render.resolution_y = resolution
        self.rig.animation_data.action = action
        scene.frame_set(frame)
        look = Vector((0, 0, self.H * look_z))
        d = {'seite': Vector((0, -1, 0.1)), 'vorn': Vector((1, 0, 0.1)),
             'schraeg': Vector((1, -0.8, 0.25)), 'oben': Vector((0.6, -0.6, 1.0))}[view]
        self._cam.location = look + d.normalized() * distance
        self._cam.rotation_euler = (look - self._cam.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = os.path.join(str(self.preview_dir), 'rig_%s.png' % name)
        bpy.ops.render.render(write_still=True)

    def render_parts(self, name):
        """Kontrollbild der Zuordnung: Arme rot, Beine blau, Rumpf/Kopf grau -
        VOR dem Animieren pruefen, ob die Erkennung die Figur richtig zerlegt."""
        me = self.body.data
        attr = me.color_attributes.new('Zuordnung', 'FLOAT_COLOR', 'POINT')
        for v in self.verts:
            wa, wl = self.arm_weight[v.index], self.leg_weight[v.index]
            attr.data[v.index].color = (0.3 + 0.7 * wa, 0.3, 0.3 + 0.7 * wl * (1 - wa), 1.0)
        scene = bpy.context.scene
        idle = bpy.data.actions.new('Zuordnung')
        self.rig.animation_data_create()
        self.render(idle, 0, 'vorn', name + '_warm')   # Kamera anlegen
        scene.display.shading.color_type = 'VERTEX'
        for view in ('vorn', 'seite'):
            self.render(idle, 0, view, '%s_%s' % (name, view))
        scene.display.shading.color_type = 'TEXTURE'
        me.color_attributes.remove(attr)

    # -- 8. Export -------------------------------------------------------------
    def export(self, fbx, rest_action):
        self.rig.animation_data.action = rest_action
        bpy.context.scene.frame_set(0)
        bpy.ops.object.select_all(action='DESELECT')
        self.rig.select_set(True)
        self.body.select_set(True)
        bpy.context.view_layer.objects.active = self.rig
        bpy.ops.export_scene.fbx(
            filepath=str(fbx), use_selection=True, apply_unit_scale=True, global_scale=1.0,
            apply_scale_options='FBX_SCALE_NONE', object_types={'ARMATURE', 'MESH'},
            mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False,
            bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False,
            bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
            axis_forward='-Z', axis_up='Y', path_mode='STRIP', embed_textures=False)
        self.log('FBX %s (%.1f MB), %d Dreiecke, %d Materialien' % (
            fbx, os.path.getsize(str(fbx)) / 1e6, len(self.body.data.polygons), len(self.body.data.materials)))


# -- Gemeinsame Bewegungsbausteine ----------------------------------------------
def breathe_pose(breathe):
    """Atemzug wie Denno im Cafe: Brust +1,2 % breit, +0,4 % hoch, Schultern mit."""
    return {
        'Chest': ((-1.2 * breathe, 0, 0), (0, 0, 0),
                  (1 + BREATH_WIDTH * breathe, 1 + BREATH_RISE * breathe, 1 + BREATH_WIDTH * breathe)),
        'Head': ((1.0 * breathe, 0, 0), (0, 0, 0)),
        'UpperArm_L': ((FWD * 1.2 * breathe, 0, 0), (0, 0, 0)),
        'UpperArm_R': ((FWD * 1.2 * breathe, 0, 0), (0, 0, 0)),
        'LowerArm_L': ((FWD * 6, 0, 0), (0, 0, 0)),
        'LowerArm_R': ((FWD * 6, 0, 0), (0, 0, 0)),
    }


def walk_legs(a, swing=24.0, knee=34.0, bob=0.022):
    """Beine und Hueften eines Schrittzyklus bei Phase a (0..1)."""
    s, c = math.sin(a * 2 * math.pi), math.cos(a * 2 * math.pi)
    knee_l = KNEE * knee * max(0.0, s)     # L ist hinten, wenn s > 0
    knee_r = KNEE * knee * max(0.0, -s)
    return {
        'Thigh_L': ((-FWD * swing * s, 0, 0), (0, 0, 0)),
        'Thigh_R': ((FWD * swing * s, 0, 0), (0, 0, 0)),
        'Shin_L': ((knee_l, 0, 0), (0, 0, 0)),
        'Shin_R': ((knee_r, 0, 0), (0, 0, 0)),
        'Foot_L': ((-knee_l * 0.5, 0, 0), (0, 0, 0)),
        'Foot_R': ((-knee_r * 0.5, 0, 0), (0, 0, 0)),
        'Hips': ((0, 4.0 * s, 0), (0, bob * (abs(c) - 0.5), 0)),
        'Spine': ((0, -3.0 * s, 0), (0, 0, 0)),
    }


def walk_arms(a, arm=18.0):
    s = math.sin(a * 2 * math.pi)
    return {
        # Arme gegenlaeufig: linkes Bein hinten -> linker Arm vorn.
        'UpperArm_L': ((FWD * arm * s, 0, 0), (0, 0, 0)),
        'UpperArm_R': ((-FWD * arm * s, 0, 0), (0, 0, 0)),
        'LowerArm_L': ((FWD * (10 + 8 * max(0.0, s)), 0, 0), (0, 0, 0)),
        'LowerArm_R': ((FWD * (10 + 8 * max(0.0, -s)), 0, 0), (0, 0, 0)),
        'Chest': ((0, -2.0 * s, 0), (0, 0, 0)),
    }
