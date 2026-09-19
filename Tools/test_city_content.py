"""Focused tests for safe Alkis release extraction and manifest validation."""

import hashlib
import io
import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fetch_city_content as content  # noqa: E402


class CityContentBoundaryTest(unittest.TestCase):
    def test_https_fallback_reuses_authenticated_gh_token(self):
        with mock.patch.dict(os.environ, {}, clear=True), \
                mock.patch.object(content.shutil, "which", return_value="gh"), \
                mock.patch.object(
                    content.subprocess,
                    "run",
                    return_value=SimpleNamespace(returncode=0, stdout="secret-token\n"),
                ):
            self.assertEqual(content.github_token(), "secret-token")

    def test_manifest_accepts_valid_relative_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            payload = root / "Content" / "Generated" / "tile.bin"
            payload.parent.mkdir(parents=True)
            payload.write_bytes(b"alkis16")
            digest = hashlib.sha256(payload.read_bytes()).hexdigest()
            manifest = root / "INHALT.sha256"
            manifest.write_text(
                "WiesbadenReal - Inhalts-Paket WiesbadenCity_Alkis16\n"
                "sha256  Groesse  Pfad (relativ zur Projektwurzel)\n"
                f"{digest} {payload.stat().st_size} Content/Generated/tile.bin\n",
                encoding="utf-8",
            )
            self.assertEqual(content.pruefe_manifest(str(manifest), str(root)), (1, 0, 0))

    def test_manifest_rejects_malformed_entry_instead_of_passing_empty(self):
        with tempfile.TemporaryDirectory() as tmp:
            manifest = Path(tmp) / "INHALT.sha256"
            manifest.write_text("not a manifest entry\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                content.pruefe_manifest(str(manifest), tmp)

    def test_manifest_rejects_empty_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            manifest = Path(tmp) / "INHALT.sha256"
            manifest.write_text("\n# no files yet\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                content.pruefe_manifest(str(manifest), tmp)

    def test_zip_extraction_rejects_parent_traversal_before_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "project"
            root.mkdir()
            archive = Path(tmp) / "malicious.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("../escaped.txt", "must not be written")
            with zipfile.ZipFile(archive) as zf:
                with self.assertRaises(ValueError):
                    content.entpacke_sicher(zf, str(root))
            self.assertFalse((Path(tmp) / "escaped.txt").exists())

    def test_zip_extraction_accepts_nested_release_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "project"
            root.mkdir()
            archive = Path(tmp) / "valid.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("Content/Generated/tile.bin", b"ok")
            with zipfile.ZipFile(archive) as zf:
                content.entpacke_sicher(zf, str(root))
            self.assertEqual(
                (root / "Content" / "Generated" / "tile.bin").read_bytes(), b"ok"
            )

    def test_zip_extraction_rejects_symlink_before_writing_any_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "project"
            root.mkdir()
            archive = Path(tmp) / "symlink.zip"
            link = zipfile.ZipInfo("Content/Generated/link")
            link.create_system = 3
            link.external_attr = (0o120777 << 16) | 0xA000
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("Content/Generated/should-not-extract.txt", b"no")
                zf.writestr(link, b"outside.txt")
            with zipfile.ZipFile(archive) as zf:
                with self.assertRaises(ValueError):
                    content.entpacke_sicher(zf, str(root))
            self.assertFalse((root / "Content" / "Generated").exists())

    def test_main_retries_https_after_truncated_gh_archive(self):
        payload = io.BytesIO()
        with zipfile.ZipFile(payload, "w") as zf:
            zf.writestr("payload.txt", b"complete")
            digest = hashlib.sha256(b"complete").hexdigest()
            zf.writestr(
                "INHALT.sha256",
                "WiesbadenReal - Testpaket\n"
                "sha256  Groesse  Pfad (relativ zur Projektwurzel)\n"
                f"{digest} 8 payload.txt\n",
            )
        archive_bytes = payload.getvalue()
        package = {
            "asset": "fixture.zip",
            "sha256": hashlib.sha256(archive_bytes).hexdigest(),
            "manifest": "INHALT.sha256",
            "inhalt": "Testpaket",
        }
        calls = []

        def fake_download(asset, target, repo, nur_https):
            calls.append(nur_https)
            Path(target).write_bytes(b"truncated" if not nur_https else archive_bytes)
            return True

        with tempfile.TemporaryDirectory() as tmp, \
                mock.patch.object(content, "PROJEKT", tmp), \
                mock.patch.object(content, "PAKETE", [package]), \
                mock.patch.object(content, "hole_archiv", side_effect=fake_download), \
                mock.patch.object(content.shutil, "which", return_value="gh"), \
                mock.patch.object(content.sys, "argv", ["fetch_city_content.py"]):
            self.assertEqual(content.main(), 0)
            self.assertEqual(calls, [False, True])
            self.assertEqual((Path(tmp) / "payload.txt").read_bytes(), b"complete")


if __name__ == "__main__":
    unittest.main()
