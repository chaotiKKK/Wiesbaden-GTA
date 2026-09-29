"""Nach dem Gate-Lauf: steht der Ref noch da, den das Gate gerade geprueft hat?

WARUM ES DAS GIBT - GEMESSEN am 27.09.2026, 23:32:
Der Push von `feature/platte-zen-gate` lief 25 Minuten durch alle sieben
Gates. Zwei Minuten nach dem Start committete ein anderer Thread auf
denselben Branch, 18 Minuten vor Schluss stand der Branch auf einem
Commit, den kein Gate gesehen hatte. Der Push lief durch ("Alle Gates
gruen") und hat genau diesen Commit nach origin gebracht.

Das ist die schlimmste Sorte Fehler in diesem ganzen Geflecht: kein
rotes Gate, sondern ein gruenes ohne Messung. Das Gate hat einen Baum
geprueft, der hinausgegangen ist ein anderer.

DESHALB ZWEI SCHRITTE, nicht einer:
  * `merken` liest die Refs, die git pushen WILL, BEVOR das Gate laeuft.
  * `pruefen` loest dieselben Refs NACHHER noch einmal auf und vergleicht.
    Weicht einer ab, wird der Push abgewiesen - mit beiden Shas, dem
    neuen Committer und dem Rat, es erneut zu fahren.

Warum ein eigenes Werkzeug und kein Eingriff in `vor_dem_commit.py`:
diese Datei wird von einem anderen Thread entwickelt. Der Hook ist die
Stelle, an der die Reihenfolge festliegt - dort gehoert die Pruefung
hin, ohne fremde Arbeit anzufassen.

    python Tools/push_ref_wache.py merken <datei>        # liest stdin
    python Tools/push_ref_wache.py pruefen <datei> [remote]

Rueckgabe 0 = unveraendert, 1 = Ref hat sich bewegt (Push abweisen).
"""

import io
import json
import os
import subprocess
import sys
import time


def drucke(text, file=None):
    """Print ohne Unicode-Absturz (Gleiche Hilfe wie vor_dem_commit.drucke)."""
    ziel = file if file is not None else sys.stdout
    try:
        print(text, file=ziel, flush=True)
    except UnicodeEncodeError:
        fehler = getattr(ziel, "errors", None) or "strict"
        if fehler != "strict":
            raise
        roh = text.encode(ziel.encoding or "ascii", "replace")
        kanal = getattr(ziel, "buffer", None)
        if kanal is None:
            print(roh.decode(ziel.encoding or "ascii"), file=ziel, flush=True)
            return
        kanal.write(roh + b"\n")
        kanal.flush()

NULL_SHA = "0" * 40


def refs_lesen(text):
    """Die pre-push-Eingabe: "<lokale Ref> <lokaler Sha> <Ref> <Sha>" je Zeile.

    Das Format ist das von git selbst und wird hier genauso gelesen wie in
    `gate_worktree.push_shas` - mit einer Ausnahme: dort werden nur die
    Shas gebraucht, hier die Refs NAMEN, denn nur die kann man nachher
    wieder auflosen. Loeschungen (lokaler Sha nur Nullen) kommen mit durch;
    sie sind fuer diese Pruefung uninteresting, aber nicht falsch.
    """
    refs = []
    for zeile in (text or "").splitlines():
        teile = zeile.split()
        if len(teile) < 4:
            continue
        refs.append({"lokale_ref": teile[0], "lokaler_sha": teile[1],
                     "entfernte_ref": teile[2], "entfernter_sha": teile[3]})
    return refs


def merken(text, pfad, jetzt=None):
    """Die Refs des anstehenden Pushes nach `pfad` schreiben.

    `jetzt` ist nur fuer die Tests da. Zurueckgegeben wird, was gemerkt
    wurde - damit der Aufrufer sieht, ob es ueberhaupt etwas zu pruefen
    gab (eine Loeschung ist kein Push, den man verfolgen muss).
    """
    refs = refs_lesen(text)
    daten = {"zeit": jetzt if jetzt is not None else time.time(),
             "refs": refs,
             "pruefbar": [r for r in refs if r["lokaler_sha"] != NULL_SHA]}
    ordner = os.path.dirname(pfad)
    if ordner and not os.path.isdir(ordner):
        os.makedirs(ordner, exist_ok=True)
    with io.open(pfad, "w", encoding="utf-8") as f:
        json.dump(daten, f, ensure_ascii=False, sort_keys=True)
    return daten


def geladen(pfad):
    with io.open(pfad, "r", encoding="utf-8") as f:
        return json.load(f)


def _git(repo, *args):
    try:
        r = subprocess.run(["git"] + list(args), cwd=repo, capture_output=True,
                           text=True, errors="replace", timeout=120)
    except (OSError, subprocess.SubprocessError) as e:
        return None, str(e)
    if r.returncode != 0:
        return None, (r.stderr or r.stdout).strip()
    return r.stdout.strip(), None


def _kurz(sha):
    return sha[:10] if sha else "?"


def _woher(repo, sha):
    """Wer hat diesen Commit gemacht? fuer die Meldung, nicht fuer die Regel."""
    ausgabe, fehler = _git(repo, "log", "-1", "--format=%an (%ci)", sha)
    return ausgabe if ausgabe and not fehler else ""


def _entfernter_sha(repo, remote, entfernte_ref, gemerkt):
    """Der Sha, den das Remote JETZT haelt - oder None, wenn unbekannt.

    Ohne Remote (etwa im Test) wird nicht geraten: der lokale
    Remote-Tracking-Ref ist eine Erinnerung, kein Beweis. Lieber
    "unbekannt" sagen als eine Zahl, die von gestern ist.
    """
    if not remote or gemerkt == NULL_SHA:
        # Ein NEUER Branch: nur `ls-remote` kann sagen, ob ihn inzwischen
        # jemand angelegt hat. Ohne Remote nicht feststellbar.
        if gemerkt == NULL_SHA and remote:
            ausgabe, _fehler = _git(repo, "ls-remote", remote, entfernte_ref)
            if ausgabe:
                teile = ausgabe.split()
                return teile[0] if teile else None
        return None
    ausgabe, _fehler = _git(repo, "ls-remote", remote, entfernte_ref)
    if not ausgabe:
        return None
    teile = ausgabe.split()
    return teile[0] if teile else None


def pruefen(pfad, repo=None, remote=None):
    """Sind die Refs noch die, die geprueft wurden? (rc, Zeilen)

    `rc` 1 heisst: der Push wird abgewiesen. Beide Fuelle werden
    unterschieden, weil sie verschiedene Ursachen haben:
      * die LOKALE Ref hat sich bewegt - ein Commit kam waehrend des
        Laufs dazu. Genau der Fall vom 27.09.2026.
      * das REMOTE hat sich bewegt - jemand anderes hat zwischenzeitlich
        gepusht. Unser Push waere dann nicht mehr auf seinem Stand.
    """
    repo = repo or os.getcwd()
    try:
        daten = geladen(pfad)
    except (OSError, ValueError) as e:
        # Ohne die gemerkten Refs gibt es nichts, wogegen man pruefen
        # koennte. Das ist kein Grund durchzuwinken: dann laeuft der Push
        # ohne diese Absicherung. Also abweisen und sagen, was fehlt.
        return 1, ["Push-Ref-Waechter: die Merksdatei %s ist unlesbar (%s) - "
                   "der Push wird abgewiesen, weil nicht feststellbar ist, ob "
                   "der Ref stehen blieb." % (pfad, e)]
    refs = daten.get("pruefbar") or []
    if not refs:
        return 0, ["Push-Ref-Waechter: nichts zu pruefen (nur Loeschungen?)."]

    zeilen = []
    bewegt = False
    for r in refs:
        jetzt, _fehler = _git(repo, "rev-parse", "--verify", "--quiet",
                              r["lokale_ref"] + "^{commit}")
        if not jetzt:
            bewegt = True
            zeilen.append("  %s: die Ref gibt es nicht mehr - sie wurde waehrend "
                          "des Laufs geloescht oder umbenannt (vorher %s)"
                          % (r["lokale_ref"], _kurz(r["lokaler_sha"])))
            continue
        if jetzt != r["lokaler_sha"]:
            bewegt = True
            zeilen.append("  %s: %s -> %s%s"
                          % (r["lokale_ref"], _kurz(r["lokaler_sha"]),
                             _kurz(jetzt), ("  von " + _woher(repo, jetzt))
                             if _woher(repo, jetzt) else ""))
            continue
        # Lokal unveraendert. Jetzt das Remote.
        jetzt_r = _entfernter_sha(repo, remote, r["entfernte_ref"],
                                  r["entfernter_sha"])
        if jetzt_r is None:
            if r["entfernter_sha"] != NULL_SHA:
                zeilen.append("  %s: unveraendert, aber der Stand von origin "
                              "konnte nicht abgefragt werden - nicht verglichen"
                              % r["lokale_ref"])
            continue
        erwartet = r["entfernter_sha"]
        erwartet_leer = erwartet == NULL_SHA
        if erwartet_leer and jetzt_r:
            bewegt = True
            zeilen.append("  %s: der Branch sollte NEU angelegt werden, auf "
                          "origin liegt er aber schon (%s) - zwischen Gate und "
                          "Push hat ihn jemand angelegt" % (r["lokale_ref"],
                                                             _kurz(jetzt_r)))
        elif not erwartet_leer and jetzt_r != erwartet:
            bewegt = True
            zeilen.append("  %s: origin stand auf %s, jetzt auf %s - zwischen "
                          "Gate und Push hat jemand anderes gepusht"
                          % (r["lokale_ref"], _kurz(erwartet), _kurz(jetzt_r)))

    if not bewegt:
        return 0, (zeilen + ["Push-Ref-Waechter: %d Ref(s) unveraendert - das "
                             "Gate hat genau das geprueft, was hinausgeht."
                             % len(refs)])
    kopf = ("Push-Ref-Waechter: der Ref hat sich WAEHREND des Gate-Laufs "
            "bewegt - der Push wird abgewiesen.")
    rat = ("Das Gate hat %s geprueft. Herausgehen wuerde ein anderer Stand. "
           "Neu fahren: git push (das Gate laeuft dann auf dem neuen Stand). "
           "Wer es wirklich will: git push --no-verify."
           % ", ".join(_kurz(r["lokaler_sha"]) for r in refs))
    return 1, [kopf] + zeilen + ["", rat]


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv or argv[0] not in ("merken", "pruefen"):
        print(__doc__.strip())
        return 2
    befehl = argv.pop(0)
    if not argv:
        print("Aufruf: python Tools/push_ref_wache.py %s <datei>%s"
              % (befehl, " [remote]" if befehl == "pruefen" else ""))
        return 2
    pfad = argv.pop(0)
    if befehl == "merken":
        daten = merken(sys.stdin.read(), pfad)
        print("Push-Ref-Waechter: %d Ref(s) gemerkt, davon %d zu pruefen -> %s"
              % (len(daten["refs"]), len(daten["pruefbar"]), pfad))
        return 0
    rc, zeilen = pruefen(pfad, remote=argv[0] if argv else None)
    for z in zeilen:
        drucke(z)
    return rc


if __name__ == "__main__":
    sys.exit(main())
