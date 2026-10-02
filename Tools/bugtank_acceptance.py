"""Run and verify trace-backed BugTank acceptance in the packaged Dev build."""

from __future__ import annotations

import json
import math
import re
import shutil
import struct
import subprocess
import sys
import time
import argparse
from datetime import datetime
from pathlib import Path

from karte import standard_karte, standard_karte_pfad

ROOT = Path(__file__).resolve().parents[1]
DEV_BUILDS = ROOT / "Saved" / "Package" / "dev-builds"
MAP = f"{standard_karte_pfad()}.{standard_karte()}"
NUMBER = r"[-+]?\d+(?:\.\d+)?"
SAMPLE_RE = re.compile(
    rf"WbBugTankProbe SAMPLE t=({NUMBER}) surface=(BODEN|WAND|DECKE) .*?"
    rf"keys=W([01]) D([01]).*?trace_component=([^\s]+) "
    rf"trace_distance_cm=({NUMBER}) loc=\(([^)]*)\)"
)
CAPTURE_RE = re.compile(
    rf"WbBugTankProbe CAPTURE=(BODEN|WAND|DECKE) path=(.*?) loc=.*?"
    rf"trace_component=([^\s]+) trace_distance_cm=({NUMBER}) framed=([01])"
)
CEILING_RE = re.compile(
    rf"WbBugTankProbe CEILING_TRAVEL contact_seconds=({NUMBER}) "
    rf"continuous_contact_seconds=({NUMBER}) active_w_seconds=({NUMBER}) "
    rf"continuous_active_w_seconds=({NUMBER}) delta_cm=({NUMBER}) "
    rf"trace_component=([^\s]+) trace_distance_cm=({NUMBER})"
)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
LOCK_SCRIPT = ROOT / "Tools" / "engine_run_lock.ps1"

ROUTES = (
    ("neugasse", ("-WbGoto=-1438,2860", "-WbGotoYaw=-40")),
    ("rathaus_passage", ("-WbGoto=Rathaus-Passage", "-WbGotoYaw=153")),
)


def _valid_trace(component: str, distance_cm: float, surface: str) -> bool:
    if component in ("", "None") or not (0.0 < distance_cm <= 165.0):
        return False
    return surface == "BODEN" or not component.startswith("Landscape")


def _location(value: str) -> tuple[float, float, float] | None:
    try:
        coordinates = tuple(float(part) for part in value.split(","))
    except ValueError:
        return None
    return coordinates if len(coordinates) == 3 else None


def _samples(text: str) -> list[dict]:
    parsed = []
    for line in text.splitlines():
        match = SAMPLE_RE.search(line)
        if not match:
            continue
        t, surface, w, _d, component, distance, location = match.groups()
        point = _location(location)
        if point is None:
            continue
        parsed.append({
            "t": float(t),
            "surface": surface,
            "w": w == "1",
            "component": component,
            "trace_distance_cm": float(distance),
            "loc": point,
        })
    return parsed


def _active_trace_distance(samples: list[dict], surface: str) -> float:
    distance = 0.0
    for before, after in zip(samples, samples[1:]):
        if (before["surface"] != surface or after["surface"] != surface
                or not before["w"] or not after["w"]
                or after["t"] - before["t"] > 1.25):
            continue
        if not (_valid_trace(before["component"], before["trace_distance_cm"], surface)
                and _valid_trace(after["component"], after["trace_distance_cm"], surface)):
            continue
        distance += math.dist(before["loc"], after["loc"])
    return distance


def _trace_summary(samples: list[dict], surface: str) -> dict:
    hits = [s for s in samples if s["surface"] == surface
            and _valid_trace(s["component"], s["trace_distance_cm"], surface)]
    return {
        "trace_confirmed": bool(hits),
        "components": sorted({hit["component"] for hit in hits}),
        "trace_distance_cm_range": [round(min(hit["trace_distance_cm"] for hit in hits), 1),
                                     round(max(hit["trace_distance_cm"] for hit in hits), 1)] if hits else [],
    }


def _captures(text: str) -> list[dict]:
    result = []
    for line in text.splitlines():
        match = CAPTURE_RE.search(line)
        if match:
            surface, path, component, distance, framed = match.groups()
            result.append({
                "surface": surface,
                "path": path.strip(),
                "component": component,
                "trace_distance_cm": float(distance),
                "framed": framed == "1",
            })
    return result


def analyze_logs(neugasse_log: str, ceiling_log: str) -> dict:
    floor_samples = _samples(neugasse_log)
    ceiling_samples = _samples(ceiling_log)
    floor_distance = _active_trace_distance(floor_samples, "BODEN")
    facade_distance = _active_trace_distance(floor_samples, "WAND")
    neugasse_captures = _captures(neugasse_log)
    ceiling_captures = _captures(ceiling_log)
    captures = [c for c in neugasse_captures if c["surface"] in ("BODEN", "WAND")]
    captures.extend(c for c in ceiling_captures if c["surface"] == "DECKE")

    ceiling = None
    for line in ceiling_log.splitlines():
        match = CEILING_RE.search(line)
        if not match:
            continue
        contact, continuous, active_w, continuous_w, delta, component, trace_distance = match.groups()
        values = tuple(float(value) for value in (contact, continuous, active_w, continuous_w, delta, trace_distance))
        if (values[0] >= 2.0 and values[1] >= 2.0 and values[2] >= 1.0
                and values[3] >= 1.0 and values[4] >= 100.0
                and _valid_trace(component, values[5], "DECKE")):
            ceiling = {
                "contact_seconds": values[0],
                "continuous_contact_seconds": values[1],
                "active_w_seconds": values[2],
                "continuous_active_w_seconds": values[3],
                "distance_cm": values[4],
                "trace_confirmed": True,
                "trace_component": component,
                "trace_distance_cm": values[5],
            }
            break

    floor_trace = _trace_summary(floor_samples, "BODEN")
    facade_trace = _trace_summary(floor_samples, "WAND")
    abort = next((line.split("WbBugTankProbe ABORT:", 1)[1].strip()
                  for line in ceiling_log.splitlines() if "WbBugTankProbe ABORT:" in line), "")
    passed = (
        floor_distance >= 100.0 and floor_trace["trace_confirmed"]
        and any(c["surface"] == "BODEN" for c in neugasse_captures)
        and facade_distance >= 100.0 and facade_trace["trace_confirmed"]
        and any(c["surface"] == "WAND" for c in neugasse_captures)
        and ceiling is not None
        and any(c["surface"] == "DECKE" for c in ceiling_captures)
    )
    return {
        "passed": passed,
        "floor": {"distance_cm": round(floor_distance, 1), **floor_trace},
        "facade": {"distance_cm": round(facade_distance, 1), **facade_trace},
        "ceiling": ceiling or {"distance_cm": 0.0, "trace_confirmed": False, "abort": abort},
        "captures": captures,
    }


def _package_executable(package: Path) -> Path | None:
    executable = package / "Binaries" / "Win64" / "WiesbadenReal.exe"
    pak_dir = package / "Content" / "Paks"
    paks = list(pak_dir.glob("*.pak")) if pak_dir.is_dir() else []
    stores = list(pak_dir.glob("*.utoc")) if pak_dir.is_dir() else []
    containers = list(pak_dir.glob("*.ucas")) if pak_dir.is_dir() else []
    if executable.is_file() and (paks or (stores and containers)):
        return executable
    return None


def _find_package(package_dir: Path | None = None) -> tuple[Path, Path]:
    if package_dir is not None:
        package = Path(package_dir).resolve()
        executable = _package_executable(package)
        if executable is None:
            raise RuntimeError(f"Development-Paket unvollstaendig: {package}")
        return package.parents[1], executable

    candidates = []
    if not DEV_BUILDS.is_dir():
        raise RuntimeError(f"Development-Paketordner fehlt: {DEV_BUILDS}")
    for build in DEV_BUILDS.iterdir():
        package = build / "Windows" / "WiesbadenReal"
        executable = _package_executable(package)
        if executable:
            candidates.append((executable.stat().st_mtime, build, executable))
    if not candidates:
        raise RuntimeError(f"Kein vollstaendiges Windows-Development-Paket unter {DEV_BUILDS}")
    _, build, executable = max(candidates)
    return build, executable


def _powershell(*arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(LOCK_SCRIPT), *arguments],
        cwd=ROOT, text=True, encoding="utf-8", errors="replace", capture_output=True,
    )


def _engine_processes() -> list[dict]:
    command = (
        "$p=Get-CimInstance Win32_Process | Where-Object { "
        "($_.Name -match '^(UnrealEditor|UnrealEditor-Cmd|WiesbadenReal|AutomationTool|UnrealBuildTool)\\.exe$') "
        "-or ($_.Name -eq 'dotnet.exe' -and $_.CommandLine -match 'UnrealBuildTool|AutomationTool|RunUAT') "
        "}; $p | Select-Object ProcessId,ParentProcessId,Name,CommandLine | ConvertTo-Json -Compress"
    )
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-Command", command], cwd=ROOT,
        text=True, encoding="utf-8", errors="replace", capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"Unreal-Prozesspruefung fehlgeschlagen: {result.stderr.strip()}")
    payload = result.stdout.strip()
    if not payload:
        return []
    decoded = json.loads(payload)
    return decoded if isinstance(decoded, list) else [decoded]


def _disk_percent_free() -> float:
    usage = shutil.disk_usage(ROOT)
    return 100.0 * usage.free / usage.total


def _is_own_lock(status: str) -> bool:
    return ("Lock: von diesem Lauf gehalten" in status
            or "Lock: von diesem Lauf bereits gehalten" in status)


def _preflight(allow_locked_processes: bool = False) -> str:
    status = _powershell("-Modus", "Status")
    lock_text = (status.stdout + status.stderr).strip()
    is_free = "Lock: frei" in lock_text
    if status.returncode not in (0, 3):
        raise RuntimeError(f"Lockstatus nicht lesbar: {lock_text}")
    processes = _engine_processes()
    if processes and (is_free or not allow_locked_processes):
        details = "; ".join(f"{p['Name']} PID {p['ProcessId']}: {p.get('CommandLine', '')}" for p in processes)
        raise RuntimeError(f"Unreal-Prozess ohne passenden freien Startpunkt: {details}")
    if _disk_percent_free() < 10.0:
        raise RuntimeError(f"Platten-Gate rot: nur {_disk_percent_free():.1f}% frei (Minimum 10%).")
    return lock_text


def _take_lock() -> bool:
    # Check active engines before waiting, and let the project wrapper recheck
    # disk space atomically before acquiring its machine-wide lock.
    status = _preflight(allow_locked_processes=True)
    if _is_own_lock(status):
        return False
    print(f"{status}; warte bis zu 30 Minuten auf den globalen Unreal-Lock.", flush=True)
    for process in _engine_processes():
        print(f"Aktiver Unreal-Prozess: {process['Name']} PID {process['ProcessId']} "
              f"Parent {process['ParentProcessId']} — {process.get('CommandLine', '')}", flush=True)
    result = _powershell("-Modus", "Nehmen", "-Name", "bugtank_acceptance", "-WarteSekunden", "1800")
    if result.returncode != 0:
        raise RuntimeError((result.stdout + result.stderr).strip() or f"Lock-Aufnahme Exit {result.returncode}")
    try:
        status = _preflight()
        if not _is_own_lock(status):
            raise RuntimeError(f"Globaler Unreal-Lock gehoert nach Aufnahme nicht diesem Lauf: {status}")
        return True
    except Exception:
        _release_lock()
        raise


def _release_lock() -> None:
    result = _powershell("", "-Modus", "Freigeben")
    if result.returncode != 0:
        raise RuntimeError((result.stdout + result.stderr).strip() or f"Lock-Freigabe Exit {result.returncode}")


def _fresh_png(path: Path, started_at: float) -> bool:
    try:
        if not path.is_file() or path.stat().st_mtime < started_at - 1.0 or path.stat().st_size < 24:
            return False
        with path.open("rb") as image:
            header = image.read(24)
        if header[:8] != PNG_SIGNATURE or header[12:16] != b"IHDR":
            return False
        width, height = struct.unpack(">II", header[16:24])
        return width > 0 and height > 0
    except OSError:
        return False


def _run_route(executable: Path, route: tuple[str, tuple[str, ...]], run_dir: Path) -> tuple[int, Path]:
    name, start = route
    log_path = run_dir / f"{name}.log"
    stdout_path = run_dir / f"{name}.stdout.log"
    command = [
        str(executable), MAP, "-game", *start, "-WbTime=13", "-WbBugTank",
        "-WbBugTankProbe", "-WbQuitAfter=55", "-windowed", "-ResX=1280",
        "-ResY=720", "-unattended", "-nop4", f"-abslog={log_path}",
    ]
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout:
        process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            return_code = process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise RuntimeError(f"Paketlauf {name} ueberschritt 180 s; eigener Spielprozess beendet.")
    return return_code, log_path


def run(package_dir: Path | None = None) -> int:
    acquired_lock = _take_lock()
    try:
        return _run_locked(package_dir)
    finally:
        if acquired_lock:
            _release_lock()


def _run_locked(package_dir: Path | None = None) -> int:
    build, executable = _find_package(package_dir)
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    run_dir = ROOT / "Saved" / "Diagnose" / "BugTankAcceptance" / run_id
    evidence_dir = run_dir / "evidence"
    evidence_dir.mkdir(parents=True, exist_ok=False)
    started_at = time.time()
    (run_dir / "run.json").write_text(json.dumps({
        "started_at": started_at,
        "package": str(build),
        "executable": str(executable),
        "map": MAP,
        "routes": [{
            "name": name,
            "arguments": [MAP, "-game", *start, "-WbTime=13", "-WbBugTank", "-WbBugTankProbe",
                          "-WbQuitAfter=55", "-windowed", "-ResX=1280", "-ResY=720", "-unattended", "-nop4"],
        } for name, start in ROUTES],
    }, indent=2), encoding="utf-8")

    return_codes = {}
    logs = {}
    for route in ROUTES:
        name = route[0]
        try:
            _preflight()
            print(f"Starte Development-Paketroute: {name}.", flush=True)
            return_codes[name], log_path = _run_route(executable, route, run_dir)
            logs[name] = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
            print(f"Route {name} beendet, Exit {return_codes[name]}; Log: {log_path}", flush=True)
        except Exception as error:
            return_codes[name] = -1
            logs[name] = ""
            print(f"{name}: FEHLER: {error}")
            break

    result = analyze_logs(logs.get("neugasse", ""), logs.get("rathaus_passage", ""))
    failures = []
    for name, code in return_codes.items():
        if code != 0:
            failures.append(f"Paketprozess {name} endete mit Exit-Code {code}.")
    binary_dir = executable.parent
    for capture in result["captures"]:
        source = Path(capture["path"])
        if not source.is_absolute():
            source = (binary_dir / source).resolve()
        surface = capture["surface"]
        valid = _fresh_png(source, started_at) and _valid_trace(
            capture["component"], capture["trace_distance_cm"], surface)
        if not valid:
            failures.append(f"{surface}-Bild fehlt, ist nicht frisch oder ohne gueltigen Oberflaechentrace: {source}")
            continue
        destination = evidence_dir / f"{surface.lower()}.png"
        shutil.copy2(source, destination)
        capture["evidence"] = str(destination)
    required_images = {"BODEN", "WAND", "DECKE"}
    copied_images = {capture["surface"] for capture in result["captures"] if "evidence" in capture}
    for surface in sorted(required_images - copied_images):
        failures.append(f"Kein frischer Bildbeleg fuer {surface}.")
    result["process_exit_codes"] = return_codes
    result["failures"] = failures
    result["passed"] = result["passed"] and not failures
    (run_dir / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")

    print(f"BugTank Development-Abnahmelauf: {'BESTANDEN' if result['passed'] else 'FEHLGESCHLAGEN'}")
    print(f"Paket: {build}")
    print(f"Belege und Logs: {run_dir}")
    print(f"Boden: {result['floor']['distance_cm']:.0f} cm trace-bestaetigt; Komponenten: {', '.join(result['floor']['components'])}")
    print(f"Fassade: {result['facade']['distance_cm']:.0f} cm trace-bestaetigt; Komponenten: {', '.join(result['facade']['components'])}")
    ceiling = result["ceiling"]
    print("Decke: " + (f"{ceiling['distance_cm']:.0f} cm, W {ceiling['active_w_seconds']:.2f} s, "
                     f"Kontakt {ceiling['contact_seconds']:.2f} s" if "active_w_seconds" in ceiling else
                     f"nicht bestaetigt ({ceiling['abort']})" if ceiling.get("abort") else "nicht bestaetigt"))
    for failure in failures:
        print(f"FEHLER: {failure}")
    if not failures:
        print("Bilder: " + ", ".join(str(evidence_dir / f"{name}.png") for name in ("boden", "wand", "decke")))
    return 0 if result["passed"] else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true", required=True)
    parser.add_argument("--package-dir", type=Path,
                        help="testet exakt dieses Development-Paket statt Saved/Package/dev-builds")
    args = parser.parse_args(argv)
    return run(args.package_dir)


if __name__ == "__main__":
    raise SystemExit(main())
