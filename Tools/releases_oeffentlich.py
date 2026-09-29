# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""Spiegelt die Meilenstein-Releases in das OEFFENTLICHE Schaufenster-Repo.

GEMESSEN am 27.09.2026, warum es das gibt: von allen 37 Release-Assets des
privaten Spiel-Repos liefert **keines** etwas an jemanden ohne Konto - alle
37 antworten anonym mit 404, die Release-Seite selbst ebenfalls (kein
Login-Redirect, gar nichts). Die Bilder in den Release-Texten zeigen inzwischen
auf das oeffentliche Schaufenster, die *Download-Liste* blieb aber im privaten
Repo unsichtbar. Ein Release, den niemand ohne Konto sieht, ist aber genau
das, was diese Werkzeuge verhindern sollen.

Derselbe Text und dieselben Bilder, im oeffentlichen Repo:
  * Text aus `releases_texte_ausrichten.release_text` - dieselbe Funktion, die
    auch die privaten Texte schreibt. Kein zweiter Formatter, der auseinander-
    laufen koennte.
  * Assets aus `docs/meilensteine/bilder` - die Bilder, die die Seite im
    Abschnitt zeigt, nichts sonst.

    python Tools/releases_oeffentlich.py                    # Plan
    python Tools/releases_oeffentlich.py --anwenden         # spiegeln
    python Tools/releases_oeffentlich.py --anwenden --ref origin/main

Das private Repo wird von diesem Werkzeug NICHT angefasst. Es schreibt nur
in --ziel, und dort werden ausschliesslich die 14 Meilenstein-Tags
angefasst - das Release `city-content-alkis16` (Kartendaten) bleibt unberuehrt,
denn es gehoert nicht in ein oeffentliches Repo.
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys
import tempfile
import time

REPO = pathlib.Path(__file__).resolve().parents[1]
TOOLS = REPO / "Tools"
sys.path.insert(0, str(TOOLS))

import releases_bilder_ausrichten as rba  # noqa: E402
import releases_texte_ausrichten as rta  # noqa: E402

SEITENPFAD = "docs/meilensteine.md"
ZIEL_VORGABE = "chaotiKKK/wiesbaden-real-meilensteine"


class Fehler(RuntimeError):
    pass


def gh(*args, repo=None, versuche=3):
    """`gh ...` mit Wiederholung bei Netzfehlern.

    GEMESSEN am 27.09.2026: nach acht von vierzehn Releases brach der Lauf an
    `dial tcp ...: connectex: Ein Verbindungsversuch ist fehlgeschlagen` ab -
    mitten in einem Lauf, der 35 Bilder hochlaedt. Ein Abbruch mitten drin
    ist nicht schlimm (der Lauf ist wiederholbar und stueckweise), schlimm
    waere, ihn als "das Repo verweigert die Anlage" zu melden. Deshalb wird
    ein Verbindungsfehler dreimal versucht; ein echter gh-Fehler (Exit != 0
    mit Text in stderr) bleibt ein Fehler.
    """
    befehl = ["gh", *args]
    if repo:
        befehl += ["--repo", repo]
    letzter = (1, "", "")
    for versuch in range(versuche):
        fertig = subprocess.run(befehl, cwd=REPO, capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        code, raus, fehler = fertig.returncode, fertig.stdout, fertig.stderr
        letzter = (code, raus, fehler)
        if code == 0:
            return code, raus, fehler
        if not netzfehler(fehler):
            return code, raus, fehler
        if versuch < versuche - 1:
            print("    Netzfehler, Versuch %d/%d: %s"
                  % (versuch + 2, versuche, (fehler or "").strip()[:90]), flush=True)
            time.sleep(5 * (versuch + 1))
    return letzter


def netzfehler(fehler):
    text = (fehler or "").lower()
    return any(kennzeichen in text for kennzeichen in (
        "connectex", "connection", "timeout", "temporarily", "eof",
        "no such host", "tls", "reset by peer"))


def seite_lesen(ref):
    fertig = subprocess.run(["git", "show", f"{ref}:{SEITENPFAD}"], cwd=REPO,
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
    if fertig.returncode != 0:
        raise Fehler(f"Die Seite {SEITENPFAD} gibt es in {ref} nicht - "
                     f"`git fetch origin`?")
    with tempfile.NamedTemporaryFile("w", suffix=".md", delete=False,
                                     encoding="utf-8", newline="\n") as datei:
        datei.write(fertig.stdout)
        pfad = pathlib.Path(datei.name)
    rta.SEITE = pfad
    return pfad


BILDVERWEIS = re.compile(r"meilensteine/bilder/([^\s)]+)")


def bilder_pro_meilenstein(pfad):
    """Die Bilder je Abschnitt - dieselbe Logik wie im Bild-Ausrichter."""
    out, aktuell = {}, None
    for zeile in pfad.read_text(encoding="utf-8").splitlines():
        kopf = re.match(r"^## (\d+)\.\s", zeile)
        if kopf:
            aktuell = int(kopf.group(1))
            out.setdefault(aktuell, [])
            continue
        if aktuell is None:
            continue
        for name in BILDVERWEIS.findall(zeile):
            if name not in out[aktuell]:
                out[aktuell].append(name)
    return out


def ziel_releases(repo):
    code, raus, fehler = gh("release", "list", "--limit", "100", "--json", "tagName",
                            repo=repo)
    if code != 0:
        raise Fehler(f"`gh release list` im oeffentlichen Repo scheitert: "
                     f"{(fehler or '').strip()[:200]}")
    return {e["tagName"] for e in json.loads(raus or "[]")}


def ziel_release(repo, tag):
    code, raus, _ = gh("release", "view", tag, "--json", "assets,body", repo=repo)
    if code != 0:
        return None
    daten = json.loads(raus or "{}")
    return {"assets": [a.get("name", "") for a in (daten.get("assets") or [])],
            "body": daten.get("body") or ""}


def spiegeln(repo, seiten_pfad, bilder_wurzel, sha, anwenden, meldung):
    bilder = bilder_pro_meilenstein(seiten_pfad)
    vorhanden = ziel_releases(repo)
    geaendert = 0
    for num, tag in sorted(rba.TAGS.items()):
        wollen = bilder.get(num, [])
        soll_text = rta.release_text(num, sha)
        ist = ziel_release(repo, tag)

        if ist is None:
            # Auch im Plan weitermachen: ein fehlendes Release ist der
            # aufwendigste Fall (Text + alle Bilder), und gerade den will man
            # VOR dem Anlegen sehen, nicht danach.
            meldung.append(f"M{num:02d} {tag}: Release fehlt im oeffentlichen Repo")
            ist = {"assets": [], "body": ""}
            if anwenden:
                notiz = seiten_pfad.parent / f"oeffentlich_{tag}.md"
                notiz.write_text(soll_text, encoding="utf-8", newline="\n")
                code, _, fehler = gh("release", "create", tag, "--title",
                                     f"Meilenstein {num}: {rta.SEITEN_TITEL[num]}",
                                     "--notes-file", str(notiz), repo=repo)
                if code != 0:
                    raise Fehler(f"Anlegen von {tag} scheitert: "
                                 f"{(fehler or '').strip()[:200]}")
                meldung[-1] += "  -> angelegt"

        fehlend = [w for w in wollen if w not in ist["assets"]]
        ueberzaehlig = [i for i in ist["assets"] if i not in wollen]
        text_weicht_ab = ist["body"].strip() != soll_text.strip()
        if not (fehlend or ueberzaehlig or text_weicht_ab):
            meldung.append(f"M{num:02d} {tag}: passt ({len(wollen)} Bilder)")
            continue
        if ist["body"]:
            meldung.append(f"M{num:02d} {tag}:")
        else:
            meldung.append(f"   wuerde anlegen mit {len(wollen)} Bildern:")
        for name in fehlend:
            meldung.append(f"   + {name}")
        for name in ueberzaehlig:
            meldung.append(f"   - {name} (nicht auf der Seite)")
        if text_weicht_ab:
            meldung.append("   ~ Release-Text weicht ab")
        if not anwenden:
            continue
        for name in fehlend:
            quelle = bilder_wurzel / name
            if not quelle.is_file():
                raise Fehler(f"{quelle} fehlt lokal - der Plan nennt nur "
                             f"Bilder der Seite, also ist das ein Fehler im Baum")
            code, _, fehler = gh("release", "upload", tag, str(quelle), "--clobber",
                                 repo=repo)
            if code != 0:
                raise Fehler(f"Upload {name} scheitert: {(fehler or '').strip()[:200]}")
        for name in ueberzaehlig:
            code, _, fehler = gh("release", "delete-asset", tag, name, "--yes",
                                 repo=repo)
            if code != 0:
                raise Fehler(f"Loeschen von {name} scheitert: {(fehler or '').strip()[:200]}")
        if text_weicht_ab:
            notiz = seiten_pfad.parent / f"oeffentlich_{tag}.md"
            notiz.write_text(soll_text, encoding="utf-8", newline="\n")
            code, _, fehler = gh("release", "edit", tag, "--notes-file", str(notiz),
                                 repo=repo)
            if code != 0:
                raise Fehler(f"Text von {tag} scheitert: {(fehler or '').strip()[:200]}")
        geaendert += 1
        if len(meldung) > 1:
            meldung[-1] += "  -> angepasst"
    return geaendert


def hauptprogramm(argv=None):
    ap = argparse.ArgumentParser(
        description="Meilenstein-Releases in das oeffentliche Schaufenster-Repo spiegeln.")
    ap.add_argument("--ziel", default=ZIEL_VORGABE, help="oeffentliches Repo")
    ap.add_argument("--ref", default="origin/main", help="Stand der Seite")
    ap.add_argument("--sha", default=None, help="Stand im Code im Fuss (Vorgabe: der Ref)")
    ap.add_argument("--bilder", default=None, help="Bilderverzeichnis")
    ap.add_argument("--anwenden", action="store_true")
    args = ap.parse_args(argv)

    bilder_wurzel = pathlib.Path(args.bilder).resolve() if args.bilder \
        else REPO / "docs" / "meilensteine" / "bilder"
    sha = args.sha
    if not sha:
        # errors="replace": Git-Metadaten dekodier-tolerant lesen (PEP 686).
        fertig = subprocess.run(["git", "rev-parse", "--short", args.ref], cwd=REPO,
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", check=True)
        sha = fertig.stdout.strip()

    print(f"Spiegel nach {args.ziel} (Seite {args.ref} @ {sha})")
    seiten_pfad = seite_lesen(args.ref)
    meldung = []
    try:
        geaendert = spiegeln(args.ziel, seiten_pfad, bilder_wurzel, sha,
                             args.anwenden, meldung)
    finally:
        try:
            seiten_pfad.unlink()
        except OSError:
            pass
    for zeile in meldung:
        print("  " + zeile)
    if not args.anwenden:
        print("\nNur Plan. Zum Ausfuehren: --anwenden")
    else:
        print(f"\n{geaendert} Release(s) angepasst.")
    return 0


if __name__ == "__main__":
    raise SystemExit(hauptprogramm())
