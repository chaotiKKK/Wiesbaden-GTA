r"""Selbsttest: der Engine-Lock darf einen toten TREIBER nicht mit einem leeren
PROZESSBAUM verwechseln (Tools/engine_run_lock.ps1, Zustand NACHKOMMEN).

GEMESSEN am 28.09.2026 um 03:36: die Sperrdatei gehoerte zur toten PID 38032
(Label push_gate), waehrend im Hauptbaum dotnet.exe und vier cl.exe
kompilierten. Die Sperre meldete "verwaist", der naechste Lauf uebernahm nach
Protokoll - und dann liefen zwei Builds in einem Verzeichnis. Der Link starb an
LNK1104, weil der andere Prozess unsere Modul-DLL offenhielt. Dieselbe
Verwechslung laesst cleanup_unreal_processes.ps1 (Exit 0 = darf beenden) einen
noch arbeitenden Editor als Rest erschlagen.

Warum eine eigene Datei und nicht in test_gate_worktree.py: dort liegen die
bestehenden Lock-Tests, aber diese Datei hat gerade fremde Aenderungen in
Arbeit. Ein eigener Fall gehoert in eine eigene Datei, die niemand sonst
anfasst.

ZWEITER FALL IN DIESER DATEI (28.09.2026): die beiden Fristen des Skripts.
`-Modus Start` wartet seit heute auf einen BELEGTEN Lock (-StartWarteSekunden,
Vorgabe 1800 s), `-Modus Nehmen` nicht (Vorgabe `-WarteSekunden 0`), weil
`motor_sperre` in gate_worktree.py seine EIGENE Frist mitbringt und sich genau
auf diese Null verlaesst. Beide Zahlen sind Vertrag - die Gegenproben unten
sichern sie einzeln ab.

DRITTER FALL (28.09.2026, 20:21:53): die UEBERNAHME einer verwaisten Sperre.
Sie lief in zwei Schritten (erst `Remove-Item`, dann `CreateNew`), und seit die
Wrapper auf einen belegten Lock warten, stehen mehrere Uebernehmer gleichzeitig
an derselben Datei. GEMESSEN: mein Commandlet-Lauf und eine fremde
Bugtank-Pruefung uebernahmen im selben Moment; der Zweite raeumte die frische
Sperre des Ersten weg, die Sperrdatei trug danach den fremden Besitzer, und
MEIN Lauf baute trotzdem das Editor-Target neu - neben einem fremden
Editorlauf. Die Uebernahme raeumt die alte Datei jetzt per `Move-Item` weg
(atomar); die Tests unten fahren eine Rennrunde aus vier Wartenden und die
Sabotage-Kopie mit dem alten Zweischritt.

VIERTER FALL (30.09.2026): die VORREINIGUNG der reproduzierbaren Caches. Unter
der Meldegrenze von 20 % raeumt der Start sie, BEVOR er den Lock nimmt - die
dritte Antwort des Platten-Gates neben Warten und Abbrechen. Die Faelle unten
messen Argumente und Reihenfolge mit einem ERSATZ-Waechter (er loescht nichts)
und sichern den Attrappen-Zweig ab: eine erfundene Plattenzahl loescht nie die
echte Platte - ohne ihn raeumte die Suite selbst die Caches dieses Rechners.

    python -m unittest discover -s Tools -p "test_engine_run_lock.py"
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
LOCK = WURZEL / "Tools" / "engine_run_lock.ps1"
CLEANUP = WURZEL / "Tools" / "cleanup_unreal_processes.ps1"

# Hilfsprozess: schreibt die Sperrdatei mit SEINER eigenen PID, startet
# wahlweise einen abgekoppelten Schläfer und endet dann. Danach ist der
# Besitzer tot und der Schläfer lebt weiter - genau der gemessene Zustand.
# OwnerStart bleibt leer: dann prueft das Skript die Startzeit nicht, und der
# tote Besitzer ist eindeutig zuzuordnen.
HELFER = r"""
import os, subprocess, sys
lock, mitKind, dauer = sys.argv[1], sys.argv[2] == "kind", float(sys.argv[3])
kind = None
if mitKind:
    kind = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(%r)" % dauer],
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
with open(lock, "w", encoding="ascii") as fh:
    fh.write("LockVersion=1\nOwnerPid=%d\nOwnerName=python\nOwnerStart=\n"
             "Label=push_gate\nTakenAt=2026-09-28 03:22:35\nHost=TEST\n" % os.getpid())
if kind:
    with open(lock + ".kind", "w", encoding="ascii") as fh:
        fh.write(str(kind.pid))
"""


# Process-Kill und Lebendpruefung ueber taskkill/tasklist statt PowerShell:
# eine PowerShell-Sitzung kostet auf dieser Maschine rund zwei Sekunden
# Startzeit, und dieser Test toetet mehrere Prozesse. Gemessen: damit faellt
# die Suite von 77 s auf wenige Sekunden - wichtig, weil sie im vollen Gate
# mitlaeuft.
# GEMESSEN auf dieser Maschine: `tasklist` schreibt ein Byte (0x81), das cp1252
# nicht dekodieren kann. Ohne encoding= stirbt der Reader-Thread des subprocess
# mit UnicodeDecodeError, und `.stdout` ist danach None - nicht leer, sondern
# None. Eine Lebendpruefung ueber tasklist ist damit unbrauchbar, solange sie
# nicht ausdruecklich dekodiert. taskkill /F ist synchron und meldet im
# Exit-Code (128 = nicht gefunden), deshalb wird der Erfolg daran gemessen und
# gar nicht erst nachgesehen.
def beenden(pid):
    fertig = subprocess.run(["taskkill", "/F", "/PID", str(pid)],
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace", timeout=60)
    return fertig.returncode == 0


def ps(skript, *args, timeout=120):
    return subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(skript)] + list(args),
        capture_output=True, text=True, timeout=timeout, encoding="utf-8", errors="replace")


def felder(pfad):
    text = Path(pfad).read_text(encoding="utf-8")
    return dict(z.split("=", 1) for z in text.splitlines() if "=" in z)


class NachkommenTest(unittest.TestCase):
    """Besitzer tot, Kinder arbeiten: belegt - nicht verwaist."""

    def sperre_bauen(self, tmp, mit_kind, dauer=120.0):
        """Sperrdatei mit totem Besitzer bauen; Rueckgabe (pfad, kind_pid)."""
        pfad = str(Path(tmp) / "engine_run.lock")
        lauf = subprocess.run(
            [sys.executable, "-c", HELFER, pfad, "kind" if mit_kind else "ohne", str(dauer)],
            capture_output=True, text=True, timeout=120)
        self.assertEqual(lauf.returncode, 0, lauf.stdout + lauf.stderr)
        kind_pid = None
        if mit_kind:
            self.assertTrue(Path(pfad + ".kind").exists(), "Hilfsprozess hat keine Kind-PID geschrieben")
            kind_pid = int(Path(pfad + ".kind").read_text(encoding="ascii").strip())
        return pfad, kind_pid

    def kind_beenden(self, kind_pid):
        beenden(kind_pid)
        # Kurz warten: taskkill /F ist synchron, der Prozess verschwindet aber
        # nicht im selben Augenblick aus der Prozesstabelle.
        time.sleep(0.5)

    def test_toter_besitzer_mit_lebendem_kind_ist_belegt(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad, kind_pid = self.sperre_bauen(tmp, mit_kind=True)
            try:
                status = ps(LOCK, "-Modus", "Status", "-LockPfad", pfad)
                ausgabe = status.stdout + status.stderr
                self.assertEqual(status.returncode, 3, ausgabe)
                self.assertIn("BELEGT", ausgabe)
                self.assertIn("Nachkommen", ausgabe)
                # Der Kern: NICHT verwaist. Sonst wuerde uebernommen - und die
                # Prozessbereinigung beendet bei Exit 0 den arbeitenden Editor.
                self.assertNotIn("verwaist", ausgabe)
                # Und der Bediener sieht, WER noch arbeitet.
                self.assertIn(str(kind_pid), ausgabe)

                # -PlattenGrenze 0: dieser Test prueft den Sperrzustand, nicht
                # den Plattenplatz. Ohne den Schalter haenge sein Ergebnis daran,
                # wie voll die Platte des Rechners gerade ist.
                nehmen = ps(LOCK, "-Modus", "Nehmen", "-LockPfad", pfad, "-PlattenGrenze", "0",
                            "-Name", "unittest")
                self.assertEqual(nehmen.returncode, 3, nehmen.stdout + nehmen.stderr)
                # Die Sperrdatei bleibt unangetastet: kein Uebernehmen.
                self.assertEqual(felder(pfad)["Label"], "push_gate")
                self.assertNotEqual(int(felder(pfad)["OwnerPid"]), os.getpid())
            finally:
                self.kind_beenden(kind_pid)

            # Selbstheilung: der Baum ist leer, jetzt ist die Sperre verwaist
            # und wird uebernommen - wie vorher.
            status2 = ps(LOCK, "-Modus", "Status", "-LockPfad", pfad)
            self.assertIn("verwaist", status2.stdout, status2.stdout + status2.stderr)
            self.assertEqual(status2.returncode, 0, status2.stdout + status2.stderr)

            nehmen2 = ps(LOCK, "-Modus", "Nehmen", "-LockPfad", pfad, "-PlattenGrenze", "0",
                         "-Name", "unittest")
            self.assertEqual(nehmen2.returncode, 0, nehmen2.stdout + nehmen2.stderr)
            self.assertIn("verwaist", nehmen2.stdout)
            self.assertEqual(felder(pfad)["Label"], "unittest")
            self.assertEqual(int(felder(pfad)["OwnerPid"]), os.getpid())

    def test_toter_besitzer_ohne_kinder_bleibt_verwaist(self):
        """Gegenprobe gegen eine Ueberkorrektur: ohne Nachkommen muss die
        Uebernahme erhalten bleiben, sonst blockiert eine tote Sperre fuer
        immer und der naechste Lauf versackt daran."""
        with tempfile.TemporaryDirectory() as tmp:
            pfad, _ = self.sperre_bauen(tmp, mit_kind=False)
            status = ps(LOCK, "-Modus", "Status", "-LockPfad", pfad)
            self.assertEqual(status.returncode, 0, status.stdout + status.stderr)
            self.assertIn("verwaist", status.stdout)
            self.assertNotIn("Nachkommen", status.stdout)

            nehmen = ps(LOCK, "-Modus", "Nehmen", "-LockPfad", pfad, "-PlattenGrenze", "0",
                        "-Name", "unittest")
            self.assertEqual(nehmen.returncode, 0, nehmen.stdout + nehmen.stderr)
            self.assertEqual(felder(pfad)["Label"], "unittest")

    def test_sabotage_ohne_nachkommen_abfrage_meldet_falsch_verwaist(self):
        """Eine Pruefung, die bei falschem Code gruen bleibt, misst nichts.

        Hier wird eine Kopie des Skripts gefahren, in der die Nachkommen-Abfrage
        abgeschaltet ist. Genau dann MUSS der gemessene Fall als verwaist
        erscheinen - sonst haengt das Ergebnis oben nicht an der neuen Regel.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad, kind_pid = self.sperre_bauen(tmp, mit_kind=True)
            try:
                text = LOCK.read_text(encoding="utf-8")
                alt = "$nach = @(Get-Nachkommen ([int]$Lock.Pid) 16 $ErstAb)"
                self.assertIn(alt, text, "Anker fuer die Sabotage nicht gefunden")
                kopie = Path(tmp) / "engine_run_lock_sabotage.ps1"
                kopie.write_text(text.replace(alt, "$nach = @()"), encoding="utf-8")

                lauf = ps(kopie, "-Modus", "Status", "-LockPfad", pfad)
                ausgabe = lauf.stdout + lauf.stderr
                self.assertEqual(lauf.returncode, 0, ausgabe)
                self.assertIn("verwaist", ausgabe)
                self.assertNotIn("Nachkommen", ausgabe)
            finally:
                self.kind_beenden(kind_pid)

    def test_cleanup_beendet_bei_nachkommen_nichts(self):
        """Der eigentliche Schaden waere hier: cleanup_unreal_processes.ps1
        beendet alles, was UnrealEditor heisst, sobald der Lock Status 0 meldet.
        Mit -DryRun wird nichts beendet - geprueft wird, dass der Lauf gar nicht
        erst bis dahin kommt, sondern am Lock abbricht."""
        with tempfile.TemporaryDirectory() as tmp:
            pfad, kind_pid = self.sperre_bauen(tmp, mit_kind=True)
            try:
                lauf = subprocess.run(
                    ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(CLEANUP),
                     "-DryRun", "-LockPfad", pfad],
                    capture_output=True, text=True, timeout=120, encoding="utf-8", errors="replace")
                ausgabe = lauf.stdout + lauf.stderr
                self.assertNotEqual(lauf.returncode, 0,
                                    "cleanup lief trotz Nachkommen durch: " + ausgabe)
                self.assertIn("Lock", ausgabe)
                # Der Abbruch kommt vom Lock, nicht von einem Fehler im Skript.
                self.assertNotIn("Prozessbereinigung: UnrealEditor/zenserver-Reste werden beendet", ausgabe)
            finally:
                self.kind_beenden(kind_pid)


# Elternprozess fuer den Recycle-Fall: startet ein Kind, schreibt dessen PID
# neben die Sperrdatei und lebt selbst weiter. Damit hat die PID einen LEBBENDEN
# Inhaber mit einem Kind - genau die Lage, in der eine neu vergebene PID wie
# eine fremde Sitzung mit Nachkommen aussah.
NEUER_INHABER = r"""
import os, subprocess, sys, time
lock, dauer = sys.argv[1], float(sys.argv[2])
kind = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(%r)" % dauer],
                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
with open(lock + ".neu", "w", encoding="ascii") as fh:
    fh.write(str(kind.pid))
time.sleep(dauer)
"""


class RecycleTest(unittest.TestCase):
    """Eine neu vergebene PID darf keine fremde Sitzung vortaeuschen.

    Der Anlass: OwnerPid allein ist eine Zahl. Ist sie nach dem Tod des Besitzers
    an einen anderen Prozess gegangen und hat der Kinder, sah die Sperre wie eine
    lebende fremde Sitzung aus (Zustand Nachkommen) und blockierte auf ein
    Phantom. Der Recycle-Schnitt in Get-Nachkommen loest das: Kinder, die nach
    dem Start des neuen Inhabers entstanden sind, koennen nicht von unserem toten
    Besitzer stammen.
    """

    def inhaber_starten(self, tmp, dauer=120.0):
        pfad = str(Path(tmp) / "engine_run.lock")
        prozess = subprocess.Popen(
            [sys.executable, "-c", NEUER_INHABER, pfad, str(dauer)],
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        for _ in range(60):
            if Path(pfad + ".neu").exists():
                break
            time.sleep(0.25)
        self.assertTrue(Path(pfad + ".neu").exists(), "Kind-PID wurde nicht geschrieben")
        kind_pid = int(Path(pfad + ".neu").read_text(encoding="ascii").strip())
        return pfad, prozess, kind_pid

    def alle_beenden(self, prozess, kind_pid):
        for pid in (kind_pid, prozess.pid):
            beenden(pid)
        try:
            prozess.wait(timeout=30)
        except subprocess.TimeoutExpired:
            pass

    def sperrdatei_mit_alter_startzeit(self, pfad, pid):
        """Sperrdatei auf eine PID, deren Startzeit NICHT die eingetragene ist -
        damit ist die Neubelegung der PID bewiesen, nicht vermutet."""
        Path(pfad).write_text(
            "LockVersion=1\nOwnerPid=%d\nOwnerName=python\n"
            "OwnerStart=2020-01-01T00:00:00.0000000+01:00\n"
            "Label=alter_lauf\nTakenAt=2026-09-28 03:22:35\nHost=TEST\n" % pid,
            encoding="ascii")

    def test_neu_vergebene_pid_mit_kind_ist_nicht_belegt(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad, inhaber, kind_pid = self.inhaber_starten(tmp)
            try:
                self.sperrdatei_mit_alter_startzeit(pfad, inhaber.pid)
                status = ps(LOCK, "-Modus", "Status", "-LockPfad", pfad)
                ausgabe = status.stdout + status.stderr
                self.assertEqual(status.returncode, 0, ausgabe)
                self.assertIn("verwaist", ausgabe)
                # Auf BELEGT pruefen, nicht auf das Wort "Nachkommen": genau das
                # steht naemlich in dem erklaerenden Zusatz ("... zaehlen nicht
                # als Nachkommen des Laufs"). Der Zustand selbst heisst BELEGT.
                self.assertNotIn("BELEGT", ausgabe)
                # Und es steht dabei, WARUM - sonst sucht der Bediener einen Lauf,
                # den es nicht gibt.
                self.assertIn("an einen anderen Prozess vergeben", ausgabe)
            finally:
                self.alle_beenden(inhaber, kind_pid)

    def test_sabotage_ohne_recycle_schnitt_meldet_belegt(self):
        """Gegenprobe: ohne den Schnitt MUSS derselbe Fall als belegt gelten -
        sonst haengt das Ergebnis oben nicht an der neuen Regel."""
        with tempfile.TemporaryDirectory() as tmp:
            pfad, inhaber, kind_pid = self.inhaber_starten(tmp)
            try:
                self.sperrdatei_mit_alter_startzeit(pfad, inhaber.pid)
                text = LOCK.read_text(encoding="utf-8")
                alt = "-ErstAb $p.StartTime -Recycled"
                self.assertIn(alt, text, "Anker fuer die Sabotage nicht gefunden")
                kopie = Path(tmp) / "engine_run_lock_sabotage2.ps1"
                kopie.write_text(text.replace(alt, "-Recycled"), encoding="utf-8")
                lauf = ps(kopie, "-Modus", "Status", "-LockPfad", pfad)
                ausgabe = lauf.stdout + lauf.stderr
                self.assertEqual(lauf.returncode, 3, ausgabe)
                self.assertIn("Nachkommen", ausgabe)
            finally:
                self.alle_beenden(inhaber, kind_pid)

    def test_kette_steht_mit_pid_und_startzeit_in_der_sperrdatei(self):
        """Die Sperrdatei soll den Lauf identifizierbar machen: nicht eine
        nackte PID, sondern die Kette aus PID UND Startzeit."""
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            nehmen = ps(LOCK, "-Modus", "Nehmen", "-LockPfad", pfad,
                        "-PlattenGrenze", "0", "-Name", "unittest")
            self.assertEqual(nehmen.returncode, 0, nehmen.stdout + nehmen.stderr)
            daten = felder(pfad)
            self.assertIn("Kette", daten, "keine Kette in der Sperrdatei")
            eintraege = [e for e in daten["Kette"].split(";") if e]
            self.assertTrue(eintraege, "Kette ist leer")
            kopf_pid, kopf_start = eintraege[0].split("|", 1)
            self.assertEqual(int(kopf_pid), int(daten["OwnerPid"]))
            self.assertEqual(kopf_start, daten["OwnerStart"])
            # Jeder Eintrag traegt eine Startzeit, sonst taugt er nicht zur
            # Identitaetsprobe.
            for eintrag in eintraege:
                self.assertIn("|", eintrag, "Eintrag ohne Startzeit: %r" % eintrag)
                self.assertTrue(eintrag.split("|", 1)[1].strip(),
                                "leere Startzeit: %r" % eintrag)


class PlattenWartenTest(unittest.TestCase):
    """Ein voruebergehend voller Platz VERZOEGERT einen Start, statt ihn zu
    toeten (28.09.2026).

    Der Anlass ist gemessen: die Python-Suiten eines Push-Laufs kippten bei
    10,0 % frei auf den Abbruchweg, und dieselbe Platte hatte zwanzig Minuten
    spaeter wieder Platz (Raeumlauf, endender Cook). `motor_sperre` wartet
    seit dd74ab3 selbst - hier geht es um den WEG, den alle uebrigen Starts
    nehmen: `-Modus Start` in den .cmd-Wrappern.

    Gemessen wird mit -PlattenTestReihe: der freie Platz wird als FOLGE
    vorgegeben (0.5,400 = erst zu voll, dann Heilung). Ein fester Wert kann
    nicht heilen - und genau deshalb wird auf ihn nie gewartet (eigener Fall
    unten), die Attrappe muss die Wirklichkeit nachbilden koennen.
    """

    def test_start_wartet_und_faengt_die_heilende_platte_ab(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            beginn = time.monotonic()
            # -DryRun: der Erfolgsweg von `Start` faehrt danach die ECHTE
            # Prozessbereinigung. In einem Test hat die nichts zu suchen -
            # sie wuerde einem fremden Editor die Arbeit wegschiessen.
            lauf = ps(LOCK, "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                      "-PlattenTestReihe", "0.5,400", "-PlattenWarteSekunden", "4",
                      "-DryRun")
            gedauert = time.monotonic() - beginn
            ausgabe = lauf.stdout + lauf.stderr
            self.assertEqual(lauf.returncode, 0, ausgabe)
            self.assertIn("warte auf Plattenplatz", ausgabe)
            self.assertIn("gruen", ausgabe)
            self.assertTrue(Path(pfad).exists(), "Lock fehlt nach dem Warten")
            # Der Kern: es wurde WIRKLICH gewartet, das Gruen ist nicht Zufall.
            self.assertGreaterEqual(gedauert, 2.0,
                                    "ohne Wartezeit durchgelaufen: %.1f s" % gedauert)
            self.assertLess(gedauert, 60.0,
                            "die Frist wurde ueberschritten: %.1f s" % gedauert)
            # Gedrosselt: eine Meldung, nicht eine je Messung.
            self.assertEqual(ausgabe.count("warte auf Plattenplatz"), 1, ausgabe)

    def test_fester_testwert_wird_nie_gewartet(self):
        """Die Gegenprobe zum Warten: -PlattenTestGiga ist eine FESTE Zahl.

        Sie kann nicht heilen, warten waere sinnlos - und der Selbsttest
        haenge 30 Minuten an einem Wert, der sich nie aendert. Wer das Warten
        an eine Simulation haengt, hat es genau hier falsch gemacht.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            beginn = time.monotonic()
            lauf = ps(LOCK, "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                      "-PlattenTestGiga", "0.5", "-PlattenWarteSekunden", "30")
            gedauert = time.monotonic() - beginn
            ausgabe = lauf.stdout + lauf.stderr
            self.assertEqual(lauf.returncode, 4, ausgabe)
            self.assertIn("ABBRUCH", ausgabe)
            self.assertNotIn("warte auf Plattenplatz", ausgabe)
            self.assertLess(gedauert, 20.0, "auf einen festen Wert gewartet: %.1f s" % gedauert)
            self.assertFalse(Path(pfad).exists(), "Abbruch hat eine Sperrdatei hinterlassen")

    def test_nehmen_wartet_nie_auch_bei_heilender_reihe(self):
        """`Nehmen` ist die Primaerstelle fuer Aufrufer mit EIGENER Frist.

        `motor_sperre` faehrt genau diesen Modus und wartet selbst in
        15-s-Takten bis zu seiner Frist (mit Attrappen-Uhr pruefbar). Ein
        zweites Warten im Skript wuerde diese Frist verdoppeln und ihre Uhr
        entwerten. Der zweite Reihenwert waere gut - `Nehmen` darf ihn nicht
        abwarten.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            beginn = time.monotonic()
            lauf = ps(LOCK, "-Modus", "Nehmen", "-Name", "unittest", "-LockPfad", pfad,
                      "-PlattenTestReihe", "0.5,400", "-PlattenWarteSekunden", "30")
            gedauert = time.monotonic() - beginn
            ausgabe = lauf.stdout + lauf.stderr
            self.assertEqual(lauf.returncode, 4, ausgabe)
            self.assertIn("ABBRUCH", ausgabe)
            self.assertNotIn("warte auf Plattenplatz", ausgabe)
            self.assertLess(gedauert, 20.0, "Nehmen hat gewartet: %.1f s" % gedauert)
            self.assertFalse(Path(pfad).exists(), "Abbruch hat eine Sperrdatei hinterlassen")

    def test_frist_ablauf_bricht_ab_und_laesst_keine_sperrdatei(self):
        """Bleibt die Platte bis zur Frist zu voll, faellt der Start doch.

        Auch dann gilt die Regel des Gate: der Abbruch hinterlaesst NICHTS -
        sonst liest der naechste Lauf Exit 4 als "Lock belegt" und raeumt
        notfalls fremde Editoren weg.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            beginn = time.monotonic()
            lauf = ps(LOCK, "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                      "-PlattenTestReihe", "0.5", "-PlattenWarteSekunden", "3")
            gedauert = time.monotonic() - beginn
            ausgabe = lauf.stdout + lauf.stderr
            self.assertEqual(lauf.returncode, 4, ausgabe)
            self.assertIn("ABBRUCH", ausgabe)
            self.assertIn("blieb bis zur Frist zu voll", ausgabe)
            self.assertGreaterEqual(gedauert, 1.5,
                                    "der Ablauf kam ohne Warten: %.1f s" % gedauert)
            self.assertFalse(Path(pfad).exists(), "Abbruch hat eine Sperrdatei hinterlassen")

    def test_frist_abschaltbar_und_grenze_bleibt(self):
        """Der Vertrag in Textform: die Frist ist eine eigene Angabe, das
        Warten abschaltbar, und die Abbruchgrenze bleibt bei 10 % (die
        Meldegrenze des Waechters ist eine andere)."""
        text = LOCK.read_text(encoding="utf-8")
        self.assertIn("[int]$PlattenWarteSekunden = 1800", text)
        self.assertIn("[double]$PlattenGrenze = 10.0", text)
        # Der feste Selbsttest-Wert bleibt unveraendert: die bestehenden
        # Lock-Tests und PlattenGateTest haengen an seiner Form.
        self.assertIn("[double]$PlattenTestGiga = -1", text)


def gib_fuer(prozent):
    """Die GiB-Zahl, die -PlattenTestGiga/-PlattenTestReihe fuer diesen
    Prozentwert braucht.

    Das Skript rechnet `giga * 1GB / gesamt`, und PowerShell liest `1GB`
    BINAER - die Testzahl ist also GiB. Die Gesamtgroesse kommt vom ECHTEN
    Laufwerk (Get-ProzentAusGiga liest sie dort), damit der Test nicht an der
    Plattengroesse dieses Rechners haengt: 20 % sind 190,5 GiB auf einem
    953-GiB-Laufwerk und auf einem anderen etwas anderes.
    """
    gesamt = shutil.disk_usage(str(WURZEL)).total
    return gesamt * prozent / 100.0 / (1024.0 ** 3)


def frei_prozent():
    """Der ECHTE Plattenstand in Prozent - fuer die Faelle, die nur unter der
    Meldegrenze etwas messen und bei gesunder Platte uebersprungen werden."""
    stand = shutil.disk_usage(str(WURZEL))
    return stand.free / stand.total * 100.0


class PlattenVorreinigungTest(unittest.TestCase):
    """Der Engine-Start raeumt die Caches, BEVOR er den Lock nimmt
    (30.09.2026).

    Der Anlass ist gemessen: das Platten-Gate kannte nur Warten und Abbrechen,
    obwohl die reproduzierbaren Caches als Reserve danebenlagen. Am 29.09.2026
    standen 10,2 % frei (0 % Luft bis zur Abbruchgrenze), dieselbe
    cache-only-Reinigung holte 11 GB - von Hand. Jetzt holt sie der Start
    selbst, unter der Meldegrenze von 20 %.

    Gemessen wird mit einem ERSATZ-Waechter (-PlattenReinigerSkript): er
    loescht nichts, sondern schreibt auf, WIE er gerufen wurde und ob zu
    diesem Zeitpunkt schon eine Sperrdatei lag. Genau das ist die Behauptung -
    Argumente und Reihenfolge, nicht "irgendwas ist passiert".

    Der echte Waechter darf in dieser Suite NIE laufen: er loescht die Caches
    des Rechners. Genau dafuer gibt es den Attrappen-Zweig (eigener Fall
    unten) - eine erfundene Plattenzahl loescht nichts.
    """

    def falscher_waechter(self, tmp, protokoll, lock, exit_code=0):
        skript = Path(tmp) / "falscher_waechter.py"
        skript.write_text(
            "import json, os, sys\n"
            "with open(r'%s', 'a', encoding='utf-8') as fh:\n"
            "    fh.write(json.dumps({'argv': sys.argv[1:],\n"
            "                         'lock_da': os.path.exists(r'%s')}) + '\\n')\n"
            "print('GELOESCHT: <erfunden>')\n"
            "print('zusammen: 1.00 GiB')\n"
            "sys.exit(%d)\n" % (protokoll, lock, exit_code), encoding="utf-8")
        return skript

    def test_unter_der_meldegrenze_wird_vor_dem_lock_geraeumt(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            protokoll = Path(tmp) / "reinigung.jsonl"
            waechter = self.falscher_waechter(tmp, str(protokoll), pfad)
            # 18,5 % frei: unter der Meldegrenze (20 %), ueber der Abbruchgrenze
            # (10 %). Nach dem Raeumen 40 % - so ist das Gruen des Gates die
            # Folge der Reinigung und nicht der Reihe geschuldet.
            reihe = "%.1f,%.1f" % (gib_fuer(18.5), gib_fuer(40.0))
            lauf = ps(LOCK, "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                      "-PlattenTestReihe", reihe, "-PlattenWarteSekunden", "0",
                      "-PlattenReinigerSkript", str(waechter), "-DryRun")
            ausgabe = lauf.stdout + lauf.stderr
            self.assertEqual(lauf.returncode, 0, ausgabe)
            self.assertIn("BEVOR der Lock genommen wird", ausgabe)
            self.assertIn("nach der Vorreinigung", ausgabe)
            self.assertIn("gruen", ausgabe)
            # Der Befund des Waechters wird durchgereicht, nicht verschluckt:
            # die Zeile, die den Eingriff belegt, steht im Lock-Log.
            self.assertIn("GELOESCHT: <erfunden>", ausgabe)
            self.assertTrue(Path(pfad).exists(), "Lock fehlt nach der Vorreinigung")
            rufe = [json.loads(z) for z in
                    protokoll.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(rufe), 1,
                             "der Raeumlauf lief %d mal statt einmal" % len(rufe))
            # NUR die Cache-Klasse: --auch-ausgabe kostet Bauzeit,
            # --auch-devbuilds sind die Beweise des fremden Threads.
            self.assertEqual(rufe[0]["argv"], ["--reinigen"],
                             "der Raeumlauf bekam andere Argumente: %r" % rufe[0]["argv"])
            self.assertFalse(rufe[0]["lock_da"],
                             "geraeumt wurde erst NACH dem Lock - die Sperrdatei lag schon")

    def test_ueber_der_meldegrenze_wird_nicht_geraeumt(self):
        """Ueber 20 % gibt es nichts zu holen - und nichts zu loeschen.

        Ohne diesen Fall waere jede Engine-Start-Meldung voller Raeumzeilen,
        und der Waechter liefe bei gesunder Platte mit. Ein Raeumlauf, der
        nichts zu raeumen hat, ist trotzdem ein Eingriff: er misst die
        Kandidaten (Sekunden) und schreibt ins Loeschprotokoll.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            protokoll = Path(tmp) / "reinigung.jsonl"
            waechter = self.falscher_waechter(tmp, str(protokoll), pfad)
            lauf = ps(LOCK, "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                      "-PlattenTestReihe", "%.1f" % gib_fuer(22.0),
                      "-PlattenWarteSekunden", "0",
                      "-PlattenReinigerSkript", str(waechter), "-DryRun")
            ausgabe = lauf.stdout + lauf.stderr
            self.assertEqual(lauf.returncode, 0, ausgabe)
            self.assertIn("gruen", ausgabe)
            self.assertNotIn("geraeumt", ausgabe)
            self.assertFalse(protokoll.exists(), "ueber der Grenze wurde geraeumt")

    def test_attrappe_loescht_nie_die_echte_platte(self):
        """Eine ERFUNDENE Plattenzahl darf keine echten Dateien anfassen.

        Ohne diesen Zweig liefe hier der ECHTE Waechter: die Suite faehrt
        Platzreihen wie 0.5,400, und 0.5 % frei haette die Caches dieses
        Rechners geloescht - ein Testlauf als Plattenraeuber. Ohne
        -PlattenReinigerSkript wird darum nur GEMELDET.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            lauf = ps(LOCK, "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                      "-PlattenTestReihe", "%.1f" % gib_fuer(0.5),
                      "-PlattenWarteSekunden", "0")
            ausgabe = lauf.stdout + lauf.stderr
            self.assertEqual(lauf.returncode, 4, ausgabe)
            self.assertIn("eine Probe loescht nichts", ausgabe)
            self.assertNotIn("werden geraeumt, BEVOR", ausgabe)
            self.assertFalse(Path(pfad).exists(),
                             "Abbruch hat eine Sperrdatei hinterlassen")

    @unittest.skipUnless(frei_prozent() < 20.0,
                         "die Platte ist gesund - dieser Fall misst nur unter 20 %")
    def test_umgeleiteter_lock_allein_ist_schon_eine_probe(self):
        """Ein umgeleiteter Lock ist eine Probe, auch ohne Attrappen-Zahl.

        Der Fall, der es braucht: test_gate_worktree.py faehrt den Lock 21 mal
        mit -LockPfad in %TEMP% - und misst dabei die ECHTE Platte. Ohne diese
        Regel raeumte so ein Testlauf die Caches des Rechners.

        Gemessen wird mit einer KOPIE des Skripts in %TEMP% und einem
        Ersatz-Waechter an dessen Standardpfad: er wuerde sich melden, wenn
        geraeumt wird - loeschen kann er nichts.
        """
        with tempfile.TemporaryDirectory() as tmp:
            ps1 = Path(tmp) / "engine_run_lock.ps1"
            shutil.copy(LOCK, ps1)
            protokoll = Path(tmp) / "reinigung.jsonl"
            lock = str(Path(tmp) / "l.lock")
            self.falscher_waechter(tmp, str(protokoll), lock)
            (Path(tmp) / "platten_waechter.py").write_text(
                (Path(tmp) / "falscher_waechter.py").read_text(encoding="utf-8"),
                encoding="utf-8")
            # -Modus Nehmen: genau dieser Weg steht in test_gate_worktree.py,
            # und er faehrt keine Prozessbereinigung (die Kopie in %TEMP% kennt
            # cleanup_unreal_processes.ps1 nicht).
            lauf = ps(ps1, "-Modus", "Nehmen", "-Name", "probe", "-LockPfad", lock)
            ausgabe = lauf.stdout + lauf.stderr
            self.assertEqual(lauf.returncode, 0, ausgabe)
            self.assertIn("eine Probe loescht nichts", ausgabe)
            self.assertNotIn("werden geraeumt, BEVOR", ausgabe)
            self.assertFalse(protokoll.exists(),
                             "der Waechter am Standardpfad wurde gerufen: "
                             + (protokoll.read_text(encoding="utf-8")
                                if protokoll.exists() else ""))

    def test_raeumfehler_ist_fail_open(self):
        """Ein kaputter Raeumlauf darf den Start nicht toeten.

        Fehlt das Skript, fehlt python oder endet der Lauf mit einem Fehler,
        bleibt es bei EINER Zeile im Log - sonst waere ein Raeumwerkzeug die
        neue Ursache fuer abgebrochene Starts. Dieselbe Haltung wie beim
        Plattenhinweis in vor_dem_commit: melden, nie verbieten.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            protokoll = Path(tmp) / "reinigung.jsonl"
            waechter = self.falscher_waechter(tmp, str(protokoll), pfad, exit_code=3)
            reihe = "%.1f,%.1f" % (gib_fuer(18.5), gib_fuer(40.0))
            lauf = ps(LOCK, "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                      "-PlattenTestReihe", reihe, "-PlattenWarteSekunden", "0",
                      "-PlattenReinigerSkript", str(waechter), "-DryRun")
            ausgabe = lauf.stdout + lauf.stderr
            self.assertEqual(lauf.returncode, 0, ausgabe)
            self.assertIn("endete mit Code 3", ausgabe)
            self.assertTrue(Path(pfad).exists(), "der Start fiel wegen des Raeumfehlers aus")

    def test_abschaltbar_mit_eigener_grenze(self):
        """-PlattenReinigungsGrenze 0 schaltet NUR die Vorreinigung ab."""
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            protokoll = Path(tmp) / "reinigung.jsonl"
            waechter = self.falscher_waechter(tmp, str(protokoll), pfad)
            lauf = ps(LOCK, "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                      "-PlattenTestReihe", "%.1f" % gib_fuer(18.5),
                      "-PlattenReinigungsGrenze", "0", "-PlattenWarteSekunden", "0",
                      "-PlattenReinigerSkript", str(waechter), "-DryRun")
            ausgabe = lauf.stdout + lauf.stderr
            # 18,5 % ist ueber der Abbruchgrenze - der Start laeuft gruen durch,
            # nur geraeumt wird nicht.
            self.assertEqual(lauf.returncode, 0, ausgabe)
            self.assertIn("gruen", ausgabe)
            self.assertNotIn("BEVOR der Lock genommen wird", ausgabe)
            self.assertFalse(protokoll.exists(), "trotz Grenze 0 geraeumt")

    def test_status_und_freigeben_raeumen_nie(self):
        """Die beiden Verwaltungsmodi fassen die Platte nicht an.

        Auf einer vollen Platte muss man seinen Lock noch loesen koennen -
        dieselbe Begruendung wie beim Platten-Gate. Ein Raeumlauf dort waere
        der schlimmste Ort: er wuerde, auf einer vollen Platte, zur Freigabe
        erst noch Platz schaffen wollen.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            protokoll = Path(tmp) / "reinigung.jsonl"
            waechter = self.falscher_waechter(tmp, str(protokoll), pfad)
            for modus in ("Status", "Freigeben"):
                lauf = ps(LOCK, "-Modus", modus, "-LockPfad", pfad,
                          "-PlattenTestReihe", "%.1f" % gib_fuer(0.5),
                          "-PlattenReinigerSkript", str(waechter))
                self.assertEqual(lauf.returncode, 0, modus + ": " + lauf.stdout + lauf.stderr)
            self.assertFalse(protokoll.exists(), "Status/Freigeben haben geraeumt")

    def test_vertrag_der_meldegrenze(self):
        """Die 20 % stehen an zwei Stellen - sie muessen dieselbe Zahl sein.

        `GRENZE_PROZENT` im Waechter ist die Meldegrenze, die Vorgabe des
        Skripts ist die Raeumgrenze. Zwei Zahlen, die dasselbe meinen: laufen
        sie auseinander, raeumt der Start entweder zu spaet (unter 10 % faellt
        er schon) oder bei jeder Meldung.
        """
        text = LOCK.read_text(encoding="utf-8")
        self.assertIn("[double]$PlattenReinigungsGrenze = 20.0", text)
        self.assertIn("[double]$PlattenGrenze = 10.0", text)
        # Die Probe-Regel nennt beide Faelle: Attrappen-Zahl UND umgeleiteter
        # Lock. Faellt einer davon heraus, raeumt eine fremde Testsuite.
        probe = [z for z in text.splitlines() if z.strip().startswith("$probe =")]
        self.assertEqual(len(probe), 1, "die Probe-Regel fehlt: %r" % probe)
        self.assertIn("$PlattenTestReihe", probe[0])
        self.assertIn("$PlattenTestGiga", probe[0])
        # NICHT $LockPfad: der Parameter wird unten von der AUFGELOESTEN
        # Sperrdatei ueberschrieben (PowerShell vergleicht Namen ohne
        # Ruecksicht auf Gross/Klein) und waere immer wahr.
        self.assertIn("$script:LockPfadGesetzt", probe[0])
        self.assertIn("$script:LockPfadGesetzt = ($PSBoundParameters", text)
        sys.path.insert(0, str(WURZEL / "Tools"))
        import platten_waechter
        self.assertEqual(platten_waechter.GRENZE_PROZENT, 20.0,
                         "die Meldegrenze des Waechters ist gewandert")
        self.assertEqual(tuple(platten_waechter.NUR_LOESCHEN), ("cache",),
                         "die Vorgabe von --reinigen ist nicht mehr die Cache-Klasse")


# Elternprozess fuer den Warte-Fall: ein FREMDER lebender Besitzer mit einer
# Sperrdatei, die auf ihn zeigt. OwnerStart bleibt leer - so wird keine
# Startzeit verglichen und der Besitzer ist eindeutig der fremde Prozess.
class WartenAufDenLockTest(unittest.TestCase):
    """Ein belegter Lock verzoegert den Start, statt ihn zu toeten.

    Der Anlass ist die Asymmetrie vom 28.09.2026: die Plattennot war bereits
    wait-tolerant gemacht worden, ein BELEGTER Lock aber brach weiterhin sofort
    mit Exit 3 ab - obwohl die Lage dieselbe ist. Ein fremder Lauf gibt die
    Sperre mit seinem Prozess von selbst frei, wer wartet bekommt sie; wer
    sofort abbricht, verliert den Lauf und wirft ihn von Hand noch einmal an.

    Die zweite Haelfte der Regel ist genauso wichtig und wird eigens
    geprueft: `-Modus Nehmen` (der Weg von motor_sperre) wartet NICHT. Ein
    zweites Warten dort verdoppelte die Frist jedes Push-Laufs und entwertete
    die Attrappen-Uhr der Python-Tests.
    """

    def fremde_sperre(self, tmp, dauer=120):
        """Lebender fremder Besitzer + Sperrdatei darauf. Rueckgabe (pfad, prozess)."""
        pfad = str(Path(tmp) / "engine_run.lock")
        fremder = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(%d)" % dauer],
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        Path(pfad).write_text(
            "LockVersion=1\nOwnerPid=%d\nOwnerName=python\nLabel=rebake_alkis25\n"
            "TakenAt=2026-09-28 19:00:00\nHost=TEST\n" % fremder.pid,
            encoding="ascii")
        return pfad, fremder

    def wartezeilen(self, lauf):
        return [z for z in (lauf.stdout or "").splitlines() if "warte auf Freigabe" in z]

    def test_start_wartet_und_bricht_erst_zur_frist_ab(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad, fremder = self.fremde_sperre(tmp)
            try:
                beginn = time.monotonic()
                # -PlattenGrenze 0: dieser Test prueft die SPERRE, nicht den
                # Platz. Sonst haengt sein Ergebnis an der Platte des Rechners.
                # -DryRun: der Erfolgsweg von `Start` faehrt danach die ECHTE
                # Prozessbereinigung - in einem Test hat die nichts zu suchen.
                lauf = ps(LOCK, "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                          "-PlattenGrenze", "0", "-StartWarteSekunden", "3", "-DryRun")
                gedauert = time.monotonic() - beginn
                ausgabe = lauf.stdout + lauf.stderr
                self.assertEqual(lauf.returncode, 3, ausgabe)
                self.assertIn("BELEGT", ausgabe)
                self.assertIn("bis zur Frist belegt", ausgabe)
                # Der Kern: es wurde WIRKLICH gewartet, nicht sofort abgebrochen.
                self.assertGreaterEqual(gedauert, 2.0,
                                        "ohne Wartezeit abgebrochen: %.1f s" % gedauert)
                self.assertLess(gedauert, 60.0, "Frist ueberschritten: %.1f s" % gedauert)
                # Gedrosselt: eine Meldung, nicht eine je Abfrage (alle 2 s).
                self.assertEqual(len(self.wartezeilen(lauf)), 1, ausgabe)
                # Warten ist kein Uebernehmen: die fremde Sperre bleibt, wie sie ist.
                self.assertEqual(felder(pfad)["Label"], "rebake_alkis25")
                self.assertEqual(int(felder(pfad)["OwnerPid"]), fremder.pid)
                # Und die Bereinigung lief nicht - der fremde Editor lebt noch.
                self.assertNotIn("Prozessbereinigung", ausgabe)
            finally:
                fremder.terminate()
                fremder.wait(timeout=30)

    def test_nehmen_wartet_nie_und_start_mit_null_auch_nicht(self):
        """Die Grenze, die die Wartefrist der Wrapper einzieht.

        `Nehmen` ist der Weg von motor_sperre: dort waere ein zweites Warten
        eine verdoppelte Frist. Und ein ausdrueckliches `-WarteSekunden 0`
        schaltet das Warten von `Start` ab - der Notausgang muss erhalten
        bleiben, sonst haengt ein Wrapper 30 Minuten an einem Lauf, den der
        Bediener abbrechen wollte.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad, fremder = self.fremde_sperre(tmp)
            try:
                for modus, extra in (("Nehmen", []), ("Start", ["-WarteSekunden", "0", "-DryRun"])):
                    beginn = time.monotonic()
                    lauf = ps(LOCK, "-Modus", modus, "-Name", "unittest", "-LockPfad", pfad,
                              "-PlattenGrenze", "0", *extra)
                    gedauert = time.monotonic() - beginn
                    ausgabe = lauf.stdout + lauf.stderr
                    self.assertEqual(lauf.returncode, 3, "%s: %s" % (modus, ausgabe))
                    self.assertFalse(self.wartezeilen(lauf),
                                     "%s hat gewartet: %s" % (modus, ausgabe))
                    self.assertLess(gedauert, 20.0,
                                    "%s kam nicht sofort zurueck: %.1f s" % (modus, gedauert))
            finally:
                fremder.terminate()
                fremder.wait(timeout=30)

    def test_start_faengt_eine_freigabe_waehrend_des_wartens_ab(self):
        """Die Gegenprobe zum Abbruch: warten muss sich auch AUSZAHLEN.

        Der fremde Besitzer endet mitten in der Wartezeit. Genau das ist der
        Alltagsfall (ein Lauf geht zu Ende, waehrend der naechste ansteht) -
        der wartende Start muss die Sperre dann bekommen und durchlaufen.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad, fremder = self.fremde_sperre(tmp, dauer=120)
            lauf = subprocess.Popen(
                ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(LOCK),
                 "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                 "-PlattenGrenze", "0", "-StartWarteSekunden", "90", "-DryRun"],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                encoding="utf-8", errors="replace")
            try:
                time.sleep(5.0)          # mitten im Warten
                self.assertTrue(beenden(fremder.pid), "fremder Besitzer liess sich nicht beenden")
                ausgabe, _ = lauf.communicate(timeout=120)
            finally:
                fremder.terminate()
                fremder.wait(timeout=30)
            self.assertEqual(lauf.returncode, 0, ausgabe)
            self.assertIn("verwaist", ausgabe)
            self.assertIn("uebernommen", ausgabe)
            daten = felder(pfad)
            self.assertEqual(daten["Label"], "unittest")
            self.assertEqual(int(daten["OwnerPid"]), os.getpid())

    def test_sabotage_ohne_die_neue_abfrage_bricht_sofort_ab(self):
        """Eine Pruefung, die bei falschem Code gruen bleibt, misst nichts.

        Gefahren wird eine Kopie, in der die Abfrage "wurde -WarteSekunden
        ausdruecklich gesetzt?" fest auf JA steht - dann gewinnt die Vorgabe 0
        und derselbe Aufruf bricht sofort ab. Ohne diesen Gegenlauf haengt das
        Warten oben nicht an der neuen Regel, sondern an einem Zufall.
        """
        with tempfile.TemporaryDirectory() as tmp:
            pfad, fremder = self.fremde_sperre(tmp)
            try:
                text = LOCK.read_text(encoding="utf-8")
                alt = '$script:WarteSekundenGesetzt = $PSBoundParameters.ContainsKey("WarteSekunden")'
                self.assertIn(alt, text, "Anker fuer die Sabotage nicht gefunden")
                kopie = Path(tmp) / "engine_run_lock_sabotage3.ps1"
                kopie.write_text(text.replace(alt, "$script:WarteSekundenGesetzt = $true"),
                                 encoding="utf-8")

                beginn = time.monotonic()
                lauf = ps(kopie, "-Modus", "Start", "-Name", "unittest", "-LockPfad", pfad,
                          "-PlattenGrenze", "0", "-StartWarteSekunden", "30", "-DryRun")
                gedauert = time.monotonic() - beginn
                ausgabe = lauf.stdout + lauf.stderr
                self.assertEqual(lauf.returncode, 3, ausgabe)
                self.assertFalse(self.wartezeilen(lauf), "die Sabotage hat gewartet: " + ausgabe)
                self.assertLess(gedauert, 20.0, "Sabotage wartete: %.1f s" % gedauert)
            finally:
                fremder.terminate()
                fremder.wait(timeout=30)

    def test_vertrag_der_beiden_fristen(self):
        """Der Vertrag in Textform - die Null fuer `Nehmen` ist die Falle.

        motor_sperre uebergibt -WarteSekunden NICHT und verlaesst sich auf die
        Vorgabe des Skripts; nur so bleibt es bei EINER Frist je Push-Lauf.
        Wer die Vorgabe von `-WarteSekunden` auf einen Wert groesser 0 zieht,
        verdoppelt sie still. Deshalb steht beides hier nebeneinander.
        """
        text = LOCK.read_text(encoding="utf-8")
        self.assertIn("[int]$WarteSekunden = 0", text)
        self.assertIn("[int]$StartWarteSekunden = 1800", text)
        motor = (WURZEL / "Tools" / "gate_worktree.py").read_text(encoding="utf-8")
        anfang = motor.index('befehl = ["powershell"')
        kommandobau = motor[anfang:motor.index("while True:", anfang)]
        self.assertNotIn("-WarteSekunden", kommandobau,
                         "motor_sperre setzt die Frist jetzt selbst - dann muss die "
                         "Vorgabe des Skripts NICHT mehr 0 bleiben")


class UebernahmeRennenTest(unittest.TestCase):
    """Verwaiste Sperre, mehrere Wartende: genau EINER darf uebernehmen.

    Seit die .cmd-Wrapper auf einen belegten Lock warten, treffen mehrere
    Uebernehmer gleichzeitig auf dieselbe verwaiste Datei. Die Uebernahme
    raeumt sie deshalb mit `Move-Item` weg (atomar auf derselben Platte) und
    legt danach mit `CreateNew` an - ein Rennen entscheidet, kein Zweischritt.

    Jeder Wartende startet ueber einen EIGENEN Startprozess: der ist der
    ELTERNprozess, den das Skript als Besitzer eintraegt. Waere er fuer alle
    vier derselbe (der Testprozess), gaelte die frische Sperre des Gewinners
    den Verlierern als EIGEN - der Test pruefte dann seinen eigenen Aufbau.
    Und der Startprozess H A E L T nach dem Skriptende noch: genau das macht
    ein Wrapper, dessen Besitzer der aufrufende cmd.exe ist. Ohne diese
    Haltezeit waere die Sperre direkt nach dem Skriptende verwaist, und der
    naechste Wartende duerfte sie zu Recht uebernehmen.
    """

    # Frist der Verlierer und Haltezeit des Gewinners: die Frist muss kuerzer
    # sein, sonst warten die Verlierer, bis der Gewinner fertig ist, und der
    # Test koennte "belegt" gar nicht mehr von "frei" unterscheiden.
    FRIST = 3
    HALTEN = 6.0

    def verwaiste_sperre(self, tmp):
        """Sperrdatei mit einer PID, die es nicht mehr gibt.

        Ohne `OwnerStart` prueft das Skript die Startzeit nicht, und 999999 ist
        garantiert kein lebender Prozess - der Zustand ist eindeutig verwaist.
        """
        pfad = str(Path(tmp) / "engine_run.lock")
        Path(pfad).write_text(
            "LockVersion=1\nOwnerPid=999999\nOwnerName=python\nOwnerStart=\n"
            "Label=push_gate\nTakenAt=2026-09-28 20:00:00\nHost=TEST\n",
            encoding="ascii")
        return pfad

    def kopie_mit(self, tmp, dateiname, alt, neu):
        """Kopie des Skripts mit EINER ersetzten Stelle (Sabotage/Gegenprobe)."""
        text = LOCK.read_text(encoding="utf-8")
        self.assertIn(alt, text, "Anker fuer den Eingriff nicht gefunden: " + dateiname)
        ziel = Path(tmp) / dateiname
        ziel.write_text(text.replace(alt, neu), encoding="utf-8")
        return ziel

    def rennrunde(self, skript, pfad, anzahl=4, frist=None, halten=None, stufe=0.0):
        """`anzahl` Starts gleichzeitig auf dieselbe Sperre. Rueckgabe [(rc, ausgabe)].

        -PlattenGrenze 0: geprueft wird die Sperre, nicht die Platte des
        Rechners. -DryRun: der Erfolgsweg faehrt die ECHTE
        Prozessbereinigung - die hat in einem Test nichts zu suchen.

        `stufe` spreizt die Starts (Sekunden zwischen zwei Popen). GEMESSEN
        am 30.09.2026: ohne Spreizung schob ein Lastlauf alle vier
        Raeumen-Schritte VOR alle Anlegen-Schritte, das atomare CreateNew
        maskierte damit den Zweischritt der Sabotage (ein Gewinner), und die
        Gegenprobe wurde rot, obwohl der alte Code den Fehler hat. Mit
        Spreizung liegt jedes Raeumen+Anlegen-Paar hinter dem des
        Vorgaengers - genau der Ablauf vom 28.09.2026.
        """
        frist = self.FRIST if frist is None else frist
        halten = self.HALTEN if halten is None else halten
        starter = ("import subprocess, sys, time\n"
                   "halten = float(sys.argv[1])\n"
                   "rc = subprocess.call(sys.argv[2:])\n"
                   "time.sleep(halten)\n"
                   "sys.exit(rc)\n")
        aufruf = [sys.executable, "-c", starter, str(halten),
                  "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                  "-File", str(skript), "-Modus", "Start", "-Name", "rennrunde",
                  "-LockPfad", str(pfad), "-PlattenGrenze", "0",
                  "-StartWarteSekunden", str(frist), "-DryRun"]
        laeufe = []
        for _ in range(anzahl):
            laeufe.append(subprocess.Popen(aufruf, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, text=True,
                                           encoding="utf-8", errors="replace"))
            if stufe > 0:
                time.sleep(stufe)
        ergebnisse = []
        for lauf in laeufe:
            ausgabe, _ = lauf.communicate(timeout=300)
            ergebnisse.append((lauf.returncode, ausgabe))
        return ergebnisse

    # Der Eingriff, der das Rennfenster WEIT aufmacht (vorher gab es nur
    # Mikrosekunden zwischen Wegdoen und Anlegen). Er sitzt VOR der Uebernahme,
    # damit alle vier Wartenden gleichzeitig an der verwaisten Datei stehen -
    # genau der Zustand, den die Warteschleife der Wrapper erzeugt.
    SPALT = '            $weggeraumt = "{0}.verwaist-{1}" -f $Pfad, [Guid]::NewGuid().ToString("N")'
    ZWEISCHRITT = (
        "            try {\n"
        "                Move-Item -LiteralPath $Pfad -Destination $weggeraumt -ErrorAction Stop\n"
        "            } catch {\n"
        "                Start-Sleep -Milliseconds 200\n"
        "                continue\n"
        "            }\n"
        "            Remove-Item -LiteralPath $weggeraumt -Force -ErrorAction SilentlyContinue\n"
    )

    def test_nur_einer_uebernimmt_die_verwaiste_sperre(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad = self.verwaiste_sperre(tmp)
            ergebnisse = self.rennrunde(LOCK, pfad)
            ausgaben = "\n---\n".join(a for _, a in ergebnisse)

            # Genau EIN Gewinner. Beim alten Zweischritt waren es zwei oder
            # mehr - und zwei Engines gleichzeitig sind der Schaden.
            gewinner = [rc for rc, _ in ergebnisse if rc == 0]
            self.assertEqual(len(gewinner), 1, ausgaben)
            # Die Verlierer brechen erst an der FRIST ab - sie haben gewartet,
            # nicht gestohlen.
            for rc, aus in ergebnisse:
                self.assertEqual(rc, 0 if rc == 0 else 3, aus)
                if rc != 0:
                    self.assertIn("bis zur Frist belegt", aus)
            # Und die Sperre gehoert danach genau diesem Lauf.
            self.assertEqual(felder(pfad)["Label"], "rennrunde")

    def test_weiter_spalt_mit_atomarer_uebernahme_laesst_einen_durch(self):
        """Positivkontrolle: der breite Spalt allein laesst niemanden zusaetzlich durch.

        Dieselbe Verzoegerung wie in der Sabotage, aber die ECHTE Uebernahme
        (Move-Item). Ohne diese Probe liesse sich einwenden, nicht der
        Zweischritt sei schuld, sondern die Verzoegerung - dann haette der
        Test oben nichts ueber den Code gesagt.

        BIS ZU DREI ANLAEUFE (30.09.2026): der 4-s-Spalt ist kuenstlich, und
        auch die atomare Uebernahme hat darin ein Fenster von der Pruefung bis
        zum Move. Faellt in dieses Fenster die FRISCHE Sperre des Vorgaengers,
        zieht der Move sie weg und der zweite uebernimmt ebenfalls - GEMESSEN
        in einem Lastlauf, der diese Kontrolle rot machte. Im ECHTEN Ablauf
        ist das Fenster Mikrosekunden statt Sekunden; geprueft wird hier der
        kuenstliche Fall, und ein Anlauf mit genau einem Gewinner belegt ihn.
        Drei Anlaeufe mit MEHR als einem Gewinner sind dagegen ein Befund.
        """
        with tempfile.TemporaryDirectory() as tmp:
            for versuch in range(3):
                ordner = Path(tmp) / ("versuch%d" % versuch)
                ordner.mkdir()
                pfad = self.verwaiste_sperre(ordner)
                kopie = self.kopie_mit(
                    ordner, "engine_run_lock_spalt.ps1", self.SPALT,
                    "            Start-Sleep -Seconds 4\n" + self.SPALT)
                ergebnisse = self.rennrunde(kopie, pfad, frist=10, halten=8.0)
                ausgaben = "\n---\n".join(a for _, a in ergebnisse)
                uebernahmen = sum(1 for _, aus in ergebnisse if "Lock: gehalten" in aus)
                if uebernahmen <= 1:
                    self.assertEqual(uebernahmen, 1, ausgaben)
                    return
            self.fail(
                "die atomare Uebernahme liess in 3 Anlaeufen je mehrere "
                "Uebernehmer zu - der breite Spalt allein reicht dann als "
                "Erklaerung:\n" + ausgaben)

    def test_sabotage_uebernahme_im_zweischritt_laesst_mehrere_durch(self):
        """Eine Regel, deren Gegenprobe gruen bleibt, ist keine Regel.

        Gefahren wird eine Kopie mit dem ALTEN Zweischritt (Datei loeschen,
        dann anlegen) und demselben breiten Spalt: alle vier Wartenden lesen
        die verwaiste Datei, alle vier raeumen weg und legen an - nacheinander
        loescht jeder die frische Sperre des Vorgaengers. Genau dieses Bild
        war am 28.09.2026 um 20:21:53 auf der Maschine zu sehen (zwei Laeufe
        mit demselben Anspruch, und die Sperrdatei trug am Ende nur den
        letzten).

        SPREIZUNG UND WIEDERHOLUNG (30.09.2026): ohne Spreizung schob ein
        Lastlauf alle vier Raeumen-Schritte vor alle Anlegen-Schritte - dann
        faengt das atomare CreateNew den Doppelzugriff ab (GEMESSEN: ein
        Gewinner) und die Gegenprobe wird rot, obwohl der alte Code den
        Fehler hat. Die Spreizung haelt die Paare auseinander, die
        Wiederholung faengt die Restunschaerfe eines echten Rennens ab;
        erst wenn drei Anlaeufe je nur einen Gewinner sehen, ist die
        Sabotage nicht mehr aussagekraeftig.
        """
        with tempfile.TemporaryDirectory() as tmp:
            for versuch in range(3):
                ordner = Path(tmp) / ("versuch%d" % versuch)
                ordner.mkdir()
                pfad = self.verwaiste_sperre(ordner)
                kopie = self.kopie_mit(
                    ordner, "engine_run_lock_sabotage_uebernahme.ps1", self.ZWEISCHRITT,
                    "            Start-Sleep -Seconds 4\n"
                    "            Remove-Item -LiteralPath $Pfad -Force -ErrorAction SilentlyContinue\n")
                ergebnisse = self.rennrunde(kopie, pfad, frist=12, halten=6.0, stufe=0.8)
                # Gezaehlt wird die UEBERNAHME, nicht der Exit-Code: die Kopie
                # liegt im Temp-Verzeichnis, ihr Schritt
                # `cleanup_unreal_processes.ps1` kann dort gar nicht laufen,
                # und der Exit-Code haengt damit an einer Datei, die mit der
                # Frage nichts zu tun hat.
                uebernahmen = sum(1 for _, aus in ergebnisse if "Lock: gehalten" in aus)
                if uebernahmen >= 2:
                    return
            self.fail(
                "die Sabotage liess in 3 Anlaeufen nie mehr als einen durch - dann "
                "misst der Test oben nichts:\n"
                + "\n---\n".join(a for _, a in ergebnisse))


if __name__ == "__main__":
    unittest.main()
