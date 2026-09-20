"""Selbsttest des Moebel-Nachzugs (Tools/fetch_street_furniture.py).

Geprueft wird die reine Logik - Whitelist, Schema, unbekannte Arten und das
Einmischen. Kein Netz, keine 144-MB-Datei.

Aufruf (aus der Projektwurzel):
    python -m unittest discover -s Tools -p "test_street_furniture.py"
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fetch_street_furniture as moebel  # noqa: E402


def knoten(node_id, tags, lat=50.08, lon=8.24):
    return {"type": "node", "id": node_id, "lat": lat, "lon": lon, "tags": tags}


class ArtBestimmungTest(unittest.TestCase):
    def test_alle_acht_kategorien_werden_erkannt(self):
        erwartet = {
            "bench": {"amenity": "bench"},
            "bollard": {"barrier": "bollard"},
            "waste_basket": {"amenity": "waste_basket"},
            "vending_machine": {"amenity": "vending_machine"},
            "recycling": {"amenity": "recycling"},
            "fire_hydrant": {"emergency": "fire_hydrant"},
            "post_box": {"amenity": "post_box"},
            "picnic_table": {"leisure": "picnic_table"},
        }
        self.assertEqual(len(erwartet), len(moebel.KATEGORIEN))
        for art, tags in erwartet.items():
            with self.subTest(art=art):
                self.assertEqual(moebel.art_bestimmen(tags), art)

    def test_fremde_und_leere_tags_ergeben_keine_art(self):
        for tags in ({}, None, {"amenity": "restaurant"}, {"highway": "street_lamp"},
                     {"barrier": "gate"}, {"amenity": "bench_shop"}):
            with self.subTest(tags=tags):
                self.assertIsNone(moebel.art_bestimmen(tags))

    def test_mehrfach_getaggter_knoten_bekommt_die_erste_kategorie(self):
        # Ein Automat am Recycling-Container: beide Tags stehen am selben Knoten.
        art = moebel.art_bestimmen({"amenity": "vending_machine", "barrier": "bollard"})
        self.assertEqual(art, "bollard", "Reihenfolge der KATEGORIEN entscheidet")


class MoebelKnotenTest(unittest.TestCase):
    def test_schema_traegt_nur_die_art(self):
        # Material, Farbe und Bauart wanderten frueher mit in die 145-MB-Datei,
        # fuer eine Variantenwahl, die es nicht gibt. Was kein Leser anfasst,
        # gehoert nicht in die Datei.
        gefiltert, zaehler, verworfen = moebel.moebel_knoten([
            knoten(7, {"amenity": "bench", "material": "wood", "operator": "Stadt"}),
        ])
        self.assertEqual(verworfen, 0)
        self.assertEqual(zaehler, {"bench": 1})
        self.assertEqual(gefiltert[0], {
            "type": "node", "id": 7, "lat": 50.08, "lon": 8.24,
            "tags": {moebel.ART_TAG: "bench", "amenity": "bench"},
        })

    def test_unbekannte_arten_werden_verworfen_und_gezaehlt(self):
        gefiltert, zaehler, verworfen = moebel.moebel_knoten([
            knoten(1, {"amenity": "bench"}),
            knoten(2, {"amenity": "cafe"}),
            {"type": "way", "id": 3, "tags": {"amenity": "bench"}},
            {"type": "node", "id": 4, "tags": {"amenity": "bench"}},  # ohne lat/lon
        ])
        self.assertEqual([k["id"] for k in gefiltert], [1])
        self.assertEqual(zaehler, {"bench": 1})
        self.assertEqual(verworfen, 3)

    def test_reihenfolge_ist_deterministisch(self):
        roh = [knoten(i, {"barrier": "bollard"}) for i in (9, 3, 42, 7)]
        erster, _, _ = moebel.moebel_knoten(roh)
        zweiter, _, _ = moebel.moebel_knoten(list(reversed(roh)))
        self.assertEqual([k["id"] for k in erster], [3, 7, 9, 42])
        self.assertEqual(erster, zweiter)


class EinmischenTest(unittest.TestCase):
    def test_neue_knoten_kommen_dazu(self):
        elemente = [knoten(1, {"highway": "street_lamp"})]
        moebel_knoten_ = [knoten(2, {moebel.ART_TAG: "bollard", "barrier": "bollard"})]
        ergaenzt, angereichert = moebel.einmischen(elemente, moebel_knoten_)
        self.assertEqual((ergaenzt, angereichert), (1, 0))
        self.assertEqual([el["id"] for el in elemente], [1, 2])

    def test_vorhandener_knoten_wird_ergaenzt_statt_dupliziert(self):
        # Die 1.761 Baenke stehen schon in der Basis - ein zweiter Eintrag mit
        # derselben Id wuerde im C++-Parser den ersten ueberschreiben.
        elemente = [knoten(5, {"amenity": "bench"})]
        ergaenzt, angereichert = moebel.einmischen(
            elemente, [knoten(5, {moebel.ART_TAG: "bench", "amenity": "bench"})])
        self.assertEqual((ergaenzt, angereichert), (0, 1))
        self.assertEqual(len(elemente), 1)
        self.assertEqual(elemente[0]["tags"][moebel.ART_TAG], "bench")

    def test_vorhandene_tags_werden_nicht_ueberschrieben(self):
        elemente = [knoten(5, {"amenity": "bench", "material": "metal"})]
        moebel.einmischen(
            elemente,
            [knoten(5, {moebel.ART_TAG: "bench", "amenity": "bench", "material": "wood"})])
        self.assertEqual(elemente[0]["tags"]["material"], "metal")

    def test_zweiter_lauf_aendert_nichts(self):
        elemente = [knoten(1, {"highway": "street_lamp"})]
        neue = [knoten(2, {moebel.ART_TAG: "bollard", "barrier": "bollard"})]
        moebel.einmischen(elemente, neue)
        ergaenzt, angereichert = moebel.einmischen(elemente, neue)
        self.assertEqual((ergaenzt, angereichert), (0, 0))
        self.assertEqual(len(elemente), 2)


class AbfrageTest(unittest.TestCase):
    def test_abfrage_deckt_jede_kategorie_im_stadtrechteck_ab(self):
        q = moebel.overpass_abfrage()
        for _, schluessel, wert in moebel.KATEGORIEN:
            self.assertIn('node["{0}"="{1}"]'.format(schluessel, wert), q)
        self.assertEqual(q.count("node["), len(moebel.KATEGORIEN))
        self.assertIn("{0},{1},{2},{3}".format(*moebel.CLIP), q)


if __name__ == "__main__":
    unittest.main()
