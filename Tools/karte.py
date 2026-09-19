"""Die aktuelle Stadt-Karte - EINE Quelle fuer alle Werkzeuge.

Gelesen wird `GameDefaultMap` aus `Config/DefaultEngine.ini`. Das ist die
Karte, die das Spiel ohne Argument startet; alles andere waere eine zweite
Wahrheit.

WARUM: Die Werkzeuge trugen ihre Karte als Vorgabe fest im Text - nach drei
Bakes zeigten sie auf Alkis3, Alkis4 und Alkis15, waehrend die Stadt laengst
Alkis17 war. Ein Lauf auf einer toten Karte sieht aus wie ein Lauf; er macht
nur etwas anderes.

Benutzung::

    from karte import standard_karte, standard_karte_pfad
    MAP = os.environ.get("WB_MAP", standard_karte_pfad())

Die Umgebungsvariable behaelt Vorrang - ein Bake-Lauf gibt seine Zielkarte
weiterhin ausdruecklich mit.
"""

import os
import re

_HIER = os.path.dirname(os.path.abspath(__file__))
INI = os.path.join(_HIER, "..", "Config", "DefaultEngine.ini")


def _lies_ini(pfad=INI):
    """GameDefaultMap aus der Ini holen (roh, mit Asset-Suffix)."""
    with open(pfad, "rb") as f:
        roh = f.read()
    for kodierung in ("utf-8-sig", "utf-8", "utf-16", "latin-1"):
        try:
            text = roh.decode(kodierung)
            break
        except (UnicodeDecodeError, UnicodeError):
            continue
    else:
        raise UnicodeError(f"{pfad}: keine passende Kodierung gefunden")

    treffer = re.search(r"^GameDefaultMap\s*=\s*(.+)$", text, re.MULTILINE)
    if not treffer:
        raise LookupError(f"{pfad}: GameDefaultMap steht nicht darin")
    return treffer.group(1).strip()


def standard_karte(pfad=INI):
    """Kurzer Kartenname, z. B. 'WiesbadenCity_Alkis17'."""
    roh = _lies_ini(pfad)
    # "/Game/Maps/WiesbadenCity_Alkis17.WiesbadenCity_Alkis17" -> Kurzname
    name = roh.rsplit("/", 1)[-1]
    return name.split(".", 1)[0]


def standard_karte_pfad(pfad=INI):
    """Paketpfad, z. B. '/Game/Maps/WiesbadenCity_Alkis17'."""
    return "/Game/Maps/" + standard_karte(pfad)


if __name__ == "__main__":
    print(standard_karte_pfad())
