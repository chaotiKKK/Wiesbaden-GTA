import os
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from bugtank_acceptance import (
    MAP, PNG_SIGNATURE, _find_package, _fresh_png, _is_own_lock, analyze_logs, run,
)
from karte import standard_karte, standard_karte_pfad

ROOT = Path(__file__).resolve().parents[1]


def sample(t, surface, key, component, x, y, z, distance=58.0):
    return (
        f"LogWbVehicles: WbBugTankProbe SAMPLE t={t:.1f} surface={surface} "
        "ceiling_contact_seconds=0.00 ceiling_active_w_seconds=0.00 "
        f"continuous_contact_seconds=1.00 continuous_active_w_seconds=1.00 "
        f"keys=W{key} D0 phase=1.0 from_surface_cm=200 "
        f"trace_component={component} trace_distance_cm={distance:.1f} "
        f"loc=({x:.0f},{y:.0f},{z:.0f}) up=(0,0,1)"
    )


class BugTankAcceptanceTest(unittest.TestCase):
    def test_routes_use_the_configured_default_map(self):
        expected = f"{standard_karte_pfad()}.{standard_karte()}"

        self.assertEqual(MAP, expected)

    def test_lock_status_recognizes_documented_owned_lock_wordings(self):
        self.assertTrue(_is_own_lock("Lock: von diesem Lauf gehalten - PID 42."))
        self.assertTrue(_is_own_lock("Lock: von diesem Lauf bereits gehalten: PID 42."))
        self.assertFalse(_is_own_lock("Lock: BELEGT durch einen anderen Lauf - PID 42."))

    def test_surface_distance_counts_only_adjacent_w_samples_with_valid_contact(self):
        floor = "\n".join((
            sample(1, "BODEN", 1, "RoadCollisionStaticMesh", 0, 0, 0),
            sample(2, "BODEN", 1, "LandscapeHeightfieldCollisionComponent_1", 250, 0, 0),
            sample(3, "BODEN", 0, "RoadCollisionStaticMesh", 500, 0, 0),
        ))
        wall = "\n".join((
            sample(1, "WAND", 1, "BoxComponent_1", 0, 0, 0),
            sample(2, "WAND", 1, "BoxComponent_2", 0, 400, 0),
            sample(3, "WAND", 1, "LandscapeHeightfieldCollisionComponent_3", 0, 900, 0),
        ))

        result = analyze_logs(floor + "\n" + wall, "")

        self.assertEqual(result["floor"]["distance_cm"], 250.0)
        self.assertEqual(result["facade"]["distance_cm"], 400.0)

    def test_gaps_and_unconfirmed_facade_hits_do_not_count_as_wall_travel(self):
        wall = "\n".join((
            sample(1, "WAND", 1, "BoxComponent_1", 0, 0, 0),
            sample(3, "WAND", 1, "BoxComponent_1", 0, 900, 0),
            sample(4, "WAND", 1, "None", 0, 1400, 0),
            sample(5, "WAND", 1, "LandscapeHeightfieldCollisionComponent_3", 0, 1900, 0),
        ))

        result = analyze_logs("", wall)

        self.assertEqual(result["facade"]["distance_cm"], 0.0)
        self.assertFalse(result["passed"])

    def test_ceiling_requires_trace_confirmed_active_w_travel(self):
        ceiling = (
            "LogWbVehicles: WbBugTankProbe CEILING_TRAVEL "
            "contact_seconds=2.51 continuous_contact_seconds=2.00 "
            "active_w_seconds=2.03 continuous_active_w_seconds=2.03 "
            "delta_cm=831 trace_component=BoxComponent_7 trace_distance_cm=58.0 "
            "start=(0,0,0) now=(0,831,0)"
        )

        result = analyze_logs("", ceiling)

        self.assertEqual(result["ceiling"]["distance_cm"], 831.0)
        self.assertEqual(result["ceiling"]["active_w_seconds"], 2.03)
        self.assertTrue(result["ceiling"]["trace_component"])

    def test_complete_contact_and_capture_evidence_passes(self):
        floor_and_wall = "\n".join((
            sample(1, "BODEN", 1, "RoadCollisionStaticMesh", 0, 0, 0),
            sample(2, "BODEN", 1, "RoadCollisionStaticMesh", 200, 0, 0),
            sample(3, "WAND", 1, "BoxComponent_1", 200, 0, 0),
            sample(4, "WAND", 1, "BoxComponent_2", 200, 200, 0),
            "WbBugTankProbe CAPTURE=BODEN path=floor.png loc=(0,0,0) up=(0,0,1) "
            "trace_component=RoadCollisionStaticMesh trace_distance_cm=58.0 framed=0",
            "WbBugTankProbe CAPTURE=WAND path=wall.png loc=(0,0,0) up=(1,0,0) "
            "trace_component=BoxComponent_2 trace_distance_cm=58.0 framed=0",
        ))
        ceiling = "\n".join((
            "WbBugTankProbe CAPTURE=DECKE path=ceiling.png loc=(0,0,0) up=(0,0,-1) "
            "trace_component=BoxComponent_3 trace_distance_cm=58.0 framed=1",
            "WbBugTankProbe CEILING_TRAVEL contact_seconds=2.51 continuous_contact_seconds=2.00 "
            "active_w_seconds=2.03 continuous_active_w_seconds=2.03 delta_cm=831 "
            "trace_component=BoxComponent_3 trace_distance_cm=58.0 start=(0,0,0) now=(0,831,0)",
        ))

        result = analyze_logs(floor_and_wall, ceiling)

        self.assertTrue(result["passed"])

    def test_landscape_ceiling_hit_is_not_accepted_as_ceiling_travel(self):
        ceiling = (
            "LogWbVehicles: WbBugTankProbe CEILING_TRAVEL "
            "contact_seconds=2.51 continuous_contact_seconds=2.00 "
            "active_w_seconds=2.03 continuous_active_w_seconds=2.03 "
            "delta_cm=831 trace_component=LandscapeHeightfieldCollisionComponent_7 "
            "trace_distance_cm=58.0 start=(0,0,0) now=(0,831,0)"
        )

        result = analyze_logs("", ceiling)

        self.assertEqual(result["ceiling"]["distance_cm"], 0.0)
        self.assertFalse(result["passed"])

    def test_ceiling_without_sustained_w_contact_is_not_accepted(self):
        ceiling = (
            "WbBugTankProbe CEILING_TRAVEL contact_seconds=2.51 continuous_contact_seconds=2.00 "
            "active_w_seconds=0.20 continuous_active_w_seconds=0.20 delta_cm=831 "
            "trace_component=BoxComponent_7 trace_distance_cm=58.0 start=(0,0,0) now=(0,831,0)"
        )

        result = analyze_logs("", ceiling)

        self.assertFalse(result["passed"])
        self.assertEqual(result["ceiling"]["distance_cm"], 0.0)

    def test_ceiling_contact_abort_reason_is_reported(self):
        ceiling = "WbBugTankProbe ABORT: no surface contact at t=3.1 current_contact=0; no surface capture."

        result = analyze_logs("", ceiling)

        self.assertIn("no surface contact", result["ceiling"]["abort"])

    def test_image_must_be_a_fresh_png(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.png"
            path.write_bytes(PNG_SIGNATURE + struct.pack(">I4sII", 13, b"IHDR", 1280, 720))
            now = path.stat().st_mtime

            self.assertTrue(_fresh_png(path, now - 1.0))
            path.write_bytes(PNG_SIGNATURE + b"truncated")
            self.assertFalse(_fresh_png(path, now - 1.0))
            path.write_bytes(PNG_SIGNATURE + struct.pack(">I4sII", 13, b"IHDR", 1280, 720))
            os.utime(path, (now - 10.0, now - 10.0))
            self.assertFalse(_fresh_png(path, now - 1.0))

    def test_release_gate_can_target_the_package_just_created(self):
        with tempfile.TemporaryDirectory() as directory:
            package_root = Path(directory) / "Saved" / "Package"
            package = package_root / "Windows" / "WiesbadenReal"
            executable = package / "Binaries" / "Win64" / "WiesbadenReal.exe"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"MZ")
            pak = package / "Content" / "Paks" / "WiesbadenReal-Windows.pak"
            pak.parent.mkdir(parents=True)
            pak.write_bytes(b"pak")

            build, found_executable = _find_package(package_dir=package)

            self.assertEqual(build, package_root)
            self.assertEqual(found_executable, executable)

    def test_release_gate_rejects_an_incomplete_explicit_package(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "unvollstaendig"):
                _find_package(package_dir=Path(directory))

    def test_nested_release_gate_does_not_release_its_parent_lock(self):
        with (
            mock.patch("bugtank_acceptance._preflight",
                       return_value="Lock: von diesem Lauf bereits gehalten: PID 42"),
            mock.patch("bugtank_acceptance._release_lock") as release_lock,
            mock.patch("bugtank_acceptance._run_locked", return_value=0),
        ):
            self.assertEqual(run(), 0)

        release_lock.assert_not_called()

    def test_build_release_runs_bugtank_gate_before_shortcut_and_restores_previous(self):
        source = (ROOT / "Tools" / "build_release.ps1").read_text(encoding="utf-8")
        gate_at = source.index('Section 5 "BugTank-Development-Abnahme')
        shortcut_at = source.index('Section 6 "Desktop-Verknuepfung')
        gate = source[gate_at:shortcut_at]

        self.assertLess(gate_at, shortcut_at)
        self.assertIn("bugtank_acceptance.py", gate)
        self.assertIn("--package-dir", gate)
        self.assertIn("$PackageDir", gate)
        self.assertIn("$PrevPackageDir", gate)
        self.assertIn("Fail \"Gate 5 (BugTank-Development-Abnahme)\"", gate)


if __name__ == "__main__":
    unittest.main()
