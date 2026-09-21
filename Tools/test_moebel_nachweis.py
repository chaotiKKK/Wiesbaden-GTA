"""Selbsttest des Laufzeit-Nachweises (Tools/moebel_nachweis.py).

Geprueft wird alles, was ohne Engine pruefbar ist: der Nachbau der
Platzierungsregel, die Ortswahl, das Posenformat, die Kennzahlen-Auswertung
und die Startzeile. Kein Spiel, kein Netz, keine 144-MB-Datei.

Aufruf (aus der Projektwurzel):
    python -m unittest Tools.test_moebel_nachweis
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import moebel_nachweis as nw  # noqa: E402


def weg(lat0, lon0, lat1, lon1, typ="residential", tags=None):
    return {"type": "way",
            "geometry": [{"lat": lat0, "lon": lon0}, {"lat": lat1, "lon": lon1}],
            "tags": dict(tags or {}, highway=typ)}


def knoten(lat, lon, schluessel, wert):
    return {"type": "node", "id": 1, "lat": lat, "lon": lon, "tags": {schluessel: wert}}


class BreitenTest(unittest.TestCase):
    def test_strassen_haben_gehweg_wege_nicht(self):
        # Das ist die Ursache fuer 78 % der Verwuerfe: ein Fussweg hat in der
        # Tabelle Gehwegbreite 0, der befestigte Streifen endet nach 0,9 m.
        self.assertEqual(nw.wegbreiten("residential", {}), (2.75, 2.0))
        self.assertEqual(nw.wegbreiten("footway", {}), (0.9, 0.0))

    def test_gemessene_breite_schlaegt_die_rechnung(self):
        halb, _ = nw.wegbreiten("residential", {"width": "8"})
        self.assertEqual(halb, 4.0)

    def test_unsinnige_breite_wird_verworfen(self):
        halb, _ = nw.wegbreiten("residential", {"width": "999"})
        self.assertEqual(halb, 2.75)
        halb, _ = nw.wegbreiten("residential", {"width": "breit"})
        self.assertEqual(halb, 2.75)


class RegelTest(unittest.TestCase):
    def test_am_rand_bleibt_stehen(self):
        # Wohnstrasse: befestigt bis 2,75 + 2,0 = 4,75 m ab Achse.
        self.assertTrue(nw.ueberlebt("bench", (4.0, 2.75, 2.0, "residential")))

    def test_knapp_daneben_wird_angedockt(self):
        self.assertTrue(nw.ueberlebt("bench", (6.0, 2.75, 2.0, "residential")))

    def test_zu_weit_weg_faellt_raus(self):
        self.assertFalse(nw.ueberlebt("bench", (8.0, 2.75, 2.0, "residential")))

    def test_am_fussweg_traegt_das_bankett(self):
        # Kalibrierung vom 21.09.2026: befestigt bis 0,9 + 2,5 m Bankett,
        # plus 2,5 m Reichweite -> erst ab 5,9 m ist Schluss. Vorher endete
        # es bei 2,4 m, und dieser Test hat genau das festgehalten, nachdem
        # das C++ laengst weiter war.
        self.assertTrue(nw.ueberlebt("bench", (2.5, 0.9, 0.0, "footway")))
        self.assertTrue(nw.ueberlebt("bench", (5.8, 0.9, 0.0, "footway")))
        self.assertFalse(nw.ueberlebt("bench", (6.0, 0.9, 0.0, "footway")))

    def test_bankett_typen_decken_das_cpp_ab(self):
        # Die Liste steht im C++ (WbBankettErlaubt) ein zweites Mal. Beim
        # Uebertragen fiel living_street heraus - eine Spielstrasse hat
        # keinen eigenen Gehweg, begangen wird sie trotzdem. Der Nachbau
        # verwarf dort Moebel, die der Bake stehen laesst.
        self.assertIn("living_street", nw.BANKETT_TYPEN)
        self.assertTrue(nw.ueberlebt("bench", (5.0, 0.9, 0.0, "living_street")))

    def test_das_bankett_gilt_NICHT_auf_der_autobahn(self):
        # Eine Autobahn hat auch keinen Gehweg - aber dort steht kein
        # begehbarer Streifen, sondern die Standspur. Die Begruendung des
        # Banketts traegt hier nicht.
        self.assertFalse(nw.ueberlebt("bench", (10.5, 7.5, 0.0, "motorway")))

    def test_poller_duerfen_ueberall_stehen(self):
        self.assertTrue(nw.ueberlebt("bollard", (50.0, 2.75, 2.0, "residential")))

    def test_ohne_weg_in_reichweite_faellt_alles_raus(self):
        self.assertFalse(nw.ueberlebt("bench", None))

    def test_groessere_reichweite_rettet_mehr(self):
        # An einer Strasse MIT Gehweg: dort greift das Bankett nicht, also
        # misst dieser Fall allein die Reichweite. Der frueher benutzte
        # Fussweg taugt dafuer nicht mehr - dort wirken seit dem 21.09.2026
        # zwei Hebel, und ein Test, der zwei Dinge gleichzeitig bewegt, sagt
        # ueber keines davon etwas.
        fall = (6.8, 2.75, 2.0, "residential")     # befestigt bis 4,75 m
        self.assertFalse(nw.ueberlebt("bench", fall, reichweite_m=1.5))
        self.assertTrue(nw.ueberlebt("bench", fall, reichweite_m=2.6))


class OrtswahlTest(unittest.TestCase):
    def setUp(self):
        # Eine Wohnstrasse entlang des Aequators der Stadt.
        self.netz = nw.Wegnetz([weg(50.0824, 8.2400, 50.0824, 8.2450)])

    def _bank(self, meter_neben_der_achse, lon=8.2410):
        lat = 50.0824 - meter_neben_der_achse / 110574.0
        return knoten(lat, lon, "amenity", "bench")

    def test_waehlt_den_ort_mit_den_meisten_ueberlebenden(self):
        knoten_liste = [self._bank(3.0, 8.2410), self._bank(3.0, 8.24101),
                        self._bank(3.0, 8.24102), self._bank(3.0, 8.2440)]
        ort = nw.ort_waehlen(knoten_liste, self.netz, "bench", radius_m=10.0)
        self.assertIsNotNone(ort)
        self.assertEqual(ort["gleiche_art_im_umkreis"], 3)
        self.assertEqual(ort["wegtyp"], "residential")

    def test_verworfene_moebel_werden_nicht_zum_fotoort(self):
        # 30 m neben der Achse - das ueberlebt die Regel nicht.
        ort = nw.ort_waehlen([self._bank(30.0)], self.netz, "bench")
        self.assertIsNone(ort)

    def test_andere_arten_zaehlen_nicht(self):
        # 3 m neben der Achse: ueberlebt die Regel, ist aber kein Bank-Ort.
        korb_lat = 50.0824 - 3.0 / 110574.0
        koerbe = [knoten(korb_lat, 8.2410, "amenity", "waste_basket")]
        self.assertIsNone(nw.ort_waehlen(koerbe, self.netz, "bench"))
        self.assertIsNotNone(nw.ort_waehlen(koerbe, self.netz, "waste_basket"))

    def test_unsichtbare_kante_wird_uebersprungen(self):
        # An einem Feldweg steht die Bank zwar regelkonform, aber die Kante
        # ist im Bild nicht zu sehen - als Fotoort taugt sie nicht.
        feldweg = nw.Wegnetz([weg(50.0824, 8.2400, 50.0824, 8.2450, "track")])
        bank = [knoten(50.08239, 8.2410, "amenity", "bench")]
        self.assertIsNone(nw.ort_waehlen(bank, feldweg, "bench"))
        self.assertIsNotNone(nw.ort_waehlen(bank, feldweg, "bench",
                                            nur_sichtbare_kante=False))


class PosenTest(unittest.TestCase):
    def test_format_und_augenhoehe(self):
        text = nw.posen_text(-1652, 39545, hoehe_m=1.7, abstand_m=6.0)
        zeilen = [z for z in text.splitlines() if z and not z.startswith("#")]
        self.assertEqual(len(zeilen), 5)          # vier Richtungen + Uebersicht
        for zeile in zeilen[:4]:
            felder = [f.strip() for f in zeile.split(",")]
            self.assertEqual(len(felder), 8)
            self.assertEqual(felder[0], "1.7")
            self.assertEqual(felder[1], "-1652")
            self.assertEqual(felder[2], "39545")

    def test_blick_zeigt_zurueck_auf_das_motiv(self):
        text = nw.posen_text(0, 0, richtungen=(0, 90))
        zeilen = [z for z in text.splitlines() if z and not z.startswith("#")]
        for zeile in zeilen[:2]:
            felder = [f.strip() for f in zeile.split(",")]
            yaw, blick = int(felder[3]), int(felder[6])
            self.assertEqual((yaw + 180) % 360, blick)

    def test_uebersicht_steht_hoeher_und_weiter(self):
        zeilen = [z for z in nw.posen_text(0, 0, 1.7, 6.0).splitlines()
                  if z and not z.startswith("#")]
        oben = [f.strip() for f in zeilen[-1].split(",")]
        self.assertGreater(float(oben[0]), 1.7)
        self.assertGreater(float(oben[5]), 6.0)


class KennzahlenTest(unittest.TestCase):
    LOG = (
        "[2026.09.20-13.43.33:124][645]LogWbRoads: Strassenmoebel: 2012 von 3607 "
        "OSM-Knoten uebernommen (1357 angedockt, 131 im Gebaeude verworfen, "
        "1464 ohne befestigten Rand verworfen).\n"
        "[2026.09.20-13.43.38:945][286]LogWbCore: Strassenmoebel gestellt: 2012 Moebel, "
        "4506 Teil-Instanzen in 5 Zeichengruppen (Baenke 527, Poller 711).\n"
        "[2026.09.20-13.50.54:947][580]LogWbStreaming: Bildzeit (2402 Bilder): "
        "Mittel 6.2 ms (160 Bilder/s), schlechtestes 39.9 ms.\n")

    def test_liest_alle_zahlen(self):
        w = nw.kennzahlen_lesen(self.LOG)
        self.assertEqual(w["uebernommen"], 2012)
        self.assertEqual(w["osm_knoten"], 3607)
        self.assertEqual(w["angedockt"], 1357)
        self.assertEqual(w["verworfen_gebaeude"], 131)
        self.assertEqual(w["verworfen_ohne_rand"], 1464)
        self.assertEqual(w["gestellte_moebel"], 2012)
        self.assertEqual(w["teil_instanzen"], 4506)
        self.assertEqual(w["zeichengruppen"], 5)
        self.assertEqual(w["bildzeit_ms"], 6.2)
        self.assertEqual(w["bilder_je_s"], 160)

    def test_leeres_log_ergibt_keine_zahlen_statt_absturz(self):
        self.assertEqual(nw.kennzahlen_lesen(""), {})

    def test_letzte_bildzeit_gewinnt(self):
        w = nw.kennzahlen_lesen(self.LOG + "Mittel 9.9 ms (101 Bilder/s)\n")
        self.assertEqual(w["bilder_je_s"], 101)


class StartzeileTest(unittest.TestCase):
    def test_argumente_bleiben_am_komma_heil(self):
        # Die Falle: ueber eine .cmd wuerde -WbGoto=-100,200 an zwei Argumente
        # zerfallen. Als Liste bleibt es ein Token.
        args = nw.engine_argumente("C:/posen.txt", -100, 200)
        self.assertIn("-WbGoto=-100,200", args)

    def test_kollision_und_osm_werden_ueberschrieben(self):
        args = " ".join(nw.engine_argumente("p.txt", 0, 0))
        self.assertIn("bCreateCollision=True", args)
        self.assertIn("OsmFilePath=" + nw.OSM_MOEBEL, args)

    def test_startet_die_ungebackene_karte(self):
        # Nur dort laeuft der Laufzeit-Bau und damit SpawnFurniture.
        self.assertIn(nw.KARTE, nw.engine_argumente("p.txt", 0, 0))

    def test_default_karte_wird_nicht_angefasst(self):
        args = " ".join(nw.engine_argumente("p.txt", 0, 0))
        self.assertNotIn("Alkis", args)


if __name__ == "__main__":
    unittest.main()
