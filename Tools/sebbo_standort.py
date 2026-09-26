r"""Die Weltkoordinate des SebboTower - abgeleitet, nicht eingetragen.

    python Tools/sebbo_standort.py          ->  -113514,-125729
    python Tools/sebbo_standort.py --json   ->  {"x_cm": ..., "y_cm": ...}

WOFUER: run_ankunft_probe.cmd trug die Koordinate als feste Zahl. Verschiebt
jemand den Standort in SebboHqSite.h, faehrt die Sonde weiter an die alte
Stelle, findet dort keinen Turm - und meldet das nicht als Fehler, sondern
als "Startpunkt steckt im Gelaende". Eine Messung, die still am falschen Ort
misst, ist schlimmer als keine.

KEINE ZAHL STEHT HIER. Standort und Ursprung werden aus den Headern gelesen:

    Source/WiesbadenReal/World/SebboHqSite.h        Breite, Laenge
    Source/WiesbadenReal/GIS/GeoCoordinateConverter.h  Ursprung, WGS84

Gespiegelt ist nur die RECHNUNG (ECEF -> ENU, wie GeoToUnrealGround). Dass
sie mit der Engine uebereinstimmt, behauptet dieses Modul nicht, sondern
test_sebbo_standort.py haelt es gegen einen gemessenen Wert aus dem
Spiel-Log fest.
"""
import argparse
import math
import os
import re
import sys

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE_H = os.path.join(WURZEL, "Source", "WiesbadenReal", "World", "SebboHqSite.h")
KONV_H = os.path.join(WURZEL, "Source", "WiesbadenReal", "GIS", "GeoCoordinateConverter.h")


def konstante(pfad, name):
    """Liest `<name> = <zahl>;` aus einem C++-Header.

    Absichtlich streng: fehlt die Konstante oder wurde sie umbenannt, bricht
    das hier ab. Ein Vorgabewert waere genau die stille Zweitwahrheit, die
    dieses Modul abschaffen soll.
    """
    with open(pfad, encoding="utf-8") as datei:
        text = datei.read()
    treffer = re.search(r"\b%s\s*=\s*(-?[0-9.]+(?:[eE][+-]?[0-9]+)?)\s*;" % name, text)
    if not treffer:
        raise SystemExit("%s: Konstante %s nicht gefunden" % (os.path.basename(pfad), name))
    return float(treffer.group(1))


def _ecef(lon_grad, lat_grad, hoehe_m, a, e2):
    """Geodaetisch -> ECEF, wie UGeoCoordinateConverter::GeodeticToECEF."""
    lon = math.radians(lon_grad)
    lat = math.radians(lat_grad)
    sin_lat = math.sin(lat)
    cos_lat = math.cos(lat)
    n = a / math.sqrt(1.0 - e2 * sin_lat * sin_lat)
    return (
        (n + hoehe_m) * cos_lat * math.cos(lon),
        (n + hoehe_m) * cos_lat * math.sin(lon),
        (n * (1.0 - e2) + hoehe_m) * sin_lat,
    )


def standort_cm(site_h=SITE_H, konv_h=KONV_H):
    """Weltkoordinate des Turmfusses in Zentimetern (X Ost, Y Sued).

    Wie GeoToUnrealGround: das Ziel wird auf der URSPRUNGSHOEHE ausgewertet,
    nicht auf 0. Mit Hoehe 0 liegt das Ergebnis rund 2 cm daneben - klein,
    aber es waere eine andere Rechnung als die der Engine.
    """
    lat = konstante(site_h, "Latitude")
    lon = konstante(site_h, "Longitude")
    a = konstante(konv_h, "WGS84_SemiMajorAxis")
    e2 = konstante(konv_h, "WGS84_EccentricitySquared")
    o_lon = konstante(konv_h, "WiesbadenOriginLongitude")
    o_lat = konstante(konv_h, "WiesbadenOriginLatitude")
    o_h = konstante(konv_h, "WiesbadenOriginHeight")

    ursprung = _ecef(o_lon, o_lat, o_h, a, e2)
    ziel = _ecef(lon, lat, o_h, a, e2)
    delta = [ziel[i] - ursprung[i] for i in range(3)]

    lon_rad = math.radians(o_lon)
    lat_rad = math.radians(o_lat)
    ost = (-math.sin(lon_rad), math.cos(lon_rad), 0.0)
    nord = (-math.sin(lat_rad) * math.cos(lon_rad),
            -math.sin(lat_rad) * math.sin(lon_rad),
            math.cos(lat_rad))

    punkt = lambda u, v: sum(u[i] * v[i] for i in range(3))
    # Unreal ist linkshaendig: +Y zeigt nach SUEDEN, darum das Minus.
    return (punkt(delta, ost) * 100.0, -punkt(delta, nord) * 100.0)


def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(description="Weltkoordinate des SebboTower.")
    p.add_argument("--json", action="store_true", help="als JSON statt als X,Y")
    a = p.parse_args(argv)

    x, y = standort_cm()
    if a.json:
        print('{"x_cm": %.0f, "y_cm": %.0f}' % (x, y))
    else:
        # Genau die Form, die -WbGoto erwartet.
        print("%.0f,%.0f" % (x, y))
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
