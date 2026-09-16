# TDD fuer den amtlichen CityGML-Dachform-Parser.
#   python -m unittest test_citygml_roofs -v
import unittest
from citygml_roofs import roof_shape_from_code, parse_building_block, ROOFTYPE_MAP

BUILDING = '''<bldg:Building gml:id="DEHE062000013OfY">
  <core:creationDate>2020-03-07</core:creationDate>
  <gen:stringAttribute name="Dachneigung"><gen:value>35</gen:value></gen:stringAttribute>
  <gen:stringAttribute name="GebaeudeHoehe"><gen:value>9.4</gen:value></gen:stringAttribute>
  <gen:stringAttribute name="Firsthoehe"><gen:value>112.5</gen:value></gen:stringAttribute>
  <gen:stringAttribute name="MittlereTraufHoehe"><gen:value>109.0</gen:value></gen:stringAttribute>
  <gen:stringAttribute name="AbsoluteHoehe"><gen:value>103.1</gen:value></gen:stringAttribute>
  <gen:stringAttribute name="ALKISOID"><gen:value>DEHE062000013OfY</gen:value></gen:stringAttribute>
  <bldg:roofType>3100</bldg:roofType>
  <bldg:measuredHeight>9.4</bldg:measuredHeight>
  <bldg:function>31001_1000</bldg:function>
</bldg:Building>'''


class MapTest(unittest.TestCase):
    def test_known_codes(self):
        self.assertEqual(roof_shape_from_code("1000"), "flat")
        self.assertEqual(roof_shape_from_code("3100"), "gabled")
        self.assertEqual(roof_shape_from_code("3200"), "hipped")
        self.assertEqual(roof_shape_from_code("3500"), "pyramidal")
        self.assertEqual(roof_shape_from_code("2100"), "skillion")
        self.assertEqual(roof_shape_from_code("3700"), "dome")

    def test_unknown_code_flat(self):
        self.assertEqual(roof_shape_from_code("1234"), "flat")
        self.assertEqual(roof_shape_from_code(""), "flat")

    def test_all_mapped_are_valid_osm(self):
        valid = {"flat", "gabled", "hipped", "pyramidal", "skillion", "dome"}
        self.assertTrue(set(ROOFTYPE_MAP.values()) <= valid)


class ParseTest(unittest.TestCase):
    def test_full_building(self):
        aid, info = parse_building_block(BUILDING)
        self.assertEqual(aid, "DEHE062000013OfY")
        self.assertEqual(info["roof"], "gabled")     # 3100
        self.assertEqual(info["code"], "3100")
        self.assertAlmostEqual(info["rh"], 3.5, places=2)  # 112.5 - 109.0
        self.assertAlmostEqual(info["h"], 9.4, places=2)

    def test_flat_has_no_roof_height(self):
        b = BUILDING.replace("3100", "1000").replace(
            "<gen:value>109.0", "<gen:value>112.5")  # Traufe==First -> rh<=0
        aid, info = parse_building_block(b)
        self.assertEqual(info["roof"], "flat")
        self.assertNotIn("rh", info)

    def test_missing_id(self):
        aid, info = parse_building_block("<bldg:Building></bldg:Building>")
        self.assertIsNone(aid)

    def test_multipart_picks_tallest(self):
        # Mehrteil-Gebaeude: niedriger Flachdach-Anbau ZUERST, hoher Sattel-Hauptteil
        # danach. Der hoechste Teil (max measuredHeight) bestimmt Hoehe+Dachform -
        # NICHT der erste (das war der Bug vor der 3DGM-PDF-Auswertung).
        b = ('<bldg:Building gml:id="DEHE_MP">'
             '<gen:stringAttribute name="ALKISOID"><gen:value>DEHE_MP</gen:value></gen:stringAttribute>'
             '<bldg:consistsOfBuildingPart><bldg:BuildingPart gml:id="p1">'
             '<gen:stringAttribute name="Firsthoehe"><gen:value>103.2</gen:value></gen:stringAttribute>'
             '<gen:stringAttribute name="MittlereTraufHoehe"><gen:value>103.2</gen:value></gen:stringAttribute>'
             '<bldg:roofType>1000</bldg:roofType><bldg:measuredHeight>3.2</bldg:measuredHeight>'
             '</bldg:BuildingPart></bldg:consistsOfBuildingPart>'
             '<bldg:consistsOfBuildingPart><bldg:BuildingPart gml:id="p2">'
             '<gen:stringAttribute name="Firsthoehe"><gen:value>112.0</gen:value></gen:stringAttribute>'
             '<gen:stringAttribute name="MittlereTraufHoehe"><gen:value>108.5</gen:value></gen:stringAttribute>'
             '<bldg:roofType>3100</bldg:roofType><bldg:measuredHeight>12.0</bldg:measuredHeight>'
             '</bldg:BuildingPart></bldg:consistsOfBuildingPart>'
             '</bldg:Building>')
        aid, info = parse_building_block(b)
        self.assertEqual(aid, "DEHE_MP")
        self.assertEqual(info["roof"], "gabled")   # hoher Teil (3100), nicht 1000
        self.assertAlmostEqual(info["h"], 12.0, places=2)
        self.assertAlmostEqual(info["rh"], 3.5, places=2)  # 112.0 - 108.5


if __name__ == "__main__":
    unittest.main()
