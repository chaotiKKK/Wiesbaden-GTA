"""Tests fuer Gate 7 (Tools/verify_fahrphysik.py): Sollwerte der Fahrphysik.

Ohne Engine: die Kennzahlen kommen als Wertetabelle oder als erzeugtes
Telemetrie-Log. Jeder bekannte Rueckfall (vor der Grip-Kalibrierung, mit
820 kg) muss ROT werden, der kalibrierte Stand GRUEN, ein unvollstaendiger
Lauf NICHT GEMESSEN.

    python -m unittest Tools.test_verify_fahrphysik -v
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import verify_fahrphysik as vf  # noqa: E402

# Gemessen am 29.09.2026 (Fahrmessung Wiese, fahrmessung_auswerten.py).
KALIBRIERT = {  # masse_nachher1: 970 kg, Grip + Bremse kalibriert
    "Radspin im Anfahren [s]": 0,
    "0-60 mph (96,6 km/h) [s]": 18.27,
    "Querbeschl. max rechts [g]": 0.72,
    "Querbeschl. max links [g]": 0.71,
    "Bremsweg auf 100 km/h normiert [m]": 53.47,
    "mittl. Verzoegerung [g]": 0.73,
    "Raeder gleiten beim Bremsen [%]": 0.0,
}
VOR_GRIP = dict(KALIBRIERT, **{  # feder_nachher1: mu 0,75, Bremse 5600 N
    "Radspin im Anfahren [s]": 2.1,
    "0-60 mph (96,6 km/h) [s]": 14.53,
    "Bremsweg auf 100 km/h normiert [m]": 60.33,
    "mittl. Verzoegerung [g]": 0.64,
})
MIT_820_KG = dict(KALIBRIERT, **{"0-60 mph (96,6 km/h) [s]": 15.44})


class Kennzahlen(unittest.TestCase):
    def test_kalibrierter_stand_ist_gruen(self):
        code, zeilen = vf.pruefe_kennzahlen(KALIBRIERT)
        self.assertEqual(code, vf.GRUEN, "\n".join(zeilen))

    def test_stand_vor_der_grip_kalibrierung_ist_rot(self):
        code, zeilen = vf.pruefe_kennzahlen(VOR_GRIP)
        self.assertEqual(code, vf.ROT)
        rot = [z for z in zeilen if "[ROT]" in z]
        for name in ("Radspin", "0-60 mph", "Bremsweg", "Verzoegerung"):
            self.assertTrue(any(name in z for z in rot), "%s nicht rot: %s" % (name, rot))

    def test_820_kg_sind_rot(self):
        code, zeilen = vf.pruefe_kennzahlen(MIT_820_KG)
        self.assertEqual(code, vf.ROT)
        rot = [z for z in zeilen if "[ROT]" in z]
        self.assertEqual(len(rot), 1, rot)
        self.assertIn("0-60 mph", rot[0])

    def test_jede_kennzahl_hat_ein_band_das_greift(self):
        """Jede Sollgroesse faellt allein durch, wenn sie weit daneben liegt."""
        for name, unten, oben, _ in vf.SOLLWERTE:
            k = dict(KALIBRIERT)
            k[name] = oben * 1.5 + 1.0
            self.assertEqual(vf.pruefe_kennzahlen(k)[0], vf.ROT, "%s zu hoch nicht rot" % name)
            if unten is not None:
                k[name] = unten * 0.5
                self.assertEqual(vf.pruefe_kennzahlen(k)[0], vf.ROT, "%s zu tief nicht rot" % name)

    def test_fehlende_kennzahl_ist_nicht_gemessen(self):
        k = dict(KALIBRIERT)
        k["0-60 mph (96,6 km/h) [s]"] = None   # Fahrt erreichte 60 mph nie
        self.assertEqual(vf.pruefe_kennzahlen(k)[0], vf.NICHT_GEMESSEN)
        del k["0-60 mph (96,6 km/h) [s]"]
        self.assertEqual(vf.pruefe_kennzahlen(k)[0], vf.NICHT_GEMESSEN)


class Logdatei(unittest.TestCase):
    def schreibe(self, zeilen):
        f = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False, encoding="utf-8")
        f.write("\n".join(zeilen) + "\n")
        f.close()
        self.addCleanup(os.unlink, f.name)
        return f.name

    def test_fehlendes_log_ist_nicht_gemessen(self):
        code, _ = vf.pruefe_log(os.path.join(tempfile.gettempdir(), "gibt_es_nicht_7.log"))
        self.assertEqual(code, vf.NICHT_GEMESSEN)

    def test_abgebrochener_lauf_ist_nicht_gemessen(self):
        """Ohne 'Messlauf beendet' zaehlt auch ein langes Log nicht."""
        zeile = ("LogWbVehicles: WbFahrt t=%.2f v=10.00 ax=0.00 ay=0.00 gier=0.00 schwimm=0.00 "
                 "lenk=0.000 gas=1.00 bremse=0.00 nick=0.00 wank=0.00 hub=0.00 spin=0 block=0 "
                 "gleit=0 gang=1 boden=0.0 spalt=0.00 fz=0.00")
        pfad = self.schreibe([zeile % (i / 10.0) for i in range(1000)])
        self.assertEqual(vf.pruefe_log(pfad)[0], vf.NICHT_GEMESSEN)

    def test_zu_wenige_proben_sind_nicht_gemessen(self):
        pfad = self.schreibe(["LogWbVehicles: WbFahrt t=0.10 v=1.00 ax=1.00 gas=1.00",
                              "LogWbStreaming: Messlauf beendet nach 120 Sekunden (-WbQuitAfter)."])
        self.assertEqual(vf.pruefe_log(pfad)[0], vf.NICHT_GEMESSEN)

    def test_das_ergebnis_steht_in_der_datei(self):
        with tempfile.TemporaryDirectory() as tmp:
            alt = vf.ERGEBNIS
            vf.ERGEBNIS = os.path.join(tmp, "fahrphysik_gate.txt")
            try:
                code = vf.main(["x", os.path.join(tmp, "fehlt.log")])
                with open(vf.ERGEBNIS, encoding="utf-8") as f:
                    text = f.read()
            finally:
                vf.ERGEBNIS = alt
        self.assertEqual(code, vf.NICHT_GEMESSEN)
        self.assertIn("nicht gemessen", text)


if __name__ == "__main__":
    unittest.main()
