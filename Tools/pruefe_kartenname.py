"""Prueft, dass der Kartenname nur an EINER Stelle steht.

Die eine Stelle ist `GameDefaultMap` in `Config/DefaultEngine.ini`. Alles
andere - Launcher, Foto-, Mess- und Diagnose-Skripte, Werkzeuge - liest sie
ueber `Tools/karte.cmd` bzw. `Tools/karte.py`.

WARUM DIESE WACHE: Ohne sie waechst die Verdrahtung nach. Gemessen am
19.09.2026, bevor sie es gab: 14 Skripte starteten `WiesbadenCity_Alkis` - eine
Karte, die es gar nicht mehr gibt und die den Start in einem englischen
Engine-Dialog haengen laesst -, weitere rund 20 zeigten auf Alkis3, Alkis4 oder
Alkis15, waehrend die Stadt Alkis17 war. Ein Messlauf auf einer toten Karte
sieht aus wie ein Messlauf.

Aufruf::

    python Tools/pruefe_kartenname.py          # 0 = sauber, 1 = Fundstellen

Ausgenommen sind nur Dateien, bei denen der Kartenname der ZWECK ist (ein Bake
schreibt eine bestimmte neue Karte) oder die ihn als Geschichte festhalten.
"""

import os
import re
import sys

# Nur KONKRETE Namen sind ein Fehler: "WiesbadenCity_Alkis17" und der tote
# "WiesbadenCity_Alkis" ohne Ziffer. Platzhalter wie "WiesbadenCity_AlkisNN"
# in Fehlermeldungen und Beispielen sind ausdruecklich erlaubt - sie zeigen
# die FORM, nicht eine Karte.
MUSTER = re.compile(r"WiesbadenCity_Alkis(?:[0-9][0-9A-Za-z]*)?\b")

# Der Name IST hier die Aussage - kein Fehler.
AUSGENOMMEN_DATEIEN = {
    # Die eine Quelle.
    "Config/DefaultEngine.ini",
    # Die Leser dieser Quelle nennen ein Beispiel.
    "Tools/karte.cmd",
    "Tools/karte.py",
    "Tools/karte.ps1",
    "Tools/pruefe_kartenname.py",
    # Bake-Skripte: sie erzeugen GENAU diese Karte.
    "rebake_alkis10.cmd", "rebake_alkis11.cmd", "rebake_alkis12.cmd",
    "rebake_alkis13.cmd", "rebake_alkis16.cmd", "rebake_alkis17.cmd",
    "rebake_lod2.cmd", "rebake_lod2_dgm1.cmd", "rebake_nodgm.cmd",
    "rebuild_stadt.cmd", "rebuild_baumfrei.cmd", "rebuild_map.cmd",
    # Nach einer Version benannt.
    "shot_alkis16.cmd", "fps_alkis10.cmd",
    # Erklaert im Kommentar, warum der alte Name weg ist.
    "play.cmd",
    # Gehoeren dem Verkehrsschild-Strang (uncommitted). Nach dessen
    # Auslieferung hier streichen und die Dateien mit umstellen.
    "README.md",
    "docs/README.md",
    "Tools/fetch_city_content.cmd",
    "Tools/fetch_city_content.py",
    "docs/reference/stadtinhalt-holen.md",
}

# Geschichte: Lernpunkte und alte Plaene halten fest, was damals galt.
AUSGENOMMEN_ORDNER = (
    "Saved/",
    "docs/superpowers/plans/",
    "Intermediate/",
    "Binaries/",
    "Content/",
)

AUSGENOMMEN_ENDUNGEN = (".log", ".png", ".uasset", ".umap", ".json")

GEPRUEFTE_ENDUNGEN = (".cmd", ".py", ".ini", ".md", ".cpp", ".h", ".cs", ".ps1")


def relativ(wurzel, pfad):
    return os.path.relpath(pfad, wurzel).replace(os.sep, "/")


def main():
    wurzel = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    fundstellen = []

    for ordner, unterordner, dateien in os.walk(wurzel):
        rel_ordner = relativ(wurzel, ordner) + "/"
        if rel_ordner.startswith(".git/") or any(
                rel_ordner.startswith(a) for a in AUSGENOMMEN_ORDNER):
            unterordner[:] = []
            continue

        for datei in dateien:
            if not datei.endswith(GEPRUEFTE_ENDUNGEN) or datei.endswith(AUSGENOMMEN_ENDUNGEN):
                continue
            pfad = os.path.join(ordner, datei)
            rel = relativ(wurzel, pfad)
            if rel in AUSGENOMMEN_DATEIEN or datei in AUSGENOMMEN_DATEIEN:
                continue
            # AGENTS.md und NEUER_PC.md halten Geschichte fest.
            if rel in ("AGENTS.md", "NEUER_PC.md"):
                continue

            try:
                with open(pfad, "rb") as f:
                    text = f.read().decode("utf-8", errors="replace")
            except OSError:
                continue

            for nummer, zeile in enumerate(text.splitlines(), 1):
                if MUSTER.search(zeile):
                    fundstellen.append((rel, nummer, zeile.strip()[:100]))

    if not fundstellen:
        print("Kartenname: sauber - nur Config/DefaultEngine.ini nennt ihn.")
        return 0

    print(f"Kartenname fest verdrahtet an {len(fundstellen)} Stelle(n):")
    for rel, nummer, zeile in fundstellen:
        print(f"  {rel}:{nummer}: {zeile}")
    print("\nStattdessen die Quelle lesen: Tools/karte.cmd (Batch) oder "
          "Tools/karte.py (Python).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
