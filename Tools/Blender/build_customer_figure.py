"""Lieferkunden und Ladengaeste riggen: jede Figur aus Tools/kunden_figuren.json
bekommt Skelett, Gewichte und dieselben vier Bewegungen.

  Idle  4,2 s Atemzug wie Denno im Cafe; Gewicht verlagern und Umschauen
        rechnet der Actor (AWiesbadenDennoShop::ComputeDennoIdle).
  Walk  1 s Schrittzyklus, gebaut fuer WALK_SPEED_MPS (im Spiel nach Tempo
        skaliert): Oberschenkel +-24 Grad, Kniebeugung am hinteren Bein, Arme
        gegenlaeufig, Hueftheben.
  Wave  2,4 s einmal: rechten Arm heben, dreimal winken, senken (Dank).
        Figuren mit starren Armen (arme: 'starr') nicken statt zu winken -
        derselbe Name, damit das Spiel alle gleich behandelt.
  Sit   4,2 s Schleife: sitzen (Bistrostuhl, Sitz 48 cm) und atmen - Gast in
        Dennos Cafe und im Friseurstuhl.

Alle Figuren haben dieselben Bewegungsnamen und dasselbe Schritttempo - das
Spiel behandelt sie gleich und wechselt nur die Figur (WiesbadenCustomerFigures).

Ziel je Figur: Data/Raw/Kunden/<Name>/SK_<Name>.fbx + tex/T_<Name>_Part*.png
(-> Tools/import_tripo_figure.py). Kontrollbilder: .planning/kunden-figuren/<Name>/
(rig_teile_* = Zuordnung Arme rot / Beine blau VOR dem Animieren pruefen).

Blender 5.2:
  blender -b -P Tools/Blender/build_customer_figure.py -- Mira
  blender -b -P Tools/Blender/build_customer_figure.py -- alle
"""
import json
import math
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent))
from tripo_rig import (TripoRig, FWD, KNEE, ABDUCT_R, smoothstep,   # noqa: E402
                       breathe_pose, walk_legs, walk_arms)

ROOT = Path(__file__).resolve().parents[2]
REGISTRY = json.loads((ROOT / 'Tools/kunden_figuren.json').read_text(encoding='utf-8'))['figuren']
# Tempo, fuer das der Schrittzyklus gebaut ist (Schrittlaenge x 2 je Sekunde).
# Muss zu WiesbadenDennoDelivery::CustomerWalkAnimSpeedCmS passen.
WALK_SPEED_MPS = 1.30
IDLE_FRAMES = round(4.2 * 30)   # 126 - Dennos Atemtakt
# Sitzen auf dem Bistrostuhl (Sitz 48 cm): das Hueftgelenk sinkt auf 57 cm, der
# Oberschenkel liegt 17 Grad unter der Waagerechten, damit die Fuesse bei
# senkrechtem Schienbein auf dem Boden stehen. Im Friseurstuhl (Sitz 58 cm)
# hebt der Actor die Figur um 10 cm - die Fuesse stehen dann auf der Stuetze.
SEAT_HIP_Z = 0.57
THIGH_DOWN_DEG = 17.0


def build(name, cfg):
    out_dir = ROOT / 'Data/Raw/Kunden' / name
    rig = TripoRig(ROOT / cfg['quelle'], name, height_m=cfg['hoehe_m'], target_tris=cfg.get('dreiecke', 16000),
                   tex_dir=out_dir / 'tex', preview_dir=ROOT.parent / '.planning/kunden-figuren' / name,
                   arm_outward_deg=cfg.get('arme_aussen_grad', 10.0), arms=cfg.get('arme', 'T'),
                   crotch_frac=cfg.get('schritt_anteil'), preview_exposure=cfg.get('vorschau_belichtung', 0.8),
                   facing=cfg.get('blick', '-Y'))
    rig.render_parts('teile')

    def idle_at(f):
        return breathe_pose(0.5 - 0.5 * math.cos(2 * math.pi * f / IDLE_FRAMES))

    def walk_at(f):
        a = f / 30.0
        pose = walk_legs(a)
        pose.update(walk_arms(a))
        return pose

    def wave_at(f):
        # 0-14 heben, 14-58 dreimal winken, 58-72 senken.
        up = smoothstep(0, 14, f) * (1 - smoothstep(58, 72, f))
        wav = math.sin((f - 14) / 44.0 * 3 * 2 * math.pi) if 14 <= f <= 58 else 0.0
        return {
            # Oberarm seitlich auf Schulterhoehe, Unterarm steil nach oben
            # (beides um die Blickachse), gewinkt wird mit dem Unterarm.
            'UpperArm_R': ((FWD * 15 * up, 0, ABDUCT_R * 95 * up), (0, 0, 0)),
            'LowerArm_R': ((0, 0, ABDUCT_R * (65 + 22 * wav) * up), (0, 0, 0)),
            'Hand_R': ((0, 0, ABDUCT_R * 10 * wav * up), (0, 0, 0)),
            'Chest': ((0, -4 * up, 0), (0, 0, 0)),
            'Head': ((0, 3 * up, 4 * up), (0, 0, 0)),
            'LowerArm_L': ((FWD * 6, 0, 0), (0, 0, 0)),
        }

    def nod_at(f):
        # Dank ohne Arme: zweimal nicken, leicht verneigen.
        bow = smoothstep(0, 14, f) * (1 - smoothstep(58, 72, f))
        nod = math.sin((f - 14) / 44.0 * 2 * 2 * math.pi) if 14 <= f <= 58 else 0.0
        return {
            'Chest': ((-5 * bow, 0, 0), (0, 0, 0)),
            'Head': ((-(6 + 7 * max(0.0, nod)) * bow, 0, 0), (0, 0, 0)),
        }

    def sit_at(f):
        breathe = 0.5 - 0.5 * math.cos(2 * math.pi * f / IDLE_FRAMES)
        thigh = 90.0 - THIGH_DOWN_DEG
        pose = breathe_pose(breathe)
        pose.update({
            'Hips': ((0, 0, 0), (0, SEAT_HIP_Z - rig.hip_z, 0)),
            'Thigh_L': ((FWD * thigh, 0, 0), (0, 0, 0)),
            'Thigh_R': ((FWD * thigh, 0, 0), (0, 0, 0)),
            'Shin_L': ((KNEE * thigh, 0, 0), (0, 0, 0)),
            'Shin_R': ((KNEE * thigh, 0, 0), (0, 0, 0)),
            # Haende ruhen auf den Oberschenkeln.
            'UpperArm_L': ((FWD * (10 + 1.2 * breathe), 0, 0), (0, 0, 0)),
            'UpperArm_R': ((FWD * (10 + 1.2 * breathe), 0, 0), (0, 0, 0)),
            'LowerArm_L': ((FWD * 34, 0, 0), (0, 0, 0)),
            'LowerArm_R': ((FWD * 34, 0, 0), (0, 0, 0)),
        })
        return pose

    idle = rig.make_action('%s_Idle' % name, IDLE_FRAMES, idle_at)
    walk = rig.make_action('%s_Walk' % name, 30, walk_at)
    rigid = cfg.get('arme') == 'starr'
    wave = rig.make_action('%s_Wave' % name, 72, nod_at if rigid else wave_at)
    sit = rig.make_action('%s_Sit' % name, IDLE_FRAMES, sit_at)
    for f in (0, 8, 15, 23):
        rig.render(walk, f, 'seite', 'walk_%02d' % f)
    for f in (0, 20, 30, 40):
        rig.render(wave, f, 'vorn', 'wave_%02d' % f)
    rig.render(idle, 0, 'vorn', 'idle')
    rig.render(sit, 0, 'seite', 'sit_seite')
    rig.render(sit, 0, 'schraeg', 'sit_schraeg')
    rig.export(out_dir / ('SK_%s.fbx' % name), idle)
    rig.log('FERTIG')


wanted = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else ['alle']
names = list(REGISTRY) if wanted in (['alle'], []) else wanted
for figure in names:
    if figure not in REGISTRY:
        raise SystemExit('Figur %s fehlt in Tools/kunden_figuren.json' % figure)
    build(figure, REGISTRY[figure])
