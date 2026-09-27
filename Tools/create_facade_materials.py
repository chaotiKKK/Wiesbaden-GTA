"""Fassaden-Gliederung: Fenster bekommen Rahmen, Bank und Tiefe.

WARUM - gemessen am Vorher-Bild (Saved/Diagnose/luft_spawncoord.png):

Die Fenster waren FLACHE dunkle Rechtecke im Putz - ein aufgemaltes Raster
ohne Rahmen, ohne Bank, ohne Tiefe. Aus der Naehe liest sich die Wand als
"Box mit Fenster-Aufkleber". Genau das nennt der Auftrag als Fehler.

WAS SICH AENDERT (nur im Shader, kein Dreieck, kein Neubau):

Um jede Glasflaeche liegt jetzt ein heller RAHMEN (die Laibung), unter dem
Fenster eine steinerne FENSTERBANK, und am Geschossuebergang ein GESIMS.
Der Rahmen entsteht ohne Subtraktion allein aus der Zeichen-Reihenfolge: erst
fuellt der Rahmen die ganze Fensteroeffnung, dann ueberschreibt das (kleinere)
Glas die Mitte - stehen bleibt der Rahmen als Ring. Das gibt dem Fenster die
Tiefe, die eine flache Scheibe nie hatte.

WELCHE FASSADEN: die vier fenstertragenden Shader-Fassaden, die der
BuildingGenerator nach Baujahr vergibt -
  * Sandstein  (Gruenderzeit 1850-1920, praegt die Innenstadt)
  * Putz       (1920-1960, mit echter Putztextur)
  * Backstein  (Wiesbadener Klinker)
  * Fachwerk
Glas (Vorhangfassade, ganz verglast) und Beton (Fototextur) bleiben
unberuehrt - sie tragen kein aufgemaltes Fensterraster.

DIE TYP-VARIATION bleibt erhalten: jede der vier hat ihre eigene Wandfarbe,
Rauheit, Sockelfarbe und Rausch-Koernung (Klinker grob, Sandstein fein). Das
gemeinsame Fensterwerk klingt darueber wie echte Bauteile, die es in jeder
Bauweise gibt.

TECHNIK: Die Helfer (band/lerp/make_facade/...) kommen aus build_materials.py.
Dessen Modul ruft am Ende ungeschuetzt main() (baut ALLE Stadtmaterialien neu
und wuerde das eben verbesserte Dach ueberschreiben). Darum wird der Quelltext
OHNE die letzte main()-Zeile ausgefuehrt; danach wird nur die Fensterfunktion
ersetzt und werden nur die vier Fassaden neu gebaut.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject
    -ExecCmds="py exec(open('Tools/create_facade_materials.py').read())"
    -unattended -nosplash -nop4 -nullrhi
"""

import io
import unreal

BM = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\build_materials.py"


def log(m):
    unreal.log("###FACADE### %s" % m)


# --- build_materials.py OHNE das abschliessende main() ausfuehren -----------
src = io.open(BM, encoding="utf-8").read()
cut = src.rindex("\nmain()")
src = src[:cut]
ns = {"__name__": "wb_facade_helpers", "__file__": BM}
exec(compile(src, "build_materials.py", "exec"), ns)
log("Helfer aus build_materials geladen (ohne main()).")


# --- Verbesserte Fenster: Rahmen, Bank, Gesims ------------------------------
ENHANCED = r'''
def add_facade_windows(mat, wall_color, wall_out=""):
    """Fenster mit Rahmen (Laibung), Fensterbank und Gesims - statt flacher Glasflaeche.

    UV: U in Metern entlang der Wand, V in Geschossen. Eine Fensterachse je
    2,6 m, ein Fenster je Geschoss.
    """
    uv = expr(mat, unreal.MaterialExpressionTextureCoordinate, -2300, 900)
    u = channel(mat, uv, "r", -2100, 800)
    v = channel(mat, uv, "g", -2100, 1000)

    bay = frac(mat, div(mat, u, 2.6, -1900, 800), -1750, 800)
    floor_pos = frac(mat, v, -1750, 1000)

    # Fensteroeffnung (Laibung, aussen) und Glasflaeche (innen, kleiner).
    opening = mul(mat, band(mat, bay, 0.28, 0.72, -1560, 620),
                       band(mat, floor_pos, 0.30, 0.84, -1560, 720), -1180, 660)
    glass = mul(mat, band(mat, bay, 0.335, 0.665, -1560, 900),
                     band(mat, floor_pos, 0.365, 0.79, -1560, 1000), -1180, 940)

    # Fensterbank: schmales helles Band direkt unter der Oeffnung.
    sill = mul(mat, band(mat, bay, 0.255, 0.745, -1560, 1160),
                    band(mat, floor_pos, 0.245, 0.305, -1560, 1260), -1180, 1200)

    # Gesims: schmaler Schattenstrich am Geschossuebergang - gliedert die
    # Fassade horizontal auch dort, wo keine Fenster sitzen. AUS DER NAEHE
    # nachjustiert: das alte Band (0,895..0,99, fast schwarz) las sich als
    # dicker schwarzer Streifen statt als Gesims. Jetzt schmaler und deutlich
    # heller - ein weicher Schattenstrich, keine Teerfuge.
    cornice = band(mat, floor_pos, 0.915, 0.975, -1560, 1440)

    glass_c = c3(mat, 0.020, 0.028, 0.038, -720, 460)     # dunkles Glas
    frame_c = c3(mat, 0.865, 0.845, 0.795, -720, 640)     # heller Rahmen (gestrichen)
    sill_c = c3(mat, 0.700, 0.680, 0.640, -720, 1160)     # steinerne Bank
    cornice_c = c3(mat, 0.205, 0.195, 0.180, -720, 1440)  # weicher Schattenstrich

    # Zeichen-Reihenfolge: Wand -> Gesims -> Bank -> Rahmen(Oeffnung) -> Glas.
    # Das Glas ueberschreibt die Mitte der Oeffnung, der Rahmen bleibt als Ring.
    base = lerp(mat, wall_color, cornice_c, cornice, -440, 900, a_out=wall_out)
    base = lerp(mat, base, sill_c, sill, -300, 950)
    base = lerp(mat, base, frame_c, opening, -160, 700)
    base = lerp(mat, base, glass_c, glass, -20, 700)

    # Glas glatter und dunkler als die Wand; nur das Glas leicht metallisch.
    rough = lerp(mat, c1(mat, 0.88, -720, 1700), c1(mat, 0.34, -720, 1800),
                 glass, -440, 1750)
    metal = mul(mat, glass, 0.22, -440, 1950)
    return base, rough, metal
'''
exec(compile(ENHANCED, "enhanced_windows", "exec"), ns)
log("Fensterfunktion ersetzt (Rahmen/Bank/Gesims).")


# --- Nur die vier fenstertragenden Fassaden neu bauen -----------------------
# Definitionen 1:1 aus build_materials.make_facade_variants uebernommen, damit
# Farbe/Rauheit/Sockel/Koernung je Typ unveraendert bleiben.
textures = ns["import_facade_textures"]()
make_facade = ns["make_facade"]
make_wall = ns["make_wall"]

make_wall(textures, "M_WbFacade_Putz")
log("Putz neu (Putztextur + neue Fenster).")

make_facade("M_WbFacade_Backstein", (0.205, 0.085, 0.060), (0.330, 0.150, 0.100),
            roughness=0.88, noise_scale=6.0, socket=(0.085, 0.040, 0.028))
log("Backstein neu.")

make_facade("M_WbFacade_Sandstein", (0.435, 0.375, 0.280), (0.615, 0.535, 0.405),
            roughness=0.80, noise_scale=3.0, socket=(0.175, 0.145, 0.105))
log("Sandstein neu.")

make_facade("M_WbFacade_Fachwerk", (0.115, 0.075, 0.050), (0.520, 0.475, 0.390),
            roughness=0.90, noise_scale=12.0, socket=(0.090, 0.068, 0.048))
log("Fachwerk neu.")

log("FERTIG: vier Fassaden mit Rahmen/Bank/Gesims neu gebaut.")

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
