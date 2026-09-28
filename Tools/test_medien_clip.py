# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""medien.py clip: aus einem -WbClip-Ordner den richtigen ffmpeg-Aufruf bauen.

Der Clip-Modus des Spiels schreibt Bilder + clip.json; hier wird geprueft,
dass Bildrate und Muster aus der clip.json kommen (nicht geraten werden) und
dass --von/--bis Sekunden auf der Zeitachse des Clips sind.
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import medien  # noqa: E402


class ClipBefehlTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.ordner = self.tmp.name
        with open(os.path.join(self.ordner, 'clip.json'), 'w', encoding='utf-8') as f:
            json.dump({'name': 'probe', 'fps': 24, 'bilder': 48, 'soll': 48, 'vollstaendig': True,
                       'muster': 'clip_%05d.jpg'}, f)

    def tearDown(self):
        self.tmp.cleanup()

    def test_bildrate_und_muster_aus_der_clip_json(self):
        cmd, info = medien.clip_befehl(self.ordner, 'x.gif')
        i = cmd.index('-framerate')
        self.assertEqual(cmd[i + 1], '24')
        self.assertEqual(cmd[cmd.index('-i') + 1], os.path.join(self.ordner, 'clip_%05d.jpg'))
        self.assertTrue(info['vollstaendig'])

    def test_gif_mit_eigener_palette_und_schnitt_in_sekunden(self):
        cmd, _ = medien.clip_befehl(self.ordner, 'x.gif', von=1.5, bis=4, fps=10, breite=320,
                                    crop='960:400:0:320')
        filt = cmd[cmd.index('-filter_complex') + 1]
        self.assertTrue(filt.startswith('trim=start=1.5:end=4,setpts=PTS-STARTPTS,crop=960:400:0:320,'))
        self.assertIn('fps=10,scale=320:-1', filt)
        self.assertIn('palettegen', filt)
        self.assertEqual(cmd[-1], 'x.gif')

    def test_ohne_schnitt_kein_trim(self):
        cmd, _ = medien.clip_befehl(self.ordner, 'x.gif')
        self.assertNotIn('trim=', cmd[cmd.index('-filter_complex') + 1])

    def test_mp4_mit_gerader_hoehe_fuer_yuv420p(self):
        cmd, _ = medien.clip_befehl(self.ordner, 'x.MP4', breite=640)
        self.assertIn('libx264', cmd)
        self.assertIn('yuv420p', cmd)
        self.assertIn('scale=640:-2:flags=lanczos', cmd[cmd.index('-vf') + 1])
        self.assertNotIn('-filter_complex', cmd)

    def test_ohne_clip_json_kein_raten(self):
        with tempfile.TemporaryDirectory() as leer:
            with self.assertRaises(FileNotFoundError):
                medien.clip_befehl(leer, 'x.gif')


if __name__ == '__main__':
    unittest.main()
