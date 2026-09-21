r"""Die Engine dieses Projekts - EINE Quelle fuer alle Werkzeuge.

WARUM: Auf diesem Rechner liegen ZWEI Engines 5.8 nebeneinander::

    C:\Program Files\Epic Games\UE_5.8          5.8.2, Build.bat 09.09.2026
    C:\freebuff\WiesbadenReal_Sicherung\UE_5.8  5.8.1, Build.bat 11.08.2026

Beide heissen "UE_5.8", beide existieren, beide bauen - aber sie tragen
verschiedene PATCH-Staende. Das Projekt-Intermediate haelt den Shared-PCH
genau einer davon. Mischt man sie, stirbt der Build in einem ENGINE-Header
(`GenericPlatform.h`: C2953 "SelectIntPointerType" bereits definiert) - das
sieht nach kaputtem Engine-Quelltext aus und ist keiner.

Genau das ist am 21.09.2026 passiert: die Release-Pipeline leitete ihren
Engine-Pfad aus dem Projektordner ab und erwischte die Kopie, waehrend
Gate-1-Skript und Starter die installierte benutzten. Eine Test-Path-Pruefung
faellt darauf NICHT herein - die alte Kopie ist ja da. Ein Pfad, der
existiert und trotzdem falsch ist, faellt nur dem Vergleich auf.

Benutzung::

    from engine import engine_wurzel, build_bat, editor_cmd
    subprocess.run([build_bat(), "WiesbadenRealEditor", ...])

`WB_ENGINE` in der Umgebung behaelt Vorrang - wer bewusst gegen eine andere
Engine baut, sagt es ausdruecklich und bekommt sie.
"""
import json
import os
import re

# Die eine Stelle. Wer sie aendert, aendert sie fuer alle - und
# Tools/pruefe_engine.py haelt jede andere Nennung im Repo dagegen.
KANONISCH = r"C:\Program Files\Epic Games\UE_5.8"

_HIER = os.path.dirname(os.path.abspath(__file__))
PROJEKT = os.path.dirname(_HIER)


def engine_wurzel():
    """Die zu benutzende Engine; WB_ENGINE gewinnt."""
    return os.environ.get("WB_ENGINE") or KANONISCH


def build_bat():
    return os.path.join(engine_wurzel(), "Engine", "Build", "BatchFiles", "Build.bat")


def run_uat():
    return os.path.join(engine_wurzel(), "Engine", "Build", "BatchFiles", "RunUAT.bat")


def editor():
    return os.path.join(engine_wurzel(), "Engine", "Binaries", "Win64", "UnrealEditor.exe")


def editor_cmd():
    return os.path.join(engine_wurzel(), "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe")


def build_version(wurzel=None):
    """(major, minor, patch) aus Engine/Build/Build.version, sonst None.

    Der Ordnername luegt nicht absichtlich, aber er luegt: BEIDE Engines
    heissen "UE_5.8", und erst die Patch-Nummer (5.8.2 gegen 5.8.1) trennt
    sie. Wer nur den Namen vergleicht, haelt sie fuer dieselbe.
    """
    pfad = os.path.join(wurzel or engine_wurzel(), "Engine", "Build", "Build.version")
    try:
        with open(pfad, encoding="utf-8-sig") as f:
            daten = json.load(f)
    except (OSError, ValueError):
        return None
    try:
        return (int(daten["MajorVersion"]), int(daten["MinorVersion"]),
                int(daten.get("PatchVersion", 0)))
    except (KeyError, TypeError, ValueError):
        return None


def uproject_erwartung():
    """major.minor aus EngineAssociation des .uproject, sonst None."""
    pfad = os.path.join(PROJEKT, "WiesbadenReal.uproject")
    try:
        with open(pfad, encoding="utf-8-sig") as f:
            daten = json.load(f)
    except (OSError, ValueError):
        return None
    treffer = re.match(r"^(\d+)\.(\d+)", str(daten.get("EngineAssociation", "")))
    return (int(treffer.group(1)), int(treffer.group(2))) if treffer else None


def pruefen(wurzel=None):
    """(ok, meldung) - existiert die Engine und passt sie zum .uproject?"""
    w = wurzel or engine_wurzel()
    if not os.path.isfile(os.path.join(w, "Engine", "Build", "BatchFiles", "Build.bat")):
        return False, "keine Engine unter %s" % w
    version = build_version(w)
    if version is None:
        return False, "%s traegt keine lesbare Build.version" % w
    erwartet = uproject_erwartung()
    if erwartet and version[:2] != erwartet:
        return False, ("%s ist %d.%d.%d, das Projekt verlangt %d.%d"
                       % (w, version[0], version[1], version[2], erwartet[0], erwartet[1]))
    return True, "%s (%d.%d.%d)" % (w, version[0], version[1], version[2])


if __name__ == "__main__":
    ok, meldung = pruefen()
    print(("OK:    " if ok else "FEHLER: ") + meldung)
    raise SystemExit(0 if ok else 1)
