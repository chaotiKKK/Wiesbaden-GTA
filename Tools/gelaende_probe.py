"""Gelaende gegen Pflaster: liegt Gras UEBER Fahrbahn/Gehweg, oder schwebt Pflaster?

Die Pipeline prueft nur die Strassenmitte - genau dort ist das Gelaende eingeebnet.
Die Fehler sitzen an den Raendern: am Hang sticht bergseitig das Gras durch
Fahrbahnrand und Gehweg, talseitig haengt der Gehweg in der Luft (gemeldet
25.09.2026 fuer die Emser Strasse). Ursache ist das Landscape-Raster
(7,81 m Maschenweite), zwischen dessen Stuetzpunkten das Gelaende bilinear
ansteigt.

Gemessen wird gegen die ECHTE Landscape der gebackenen Karte (Strahl von oben,
nur Landscape-Treffer zaehlen), an jedem Mittellinienpunkt quer zur Strasse:
Fahrbahnrand innen (-30 cm) und Gehweg-Aussenkante (-30 cm), beidseitig.

    WB_MAP=WiesbadenCity_Alkis22              (Vorgabe: Default-Karte)
    WB_PROBE_STRASSE=Emser                    (Teilname; leer = nur Stichprobe)
    Tools/gelaende_probe.cmd

Ergebnis: gelaende_probe_result.txt in der Projektwurzel.
"""
import math
import os
import random

import unreal

ROOT = unreal.Paths.project_dir()
ERGEBNIS = os.path.join(ROOT, "gelaende_probe_result.txt")
ZEILEN = []


def log(msg):
    unreal.log("[Gelaende-Probe] %s" % msg)
    ZEILEN.append(str(msg))


def standard_karte():
    ini = os.path.join(ROOT, "Config", "DefaultEngine.ini")
    for line in open(ini, encoding="utf-8", errors="ignore"):
        if line.startswith("GameDefaultMap="):
            return line.split("=", 1)[1].strip().split(".")[0]
    return "/Game/Maps/WiesbadenCity_Alkis22"


def main():
    karte = os.environ.get("WB_MAP") or standard_karte()
    # Git-Bash verbiegt Werte mit fuehrendem "/" zu Windows-Pfaden - darum
    # genuegt auch der reine Kartenname (WB_MAP=WiesbadenCity_Alkis22).
    if "/Game/" in karte:
        karte = karte[karte.index("/Game/"):]
    elif not karte.startswith("/"):
        karte = "/Game/Maps/" + karte
    strasse = os.environ.get("WB_PROBE_STRASSE", "Emser")
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(karte):
        log("ABBRUCH: Karte %s nicht geladen" % karte)
        return
    world = unreal.EditorLevelLibrary.get_editor_world()
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    builder = next((a for a in actors if a.get_class().get_name() == "WiesbadenWorldBuilder"), None)
    lands = [a for a in actors if "Landscape" in a.get_class().get_name()]
    if builder is None or not lands:
        log("ABBRUCH: WorldBuilder %s, Landscape %d" % (builder, len(lands)))
        return
    net = builder.get_editor_property("road_network")
    segs = net.get_editor_property("segments")
    log("Karte %s: %d Segmente, %d Landscape-Actor(s)" % (karte, len(segs), len(lands)))

    def gelaende(x, y, z_hint):
        hits = unreal.SystemLibrary.line_trace_multi(
            world, unreal.Vector(x, y, z_hint + 5000.0), unreal.Vector(x, y, z_hint - 5000.0),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
        for h in hits or []:
            t = h.to_tuple()
            actor = t[9] if len(t) > 9 else None
            if actor is not None and "Landscape" in actor.get_class().get_name():
                return t[4].z   # impact point
        return None

    kreuzungen = [(i.get_editor_property("location").x, i.get_editor_property("location").y,
                   i.get_editor_property("radius_cm")) for i in net.get_editor_property("intersections")]

    def kreuzungsabstand(x, y):
        best = 1e18
        for kx, ky, kr in kreuzungen:
            if abs(kx - x) > 6000 or abs(ky - y) > 6000:
                continue
            best = min(best, math.hypot(kx - x, ky - y) - kr)
        return best

    def messe(seg, stats, worst, name, mit_kreuzung=False, mit_klasse=False):
        line = seg.get_editor_property("trimmed_centerline")
        if len(line) < 2:
            line = seg.get_editor_property("centerline")
        if len(line) < 2:
            return
        w = seg.get_editor_property("carriageway_width_cm") * 0.5
        sw = seg.get_editor_property("sidewalk_width_cm")
        kerb = seg.get_editor_property("kerb_height_cm")
        st_enum = seg.get_editor_property("sidewalk_type")
        typ = (getattr(st_enum, "name", None) or str(st_enum)).split(".")[-1].split(":")[0].strip(" <>").upper()
        seiten = {"BOTH": (1, -1), "LEFT": (-1,), "RIGHT": (1,)}.get(typ, ())
        for i in range(len(line)):
            p = line[i]
            q = line[min(i + 1, len(line) - 1)] if i + 1 < len(line) else line[i - 1]
            dx, dy = (q.x - p.x, q.y - p.y) if i + 1 < len(line) else (p.x - q.x, p.y - q.y)
            L = math.hypot(dx, dy) or 1.0
            # rechte Hand (Welt: Ost +X, Sued +Y)
            rx, ry = -dy / L, dx / L
            proben = [("Mitte", 1, 0.0, 0.0)] + [("Rand", s, w - 30.0, 0.0) for s in (1, -1)]
            proben += [("Gehweg", s, w + max(sw, 100.0) - 30.0, kerb) for s in seiten]
            for art, s, off, dz in proben:
                x, y = p.x + rx * off * s, p.y + ry * off * s
                g = gelaende(x, y, p.z)
                if g is None:
                    continue
                d = g - (p.z + dz)          # > 0: Gras ueber Pflaster
                if mit_klasse:
                    hw = seg.get_editor_property("highway_type")
                    hwn = str(getattr(hw, "name", hw)).upper()
                    weg = any(k in hwn for k in ("FOOTWAY", "CYCLEWAY", "PATH", "STEPS", "TRACK"))
                    art = ("Weg-" if weg else "Fahrbahn-") + art
                if mit_kreuzung:
                    art = art + ("-Kreuzungsnah" if kreuzungsabstand(x, y) < 1500 else "-frei")
                st = stats.setdefault(art, [0, 0, 0, 0.0])
                st[0] += 1
                if d > 2.0:
                    st[1] += 1
                if d < -40.0:
                    st[2] += 1
                st[3] += d
                worst.append((d, art, name, x, y))

    # 0) Punktabfrage: alle Treffer (Actor, Komponente, Hoehe) auf einem Raster
    #    um WB_PROBE_PUNKT=X,Y (Radius 10 m, Schritt 2,5 m) - fuer Flecken, die
    #    nicht zur Landscape gehoeren (Gruenflaechen-Meshes o. ae.).
    punkt = os.environ.get("WB_PROBE_PUNKT", "")
    if punkt:
        px, py = (float(v) for v in punkt.split(","))
        log("== Punktabfrage um (%.0f, %.0f) ==" % (px, py))
        for oy in range(-1000, 1001, 250):
            for ox in range(-1000, 1001, 250):
                hits = unreal.SystemLibrary.line_trace_multi(
                    world, unreal.Vector(px + ox, py + oy, 50000.0), unreal.Vector(px + ox, py + oy, -10000.0),
                    unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
                teile = []
                for h in hits or []:
                    t = h.to_tuple()
                    a = t[9] if len(t) > 9 else None
                    c = t[10] if len(t) > 10 else None
                    teile.append("%s/%s %.0f" % (a.get_actor_label() if a else "?", c.get_name() if c else "?", t[4].z))
                log("  (%+5d,%+5d): %s" % (ox, oy, " | ".join(teile) or "-"))
        for ks in net.get_editor_property("intersections"):
            loc = ks.get_editor_property("location")
            if math.hypot(loc.x - px, loc.y - py) < 2500:
                poly = ks.get_editor_property("polygon")
                zs = [q.z for q in poly] or [loc.z]
                log("  Kreuzung bei (%+6.0f,%+6.0f) Z %.0f, Radius %.0f, Platte %d Ecken Z %.0f..%.0f, Arme %d" % (
                    loc.x - px, loc.y - py, loc.z, ks.get_editor_property("radius_cm"), len(poly), min(zs), max(zs),
                    len(ks.get_editor_property("arms"))))
                for q in poly:
                    g = gelaende(q.x, q.y, q.z)
                    log("      Ecke (%+6.0f,%+6.0f) Z %.0f, Gelaende %s" % (q.x - px, q.y - py, q.z, "-" if g is None else "%.0f" % g))
        for seg in segs:
            line = seg.get_editor_property("trimmed_centerline")
            if len(line) < 2:
                line = seg.get_editor_property("centerline")
            nah = [(math.hypot(q.x - px, q.y - py), q) for q in line]
            nah = [t for t in nah if t[0] < 2500]
            if nah:
                hw = seg.get_editor_property("highway_type")
                log("  Segment %-24s %-12s Layer %d Breite %4.0f Begleitweg %s:" % (
                    str(seg.get_editor_property("street_name"))[:24], str(getattr(hw, "name", hw))[:12],
                    seg.get_editor_property("layer"), seg.get_editor_property("carriageway_width_cm"),
                    seg.get_editor_property("begleitweg")))
                w = seg.get_editor_property("carriageway_width_cm") * 0.5
                for i, q in enumerate(line):
                    if math.hypot(q.x - px, q.y - py) >= 1500:
                        continue
                    r = line[i + 1] if i + 1 < len(line) else line[i - 1]
                    dx, dy = (r.x - q.x, r.y - q.y) if i + 1 < len(line) else (q.x - r.x, q.y - r.y)
                    L = math.hypot(dx, dy) or 1.0
                    rx, ry = -dy / L, dx / L
                    quer = []
                    for off in (-(w - 30.0), 0.0, w - 30.0):
                        g = gelaende(q.x + rx * off, q.y + ry * off, q.z)
                        quer.append("-" if g is None else "%+.0f" % (g - q.z))
                    log("      (%+6.0f,%+6.0f) Achse %.0f | Gelaende minus Achse: Rand %s, Mitte %s, Rand %s" % (
                        q.x - px, q.y - py, q.z, quer[0], quer[1], quer[2]))

    # 1) Zielstrasse vollstaendig
    if strasse:
        stats, worst = {}, []
        n = 0
        for seg in segs:
            name = seg.get_editor_property("street_name")
            if strasse.lower() in str(name).lower() and seg.get_editor_property("layer") == 0 \
                    and not seg.get_editor_property("is_area"):
                messe(seg, stats, worst, str(name), mit_kreuzung=True)
                n += 1
        log("== %s (%d Segmente) ==" % (strasse, n))
        for art, (cnt, ueber, schwebt, summe) in stats.items():
            log("%-7s %5d Proben | Gras UEBER Pflaster (>2 cm): %4d (%.0f %%) | Pflaster schwebt (>40 cm): %4d (%.0f %%) | Mittel %+.0f cm"
                % (art, cnt, ueber, 100.0 * ueber / cnt, schwebt, 100.0 * schwebt / cnt, summe / cnt))
        mitte = sorted(d for d, art, *_ in worst if art.startswith("Mitte"))
        if mitte:
            log("Mitte: Soll -14 cm | Median %+.0f cm, 10%%-Quantil %+.0f, 90%%-Quantil %+.0f"
                % (mitte[len(mitte) // 2], mitte[len(mitte) // 10], mitte[len(mitte) * 9 // 10]))
        worst.sort(key=lambda t: -t[0])
        for d, art, name, x, y in worst[:5]:
            log("  schlimmstes Gras: %+.0f cm %s bei (%.0f, %.0f)" % (d, art, x, y))
        worst.sort(key=lambda t: t[0])
        for d, art, name, x, y in worst[:5]:
            log("  schlimmstes Schweben: %+.0f cm %s bei (%.0f, %.0f)" % (d, art, x, y))

    # Nachbarschaft der schlimmsten freien Stellen: welche Segmente liegen dort?
    if strasse:
        frei = sorted([t for t in worst if t[1].endswith("frei")], key=lambda t: -abs(t[0]))[:4]
        for d, art, name, x, y in frei:
            log("-- Umgebung von %s %+.0f cm bei (%.0f, %.0f):" % (art, d, x, y))
            for seg in segs:
                line = seg.get_editor_property("centerline")
                best = None
                for q in line:
                    dd = math.hypot(q.x - x, q.y - y)
                    if dd < 1500 and (best is None or dd < best[0]):
                        best = (dd, q.z)
                if best:
                    hw = seg.get_editor_property("highway_type")
                    log("     %-28s %-18s Layer %d  Abstand %4.0f cm  Achshoehe %.0f cm  Breite %.0f"
                        % (str(seg.get_editor_property("street_name"))[:28], str(getattr(hw, "name", hw))[:18],
                           seg.get_editor_property("layer"), best[0], best[1], seg.get_editor_property("carriageway_width_cm")))
            g = gelaende(x, y, 0.0 + 5000)
    # 1b) Laengsprofil-Dellen: Abweichung der Achse vom gleitenden Median
    #     (+-8 m) - dieselbe Groesse, die SmoothLongitudinalProfile entfernt.
    if os.environ.get("WB_PROBE_DELLEN"):
        klassen = {}
        beispiele = []
        for seg in segs:
            if seg.get_editor_property("layer") != 0 or seg.get_editor_property("is_area")                     or seg.get_editor_property("is_bridge") or seg.get_editor_property("is_tunnel"):
                continue
            line = seg.get_editor_property("centerline")
            if len(line) < 3:
                continue
            bogen = [0.0]
            for i in range(1, len(line)):
                bogen.append(bogen[-1] + math.hypot(line[i].x - line[i - 1].x, line[i].y - line[i - 1].y))
            if bogen[-1] < 1600:
                continue
            groesste = 0.0
            wo = None
            for i in range(len(line)):
                fenster = sorted(line[j].z for j in range(len(line)) if abs(bogen[j] - bogen[i]) <= 800)
                med = fenster[len(fenster) // 2]
                if abs(line[i].z - med) > abs(groesste):
                    groesste, wo = line[i].z - med, line[i]
            hw = seg.get_editor_property("highway_type")
            k = str(getattr(hw, "name", hw)).upper()
            st = klassen.setdefault(k, [0, 0, 0])
            st[0] += 1
            st[1] += abs(groesste) > 30
            st[2] += abs(groesste) > 60
            if abs(groesste) > 60:
                beispiele.append((abs(groesste), groesste, str(seg.get_editor_property("street_name")), wo.x, wo.y))
        log("== Laengsprofil: Abweichung vom Median +-8 m (Segmente >= 16 m) ==")
        for k, (n, d30, d60) in sorted(klassen.items(), key=lambda t: -t[1][0]):
            log("  %-16s %6d Segmente | ueber 30 cm: %5d (%.1f %%) | ueber 60 cm: %4d" % (k, n, d30, 100.0 * d30 / n, d60))
        for a, g, name, x, y in sorted(beispiele, reverse=True)[:10]:
            log("  %+5.0f cm %-28s bei (%.0f, %.0f)" % (g, name[:28], x, y))

    # 2) Stichprobe ueber die Stadt (feste Saat, vergleichbar zwischen Laeufen)
    rnd = random.Random(7)
    kandidaten = [s for s in segs if s.get_editor_property("layer") == 0 and not s.get_editor_property("is_area")
                  and not s.get_editor_property("is_bridge") and not s.get_editor_property("is_tunnel")]
    probe = rnd.sample(kandidaten, min(1500, len(kandidaten)))
    stats, worst = {}, []
    for seg in probe:
        messe(seg, stats, worst, str(seg.get_editor_property("street_name")), mit_klasse=True)
    log("== Stadt-Stichprobe (%d Segmente, Saat 7) ==" % len(probe))
    for art, (cnt, ueber, schwebt, summe) in stats.items():
        log("%-7s %5d Proben | Gras UEBER Pflaster (>2 cm): %4d (%.0f %%) | Pflaster schwebt (>40 cm): %4d (%.0f %%) | Mittel %+.0f cm"
            % (art, cnt, ueber, 100.0 * ueber / cnt, schwebt, 100.0 * schwebt / cnt, summe / cnt))


try:
    main()
except Exception as ex:   # noqa: BLE001 - Ergebnis muss in die Datei
    log("AUSNAHME: %r" % ex)
with open(ERGEBNIS, "w", encoding="utf-8") as f:
    f.write("\n".join(ZEILEN) + "\n")
if "-unattended" in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.quit_editor()
