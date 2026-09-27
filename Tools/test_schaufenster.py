# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""Schaufenster: was aus der Meilenstein-Seite oeffentlich wird - und was nicht.

Die Seite ist oeffentlich (GitHub Pages), das Spiel-Repo privat. Geprueft wird
deshalb vor allem, dass kein Link ins private Repo durchrutscht und dass ein
fehlendes Bild den Lauf abbricht, statt ein kaputtes Schaufenster zu liefern.
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import schaufenster  # noqa: E402

SEITE = """# Meilensteine

Was in *Wiesbaden Real* schon läuft. Neueste zuerst. Zu jedem
Meilenstein gibt es auch ein
[Release auf GitHub](https://github.com/chaotiKKK/Wiesbaden-GTA/releases) mit
denselben Bildern.

> Die Bilder stammen aus Testläufen.

| # | Meilenstein | Zeitraum | Stand |
|---|---|---|---|
| 2 | [Nerobergbahn](#2-nerobergbahn) | 04.–17.09. | fertig |
| 1 | [Wahrzeichen](#1-wahrzeichen) | 07.–26.09. | Shuttle-Denkmal offen |

---

## 2. Nerobergbahn

*04.–17.09.2026*

![Begegnung](meilensteine/bilder/02-a.gif)

Die Bahn fährt <schnell> auf den **Neroberg**.

---

## 1. Wahrzeichen

*07.–26.09.2026*

Marktkirche. *Noch offen:* Shuttle.

| | |
|---|---|
| ![Kirche](meilensteine/bilder/01-a.jpg) | ![Forum](meilensteine/bilder/01-b.jpg) |
"""


class LesenTest(unittest.TestCase):
    def setUp(self):
        self.d = schaufenster.lesen(SEITE)

    def test_satz_mit_link_ins_private_repo_faellt_weg(self):
        einleitung = ' '.join(self.d['einleitung'])
        self.assertNotIn('Wiesbaden-GTA', einleitung)
        self.assertNotIn('Release', einleitung)
        self.assertIn('Neueste zuerst.', einleitung)

    def test_abschnitte_mit_stand_aus_der_tabelle(self):
        a = self.d['abschnitte']
        self.assertEqual([h['nr'] for h in a], [2, 1])
        self.assertEqual(a[0]['stand'], 'fertig')
        self.assertEqual(a[1]['stand'], 'Shuttle-Denkmal offen')
        self.assertEqual(a[0]['datum'], '04.–17.09.2026')

    def test_grosse_bilder_und_tabellenbilder_getrennt(self):
        a = self.d['abschnitte']
        self.assertEqual(a[0]['gross'], [('Begegnung', 'meilensteine/bilder/02-a.gif')])
        self.assertEqual([s for _, s in a[1]['klein']], ['meilensteine/bilder/01-a.jpg', 'meilensteine/bilder/01-b.jpg'])

    def test_hinweis_aus_dem_zitat(self):
        self.assertEqual(self.d['hinweis'], 'Die Bilder stammen aus Testläufen.')


class HtmlTest(unittest.TestCase):
    def test_escaping_und_hervorhebungen(self):
        self.assertEqual(schaufenster.inline('fährt <schnell> auf den **Neroberg**'),
                         'fährt &lt;schnell&gt; auf den <strong>Neroberg</strong>')
        self.assertEqual(schaufenster.inline('*Noch offen:* x'), '<em>Noch offen:</em> x')

    def test_link_ins_private_repo_wird_zu_text(self):
        self.assertEqual(schaufenster.inline('[hier](https://github.com/chaotiKKK/Wiesbaden-GTA/pulls)'), 'hier')

    def test_offener_halt_wird_markiert(self):
        html = schaufenster.seite(schaufenster.lesen(SEITE), 'Stand heute')
        self.assertIn('class="halt offen" id="halt-1"', html)
        self.assertIn('class="halt" id="halt-2"', html)
        self.assertNotIn('Wiesbaden-GTA', html)


class ErzeugenTest(unittest.TestCase):
    def test_fehlendes_bild_bricht_ab(self):
        with tempfile.TemporaryDirectory() as q, tempfile.TemporaryDirectory() as z:
            open(os.path.join(q, '02-a.gif'), 'wb').close()
            with self.assertRaises(RuntimeError):
                schaufenster.erzeugen(SEITE, q, z, 'x')
            self.assertFalse(os.path.exists(os.path.join(z, 'index.html')))

    def test_vollstaendig_nur_html_bilder_readme(self):
        with tempfile.TemporaryDirectory() as q, tempfile.TemporaryDirectory() as z:
            for n in ('02-a.gif', '01-a.jpg', '01-b.jpg'):
                open(os.path.join(q, n), 'wb').close()
            self.assertEqual(schaufenster.erzeugen(SEITE, q, z, 'x'), 3)
            self.assertEqual(sorted(os.listdir(z)), ['.nojekyll', 'README.md', 'bilder', 'index.html'])
            self.assertEqual(sorted(os.listdir(os.path.join(z, 'bilder'))), ['01-a.jpg', '01-b.jpg', '02-a.gif'])


if __name__ == '__main__':
    unittest.main()
