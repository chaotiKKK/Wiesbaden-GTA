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
    Gate 4  Plasmacutter-Bild   1 min  (startet den Unreal-Editor)
    Gate 5  Ankerzustand (WP)   3 min  (startet den Unreal-Editor)

Ein Hook, der vor JEDEM Commit eine Viertelstunde braucht, wird binnen eines
Tages mit --no-verify umgangen; dann prueft er gar nichts mehr. Darum:

* **schnell** laeuft vor jedem Commit und enthaelt nur, was zur
  Release-Pipeline gehoert: Gate 0 immer, Gate 1 nur, wenn wirklich C++
  dabei ist - wer nur ein Python-Werkzeug aendert, wartet nicht auf einen
  Compiler.
* **voll** laeuft vor dem PUSH. Dort ist die Wartezeit vertretbar, und nichts
  verlaesst den Rechner ungeprueft. Die Blockade wandert damit vom
  Paketieren an die Stelle, an der sie noch billig ist.

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
import os
import re
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


def uebersprungen_aus(fertig):
    """Wie viele Tests haben die Suiten uebersprungen? (Anzahl, Gesamtzahl)

    unittest schreibt seine Zusammenfassung nach STDERR:

        Ran 313 tests in 108.479s
        OK (skipped=13)

    Wofuer das ueberhaupt: EIN EXIT-CODE 0 heisst "kein Test ist
    fehlgeschlagen" - er sagt nichts darueber, wie viele ueberhaupt gelaufen
    sind. Ein uebersprungener Test ist aber genau der, der nichts geprueft
    hat. GEMESSEN am 27.09.2026: sechs Gates gruen, davon sechs Tests, die
    uebersprungen waren (test_verify_cuttable_gate braucht Belege, die Gate 4
    erst danach erzeugt). Ohne diese Zahl sieht das Ergebnis vollstaendig
    aus.

    None heisst: die Zusammenfassung war nicht lesbar. Das ist bewusst NICHT
    "0 uebersprungen" - eine nicht gelesene Zahl als Null zu melden waere
    genau die Art Ampel ohne Lampe, gegen die diese Gates gebaut sind.
    """
    text = (getattr(fertig, "stderr", "") or "") + (getattr(fertig, "stdout", "") or "")
    gesamt = re.search(r"Ran (\d+) tests?", text)
    if not gesamt:
        return None
    ueber = re.search(r"skipped=(\d+)", text)
    return (int(ueber.group(1)) if ueber else 0), int(gesamt.group(1))


class Lauf:
    """Ein Gate mit seiner gemessenen Dauer - Zahlen statt Eindruecke."""

    def __init__(self):
        self.ergebnisse = []
        self.notizen = []

    def fahre(self, name, befehl, *, shell_cmd=False, notiz=None):
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
        print("      %s  %.0f s" % ("gruen" if ok else "ROT  ", dauer), end="",
              flush=True)
        if notiz is not None:
            zusatz = notiz(fertig)
            if zusatz:
                self.notizen.append((name, zusatz))
                print("  (%s)" % zusatz, flush=True)
                return ok
        print("", flush=True)
        return ok

    def ueberspringe(self, name, grund):
        self.ergebnisse.append((name, None, 0.0, None))
        print("  ... %s\n      uebersprungen: %s" % (name, grund), flush=True)

    def bericht(self):
        rot = [e for e in self.ergebnisse if e[1] is False]
        gesamt = sum(e[2] for e in self.ergebnisse)
        print("\n  %d Gate(s) in %.0f s." % (len(self.ergebnisse), gesamt))
        for name, zusatz in self.notizen:
            print("  %s: %s" % (name, zusatz))
        for name, ok, _, fertig in rot:
            print("\nROT: %s" % name)
            text = ((fertig.stdout or "") + (fertig.stderr or "")).strip().splitlines()
            for zeile in text[-15:]:
                print("     " + zeile[:140])
        return len(rot)


def anker_gate_fahren(lauf, ziel=None):
    """Gate 5 mit BEWEIS statt Exit-Code.

    GEMESSEN am 27.09.2026: der Schritt meldete in einem echten Push-Lauf
    nach 10 Sekunden "gruen", ohne ein Log und ohne Ergebnisdatei zu
    hinterlassen - es wurde gar nicht gemessen. Ein Gate, das nur auf den
    Exit-Code schaut, ist genau die Ampel ohne Lampe, die Gate 5 verhindern
    soll.

    Der Beweis ist der Zeitstempel der Ergebnisdatei: sie muss aus diesem
    Lauf stammen, nicht von gestern. Bewusst NICHT "Datei vorher loeschen" -
    das wuerde bei jedem Testlauf eine echte Messung im Arbeitsbaum
    wegraeumen, und ein Test, der nebenbei Dateien loescht, ist kein guter
    Test. Das Skript selbst loescht sein Ergebnis ohnehin vor dem Messen.

    `ziel` ist nur fuer die Tests da; im Betrieb ist es die Ergebnisdatei
    der Standardkarte im Projektwurzelverzeichnis.
    """
    if ziel is None:
        ziel = os.path.join(WURZEL, "Saved", "Diagnose", "anchor_verify.txt")

    start = time.time()
    ok = lauf.fahre("Gate 5  Ankerzustand (WP)",
                    r"Tools\verify_anchor.cmd", shell_cmd=True)

    # Eine Sekunde Toleranz: die Dateisysteme runden die Zeitstempel je
    # nach Plattform, und eine Messung, die im selben Lauf endet, darf
    # nicht daran scheitern, dass ihr Zeitstempel eine Hauchsekunde
    # aelter ist als der Laufbeginn.
    neu = False
    try:
        neu = os.path.getmtime(ziel) >= start - 1.0
    except OSError:
        neu = False
    if not neu:
        print("      ROT   kein neues Ergebnis unter %s - der Lauf hat "
              "nicht gemessen." % ziel, flush=True)
        # fahre() hat den Schritt schon als gruen vermerkt; der Beweis
        # entscheidet, also wird der Eintrag auf rot gezogen. Die
        # Testdoppel kennen `ergebnisse` nicht - dann gibt es nur den
        # Rueckgabewert, und die Stufenzuordnung bleibt unberuehrt.
        ergebnisse = getattr(lauf, "ergebnisse", None)
        if isinstance(ergebnisse, list) and ergebnisse:
            name, _ok, dauer, fertig = ergebnisse[-1]
            if name.startswith("Gate 5"):
                ergebnisse[-1] = (name, False, dauer, fertig)
        return False
    return ok


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


def suiten_notiz(fertig):
    """Der Zusatz hinter dem Schritt Python-Suiten: wie viele haben gar nichts
    geprueft?"""
    z = uebersprungen_aus(fertig)
    if z is None:
        return ("Zusammenfassung der Suites nicht lesbar - wie viele Tests "
                "uebersprungen wurden, weiss dieser Lauf nicht")
    ueber, gesamt = z
    if ueber == 0:
        return "alle %d Tests gelaufen" % gesamt
    return "%d von %d Tests UEBERSPRUNGEN (sie haben nichts geprueft)" % (ueber, gesamt)


def gates_fahren(stufe, dateien):
    lauf = Lauf()
    print("Gates vor dem Commit (Stufe: %s)" % stufe)

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
                    "-s", "Tools", "-p", "test_*.py"], notiz=suiten_notiz)
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
    # Startet einen Editor, also in derselben Stufe wie die anderen Editor-
    # Laeufe; den Engine-Lock haelt vor_dem_commit von Gate 0 an.
    if stufe == "voll":
        anker_gate_fahren(lauf)
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
    a = p.parse_args(argv)

    if os.environ.get("WB_KEINE_GATES") == "1":
        print("WB_KEINE_GATES=1 - Gates uebersprungen.")
        return 0

    # Push: NICHT den Arbeitsbaum pruefen - dort liegt fremde laufende Arbeit.
    # Die Commits, die hinausgehen, kommen vom Hook auf stdin und werden in
    # einem eigenen Worktree gebaut, getestet und geraucht.
    if a.push_refs:
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

    rot = gates_fahren(a.stufe, dateien)
    if rot:
        print("\n%d Gate(s) ROT - der Commit wird abgewiesen." % rot)
        print("Wenn das so gewollt ist: git commit --no-verify")
        return 1
    print("Alle Gates gruen.")
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
