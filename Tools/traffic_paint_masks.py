r"""Lackmasken der Verkehrsautos: welcher Texel ist Lack (und darf je Fahrzeug umgefaerbt werden)?

    python Tools/traffic_paint_masks.py                  # alle Fahrzeuge aus Tools/verkehr_fahrzeuge.json
    python Tools/traffic_paint_masks.py Golf BmwGrau     # nur diese
    python Tools/traffic_paint_masks.py Golf --vorschau 0.1,0.55,0.2 --ziel <ordner>
                                                         # umgefaerbte Texturen zum Ansehen

WOFUER: Tripo backt die Lackfarbe in die Texturen - und Scheiben, Lampen,
Zierleisten liegen oft auf DERSELBEN Textur. Umfaerben darf darum nicht je
Material geschehen, sondern je Texel. Lack ist, was im FARBTON (Chromatizitaet
r:g:b, unabhaengig von der eingebackenen Schattierung) dem Grundlack des Modells
gleicht und in der HELLIGKEIT nahe daran liegt - so fallen Glas (dunkel), Gummi,
Chrom und rote Rueckleuchten (anderer Farbton) heraus.

Leuchten mit Lackfarbe (roter Golf: Rueckleuchten und Lack im selben Farbwinkel)
trennt die Farbe nicht - dafuer stanzt lampenzonen.json (Tools/Blender/
traffic_lamp_zones.py: Flaechen an Heck/Front in Lampenhoehe) sie aus der Maske.

Ergebnis je Fahrzeug unter Data/Raw/Verkehr/<Name>/:
    tex/L_<Name>_PartN.png   Lackmaske (Graustufen, 1 = Lack) - nur Teile mit Lack
    lack.json                Referenzlack + Lackanteil je Teil (fuer den Import)

Die UMFAERBUNG selbst (umfaerben() unten) rechnet das Material
M_WbTrafficCarLack in Unreal genauso: neue Farbe mal Helligkeitsverhaeltnis
zum Grundlack - die eingebackene Schattierung bleibt, nur der Farbton wechselt.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / 'Tools' / 'verkehr_fahrzeuge.json'
# Teile mit weniger Lack bekommen keine Maske (Glas, Lampen, Innenraum ...).
MIN_ANTEIL = 0.04
MASKE_MAX_KANTE = 1024
LEER_MASKE = 'L_WbLackLeer.png'
LUM = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def helligkeit(rgb):
    return rgb @ LUM


def chroma(rgb):
    return rgb / np.maximum(rgb.sum(axis=-1, keepdims=True), 1e-4)


def glatt(k0, k1, x):
    t = np.clip((x - k0) / max(k1 - k0, 1e-6), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def farbwinkel_und_saettigung(rgb):
    """HSV-Farbwinkel (Grad) und Saettigung je Texel."""
    mx = rgb.max(axis=-1)
    mn = rgb.min(axis=-1)
    d = np.maximum(mx - mn, 1e-6)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    h = np.where(mx == r, ((g - b) / d) % 6.0, np.where(mx == g, (b - r) / d + 2.0, (r - g) / d + 4.0)) * 60.0
    return h, (mx - mn) / np.maximum(mx, 1e-6)


def lackmaske(rgb, lack):
    """0..1 je Texel (datenrein, getestet): Farbton UND Helligkeit wie der Grundlack.

    Bunter Lack (farbton_grad gesetzt): gleicher Farbwinkel bei Mindestsaettigung -
    so zaehlen auch Sonnenreflexe und die blasseren unteren Partien (Kaefer,
    BMW blau) zum Lack, neutrales Grau (Chrom, Zierleisten) aber nicht.
    Unbunter Lack (weiss, grau): Chromatizitaets-Abstand, die Helligkeit trennt Glas und Gummi.
    """
    ref = np.asarray(lack['rgb'], dtype=np.float32)
    verhaeltnis = helligkeit(rgb) / max(float(helligkeit(ref)), 1e-4)
    lo, hi = float(lack['hell_min']), float(lack['hell_max'])
    hell = glatt(lo * 0.8, lo, verhaeltnis) * (1.0 - glatt(hi, hi * 1.25, verhaeltnis))
    if 'farbton_grad' in lack:
        winkel, saettigung = farbwinkel_und_saettigung(rgb)
        ref_winkel, _ = farbwinkel_und_saettigung(ref[None, :])
        dh = np.abs((winkel - float(ref_winkel[0]) + 180.0) % 360.0 - 180.0)
        toleranz, smin = float(lack['farbton_grad']), float(lack['saettigung_min'])
        farbe = (1.0 - glatt(toleranz, toleranz * 1.6, dh)) * glatt(smin * 0.7, smin, saettigung)
    else:
        toleranz = float(lack['farbton'])
        abstand = np.linalg.norm(chroma(rgb) - chroma(ref), axis=-1)
        farbe = 1.0 - glatt(toleranz, toleranz * 1.8, abstand)
    return (farbe * hell).astype(np.float32)


def umfaerben(rgb, maske, lack, neu):
    """Wie M_WbTrafficCarLack: neu * (Helligkeit / Helligkeit des Grundlacks), gemischt mit der Maske."""
    ref = np.asarray(lack['rgb'], dtype=np.float32)
    verhaeltnis = np.clip(helligkeit(rgb) / max(float(helligkeit(ref)), 1e-4), 0.0, 2.5)
    lackiert = np.clip(np.asarray(neu, dtype=np.float32) * verhaeltnis[..., None], 0.0, 1.0)
    return rgb * (1.0 - maske[..., None]) + lackiert * maske[..., None]


def leuchtenfarbe(rgb):
    """1 fuer Texel, die wie eine Rueck-, Brems- oder Blinkleuchte aussehen (Rot bis
    Orange). Helles farbloses Glas NICHT: beim weissen T6 waere das der Lack an
    Stossstange und Heck - und bunte Lacke schliessen farbloses Glas ohnehin aus."""
    winkel, saettigung = farbwinkel_und_saettigung(rgb)
    return (((winkel > 320.0) | (winkel < 45.0)) & (saettigung > 0.3)).astype(np.float32)


def lampen_ausstanzen(maske, rgb, dreiecke):
    """In den Lampenzonen (UV-Dreiecke aus Tools/Blender/traffic_lamp_zones.py) nur
    die Texel in Leuchtenfarbe auf 0 setzen, mit 2 Texel Rand - beim roten Golf
    trennt die Farbe allein Rueckleuchte und Lack nicht; bei allen anderen Lacken
    bleibt der Lack in der Zone umfaerbbar."""
    if not dreiecke:
        return maske
    h, w = maske.shape
    zone = Image.new('L', (w, h), 0)
    stift = ImageDraw.Draw(zone)
    for d in dreiecke:
        punkte = [(d[i] * w, (1.0 - d[i + 1]) * h) for i in (0, 2, 4)]
        stift.polygon(punkte, fill=255, outline=255, width=2)
    in_zone = np.asarray(zone, dtype=np.float32) / 255.0
    return maske * (1.0 - in_zone * leuchtenfarbe(rgb))


def teil_nummer(pfad):
    return int(pfad.stem.split('Part')[1])


def fahrzeug(name, cfg, vorschau=None, ziel=None):
    tex_dir = ROOT / 'Data' / 'Raw' / 'Verkehr' / name / 'tex'
    lack = cfg['lack']
    zonen_datei = tex_dir.parent / 'lampenzonen.json'
    zonen = json.loads(zonen_datei.read_text(encoding='utf-8')) if zonen_datei.exists() else {}
    anteile = {}
    for alt in tex_dir.glob('L_%s_Part*.png' % name):
        alt.unlink()
    for png in sorted(tex_dir.glob('T_%s_Part*.png' % name), key=teil_nummer):
        bild = Image.open(png).convert('RGB')
        rgb = np.asarray(bild, dtype=np.float32) / 255.0
        teil = teil_nummer(png)
        maske = lampen_ausstanzen(lackmaske(rgb, lack), rgb, zonen.get(str(teil)))
        anteil = float(maske.mean())
        if anteil >= MIN_ANTEIL:
            anteile[teil] = round(anteil, 4)
            m = Image.fromarray((maske * 255.0 + 0.5).astype(np.uint8), 'L')
            if max(m.size) > MASKE_MAX_KANTE:
                f = MASKE_MAX_KANTE / max(m.size)
                m = m.resize((max(1, round(m.size[0] * f)), max(1, round(m.size[1] * f))), Image.BILINEAR)
            m.save(tex_dir / ('L_%s_Part%d.png' % (name, teil)))
        if vorschau is not None and ziel is not None:
            neu = umfaerben(rgb, maske, lack, vorschau) if anteil >= MIN_ANTEIL else rgb
            Path(ziel).mkdir(parents=True, exist_ok=True)
            Image.fromarray((neu * 255.0 + 0.5).astype(np.uint8), 'RGB').save(Path(ziel) / png.name)
    bericht = {'rgb': lack['rgb'], 'teile': {str(k): v for k, v in sorted(anteile.items())}}
    (ROOT / 'Data' / 'Raw' / 'Verkehr' / name / 'lack.json').write_text(
        json.dumps(bericht, indent=2), encoding='utf-8')
    print('%s: %d Lackteile %s' % (name, len(anteile),
                                  ', '.join('%d:%.0f%%' % (k, v * 100) for k, v in sorted(anteile.items()))))
    return anteile


def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('namen', nargs='*')
    p.add_argument('--vorschau', help='r,g,b - Texturen umgefaerbt nach --ziel schreiben')
    p.add_argument('--ziel')
    a = p.parse_args(argv)
    register = json.loads(REGISTRY.read_text(encoding='utf-8'))['fahrzeuge']
    namen = a.namen or list(register)
    vorschau = [float(x) for x in a.vorschau.split(',')] if a.vorschau else None
    for name in namen:
        fahrzeug(name, register[name], vorschau, a.ziel)
    # Vorgabe-Maske des Materials M_WbTrafficCarLack (schwarz = nichts umfaerben).
    Image.new('L', (4, 4), 0).save(ROOT / 'Data' / 'Raw' / 'Verkehr' / LEER_MASKE)
    return 0


if __name__ == '__main__':
    raise SystemExit(hauptprogramm())
