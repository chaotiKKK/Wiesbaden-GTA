"""Baut eine Liniendatei (Data/Raw/Bus/line<ref>.json) aus einer OSM-Busrelation.

Warum neu geschrieben: der erste Fasser (17.09.) suchte die naechste Fortsetzung
ueber ABGERUNDETE KOORDINATEN (7 Stellen) und brach ab, sobald kein Weg mehr mit
genau diesem Endpunkt begann. Genau das passierte bei Linie 6 hinter "Mudra-
Kaserne": der gefahrene Weg endete danach bei 10,2 km an der Stadtgrenze, obwohl
die Relation 297 Wege bis Mainz-Gonsenheim enthaelt - die Strecke nach Mainz
"fehlte" also im Spiel, obwohl sie in OSM vollstaendig ist.

Jetzt laeuft der Fasser die MITGLIEDER IN IHRER REIHENFOLGE ab und verbindet
ueber KNOTEN-IDs (exakt, nicht gerundet). Wenn der naechste Weg nicht anschliesst,
sucht er unter den restlichen Wegen den, der am aktuellen Endknoten haengt
(OSM-Reihenfolge ist nicht immer perfekt); bleibt eine Luecke, wird sie mit einem
geraden Stueck ueberbrueckt und GEMELDET (die Laenge steht im Bericht - eine
grosse Luecke waere ein Datenfehler, keine Fahrbahn).

Halte: die Node-Mitglieder mit Rolle stop/stop_entry_only in Relations-Reihenfolge,
mit ihren Namen (die liefert die OSM-API mit). Die Zuordnung auf die Polylinie
macht der Actor (naechster Punkt).

RUECKWEG (25.09.): die Gegenrichtung faehrt NICHT mehr dieselbe Linie
rueckwaerts, sondern ihre eigene OSM-Relation (LINE_RETURN) mit eigenen Halten.
Auf getrennten Richtungsfahrbahnen (Hauptbahnhof) liegt ihre Halte auf der
ANDEREN Fahrbahn - rueckwaerts auf der Hinweg-Linie stand der Bus dort auf der
falschen Strassenseite. Dazu die Anschlussfahrten im echten Strassennetz
(Overpass, Einbahnstrassen beachtet): am fernen Ende vom Hinweg-Ende zum
Rueckweg-Anfang, am Nordfriedhof von der Ausstiegs- zur Einstiegshaltestelle -
real biegen 3 und 6 vor der Kreuzung rechts in den Hellkundweg (Ausstieg,
Pause) und fahren in einer Linkskurve zur Einstiegshaltestelle.
Neue Schluessel: return_path, return_stops, return_stop_names, return_source.

Aufruf: python Tools/build_bus_line.py [ref ...]
"""
import hashlib
import heapq
import json
import math
import os
import sys
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
PROJ = HERE.rsplit("/Tools", 1)[0]
OSM_DIR = PROJ + "/Data/Raw/OSM"
BUS_DIR = PROJ + "/Data/Raw/Bus"

# ref -> Relation (muss zu Tools/fetch_bus_lines.py passen)
LINE_RELATIONS = {
    "6": 17483739,
    "3": 18290457,
}

# Zielschilder je Linie. Die Materialien liegen unter /Game/Vehicles/Bus/Blind
# (Tools/make_bus_blind_textures.py + import_bus_blinds.py); der Bus liest die
# Namen aus der Liniendatei. Fuer Linie 6 sind es die bestehenden Assets, fuer
# Linie 3 neu erzeugte.
LINE_BLINDS = {
    "6": {"forward": "M_WbBlindMainz", "backward": "M_WbBlindNord", "line": "M_WbBlindRoute6"},
    "3": {"forward": "M_WbBlindL3Rheinufer", "backward": "M_WbBlindL3Nordfriedhof", "line": "M_WbBlindL3"},
}

# Anzeigetexte der Schilder (Vorder-/Seitenschild) und Halte mit DFI-Saeule.
# Die Halteliste wird gegen stop_names geprueft: ein Tippfehler wuerde sonst
# stillschweigend keine Saeule aufstellen.
LINE_DISPLAY = {
    # all_stops: JEDE Halte bekommt eine DFI-Saeule. Der Monitor stellt je Halte
    # ZWEI auf (eine pro Strassenseite) - in der Datei steht dann statt einer
    # Namensliste der Platzhalter "*" (siehe WiesbadenBusLineFile::ReadLine).
    # Ohne ihn bekaeme eine verlaengerte Linie still weniger Saeulen als vorher.
    "6": {"forward": "Mainz-Gonsenheim", "backward": "Nordfriedhof", "all_stops": True},
    "3": {"forward": "Biebrich Rheinufer", "backward": "Nordfriedhof", "all_stops": True},
}

# Gegenrichtung je Linie (Tools/fetch_bus_lines.py holt sie mit). Gewaehlt ist
# jeweils die Relation mit demselben "via" wie die Hinfahrt.
LINE_RETURN = {
    "6": 1730099,   # Mainz Gonsenheim Wildpark -> Wiesbaden Nordfriedhof
    "3": 7190688,   # Wilhelm-Kopp-Strasse -> Nordfriedhof (via Welfenstrasse)
}

# Ausstieg am Nordfriedhof, wo die OSM-Relation veraltet ist: die 3 biegt wie
# die 6 vor der Kreuzung rechts in den Hellkundweg (Ortskenntnis, 25.09.); die
# Relation 7190688 fuehrt den Ausstieg noch an der Platter Strasse.
ARRIVAL_OVERRIDE = {
    "3": (50.0968507, 8.2211677),
}

# Anschlussfahrten laufen ueber diese Strassenarten (Overpass-Ausschnitt).
LINK_HIGHWAYS = {"motorway", "trunk", "primary", "secondary", "tertiary", "unclassified", "residential",
                 "service", "living_street", "busway", "motorway_link", "trunk_link", "primary_link",
                 "secondary_link", "tertiary_link", "bus_guideway"}
OVERPASS = ["https://lz4.overpass-api.de/api/interpreter", "https://overpass-api.de/api/interpreter"]

BLIND_DIR = "/Game/Vehicles/Bus/Blind"

# Ab dieser Laenge gilt ein ueberbruecktes Stueck als verdaechtig und wird laut
# gemeldet. Kurze Spruenge (abgeschnittene Einmuendungen) sind normal.
GAP_WARN_M = 40.0
# Suchfenster beim Umsortieren: so viele der restlichen Wege werden nach einem
# Anschluss am aktuellen Endknoten abgesucht.
LOOKAHEAD = 12


def dist_m(a, b):
    dy = (b[0] - a[0]) * 111320.0
    dx = (b[1] - a[1]) * 111320.0 * math.cos(math.radians((a[0] + b[0]) * 0.5))
    return math.hypot(dx, dy)


def haversine_m(a, b):
    return dist_m(a, b)


def load_relation(rel_id):
    path = "%s/bus_route_%d.json" % (OSM_DIR, rel_id)
    if not os.path.exists(path):
        raise SystemExit("Rohdatei fehlt: %s - erst Tools/fetch_bus_lines.py laufen lassen." % path)
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def build(ref):
    fwd = assemble(ref, LINE_RELATIONS[ref])
    out = fwd["out"]
    ret_id = LINE_RETURN.get(ref)
    if ret_id:
        ret = assemble(ref, ret_id, quiet=False)
        add_return_leg(ref, out, fwd, ret)
    os.makedirs(BUS_DIR, exist_ok=True)
    out_path = "%s/line%s.json" % (BUS_DIR, ref)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print("           Datei geschrieben: %s" % os.path.basename(out_path))
    return out


def fetch_roads(lo, hi):
    """Strassen in einem Ausschnitt (Overpass), zwischengespeichert in Data/Raw/OSM."""
    key = hashlib.sha1(("%.5f,%.5f,%.5f,%.5f" % (lo[0], lo[1], hi[0], hi[1])).encode()).hexdigest()[:12]
    cache = "%s/links_%s.json" % (OSM_DIR, key)
    if os.path.exists(cache):
        with open(cache, encoding="utf-8") as f:
            return json.load(f)
    q = '[out:json][timeout:90];way(%.5f,%.5f,%.5f,%.5f)["highway"];(._;>;);out body;' % (lo[0], lo[1], hi[0], hi[1])
    last = None
    for host in OVERPASS:
        try:
            req = urllib.request.Request(host + "?data=" + urllib.parse.quote(q),
                                         headers={"User-Agent": "WiesbadenReal/1.0 (Buslinien)", "Accept": "application/json"})
            d = json.load(urllib.request.urlopen(req, timeout=120))
            with open(cache, "w", encoding="utf-8") as f:
                json.dump(d, f)
            return d
        except Exception as ex:   # naechster Server
            last = ex
    raise SystemExit("Overpass nicht erreichbar: %s" % last)


def route_link(a, b, label):
    """Fahrweg von a nach b im OSM-Strassennetz (Dijkstra, Einbahnstrassen beachtet)."""
    if dist_m(a, b) < 3.0:
        return []
    m = 0.004
    lo = (min(a[0], b[0]) - m, min(a[1], b[1]) - m)
    hi = (max(a[0], b[0]) + m, max(a[1], b[1]) + m)
    net = fetch_roads(lo, hi)
    nodes = {e["id"]: (e["lat"], e["lon"]) for e in net["elements"] if e["type"] == "node"}
    graph = {}
    for w in net["elements"]:
        if w["type"] != "way":
            continue
        t = w.get("tags", {})
        if t.get("highway") not in LINK_HIGHWAYS or t.get("access") in ("no", "private") and t.get("psv") != "yes":
            continue
        ns = [n for n in w["nodes"] if n in nodes]
        one = t.get("oneway") in ("yes", "1", "true") and t.get("oneway:bus") != "no"
        for p, q in zip(ns, ns[1:]):
            d = dist_m(nodes[p], nodes[q])
            graph.setdefault(p, []).append((q, d))
            if not one:
                graph.setdefault(q, []).append((p, d))
    if not graph:
        print("  WARNUNG: %s - kein Strassennetz, gerade verbunden" % label)
        return [b]
    na = min(graph, key=lambda n: dist_m(nodes[n], a))
    nb = min(graph, key=lambda n: dist_m(nodes[n], b))
    best, prev, heap = {na: 0.0}, {}, [(0.0, na)]
    while heap:
        d, n = heapq.heappop(heap)
        if n == nb:
            break
        if d > best.get(n, 1e18):
            continue
        for q, w in graph.get(n, []):
            if d + w < best.get(q, 1e18):
                best[q], prev[q] = d + w, n
                heapq.heappush(heap, (d + w, q))
    if nb not in best:
        print("  WARNUNG: %s - kein Fahrweg gefunden, gerade verbunden (%.0f m)" % (label, dist_m(a, b)))
        return [b]
    chain = [nb]
    while chain[-1] != na:
        chain.append(prev[chain[-1]])
    pts = [nodes[n] for n in reversed(chain)]
    print("           Anschluss %s: %.0f m ueber %d Knoten (Luftlinie %.0f m)"
          % (label, best[nb], len(pts), dist_m(a, b)))
    return pts + [b]


def join_paths(*parts):
    out = []
    for part in parts:
        for p in part:
            p = (round(p[0], 7), round(p[1], 7))
            if not out or dist_m(out[-1], p) > 0.05:
                out.append(p)
    return out


def add_return_leg(ref, out, fwd, ret):
    """Rueckweg = Anschluss am fernen Ende + Gegenrichtungs-Relation + Nordfriedhof-Anschluss."""
    fpath, rpath = fwd["path"], ret["path"]
    far = route_link(fpath[-1], rpath[0], "fernes Ende (Hinweg-Ende -> Rueckweg-Anfang)")
    stops, names = list(ret["stops"]), list(ret["names"])
    arrival = ARRIVAL_OVERRIDE.get(ref)
    if arrival:
        # Veralteter Ausstieg: die letzte Halte (Nordfriedhof) an die echte Stelle legen.
        stops[-1] = [round(arrival[0], 7), round(arrival[1], 7)]
    departure = fpath[0]
    near = route_link(rpath[-1], departure, "Nordfriedhof (Ausstieg -> Einstieg)")
    path = join_paths(far, rpath, near)
    # Pruefen: liegen die Rueckweg-Halte aufsteigend auf dem Rueckweg?
    arc = [0.0]
    for i in range(1, len(path)):
        arc.append(arc[-1] + dist_m(path[i - 1], path[i]))
    sa = []
    for c in stops:
        bi = min(range(len(path)), key=lambda i: dist_m(c, path[i]))
        sa.append((arc[bi], dist_m(c, path[bi])))
    ok = all(sa[i][0] <= sa[i + 1][0] + 1.0 for i in range(len(sa) - 1))
    out["return_path"] = [[p[0], p[1]] for p in path]
    out["return_stops"] = stops
    out["return_stop_names"] = names
    out["return_source"] = "OSM Relation %d (%s), abgerufen per OSM-API full.json" % (ret["rel_id"], ret["name"])
    print("Linie %-2s Rueckweg: %d Punkte, %.2f km, %d Halte, aufsteigend %s, groesster Halt-Abstand %.0f m"
          % (ref, len(path), arc[-1] / 1000.0, len(stops), "ja" if ok else "NEIN", max(x[1] for x in sa)))
    print("           Ausstieg Nordfriedhof bei Bogen %.0f m, Rueckweg endet an der Einstiegshaltestelle (%.0f m daneben)"
          % (sa[-1][0], dist_m(path[-1], departure)))


def assemble(ref, rel_id, quiet=False):
    data = load_relation(rel_id)
    els = data["elements"]
    rel = next(e for e in els if e["type"] == "relation")
    nodes = {e["id"]: (e["lat"], e["lon"]) for e in els if e["type"] == "node"}
    ways = {e["id"]: e["nodes"] for e in els if e["type"] == "way"}
    way_tags = {e["id"]: e.get("tags", {}) for e in els if e["type"] == "way"}
    node_tags = {e["id"]: e.get("tags", {}) for e in els if e["type"] == "node"}

    # Steig-/Bahnsteigflaechen sind MITGLIEDER der Relation, aber kein Fahrweg:
    # `highway=platform` (Rolle "platform") beschreibt den Wartebereich an der
    # Halte - und zwar als geschlossene Flaeche. Ungefiltert landete die Schleife
    # in der Strecke (Linie 6 fuhr am Mainzer Hauptbahnhof West rund 20 Punkte
    # um den Steg herum, am Landtag 25 m quer ueber den Steig).
    member_ways = [(m["ref"], (m.get("role") or "")) for m in rel["members"] if m["type"] == "way"]

    def is_platform(w):
        t = way_tags.get(w, {})
        return (t.get("highway") == "platform" or t.get("public_transport") == "platform"
                or t.get("railway") == "platform")

    platform_members = [w for w, role in member_ways if role == "platform" or is_platform(w)]
    plat_skip = set(platform_members)
    way_members = [w for w, _ in member_ways if w not in plat_skip]
    stop_members = [m for m in rel["members"]
                    if m["type"] == "node" and (m.get("role") or "").startswith("stop")]

    # Halte in Relations-Reihenfolge (die erste Halte ist der Anfang der Linie).
    stops, names = [], []
    for m in stop_members:
        c = nodes.get(m["ref"])
        if not c:
            continue
        nm = node_tags.get(m["ref"], {}).get("name")
        if names and nm and names[-1] == nm:
            continue
        stops.append([round(c[0], 7), round(c[1], 7)])
        names.append(nm or "")

    # -- Wege zur Fahrstrecke verbinden --------------------------------------
    # Gierig ueber WEGENDEN, mit der Mitglieder-Reihenfolge als Vorliebe.
    #
    # Drei Verfahren wurden hier durchprobiert, die ersten zwei mit Beleg:
    #  1. KOORDINATEN-ENDEN gerundet (die erste Fassung): brach ab, sobald kein
    #     Weg mehr mit genau diesem Endpunkt begann - Linie 6 endete dadurch
    #     hinter "Mudra-Kaserne", die 8 km nach Mainz fehlten.
    #  2. NUR MITGLIEDER-REIHENFOLGE: Linie 6 beginnt mit drei STUMMELN (25/60/
    #     146 m) bei Mainz-Kastel - die Reihe laeuft dort ins Leere, und der
    #     Fasser sprang anschliessend 9,8 km quer durch die Stadt.
    #  3. Nur KNOTEN-ANSCHLUSS (graphisch): verliert bei Linie 3 ganze 19 Wege,
    #     weil dort Wege an einem MITTLEREN Knoten haengen (eine Querstrasse
    #     zweigt mitten in einem laengeren Weg ab) - die Kette zerfaellt dann in
    #     Teile, die per Definition nicht "anschliessen".
    #
    # Deshalb jetzt: naechster Weg = der mit dem naechsten ENDPUNKT; sind
    # mehrere fast gleich nah (Toleranz), gewinnt die fruehere Mitgliedsnummer.
    # Ein kurzes Eckenschneiden an Knoten (wenige Meter) ist die Folge und fuer
    # die Fahrt unschaedlich; echte Spruenge werden gemeldet.
    #
    # ABER: ueber eine echte Luecke wird NICHT gesprungen. Der Rest der Relation
    # bleibt liegen und wird gemeldet - eine gerade Linie von 4 km quer durch
    # die Stadt als "Fahrbahn" waere schlimmer als ein fehlender Rest.
    TOL_M = 25.0
    GAP_MAX_M = 400.0

    def ends_pts(w):
        ids = [n for n in ways[w] if n in nodes]
        return ids, [nodes[n] for n in ids]

    # Start: das Ende der Strecke, das der ersten Halte am naechsten liegt.
    start_pick, start_d = None, float("inf")
    for i, w in enumerate(way_members):
        ids, pts = ends_pts(w)
        if not pts:
            continue
        d = min(dist_m(pts[0], stops[0]), dist_m(pts[-1], stops[0]))
        if d < start_d:
            start_d, start_pick = d, i

    path = []
    used = set()
    gaps = []
    remaining = list(way_members)
    cur = None
    if start_pick is not None:
        w = remaining.pop(start_pick)
        ids, pts = ends_pts(w)
        if dist_m(pts[-1], stops[0]) < dist_m(pts[0], stops[0]):
            ids, pts = list(reversed(ids)), list(reversed(pts))
        path.extend(pts)
        used.add(w)
        cur = pts[-1]

    while remaining:
        best_i, best_d = None, float("inf")
        for i, w in enumerate(remaining):
            ids, pts = ends_pts(w)
            if not pts:
                continue
            d = min(dist_m(cur, pts[0]), dist_m(cur, pts[-1]))
            if d < best_d - TOL_M or (best_i is None and d <= best_d + TOL_M):
                best_d, best_i = d, i
        if best_i is None or best_d > GAP_MAX_M:
            break
        w = remaining.pop(best_i)
        ids, pts = ends_pts(w)
        if dist_m(cur, pts[-1]) < dist_m(cur, pts[0]):
            ids, pts = list(reversed(ids)), list(reversed(pts))
        if best_d > 1.0:
            gaps.append((best_d, path[-1], pts[0]))
        path.extend(pts)
        used.add(w)
        cur = pts[-1]

    # Doppelte Punkte entfernen. Zwei aufeinanderfolgende Wege enden und beginnen
    # am selben Knoten (289 solcher Stellen bei Linie 6) - das sind Nulllaengen-
    # Segmente in der Fahrtlinie, in denen die Richtung unbestimmt ist und die
    # Bogenlaenge stillsteht. Entdoppelt wird auf der gerundeten Koordinate, denn
    # nur die steht spaeter in der Datei.
    rounded = [(round(a, 7), round(b, 7)) for (a, b) in path]
    path = [p for i, p in enumerate(rounded) if i == 0 or p != rounded[i - 1]]
    dropped_dups = len(rounded) - len(path)

    # Pruefen: liegen die Halte in Fahrtrichtung aufsteigend auf der Linie?
    arc = [0.0]
    for i in range(1, len(path)):
        arc.append(arc[-1] + dist_m(path[i - 1], path[i]))
    stop_arc = []
    for c in stops:
        best, bestd = 0, float("inf")
        for i, p in enumerate(path):
            d = dist_m(c, p)
            if d < bestd:
                bestd, best = d, i
        stop_arc.append((arc[best], bestd))
    order_ok = all(stop_arc[i][0] <= stop_arc[i + 1][0] + 1.0 for i in range(len(stop_arc) - 1))

    disp = LINE_DISPLAY.get(ref, {})
    result = {"rel_id": rel_id, "name": rel["tags"].get("name"), "path": path, "stops": stops, "names": names}
    if disp.get("all_stops"):
        mon_stops = ["*"]
    else:
        mon_stops = [s for s in disp.get("monitor_stops", []) if s in names]
    wanted = [] if disp.get("all_stops") else disp.get("monitor_stops", [])
    missing = [s for s in wanted if s not in names]
    if missing:
        print("  WARNUNG: DFI-Halte nicht in der Linie: %s" % ", ".join(missing))

    out = {
        "ref": rel["tags"].get("ref"),
        "from": rel["tags"].get("from"),
        "to": rel["tags"].get("to"),
        "via": rel["tags"].get("via"),
        "colour": rel["tags"].get("colour"),
        "operator": rel["tags"].get("operator"),
        "source": "OSM Relation %d (%s), abgerufen per OSM-API full.json" % (rel_id, rel["tags"].get("name")),
        "interval_minutes": int(rel["tags"].get("interval", 20)),
        "headway_seconds": int(rel["tags"].get("interval", 20)) * 60,
        "terminus_dwell_seconds": 600,
        "blinds": {
            "dir": BLIND_DIR,
            "forward": LINE_BLINDS.get(ref, {}).get("forward", "M_WbBlindMainz"),
            "backward": LINE_BLINDS.get(ref, {}).get("backward", "M_WbBlindNord"),
            "line": LINE_BLINDS.get(ref, {}).get("line", "M_WbBlindRoute6"),
            "text_forward": disp.get("forward", rel["tags"].get("to")),
            "text_backward": disp.get("backward", rel["tags"].get("from")),
        },
        "monitor_stops": mon_stops,
        "path": [[round(a, 7), round(b, 7)] for (a, b) in path],
        "stops": stops,
        "stop_names": names,
    }
    result["out"] = out
    total = arc[-1] / 1000.0
    print("Linie %-2s: %d/%d Wege verbaut, %d Punkte, %.2f km, %d Halte"
          % (ref, len(used), len(way_members), len(path), total, len(stops)))
    print("           Ziel laut OSM: %s -> %s" % (out["from"], out["to"]))
    print("           Halte aufsteigend: %s, groesster Halt-Abstand zur Linie %.0f m"
          % ("ja" if order_ok else "NEIN", max([s[1] for s in stop_arc] or [0])))
    print("           Streckenanfang %.0f m von Halte 0 entfernt (%.5f, %.5f), Streckenende %.0f m von der letzten Halte"
          % (dist_m(path[0], stops[0]) if path and stops else -1.0, path[0][0], path[0][1],
             dist_m(path[-1], stops[-1]) if path and stops else -1.0))
    print("           Ueberbrueckte Luecken: %d (Summe %.0f m)"
          % (len(gaps), sum(g[0] for g in gaps)))
    print("           %d doppelte Knoten entfernt (Wege teilen sich Anfang/Ende)" % dropped_dups)
    for g in [x for x in gaps if x[0] > GAP_WARN_M][:6]:
        print("             Luecke %.0f m bei (%.5f, %.5f)" % (g[0], g[1][0], g[1][1]))
    if platform_members:
        names_p = []
        for w in platform_members:
            nm = way_tags.get(w, {}).get("name")
            names_p.append(nm if nm else str(w))
        print("           %d Steig-/Bahnsteigflaechen sind kein Fahrweg (uebersprungen): %s"
              % (len(platform_members), ", ".join(names_p[:6])))
    if remaining:
        rest = 0.0
        for w in remaining:
            ids, pts = ends_pts(w)
            rest += sum(dist_m(pts[i], pts[i + 1]) for i in range(len(pts) - 1))
        print("           %d Wege (%.0f m) lagen zu weit weg (> %.0f m) und bleiben ungenutzt"
              % (len(remaining), rest, GAP_MAX_M))
    if len(used) != len(way_members):
        print("           %d Wege NICHT verbaut (nicht angeschlossen) - erste:"
              % (len(way_members) - len(used)))
        shown = 0
        for w in way_members:
            if w in used:
                continue
            pts = [nodes[n] for n in ways[w] if n in nodes]
            if not pts:
                continue
            ln = sum(dist_m(pts[i], pts[i + 1]) for i in range(len(pts) - 1))
            print("             Weg %d: %.0f m bei (%.5f, %.5f)"
                  % (w, ln, pts[0][0], pts[0][1]))
            shown += 1
            if shown >= 6:
                break
    if not order_ok:
        print("           WARNUNG: Halte-Reihenfolge passt nicht zur Fahrtrichtung.")
    return result


def main():
    want = [a for a in sys.argv[1:] if not a.startswith("-")] or sorted(LINE_RELATIONS)
    for ref in want:
        build(ref)


if __name__ == "__main__":
    main()
