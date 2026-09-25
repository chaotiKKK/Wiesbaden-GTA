"""Raeder der Verkehrsautos gerade stellen: echte Radachse genau auf die Querachse (Y).

Das Spiel dreht jedes Rad um die Y-Achse durch die Mitte seiner Bounds
(ComputeWheelTransform). Tripo erzeugt Autos aber oft mit EINGESCHLAGENEN
Vorderraedern - T6 vorn 20 Grad, Kaefer vorn 14 Grad. Um Y gedreht taumeln
solche Raeder sichtbar ("wackeln, drehen nicht rund").

Die echte Achse ist die Richtung, in der das Rad am SCHMALSTEN ist: gesucht
werden Einschlag (um Z) und Sturz (um X), bei denen die Breite entlang Y
minimal wird. Die Hauptachse kleinster Streuung taugt dafuer NICHT - Nabe,
Bremstrommel und Felgenstern sind unsymmetrisch und verzogen sie um bis zu
7 Grad (Kaefer hinten: "gerade gestellt" wurde das Rad 3,6 cm breiter, also
erst schief).

Genutzt von Tools/Blender/build_traffic_cars.py (Neubau) und
Tools/Blender/straighten_traffic_wheels.py (vorhandene Rad-FBX nachbessern).
"""
import math

import numpy as np
from mathutils import Matrix, Vector


def _width(points, yaw, camber):
    """Breite entlang Y nach Drehung um Z (yaw) und X (camber), Radiant."""
    cy, sy = math.cos(yaw), math.sin(yaw)
    cx, sx = math.cos(camber), math.sin(camber)
    # y-Komponente von Rx(camber) @ Rz(yaw) @ p
    x, y, z = points[:, 0], points[:, 1], points[:, 2]
    y1 = sy * x + cy * y
    y2 = cx * y1 - sx * z
    return float(y2.max() - y2.min())


def find_alignment(points):
    """(Einschlag, Sturz) in Grad, die das Rad am schmalsten machen, und die Breiten davor/danach."""
    p = np.asarray(points, dtype=np.float64)
    p = p - 0.5 * (p.min(axis=0) + p.max(axis=0))
    best = (_width(p, 0.0, 0.0), 0.0, 0.0)
    before = best[0]
    for step, span in ((1.0, 25.0), (0.1, 1.0), (0.01, 0.1)):
        _, y0, c0 = best
        for yaw in np.arange(y0 - span, y0 + span + 1e-9, step):
            for cam in np.arange(c0 - span, c0 + span + 1e-9, step):
                w = _width(p, math.radians(yaw), math.radians(cam))
                if w < best[0] - 1e-9:
                    best = (w, float(yaw), float(cam))
    return best[1], best[2], before, best[0]


def straighten(obj, ground=True, min_gain=0.0005):
    """Dreht die Rad-Geometrie um die Bounds-Mitte, bis sie entlang Y am
    schmalsten ist. Nur wenn das die Breite merklich verringert (min_gain, m)
    - sonst bleibt das Rad unangetastet. Rueckgabe: (Einschlag, Sturz, Breite
    vorher, nachher) in Grad bzw. Metern."""
    mw = obj.matrix_world
    pts = np.array([tuple(mw @ v.co) for v in obj.data.vertices], dtype=np.float64)
    yaw, camber, before, after = find_alignment(pts)
    if before - after < min_gain:
        return 0.0, 0.0, before, before
    center = Vector(tuple(0.5 * (pts.min(axis=0) + pts.max(axis=0))))
    rot = (Matrix.Rotation(math.radians(camber), 4, 'X') @ Matrix.Rotation(math.radians(yaw), 4, 'Z'))
    fix = Matrix.Translation(center) @ rot @ Matrix.Translation(-center)
    obj.matrix_world = fix @ obj.matrix_world
    if ground:
        low = min((obj.matrix_world @ v.co).z for v in obj.data.vertices)
        obj.matrix_world = Matrix.Translation((0.0, 0.0, -low)) @ obj.matrix_world
    return yaw, camber, before, after
