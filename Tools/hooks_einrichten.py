r"""Die Git-Hooks dieses Projekts scharf schalten (oder abschalten).

    python Tools/hooks_einrichten.py            # einschalten
    python Tools/hooks_einrichten.py --zeigen   # nur den Stand melden
    python Tools/hooks_einrichten.py --aus      # wieder abschalten

WARUM NICHT .git/hooks: dieser Ordner ist NICHT versioniert. Ein Hook, den
man dort ablegt, ueberlebt keinen frischen Klon und keinen zweiten Rechner -
er ist genau so lange da, wie sich jemand daran erinnert, ihn hinzulegen.
Die Hooks liegen darum unter `Tools/git-hooks/` im Repo, und `core.hooksPath`
zeigt dorthin. Das ist eine LOKALE Einstellung; sie muss einmal pro Klon
gesetzt werden, und genau dafuer gibt es dieses Werkzeug.

Was dann laeuft:

    pre-commit   Gate 0, Python-Suiten, Gate 1 wenn C++ dabei ist (~48 s)
    pre-push     zusaetzlich Gates 2 und 3 (Minuten, Unreal-Editor)
"""
import argparse
import os
import subprocess
import sys

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOKS = os.path.join("Tools", "git-hooks")
ERWARTET = ("pre-commit", "pre-push")


def git(*args):
    fertig = subprocess.run(["git", *args], cwd=WURZEL, capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    return fertig.returncode, fertig.stdout.strip()


def stand():
    _, pfad = git("config", "--get", "core.hooksPath")
    return pfad


def vorhanden():
    fehlt = [h for h in ERWARTET
             if not os.path.isfile(os.path.join(WURZEL, HOOKS, h))]
    return fehlt


def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(description="Git-Hooks ein- oder ausschalten.")
    p.add_argument("--zeigen", action="store_true", help="nur den Stand melden")
    p.add_argument("--aus", action="store_true", help="Hooks abschalten")
    a = p.parse_args(argv)

    fehlt = vorhanden()
    if fehlt and not a.aus:
        print("FEHLER: diese Hooks fehlen unter %s: %s" % (HOOKS, ", ".join(fehlt)))
        return 1

    if a.zeigen:
        jetzt = stand()
        if jetzt == HOOKS.replace(os.sep, "/") or jetzt == HOOKS:
            print("Hooks sind scharf: core.hooksPath = %s" % jetzt)
            for h in ERWARTET:
                print("  %s" % h)
            return 0
        print("Hooks sind NICHT scharf (core.hooksPath = %s)."
              % (jetzt or "nicht gesetzt"))
        print("Einschalten: python Tools/hooks_einrichten.py")
        return 1

    if a.aus:
        git("config", "--unset", "core.hooksPath")
        print("Hooks abgeschaltet (core.hooksPath entfernt).")
        return 0

    rc, _ = git("config", "core.hooksPath", HOOKS.replace(os.sep, "/"))
    if rc != 0:
        print("FEHLER: core.hooksPath liess sich nicht setzen.")
        return 1
    print("Hooks scharf: core.hooksPath = %s" % stand())
    print("  pre-commit  Gate 0, Python-Suiten, Gate 1 bei C++ (~48 s)")
    print("  pre-push    zusaetzlich Gates 2 und 3 (Minuten)")
    print("\nNotausgang im Einzelfall: --no-verify oder WB_KEINE_GATES=1")
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
