"""Sammelt die PBR-Materialien der Stadt an EINER Stelle.

Zwei Quellen, beide CC0:

* Bereits heruntergeladene Pakete im Downloads-Ordner (ambientCG, Poly Haven)
* Fehlende Materialien ueber die oeffentliche ambientCG-Schnittstelle

Bewusst NICHT uebernommen werden Poliigon-Pakete und
CityStreetAsphaltGenericClean001: In keinem liegt eine Lizenzdatei, und
Poliigons Lizenz ist nicht CC0 - sie schraenkt die Weitergabe ein. Fuer ein
Paket, das ausgeliefert wird, ist das ohne belegte Lizenz ein Risiko.

Die Schnittstelle wird SPARSAM benutzt. Die Betreiberseite schreibt selbst:
"All of this is operated and filled with content by just one person ... the
API is therefore potentially not as reliable or stable as needed for an
enterprise-level application." Deshalb einmalig herunterladen und ablegen,
niemals zur Laufzeit abrufen.

Aufruf:  py Tools/collect_materials.py
"""

import io
import json
import os
import re
import shutil
import sys
import urllib.request
import zipfile

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOWNLOADS = os.path.join(os.path.expanduser("~"), "Downloads")
OUT = os.path.join(PROJECT, "Data", "Raw", "Materials")

# Vorhandene Pakete -> Zielname im Projekt.
#
# Der Zielname sagt, WOFUER das Material da ist, nicht woher es kommt. Ein
# Ordner "Bricks092" beantwortet beim naechsten Mal nicht die Frage, welche
# Fassade damit gebaut wird.
LOCAL = {
    # Echte Gebaeudefassaden MIT Fenstern in der Textur.
    #
    # Die Fenster werden bisher im Shader gerechnet (add_facade_windows:
    # Baender aus frac(U/2,6) und frac(V)). Fotografierte Fassaden ersetzen
    # das durch Wirklichkeit - und drei der vier bringen einen
    # EMISSION-Kanal mit. Damit leuchten Fenster bei Nacht von selbst; das
    # Wettersystem regelt die Tageszeit bereits, und eine Stadt mit dunklen
    # Fenstern am Abend sieht tot aus. Diese Faehigkeit hatte das Projekt
    # bisher gar nicht.
    "Facade006_2K-PNG.zip":  "Facade_Glasturm",
    "Facade009_2K-PNG.zip":  "Facade_Hochhaus_Nacht",
    "Facade018A_2K-PNG.zip": "Facade_Buerohaus",
    "Facade020B_2K-PNG.zip": "Facade_Nachkrieg",

    # Fahrbahn, Gehweg, Gelaende.
    "Road007_2K-PNG.zip":    "Fahrbahn_Strasse",
    "PavingStones150_2K-PNG.zip": "Gehweg_Pflaster",
    "Grass001_2K-PNG.zip":   "Gelaende_Wiese",
    "Grass005_2K-PNG.zip":   "Gelaende_Gras",
    "Rock058_2K-PNG.zip":    "Gelaende_Stein",

    "Bricks092_2K-PNG.zip":  "Facade_Backstein",
    "Bricks085_2K-PNG.zip":  "Facade_Klinker",
    "Bricks097_2K-PNG.zip":  "Facade_Ziegel",
    "Bricks104_2K-PNG.zip":  "Facade_Sandstein",
    "Tiles011_2K-PNG.zip":   "Gehweg_Platten",
    "Tiles143_2K-PNG.zip":   "Platz_Pflaster",
    "RoadLines007_2K-PNG.zip": "Fahrbahn_Markierung",
    "Rocks004_2K-PNG.zip":   "Gelaende_Fels",
    "forest_ground_06_4k.blend.zip": "Gelaende_Waldboden",
    "rocky_terrain_02_4k.blend.zip": "Gelaende_Steinig",
}

# Fehlende Materialien ueber die Schnittstelle. Asphalt fehlt in den
# vorhandenen Paketen vollstaendig - und die Fahrbahn ist der Posten, der in
# jedem einzelnen Bild zu sehen ist.
REMOTE = {
    "Asphalt031": "Fahrbahn_Asphalt",
    "Asphalt033": "Fahrbahn_Asphalt_Alt",
    "Concrete034": "Bordstein_Beton",
}

RESOLUTION = "2K-PNG"

# Welche Kanaele das Projekt braucht. Displacement und AmbientOcclusion
# bleiben liegen: Verschiebung braucht Tessellierung (gibt es hier nicht), und
# Verdeckung rechnet die Engine bereits als Bildschirmeffekt.
WANTED = {
    "Color": "Color", "BaseColor": "Color", "_diff_": "Color", "_COL_": "Color",
    "NormalGL": "Normal", "_nor_gl_": "Normal",
    "Roughness": "Roughness", "_rough_": "Roughness",
    # Nur bei den Fassaden vorhanden - und dort der eigentliche Gewinn:
    # beleuchtete Fenster bei Nacht.
    "Emission": "Emission",
    # Fensterglas ist spiegelnd, Putz nicht. Ohne diese Karte muesste das
    # Material raten, welcher Teil der Fassade was ist.
    "Metalness": "Metallic",
}


def classify(filename):
    """Kanal einer Texturdatei bestimmen; None, wenn nicht gebraucht.

    NormalDX wird ausdruecklich verworfen. Unreal erwartet die
    OpenGL-Konvention; die DirectX-Fassung hat die Gruenachse gespiegelt und
    beleuchtet Vertiefungen als Erhebungen - ein Fehler, den man erst im
    Streiflicht sieht.
    """
    if "NormalDX" in filename:
        return None

    # Verdeckung und Verschiebung ausdruecklich verwerfen, BEVOR die
    # Schluesselwoerter greifen: "AmbientOcclusion" enthaelt kein "Color",
    # aber Namen aendern sich, und ein stiller Fehlgriff hier waere eine
    # Graustufenkarte als Grundfarbe.
    if "AmbientOcclusion" in filename or "Displacement" in filename:
        return None
    for key, channel in WANTED.items():
        if key in filename:
            return channel
    return None


def note_license(folder, source, license_text):
    with io.open(os.path.join(folder, "LIZENZ.txt"), "w", encoding="utf-8") as f:
        f.write("Quelle: %s\nLizenz: %s\n" % (source, license_text))


def take_local(zip_name, target):
    path = os.path.join(DOWNLOADS, zip_name)
    if not os.path.exists(path):
        print("WBMAT uebersprungen (fehlt): %s" % zip_name)
        return False

    folder = os.path.join(OUT, target)
    os.makedirs(folder, exist_ok=True)

    taken = {}
    with zipfile.ZipFile(path) as z:
        for name in z.namelist():
            base = os.path.basename(name)
            if not base.lower().endswith((".png", ".jpg", ".jpeg", ".exr")):
                continue
            channel = classify(base)
            if channel is None or channel in taken:
                continue
            ext = os.path.splitext(base)[1].lower()
            dest = os.path.join(folder, "%s_%s%s" % (target, channel, ext))
            with z.open(name) as src, io.open(dest, "wb") as dst:
                shutil.copyfileobj(src, dst)
            taken[channel] = os.path.basename(dest)

    source = "ambientCG" if "-PNG.zip" in zip_name else "Poly Haven"
    note_license(folder, "%s (%s)" % (source, zip_name), "CC0 1.0 Universal")
    print("WBMAT %-22s <- %-34s %s" % (target, zip_name, ", ".join(sorted(taken))))
    return len(taken) >= 2


def take_remote(asset_id, target):
    folder = os.path.join(OUT, target)
    if os.path.isdir(folder) and os.listdir(folder):
        print("WBMAT %-22s liegt bereits vor" % target)
        return True

    url = ("https://ambientcg.com/api/v2/downloads_csv"
           "?id=%s&type=Material&sort=Popular" % asset_id)
    try:
        request = urllib.request.Request(url, headers={
            "User-Agent": "WiesbadenReal/1.0 (Stadtprojekt, einmaliger Materialabruf)"})
        with urllib.request.urlopen(request, timeout=60) as r:
            csv = r.read().decode("utf-8", "replace")
    except Exception as e:
        print("WBMAT %s: Abfrage fehlgeschlagen (%s)" % (asset_id, e))
        return False

    link = None
    for line in csv.splitlines():
        if RESOLUTION in line and "http" in line:
            match = re.search(r"(https?://\S+?\.zip)", line)
            if match:
                link = match.group(1)
                break

    if not link:
        print("WBMAT %s: kein %s-Paket gefunden" % (asset_id, RESOLUTION))
        return False

    os.makedirs(folder, exist_ok=True)
    tmp = os.path.join(folder, "_download.zip")
    try:
        # MIT Kennung anfragen.
        #
        # Ohne User-Agent antwortet ambientCG mit HTTP 403. Das ist kein
        # Fehler des Servers, sondern eine Abwehr gegen Massenabrufe - und die
        # Seite bittet ausdruecklich darum, sparsam zu sein. Die Kennung sagt,
        # wer fragt.
        request = urllib.request.Request(link, headers={
            "User-Agent": "WiesbadenReal/1.0 (Stadtprojekt, einmaliger Materialabruf)"})
        with urllib.request.urlopen(request, timeout=180) as r, io.open(tmp, "wb") as f:
            shutil.copyfileobj(r, f)
    except Exception as e:
        print("WBMAT %s: Download fehlgeschlagen (%s)" % (asset_id, e))
        return False

    taken = {}
    with zipfile.ZipFile(tmp) as z:
        for name in z.namelist():
            base = os.path.basename(name)
            if not base.lower().endswith(".png"):
                continue
            channel = classify(base)
            if channel is None or channel in taken:
                continue
            dest = os.path.join(folder, "%s_%s.png" % (target, channel))
            with z.open(name) as src, io.open(dest, "wb") as dst:
                shutil.copyfileobj(src, dst)
            taken[channel] = True
    os.remove(tmp)

    note_license(folder, "ambientCG %s" % asset_id, "CC0 1.0 Universal")
    print("WBMAT %-22s <- ambientCG %-12s %s" % (target, asset_id, ", ".join(sorted(taken))))
    return len(taken) >= 2


os.makedirs(OUT, exist_ok=True)

ok = 0
for zip_name, target in LOCAL.items():
    if take_local(zip_name, target):
        ok += 1

for asset_id, target in REMOTE.items():
    if take_remote(asset_id, target):
        ok += 1

print("WBMAT FERTIG: %d von %d Materialien unter %s"
      % (ok, len(LOCAL) + len(REMOTE), OUT))
