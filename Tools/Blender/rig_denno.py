"""Denno (sie) mit Skelett: arbeitet hektisch in Cafe und Friseur und reicht
beim Annehmen eines Lieferauftrags das Paket - mit Zwinkern.

Quelle: Data/Raw/Denno/denno_source.glb (Tripo, 6 Teile, 1,87 Mio. Dreiecke,
T-Pose, kein Skelett - dieselbe Quelle wie das statische SM_Denno aus
build_denno_figure.py, das als Rueckfall im Laden bleibt).
Ziel:   Data/Raw/Denno/SK_Denno.fbx + Data/Raw/Denno/tex/T_Denno_Part*.png
        (-> Tools/import_tripo_figure.py, WB_FIGUR=Denno).

Skelett und Gewichte: Tools/Blender/tripo_rig.py, dazu ein Augenlid-Knochen
Eye_R (staucht die Augenpartie senkrecht: Zwinkern ohne Gesichtsrig).

Bewegungen (30 Bilder/s) - AWiesbadenDennoShop spielt sie nach ihrem
Arbeitsplan (WiesbadenDennoWork):
  Denno_Idle       4,2 s Atemzug (wie zuvor die Figur ohne Skelett)
  Denno_Walk       0,7 s hektischer Schrittzyklus, gebaut fuer WALK_SPEED_MPS
  Denno_WalkCarry  0,7 s dasselbe mit Tablett/Paket vor dem Bauch
  Denno_Sweep      1,0 s fegen (Besen rechts, Oberkoerper schwingt)
  Denno_Wipe       1,2 s Tisch abwischen (vorgebeugt, rechte Hand kreist)
  Denno_Tidy       2,4 s aufraeumen (abwechselnd greifen und abstellen)
  Denno_CutHair    1,2 s Haare schneiden (Kamm links, Schere rechts schnippt)
  Denno_Serve      1,5 s einmal: Tasse abstellen, Arme sinken
  Denno_Handover   3,0 s einmal: Paket reichen, zwinkern, Arme zurueck
                   (das Paket verlaesst ihre Haende bei HANDOVER_RELEASE_S)

Blender 5.2: blender -b -P Tools/Blender/rig_denno.py
Kontrollbilder: .planning/denno-shop/rig/rig_*.png
"""
import math
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent))
from tripo_rig import (TripoRig, FWD, KNEE, ABDUCT_R, ABDUCT_L, smoothstep,   # noqa: E402
                       breathe_pose, walk_legs, walk_arms)

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / 'Data/Raw/Denno'
rig = TripoRig(OUT_DIR / 'denno_source.glb', 'Denno', height_m=1.68, target_tris=20000,
               tex_dir=OUT_DIR / 'tex', preview_dir=ROOT.parent / '.planning/denno-shop/rig',
               arm_outward_deg=12.0, eyelid='R', crotch_frac=0.46, preview_exposure=1.8)

# Hektisch: groesserer Schritt, schnellerer Takt. Schrittlaenge ~0,8 m je
# Schritt -> 1,6 m je Zyklus in 0,7 s. Muss zu WiesbadenDennoWork::
# WalkAnimSpeedCmS passen.
WALK_FRAMES = 21
WALK_SPEED_MPS = 2.2
# Rumpf nach VORN neigen: Drehung um die lokale X-Achse der Rumpfknochen
# (Vorzeichen aus dem Kontrollbild rig_walk_*.png).
# Erster Lauf mit +1: sie lehnte sich beim Wischen und Schneiden nach HINTEN.
LEAN = -1.0
HANDOVER_FRAMES = 90
HANDOVER_RELEASE_S = 52 / 30.0     # WiesbadenDennoWork::HandoverReleaseSeconds
IDLE_FRAMES = round(4.2 * 30)


def lean(deg):
    return (LEAN * deg, 0, 0)


def idle_at(f):
    return breathe_pose(0.5 - 0.5 * math.cos(2 * math.pi * f / IDLE_FRAMES))


def walk_at(f):
    a = f / WALK_FRAMES
    pose = walk_legs(a, swing=30.0, knee=45.0, bob=0.03)
    pose.update(walk_arms(a, arm=26.0))
    pose['Spine'] = ((LEAN * 5.0, -4.0 * math.sin(a * 2 * math.pi), 0), (0, 0, 0))
    return pose


# Die Arme haengen in der Ruhe 12 Grad nach aussen (Arme aus der T-Pose
# gesenkt). Zum Tragen und Reichen schwenken die Oberarme 18 Grad zur Mitte -
# ohne das hielt sie das Paket mit 68 cm weit gespreizten Haenden, es
# schwebte. Geschwenkt wird um die lokale Y-Achse (senkrecht in der Ruhe): die
# Z-Achse liegt beim vorgestreckten Arm fast in Armrichtung und senkte die
# Hand nur ab (gemessen: 30 Grad um Z -> 61 cm, 18 Grad um Y -> ~34 cm).
INWARD = 18.0
CARRY = {
    'UpperArm_L': ((FWD * 28, ABDUCT_L * -INWARD, 0), (0, 0, 0)),
    'UpperArm_R': ((FWD * 28, ABDUCT_R * -INWARD, 0), (0, 0, 0)),
    'LowerArm_L': ((FWD * 72, 0, 0), (0, 0, 0)),
    'LowerArm_R': ((FWD * 72, 0, 0), (0, 0, 0)),
}


def walk_carry_at(f):
    a = f / WALK_FRAMES
    pose = walk_legs(a, swing=26.0, knee=40.0, bob=0.025)
    pose.update(CARRY)
    pose['Spine'] = (lean(3.0), (0, 0, 0))
    return pose


def sweep_at(f):
    s = math.sin(2 * math.pi * f / 30)
    return {
        'Spine': ((LEAN * 10, 0, 0), (0, 0, 0)),
        'Chest': ((0, 16 * s, 0), (0, 0, 0)),
        'Head': ((LEAN * -6, -8 * s, 0), (0, 0, 0)),
        'Thigh_L': ((FWD * 8, 0, 0), (0, 0, 0)), 'Thigh_R': ((FWD * 8, 0, 0), (0, 0, 0)),
        'Shin_L': ((KNEE * 14, 0, 0), (0, 0, 0)), 'Shin_R': ((KNEE * 14, 0, 0), (0, 0, 0)),
        # Besen rechts: rechte Hand unten am Stiel, linke oben quer vor dem Koerper.
        'UpperArm_R': ((FWD * (22 + 10 * s), 0, 0), (0, 0, 0)),
        'LowerArm_R': ((FWD * 28, 0, 0), (0, 0, 0)),
        'UpperArm_L': ((FWD * (48 - 10 * s), 0, -ABDUCT_L * 18), (0, 0, 0)),
        'LowerArm_L': ((FWD * 40, 0, 0), (0, 0, 0)),
    }


def wipe_at(f):
    t = 2 * math.pi * f / 36
    return {
        'Spine': (lean(16), (0, 0, 0)),
        'Chest': (lean(12), (0, 0, 0)),
        'Head': (lean(8), (0, 0, 0)),
        'Thigh_L': ((FWD * 10, 0, 0), (0, 0, 0)), 'Thigh_R': ((FWD * 10, 0, 0), (0, 0, 0)),
        'Shin_L': ((KNEE * 16, 0, 0), (0, 0, 0)), 'Shin_R': ((KNEE * 16, 0, 0), (0, 0, 0)),
        # Rechte Hand kreist mit dem Tuch, die linke stuetzt sich auf.
        'UpperArm_R': ((FWD * (58 + 10 * math.cos(t)), 0, ABDUCT_R * (6 + 12 * math.sin(t))), (0, 0, 0)),
        'LowerArm_R': ((FWD * 18, 0, 0), (0, 0, 0)),
        'UpperArm_L': ((FWD * 46, 0, 0), (0, 0, 0)),
        'LowerArm_L': ((FWD * 22, 0, 0), (0, 0, 0)),
    }


def tidy_at(f):
    s = math.sin(2 * math.pi * f / 72)
    return {
        'Spine': (lean(8), (0, 0, 0)),
        'Chest': ((0, 12 * s, 0), (0, 0, 0)),
        'Head': ((LEAN * 6, 10 * s, 0), (0, 0, 0)),
        'UpperArm_R': ((FWD * (40 + 30 * max(0.0, s)), 0, 0), (0, 0, 0)),
        'UpperArm_L': ((FWD * (40 + 30 * max(0.0, -s)), 0, 0), (0, 0, 0)),
        'LowerArm_R': ((FWD * (30 + 20 * max(0.0, -s)), 0, 0), (0, 0, 0)),
        'LowerArm_L': ((FWD * (30 + 20 * max(0.0, s)), 0, 0), (0, 0, 0)),
    }


def cut_hair_at(f):
    snip = math.sin(2 * math.pi * 6 * f / 36)        # sechsmal je Zyklus
    sway = math.sin(2 * math.pi * f / 36)
    return {
        'Spine': (lean(8), (0, 0, 0)),
        'Head': ((LEAN * 12, 6 * sway, 0), (0, 0, 0)),
        'Hips': ((0, 0, 1.2 * sway), (0, 0, 0)),
        'UpperArm_R': ((FWD * 62, 0, ABDUCT_R * 12), (0, 0, 0)),
        'LowerArm_R': ((FWD * (58 + 4 * snip), 0, 0), (0, 0, 0)),
        'Hand_R': ((0, 0, ABDUCT_R * 14 * snip), (0, 0, 0)),
        'UpperArm_L': ((FWD * 60, 0, ABDUCT_L * 8), (0, 0, 0)),
        'LowerArm_L': ((FWD * (64 + 6 * sway), 0, 0), (0, 0, 0)),
    }


def blend(a, b, t):
    """Zwei Posen mischen (Euler/Ort/Skalierung linear); fehlende Knochen = Ruhe."""
    rest = ((0, 0, 0), (0, 0, 0), (1, 1, 1))
    out = {}
    for bone in set(a) | set(b):
        pa = tuple(a.get(bone, rest)) + rest[len(a.get(bone, rest)):]
        pb = tuple(b.get(bone, rest)) + rest[len(b.get(bone, rest)):]
        out[bone] = tuple(tuple(x + (y - x) * t for x, y in zip(ca, cb)) for ca, cb in zip(pa, pb))
    return out


SERVE_LOW = {
    'Spine': (lean(18), (0, 0, 0)),
    'Head': (lean(6), (0, 0, 0)),
    'UpperArm_L': ((FWD * 46, 0, 0), (0, 0, 0)), 'UpperArm_R': ((FWD * 46, 0, 0), (0, 0, 0)),
    'LowerArm_L': ((FWD * 32, 0, 0), (0, 0, 0)), 'LowerArm_R': ((FWD * 32, 0, 0), (0, 0, 0)),
}


def serve_at(f):
    if f <= 22:
        return blend(CARRY, SERVE_LOW, smoothstep(0, 22, f))
    return blend(SERVE_LOW, breathe_pose(0.0), smoothstep(22, 45, f))


REACH = {
    'Spine': (lean(6), (0, 0, 0)),
    'UpperArm_L': ((FWD * 74, ABDUCT_L * -INWARD, 0), (0, 0, 0)),
    'UpperArm_R': ((FWD * 74, ABDUCT_R * -INWARD, 0), (0, 0, 0)),
    'LowerArm_L': ((FWD * 22, 0, 0), (0, 0, 0)), 'LowerArm_R': ((FWD * 22, 0, 0), (0, 0, 0)),
}


def handover_at(f):
    if f <= 12:
        pose = dict(CARRY)
    elif f <= 30:
        pose = blend(CARRY, REACH, smoothstep(12, 30, f))
    elif f <= 52:
        pose = dict(REACH)
    elif f <= 70:
        pose = blend(REACH, breathe_pose(0.0), smoothstep(52, 70, f))
    else:
        pose = breathe_pose(0.0)
    # Zwinkern (rechtes Auge) mit leicht geneigtem Kopf, dann ein kleines Nicken.
    close = smoothstep(34, 38, f) * (1 - smoothstep(46, 50, f))
    tilt = smoothstep(32, 38, f) * (1 - smoothstep(50, 58, f))
    nod = smoothstep(72, 78, f) * (1 - smoothstep(80, 88, f))
    pose['Eye_R'] = ((0, 0, 0), (0, 0, 0), (1, 1, 1 - 0.8 * close))
    pose['Head'] = ((LEAN * (4 * tilt + 8 * nod), 0, -7 * tilt), (0, 0, 0))
    return pose


actions = {
    'Idle': rig.make_action('Denno_Idle', IDLE_FRAMES, idle_at),
    'Walk': rig.make_action('Denno_Walk', WALK_FRAMES, walk_at),
    'WalkCarry': rig.make_action('Denno_WalkCarry', WALK_FRAMES, walk_carry_at),
    'Sweep': rig.make_action('Denno_Sweep', 30, sweep_at),
    'Wipe': rig.make_action('Denno_Wipe', 36, wipe_at),
    'Tidy': rig.make_action('Denno_Tidy', 72, tidy_at),
    'CutHair': rig.make_action('Denno_CutHair', 36, cut_hair_at),
    'Serve': rig.make_action('Denno_Serve', 45, serve_at),
    'Handover': rig.make_action('Denno_Handover', HANDOVER_FRAMES, handover_at, key_step=1),
}

for f in (0, 5, 10, 16):
    rig.render(actions['Walk'], f, 'seite', 'walk_%02d' % f)
rig.render(actions['WalkCarry'], 5, 'seite', 'walkcarry')
for f in (0, 7, 15, 22):
    rig.render(actions['Sweep'], f, 'schraeg', 'sweep_%02d' % f)
rig.render(actions['Wipe'], 0, 'seite', 'wipe_seite')
rig.render(actions['Tidy'], 18, 'seite', 'tidy_seite')
rig.render(actions['CutHair'], 0, 'seite', 'cut_seite')
rig.render(actions['CutHair'], 0, 'schraeg', 'cut_schraeg')
rig.render(actions['Serve'], 22, 'seite', 'serve_seite')
for f in (0, 30, 44, 60):
    rig.render(actions['Handover'], f, 'seite', 'hand_%02d' % f)
# Gesicht nah: offen / gezwinkert.
for f in (30, 44):
    rig.render(actions['Handover'], f, 'vorn', 'wink_%02d' % f, distance=0.55, look_z=0.93, resolution=(500, 500))
# Haende am Paket: Reichen und Tragen von vorn.
rig.render(actions['Handover'], 40, 'vorn', 'hand_vorn')
rig.render(actions['WalkCarry'], 5, 'vorn', 'carry_vorn')

rig.export(OUT_DIR / 'SK_Denno.fbx', actions['Idle'])
rig.log('FERTIG')
