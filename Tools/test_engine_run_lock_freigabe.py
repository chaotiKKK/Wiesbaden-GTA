"""engine_run_lock.ps1 -Modus Freigeben: nur der Lauf, der die Sperre nahm, gibt sie frei.

GEMESSEN am 29.09.2026: im Push-Lauf haelt der Hook (Python) die Sperre ab
Gate 0. Gate 4 (run_cut_shots.cmd) ruft darunter Start + Freigeben - und
Freigeben loeschte die Sperre, weil der Besitzer zur eigenen Prozesskette
gehoerte. Alle Gates danach liefen ohne Sperre; ein paralleler Lauf haette
dazwischen starten und Editoren beenden koennen.

Echte Aufrufe des Skripts, aber nur -Modus Nehmen/Freigeben mit eigener
Sperrdatei (-LockPfad) - kein Start, also keine Prozessbereinigung.

    python -m unittest Tools.test_engine_run_lock_freigabe -v
"""
import os
import subprocess
import tempfile
import unittest

SKRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "engine_run_lock.ps1")


def ps(modus, pfad, *mehr):
    return ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", SKRIPT,
            "-Modus", modus, "-LockPfad", pfad, "-Name", "freigabe_test"] + list(mehr)


def cmd_zeile(befehl):
    return " ".join('"%s"' % t if " " in t else t for t in befehl)


class Freigabe(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.mkdtemp()
        self.pfad = os.path.join(tmp, "test_engine_run.lock")
        self.addCleanup(lambda: os.path.exists(self.pfad) and os.remove(self.pfad))

    def nehmen(self):
        """Sperre nehmen - Besitzer ist DIESER Python-Prozess (der Elternprozess)."""
        r = subprocess.run(ps("Nehmen", self.pfad), capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertTrue(os.path.exists(self.pfad))

    def test_ein_unterlauf_gibt_die_sperre_des_umschliessenden_nicht_frei(self):
        """Wie Gate 4 im Push: cmd unter dem Besitzer ruft Freigeben."""
        self.nehmen()
        r = subprocess.run(["cmd", "/c", cmd_zeile(ps("Freigeben", self.pfad))],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertTrue(os.path.exists(self.pfad),
                        "der Unterlauf hat die Sperre des umschliessenden Laufs geloescht:\n"
                        + r.stdout)

    def test_der_besitzer_selbst_gibt_frei(self):
        self.nehmen()
        r = subprocess.run(ps("Freigeben", self.pfad), capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertFalse(os.path.exists(self.pfad), r.stdout)

    def test_ein_wrapper_gibt_seine_eigene_sperre_frei(self):
        """Wie jedes .cmd ausserhalb des Push: Start/Nehmen und Freigeben im selben cmd."""
        zeile = cmd_zeile(ps("Nehmen", self.pfad)) + " & " + cmd_zeile(ps("Freigeben", self.pfad))
        r = subprocess.run(["cmd", "/c", zeile], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertFalse(os.path.exists(self.pfad),
                         "der Wrapper konnte seine eigene Sperre nicht freigeben:\n" + r.stdout)

    def test_gewalt_gibt_auch_die_umschliessende_frei(self):
        self.nehmen()
        r = subprocess.run(["cmd", "/c", cmd_zeile(ps("Freigeben", self.pfad, "-Gewalt"))],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertFalse(os.path.exists(self.pfad), r.stdout)


if __name__ == "__main__":
    unittest.main()
