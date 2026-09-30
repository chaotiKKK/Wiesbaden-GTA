"""Tests fuer Tools/fahrmessung_auswerten.py: Beschleunigungszeiten aus dem Stand."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import fahrmessung_auswerten as fa


def probe(t, v, ax, gas=1.0):
    return {"t": t, "v": v, "ax": ax, "gas": gas}


class ZeitAusDemStand(unittest.TestCase):
    def test_start_wird_zurueckgerechnet(self):
        # Gleichmaessig 2 m/s^2 ab t=0,15; die erste Gaszeile (t=0,55) faehrt
        # schon 2,88 km/h - gemessen wird trotzdem ab dem Stillstand.
        proben = [probe(0.15, 0.0, 0.0, gas=0.0)]
        t = 0.55
        while t < 20.0:
            proben.append(probe(t, (t - 0.15) * 2.0 * 3.6, 2.0))
            t += 0.1
        start = fa.erste(proben, lambda p: p["gas"] > 0.5)
        # 50 km/h = 13,89 m/s -> 6,94 s bei 2 m/s^2
        self.assertAlmostEqual(fa.zeit_aus_dem_stand(proben, start, 50.0), 50.0 / 3.6 / 2.0, places=2)

    def test_rueckrechnung_hoechstens_eine_sekunde(self):
        """Rollt der Wagen an der ersten Gaszeile schon (Gefaelle), erfindet
        v/ax sonst Sekunden: 3 km/h bei 0,15 m/s2 waeren 5,6 s."""
        proben = [probe(1.0, 3.0, 0.15), probe(1.1, 3.1, 0.15), probe(1.2, 60.0, 5.0)]
        zeit = fa.zeit_aus_dem_stand(proben, 0, 50.0)
        self.assertLess(zeit, 2.5, "Start %.1f s zurueckgerechnet" % zeit)

    def test_nie_erreicht(self):
        proben = [probe(0.5, 5.0, 3.0), probe(0.6, 6.0, 3.0)]
        self.assertIsNone(fa.zeit_aus_dem_stand(proben, 0, 100.0))


if __name__ == "__main__":
    unittest.main()
