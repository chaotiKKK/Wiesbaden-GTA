"""Holt fehlende Verkehrszeichen-Grafiken der amtlichen VzKat-Serie von Wikimedia Commons.

Warum
-----
Die Schild-Texturen unter `Content/Textures/TrafficSigns/Sign_<VzKat>.png` sind
PNG-Renderings der amtlichen Zeichen-SVGs (gemeinfrei nach § 5 UrhG). Der
Projekt-Katalog kannte 41 Zeichen; die OSM-Daten der Stadt nennen ~70 weitere
(z. B. 239 Sonderweg Fussgaenger, 242 Fussgaengerbereich, 1022-10 Radfahrer
frei). Ohne Grafik spawnt der Ausstattungs-Spawner eine leere Tafel und die
Engine loggt "Schild-Textur nicht gefunden".

Was das Skript tut
------------------
1. Bestimmt die Zeichen-Ids, die in `Data/Raw/OSM/wiesbaden.osm.json` stehen
   (gleiche Normalisierung wie der Parser: DE-Prefix, Bedingungen in eckigen
   Klammern, Platzhalter wie "none" verwerfen).
2. Sucht je Id die amtliche Datei auf Commons ("Zeichen <id>" bzw.
   "Zusatzzeichen <id>"), bevorzugt die neueste StVO-Fassung als SVG.
3. Laedt das 960-px-PNG-Rendering, legt es quadratisch mit transparentem Rand
   ab und speichert `Sign_<id>.png` neben die vorhandenen Bilder.
4. Schreibt die Quelle je Bild nach `Saved/Diagnose/schildquellen.json` (fuer
   ATTRIBUTION.md).

Aufruf:
    python Tools/fetch_sign_textures.py            # nur Fehlende holen
    python Tools/fetch_sign_textures.py --alle     # auch Vorhandene neu holen
    python Tools/fetch_sign_textures.py --nur 239,242
"""

import argparse
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

PROJEKT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZIEL = os.path.join(PROJEKT, "Content", "Textures", "TrafficSigns")
OSM = os.path.join(PROJEKT, "Data", "Raw", "OSM", "wiesbaden.osm.json")
KATALOG = os.path.join(PROJEKT, "Content", "Config", "TrafficSignCatalog.json")
QUELLEN = os.path.join(PROJEKT, "Saved", "Diagnose", "schildquellen.json")
USER_AGENT = "WiesbadenReal/1.0 (lokales Unreal-Projekt; Zeichen-Grafiken aus Wikimedia Commons)"
BREITE = 960
PLATZHALTER = {"none", "no", "false", "kein", "nothing", "-", "\\", ""}
JAHRE = [1992, 2017, 2013, 2009, 2006, 2010, 1971, 1970, 1988, 1985]
LEGACY_ID_ALIASES = {
    # In the Wiesbaden extract these are parameterized/old spellings of
    # canonical assets, not three additional graphics.
    "1036-37": "1026-37",
    "260-30": "260",
}


def api(params, versuche=4):
    """API-Aufruf mit Geduld: Wikimedia drosselt haeufige Anfragen (429)."""
    url = "https://commons.wikimedia.org/w/api.php?" + urllib.parse.urlencode(params)
    for n in range(versuche):
        try:
            anfrage = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(anfrage, timeout=40) as a:
                return json.load(a)
        except Exception as e:
            if n == versuche - 1:
                raise
            wartezeit = 2.0 * (n + 1)
            print("    (%s - warte %.0f s)" % (e, wartezeit), flush=True)
            time.sleep(wartezeit)
    return {}


def namensform(sign_id):
    return sign_id.replace(".", "-")


def token_normalisieren(token):
    """Wie der Parser: DE-Prefix weg, Bedingung/Wert in [] weg, Platzhalter weg."""
    t = token.strip()
    if not t:
        return None
    if t.lower().startswith("de:"):
        t = t[3:].strip()
    elif t.upper().startswith("DE") and len(t) > 2 and t[2].isdigit():
        t = t[2:].strip()
    if t.lower() in PLATZHALTER:
        return None
    basis = t.split("[")[0].strip()
    if not basis or basis.lower() in PLATZHALTER:
        return None
    # Tempolimit mit OSM-Werttrenner: 274.1:30 / 274:30 -> 274-30.
    m = re.fullmatch(r"^(27[48])(?:\.1)?:(\d+)$", basis)
    if m:
        return "%s-%s" % m.groups()

    # Tempolimit 274[30] bzw. 274.1[30] -> 274-30. Ohne Wert bleibt 274
    # eine reine Katalog-Basis ohne eigene Grafik; 274.1 dagegen hat eine
    # eigene Beginn-einer-Zone-Grafik.
    m = re.fullmatch(r"^(27[48])(?:\.1)?$", basis)
    if m:
        wert = re.search(r"\[([0-9]+)\]", t)
        if wert:
            return "%s-%s" % (m.group(1), wert.group(1))
        if basis in ("274", "278"):
            return None

    # 1001-30[200] und die im Rohbestand vorkommende Kurzform 1001-30-200
    # zeigen dieselbe Tafel; die Zahl ist der variable Aufdruck.
    if re.fullmatch(r"1001-30-\d+", basis):
        return "1001-30"
    return LEGACY_ID_ALIASES.get(basis, basis)


def osm_tokens(wert):
    """Trennt Zeichen nur ausserhalb von Bedingungen in eckigen Klammern."""
    tokens = []
    start = 0
    tiefe = 0
    for index, zeichen in enumerate(wert):
        if zeichen == "[":
            tiefe += 1
        elif zeichen == "]":
            tiefe = max(0, tiefe - 1)
        elif tiefe == 0 and zeichen in ",;":
            tokens.append(wert[start:index])
            start = index + 1
    tokens.append(wert[start:])
    return tokens


def katalog_ids():
    """Ids und Aliase des Projekt-Katalogs in Asset-Namensform."""
    if not os.path.exists(KATALOG):
        return set(), {}
    with open(KATALOG, encoding="utf-8") as fh:
        daten = json.load(fh)
    eintraege = daten["signs"] if isinstance(daten, dict) else daten
    ids, alias = set(), {}
    for e in eintraege:
        ids.add(namensform(e["id"]))
        for a in e.get("aliases", []):
            alias[namensform(a)] = namensform(e["id"])
    return ids, alias


def fehlende_ids(nur=None):
    """Alle Zeichen-Ids der Stadt, die keine PNG-Grafik haben."""
    if nur:
        return [s.strip() for s in nur.split(",") if s.strip()]
    vorhanden = {f[5:-4] for f in os.listdir(ZIEL) if f.endswith(".png")}
    ids, alias = katalog_ids()
    with open(OSM, encoding="utf-8", errors="replace") as fh:
        raw = fh.read()
    tokens = []
    for wert in re.findall(r'"traffic_sign"\s*:\s*"([^"]*)"', raw):
        tokens += osm_tokens(wert)
    brauchen = {}
    for t in tokens:
        i = token_normalisieren(t)
        if not i:
            continue
        i = namensform(i)
        i = alias.get(i, i)
        if i not in vorhanden:
            brauchen[i] = brauchen.get(i, 0) + 1
    return [i for i, _ in sorted(brauchen.items(), key=lambda kv: (-kv[1], kv[0]))]


def id_formen(sign_id):
    """Schreibweisen der Nummer: VzKat nutzt den Punkt (244.1), OSM den Bindestrich (244-1)."""
    formen = [sign_id]
    m = re.match(r"^(\d{3})-(\d)$", sign_id)
    if m:
        formen.append("%s.%s" % m.groups())
    m = re.match(r"^(\d{3})\.(\d)$", sign_id)
    if m:
        formen.append("%s-%s" % m.groups())
    return formen


def suche(sign_id):
    """Sucht die amtliche Datei auf Commons und liefert (Titel, Bild-URL, Lizenz).

    Ein Aufruf je Zeichen: generator=search liefert Suche und Bild-URL zusammen -
    das haelt die Anfragen (und damit die Drosselung) klein.
    """
    praefix = "Zusatzzeichen" if sign_id.startswith("10") else "Zeichen"
    for form in id_formen(sign_id):
        # Nur echte Zeichen-Dateien, in denen die Nummer als eigenes Token steht:
        # auf "242" darf also kein ".2" (Zeichen 242.2) folgen.
        muster = re.compile(r"^File:%s %s(?=[ ,]|$)" % (praefix, re.escape(form)))
        for frage in ["%s %s StVO" % (praefix, form), "%s %s" % (praefix, form)]:
            try:
                d = api({"action": "query", "generator": "search", "gsrsearch": frage,
                         "gsrnamespace": "6", "gsrlimit": "20", "prop": "imageinfo",
                         "iiprop": "url|mime|extmetadata", "iiurlwidth": str(BREITE),
                         "format": "json"})
            except Exception as e:
                print("    (Suche '%s' fehlgeschlagen: %s)" % (frage, e), flush=True)
                continue
            seiten = (d.get("query") or {}).get("pages", {}) or {}
            kandidaten = []
            for p in seiten.values():
                titel = p.get("title", "")
                info = (p.get("imageinfo") or [{}])[0]
                if not muster.match(titel) or not titel.lower().endswith(".svg"):
                    continue
                if not info.get("thumburl"):
                    continue
                jahr = max([j for j in JAHRE if str(j) in titel] or [0])
                lizenz = (info.get("extmetadata", {}).get("LicenseShortName", {}) or {}).get("value", "") or ""
                kandidaten.append((-jahr, len(titel), titel, info["thumburl"], lizenz))
            if kandidaten:
                kandidaten.sort()
                _, _, titel, url, lizenz = kandidaten[0]
                return titel, url, lizenz
            time.sleep(1.0)
    return None, None, None


def quadratisch(pfad):
    """Setzt die Grafik mittig auf eine quadratische, transparente Flaeche."""
    from PIL import Image
    im = Image.open(pfad).convert("RGBA")
    seite = max(im.width, im.height)
    neu = Image.new("RGBA", (seite, seite), (0, 0, 0, 0))
    neu.paste(im, ((seite - im.width) // 2, (seite - im.height) // 2))
    neu.save(pfad)
    return neu.size


def hole(ids, alle=False):
    from PIL import Image  # noqa: F401  (nur zur fruehen Fehlermeldung)
    vorhanden = {f[5:-4] for f in os.listdir(ZIEL) if f.endswith(".png")}
    quellen = {}
    if os.path.exists(QUELLEN):
        quellen = json.load(open(QUELLEN, encoding="utf-8"))
    geholt = fehler = 0
    for i, sign_id in enumerate(ids, 1):
        if sign_id in vorhanden and not alle:
            continue
        titel, url, lizenz = suche(sign_id)
        if not url:
            print("[%2d/%d] %-12s KEINE DATEI GEFUNDEN" % (i, len(ids), sign_id), flush=True)
            fehler += 1
            continue
        ziel = os.path.join(ZIEL, "Sign_%s.png" % namensform(sign_id))
        try:
            anfrage = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(anfrage, timeout=60) as a, open(ziel, "wb") as fh:
                fh.write(a.read())
            groesse = quadratisch(ziel)
            quellen[namensform(sign_id)] = {"titel": titel, "url": url, "lizenz": lizenz}
            print("[%2d/%d] %-12s %-58s %s" % (i, len(ids), sign_id, titel[5:][:58], groesse), flush=True)
            geholt += 1
        except Exception as e:
            print("[%2d/%d] %-12s FEHLER %s" % (i, len(ids), sign_id, e), flush=True)
            fehler += 1
        time.sleep(1.0)          # Wikimedia hoeflich behandeln
    os.makedirs(os.path.dirname(QUELLEN), exist_ok=True)
    with open(QUELLEN, "w", encoding="utf-8") as fh:
        json.dump(quellen, fh, indent=1, ensure_ascii=False)
    print("\nGeholt: %d, ohne Treffer/Fehler: %d, Quellenliste: %s" % (geholt, fehler, QUELLEN))
    return 0 if fehler == 0 else 1


def kategorie(sign_id):
    """VzKat-Gruppe aus der Nummer: 1xx Gefahr, 2xx Vorschrift, 3xx Richt, 10xx Zusatz, 6xx Einrichtung."""
    ziffern = sign_id.split("-")[0].split(".")[0]
    if ziffern.startswith("10"):
        return "Zusatzzeichen"
    if ziffern.startswith("1"):
        return "Gefahrzeichen"
    if ziffern.startswith("2"):
        return "Vorschriftzeichen"
    if ziffern.startswith("3"):
        return "Richtzeichen"
    if ziffern.startswith("6"):
        return "Verkehrseinrichtung"
    return "Unbekannt"


def name_aus_titel(titel, sign_id):
    """Amtlicher Name aus dem Dateinamen: 'Zeichen 239 - Sonderweg Fussgaenger, StVO 1992.svg'."""
    t = titel[5:] if titel.startswith("File:") else titel
    t = re.sub(r"\.(svg|png)$", "", t, flags=re.I)
    t = re.sub(r"^(Zeichen|Zusatzzeichen)\s+[0-9][\w\.\-]*\s*[-–]?\s*", "", t)
    t = re.sub(r"[;, ]+\s*(?:StVO|StVZO|StVO-Novelle)\s*\d{0,4}.*$", "", t)
    t = re.sub(r"[;,]\s*$", "", t)
    t = re.sub(r"\s*\((?:\d+\s*x\s*\d+|\d+)\)", "", t)
    t = t.strip(" ,-")
    # Projektstil: Umlaute wie in den vorhandenen Katalogeintraegen umschreiben.
    for a, b in (("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("Ä", "Ae"), ("Ö", "Oe"), ("Ü", "Ue"), ("ß", "ss")):
        t = t.replace(a, b)
    return t or sign_id


def katalog_ergaenzen():
    """Traegt alle geholten Zeichen mit amtlichem Namen in den Projekt-Katalog ein."""
    quellen = json.load(open(QUELLEN, encoding="utf-8")) if os.path.exists(QUELLEN) else {}
    daten = json.load(open(KATALOG, encoding="utf-8"))
    eintraege = daten["signs"]
    bekannt = {namensform(e["id"]) for e in eintraege}
    bekannt |= {namensform(a) for e in eintraege for a in e.get("aliases", [])}
    neu = 0
    for form in sorted(quellen):
        if form in bekannt:
            continue
        titel = quellen[form]["titel"]
        # Katalog-Id in VzKat-Schreibweise (Punkt statt Bindestrich bei Unterzeichen).
        kat_id = re.sub(r"^(\d{3})-(\d)$", r"\1.\2", form)
        eintrag = {"id": kat_id, "name": name_aus_titel(titel, form), "category": kategorie(form)}
        if kat_id != form:
            eintrag["aliases"] = [form]
        eintraege.append(eintrag)
        bekannt.add(form)
        neu += 1
    with open(KATALOG, "w", encoding="utf-8") as fh:
        json.dump(daten, fh, indent="\t", ensure_ascii=False)
        fh.write("\n")
    print("Katalog: %d Eintraege ergaenzt (jetzt %d)." % (neu, len(eintraege)))
    return 0


def main():
    ap = argparse.ArgumentParser(description="Fehlende amtliche Zeichen-Grafiken holen")
    ap.add_argument("--alle", action="store_true", help="auch vorhandene Bilder neu holen")
    ap.add_argument("--nur", help="Komma-Liste von Ids statt der automatischen Auswahl")
    ap.add_argument("--liste", action="store_true", help="nur zeigen, was fehlt")
    ap.add_argument("--katalog", action="store_true",
                    help="geholte Zeichen mit amtlichem Namen in Content/Config/TrafficSignCatalog.json eintragen")
    args = ap.parse_args()
    if not os.path.exists(OSM):
        print("OSM-Quelle fehlt: %s" % OSM)
        return 2
    if args.katalog:
        return katalog_ergaenzen()
    ids = fehlende_ids(args.nur)
    if args.liste:
        print("%d Zeichen ohne Grafik: %s" % (len(ids), ", ".join(ids)))
        return 0
    print("%d Zeichen ohne Grafik - hole sie von Wikimedia Commons." % len(ids))
    return hole(ids, args.alle)


if __name__ == "__main__":
    sys.exit(main())
