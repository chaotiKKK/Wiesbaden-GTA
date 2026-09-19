"""Holt den gebackenen Stadt-Inhalt der Karte WiesbadenCity_Alkis16 in einen frischen Klon.

Warum es dieses Skript gibt
---------------------------
`Content/__ExternalActors__/`, `Content/Generated/` und `Content/Materials/AAA/`
stehen in der .gitignore, weil eine einzige gebackene Stadt 2,5 GB belegt
(groesste Einzeldatei 1,21 GB - ueber GitHubs Grenze von 100 MB fuer normale
Git-Objekte), dazu 194 MB Bake-Materialien. Git LFS scheidet aus
Kostengruenden aus: frei sind 1 GiB Speicher und 1 GiB Bandbreite pro Monat,
ein Klon braucht mehr als das Doppelte. Darum liegt der Inhalt gepackt als
Release-Asset (unbegrenzte Bandbreite) und dieses Skript holt ihn.

Ablauf je Paket
---------------
1. Archiv aus dem GitHub-Release laden (`gh` wenn vorhanden - kann auch private
   Repos - sonst HTTPS mit GH_TOKEN/GITHUB_TOKEN).
2. sha256 des Archivs gegen die hier fest hinterlegte Summe pruefen.
3. In die Projektwurzel entpacken.
4. Jede Datei einzeln gegen die mitgelieferte INHALT-Liste pruefen.

Aufruf
------
    python Tools/fetch_city_content.py              # alles holen und entpacken
    python Tools/fetch_city_content.py --check      # nur pruefen, was schon da ist
    python Tools/fetch_city_content.py --asset AaaBakeMaterialien.zip
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

REPO = "chaotiKKK/Wiesbaden-GTA"
TAG = "city-content-alkis16"

# Was gehoert zu einem spielbaren Klon? Je Paket: Archivname, erwartete Summe
# (ein vertauschtes Asset faellt damit auf) und die INHALT-Liste, die im Archiv
# steckt und nach dem Entpacken in der Projektwurzel liegt.
PAKETE = [
    {
        "asset": "WiesbadenCity_Alkis16_content.zip",
        "sha256": "5e890118f13541c3022402d9d82032319e78c954590f35becc461486d58f5cfa",
        "manifest": "INHALT.sha256",
        "inhalt": "Karte WiesbadenCity_Alkis16: Actor-Pakete + gebackene Kacheln (2019 + 4578 Dateien)",
    },
    {
        "asset": "AaaBakeMaterialien.zip",
        "sha256": "b4eec803975bf96585fe65599b384fd3bcd01f124b32af2a4a5df1237339deb3",
        "manifest": "INHALT.BakeMaterialien.sha256",
        "inhalt": "Bake-Materialien Content/Materials/AAA (51 Dateien) - von den "
                  "Nerobergbahn-Materialien referenzierte Fassadentexturen",
    },
]

PROJEKT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sha256_datei(pfad):
    h = hashlib.sha256()
    with open(pfad, "rb") as fh:
        for block in iter(lambda: fh.read(4 << 20), b""):
            h.update(block)
    return h.hexdigest()


def github_token():
    """Liest explizite Tokens oder uebernimmt die lokale gh-Anmeldung."""
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        return token
    gh = shutil.which("gh")
    if not gh:
        return ""
    try:
        result = subprocess.run(
            [gh, "auth", "token"], capture_output=True, text=True, check=False)
    except OSError:
        return ""
    return result.stdout.strip() if result.returncode == 0 else ""


def _sicherer_pfad(basis, relativ):
    """Liefert einen Pfad unter `basis`; absolute/traversierende Pfade sind ungueltig."""
    relativ = relativ.replace("/", os.sep).replace("\\", os.sep)
    if not relativ or os.path.isabs(relativ) or os.path.splitdrive(relativ)[0]:
        raise ValueError("Manifest-/Zip-Pfad ist nicht relativ: %r" % relativ)
    root = os.path.abspath(basis)
    ziel = os.path.abspath(os.path.join(root, relativ))
    try:
        innerhalb = os.path.commonpath((root, ziel)) == root
    except ValueError:
        innerhalb = False
    if not innerhalb:
        raise ValueError("Manifest-/Zip-Pfad verlaesst das Ziel: %r" % relativ)
    return ziel


def _manifest_eintraege(zeilen, basis):
    eintraege = []
    for nummer, zeile in enumerate(zeilen, 1):
        text = zeile.strip().lstrip("\ufeff")
        if not text or text.startswith("#"):
            continue
        if nummer == 1 and text.startswith("WiesbadenReal - "):
            continue
        if nummer == 2 and re.fullmatch(
                r"sha256\s+Groesse\s+Pfad \(relativ zur Projektwurzel\)", text):
            continue
        teile = text.split(None, 2)
        if len(teile) != 3:
            raise ValueError("ungueltige Manifestzeile %d" % nummer)
        soll, groesse_text, relativ = teile
        if not re.fullmatch(r"[0-9a-fA-F]{64}", soll):
            raise ValueError("ungueltige SHA-256 in Manifestzeile %d" % nummer)
        try:
            groesse = int(groesse_text)
        except ValueError as exc:
            raise ValueError("ungueltige Dateigroesse in Manifestzeile %d" % nummer) from exc
        if groesse < 0:
            raise ValueError("negative Dateigroesse in Manifestzeile %d" % nummer)
        _sicherer_pfad(basis, relativ)
        eintraege.append((soll, groesse, relativ))
    if not eintraege:
        raise ValueError("Manifest enthaelt keine Dateien")
    return eintraege


def hole_archiv(asset, ziel, repo, nur_https):
    """Laedt ein Asset nach `ziel`. gh zuerst (private Repos), sonst HTTPS."""
    gh = shutil.which("gh")
    if gh and not nur_https:
        print("  Lade ueber gh: %s" % asset)
        r = subprocess.run([gh, "release", "download", TAG, "--repo", repo,
                            "--pattern", asset, "--dir", os.path.dirname(ziel),
                            "--clobber"])
        if r.returncode == 0 and os.path.exists(ziel):
            return True
        print("  gh-Ablauf fehlgeschlagen (Exit %d) - versuche HTTPS." % r.returncode)
    token = github_token()
    url = "https://api.github.com/repos/%s/releases/tags/%s" % (repo, TAG)
    kopf = {"Accept": "application/vnd.github+json", "User-Agent": "wb-fetch"}
    if token:
        kopf["Authorization"] = "Bearer " + token
    print("  Lade ueber HTTPS: %s" % url)
    with urllib.request.urlopen(urllib.request.Request(url, headers=kopf)) as a:
        daten = json.load(a)
    eintrag = next((x for x in daten["assets"] if x["name"] == asset), None)
    if not eintrag:
        print("  Asset %s fehlt im Release %s." % (asset, TAG))
        return False
    kopf2 = {"Accept": "application/octet-stream", "User-Agent": "wb-fetch"}
    if token:
        kopf2["Authorization"] = "Bearer " + token
    with urllib.request.urlopen(urllib.request.Request(eintrag["url"], headers=kopf2)) as q, \
            open(ziel, "wb") as fh:
        shutil.copyfileobj(q, fh, 4 << 20)
    return True


def pruefe_manifest(manifest_pfad, basis):
    fehlend, falsch, ok = 0, 0, 0
    with open(manifest_pfad, "r", encoding="utf-8", errors="replace") as fh:
        eintraege = _manifest_eintraege(fh.read().splitlines(), basis)
    for soll, groesse, rel in eintraege:
        p = _sicherer_pfad(basis, rel)
        if not os.path.exists(p):
            fehlend += 1
            if fehlend <= 5:
                print("    fehlt: %s" % rel)
            continue
        if os.path.getsize(p) != groesse or sha256_datei(p) != soll:
            falsch += 1
            if falsch <= 5:
                print("    stimmt nicht: %s" % rel)
            continue
        ok += 1
    return ok, fehlend, falsch


def entpacke_sicher(archiv, ziel):
    """Prueft alle Zip-Namen vor dem Entpacken und verhindert Path Traversal."""
    ziel = os.path.abspath(ziel)
    for info in archiv.infolist():
        _sicherer_pfad(ziel, info.filename)
        modus = (info.external_attr >> 16) & 0o170000
        if modus == stat.S_IFLNK:
            raise ValueError("Zip-Symlink ist nicht erlaubt: %s" % info.filename)
    archiv.extractall(ziel)


def main():
    ap = argparse.ArgumentParser(description="Stadt-Inhalt (WiesbadenCity_Alkis16) holen")
    ap.add_argument("--repo", default=REPO)
    ap.add_argument("--asset", help="nur dieses Paket holen (Standard: alle)")
    ap.add_argument("--check", action="store_true", help="nichts laden, nur Vorhandenes pruefen")
    ap.add_argument("--https", action="store_true", help="gh nicht benutzen, direkt HTTPS")
    ap.add_argument("--ohne-sha", action="store_true", help="Archivsumme nicht pruefen (nur zum Testen)")
    args = ap.parse_args()

    pakete = [p for p in PAKETE if not args.asset or p["asset"] == args.asset]
    if not pakete:
        print("Unbekanntes Paket: %s" % args.asset)
        return 2

    fehler = 0
    if args.check:
        for p in pakete:
            m = os.path.join(PROJEKT, p["manifest"])
            if not os.path.exists(m):
                print("%s: keine %s gefunden - noch nie geholt?" % (p["asset"], p["manifest"]))
                fehler += 1
                continue
            try:
                ok, fehlend, falsch = pruefe_manifest(m, PROJEKT)
            except ValueError as exc:
                print("%s: ungueltiges Manifest: %s" % (p["asset"], exc))
                fehler += 1
                continue
            print("%-32s %d in Ordnung, %d fehlend, %d falsch" % (p["asset"], ok, fehlend, falsch))
            fehler += 1 if (fehlend or falsch) else 0
        return 1 if fehler else 0

    with tempfile.TemporaryDirectory(prefix="wb_city_") as tmp:
        for p in pakete:
            print(p["asset"])
            print("  %s" % p["inhalt"])
            archiv = os.path.join(tmp, p["asset"])
            if not hole_archiv(p["asset"], archiv, args.repo, args.https):
                return 2
            print("  Archiv: %.2f GB" % (os.path.getsize(archiv) / 1e9))
            if not args.ohne_sha:
                ist = sha256_datei(archiv)
                if (ist.lower() != p["sha256"].lower()
                        and not args.https and shutil.which("gh")):
                    print("  gh-Download unvollstaendig - versuche HTTPS erneut.")
                    try:
                        os.remove(archiv)
                    except OSError:
                        pass
                    if hole_archiv(p["asset"], archiv, args.repo, True):
                        ist = sha256_datei(archiv)
                if ist.lower() != p["sha256"].lower():
                    print("  ABBRUCH: Summe %s weicht von %s ab." % (ist, p["sha256"]))
                    return 3
                print("  Summe stimmt: %s" % ist)
            with zipfile.ZipFile(archiv) as z:
                print("  Entpacke %d Eintraege nach %s" % (len(z.namelist()), PROJEKT))
                try:
                    entpacke_sicher(z, PROJEKT)
                except ValueError as exc:
                    print("  ABBRUCH: unsicherer Archivpfad: %s" % exc)
                    return 4
            m = os.path.join(PROJEKT, p["manifest"])
            try:
                ok, fehlend, falsch = pruefe_manifest(m, PROJEKT)
            except ValueError as exc:
                print("  ABBRUCH: ungueltiges Manifest: %s" % exc)
                return 4
            print("  Dateipruefung: %d in Ordnung, %d fehlend, %d falsch" % (ok, fehlend, falsch))
            if fehlend or falsch:
                fehler += 1
    if fehler:
        return 1
    print("Fertig. Stadt starten mit: Wiesbaden_spielen.cmd")
    return 0


if __name__ == "__main__":
    sys.exit(main())
