"""Regression tests for the OSM traffic-sign asset boundary."""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fetch_sign_textures as signs  # noqa: E402


class SignTextureNormalizationTest(unittest.TestCase):
    def test_parser_forms_resolve_to_asset_ids(self):
        cases = {
            "DE:274.1:30": "274-30",
            "DE:1001-30-200": "1001-30",
            "DE:1036-37": "1026-37",
            "DE:260-30": "260",
            "DE240": "240",
        }
        for raw, expected in cases.items():
            with self.subTest(raw=raw):
                self.assertEqual(signs.token_normalisieren(raw), expected)

    def test_real_osm_variants_are_satisfied_by_canonical_assets(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            osm = root / "wiesbaden.osm.json"
            osm.write_text(json.dumps({
                "elements": [{"tags": {"traffic_sign":
                    "DE:260-30;DE:1036-37;DE:1001-30-200"}}]
            }), encoding="utf-8")
            texture_dir = root / "textures"
            texture_dir.mkdir()
            for name in ("260", "1026-37", "1001-30"):
                (texture_dir / f"Sign_{name}.png").write_bytes(b"test")
            catalog = root / "catalog.json"
            catalog.write_text(json.dumps({"signs": [
                {"id": "260"}, {"id": "1026-37"}, {"id": "1001-30"}
            ]}), encoding="utf-8")

            with mock.patch.object(signs, "OSM", str(osm)), \
                    mock.patch.object(signs, "ZIEL", str(texture_dir)), \
                    mock.patch.object(signs, "KATALOG", str(catalog)):
                self.assertEqual(signs.fehlende_ids(), [])


if __name__ == "__main__":
    unittest.main()
