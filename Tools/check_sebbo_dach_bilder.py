"""Prueft die aufgemalten Bilder der Dachaufbauten an ihren Kontrollbildern.

Wortmarke, AG-Logo und Magazin-Cover sind AUFGEMALTE Grafik auf einer Flaeche
1 cm vor der Platte (Slot `SbLogo`, `SbLogoAG`, `MgCover`). Ihre UVs tragen
dieselbe feste Reihenfolge: unten links, unten rechts, oben rechts, unten
links. Vertauscht man oben und unten, steht die Schrift auf dem Kopf; kehrt
man die Laufrichtung um, steht sie gespiegelt - beides sieht man im Spiel
erst nach dem Import.

GEMESSEN wird deshalb nicht, ob das Bild "hübsch" aussieht, sondern WO die
Farbflecken liegen - und zwar gegen die QUELLTEXTUR als Wahrheit, nicht gegen
von mir geschaetzte Schwellen:

  * Fuer jede Farbe (Gelb, Rosa, Weiss, Gruen, Hellblau) wird der Schwerpunkt
    in der Textur und im Kontrollbild bestimmt, beide auf die jeweilige
    Flaeche normiert. Stimmen die beiden Schwerpunkte innerhalb von 5 % der
    Flaechenbreite bzw. -hoehe ueberein, steht das Bild richtig herum. Waere
    die UV-Reihenfolge vertauscht, laege der Abstand beim Spiegeln bei 20 bis
    40 % - der Test haette keine Grauzone.
  * Zusaetzlich feste Aussagen, die ein "gar nichts gerendert" auffallen
    lassen: das AG-Logo muss links, die Wortmarke rechts, das Cover muss den
    gruenen Grund tonen.

Die Bildpunkte werden nach KANALVERHAELTNIS erkannt, nicht nach festen
RGB-Werten: der Kontrollrender ist eine Beleuchtung, keine Texturprobe. Das
Gelb der Flamme (255,208,92) kommt als (239,203,105) zurueck, das Rosa des
Herzens als (238,74,214). Die Testfunktionen vergleichen Kanaele miteinander
und fragen nach der ORDNUNG, nicht nach der Staerke - genau die braucht die
Frage "steht das Bild gespiegelt?".

Ohne Blender lauffahig (nur Pillow):
  python Tools/check_sebbo_dach_bilder.py
Meldet am Ende "Kontrollbilder: N Fehler"; 0 = die Grafik steht richtig herum.
"""

import os
import sys

from PIL import Image

PROJEKT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VORSCHAU = os.path.join(PROJEKT, "Data", "Raw", "SebboTower")
TEXTUREN = os.path.join(PROJEKT, "Content", "SebboTower", "Textures",
                        "Source")

FEHLER = []

# Toleranz zwischen Quelltextur und Kontrollbild, als Anteil der Flaeche.
TOLERANZ = 0.05

# Mindestanteil der Flaeche, den ein Merkmal in der QUELLE einnehmen muss,
# damit es ueberhaupt als Lagentauglichkeit gilt. 0,1 %: darunter ist das
# Merkmal Rauschen (im Cover waren es 244 Bildpunkte eines hellen Blaus -
# das "saess" zu 0,010, aber zu 0,063 daneben und damit ein Fehlalarm).
MINDEST_ANTEIL = 0.001


def log(msg):
    print("###WBSDIMG### %s" % msg)


def pruefe(bedingung, text):
    log("%s %s" % ("ok  " if bedingung else "FEHL", text))
    if not bedingung:
        FEHLER.append(text)


# ---------------------------------------------------------------------------
# Farberkennung nach KANALVERHAELTNIS (siehe Kopf).
# ---------------------------------------------------------------------------
def _ist_gelb(r, g, b):
    """Gelb und Orange: r groesster, b kleinster Kanal, deutlicher Abstand."""
    return r > 120 and r > g and g > b and (r - b) > 60


def _ist_rosa(r, g, b):
    """Rosa/Magenta: g ist der kleinste Kanal, r und b weit ueber g."""
    return r > 90 and r > g * 1.8 and b > g * 1.8


def _ist_hellgrau(r, g, b):
    """Weiss und helles Grau: Kanaele dicht beieinander und deutlich heller
    als Platte (0,09) und Himmel."""
    return max(r, g, b) - min(r, g, b) < 30 and min(r, g, b) > 118


def _ist_gruen(r, g, b):
    """Gruen: g groesster Kanal.

    Die Schranke ist absichtlich knapp (+8 je Kanal): das dunkle Blattgruen
    der Quelle (20,60,40) hat 20 Abstand, das vom Licht gewaschene im
    Render (55,86,70) nur noch 8-16. Mit +18 fand das Render im unteren
    Drittel der Tafel fast kein Gruen mehr und der Schwerpunkt wanderte um
    10 % nach oben - eine Beleuchtungswirkung, die als Fehler gewertet
    wurde. Blau (45,73,119) und Himmel (52,60,72) bleiben trotzdem draussen,
    weil dort g NICHT groesster Kanal ist.
    """
    return 40 < g < 215 and g > r + 8 and g > b + 8


def _ist_hellblau(r, g, b):
    """Der Vogel des AG-Logos: b groesster, r kleinster, alles hell."""
    return b > 110 and b > r + 35 and g > r + 15 and min(r, g, b) > 60


MERKMALE = (("Gelb", _ist_gelb), ("Rosa", _ist_rosa),
            ("Weiss", _ist_hellgrau), ("Gruen", _ist_gruen),
            ("Hellblau", _ist_hellblau))


def _ist_himmel(r, g, b):
    """Der Welthintergrund ist ein heller Blaugrauverlauf."""
    return 88 <= r <= 205 and b > r and b >= g - 6


def _schwerpunkte(px, box):
    """Je Merkmal (x, y, n), x/y auf die Box normiert (0..1, y nach unten).

    Box = (x0, y0, x1, y1) in Bildpunkten. Merkmale unter der Mindestzahl
    kommen als None zurueck - ein Merkmal mit drei Bildpunkten sagt nichts
    ueber die Lage eines Formteils aus.
    """
    x0, y0, x1, y1 = box
    bx = float(x1 - x0)
    by = float(y1 - y0)
    flaeche = bx * by
    min_n = max(60.0, MINDEST_ANTEIL * flaeche)
    sammel = {name: [0.0, 0.0, 0] for name, _ in MERKMALE}
    for y in range(int(y0), int(y1)):
        for x in range(int(x0), int(x1)):
            p = px[x, y]
            for name, test in MERKMALE:
                if test(*p):
                    sammel[name][0] += (x - x0) / bx
                    sammel[name][1] += (y - y0) / by
                    sammel[name][2] += 1
    out = {}
    for name, _ in MERKMALE:
        sx, sy, n = sammel[name]
        out[name] = (sx / n, sy / n, n) if n >= min_n else None
    return out


def _tafel_kasten(px, w, h, name):
    """Umschliessendes Rechteck des Bildteils im Frontbild (ohne Himmel)."""
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if _ist_himmel(r, g, b) or (r > 232 and g > 232 and b > 232):
                continue
            x0, y0 = min(x0, x), min(y0, y)
            x1, y1 = max(x1, x), max(y1, y)
    if x1 < 0:
        pruefe(False, "%s: Bildteil im Bild gefunden" % name)
        return None
    bx, by = float(x1 - x0), float(y1 - y0)
    log("     %-10s %4d,%4d..%4d,%4d  (%.0f x %.0f px, Seitenverhaeltnis %.3f)"
        % (name, x0, y0, x1, y1, bx, by, bx / by))
    return (x0, y0, x1, y1)


def vergleiche(textur, bild, feld, seitenverhaeltnis, bezeichnung):
    """Schwerpunkte einer Textur mit denen ihres Ausschnitts im Render.

    `feld` = (x0, y0, x1, y1) als Anteile der gefundenen Tafel. Die
    Normalisierung der Quelle auf 0..1 macht beide Bilder direkt
    vergleichbar: das Kamerabild zeigt die Textur mit u nach rechts und v nach
    oben, das PNG mit y nach unten - beides hebt sich auf, wenn man die
    Schwerpunkte in derselben Richtung normiert.
    """
    quelle = Image.open(textur).convert("RGB")
    qpx = quelle.load()
    qw, qh = quelle.size
    ist = qw / float(qh)
    pruefe(abs(ist - seitenverhaeltnis) < 0.005,
           "%s: Textur %.4f passt zur Flaeche %.4f"
           % (bezeichnung, ist, seitenverhaeltnis))

    bpx = bild.load()
    bw, bh = bild.size
    kasten = _tafel_kasten(bpx, bw, bh, bezeichnung)
    if kasten is None:
        return None
    x0, y0, x1, y1 = kasten
    bx, by = float(x1 - x0), float(y1 - y0)
    aus = (x0 + feld[0] * bx, y0 + feld[1] * by,
           x0 + feld[2] * bx, y0 + feld[3] * by)

    s_quelle = _schwerpunkte(qpx, (0, 0, qw, qh))
    s_bild = _schwerpunkte(bpx, aus)
    for name, _ in MERKMALE:
        a, b = s_quelle[name], s_bild[name]
        if a is None:
            # In der Quelle zu klein fuer einen Lagentest - ueber die Lage
            # sagt das nichts, also kein Fehler. Umgekehrt ist es einer: ein
            # Merkmal, das die Quelle deutlich zeigt und das Bild nicht, ist
            # ein fehlendes Bauteil.
            log("     %-10s %-9s in der Quelle zu klein (%s Punkte) - "
                "kein Lagentest" % (bezeichnung, name,
                                    b[2] if b else 0))
            continue
        if b is None:
            pruefe(False,
                   "%s/%s: Merkmal aus der Textur (%d Punkte) fehlt im Bild"
                   % (bezeichnung, name, a[2]))
            continue
        dx = abs(a[0] - b[0])
        dy = abs(a[1] - b[1])
        log("     %-10s %-9s Textur (%5.3f, %5.3f)  Bild (%5.3f, %5.3f)  "
            "Abweichung dx %5.3f dy %5.3f  (%d / %d Punkte)"
            % (bezeichnung, name, a[0], a[1], b[0], b[1], dx, dy,
               a[2], b[2]))
        pruefe(dx <= TOLERANZ and dy <= TOLERANZ,
               "%s/%s: Merkmal steht an derselben Stelle wie in der Textur "
               "(dx %.3f, dy %.3f, Grenze %.2f)"
               % (bezeichnung, name, dx, dy, TOLERANZ))
    return s_bild


def pruefe_texturmasse():
    """Alpha und Seitenverhaeltnis der drei Texturen.

    Die beiden Schildfelder sind AUFGEMALTE Grafik auf freier Platte - ihr
    Grund muss transparent sein, sonst ueberklebt die Textur das Anthrazit
    bzw. das Marineblau. Das Magazin-Cover ist das Gegenteil: eine Farb-
    flaeche, die die ganze Tafel fuellt und deshalb vollstaendig deckend sein
    MUSS.
    """
    faelle = (("T_WbSeboLogoAG.png", 1.49 / 1.35, False),
              ("T_WbSeboLogo.png", 3.28 / 1.35, False),
              ("T_WbSeboMagazin.png", 1.30 / 1.60, True))
    for name, soll, deckend_ganz in faelle:
        pfad = os.path.join(TEXTUREN, name)
        if not os.path.exists(pfad):
            pruefe(False, "Textur %s fehlt" % name)
            continue
        im = Image.open(pfad)
        w, h = im.size
        if im.mode != "RGBA":
            pruefe(False, "Textur %s hat kein Alpha-Kanal" % name)
            continue
        deckend = im.getchannel("A").histogram()[255]
        anteil = deckend / float(w * h)
        if deckend_ganz:
            pruefe(deckend == w * h,
                   "Textur %s ist vollstaendig deckend - das Cover ist eine "
                   "Farbflaeche und muss die ganze Tafel fuellen (%d von %d)"
                   % (name, deckend, w * h))
        else:
            pruefe(0.05 < anteil < 0.80,
                   "Textur %s: der Grund ist transparent (%.0f %% deckend, "
                   "die Grafik sitzt auf freier Platte)"
                   % (name, 100 * anteil))
        log("     %-22s %4dx%-4d  Verhaeltnis %.4f" % (name, w, h,
                                                       w / float(h)))


def pruefe_schild():
    """Schild: AG-Logo links, Wortmarke rechts, alles aufrecht."""
    bild = Image.open(os.path.join(
        VORSCHAU, "vorschau_sebo_dach_logo_front.png")).convert("RGB")
    # Das Schild ist 4,80 m breit; das marineblaue AG-Feld nimmt die ersten
    # 1,49 m, das Wortmarkenfeld die restlichen 3,28 m. In der Reihenfolge
    # der Felder von der linken Plattenkante her (Bildkasten weiter unten).
    anteil_links = 1.49 / 4.80
    s_ag = vergleiche(os.path.join(TEXTUREN, "T_WbSeboLogoAG.png"),
                      bild, (0.0, 0.0, anteil_links, 1.0),
                      1.49 / 1.35, "AG-Feld")
    s_wort = vergleiche(os.path.join(TEXTUREN, "T_WbSeboLogo.png"),
                        bild, (anteil_links + 0.06 / 4.80, 0.0, 1.0, 1.0),
                        3.28 / 1.35, "Wortmarke")
    if not s_ag or not s_wort:
        return

    # Feste Aussagen als Ergänzung zum Lagentest: die Markenfarben des
    # AG-Logos muessen im linken Feld deutlich vorhanden sein. Ohne das
    # fällt ein leeres oder unbeschriebenes Feld nicht auf.
    for name, merkmal in (("die Flamme", "Gelb"), ("das Herz", "Rosa"),
                          ("der Vogel", "Hellblau")):
        p = s_ag[merkmal]
        pruefe(p is not None and p[2] > 2000,
               "AG-Feld: %s ist deutlich sichtbar (%d Punkte)"
               % (name, p[2] if p else 0))
    pruefe(s_wort["Weiss"] is not None and s_wort["Weiss"][2] > 5000,
           "Wortmarke: die Versalien sind deutlich sichtbar (%d Punkte)"
           % (s_wort["Weiss"][2] if s_wort["Weiss"] else 0))


def pruefe_magazin():
    """Magazintafel: das Cover steht ungespiegelt und aufrecht."""
    bild = Image.open(os.path.join(
        VORSCHAU, "vorschau_sebo_dach_magazin_front.png")).convert("RGB")
    s = vergleiche(os.path.join(TEXTUREN, "T_WbSeboMagazin.png"),
                   bild, (0.0, 0.0, 1.0, 1.0), 1.30 / 1.60, "Cover")
    if not s:
        return
    gruen = s["Gruen"]
    pruefe(gruen is not None and gruen[2] > 20000,
           "Cover: der gruene Grund fuellt die Tafel (%d Punkte)"
           % (gruen[2] if gruen else 0))


def pruefe_pflanze():
    """Nur ein Rauchtest: der Kontrollrender muss Blattgruen zeigen.

    Die Pflanze hat keine Bildtextur, ihr Reif und die Blattfarben kommen aus
    dem Manifest. Geprueft wird deshalb nur, dass das Bild ueberhaupt Blatt-
    gruen zeigt und dass es ueber dem Kuebel sitzt - genug, um zu erkennen,
    dass der Export nicht leer war.
    """
    pfad = os.path.join(VORSCHAU, "vorschau_sebo_dach_pflanze_drei_viertel.png")
    if not os.path.exists(pfad):
        pruefe(False, "Pflanzen-Kontrollbild fehlt")
        return
    im = Image.open(pfad).convert("RGB")
    px = im.load()
    w, h = im.size
    s = _schwerpunkte(px, (0, 0, w, h))
    gruen, grau = s["Gruen"], s["Weiss"]
    pruefe(gruen is not None and gruen[2] > 4000,
           "Pflanze: sichtbares Blattgruen im Kontrollbild (%d Punkte)"
           % (gruen[2] if gruen else 0))
    if gruen and grau:
        log("     Blattgruen bei y %.3f, helles Grau (Kuebel) bei y %.3f"
            % (gruen[1], grau[1]))
        pruefe(gruen[1] < grau[1],
               "Pflanze: das Blattwerk sitzt UEBER dem Kuebel (y %.3f < %.3f)"
               % (gruen[1], grau[1]))


def main():
    pruefe_texturmasse()
    pruefe_schild()
    pruefe_magazin()
    pruefe_pflanze()
    log("Kontrollbilder: %d Fehler" % len(FEHLER))
    for f in FEHLER:
        log("  - %s" % f)
    return 1 if FEHLER else 0


sys.exit(main())
