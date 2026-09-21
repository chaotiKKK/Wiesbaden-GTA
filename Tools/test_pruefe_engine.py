r"""Selbsttest des Engine-Waechters (Tools/pruefe_engine.py).

Ein Waechter, der nichts findet, sieht genauso aus wie einer, der nichts zu
finden hat. Beim Bauen dieses Waechters ist genau das DREIMAL passiert:

1. Die Zeichenklasse fuer den Trenner verlor auf dem Weg durch die Shell eine
   Backslash-Ebene und passte danach nur noch auf Vorwaerts-Schraegstriche -
   kein einziger Windows-Pfad wurde gefunden, Meldung: alles in Ordnung.
2. Die Wortgrenze hinter REM wurde zu einem echten Backspace-Byte (0x08).
   grep zeigt es als "REM" an; keine Kommentarzeile passte mehr.
3. Der normalisierte Pfad behielt doppelte Trenner ("C://Program Files//"),
   worauf der Waechter die KANONISCHE Engine als Abweichler meldete.

Darum prueft dieser Test nicht "laeuft durch", sondern: FINDET er eine
bekannte Abweichung, und laesst er das Richtige in Ruhe.

    python -m unittest discover -s Tools -p "test_pruefe_engine.py"
"""
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import engine            # noqa: E402
import pruefe_engine as pe   # noqa: E402


class MusterTest(unittest.TestCase):
    """Das Muster muss Pfade in JEDER Schreibweise finden."""

    def _findet(self, zeile):
        return pe.MUSTER.findall(zeile.replace(chr(92), "/"))

    def test_findet_windows_pfad_mit_backslash(self):
        """Der Fehler Nr. 1: die Klasse passte nur auf Schraegstriche."""
        zeile = '"C:' + chr(92) + 'Program Files' + chr(92) + 'Epic Games' \
                + chr(92) + 'UE_5.8' + chr(92) + 'Engine" -bauen'
        self.assertTrue(self._findet(zeile), "Windows-Pfad wurde nicht gefunden")

    def test_findet_pfad_mit_schraegstrich(self):
        self.assertTrue(self._findet("UE=C:/freebuff/WiesbadenReal_Sicherung/UE_5.8/Engine"))

    def test_findet_doppelt_geschriebenen_pfad(self):
        """JS- und Shell-Quelltext schreiben den Backslash doppelt."""
        doppelt = "C:" + chr(92) * 2 + "Program Files" + chr(92) * 2 + "Epic Games" \
                  + chr(92) * 2 + "UE_5.8"
        self.assertTrue(self._findet("['" + doppelt + "']"))

    def test_leere_zeile_findet_nichts(self):
        self.assertEqual(self._findet("nur text ohne pfad"), [])


class NormTest(unittest.TestCase):
    """Fehler Nr. 3: doppelte Trenner machten aus richtig falsch."""

    def test_doppelte_trenner_zaehlen_wie_einfache(self):
        einfach = pe._norm("C:/Program Files/Epic Games/UE_5.8")
        doppelt = pe._norm("C://Program Files//Epic Games//UE_5.8")
        backslash = pe._norm("C:" + chr(92) + "Program Files" + chr(92)
                             + "Epic Games" + chr(92) + "UE_5.8")
        self.assertEqual(einfach, doppelt)
        self.assertEqual(einfach, backslash)

    def test_gross_klein_ist_egal(self):
        self.assertEqual(pe._norm("C:/PROGRAM FILES/X"), pe._norm("c:/program files/x"))


class KommentarTest(unittest.TestCase):
    """Fehler Nr. 2: das unsichtbare Backspace-Byte hinter REM."""

    def test_rem_gilt_als_kommentar(self):
        self.assertTrue(pe.ist_kommentar("REM   C:/x/UE_5.8"))
        self.assertTrue(pe.ist_kommentar("   rem eingerueckt"))

    def test_remove_ist_KEIN_kommentar(self):
        """REM darf nicht jedes Wort schlucken, das so anfaengt."""
        self.assertFalse(pe.ist_kommentar("REMOVE_ENGINE=C:/x/UE_5.8"))

    def test_weitere_kommentarzeichen(self):
        for z in ("# raute", "// doppelstrich", ":: cmd-marke", "-- strich", "<!-- html"):
            self.assertTrue(pe.ist_kommentar(z), z)

    def test_code_ist_kein_kommentar(self):
        self.assertFalse(pe.ist_kommentar('"C:/x/UE_5.8/Engine/Build.bat" bauen'))


class AbweichlerTest(unittest.TestCase):
    """Der Kern: findet der Waechter eine ECHTE Abweichung?"""

    def setUp(self):
        self.wurzel = Path(tempfile.mkdtemp(prefix="wb_engine_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)
        self._alt = pe.WURZEL
        pe.WURZEL = str(self.wurzel)
        self.addCleanup(lambda: setattr(pe, "WURZEL", self._alt))

    def schreibe(self, name, inhalt):
        pfad = self.wurzel / name
        pfad.parent.mkdir(parents=True, exist_ok=True)
        pfad.write_text(inhalt, encoding="utf-8")
        return name

    def test_eine_fremde_engine_faellt_auf(self):
        self.schreibe("bau.cmd",
                      '"C:/freebuff/WiesbadenReal_Sicherung/UE_5.8/Engine/Build.bat" x\n')
        treffer = pe.abweichler(["bau.cmd"])
        self.assertEqual(len(treffer), 1, "die fremde Engine wurde nicht gemeldet")
        self.assertEqual(treffer[0][0], "bau.cmd")

    def test_backslash_pfad_im_ECHTEN_weg(self):
        """Fehler Nr. 1 - und dieser Test hat ihn zuerst NICHT gefangen.

        Die erste Fassung normalisierte die Zeile im Test SELBST und pruefte
        damit nur das Muster, nicht den Weg, den der Waechter wirklich geht.
        Nimmt man dem Waechter die Normalisierung weg, blieb der Test gruen.
        Hier steht der Pfad darum in echter Windows-Schreibweise, und der
        Aufruf geht durch abweichler() - genau wie im Betrieb.
        """
        B = chr(92)
        pfad = "C:" + B + "freebuff" + B + "WiesbadenReal_Sicherung" + B + "UE_5.8"
        self.schreibe("win.cmd", chr(34) + pfad + B + "Engine" + B + "Build.bat"
                      + chr(34) + " bauen" + chr(10))
        self.assertEqual(len(pe.abweichler(["win.cmd"])), 1,
                         "Windows-Pfad mit Backslash wurde nicht gemeldet")

    def test_die_kanonische_engine_faellt_NICHT_auf(self):
        self.schreibe("gut.cmd", '"' + engine.KANONISCH + '/Engine/Build.bat" x\n')
        self.assertEqual(pe.abweichler(["gut.cmd"]), [])

    def test_im_kommentar_darf_sie_stehen(self):
        self.schreibe("warn.cmd",
                      "REM die Kopie C:/freebuff/WiesbadenReal_Sicherung/UE_5.8 nicht nehmen\n")
        self.assertEqual(pe.abweichler(["warn.cmd"]), [])

    def test_die_zeilennummer_stimmt(self):
        self.schreibe("mehr.cmd", "@echo off\nREM nichts\n"
                      '"C:/freebuff/WiesbadenReal_Sicherung/UE_5.8/Engine/x" y\n')
        treffer = pe.abweichler(["mehr.cmd"])
        self.assertEqual(treffer[0][1], 3)


class KanonischTest(unittest.TestCase):
    """Die kanonische Engine muss existieren und zum .uproject passen."""

    def test_sie_ist_da_und_passt(self):
        ok, meldung = engine.pruefen()
        self.assertTrue(ok, meldung)

    def test_eine_falsche_wurzel_wird_abgewiesen(self):
        ok, meldung = engine.pruefen(os.path.join(tempfile.gettempdir(), "gibtesnicht"))
        self.assertFalse(ok)
        self.assertIn("keine Engine", meldung)

    def test_der_ordnername_entscheidet_nicht(self):
        """Beide Engines heissen "UE_5.8" - erst die Patch-Nummer trennt sie."""
        version = engine.build_version()
        self.assertIsNotNone(version, "Build.version nicht lesbar")
        self.assertEqual(version[:2], engine.uproject_erwartung())

    def test_ausnahmen_haben_alle_eine_begruendung(self):
        for datei, grund in pe.AUSNAHMEN.items():
            self.assertTrue(grund.strip(), "Ausnahme ohne Grund: %s" % datei)


if __name__ == "__main__":
    unittest.main()
