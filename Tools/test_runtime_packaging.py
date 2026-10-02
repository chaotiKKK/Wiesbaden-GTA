"""Schlanke Paketvertrags-Checks ohne Editor, Cook oder Archiveingriffe.

    python -m unittest discover -s Tools -p test_runtime_packaging.py -v
    WB_PACKAGING_RECEIPT=Binaries/Win64/WiesbadenReal.target python -m unittest \
        discover -s Tools -p test_runtime_packaging.py -v

Der optionale Receipt-Check prueft den echten UBT-Ausgang, nicht nur Quelltext.
Ein Receipt ist trotzdem noch kein Cook-/Stage-/EXE-Nachweis.
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGING = "/Script/UnrealEd.ProjectPackagingSettings"
STAGING = "WiesbadenReal.RuntimeStaging"
COOK = {
    "/Game/Materials/City", "/Game/Materials/PostProcess",
    "/Game/Textures/TrafficSigns", "/Game/Assets/People", "/Game/Audio/Mix",
    "/Game/Props/DFI", "/Game/Vehicles/Bus", "/Game/Vehicles/Traffic",
    "/Game/Audio/Meta", "/Game/Audio/Samples", "/Game/Audio/Bus/Announce",
    "/Game/Props/EsweHalte", "/Game/SebboTower/Meshes",
    "/Game/Vehicles/Beetle/Restored", "/Game/Vehicles/BugTank",
}
FILES = {
    "Data/Raw/Bus/line3.json", "Data/Raw/Bus/line6.json",
    "Data/Raw/Bus/line6_schedule.json", "Data/Raw/Bus/announce_line3.json",
    "Data/Raw/Bus/announce_line6.json", "Data/Missions/missions.json",
    "Data/Store/unlocks.json", "Content/Config/TrafficSignCatalog.json",
    "Content/Config/WiesbadenRoadTypes.json",
}


def read(path):
    return path.read_text(encoding="utf-8-sig", errors="replace")


def array(text, section, key):
    """Die UE-Arrayoperatoren fuer EINEN lokalen DefaultGame-Vertrag lesen."""
    current, values = "", []
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1].casefold()
        elif current == section.casefold() and "=" in line and not line.startswith(";"):
            name, value = (part.strip() for part in line.split("=", 1))
            if not name:
                continue
            op = name[0] if name[0] in "+-.!" else "="
            entry_key = name[1:] if op != "=" else name
            if entry_key.casefold() != key.casefold():
                continue
            if len(value) >= 2 and value.startswith('"') and value.endswith('"'):
                value = value[1:-1]
            if op == "!":
                values.clear()
            elif op == "-":
                values = [entry for entry in values if entry != value]
            elif op == "=":
                values = [value]
            elif op == "." or value not in values:
                values.append(value)
    return values


def contract(text):
    dirs = array(text, PACKAGING, "DirectoriesToAlwaysCook")
    paths = []
    for entry in dirs:
        match = re.fullmatch(r'\(Path="(/Game/[^"*?]+)"\)', entry)
        if not match:
            raise AssertionError("Kein begrenzter Cook-Pfad: " + entry)
        paths.append(match[1])
    files = array(text, STAGING, "RuntimeFile")
    if set(paths) != COOK or set(files) != FILES or len(files) != len(FILES):
        raise AssertionError("Cook-/Dateiliste weicht vom gezielten Paketvertrag ab")
    if any(value.lower() not in ("false", "0")
           for value in array(text, PACKAGING, "bCookAll")):
        raise AssertionError("CookAll ist kein gezielter Paketvertrag")
    return paths, files


def asset_file(package):
    package = package.split(".", 1)[0]
    if not package.startswith("/Game/"):
        raise AssertionError("Kein Game-Asset: " + package)
    return ROOT / "Content" / (package[6:] + ".uasset")


def covered(package, paths):
    package = package.split(".", 1)[0]
    return any(package.startswith(path + "/") for path in paths)


def check_receipt(receipt, files):
    """UAT verwendet diese UFS-Abhaengigkeiten unter dem Projektpfad."""
    dependencies = receipt["RuntimeDependencies"]
    for relative in files:
        expected = "$(ProjectDir)/" + relative
        matches = [dep for dep in dependencies if dep["Path"].replace("\\", "/") == expected]
        if len(matches) != 1 or matches[0]["Type"] != "UFS":
            raise AssertionError("Receipt: UFS-Datei fehlt/falscher Typ: " + relative)
    selected = {dep["Path"].replace("\\", "/")[len("$(ProjectDir)/"):]
                for dep in dependencies
                if dep["Path"].replace("\\", "/").startswith(
                    ("$(ProjectDir)/Data/", "$(ProjectDir)/Content/Config/"))}
    if selected != set(files):
        raise AssertionError("Receipt: unerwartete Projekt-Laufzeitdaten: " + repr(selected - set(files)))


class RuntimePackagingTest(unittest.TestCase):
    def setUp(self):
        self.ini = read(ROOT / "Config/DefaultGame.ini")
        self.paths, self.files = contract(self.ini)

    def test_konfiguration_ist_gezielt_und_assets_existieren(self):
        for path in self.paths:
            directory = ROOT / "Content" / path[6:]
            self.assertTrue(directory.is_dir(), path)
            self.assertTrue(any(directory.rglob("*.uasset")), path)
        self.assertFalse(any("Data/Raw/OSM" in f or "Data/Raw/DEM" in f for f in self.files))

    def test_laufzeit_json_existieren_und_sind_lesbar(self):
        for path in self.files:
            with self.subTest(path=path):
                value = json.loads(read(ROOT / path))
                self.assertTrue(value, "Leere Laufzeitdaten: " + path)

    def test_runtime_verbraucher_und_assetpfade(self):
        source = ROOT / "Source/WiesbadenReal"
        consumers = [
            "World/WiesbadenBusStopMonitor.cpp", "World/SebboHqShape.cpp",
            "Vehicles/WiesbadenCarLightsComponent.cpp",
            "Vehicles/WiesbadenCarAudioComponent.cpp",
            "Vehicles/WiesbadenTireEffectsComponent.cpp",
        ]
        assets = set()
        for filename in consumers:
            text = read(source / filename)
            assets.update(re.findall(
                r'"(/Game/(?:Props/EsweHalte|SebboTower/Meshes|'
                r'Vehicles/Beetle/Restored|Audio/Samples)/[^"\s]+)"', text))
        # Zusammengesetzte Pfade der SurfaceName-/AmbienceBedPath-Lader.
        assets.update("/Game/Audio/Meta/MS_Step_" + name
                      for name in ("Asphalt", "Pflaster", "Wiese", "Innenraum"))
        assets.update("/Game/Audio/Meta/MS_Amb" + name
                      for name in ("Wind", "City", "Room", "Birds", "Night"))
        assets.add("/Game/Audio/Meta/MS_EngineBoxer")
        self.assertGreaterEqual(len(assets), 21, "Verbraucherscan fand zu wenig")
        for asset in assets:
            with self.subTest(asset=asset):
                self.assertTrue(asset_file(asset).is_file(), asset)
                self.assertTrue(covered(asset, self.paths), "Cook-Luecke: " + asset)
        for line in (3, 6):
            index = json.loads(read(ROOT / f"Data/Raw/Bus/announce_line{line}.json"))
            self.assertTrue(index["stops"])
            for stop in index["stops"]:
                self.assertTrue(asset_file(stop["asset"]).is_file(), stop["asset"])
                self.assertTrue(covered(stop["asset"], self.paths), stop["asset"])

    def test_json_ladepfade_bleiben_unveraendert(self):
        source = ROOT / "Source/WiesbadenReal"
        for filename, literals in {
            "Core/WiesbadenGameMode.cpp": ("line3.json", "line6.json", "line6_schedule.json"),
            "World/WiesbadenBusLineFile.cpp": ("Data/Raw/Bus",),
            "World/WiesbadenBusRoute.cpp": ("Data/Raw/Bus", "announce_line%s.json"),
            "Missions/WiesbadenMissionSubsystem.cpp": ("Data/Missions/missions.json",),
            "Store/WiesbadenStoreSubsystem.cpp": ("Data/Store/unlocks.json",),
            "GIS/RoadTypeLibrary.cpp": ("WiesbadenRoadTypes.json",),
            "GIS/WiesbadenTrafficSignCatalog.cpp": ("TrafficSignCatalog.json",),
        }.items():
            text = read(source / filename)
            for literal in literals:
                self.assertIn('TEXT("' + literal + '")', text, filename)

    def test_buildregel_liest_zentrale_liste_und_staged_ufs(self):
        rules = read(ROOT / "Source/WiesbadenReal/WiesbadenReal.Build.cs")
        rules = re.sub(r"//[^\n]*", "", rules)
        self.assertIn("ConfigCache.ReadHierarchy(", rules)
        self.assertIn('GetArray("WiesbadenReal.RuntimeStaging", "RuntimeFile", out RuntimeFiles)', rules)
        self.assertIn('foreach (string RelativePath in RuntimeFiles)', rules)
        self.assertIn('RuntimeDependencies.Add("$(ProjectDir)/" + RelativePath, StagedFileType.UFS);', rules)
        self.assertIn('ExternalDependencies.Add(', rules)
        self.assertIn('ConfigHierarchy.EnumerateConfigFileLocations(', rules)
        self.assertIn('FileReference.Exists(RuntimeFile)', rules)
        self.assertNotIn("Directory.GetFiles", rules)

    def test_gegenproben_fehlen_und_zu_breite_regeln_werden_rot(self):
        for bad in (
            self.ini.replace('+DirectoriesToAlwaysCook=(Path="/Game/Audio/Meta")', ""),
            self.ini.replace("+RuntimeFile=Data/Raw/Bus/line3.json", ""),
            self.ini.replace("+RuntimeFile=Data/Missions/missions.json", "+RuntimeFile=Data/Raw/*.json"),
            self.ini.replace('(Path="/Game/SebboTower/Meshes")', '(Path="/Game")'),
            self.ini.replace("+RuntimeFile=Data/Store/unlocks.json", "+RuntimeFile=../unlocks.json"),
            self.ini.replace('[/Script/UnrealEd.ProjectPackagingSettings]',
                             '[/Script/UnrealEd.ProjectPackagingSettings]\nbCookAll=True'),
        ):
            with self.assertRaises(AssertionError):
                contract(bad)

    def test_arrayleser_beachtet_entfernen_und_leeren(self):
        text = "[Test]\n+File=a\n+File=b\n-File=a\n"
        self.assertEqual(array(text, "Test", "File"), ["b"])
        self.assertEqual(array(text + "!File=ClearArray\n+File=c", "Test", "File"), ["c"])
        self.assertEqual(array('[TEST]\n+file="a"\n+FILE="b"\n-file="a"', "Test", "File"), ["b"])
        self.assertEqual(array("[Test]\n=ignored\n+File=", "Test", "File"), [""])

    def test_laufzeitdaten_sind_versioniert(self):
        """Ein frischer Clone baut sonst mit BuildException statt Paket.

        Data/Raw ist ueber .gitignore von Git ausgenommen; nur Data/Raw/Bus/*.json
        ist bewusst wieder zugelassen. Verlaesst eine der neun Dateien den Git,
        fehlt sie im Gate-Worktree und jeder Build dort bricht ab.
        """
        for path in sorted(FILES):
            with self.subTest(path=path):
                listed = subprocess.run(["git", "ls-files", "--error-unmatch", path],
                                        cwd=ROOT, capture_output=True, text=True,
                                        errors="replace", timeout=60)
                self.assertEqual(listed.returncode, 0, path + " ist nicht versioniert")

    def test_receipt_gegenprobe(self):
        receipt = {"RuntimeDependencies": [{"Path": "$(ProjectDir)/" + f, "Type": "UFS"}
                                           for f in self.files]}
        check_receipt(receipt, self.files)
        receipt["RuntimeDependencies"].append({"Path": "$(ProjectDir)/Data/Raw/OSM/wiesbaden.osm.json", "Type": "UFS"})
        with self.assertRaises(AssertionError):
            check_receipt(receipt, self.files)
        receipt["RuntimeDependencies"].pop()
        receipt["RuntimeDependencies"].append(receipt["RuntimeDependencies"][0].copy())
        with self.assertRaises(AssertionError):
            check_receipt(receipt, self.files)
        receipt["RuntimeDependencies"].pop()
        receipt["RuntimeDependencies"][0]["Type"] = "NonUFS"
        with self.assertRaises(AssertionError):
            check_receipt(receipt, self.files)
        receipt["RuntimeDependencies"].clear()
        with self.assertRaises(AssertionError):
            check_receipt(receipt, self.files)

    @unittest.skipUnless(os.environ.get("WB_PACKAGING_RECEIPT"), "UBT-Receipt separat nach Build pruefen")
    def test_echter_ubt_receipt(self):
        path = ROOT / os.environ["WB_PACKAGING_RECEIPT"]
        receipt = json.loads(read(path))
        self.assertEqual(receipt["TargetType"], "Game")
        self.assertEqual(receipt["Configuration"], "Development")
        check_receipt(receipt, self.files)


@unittest.skipUnless(os.environ.get("WB_PACKAGING_ENGINE"), "Isolierte UBT-Probe separat mit Engine-Lock starten")
class RuntimePackagingUbtTest(unittest.TestCase):
    """Echte Buildregel, UE-Hierarchie und inkrementeller Receipt im Wegwerfprojekt."""

    def test_arrayleser_gegen_ue(self):
        engine = Path(os.environ["WB_PACKAGING_ENGINE"])
        dotnet = engine / "Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe"
        assemblies = engine / "Binaries/DotNET/UnrealBuildTool"
        with tempfile.TemporaryDirectory(prefix="runtime-ini-", dir=ROOT / "Intermediate") as tmp:
            probe = Path(tmp)
            (probe / "Probe.csproj").write_text(
                '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
                '<OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework>'
                '<NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup>'
                + ''.join(f'<Reference Include="{name}"><HintPath>{assemblies / (name + ".dll")}</HintPath></Reference>'
                          for name in ("UnrealBuildTool", "EpicGames.Core"))
                + '</ItemGroup></Project>', encoding="utf-8")
            (probe / "Program.cs").write_text(
                'using System; using System.Collections.Generic; using System.Text.Json;\n'
                'using EpicGames.Core; using UnrealBuildTool;\n'
                'class Program { static void Main(string[] args) {\n'
                'var config = new ConfigHierarchy(new[] {new ConfigFile(new FileReference(args[0]), ConfigLineAction.Set)});\n'
                'List<string> values; config.GetArray("Test", "File", out values);\n'
                'Console.WriteLine(JsonSerializer.Serialize(values ?? new List<string>())); }}\n', encoding="ascii")
            result = subprocess.run([str(dotnet), "build", str(probe / "Probe.csproj"), "--nologo",
                                     "--ignore-failed-sources"], capture_output=True, text=True,
                                    encoding="utf-8", errors="replace", timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            dll = probe / "bin/Debug/net10.0/Probe.dll"
            cases = [
                '+File=a\n+File=b\n-File=a\n',
                '+File=a\n!File=ClearArray\n+File=c\n',
                '+File="a"\n+File="b"\n-File="a"\n',
                'File=a\nFile=b\n',
                '+file=a\n+FILE=b\n',
                '+File=a\n+File=a\n.File=a\n',
                '+File=\n',
                '=ignored\n+File=a\n',
            ]
            for case in cases:
                text = "[Test]\n" + case
                (probe / "Test.ini").write_text(text, encoding="ascii")
                result = subprocess.run([str(dotnet), str(dll), str(probe / "Test.ini")],
                                        capture_output=True, text=True, encoding="utf-8",
                                        errors="replace", timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                expected = json.loads(result.stdout.splitlines()[-1])
                with self.subTest(case=case, ue=expected):
                    self.assertEqual(array(text, "Test", "File"), expected)

    def test_uat_liest_die_auswahl_aus_dem_receipt(self):
        """UAT entscheidet an TargetReceipt.Read, ob eine Datei ueberhaupt
        gestaged wird - und ueberspringt sie sonst still.

        GEMESSEN am 30.09.2026: Read loest den Marker $(ProjectDir) auf, danach
        existieren alle neun Pfade, und GetStagedFileLocation (DeploymentContext)
        legt sie unterhalb des Projektwurzelrelativs ab. Ohne diesen Test prueft
        niemand, ob der Marker noch aufgeloest wird.
        """
        receipt = os.environ.get("WB_PACKAGING_RECEIPT")
        if not receipt:
            self.skipTest("WB_PACKAGING_RECEIPT fehlt: erst Game-Development-Builden")
        engine = Path(os.environ["WB_PACKAGING_ENGINE"])
        # Unreal.FindRootDirectory verlangt genau diese Struktur; UBT selbst
        # laeuft daraus, deshalb genuegt eine Kopie von Build.version.
        build_version = ROOT / "Intermediate" / "Engine" / "Build"
        build_version.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(engine / "Build/Build.version", build_version / "Build.version")
        work = (ROOT / "Intermediate" / "Engine" / "Binaries/DotNET/receiptprobe").resolve()
        work.mkdir(parents=True, exist_ok=True)
        assemblies = engine / "Binaries/DotNET/UnrealBuildTool"
        (work / "P.csproj").write_text(
            '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType>'
            '<TargetFramework>net10.0</TargetFramework></PropertyGroup><ItemGroup>'
            + "".join('<Reference Include="%s"><HintPath>%s</HintPath></Reference>' % (n, assemblies / (n + ".dll"))
                     for n in ("UnrealBuildTool", "EpicGames.Core"))
            + "</ItemGroup></Project>", encoding="utf-8")
        (work / "Program.cs").write_text(
            "using System; using System.Linq; using EpicGames.Core; using UnrealBuildTool;\n"
            "class Program { static void Main(string[] args) {\n"
            "  var r = TargetReceipt.Read(new FileReference(args[0]));\n"
            "  foreach (var d in r.RuntimeDependencies.Where(x => x.Path.FullName.Contains(args[1])))\n"
            "    Console.WriteLine(d.Path.FullName + \"|\" + FileReference.Exists(d.Path) + \"|\" + d.Type);\n"
            "} }\n", encoding="ascii")
        dotnet = engine / "Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe"
        bauen = subprocess.run([str(dotnet), "build", str(work / "P.csproj"), "--nologo", "-v", "q"],
                               capture_output=True, text=True, encoding="utf-8", errors="replace",
                               timeout=180, cwd=str(work))
        self.assertEqual(bauen.returncode, 0, bauen.stdout + bauen.stderr)
        lauf = subprocess.run([str(dotnet), str(work / "bin/Debug/net10.0/P.dll"),
                               str((ROOT / receipt).resolve()), "WiesbadenReal"],
                              capture_output=True, text=True, encoding="utf-8", errors="replace",
                              timeout=120, cwd=str(work))
        self.assertEqual(lauf.returncode, 0, lauf.stdout + lauf.stderr)
        gefunden = {}
        for zeile in lauf.stdout.splitlines():
            pfad, existiert, typ = zeile.split("|")
            gefunden[pfad.replace("\\", "/")] = (existiert, typ)
        for rel in sorted(FILES):
            with self.subTest(datei=rel):
                eintrag = gefunden.get(str(ROOT / rel).replace("\\", "/"))
                self.assertIsNotNone(eintrag, "nicht im Receipt: " + rel)
                self.assertEqual(eintrag, ("True", "UFS"), rel)

    def test_inkrementelle_hierarchie_und_fehler(self):
        """Der gecachte Receipt muss jede Aenderung der Auswahlliste mitnehmen.

        GEMESSEN am 30.09.2026 im Wegwerfprojekt: mit ExternalDependencies nur
        fuer DefaultGame.ini blieb eine nachtraeglich angelegte
        Config/Windows/WindowsGame.ini ohne Wirkung - UBT meldete
        "Result: Succeeded" und schrieb die alte Liste. Ohne jede
        ExternalDependency galt das zusaetzlich fuer DefaultGame.ini selbst.
        Deshalb enumeriert die Regel alle Orte, auch die nicht existierenden.
        """
        engine = Path(os.environ["WB_PACKAGING_ENGINE"])
        dotnet = engine / "Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe"
        ubt = engine / "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
        line3, line6, missions = ("Data/Raw/Bus/line3.json", "Data/Raw/Bus/line6.json",
                                  "Data/Missions/missions.json")
        with tempfile.TemporaryDirectory(prefix="runtime-packaging-", dir=ROOT / "Intermediate") as tmp:
            project = Path(tmp)
            source = project / "Source/WiesbadenReal"
            source.mkdir(parents=True)
            config = project / "Config/DefaultGame.ini"
            config.parent.mkdir()
            for name, quelle in (
                ("WiesbadenReal.uproject", ROOT / "WiesbadenReal.uproject"),
                ("Source/WiesbadenReal.Target.cs", ROOT / "Source/WiesbadenReal.Target.cs"),
                ("Source/WiesbadenReal/WiesbadenReal.Build.cs",
                 ROOT / "Source/WiesbadenReal/WiesbadenReal.Build.cs"),
            ):
                shutil.copyfile(quelle, project / name)
            (source / "Probe.cpp").write_text(
                '#include "Modules/ModuleManager.h"\n'
                'IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, WiesbadenReal, "WiesbadenReal");\n',
                encoding="ascii")
            for relative in (line3, line6, missions):
                (project / relative).parent.mkdir(parents=True, exist_ok=True)
                (project / relative).write_text("{}\n", encoding="ascii")
            base = "[" + STAGING + "]\n+RuntimeFile=" + line3 + "\n"
            config.write_text(base, encoding="ascii")
            override = project / "Config/Windows/WindowsGame.ini"
            receipt = project / "Binaries/Win64/WiesbadenReal.target"

            def lauf():
                result = subprocess.run(
                    [str(dotnet), str(ubt), "WiesbadenReal", "Win64", "Development",
                     "-Project=" + str(project / "WiesbadenReal.uproject"),
                     "-NoHotReloadFromIDE", "-NoUBA", "-NoEngineChanges"],
                    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600)
                return result.returncode, result.stdout + result.stderr

            def auswahl():
                code, output = lauf()
                self.assertEqual(code, 0, output)
                pfad = lambda dep: dep["Path"].replace("\\", "/")
                return sorted(pfad(dep)[len("$(ProjectDir)/"):]
                              for dep in json.loads(read(receipt))["RuntimeDependencies"]
                              if pfad(dep).startswith("$(ProjectDir)/Data/"))

            def fehler():
                code, output = lauf()
                self.assertNotEqual(code, 0, output)
                self.assertIn("RuntimeStaging:", output)

            self.assertEqual(auswahl(), [line3])
            # Der Plattform-Override ist additiv (gemessene UE-Array-Semantik)
            # und wird erst nach dem gecachten Makefile angelegt, dann geaendert.
            override.parent.mkdir(parents=True, exist_ok=True)
            override.write_text("[" + STAGING + "]\n+RuntimeFile=" + line6 + "\n", encoding="ascii")
            self.assertEqual(auswahl(), sorted([line3, line6]))
            override.write_text("[" + STAGING + "]\n+RuntimeFile=" + missions + "\n", encoding="ascii")
            self.assertEqual(auswahl(), sorted([line3, missions]))
            # Ein LEERER Override muss den Build stoppen, nicht still verschwinden.
            override.write_text("[" + STAGING + "]\n!RuntimeFile=ClearArray\n", encoding="ascii")
            fehler()
            override.unlink()
            self.assertEqual(auswahl(), [line3])
            # Grenzwerte und fehlende Daten laufen durch den echten Regelkonstruktor.
            for wert in ("", "../line6.json", "Data/Raw/*.json", "C:/x.json",
                         "Data/Raw/Bus/line6.txt", "data/raw/bus/line6.json", "Data/Raw/Bus/fehlt.json"):
                config.write_text("[" + STAGING + "]\n+RuntimeFile=" + wert + "\n", encoding="utf-8")
                fehler()
            config.write_text("[" + STAGING + "]\n", encoding="ascii")
            fehler()
            config.write_text(base, encoding="ascii")
            self.assertEqual(auswahl(), [line3])
            (project / line3).unlink()
            fehler()


if __name__ == "__main__":
    unittest.main()
