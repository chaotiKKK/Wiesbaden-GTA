# -*- coding: utf-8 -*-
"""Beleg aus dem Log ziehen: ein Umlauf eines Wagens (-WbBusLogWagon).

Liest Saved/Logs/WiesbadenReal.log und schreibt einen Auszug nach
Saved/Diagnose/beleg_umlauf.txt:

  * Probentakt (2 s) und die Laenge des erfassten Umlaufs
  * Wendezeiten: zusammenhaengende VERWEILT-Bloecke mit Dauer, angekuendigter
    Zeit und Endpunkt-Kennzeichnung (Start- UND Mainzer Ende)
  * Fahrbahnhöhe: je Abschnitt (Wiesbaden / Mainz ab dem Uebergang) Anzahl
    Proben, Grundlage (Fahrbahn des Strassennetzes bzw. Gelaende-Trace),
    Abweichung und Unterkante - die Hoehen sind der Kern der Anfrage
  * Mitfahrt: Einsteigen mit Anker/Kamera, Aussteigen
  * die Boden-Audit-Zusammenfassungen beider Linien samt Orten der Fehlmessung

Die Grenze Wiesbaden/Mainz kommt aus den Haltestellen der Liniendatei
selbst (Bogenlaenge der ersten rechtsrheinisch gelegenen Halte), nicht aus
einer im Skript hinterlegten Zahl.

Aufruf: python Tools/umlauf_beleg.py [Wagennummer] [Logdatei] [Linie]
"""
import json
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WAGON = sys.argv[1] if len(sys.argv) > 1 else "601"
LOG = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "Saved", "Logs", "WiesbadenReal.log")
LINE_REF = sys.argv[3] if len(sys.argv) > 3 else (WAGON[0] if WAGON else "6")
OUT = os.path.join(ROOT, "Saved", "Diagnose", "beleg_umlauf.txt")

# Haltestelle, ab der die Linie in Mainz liegt. Mainz-Kastel gehoert noch zu
# Wiesbaden, die Rheinbruecke liegt davor - darum "Landtag" (erste Halte am
# linken Rheinufer). Der Name kommt aus der Liniendatei, die Bogenlaenge aus
# dem Pfad; keine im Skript hinterlegte Zahl.
MAINZ_STOP = "Landtag"


def hav(a, b):
    (la1, lo1), (la2, lo2) = a, b
    p = math.pi / 180.0
    dla, dlo = (la2 - la1) * p, (lo2 - lo1) * p
    h = math.sin(dla / 2) ** 2 + math.cos(la1 * p) * math.cos(la2 * p) * math.sin(dlo / 2) ** 2
    return 2 * 6371000.0 * math.asin(min(1.0, math.sqrt(h)))


def stop_arcs(line_ref):
    """Bogenlaenge jeder Haltestelle aus Pfad + Halteliste der Liniendatei."""
    path = os.path.join(ROOT, "Data", "Raw", "Bus", "line%s.json" % line_ref)
    if not os.path.exists(path):
        return None
    d = json.load(open(path, encoding="utf-8"))
    pts, names = d["path"], d["stop_names"]
    arc, acc = [0.0], 0.0
    for i in range(1, len(pts)):
        acc += hav(pts[i - 1], pts[i])
        arc.append(acc)
    out = []
    for i, st in enumerate(d["stops"]):
        # naechster Pfadpunkt zur Halte
        best = min(range(len(pts)), key=lambda k: hav(pts[k], st))
        out.append((names[i] if i < len(names) else "?", arc[best], st))
    return out


def fmt_section(title, rows, aus):
    if not rows:
        return
    lane = sum(1 for r in rows if r["src"].startswith("Fahrbahn"))
    dev = [r["dev"] for r in rows]
    z = [r["z"] for r in rows]
    aus.append("%s: %d Proben, Bogen %.0f..%.0f m" % (title, len(rows), rows[0]["arc"], rows[-1]["arc"]))
    aus.append("    Grundlage: %d x Fahrbahn (Strassennetz), %d x Gelaende-Trace"
               % (lane, len(rows) - lane))
    aus.append("    Abweichung Spur <-> Boden-Trace: min %+.0f, Mittel %+.0f, max %+.0f cm"
               % (min(dev), sum(dev) / len(dev), max(dev)))
    aus.append("    Unterkante (Radsatz): %.2f..%.2f m" % (min(z), max(z)))


def main():
    lines = open(LOG, encoding="utf-8", errors="replace").read().splitlines()
    proto = re.compile(r"Umlauf Wagen (\d+) \((\w+)\)")
    rows = []
    for ln in lines:
        m = proto.search(ln)
        if not m or m.group(1) != WAGON:
            continue
        t = re.search(r"t=([\d.]+) s", ln)
        arc = re.search(r"Bogen ([\d.]+) von ([\d.]+) m", ln)
        z = re.search(r"Unterkante ([\-\d.]+) m", ln)
        src = re.search(r"Grundlage ([^(]+)\(", ln)
        trace = re.search(r"Gelaende-Trace ([\-\d.]+) m, Abweichung ([+\-\d.]+) cm", ln)
        state = re.search(r"\| (VERWEILT noch (\d+) s von (\d+) s|faehrt)([^|]*)\|", ln)
        dirn = re.search(r"Richtung (\w+)", ln)
        ride = re.search(r"Mitfahrt (\w+)", ln)
        if not (t and arc and z and src and trace and state and dirn and ride):
            continue
        rows.append({
            "raw": ln, "t": float(t.group(1)), "arc": float(arc.group(1)),
            "total": float(arc.group(2)), "z": float(z.group(1)),
            "src": src.group(1).strip(), "trace": float(trace.group(1)),
            "dev": float(trace.group(2)),
            "dwell": state.group(1).startswith("VERWEILT"),
            "dwell_s": float(state.group(2) or 0), "dwell_total": float(state.group(3) or 0),
            "flag": state.group(4).strip(), "dir": dirn.group(1), "ride": ride.group(1),
        })
    if not rows:
        print("keine Umlauf-Zeilen fuer Wagen %s gefunden" % WAGON)
        return 1

    line = rows[0]["raw"].split("(", 1)[1].split(")", 1)[0]
    aus = ["Beleg: Umlauf Wagen %s (Linie %s), aus %s" % (WAGON, line, os.path.basename(LOG))]
    gaps = [b["t"] - a["t"] for a, b in zip(rows, rows[1:])]
    aus.append("Proben: %d, t = %.0f..%.0f s (%.0f min), Bogen %.0f..%.0f von %.0f m"
               % (len(rows), rows[0]["t"], rows[-1]["t"], (rows[-1]["t"] - rows[0]["t"]) / 60.0,
                  rows[0]["arc"], rows[-1]["arc"], rows[0]["total"]))
    aus.append("Takt: Median %.1f s, groesste Luecke %.1f s, Summe der Fahrtrichtung(en) %s"
               % (sorted(gaps)[len(gaps) // 2], max(gaps),
                  ",".join(sorted({r["dir"] for r in rows}))))

    # Wendezeiten: zusammenhaengende VERWEILT-Bloecke
    bloecke = []
    for r in rows:
        if not r["dwell"]:
            continue
        if not bloecke or r["t"] - bloecke[-1]["bis"] > 4.0:
            bloecke.append({"von": r["t"], "bis": r["t"], "first": r, "last": r})
        else:
            bloecke[-1]["bis"] = r["t"]
            bloecke[-1]["last"] = r
    kurz = []
    for b in bloecke:
        f = b["first"]
        if f["dwell_total"] < 60.0:
            # Haltestellenverweilen: eine Zeile je Block, kein Zeilenauszug.
            kurz.append("t %.0f s Bogen %.0f m %.0f s" % (f["t"], f["arc"], f["dwell_total"]))
            continue
        aus.append("WENDEZEIT: t %.0f..%.0f s = %.0f s abgedeckt (2-s-Takt), angekuendigt "
                   "%.0f s, Bogen %.0f m, Kennzeichnung '%s', Richtung %s"
                   % (f["t"], b["bis"], b["bis"] - f["t"], f["dwell_total"], f["arc"],
                      f["flag"] or "-", f["dir"]))
        # Mitte des Blocks: belegt, dass die Wendezeit durchlaeuft und nicht nur
        # zweimal gemeldet wurde.
        mitte = min((r for r in rows), key=lambda r: abs(r["t"] - (f["t"] + b["bis"]) / 2.0))
        for label, row in (("erste", f), ("mittlere", mitte), ("letzte", b["last"])):
            aus.append("    %s Zeile: %s" % (label, row["raw"].split("LogWbBus: ", 1)[-1]))
    if kurz:
        aus.append("Haltestellenverweilen (%d Bloecke, 8 s angekuendigt): %s"
                   % (len(kurz), "; ".join(kurz)))

    stops = stop_arcs(line)
    mainz_arc = None
    if stops:
        for name, arc_m, _ in stops:
            if MAINZ_STOP.lower() in name.lower():
                mainz_arc = arc_m
                aus.append("Uebergang nach Mainz: Halte '%s' bei Bogen %.0f m (aus Pfadlänge "
                           "der Liniendatei gerechnet)" % (name, arc_m))
                break
    total = rows[0]["total"] * 100.0
    hin = [r for r in rows if r["dir"] == "hin"]
    zurueck = [r for r in rows if r["dir"] == "zurueck"]
    for name, teil in (("Abschnitt Wiesbaden", [r for r in rows if r["arc"] < (mainz_arc or total * 0.5)]),
                       ("Abschnitt Mainz", [r for r in rows if r["arc"] >= (mainz_arc or total * 0.5)])):
        fmt_section("%s + Hinfahrt/Rueckfahrt" % name, teil, aus)
    fmt_section("Fahrt hin", hin, aus)
    fmt_section("Fahrt zurueck", zurueck, aus)

    for l in lines:
        if "Mitfahrt Linie" in l and "Auge (" in l:
            aus.append("Mitfahrt: " + l.split("LogWbBus: ", 1)[-1])
    for l in lines:
        if "Fahrgast ausgestiegen" in l or "Bus-Diagnose: nach" in l:
            aus.append("Ausstieg: " + l.split("LogWbBus: ", 1)[-1])
    # Groesste Abweichungen des Protokolls selbst (Orte nennen, nicht nur Zahlen):
    extreme = sorted(rows, key=lambda r: -abs(r["dev"]))[:3]
    for r in extreme:
        aus.append("Groesste Protokoll-Abweichung: %+.0f cm bei Bogen %.0f m - %s"
                   % (r["dev"], r["arc"], r["raw"].split("LogWbBus: ", 1)[-1]))

    # Die Audit-Zeilen wiederholen sich (die Datei wird laufend neu geschrieben):
    # je Linie die LETZTE Zusammenfassung und die Ortsangaben ohne Doppelung.
    sums, worst = {}, []
    for l in lines:
        if "Boden-Audit Linie" not in l:
            continue
        txt = l.split("LogWbBus: ", 1)[-1]
        if txt.startswith("Boden-Audit Linie") and " | " in txt:
            sums[txt.split(":", 1)[0]] = txt
        elif txt not in worst:
            worst.append(txt)
    for key in sorted(sums):
        aus.append("Audit: " + sums[key])
    for txt in worst:
        aus.append("Audit-Ort: " + txt)

    text = "\n".join(aus) + "\n"
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(text)
    print("geschrieben: %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
