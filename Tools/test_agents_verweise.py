"""Waechter ueber AGENTS.md: Verweise auf Dateien und Karten muessen tragen.

WOZU: AGENTS.md ist das Gedaechtnis des Projekts - und es verweist auf
Skripte, Quellen und Karten, die sich staendig bewegen. Ein toter Verweis ist
schlimmer als keiner: der naechste Agent startet `fps_alkis10.cmd` oder backt
auf `Alkis30`, weil es so dasteht, und sucht dann eine Stunde nach einem
Fehler, den es nicht gibt. Der Karten-Eintrag "Map names move fast" nannte
monatelang eine geloeschte Karte als Standard.

Zwei Pruefungen:

* **Dateien** - jeder Pfad in Backticks, der wie eine Projektdatei aussieht,
  muss im Baum liegen (auch relativ zu `Source/WiesbadenReal/`, `/Game/...`
  als Asset unter `Content/`). Ausgenommen sind Engine-Dateien, Beispiele und
  Pfade ausserhalb des Repos (Listen unten, je mit Grund). Gitignorierte
  Pfade, die in DIESEM Checkout fehlen, werden nicht beurteilt (frischer Klon
  ohne Rohdaten). Steht in der Naehe, dass die Datei geloescht, verschoben
  oder nur Geschichte ist, gilt der Verweis als bewusst historisch.
* **Karten** - der Karten-Eintrag nennt die Standardkarte; sie muss die aus
  `Config/DefaultEngine.ini` sein. Seine Liste "Versioned in git" muss genau
  die versionierten Stadtkarten nennen. Jede in AGENTS.md erwaehnte Stadtkarte
  (`AlkisNN`) muss dort als versioniert, geloescht oder lokaler Rest stehen.
  Wer eine Karte loescht, traegt sie dort ein - dann sind alle historischen
  Erwaehnungen gedeckt. Gezaehlt wird nur, was Git kennt: unversionierte
  .umap liegen in jedem Checkout anders (der Gate-Worktree hat sie nicht),
  und ein Test, der je nach Rechner anders ausfaellt, prueft nichts.

Aufruf (aus der Projektwurzel):
    python -m unittest discover -s Tools -p "test_agents_verweise.py"
"""
import re
import subprocess
import unittest
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
AGENTS = WURZEL / "AGENTS.md"
DEFAULT_ENGINE = WURZEL / "Config" / "DefaultEngine.ini"

# Pfade, die mit diesen Ordnern beginnen, sind Projektdateien.
PROJEKT_ORDNER = ("Tools", "Source", "Content", "Config", "docs", "Data",
                  "Plugins", "Archiv", "Scripts")
# Ohne Ordner zaehlt ein Name nur mit einer dieser Endungen als Datei.
DATEI_ENDUNGEN = (".py", ".cmd", ".ps1", ".mjs", ".js", ".bat", ".json",
                  ".ini", ".md", ".h", ".cpp", ".cs", ".uproject", ".umap")
# Skripte und Quellen: liegen nie nur als gitignorierte Rohdaten vor.
CODE_ENDUNGEN = (".py", ".cmd", ".ps1", ".mjs", ".js", ".bat", ".h", ".cpp", ".cs")

# Liegen ausserhalb dieses Repos (Sicherungs-Huelle, Engine, anderer Rechner).
EXTERN_PRAEFIXE = (".planning/", ".freebuff/", ".git/", "WiesbadenReal/",
                   "Audioaufzeichnungen/", "Engine/", "UE_5.8/")
# Engine-Dateien, die AGENTS.md als Fundstelle der UE-Quelle nennt.
ENGINE_DATEIEN = {"EngineUtils.h", "Material.h", "SceneComponent.cpp",
                  "UObjectGlobals.h", "Build.bat", "GameMode.cpp"}
# Muster und Platzhalter, keine echten Dateien.
BEISPIELE = {"./script.cmd", "script.cmd", "Tools/x.cmd", "x.cmd",
             "parts/chunk_0004.json",
             # Beschreibt die FEHLENDE Datei ("Ohne line3_schedule.json ...").
             "line3_schedule.json",
             # Beispielpfad fuer die MSYS-Umschreibung von /-Argumenten.
             "/Game/Maps/WiesbadenCity_HiRes",
             # Code-Vorgabe von MapAssetPath, keine Karte auf der Platte.
             "/Game/Maps/WiesbadenCity",
             # Familienname fuer rebake_alkis2N.cmd.
             "rebake_alkis2x.cmd"}
# Kartennamen, die keine Karte meinen.
KARTEN_BEISPIELE = {
    # Log-Artefakt eines .cmd-Fehlers (%10 = %1 + "0"), wird dort erklaert.
    "Alkis150",
}

# Worte, mit denen AGENTS.md sagt: "das gibt es nicht mehr, und das ist Absicht".
WEG_MARKER = re.compile(
    r"geloescht|gel\u00f6scht|deleted|removed|no longer|existier\w* (?:hier )?nicht"
    r"|gibt es (?:hier )?(?:nicht|keine)|nicht mehr|entfernt|verschoben|mit weg"
    r"|fr(?:ue|\u00fc)her|ehemal|abgeloest|umbenannt|nur noch beispiel"
    r"|als geschichte|historisch|archiviert|ausgelagert",
    re.IGNORECASE)
MARKER_FENSTER = 250  # Zeichen vor/nach dem Verweis im selben Absatz
# Eine Stadtkarte im Fliesstext: Alkis31, `WiesbadenCity_Alkis31`. Links kein
# \b: zwischen "_" und "A" liegt fuer re keine Wortgrenze.
KARTEN_MUSTER = r"(?<![A-Za-z0-9])Alkis(\d+)\b"


def git(*args):
    return subprocess.run(["git", *args], cwd=WURZEL,
                          capture_output=True, text=True).stdout


def gitignoriert(pfade):
    """Die Pfade, die .gitignore ausschliesst.

    NUL-getrennt und als Bytes: im Textmodus schreibt Python unter Windows
    CRLF in die Eingabe, Git sucht dann nach "Saved/x.json" plus Wagenruecklauf
    - nichts gilt als ignoriert, und der Test waere nur in Checkouts gruen, in
    denen die ignorierten Dateien zufaellig auf der Platte liegen.
    """
    if not pfade:
        return set()
    aus = subprocess.run(["git", "check-ignore", "--stdin", "-z"], cwd=WURZEL,
                         input="\0".join(pfade).encode("utf-8"),
                         capture_output=True).stdout
    return {p for p in aus.decode("utf-8").split("\0") if p}


def absaetze(text):
    """(erste Zeilennummer, Absatztext) - Absatz = Block bis Leerzeile/Punkt."""
    block, start = [], 1
    for nr, zeile in enumerate(text.splitlines(), 1):
        neu = not zeile.strip() or zeile.startswith(("- ", "#", "* "))
        if neu and block:
            yield start, block
            block = []
        if zeile.strip():
            if not block:
                start = nr
            block.append(zeile)
    if block:
        yield start, block


def fundstellen(text, muster):
    """(Zeile, Treffer, Kontext) fuer jedes Vorkommen von `muster`."""
    for start, zeilen in absaetze(text):
        flach = " ".join(zeilen)
        for m in re.finditer(muster, flach):
            # Zeile des Treffers im Absatz bestimmen.
            vorher = flach[:m.start()]
            zeile, laenge = start, 0
            for i, z in enumerate(zeilen):
                laenge += len(z) + 1
                if laenge > len(vorher):
                    zeile = start + i
                    break
            kontext = flach[max(0, m.start() - MARKER_FENSTER):m.end() + MARKER_FENSTER]
            yield zeile, m, kontext


def ausklammern(verweis):
    """`Tools/a.{py,cmd}` -> Tools/a.py, Tools/a.cmd."""
    m = re.search(r"\{([^{}]+)\}", verweis)
    if not m:
        return [verweis]
    return [verweis[:m.start()] + teil + verweis[m.end():]
            for teil in m.group(1).split(",")]


def ist_dateiverweis(v):
    if not v or re.search(r"[\s<>*%$|]|\.\.\.", v) or re.match(r"^[A-Za-z]:", v):
        return False
    if re.fullmatch(r"\.\w+", v):          # nur eine Endung (".cmd")
        return False
    if v.startswith("/Game/"):
        return True
    if v.startswith("/"):
        return False
    erster = v.split("/")[0]
    return (erster in PROJEKT_ORDNER and "/" in v) or v.lower().endswith(DATEI_ENDUNGEN)


class Baum:
    """Was es gibt: verfolgte Dateien plus alles, was auf der Platte liegt."""

    def __init__(self):
        self.verfolgt = set(git("ls-files").splitlines())
        self.namen = {}
        for pfad in self.verfolgt:
            self.namen.setdefault(pfad.rsplit("/", 1)[-1], []).append(pfad)
        # Gitignorierte Dateien, die blosse Namen oft meinen: Rohdaten
        # (wiesbaden.osm.moebel.json) und lose Dateien im Stamm.
        rohdaten = WURZEL / "Data" / "Raw"
        # Data/Raw gibt es auch im frischen Klon (Data/Raw/Bus ist versioniert);
        # die gitignorierten Rohdaten erkennt man an Data/Raw/OSM.
        self.rohdaten_fehlen = not (rohdaten / "OSM").is_dir()
        gefunden = list(WURZEL.glob("*.*"))
        if not self.rohdaten_fehlen:
            gefunden += rohdaten.rglob("*.*")
        for p in gefunden:
            if p.is_file():
                self.namen.setdefault(p.name, []).append(str(p))

    def gibt_es(self, pfad):
        pfad = pfad.rstrip("/")
        return pfad in self.verfolgt or (WURZEL / pfad).exists()

    def aufloesen(self, v):
        """True, wenn der Verweis eine vorhandene Datei trifft."""
        v = v.split("::")[0]
        if v.startswith("/Game/"):
            asset = "Content/" + v[len("/Game/"):].split(".")[0]
            return any(self.gibt_es(asset + e) for e in ("", ".uasset", ".umap"))
        if "/" in v:
            kandidaten = [v, "Source/WiesbadenReal/" + v]
            if v.startswith("Content/") and "." not in v.rsplit("/", 1)[-1]:
                kandidaten += [v + ".uasset", v + ".umap"]
            return any(self.gibt_es(k) for k in kandidaten)
        # Blosser Name: irgendwo im Baum, auch als Namensende (Build.cs).
        if v in self.namen or any(n.endswith(v) for n in self.namen):
            return True
        # Ohne Rohdaten (frischer Klon) ist ein unbekannter DATEN-Name nicht
        # beurteilbar. Skripte und Quellen sind immer versioniert - ein
        # unbekanntes `fps_alkis10.cmd` ist auch dort tot.
        return self.rohdaten_fehlen and not v.lower().endswith(CODE_ENDUNGEN)


def standardkarte():
    text = DEFAULT_ENGINE.read_text(encoding="utf-8")
    m = re.search(r"^GameDefaultMap=/Game/Maps/WiesbadenCity_(Alkis\d+)\.", text, re.M)
    return m.group(1) if m else None


def karten_eintrag(text):
    """Der Absatz "Map names move fast" - die Kartenwahrheit von AGENTS.md."""
    for _, zeilen in absaetze(text):
        flach = " ".join(zeilen)
        if "Map names move fast" in flach:
            return flach
    return None


def nummern(ausdruck):
    """'2/3/7/10-13/15' oder '27-30' -> {2, 3, 7, 10, 11, 12, 13, 15}."""
    ergebnis = set()
    for teil in re.split(r"[/,]\s*", ausdruck):
        m = re.fullmatch(r"(\d+)\s*(?:-|\.\.|bis)\s*(?:Alkis)?(\d+)", teil.strip())
        if m:
            ergebnis.update(range(int(m.group(1)), int(m.group(2)) + 1))
        elif teil.strip().isdigit():
            ergebnis.add(int(teil.strip()))
    return ergebnis


def geloeschte_karten(eintrag):
    """Alle Karten, die der Karten-Eintrag als geloescht fuehrt."""
    weg = set()
    # "`Alkis4` no longer exists (deleted ..., together with Alkis2/3/7/...)"
    for m in re.finditer(r"Alkis(\d+)`? (?:no longer exists|is deleted|deleted)", eintrag):
        weg.add(int(m.group(1)))
    for m in re.finditer(r"together with Alkis([\d/\-, ]+)", eintrag):
        weg |= nummern(m.group(1))
    # "`Alkis27`..`Alkis30` deleted" / "Alkis27-30 deleted"
    for m in re.finditer(r"Alkis(\d+)`?\s*(?:-|\.\.)\s*`?(?:Alkis)?(\d+)`? (?:are |were )?deleted",
                         eintrag):
        weg.update(range(int(m.group(1)), int(m.group(2)) + 1))
    return weg


def karten_liste(eintrag, schluessel):
    """Die `AlkisNN` hinter "<schluessel>" bis zum naechsten Semikolon/Punkt."""
    m = re.search(re.escape(schluessel) + r"([^;]*)", eintrag)
    return {int(n) for n in re.findall(r"Alkis(\d+)", m.group(1))} if m else None


def lokale_reste(eintrag):
    """"`Alkis18`..`Alkis24` exist at most as unversioned local leftovers"."""
    rest = set()
    for m in re.finditer(r"`Alkis(\d+)`\s*\.\.\s*`Alkis(\d+)` exist at most as unversioned",
                         eintrag):
        rest.update(range(int(m.group(1)), int(m.group(2)) + 1))
    return rest


def versionierte_karten(baum):
    return {int(m.group(1)) for p in baum.verfolgt
            for m in [re.fullmatch(r"Content/Maps/WiesbadenCity_Alkis(\d+)\.umap", p)] if m}


class AgentsVerweise(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.text = AGENTS.read_text(encoding="utf-8")
        cls.baum = Baum()

    def test_dateiverweise_tragen(self):
        kandidaten = []
        for zeile, m, kontext in fundstellen(self.text, r"`([^`\n]+)`"):
            roh = m.group(1).strip().replace("\\", "/")
            for v in ausklammern(roh):
                if (not ist_dateiverweis(v) or v in BEISPIELE or v in ENGINE_DATEIEN
                        or v.startswith(EXTERN_PRAEFIXE)):
                    continue
                if not self.baum.aufloesen(v):
                    kandidaten.append((zeile, v, kontext))

        # Gitignoriert und hier nicht vorhanden: nicht beurteilbar. Assets
        # werden als die Datei geprueft, die sie auf der Platte waeren.
        def pruefpfad(v):
            if v.startswith("/Game/"):
                return "Content/" + v[len("/Game/"):].split(".")[0] + ".uasset"
            return v
        # Ordner ohne Schraegstrich (`Content/__ExternalActors__`) trifft ein
        # Muster "Ordner/" nur mit angehaengtem "/".
        pfade = {pruefpfad(v) for _, v, _ in kandidaten}
        ignoriert = gitignoriert(sorted(pfade | {p.rstrip("/") + "/" for p in pfade}))

        def ist_ignoriert(v):
            p = pruefpfad(v)
            return p in ignoriert or p.rstrip("/") + "/" in ignoriert

        tot = sorted({(z, v) for z, v, k in kandidaten
                      if not ist_ignoriert(v) and not WEG_MARKER.search(k)})
        self.assertEqual(
            tot, [],
            "AGENTS.md verweist auf Dateien, die es nicht (mehr) gibt - Verweis "
            "berichtigen oder in der Naehe sagen, dass sie geloescht/verschoben ist:\n"
            + "\n".join("  Zeile %d: %s" % zv for zv in tot))

    def test_karten_eintrag_nennt_die_standardkarte(self):
        eintrag = karten_eintrag(self.text)
        self.assertIsNotNone(eintrag, "Karten-Eintrag 'Map names move fast' fehlt")
        m = re.search(r"live default in `Config/DefaultEngine.ini` is `(Alkis\d+)`", eintrag)
        self.assertIsNotNone(m, "Karten-Eintrag nennt die Standardkarte nicht mehr "
                             "im Muster 'live default in `Config/DefaultEngine.ini` is `AlkisNN`'")
        self.assertEqual(
            m.group(1), standardkarte(),
            "Karten-Eintrag nennt %s als Standardkarte, Config/DefaultEngine.ini "
            "startet %s" % (m.group(1), standardkarte()))

    def test_versionierte_karten_stimmen(self):
        eintrag = karten_eintrag(self.text) or ""
        genannt = karten_liste(eintrag, "Versioned in git:")
        self.assertIsNotNone(genannt, "Karten-Eintrag hat keine Liste 'Versioned in git: ...'")
        echt = versionierte_karten(self.baum)
        self.assertEqual(
            genannt, echt,
            "Karten-Eintrag 'Versioned in git' stimmt nicht: fehlt %s, nicht versioniert %s"
            % (sorted(echt - genannt), sorted(genannt - echt)))

    def test_erwaehnte_karten_sind_im_karten_eintrag_gefuehrt(self):
        eintrag = karten_eintrag(self.text) or ""
        bekannt = (versionierte_karten(self.baum) | geloeschte_karten(eintrag)
                   | lokale_reste(eintrag))
        fehlend = {}
        for zeile, m, _ in fundstellen(self.text, KARTEN_MUSTER):
            name = "Alkis" + m.group(1)
            if name not in KARTEN_BEISPIELE and int(m.group(1)) not in bekannt:
                fehlend.setdefault(name, []).append(zeile)
        self.assertEqual(
            fehlend, {},
            "AGENTS.md erwaehnt Karten, die im Karten-Eintrag 'Map names move fast' "
            "weder versioniert noch geloescht noch lokaler Rest sind: "
            + "; ".join("%s (Zeilen %s)" % (k, ", ".join(map(str, sorted(set(z)))))
                        for k, z in sorted(fehlend.items())))


class Hilfen(unittest.TestCase):
    """Die Leser selbst - ein stummer Leser waere 'alles gruen'."""

    def test_nummern(self):
        self.assertEqual(nummern("2/3/7/10-13/15"), {2, 3, 7, 10, 11, 12, 13, 15})

    def test_geloeschte_karten(self):
        e = ("`Alkis4` no longer exists (deleted 2026-09-19, together with "
             "Alkis2/3/10-13); `Alkis27`..`Alkis30` deleted")
        self.assertEqual(geloeschte_karten(e), {2, 3, 4, 10, 11, 12, 13, 27, 28, 29, 30})

    def test_karten_listen(self):
        e = ("Versioned in git: `Alkis16`, `Alkis31`; `Alkis18`..`Alkis20` exist "
             "at most as unversioned local leftovers")
        self.assertEqual(karten_liste(e, "Versioned in git:"), {16, 31})
        self.assertEqual(lokale_reste(e), {18, 19, 20})

    def test_gitignoriert_unter_windows(self):
        # Der Fehler von damals: Wagenruecklauf am Pfad, nichts galt als ignoriert.
        self.assertEqual(gitignoriert(["Saved/probe.json", "Tools/test_agents_verweise.py"]),
                         {"Saved/probe.json"})

    def test_karten_muster_findet_karten(self):
        # Ein Muster, das nichts findet, macht die Kartenpruefung stumm-gruen
        # (so geschehen: ein Steuerzeichen statt \b im Muster).
        text = "- Standard war `WiesbadenCity_Alkis30`, davor Alkis4 und Alkis150x.\n"
        self.assertEqual([m.group(1) for _, m, _ in fundstellen(text, KARTEN_MUSTER)],
                         ["30", "4"])

    def test_ausklammern(self):
        self.assertEqual(ausklammern("Tools/a.{py,cmd}"), ["Tools/a.py", "Tools/a.cmd"])

    def test_dateiverweis_erkennung(self):
        self.assertTrue(ist_dateiverweis("Tools/foo.py"))
        self.assertTrue(ist_dateiverweis("anchor_bounds.cmd"))
        self.assertTrue(ist_dateiverweis("/Game/Maps/WiesbadenCity_Alkis31"))
        self.assertFalse(ist_dateiverweis(".cmd"))
        self.assertFalse(ist_dateiverweis("C:/Users/x.py"))
        self.assertFalse(ist_dateiverweis("GetComponentsBoundingBox"))


if __name__ == "__main__":
    unittest.main()
