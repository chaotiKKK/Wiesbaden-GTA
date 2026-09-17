# TDD fuer den CityGML-Injektor (Hoehe + Dachform + Dachhoehe).
#   python -m unittest test_apply_lod2_citygml -v
import unittest
from apply_lod2_citygml import enrich_elements


def way(aid, tags=None):
    t = {"building": "yes"}
    if aid:
        t["alkis:id"] = aid
    if tags:
        t.update(tags)
    return {"type": "way", "id": 1, "tags": t}


class EnrichTest(unittest.TestCase):
    def test_geneigtes_dach_setzt_alles(self):
        els = [way("A")]
        nh, nr, nrh = enrich_elements(els, {"A": {"h": 16.3, "roof": "gabled", "rh": 3.9}})
        self.assertEqual((nh, nr, nrh), (1, 1, 1))
        t = els[0]["tags"]
        self.assertEqual(t["height"], "16.3")
        self.assertEqual(t["roof:shape"], "gabled")
        self.assertEqual(t["roof:height"], "3.9")

    def test_flachdach_keine_dachhoehe(self):
        els = [way("A")]
        enrich_elements(els, {"A": {"h": 10.5, "roof": "flat"}})
        t = els[0]["tags"]
        self.assertEqual(t["roof:shape"], "flat")
        self.assertNotIn("roof:height", t)

    def test_amtlich_ueberschreibt_alten_roofshape(self):
        els = [way("A", {"roof:shape": "hipped"})]
        enrich_elements(els, {"A": {"h": 12.0, "roof": "gabled", "rh": 2.0}})
        self.assertEqual(els[0]["tags"]["roof:shape"], "gabled")

    def test_unbekannt_bleibt(self):
        els = [way("X")]
        nh, nr, nrh = enrich_elements(els, {"A": {"h": 9.0, "roof": "gabled"}})
        self.assertEqual((nh, nr, nrh), (0, 0, 0))
        self.assertNotIn("roof:shape", els[0]["tags"])

    def test_nodes_uebersprungen(self):
        els = [{"type": "node", "id": 2, "tags": {"alkis:id": "A"}}]
        self.assertEqual(enrich_elements(els, {"A": {"h": 9, "roof": "gabled"}}), (0, 0, 0))


if __name__ == "__main__":
    unittest.main()
