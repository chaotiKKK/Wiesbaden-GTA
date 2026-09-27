r"""Die Release-Gates fahren, BEVOR ein Commit entsteht - nicht erst beim Paket.

    python Tools/vor_dem_commit.py                # schnelle Stufe (Vorgabe)
    python Tools/vor_dem_commit.py --stufe voll   # zusaetzlich Gates 2 und 3
    python Tools/vor_dem_commit.py --gestaged     # nur was git vorgemerkt hat
    python Tools/vor_dem_commit.py --stufe voll --push-refs   # pre-push: im sauberen Worktree

WOFUER: Die Gates gab es schon, aber sie liefen erst in `build_release.cmd` -
also erst, wenn jemand ein Paket wollte. Ein Fehler von heute fiel damit
Tage spaeter auf, verteilt ueber mehrere Commits, und blockierte ausgerechnet
den Lauf, der Stunden dauert.

WARUM ZWEI STUFEN - und das ist eine gemessene Entscheidung, keine Meinung:

    Gate 0  Engine-Pfade        1 s
    Gate 1  Kompilieren         2 s ohne C++-Aenderung, Minuten mit
    Python-Suiten              31 s   (gemessen 21.09.2026, 172 Tests)
    Gate 2  Unit-Tests          Minuten (startet den Unreal-Editor)
    Gate 3  Rauchtest           Minuten (mehrere Editor-Sitzungen)
    Gate B  Besitz               0 s   (Registry, kein Prozess)
    Gate 4  Plasmacutter-Bild   1 min  (startet den Unreal-Editor)
    Gate 5  Ankerzustand (WP)   3 min  (startet den Unreal-Editor)

    Plattenplatz                0 s   (HINWEIS, kein Gate: Tools/platten_waechter.py.
                                         Im gesunden Fall ein Syscall, unter 20 Prozent
                                         freiem Plattenplatz die vollstaendige Messung.
                                         Er sperrt NIE einen Commit - eine volle Platte
                                         ist kein Fehler am Commit, und ein Hook, der
                                         Commits verweigert, wird umgangen.)

Ein Hook, der vor JEDEM Commit eine Viertelstunde braucht, wird binnen eines
Tages mit --no-verify umgangen; dann prueft er gar nichts mehr. Darum:

* **schnell** laeuft vor jedem Commit und enthaelt nur, was zur
  Release-Pipeline gehoert: Gate 0 immer, Gate 1 nur, wenn wirklich C++
  dabei ist - wer nur ein Python-Werkzeug aendert, wartet nicht auf einen
  Compiler.
* **voll** laeuft vor dem PUSH. Dort ist die Wartezeit vertretbar, und nichts
  verlaesst den Rechner ungeprueft. Die Blockade wandert damit vom
  Paketieren an die Stelle, an der sie noch billig ist.

GATE B (BESITZ) beantwortet eine Frage, die keine Qualitaetspruefung stellen
kann: **Gehoeren die Dateien in diesem Commit diesem Thread?** Gemessen am
27.09.2026 lagen vier Tage lang fremde Arbeit (SebboHq-Innenausbau, Proben-
Skripte) uncommitted im geteilten Arbeitsbaum, der Baum stand auf dem
Wegwerf-Testbranch eines anderen Threads, und `git status` sah aus wie die
eigene Arbeit. `git commit` nimmt ALLES mit, was vorgemerkt ist - git kennt
keine Threads.

    python Tools/vor_dem_commit.py --besitz-ansprechen Tools/ Source/WiesbadenReal/World/SebboHq*.cpp
    python Tools/vor_dem_commit.py --besitz-zeigen
    python Tools/vor_dem_commit.py --besitz-freigeben

Der Anspruch steht im gemeinsamen .git-Verzeichnis (`git rev-parse
--git-common-dir`) - von keinem Commit erfasst, fuer alle Worktrees desselben
Repos identisch. Er ist nach THREAD geschluesselt (nicht nach Branch: auf
einem Wegwerf-Branch arbeiten zwei Threads, und ein Branch-Schluessel liess
den Anspruch des einen beim Beanspruchen des anderen verschwinden). Er
enthaelt Thread, Branch, PID und die Dateimuster; ein Muster mit
abschliessendem `/` ist ein Praefix, sonst gilt fnmatch. Faellt eine
vorgemerkte Datei in den Anspruch eines ANDEREN Threads, ist Gate B rot und
nennt den Thread.

Drei Entscheidungen, die nicht selbstlaeufig sind:

* **Es blockiert nur ueberlappende ANSPRUECHE, nicht jeden anderen Thread.**
  Wer nichts beansprucht hat, wird nie blockiert - sonst muesste sich der
  allererste Thread eines Repos unsichtbar machen.
* **GLEICHER Branch ist nicht gleicher Thread.** Der erste echte Lauf dieses
  Gates (27.09.2026) meldete `gruen`, weil nur der Branch verglichen wurde -
  und der fremde Thread sass auf demselben Wegwerf-Branch `wt-gatetest` wie
  ich. Ein Wegwerf-Branch identifiziert niemanden; darum zaehlt der
  Thread-Name genauso wie der Branch.  * **Ein toter Prozess gibt seinen Anspruch frei** (Hinweis, kein ROT). Eine
  Registry, die einen abgestuerzten Thread ewig festhält, endet sonst in
  `--no-verify` fuer alle - dann waere das Gate nicht streng, sondern tot.
  (GEMESSEN beim Schreiben dieses Gates: die erste Fassung hat auch verwaiste
  Ansprueche blockiert. Der Test dafür ist der Grund fuer die Zweiteilung in
  Konflikte und Hinweise.)
* **Die kaputte Registry ist ROT, nicht "frei".** Wer sie loescht, schaltet
  genau das Gate ab, das ihn schuetzt. Das ist der stillschweigende Ausfall,
  vor dem hier alles andere warnt.

Gate B laeuft VOR dem Engine-Lock und vor Gate 0: es kostet Mikrosekunden,
und einen Compilerlauf fuer einen Commit, der nicht stattfinden darf, gibt
es nicht. Beim PUSH laeuft es NICHT - dort ist der Baum frisch gebaut und
der Commit schon geschrieben; Besitz gehoert an den Commit, sonst koennte
nach einem fremden Thread niemand mehr ausliefern.

GATE 4 (PLASMACUTTER-BILDFOLGE) ist die juengste Stufe und liegt ebenfalls
nur auf **voll**. Sie stellt ein Trenn-Stueck auf, schneidet es, fotografiert
die gluehende Kante aus drei Blickwinkeln und misst die Pixel selbst - ein
Bild ist ein Beleg, eine gruene Behauptung im Log nicht. Zwei Entscheidungen
daran:

* **IMMER, ohne Dateifilter.** Es gab zuerst eine Musterliste der
  Plasmacutter-Dateien als Vorbedingung. Im Commit-Worktree ist der Commit
  schon committed, eine aus dem Push-Bereich gebaute Liste ist dort LEER -
  und leer wurde als "nichts zu tun" gelesen. Das Gate waere bei jedem Push
  erscheinungslos entfallen. Was das Bild zerstoert, ist ohnehin nicht immer
  eine Plasmacutter-Datei: Licht, Material, Post-Process, Kamera.
* **IM WORKTREE LAEUFT ES OHNE DIE IMPORTIERTEN MESHES.** Content/ wird
  verlinkt, aber `waehle_stadtinhalt` nimmt aus den unversionierten Dateien
  nur die Stadtkarten - die Schnittstuecke aus Blender fehlen dort, der
  Cuttable faellt auf Wuerfel zurueck. Das Gate haengt nicht daran
  (gemessen: 5.4 / 13.5 / 2.8 % Glueh-Anteil mit Wuerfeln gegen 16 / 18 / 4 %
  mit den gebauten Meshes, Grenze ist 1 %), es belegt dort also das
  VERHALTEN, nicht die Mesh-Qualitaet.

DIE PYTHON-SUITEN LIEGEN AUF DER VOLLEN STUFE, und zwar aus zwei Gruenden:

1. Sie waren 31 s von 32 s der schnellen Stufe. Alles andere dort kostet
   zusammen eine Sekunde - der Hook bestand praktisch nur aus ihnen.
2. Sie sind kein Gate der Release-Pipeline. build_release.ps1 faehrt Gate 0
   bis 3 und ruft sie nirgends auf; vor dem Commit standen sie als Zugabe.

Sie sind VERSCHOBEN, NICHT GESTRICHEN: build_release.cmd kennt sie nicht,
darum faehrt die volle Stufe sie selbst. Nichts verlaesst den Rechner, ohne
dass sie gelaufen sind.

BEIM PUSH IM SAUBEREN WORKTREE (--push-refs, seit 25.09.2026): der
pre-push-Hook reicht die zu pushenden Commits herein, und die volle Stufe
laeuft in einem eigenen Worktree auf genau diesen Commits
(Tools/gate_worktree.py) - fremde laufende Arbeit im Arbeitsbaum kann den
Push weder faelschlich rot machen noch blockieren.

Notausgang: `git commit --no-verify` oder `WB_KEINE_GATES=1`. Er ist
absichtlich da - ein Wachposten ohne Tuer wird eingerissen, nicht benutzt.
"""
import argparse
import fnmatch
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(WURZEL, "Tools")

# Endungen, die einen Kompilierlauf noetig machen. Alles andere kann den
# Compiler nicht kaputt machen und soll ihn darum nicht kosten.
CPP_ENDUNGEN = (".cpp", ".h", ".cs", ".inl")

def saubere_umgebung():
    """Umgebung OHNE die GIT_*-Variablen des laufenden Hooks.

    GEMESSEN am 21.09.2026, und es war knapp: git setzt fuer
    `git commit --only` ein TEMPORAERES GIT_INDEX_FILE und vererbt es an
    jeden Unterprozess. Die Testsuiten legen Wegwerf-Repos an und rufen dort
    `git add -A` - das schrieb prompt in den Index des laufenden Commits.
    Ergebnis: drei Suiten fielen um, und eine Wegwerfdatei stand im Index des
    echten Commits. Waeren die Gates gruen gewesen, waere sie mitgekommen.

    Ein Hook darf nicht in den Commit hineinwirken, den er pruefen soll.
    """
    umgebung = dict(os.environ)
    for name in [k for k in umgebung if k.startswith("GIT_")]:
        del umgebung[name]
    return umgebung


def gestagte_dateien():
    """Was git fuer diesen Commit vorgemerkt hat."""
    roh = subprocess.run(["git", "diff", "--cached", "--name-only", "-z"],
                         cwd=WURZEL, capture_output=True)
    return [t.decode("utf-8", "surrogateescape")
            for t in roh.stdout.split(b"\0") if t]


def geaenderte_dateien():
    """Alles, was im Baum anders ist als HEAD - fuer Laeufe ausserhalb eines Hooks."""
    roh = subprocess.run(["git", "diff", "HEAD", "--name-only", "-z"],
                         cwd=WURZEL, capture_output=True)
    return [t.decode("utf-8", "surrogateescape")
            for t in roh.stdout.split(b"\0") if t]


def zu_pushende_dateien(cwd=WURZEL):
    """Die Dateien des Commit-Bereichs, der wirklich hinausgeht.

    GEMESSEN am 21.09.2026, und es war ein stiller Ausfall: die volle Stufe
    fragte `git diff HEAD` - also den ARBEITSBAUM. Nach einem Commit ist der
    leer, `braucht_compiler([])` war damit falsch, und Gate 1 wurde beim Push
    UEBERSPRUNGEN. Auf diesem Zweig lagen in dem Moment 20 C++-Dateien im
    Push-Bereich und null im Baum: das Kompilier-Gate feuerte nie fuer den
    Code, der tatsaechlich hinausging.

    Richtig ist der Bereich gegen den Upstream. DREI Punkte (`@{u}...HEAD`),
    nicht zwei: gemessen wird ab dem Merge-Base, also genau das, was dieser
    Zweig hinzufuegt - nicht zusaetzlich das, was der Upstream inzwischen
    selbst bekommen hat.

    Rueckgabe None = Bereich NICHT bestimmbar (kein Upstream, kaputtes Repo).
    Dann darf nichts uebersprungen werden; siehe braucht_compiler.
    """
    zeiger = subprocess.run(
        ["git", "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"],
        cwd=cwd, capture_output=True, text=True, env=saubere_umgebung())
    if zeiger.returncode != 0 or not zeiger.stdout.strip():
        return None

    roh = subprocess.run(
        ["git", "diff", "--name-only", "-z", "%s...HEAD" % zeiger.stdout.strip()],
        cwd=cwd, capture_output=True, env=saubere_umgebung())
    if roh.returncode != 0:
        return None
    return [t.decode("utf-8", "surrogateescape")
            for t in roh.stdout.split(b"\0") if t]


def braucht_compiler(dateien):
    """None = unbestimmbarer Bereich -> im Zweifel kompilieren.

    Die Richtung ist Absicht. Ein ueberfluessiger Compilerlauf kostet
    Minuten; ein ausgelassener laesst ungebauten Code hinaus.
    """
    if dateien is None:
        return True
    return any(d.lower().endswith(CPP_ENDUNGEN) for d in dateien)


# --------------------------------------------------------------------------
# BESITZ: WER DARF WAS COMMITTEN
#
# GEMESSEN am 27.09.2026, und der Befund ist der Grund fuer dieses Gate: Im
# geteilten Arbeitsbaum lagen vier Tage lang fremde Dateien (SebboHq-Innenausbau,
# Innen-Probe-Skripte eines anderen Threads) uncommitted, der Baum stand auf
# einem Wegwerf-Testbranch `wt-gatetest` eines anderen Threads, und `git status`
# sah aus wie die eigene Arbeit. Ein Thread, der committet, nimmt ALLES mit,
# was vorgemerkt ist - git kennt keineThreads, und ein Hook, der es nicht
# auch kennt, kann es nicht verhindern.
#
# Die Registry liegt im GEMEINSAMEN .git-Verzeichnis (`git rev-parse
# --git-common-dir`), nicht im Arbeitsbaum: dort ist sie von keinem Commit
# erfasst und fuer alle Worktrees desselben Repos dieselbe.
#
# Ein Anspruch nennt Thread, Branch und Dateimuster. Ein Muster mit
# abschliessendem Schraegstrich bedeutet Praefix (`Tools/`), sonst gilt es als
# fnmatch-Muster. Fremd ist ein Anspruch, wenn Branch ODER Thread nicht
# meiner ist - der 27.09.-Fall sass auf demselben Wegwerf-Branch wie der
# andere Thread, eine reine Branch-Pruefung waere dort gruen gewesen.
#
# DER SCHLUESSEL IST DER THREAD, NICHT DER BRANCH (Version 2). GEMESSEN beim
# Schreiben dieses Gates: die erste Fassung schluesselte die Registry nach
# Branch, und ein Aufruf von `--besitz-ansprechen` auf `wt-gatetest`
# ueberschrieb den Anspruch des anderen Threads, der auf DEMSELBEN Wegwerf-
# Branch sass - es gibt dort zwei Threads, das ist der ganze Punkt. Der
# Anspruch eines anderen Threads ging dabei lautlos verloren. Version 1 wird
# beim Lesen stillschweigend in Version 2 ueberfuehrt (Branch als Threadname),
# weil eine alte Registry niemanden blockieren darf.
# --------------------------------------------------------------------------

BESITZ_DATEI = "wb_besitz.json"
BESITZ_VERSION = 2


def besitz_pfad():
    """Wo die Registry liegt - oder None, wenn es kein Git-Repo ist."""
    # Testbare Uebersteuerung; im Betrieb zeigt der Pfad auf das echte
    # gemeinsame .git-Verzeichnis.
    aus_umgebung = os.environ.get("WB_BESITZ_DATEI")
    if aus_umgebung:
        return aus_umgebung
    roh = subprocess.run(["git", "rev-parse", "--git-common-dir"],
                         cwd=WURZEL, capture_output=True, text=True,
                         env=saubere_umgebung())
    if roh.returncode != 0 or not roh.stdout.strip():
        return None
    ordner = roh.stdout.strip()
    if not os.path.isabs(ordner):
        ordner = os.path.join(WURZEL, ordner)
    return os.path.join(ordner, BESITZ_DATEI)


def besitz_laden():
    """(Registry, Fehlertext). Eine kaputte Registry wird NICHT still ignoriert.

    Wer die Datei loescht, schaltet genau das Gate ab, das ihn schuetzt - das
    ist der stillschweigende Ausfall, vor dem hier alles andere warnt. Also:
    unlesbar heisst ROT mit Klartext, nicht "keine Ansprueche".
    """
    pfad = besitz_pfad()
    if not pfad:
        return None, None
    if not os.path.exists(pfad):
        return {"version": BESITZ_VERSION, "claims": {}}, None
    try:
        with open(pfad, encoding="utf-8") as f:
            daten = json.load(f)
    except (ValueError, OSError) as fehler:
        return None, "%s ist unlesbar (%s)" % (pfad, fehler)
    if not isinstance(daten, dict) or not isinstance(daten.get("claims"), dict):
        return None, "%s hat kein Format {'claims': {...}}" % pfad
    if daten.get("version", 1) < 2:
        # Version 1 schluesselte nach Branch. Der Branch-Eintrag wird zum
        # Thread-Eintrag, sonst koennte die Umstellung den Besitz aufheben.
        alt = {}
        for branch, anspruch in daten["claims"].items():
            if not isinstance(anspruch, dict):
                continue
            thread = anspruch.get("thread") or branch
            alt[thread] = dict(anspruch, thread=thread, branch=branch)
        daten = {"version": BESITZ_VERSION, "claims": alt}
    return daten, None


def besitz_sichern(registry):
    """Atomar schreiben - zwei Threads, die gleichzeitig beanspruchen,
    duerfen sich nicht gegenseitig die halbe Datei wegschreiben."""
    pfad = besitz_pfad()
    if not pfad:
        raise RuntimeError("kein Git-Repo - keine Besitz-Registry")
    os.makedirs(os.path.dirname(pfad), exist_ok=True)
    tmp = "%s.tmp%d" % (pfad, os.getpid())
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(registry, f, indent=2, sort_keys=True)
        f.write("\n")
    os.replace(tmp, pfad)
    return pfad


def prozess_lebt(pid):
    """Laeuft die PID noch? Ein toter Thread gibt seinen Anspruch frei.

    Sonst blockiert die Registry irgendwann jeden Commit, niemand traut sich
    an die Ausnahme, und alle benutzen --no-verify: das Gate ist dann nicht
    mehr streng, sondern tot.
    """
    if not pid or int(pid) <= 0:
        return False
    if os.name == "nt":
        import ctypes
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        STILL_ACTIVE = 259
        k = ctypes.windll.kernel32
        h = k.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
        if not h:
            return False
        try:
            code = ctypes.c_ulong()
            if not k.GetExitCodeProcess(h, ctypes.byref(code)):
                return False
            return code.value == STILL_ACTIVE
        finally:
            k.CloseHandle(h)
    try:
        os.kill(int(pid), 0)
    except OSError:
        return False
    return True


def besitz_treffer(muster, pfad):
    """Passt der Pfad zu einem Anspruch? `Tools/` ist Praefix, sonst fnmatch."""
    p = pfad.replace("\\", "/")
    for m in muster or ():
        m = str(m).replace("\\", "/")
        if m.endswith("/"):
            if p.startswith(m):
                return True
        elif fnmatch.fnmatchcase(p, m):
            return True
    return False


def besitz_konflikte(dateien, registry, eigener_branch, eigener_thread=None):
    """(blockierende Konflikte, Hinweise).

    Die Claims sind nach THREAD geschluesselt. Konflikt = Datei aus diesem
    Commit liegt im Muster eines Anspruchs, der nicht mir gehoert. Auch ein
    Anspruch DESSELBEN Threads auf einem anderen Branch zaehlt als fremd -
    zwei Branches sind nicht dasselbe Arbeitsverzeichnis.

    GEMESSEN am 27.09.2026, beim ersten echten Lauf dieses Gates: die Fassung
    mit NUR der Branch-Pruefung meldete `gruen` in genau der Lage, fuer die
    sie gebaut wurde. Der fremde Thread und ich sassen beide auf dem
    Wegwerf-Branch `wt-gatetest` - dort ist der Branch per Definition
    "eigen", und das Gate haette den Commit durchgelassen. Der Branch allein
    identifiziert einen Thread nicht; auf Wegwerf-Branches identifiziert er
    gar nichts.

    Ein Anspruch, dessen Prozess nicht mehr laeuft, wird NICHT blockierend
    gemeldet - er steht in den Hinweisen. Ein abgestuerzter Thread, der einen
    Branch fuer immer festhält, treibt jeden in `--no-verify`, und dann
    prueft gar nichts mehr.
    """
    konflikte, hinweise = [], []
    for thread_name, anspruch in sorted((registry.get("claims") or {}).items()):
        if eigener_thread is not None and thread_name == eigener_thread:
            continue
        branch = anspruch.get("branch") or "?"
        lebt = prozess_lebt(anspruch.get("pid"))
        treffer = [d for d in (dateien or ())
                   if besitz_treffer(anspruch.get("muster"), d)]
        if not lebt:
            hinweise.append(
                "Anspruch von '%s' (Branch %s) ist verwaist (Prozess %s "
                "laeuft nicht mehr)%s" % (
                    thread_name, branch, anspruch.get("pid"),
                    " - %d Datei(en) waeren geschuetzt gewesen" % len(treffer)
                    if treffer else ""))
            continue
        for datei in treffer:
            konflikte.append({
                "datei": datei,
                "branch": branch,
                "thread": thread_name,
                "muster": anspruch.get("muster") or [],
                "gleicher_branch": branch == eigener_branch,
            })
    return konflikte, hinweise


def besitz_gate(dateien, lauf, eigener_branch=None, thread=None):
    """Das Besitz-Gate. True = ROT.

    Es laeuft VOR dem Engine-Lock und vor Gate 0: es kostet Mikrosekunden,
    und es waere voelliger Unsinn, fuer einen Commit, der nicht stattfinden
    darf, einen Compiler anzustossen.
    """
    if eigener_branch is None:
        eigener_branch = aktueller_branch()
    thread = thread or threadname(eigener_branch)

    registry, fehler = besitz_laden()
    if fehler:
        lauf.fahre_gate("Gate B  Besitz", False, 0.0, fehler)
        return True
    if registry is None:
        lauf.ueberspringe("Gate B  Besitz", "kein Git-Repo")
        return False

    konflikte, hinweise = besitz_konflikte(dateien, registry, eigener_branch,
                                          thread)

    if not konflikte:
        lauf.fahre_gate("Gate B  Besitz", True, 0.0,
                        "\n".join(hinweise) or None)
        return False

    zeilen = list(hinweise)
    zeilen.append("%d Datei(en) aus diesem Commit gehoeren einem anderen "
                  "Thread:" % len(konflikte))
    for k in sorted(konflikte, key=lambda x: x["datei"])[:20]:
        zeilen.append("  %s  ->  %s (Branch %s, Muster %s)%s" % (
            k["datei"], k["thread"], k["branch"], ", ".join(k["muster"]),
            " - GLEICHER Branch, anderer Thread" if k["gleicher_branch"] else ""))
    if len(konflikte) > 20:
        zeilen.append("  ... und %d weitere" % (len(konflikte) - 20))
    lauf.fahre_gate("Gate B  Besitz", False, 0.0, "\n".join(zeilen))
    return True


def aktueller_branch():
    roh = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"],
                         cwd=WURZEL, capture_output=True, text=True,
                         env=saubere_umgebung())
    name = roh.stdout.strip() if roh.returncode == 0 else ""
    return name or "(kein Branch)"


def threadname(branch=None):
    """Wer bin ich? WB_THREAD, sonst git config, sonst der Branch."""
    aus_umgebung = os.environ.get("WB_THREAD")
    if aus_umgebung:
        return aus_umgebung
    roh = subprocess.run(["git", "config", "--get", "wb.thread"],
                         cwd=WURZEL, capture_output=True, text=True,
                         env=saubere_umgebung())
    if roh.returncode == 0 and roh.stdout.strip():
        return roh.stdout.strip()
    return branch or aktueller_branch()


def besitz_ansprechen(muster, thread=None, branch=None):
    registry, fehler = besitz_laden()
    if fehler:
        raise RuntimeError(fehler)
    branch = branch or aktueller_branch()
    thread = thread or threadname(branch)
    anspruch = (registry.get("claims") or {}).get(thread, {})
    alt = set(anspruch.get("muster") or ())
    registry.setdefault("claims", {})[thread] = {
        "thread": thread,
        "branch": branch,
        "pid": os.getpid(),
        "zeit": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "muster": sorted(alt | set(muster or ())),
    }
    pfad = besitz_sichern(registry)
    print("Thread '%s' auf Branch '%s' beansprucht %d Muster in %s"
          % (thread, branch, len(registry["claims"][thread]["muster"]), pfad))
    return 0


def besitz_freigeben(branch=None, thread=None):
    registry, fehler = besitz_laden()
    if fehler:
        print(fehler)
        return 3
    thread = thread or threadname(branch or aktueller_branch())
    if thread not in (registry.get("claims") or {}):
        print("Thread '%s' hat keinen Anspruch - nichts zu tun." % thread)
        return 0
    del registry["claims"][thread]
    pfad = besitz_sichern(registry)
    print("Anspruch von '%s' freigegeben (%s)." % (thread, pfad))
    return 0


def besitz_zeigen():
    registry, fehler = besitz_laden()
    if fehler:
        print(fehler)
        return 3
    claims = registry.get("claims") or {}
    if not claims:
        print("Keine Ansprueche. Beanspruchen mit:\n"
              "  python Tools/vor_dem_commit.py --besitz-ansprechen "
              "Source/WiesbadenReal/World/SebboHq*.cpp Tools/")
        return 0
    hier = aktueller_branch()
    for thread_name, anspruch in sorted(claims.items()):
        lebt = prozess_lebt(anspruch.get("pid"))
        print("Thread %s  Branch %s [%s]  pid=%s  seit %s  %s\n    %s" % (
            thread_name, anspruch.get("branch") or "?",
            "hier" if anspruch.get("branch") == hier else "fremd",
            anspruch.get("pid"), anspruch.get("zeit") or "?",
            "aktiv" if lebt else "VERWAIST",
            "\n    ".join(anspruch.get("muster") or ["(keine Muster)"])))
    return 0


class Lauf:
    """Ein Gate mit seiner gemessenen Dauer - Zahlen statt Eindruecke."""

    def __init__(self):
        self.ergebnisse = []

    def fahre(self, name, befehl, *, shell_cmd=False):
        print("  ... %s" % name, flush=True)
        start = time.time()
        if shell_cmd:
            fertig = subprocess.run(["cmd", "/c", befehl], cwd=WURZEL,
                                    capture_output=True, text=True,
                                    encoding="utf-8", errors="replace",
                                    env=saubere_umgebung())
        else:
            fertig = subprocess.run(befehl, cwd=WURZEL, capture_output=True,
                                    text=True, encoding="utf-8", errors="replace",
                                    env=saubere_umgebung())
        dauer = time.time() - start
        ok = fertig.returncode == 0
        self.ergebnisse.append((name, ok, dauer, fertig))
        print("      %s  %.0f s" % ("gruen" if ok else "ROT  ", dauer), flush=True)
        return ok

    def ueberspringe(self, name, grund):
        self.ergebnisse.append((name, None, 0.0, None))
        print("  ... %s\n      uebersprungen: %s" % (name, grund), flush=True)

    def fahre_gate(self, name, ok, dauer, meldung):
        """Ein Gate OHNE Unterprozess - Besitz und Zeitstempel werden so
        protokolliert, damit der Bericht sie wie alle anderen mitzaehlt."""
        self.ergebnisse.append((name, ok, dauer, None))
        print("  ... %s\n      %s  %.0f s" % (
            name, "gruen" if ok else "ROT  ", dauer), flush=True)
        if meldung:
            for zeile in str(meldung).splitlines():
                print("      %s" % zeile, flush=True)

    def bericht(self):
        rot = [e for e in self.ergebnisse if e[1] is False]
        gesamt = sum(e[2] for e in self.ergebnisse)
        print("\n  %d Gate(s) in %.0f s." % (len(self.ergebnisse), gesamt))
        for name, ok, _, fertig in rot:
            print("\nROT: %s" % name)
            # Ein Gate OHNE Subprozess (Besitz, Zeitstempel) hat nichts
            # nachzuschreiben - es hat seine Zeilen schon bei fahre_gate
            # gedruckt. GEMESSEN am 27.09.2026: hier stuerzte der Bericht ab
            # und der Hook endete mit einer Traceback statt einer Ablehnung.
            if fertig is None:
                continue
            text = ((fertig.stdout or "") + (fertig.stderr or "")).strip().splitlines()
            for zeile in text[-15:]:
                print("     " + zeile[:140])
        return len(rot)


def gate0_befehl(dateien):
    """Die Befehlszeile fuer Gate 0 - mit den vorgemerkten Dateien.

    WARUM UEBERGEBEN STATT FRAGEN LASSEN: Gate 0 startet mit
    saubere_umgebung(), also OHNE GIT_*. Das muss so bleiben - ein
    Unterprozess wuerde sonst in den Index des laufenden Commits schreiben
    (gemessen am 21.09.2026). Ohne GIT_INDEX_FILE sieht ein eigener
    `git diff --cached` aber den ECHTEN Index, und der ist bei
    `git commit --only` leer - genau der Weg, den ausliefern.py benutzt.
    Neu vorgemerkte Dateien entgingen dem Gate damit vollstaendig.

    gestagte_dateien() liest die Liste absichtlich MIT der Umgebung und ist
    darum richtig. Sie wird hier als Argument weitergereicht: die
    Abdichtung bleibt wirksam, die Liste stimmt trotzdem.
    """
    befehl = [sys.executable, os.path.join(TOOLS, "pruefe_engine.py")]
    if dateien:
        befehl.append("--dateien")
        befehl.extend(dateien)
    return befehl


def platten_hinweis(grenze=None):
    """Plattenplatz melden - als HINWEIS, niemals als Gate.

    WARUM KEIN GATE: Eine volle Platte ist kein Fehler am Commit. Ein Hook,
    der Commits verweigert, weil der Rechner voll ist, wird binnen eines Tages
    mit --no-verify umgangen - und dann prueft er gar nichts mehr. Der
    Waechter meldet, damit niemand erst am abgebrochenen Cook davon erfaehrt.

    DARUM AUCH NICHT UEBER DEN `Lauf`: die Gate-Buchhaltung zaehlt Gates, und
    ein Hinweis, der sich als Gate eintraegt, erzaehlt einem das Falsche -
    mitten in der Zusammenfassung, wo niemand nachfragt.

    IM GESUNDEN FALL KOSTET DAS NICHTS: `shutil.disk_usage` ist ein Syscall.
    Erst unter der Grenze wird ueberhaupt gemessen (und das Ergebnis 30
    Minuten gecacht), weil die grossen Cache-Ordner zweistellige Sekunden
    brauchen - ein Hook, der das vor JEDEM Commit tut, wird abgeschaltet.
    """
    # DER IMPORT LIEGT IM try. GEMESSEN am 27.09.2026: er stand ausserhalb
    # und damit ausserhalb des Schutzes. Das Push-Gate faehrt in einem
    # eigenen Worktree aus `git worktree add` - dort existieren nur
    # COMMITTEte Dateien. Ein neues, noch nicht committetes
    # Tools/platten_waechter.py fehlt dort, der Import scheitert, und der
    # Commit-Hook stirbt an einem Werkzeug, das nur melden sollte.
    start = time.time()
    try:
        import platten_waechter
        text = platten_waechter.warnung(grenze=grenze) if grenze \
            else platten_waechter.warnung()
    except Exception as e:  # ein Waechter darf den Commit nie verhindern
        text = "Pruefung nicht ausgefuehrt (%s)" % e
    dauer = time.time() - start
    print("  ... Plattenplatz (Hinweis, kein Gate)  %.0f s" % dauer, flush=True)
    if text:
        for zeile in str(text).splitlines():
            print("      %s" % zeile[:200], flush=True)


def gates_fahren(stufe, dateien, thread=None):
    lauf = Lauf()
    print("Gates vor dem Commit (Stufe: %s)" % stufe)

    # Gate B (Besitz) zuerst und VOR dem Engine-Lock: es kostet nichts, und
    # ein Compilerlauf fuer einen Commit, der nicht stattfinden darf, waere
    # Verschwendung. Sobald es rot ist, endet der Lauf hier.
    #
    # DER THREAD-Parameter wird DURCHGEREICHT, nicht neu ermittelt.
    # GEMESSEN am 27.09.2026: `--thread` wurde geparst, aber nicht
    # weitergegeben - besitz_gate ermittelte den Namen selbst und landete
    # beim Branchnamen. Folge: der Thread, dem die Arbeit gehoert, wurde vom
    # eigenen Gate abgewiesen, und der Besitz war unbrauchbar, weil niemand
    # seine eigene Arbeit committen konnte.
    if besitz_gate(dateien, lauf, thread=thread):
        lauf.bericht()
        return 1

    # Plattenplatz: HINWEIS, kein Gate - er sperrt nie einen Commit. Sitzt vor
    # dem Engine-Lock, weil er nichts startet und nichts beansprucht.
    platten_hinweis()

    # Die volle Stufe startet Editoren und beendet sie (Gate 2+3) - der
    # Engine-Lock muss sie ab Gate 0 umschliessen, nicht erst ab dem Aufruf
    # von build_release (Gate 1 kompiliert sonst ungeschuetzt unter einem
    # fremden Lauf). Im Push-Worktree haelt ihn schon der Hook; dann ist er
    # hier "eigen" und kostet nur die Abfrage. Die schnelle Stufe startet
    # keinen Editor und wartet deshalb auf niemanden.
    if stufe == "voll":
        import gate_worktree
        if not gate_worktree.motor_sperre("vor_dem_commit"):
            print("\nEngine-Lock belegt - Gates nicht gefahren.")
            return 1

    lauf.fahre("Gate 0  Engine-Pfade", gate0_befehl(dateien))

    # Die Python-Suiten gehoeren zur vollen Stufe, nicht vor jeden Commit.
    #
    # GEMESSEN am 21.09.2026: sie sind 31 s von 32 s der schnellen Stufe.
    # Alles andere dort kostet zusammen eine Sekunde. Sie sind ausserdem
    # KEIN Gate der Release-Pipeline - build_release.ps1 faehrt Gate 0 bis 3
    # und ruft sie nirgends auf; vor dem Commit standen sie als Zugabe.
    # Darum laufen sie jetzt dort, wo die langsamen Gates schon liegen.
    #
    # Sie laufen weiter, bevor etwas den Rechner verlaesst: der pre-push-Hook
    # faehrt die volle Stufe. Verschoben, nicht gestrichen.
    if stufe == "voll":
        lauf.fahre("Python-Suiten",
                   [sys.executable, "-m", "unittest", "discover",
                    "-s", "Tools", "-p", "test_*.py"])
    else:
        lauf.ueberspringe("Python-Suiten",
                          "Stufe schnell - sie laufen vor dem Push")

    if braucht_compiler(dateien):
        lauf.fahre("Gate 1  Kompilieren", r"Tools\build_gate1.cmd", shell_cmd=True)
    else:
        lauf.ueberspringe("Gate 1  Kompilieren", "keine C++-Datei betroffen")

    if stufe == "voll":
        lauf.fahre("Gate 2+3  Tests und Rauchtest",
                   r"Tools\build_release.cmd -GatesOnly", shell_cmd=True)
    else:
        lauf.ueberspringe("Gate 2+3  Tests und Rauchtest",
                          "Stufe schnell - sie laufen vor dem Push")

    # Gate 4: der Plasmacutter als BEWEIS, nicht als Zahlenbehauptung. Die
    # Unit-Tests koennen nur sagen, dass die Rechnung stimmt; hier steht
    # die gluehende Kante im Bild, und das Gate misst die Pixel selbst.
    #
    # IMMER in der vollen Stufe, OHNE Dateifilter. Es gab zuerst eine
    # Musterliste der Plasmacutter-Dateien - und damit zwei Fehlerquellen:
    # eine vergessene Datei laesst das Gate stillschweigend ausfallen, und
    # das, was das Bild zerstoert, ist nicht immer eine Plasmacutter-Datei
    # (Licht, Material, Post-Process, Kamera). Der Filter sparte 50 s und
    # kostete genau das, wofuer das Gate da ist.
    #
    # Der Lauf startet einen Editor, also gehoert er hinter Gate 2+3 und
    # in dieselbe Stufe - den Engine-Lock haelt vor_dem_commit von Gate 0 an,
    # der Lauf muss ihn nicht selbst beanspruchen.
    if stufe == "voll":
        lauf.fahre("Gate 4  Plasmacutter-Bildfolge",
                   r"Tools\verify_cuttable.cmd", shell_cmd=True)
    else:
        lauf.ueberspringe("Gate 4  Plasmacutter-Bildfolge",
                          "Stufe schnell - sie laeuft vor dem Push")

    # Gate 5: der gespeicherte ANKERZUSTAND der gebackenen Karte. Die
    # Verankerung ist die Voraussetzung dafuer, dass eine leere Komponente
    # nicht am Kartenursprung landet - und sie entsteht in einem Bake, nicht
    # im Code. Ein Push, der den Zustand nicht liest, kann ihn zerstoeren,
    # ohne dass jemand etwas bemerkt: die Datei ist dann nicht kaputt,
    # nur falsch verankert, und der Bruch faellt erst beim Laden auf.
    #
    # Das Skript endet ungleich null, wenn nicht 0 leere Komponenten am
    # Kartenursprung liegen (Exit 7) - und mit eigenen Codes, wenn ueberhaupt
    # keine Messung zustande kam (2/3/4/5/6). Damit ist "nichts gemessen"
    # nicht mit "alles in Ordnung" verwechselbar. Genau an dieser Verwechslung
    # ist der Alkis24-Stand wochenlang als Messung durchgegangen: die Datei
    # war da, die Zahl war falsch, und der Lauf meldete Erfolg.
    #
    # Startet einen Editor, also hinter Gate 4 in dieselbe Stufe; den
    # Engine-Lock haelt vor_dem_commit von Gate 0 an.
    if stufe == "voll":
        lauf.fahre("Gate 5  Ankerzustand (WP)",
                   r"Tools\verify_anchor.cmd", shell_cmd=True)
    else:
        lauf.ueberspringe("Gate 5  Ankerzustand (WP)",
                          "Stufe schnell - sie laeuft vor dem Push")

    return lauf.bericht()


def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(description="Release-Gates vor dem Commit fahren.")
    p.add_argument("--stufe", choices=("schnell", "voll"), default="schnell")
    p.add_argument("--gestaged", action="store_true",
                   help="nur vorgemerkte Dateien betrachten (fuer den Hook)")
    p.add_argument("--push-refs", action="store_true",
                   help="pre-push: Commits von stdin lesen und im sauberen Worktree pruefen")
    p.add_argument("--thread", help="Name dieses Threads (sonst WB_THREAD/git config)")
    p.add_argument("--besitz-zeigen", action="store_true",
                   help="alle Ansprueche auflisten")
    p.add_argument("--besitz-ansprechen", nargs="*", metavar="MUSTER",
                   help="Dateimuster fuer den aktuellen Branch beanspruchen")
    p.add_argument("--besitz-freigeben", action="store_true",
                   help="Anspruch dieses Threads zurueckgeben")
    a = p.parse_args(argv)

    # Verwaltung: die Besitz-Kommandos sind keine Gates und laufen darum
    # auch dann, wenn WB_KEINE_GATES gesetzt ist.
    if a.besitz_zeigen:
        return besitz_zeigen()
    if a.besitz_freigeben:
        return besitz_freigeben(thread=a.thread)
    if a.besitz_ansprechen is not None:
        return besitz_ansprechen(a.besitz_ansprechen, thread=a.thread)

    if os.environ.get("WB_KEINE_GATES") == "1":
        print("WB_KEINE_GATES=1 - Gates uebersprungen.")
        return 0

    # Push: NICHT den Arbeitsbaum pruefen - dort liegt fremde laufende Arbeit.
    # Die Commits, die hinausgehen, kommen vom Hook auf stdin und werden in
    # einem eigenen Worktree gebaut, getestet und geraucht.
    if a.push_refs:
        # Im Push-Worktree wird NICHT der Besitz geprueft: dort ist der Baum
        # frisch gebaut und die Commits sind schon geschrieben. Der Besitz
        # gehoert an den Commit, nicht an den Push - sonst koennte niemand
        # mehr ausliefern, nachdem ein fremder Thread einmal unrein war.
        import gate_worktree
        return gate_worktree.push_pruefen(WURZEL, sys.stdin.read())

    # WELCHE Dateien beurteilt werden, haengt an der Stufe - nicht am Zufall
    # des Arbeitsbaums.
    #
    # Die volle Stufe ist die PUSH-Stufe: dort geht ein Commit-BEREICH hinaus,
    # und der Baum ist in dem Moment typischerweise sauber. Ihn zu fragen
    # hiess, nichts zu finden und Gate 1 zu ueberspringen - gemessen mit 20
    # C++-Dateien im Bereich und null im Baum.
    if a.stufe == "voll":
        dateien = zu_pushende_dateien()
        if dateien is None:
            print("Kein Upstream - der Push-Bereich ist unbestimmbar, "
                  "es wird nichts uebersprungen.")
    elif a.gestaged:
        dateien = gestagte_dateien()
        if not dateien:
            print("Nichts vorgemerkt - nichts zu pruefen.")
            return 0
    else:
        dateien = geaenderte_dateien()

    rot = gates_fahren(a.stufe, dateien, thread=a.thread)
    if rot:
        print("\n%d Gate(s) ROT - der Commit wird abgewiesen." % rot)
        print("Gehoert die Arbeit wirklich dir, beanspruche sie zuerst:")
        print("  python Tools/vor_dem_commit.py --besitz-ansprechen <Muster>")
        print("Fremde Arbeit gehoert dem fremden Thread:")
        print("  git commit -- <nur eigene Dateien>")
        print("Wenn das so gewollt ist: git commit --no-verify")
        return 1
    print("Alle Gates gruen.")
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
