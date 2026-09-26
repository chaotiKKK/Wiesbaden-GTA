"""Die Bewegungen der Sebbo-Spielerfigur - gelesen aus DER Liste in C++.

Quelle ist EWbSebboMove in Source/WiesbadenReal/Vehicles/
WiesbadenSebboFigureComponent.h: je Eintrag der Name (= Asset A_Sebbo_<Name>)
und woher der Clip kommt:
  UMETA(TripoClip = "<Stamm>")    - fertig im Tripo-GLB
  UMETA(BlenderFrom = "<Name>")   - in Blender aus dieser Bewegung gebaut
                                    (build_sebbo_player.py, z. B. Ducken)
Blender (build_sebbo_player.py) und Import (import_tripo_figure.py) lesen nur
hier, damit es keine zweite und dritte Abschrift gibt.
"""
import re
from pathlib import Path

HEADER = (Path(__file__).resolve().parents[1] / 'Source' / 'WiesbadenReal' / 'Vehicles'
          / 'WiesbadenSebboFigureComponent.h')


def bewegungen():
    """[(Name, Art, Quelle), ...] in der Reihenfolge von EWbSebboMove.

    Art ist 'tripo' (Quelle = Stamm des Tripo-Clips) oder 'blender' (Quelle =
    Name der Bewegung, aus der Blender den Clip baut).
    """
    text = HEADER.read_text(encoding='utf-8')
    body = re.search(r'enum class EWbSebboMove\b[^{]*\{(.*?)\};', text, re.S)
    if not body:
        raise RuntimeError('EWbSebboMove nicht gefunden in %s' % HEADER)
    found = re.findall(r'^\s*(\w+)\s+UMETA\(\s*(TripoClip|BlenderFrom)\s*=\s*"([^"]+)"\s*\)',
                       body.group(1), re.M)
    if not found:
        raise RuntimeError('EWbSebboMove ohne TripoClip/BlenderFrom-Eintraege in %s' % HEADER)
    return [(name, 'tripo' if key == 'TripoClip' else 'blender', src) for name, key, src in found]


if __name__ == '__main__':
    for name, art, src in bewegungen():
        print('%-10s <- %s %s' % (name, art, src))
