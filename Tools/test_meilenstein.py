# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""meilenstein.py: die reinen Teile - Themen-Datei, Seite, README, Release-Text,
Startzeile des Aufnahmelaufs. Nichts hier ruft git, gh oder das Spiel."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import meilenstein as m  # noqa: E402

SEITE = """# Meilensteine

Einleitung.

| # | Meilenstein | Zeitraum | Stand |
|---|---|---|---|
| 2 | [Nerobergbahn](#2-nerobergbahn) | 04.–17.09. | fertig |
| 1 | [Wahrzeichen](#1-wahrzeichen) | 07.–26.09. | offen |

---

## 2. Nerobergbahn

*04.–17.09.2026*

![Ausweiche](meilensteine/bilder/02-nerobergbahn-ausweiche.gif)

Text zwei.

---

## 1. Wahrzeichen

*07.–26.09.2026*

Text eins.
"""

README = """# Wiesbaden Real

## 🏁 Meilensteine

| Sebbo | Verkehr | Laden |
|---|---|---|
| ![a](docs/meilensteine/bilder/14-a.gif) | ![b](docs/meilensteine/bilder/13-b.gif) | ![c](docs/meilensteine/bilder/11-c.gif) |

Alle 14 Meilensteine mit GIFs und Fotos aus dem Spiel.

## 📖 Dokumentation
"""


def thema(**ueber):
    daten = {'stamm': 'zweiter-hubschrauber', 'titel': 'Zweiter Hubschrauber: Cockpit',
             'zeitraum': '27.09.2026', 'stand': 'fertig', 'commit': 'dac241e',
             'text': '  Absatz eins.\n\nAbsatz zwei.  ',
             'clip': {'goto': '-117159,-118324', 'sekunden': 6, 'gif': {'bis': 4}, 'gif_alt': 'Flug'}}
    daten.update(ueber)
    return m.thema_laden(daten)


class ThemaTest(unittest.TestCase):
    def test_vorgaben_werden_ergaenzt(self):
        t = thema()
        self.assertEqual(t['clip']['fps'], 25)
        self.assertEqual(t['clip']['gif']['bis'], 4)
        self.assertEqual(t['clip']['gif']['breite'], 480)
        self.assertEqual(t['clip']['foto_bei'], 3.0)          # Mitte des Clips
        self.assertEqual(t['text'], 'Absatz eins.\n\nAbsatz zwei.')

    def test_tippfehler_ist_ein_fehler(self):
        with self.assertRaises(m.Abbruch):
            thema(titl='x')
        with self.assertRaises(m.Abbruch):
            thema(clip={'sekunde': 6})
        with self.assertRaises(m.Abbruch):
            thema(clip={'gif': {'breit': 320}})

    def test_pflichtfeld_fehlt(self):
        daten = {'stamm': 'x', 'titel': 'X'}
        with self.assertRaises(m.Abbruch):
            m.thema_laden(daten)

    def test_stamm_nur_klein_und_bindestrich(self):
        with self.assertRaises(m.Abbruch):
            thema(stamm='Zweiter Heli')


class SeiteTest(unittest.TestCase):
    def test_anker_wie_github(self):
        self.assertEqual(m.anker('11. Dennos Laden: Café, Friseur und Lieferungen'),
                         '11-dennos-laden-café-friseur-und-lieferungen')
        self.assertEqual(m.anker('4. Der Käfer: echte Fahrphysik'), '4-der-käfer-echte-fahrphysik')

    def test_naechste_nummer(self):
        self.assertEqual(m.naechste_nummer(SEITE), 3)

    def test_zeile_oben_und_abschnitt_vor_dem_neuesten(self):
        neu = m.seite_einfuegen(SEITE, thema(), 3)
        zeilen = neu.split('\n')
        kopf = zeilen.index('| # | Meilenstein | Zeitraum | Stand |')
        self.assertEqual(zeilen[kopf + 2],
                         '| 3 | [Zweiter Hubschrauber: Cockpit](#3-zweiter-hubschrauber-cockpit) | 27.09.2026 | fertig |')
        self.assertLess(neu.index('## 3. Zweiter Hubschrauber: Cockpit'), neu.index('## 2. Nerobergbahn'))
        self.assertIn('![Flug](meilensteine/bilder/03-zweiter-hubschrauber.gif)', neu)
        self.assertIn('meilensteine/bilder/03-zweiter-hubschrauber.jpg', neu)
        # Der neue Abschnitt ist abgeschlossen: sein Rumpf endet vor Nr. 2.
        titel, rumpf = m.abschnitte(neu)[3]
        self.assertEqual(titel, 'Zweiter Hubschrauber: Cockpit')
        self.assertIn('Absatz zwei.', rumpf)
        self.assertNotIn('Nerobergbahn', rumpf)
        self.assertEqual(sorted(m.abschnitte(neu)), [1, 2, 3])

    def test_doppelte_nummer_bricht_ab(self):
        with self.assertRaises(m.Abbruch):
            m.seite_einfuegen(SEITE, thema(), 2)

    def test_readme_anzahl_und_neuestes_gif_vorn(self):
        neu = m.readme_aktualisieren(README, thema(), 15, 15)
        self.assertIn('Alle 15 Meilensteine', neu)
        self.assertIn('| Zweiter Hubschrauber: Cockpit | Sebbo | Verkehr |', neu)
        self.assertIn('| ![Flug](docs/meilensteine/bilder/15-zweiter-hubschrauber.gif) | ![a](docs/meilensteine/'
                      'bilder/14-a.gif) | ![b](docs/meilensteine/bilder/13-b.gif) |', neu)
        self.assertNotIn('11-c.gif', neu)
        self.assertIn('## 📖 Dokumentation', neu)

    def test_readme_ohne_block_bleibt_unveraendert(self):
        self.assertEqual(m.readme_aktualisieren('# X\n\nText\n', thema(), 15, 15), '# X\n\nText\n')


class ReleaseTest(unittest.TestCase):
    def test_bilder_ueber_blob_main_und_fuss(self):
        rumpf = m.abschnitte(m.seite_einfuegen(SEITE, thema(), 3))[3][1]
        text = m.release_text(rumpf, 'abc123')
        self.assertIn('(https://github.com/chaotiKKK/Wiesbaden-GTA/blob/main/docs/meilensteine/bilder/'
                      '03-zweiter-hubschrauber.gif?raw=true)', text)
        self.assertNotIn('](meilensteine/', text)
        self.assertTrue(text.rstrip().endswith('(https://github.com/chaotiKKK/Wiesbaden-GTA/blob/main/docs/meilensteine.md)'))
        self.assertIn('Stand im Code: abc123', text)

    def test_tag_zweistellig(self):
        self.assertEqual(m.tag_name(3, 'x'), 'meilenstein-03-x')


class AufnahmeTest(unittest.TestCase):
    def test_goto_mit_komma_bleibt_ein_argument(self):
        t = thema(clip={'goto': '-117159,-118324', 'schalter': '-WbZuFuss=20 -WbNoLumen', 'ohne_hud': True})
        args = m.spiel_argumente(t['clip'], 'meilenstein-03-x', 'C:/l.log', 'C:/p.txt', 'C:/W.uproject')
        self.assertEqual(args[0], 'C:/W.uproject')
        self.assertIn('-WbGoto=-117159,-118324', args)
        self.assertIn('-WbClip=meilenstein-03-x', args)
        self.assertIn('-WbClipPoseFile=C:/p.txt', args)
        self.assertIn('-WbClipOhneHud', args)
        self.assertEqual(args[-2:], ['-WbZuFuss=20', '-WbNoLumen'])
        self.assertTrue(any(a.startswith('-WbQuitAfter=') for a in args))

    def test_ohne_pose_und_goto_keine_leeren_schalter(self):
        t = thema(clip={})
        args = m.spiel_argumente(t['clip'], 'n', 'l', '', 'u')
        self.assertFalse(any(a.startswith('-WbGoto') or a.startswith('-WbClipPoseFile') for a in args))

    def test_fotobild_auf_der_clipzeitachse(self):
        info = {'bilder': 150, 'fps': 25, 'muster': 'clip_%05d.jpg'}
        self.assertEqual(m.foto_bild(info, 3.0), 'clip_00075.jpg')
        self.assertEqual(m.foto_bild(info, 99), 'clip_00149.jpg')     # nie hinter das letzte Bild
        self.assertEqual(m.foto_bild(info, -1), 'clip_00000.jpg')


if __name__ == '__main__':
    unittest.main()
