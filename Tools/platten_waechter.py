r"""Der Plattenwaechter: meldet, bevor die Platte voll ist - und loescht NICHTS allein.

    python Tools/platten_waechter.py                        # Bericht, Exit 3 wenn unter der Grenze
    python Tools/platten_waechter.py --immer                 # Bericht auch bei genug Platz
    python Tools/platten_waechter.py --reinigen --trocken    # zeigt, was geloescht wuerde
    python Tools/platten_waechter.py --reinigen              # loescht die als 'cache' eingestuften Ordner
    python Tools/platten_waechter.py --schwelle 30            # eigene Grenze in Prozent
    python Tools/platten_waechter.py --budget 5               # Sekunden Mess-Budget

WOFUER: Am 27.09.2026 stand C: bei 93 Prozent (70 GB frei von 953) und es fiel
niemandem auf. Der Grund war nicht das Projekt, sondern der Cache:

    AppData\Local\UnrealEngine\Common\Zen\Data            152,6 GB
    AppData\Local\UnrealEngine\Common\DerivedDataCache     97,1 GB

Beides sind DERIVATE. Sie entstehen beim Bauen und Cooken neu und wachsen
ueber die Zeit unbegrenzt - Zen raeumt erst nach 14 Tagen Zugriffsalter auf.
Wer das erst merkt, wenn ein Lauf mit "nicht genuegend Platz" abbricht, hat
den Lauf verloren. Dieser Waechter sagt es vorher.

DIE WICHTIGSTE EIGENSCHAFT: ER KENNT DEN UNTERSCHIED ZWISCHEN "ALT" UND
"UNBENUTZT". Am selben Tag wurde `WiesbadenReal\Saved\_aaa_source` (627 MB,
Ordner vom 03.09.) fast geloescht - es sind aber die CC0-Materialsatzdateien
von ambientCG, aus denen `aaa_import_materials.py` seine Texturen zieht und
auf die `roof_variation.py` direkt zugreift. Ein Aufrumwerkzeug, das nur nach
Datum sortiert, loescht Eingangsdaten. Darum traegt jeder Pfad eine KLASSE:

    cache      _loeschbar_     abgeschlossene Reste und Kopien
    ausgabe    _regenerierbar_ entsteht beim naechsten Build/Cook neu
    eingabe    NIEMALS         wird von einem Werkzeug gelesen
    geschuetzt NIEMALS         Projektinhalt, Fremddateien, Systemdateien

Nur `cache` wird bei `--reinigen` angefasst. `eingabe` und `geschuetzt`
erscheinen im Bericht ausdruecklich als "nicht anfassen" - ein Waechter, der
das versteckt, taugt nichts.

DIE MESSUNG DARF NICHT LUEGEN. Eine Messung, die ihr Budget ueberschreitet,
bricht ab und sagt es: jedes Ergebnis traegt `vollstaendig`, und der Bericht
schreibt "abgeschnitten" dazu. Eine Zahl, die zu niedrig aussieht, weil die
Messung halb lief, ist schlimmer als gar keine.

Exit-Codes:
    0  genug Platz, oder Bericht auf --immer/--reinigen
    3  Platz UNTER der Grenze - gemeldet, nichts geloescht
"""
import argparse
import json
import os
import shutil
import sys
import time
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Ab hier an ist die Platte voll genug fuer einen Abbruch mitten im Lauf.
GRENZE_PROZENT = 20.0

# Wie lange die Messung insgesamt laufen darf. Die Kandidatenliste ist endlich
# und klein, enthaelt aber Ordner mit 200 000 Dateien - eine harte Grenze ist
# ehrlicher als ein 40-Sekunden-Haenger im Commit-Hook.
BUDGET_SEKUNDEN = 20.0

# Parallelitaet. Reine I/O-Arbeit, der Geraete-teil kommt frei - 8 Wege waren
# auf diesem Rechner der Unterschied zwischen 4 und 18 Sekunden.
JOBS = 8

CACHE_AGE_MINUTEN = 30.0

LOESCHBAR = "cache"
REGENERIERBAR = "ausgabe"
EINGABE = "eingabe"
GESCHUETZT = "geschuetzt"

# Nur diese Klasse darf --reinigen anfassen.
NUR_LOESCHEN = (LOESCHBAR,)

# (Pfad, Klasse, Grund). `{w}` = Projektwurzel, `{home}` = Benutzer.
#
# DER GRUND STEHT BEI JEDEM EINTRAG. Eine Pfadliste ohne Begruendung waechst,
# bis sie das Falsche loescht: beim naechsten Zweifel weiss niemand mehr, warum
# "Saved\Cooked" unbedenklich war und "Saved\_aaa_source" nicht.
KANDIDATEN = [
    # --- Cache: loeschbar, weil es abgeleitete Kopien sind ------------------
    (r"{home}\AppData\Local\UnrealEngine\Common\Zen\Data", LOESCHBAR,
     "Zen-Local-DDC, rechnet beim naechsten Cook neu (Zen-GC erst nach 14 Tagen)"),
    (r"{home}\AppData\Local\UnrealEngine\Common\DerivedDataCache\Buckets", LOESCHBAR,
     "Shader-/Chaos-/Mesh-Ableitungen, rechnen sich beim Editorstart neu"),
    (r"{home}\AppData\Local\npm-cache", LOESCHBAR, "npm-Downloadcache"),
    (r"{home}\AppData\Local\pip\Cache", LOESCHBAR, "pip-Downloadcache"),
    (r"{home}\AppData\Local\CrashDumps", LOESCHBAR, "abgestuerzte Prozesse"),
    (r"{home}\AppData\Local\D3DSCache", LOESCHBAR, "DirectX-Shadercache"),
    (r"{home}\.bun\install", LOESCHBAR, "bun-Paketcache"),
    (r"{home}\.gradle\caches", LOESCHBAR, "Gradle-Abhaengigkeitscache"),

    # --- Ausgabe des Projekts: regenerierbar, aber teuer --------------------
    (r"{w}\Saved\StagedBuilds", REGENERIERBAR,
     "Staging-Kopie; Saved\\Package bleibt als Release erhalten"),
    (r"{w}\Saved\Cooked", REGENERIERBAR, "Cook-Ergebnis, jederzeit neu kochbar"),
    (r"{w}\Intermediate\Build", REGENERIERBAR,
     "Objektdateien, naechster Build wird zum Vollbuild"),
    (r"{w}\.gate-worktree\WiesbadenReal\Intermediate", REGENERIERBAR,
     "Gate-Worktree-Objektdateien, baut der naechste Gate-Lauf neu"),

    # --- EINGANGSDATEN: von Werkzeugen gelesen, NIEMALS Muell --------------
    (r"{w}\Saved\_aaa_source", EINGABE,
     "CC0-Saetze von ambientCG - Quelle fuer aaa_import_materials.py und roof_variation.py"),
    (r"{w}\Data\Raw", EINGABE, "Rohdaten (OSM, ALKIS, Materialien) fuer die Bakes"),
    (r"{w}\Saved\Diagnose", EINGABE, "Belege der Gates (Schnittbilder, Ankerprotokolle)"),

    # --- Geschuetzt: Projektinhalt, Fremdabteilung, System ------------------
    (r"{w}\Content", GESCHUETZT, "Projektinhalt"),
    (r"{w}\Content\__ExternalActors__", GESCHUETZT,
     "gebackene Chunks der Karte, ~2000 Pakete - enthaelt in der Zeile darueber"),
    (r"{w}\Saved\Package", GESCHUETZT, "das ausgelieferte Release"),
    (r"{w}\Quellen", GESCHUETZT, "Eingangsquellen des Projekts"),
    (r"{w}\.gate-worktree\.wt-gate5", GESCHUETZT, "Worktree eines anderen Threads"),
    (r"{home}\Downloads", GESCHUETZT, "Nutzerdateien"),
    (r"{home}\.lmstudio", GESCHUETZT, "KI-Modelle des Nutzers"),
    (r"C:\Games", GESCHUETZT, "installierte Spiele"),
    (r"C:\pagefile.sys", GESCHUETZT, "Windows-Auslagerungsdatei"),
    (r"C:\hiberfil.sys", GESCHUETZT, "Ruhezustand, nur powercfg darf das anfassen"),
]

# `%TEMP%` steht hier NICHT in der Liste, und das ist Absicht: der Ordner
# enthaelt neben abgeschlossenen Installerresten auch die Arbeitsdaten laufender
# Werkzeuge (am 27.09.2026: claude, opencode, Freebuff). Ein ganzer Ordner
# laesst sich nicht als "alt" oder "Muell" einstufen - nur einzelne Eintraege
# darin, und das entscheidet der Mensch. Ein Pfad, den man nicht
# verantwortungsvoll einstufen kann, gehoert nicht in eine automatische Liste.


def _muster_aufloesen(pfad, wurzel=None, home=None):
    """Die `{w}`/`{home}`-Platzhalter eines Kandidaten aufloesen."""
    return pfad.format(w=wurzel or WURZEL, home=home or os.path.expanduser("~"))


def platz(pfad):
    """(frei, gesamt, prozent) fuer das Dateisystem von `pfad`."""
    b = shutil.disk_usage(pfad)
    prozent = (b.free / b.total * 100.0) if b.total else 0.0
    return b.free, b.total, prozent


def unter_schwelle(prozent, grenze=GRENZE_PROZENT):
    """Unter der Grenze? Bewusst `<`, nicht `<=`: 20,0 % frei sind kein Notfall."""
    return prozent < grenze


def groesse_messen(pfad, deadline=None):
    """Summe der Dateigroessen unter `pfad` -> (bytes, dateien, vollstaendig).

    `vollstaendig` ist False, wenn die `deadline` (ein time.monotonic()-Wert)
    waehrend des Laufs abgelaufen ist oder ein Ordner nicht lesbar war. Dann
    ist die Summe eine UNTERGRENZE, und der Bericht muss das dazusagen.
    """
    if os.path.isfile(pfad):
        try:
            return os.path.getsize(pfad), 1, True
        except OSError:
            return 0, 0, False

    gesamt = 0
    anzahl = 0
    vollstaendig = True
    stapel = [pfad]
    while stapel:
        if deadline is not None and time.monotonic() > deadline:
            vollstaendig = False
            break
        ordner = stapel.pop()
        try:
            with os.scandir(ordner) as eintraege:
                for e in eintraege:
                    try:
                        if e.is_dir(follow_symlinks=False):
                            stapel.append(e.path)
                        elif e.is_file(follow_symlinks=False):
                            gesamt += e.stat(follow_symlinks=False).st_size
                            anzahl += 1
                    except OSError:
                        vollstaendig = False
        except OSError:
            vollstaendig = False
    return gesamt, anzahl, vollstaendig


def _kandidat_messen(eintrag, deadline):
    pfad, klasse, grund = eintrag
    bytes_, dateien, vollstaendig = groesse_messen(pfad, deadline)
    return {"pfad": pfad, "klasse": klasse, "grund": grund,
            "bytes": bytes_, "dateien": dateien, "vollstaendig": vollstaendig}


def messen(kandidaten, budget=BUDGET_SEKUNDEN, jobs=JOBS, jetzt=None):
    """Alle Kandidaten messen - alle mit DERSELBEN Deadline.

    Eine Deadline fuer alle statt einer je Kandidat: sonst summiert sich das
    Budget auf das Vielfache seiner eingetragenen Groesse, und der Aufrufer
    wartet minutenlang auf einen Hook, der Sekunden brauchen soll.
    """
    if not kandidaten:
        return []
    jetzt = jetzt or time.monotonic
    frist = jetzt() + budget
    with ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        return list(pool.map(lambda e: _kandidat_messen(e, frist), kandidaten))


def kandidaten(wurzel=None, home=None):
    """Die Kandidatenliste als aufgeloeste Pfade, ohne Duplikate."""
    gesehen = set()
    raus = []
    for pfad, klasse, grund in KANDIDATEN:
        aufgeloest = _muster_aufloesen(pfad, wurzel, home)
        if aufgeloest.lower() in gesehen:
            continue
        gesehen.add(aufgeloest.lower())
        raus.append((aufgeloest, klasse, grund))
    return raus


def cache_pfad(wurzel=None):
    return os.path.join(wurzel or WURZEL, "Saved", "Diagnose", "plattenbericht.json")


def cache_laden(wurzel=None, alter=CACHE_AGE_MINUTEN, jetzt=None):
    """Frueheren Messstand laden. `None`, wenn keiner da oder zu alt.

    WARUEBERHAUPT EIN CACHE: Die Messung ueber die grossen Cache-Ordner dauert
    zweistellige Sekunden. Ein Hook, der das vor jedem Commit tut, wird mit
    --no-verify umgangen - und dann prueft er gar nichts mehr.
    """
    try:
        with open(cache_pfad(wurzel), "r", encoding="utf-8") as f:
            roh = json.load(f)
    except (OSError, ValueError):
        return None
    jetzt = jetzt or time.time
    if jetzt() - roh.get("zeit", 0) > alter * 60.0:
        return None
    # Auch beim LESEN wird die Regel geprueft. GEMESSEN am 27.09.2026: eine
    # Cache-Datei, die eine noch aeltere Fassung mit abgeschnittener Messung
    # geschrieben hatte, wurde weiterverwendet und gab 14,35 GB statt 37,90 GB
    # fuer Downloads aus - mit derselben Sorgfalt angezeigt wie eine vollstaendige
    # Zahl. Ein Cache, der eine Regel nicht kennt, ist kein Beweis fuer die
    # Einhaltung dieser Regel.
    if not messung_vollstaendig(roh.get("messungen")):
        return None
    return roh


def messung_vollstaendig(messungen):
    """Sind ALLE Ordner vollstaendig gemessen?

    NUR vollstaendige Messungen duerfen in den Cache. Sonst erbt der naechste
    Aufruf eine abgeschnittene Summe und gibt sie als Bestand aus - der
    Waechter meldet dann "weniger belegt" als wirklich belegt ist, und zwar
    mit dem Glauben einer exakten Zahl. Genau das ist die Sorte Fehler, die
    ein Cache verbreiten soll, statt sie zu verhindern.
    """
    return bool(messungen) and all(m.get("vollstaendig") for m in messungen)


def cache_sichern(daten, wurzel=None):
    """Messstand ablegen. Ein fehlgeschlagener Cache ist kein Fehler - nur
    beim naechsten Mal langsamer.

    UNVOLLSTAENDIGE MESSUNGEN WERDEN HIER VERWORFEN, an beiden Aufrufern
    nicht: die Regel sitzt in der einen Funktion, die sie durchsetzen muss.
    Ein Aufrufer, der sie vergisst, verkauft dem naechsten Aufruf eine
    abgeschnittene Summe als Bestand.
    """
    messungen = daten.get("messungen")
    # `if messungen and ...` waere eine Luecke: die leere Liste ist falsch und
    # wuerde durchrutschen - und "nichts gemessen" ist kein Bestand von null.
    if not messung_vollstaendig(messungen):
        return None
    pfad = cache_pfad(wurzel)
    try:
        os.makedirs(os.path.dirname(pfad), exist_ok=True)
        with open(pfad, "w", encoding="utf-8") as f:
            json.dump(daten, f, indent=1, ensure_ascii=False)
    except OSError:
        return None
    return pfad


def gb(bytes_):
    return bytes_ / (1024.0 ** 3)


def formatiere(bytes_):
    if bytes_ >= 1024 ** 3:
        return "%8.2f GB" % gb(bytes_)
    return "%8.1f MB" % (bytes_ / (1024.0 ** 2))


def _zeilen_block(zeilen, messungen, klasse, titel, hinweis=None):
    eintraege = sorted((m for m in messungen if m["klasse"] == klasse),
                       key=lambda m: -m["bytes"])
    eintraege = [m for m in eintraege if m["bytes"] > 0]
    if not eintraege:
        return
    zeilen.append(titel)
    if hinweis:
        zeilen.append("    " + hinweis)
    for m in eintraege:
        marke = "" if m["vollstaendig"] else "   <- Messung abgeschnitten"
        zeilen.append("  %s  %-9s %s%s" % (formatiere(m["bytes"]), m["klasse"],
                                           m["pfad"], marke))
        zeilen.append("      %s" % m["grund"])
    zeilen.append("")


def bericht(frei, gesamt, prozent, messungen, grenze=GRENZE_PROZENT, laufzeit=0.0):
    """Der Bericht. Reihenfolge: Lage, Fresser, dann die ausdruecklichen
    Nicht-Anfass-Gruppen - die stehen mit im Bild, weil ein Besetzer sonst
    genau die weglisst."""
    zeilen = ["Plattenwaechter: %.1f GB frei von %.1f GB (%.1f %%) - %s (%.0f %%)"
              % (gb(frei), gb(gesamt), prozent,
                 "UNTER der Grenze" if unter_schwelle(prozent, grenze) else "ueber der Grenze",
                 grenze), ""]

    vorhanden = [m for m in messungen if m["bytes"] > 0]
    top = sorted(vorhanden, key=lambda m: -m["bytes"])[:12]
    if top:
        zeilen.append("Groesste Speicherfresser:")
        for m in top:
            marke = "" if m["vollstaendig"] else "   <- Messung abgeschnitten"
            zeilen.append("  %s  %-9s %s%s" % (formatiere(m["bytes"]), m["klasse"],
                                               m["pfad"], marke))
        zeilen.append("")

    _zeilen_block(zeilen, messungen, EINGABE, "Nicht anfassen - Eingangsdaten:",
                  "Wird von einem Werkzeug gelesen; loeschen heisst, es neu holen.")
    _zeilen_block(zeilen, messungen, GESCHUETZT, "Nicht anfassen - geschuetzt:",
                  "Projektinhalt, Fremddateien, Systemdateien.")

    abgeschnitten = [m for m in messungen if not m["vollstaendig"] and m["bytes"] > 0]
    if abgeschnitten:
        zeilen.append("HINWEIS: %d Ordner waren nicht vollstaendig messbar (Budget %.0f s) -"
                      % (len(abgeschnitten), laufzeit))
        zeilen.append("        ihre Zahl ist eine Untergrenze, nicht der Bestand.")

    loeschbar = sum(m["bytes"] for m in messungen if m["klasse"] in NUR_LOESCHEN)
    if loeschbar:
        zeilen.append("")
        zeilen.append("Sicher loeschbar: %.1f GB  "
                      "(python Tools/platten_waechter.py --reinigen --trocken)"
                      % gb(loeschbar))
    return "\n".join(zeilen)


def warnung(wurzel=None, grenze=GRENZE_PROZENT, budget=BUDGET_SEKUNDEN,
            jetzt=None, cache_alter=CACHE_AGE_MINUTEN):
    """Kurzantwort fuer einen Hook: `None` heisst gesund, sonst der Bericht.

    Im Normalfall bezahlt der Aufrufer NICHTS: der Plattenplatz kommt aus
    einem Syscall, und erst unter der Grenze wird ueberhaupt gemessen.
    """
    wurzel = wurzel or WURZEL
    try:
        frei, gesamt, prozent = platz(wurzel)
    except OSError as e:
        return "Plattenwaechter: Plattenplatz nicht lesbar (%s)" % e
    if not unter_schwelle(prozent, grenze):
        return None

    roh = cache_laden(wurzel, cache_alter, jetzt)
    if roh and roh.get("messungen"):
        return bericht(frei, gesamt, prozent, roh["messungen"], grenze,
                       roh.get("laufzeit", 0.0))

    start = time.monotonic()
    messungen = messen(kandidaten(wurzel), budget)
    laufzeit = time.monotonic() - start
    cache_sichern({"zeit": time.time(), "laufzeit": laufzeit, "messungen": messungen},
                  wurzel)
    return bericht(frei, gesamt, prozent, messungen, grenze, laufzeit)


def erlaubte_wurzeln():
    """Wo --reinigen ueberhaupt loeschen darf."""
    wurzeln = [WURZEL, os.path.expanduser("~"),
               os.environ.get("TEMP", ""), os.environ.get("TMP", "")]
    return tuple(os.path.normpath(w).lower() for w in wurzeln if w)


def protokoll_pfad(wurzel=None):
    """Wohin das Loeschprotokoll geschrieben wird.

    JSONL, eine Zeile je Eingriff, angehaengt und nie ueberschrieben. Eine
    Reportdatei, die jeder Lauf neu schreibt, verliert genau die Historie,
    die man braucht: "wann hat dieser Wächter eigentlich geloescht".

    `wurzel` ist nicht nur Kosmetik: die Testsuite rechnet mit TEMP-Wurzeln und
    darf dort nichts im echten Projekt anruehren (tempfile_tmp). Ohne diesen
    Parameter schrieben die Tests in das ECHTE loeschprotokoll.jsonl des
    Projekts - GEMESSEN am 27.09.2026, drei Testeintraege mit tmp-Pfaden
    standen nach einem Lauf in der echten Datei. Ein Protokoll, in dem Tests
    stehen, beweist nichts ueber die Waechter.
    """
    return os.path.join(wurzel or WURZEL, "Saved", "Diagnose", "loeschprotokoll.jsonl")


def protokoll_schreiben(zeilen, pfad=None):
    """Zeilen an das Protokoll anhaengen. `False`, wenn es nicht gelingt.

    DAS RUECKGABEVERZEICHNIS IST DER GANZE SCHUTZ: `reinigen` loescht nur, wenn
    diese Funktion True liefert. Ein Loeschen ohne Protokoll waere genau die
    Sorte Eingriff, die man spaeter nicht mehr erklaeren kann.
    """
    if not zeilen:
        return True
    pfad = pfad or protokoll_pfad()
    try:
        ordner = os.path.dirname(pfad)
        if ordner and not os.path.isdir(ordner):
            os.makedirs(ordner, exist_ok=True)
        with open(pfad, "a", encoding="utf-8") as f:
            for zeile in zeilen:
                f.write(json.dumps(zeile, ensure_ascii=False, sort_keys=True) + "\n")
            f.flush()
            os.fsync(f.fileno())
        return True
    except OSError:
        return False


def _protokollzeile(m, ziel, phase, trocken, bytes_vorher=None):
    """Ein Protokolleintrag: wer, warum, wie viel, in welcher Phase."""
    return {
        "zeit": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "phase": phase,
        "trocken": bool(trocken),
        "pfad": ziel,
        "klasse": m.get("klasse", ""),
        "begruendung": m.get("grund", ""),
        "bytes": int(bytes_vorher or 0),
        "gib": gb(int(bytes_vorher or 0)),
        "dateien": m.get("dateien"),
        "messung_vollstaendig": bool(m.get("vollstaendig")),
    }


def loeschreport(messungen, geloescht, trocken=False):
    """Der lesbare Report: je Loeschpfad Begruendung und freigewordene Groesse.

    Der Grund kommt aus der Kandidatenliste - dieselbe Zeile, nach der auch
    entschieden wurde, dass der Pfad loeschbar ist. Eine Begruendung, die
    nur im Code steht und nicht im Report, hilft niemandem, wenn es drei
    Monate spaeter darum geht, warum ein Ordner fehlt.
    """
    bygueltig = {os.path.normpath(p).lower(): p for p in geloescht}
    if not bygueltig:
        return "Loeschreport: nichts %s." % ("zum Loeschen vorgemerkt" if trocken
                                             else "geloescht")
    zeilen = []
    summe = 0
    for m in messungen:
        ziel = os.path.normpath(m["pfad"]).lower()
        if ziel not in bygueltig:
            continue
        groesse = m.get("bytes") or 0
        summe += groesse
        zeilen.append("    %8.2f GiB  %s" % (gb(groesse), m["pfad"]))
        grund = (m.get("grund") or "").strip() or "(keine Begruendung hinterlegt)"
        zeilen.append("               Grund: %s" % grund)
        if not m.get("vollstaendig", True):
            zeilen.append("               ACHTUNG: Groesse unvollstaendig gemessen")
    kopf = ("KOENNTE LOESCHEN (Trockenlauf):" if trocken else "GELOESCHT:")
    return "\n".join([kopf] + zeilen + ["    zusammen: %.2f GiB" % gb(summe)])


def reinigen(messungen, trocken=False, protokoll=None, jetzt=None, wurzel=None):
    """Nur die Klassen aus `NUR_LOESCHEN` loeschen. Alles andere bleibt.

    Der Schutz ist DREIFACH (27.09.2026, war vorher doppelt):
      1. die Klasse - nur `cache` wird angefasst;
      2. die Pfadnormalisierung - nichts ausserhalb von Projektwurzel,
         Benutzerordner oder %TEMP%;
      3. das LOESCHPROTOKOLL - die Absicht wird VOR dem Eingriff geschrieben
         (Phase "absicht"), das Ergebnis danach (Phase "ergebnis" oder
         "trocken"). Laesst sich das Protokoll nicht schreiben, wird
         garnichts geloescht.
    Punkt 3 ist nicht Kosmetik: ein Wächter, der eine Platte leert, ohne zu
    sagen welchen Ordner er warum genommen hat, ist hinterher nicht mehr von
    einem Fehlgriff zu unterscheiden.

    `ignore_errors=True` heisst, dass rmtree Fehler SCHLUCKT. Deshalb wird
    das Ergebnis nicht aus dem Rueckgabewert abgeleitet, sondern danach
    geprueft, ob der Ordner wirklich weg ist. Sonst stuende im Protokoll
    "geloescht", und der Ordner laege noch da.

    `protokoll` ist ein callable (zeilen, pfad) -> bool und ueberschreibt
    das Schreiben - so testen die Faelle "nicht schreibbar" und "trocken",
    ohne das echte Dateisystem zu verbiegen. `wurzel` verschiebt die
    Protokolldatei mit (siehe protokoll_pfad).
    """
    erlaubt = erlaubte_wurzeln()
    ziel_pfad = protokoll_pfad(wurzel)
    if protokoll is not None:
        pfad_schreiben = protokoll
    else:
        pfad_schreiben = lambda zeilen: protokoll_schreiben(zeilen, ziel_pfad)
    geloescht = []
    abgewiesen = []
    ohne_protokoll = []

    absichten = []
    for m in messungen:
        if m["klasse"] not in NUR_LOESCHEN:
            continue
        ziel = os.path.normpath(m["pfad"])
        if not ziel.lower().startswith(erlaubt):
            abgewiesen.append(m["pfad"])
            continue
        if not os.path.isdir(ziel):
            continue
        if trocken:
            # Der Trockenlauf meldet nur, was er tun WOERDE - die Groesse aus
            # der Messung reicht, es wird nichts angefasst.
            absichten.append((m, ziel, m.get("bytes") or 0))
            continue
        # GEMESSEN 27.09.2026: die Groesse aus der Kandidatenliste ist eine
        # Momentaufnahme von vorher. Fuer das Protokoll wird direkt VOR dem
        # Eingriff noch einmal gemessen - nur so steht drin, was wirklich
        # weggegangen ist.
        try:
            bytes_vorher = groesse_messen(ziel)[0]
        except OSError:
            bytes_vorher = m.get("bytes") or 0
        absichten.append((m, ziel, bytes_vorher))

    if trocken:
        zeilen = [_protokollzeile(m, z, "trocken", True, b)
                  for m, z, b in absichten]
        if zeilen and not pfad_schreiben(zeilen):
            return [], list(abgewiesen) + [z for _m, z, _b in absichten]
        return [z for _m, z, _b in absichten], abgewiesen

    if absichten and not pfad_schreiben(
            [_protokollzeile(m, z, "absicht", False, b) for m, z, b in absichten]):
        # Der Schutz, der alles andere ueberwacht, hat versagt. Also: nichts
        # loeschen, und der Aufrufer erfaehrt es.
        return [], list(abgewiesen) + [z for _m, z, _b in absichten]

    for m, ziel, bytes_vorher in absichten:
        shutil.rmtree(ziel, ignore_errors=True)
        weg = not os.path.exists(ziel)
        eintrag = _protokollzeile(m, ziel, "ergebnis" if weg else "unvollstaendig",
                                  False, bytes_vorher)
        eintrag["weg"] = weg
        pfad_schreiben([eintrag])
        if not weg:
            # rmtree hat geschluckt, der Ordner liegt noch. Das gehoert
            # gemeldet, nicht verschwiegen.
            ohne_protokoll.append(ziel)
            continue
        geloescht.append(ziel)

    return geloescht, abgewiesen


def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(
        description="Plattenbelegung melden und die groessten Speicherfresser zeigen.")
    p.add_argument("--schwelle", type=float, default=GRENZE_PROZENT,
                   help="Grenze in Prozent frei (Vorgabe: %s)" % GRENZE_PROZENT)
    p.add_argument("--immer", action="store_true",
                   help="immer berichten, auch wenn genug Platz ist")
    p.add_argument("--reinigen", action="store_true",
                   help="die als 'cache' eingestuften Ordner loeschen")
    p.add_argument("--trocken", action="store_true",
                   help="mit --reinigen: nur zeigen, nicht loeschen")
    p.add_argument("--budget", type=float, default=BUDGET_SEKUNDEN,
                   help="Sekunden fuer die gesamte Messung")
    p.add_argument("--kein-cache", action="store_true",
                   help="gespeicherten Messstand nicht wiederverwenden")
    args = p.parse_args(argv)

    berichten = args.immer or args.reinigen
    frei, gesamt, prozent = platz(WURZEL)
    knapp = unter_schwelle(prozent, args.schwelle)

    if not knapp and not berichten:
        print("Plattenwaechter: %.1f GB frei von %.1f GB (%.1f %%) - ueber der Grenze (%.0f %%)"
              % (gb(frei), gb(gesamt), prozent, args.schwelle))
        return 0

    start = time.monotonic()
    roh = None if args.kein_cache else cache_laden(WURZEL)
    if roh and roh.get("messungen"):
        messungen, laufzeit = roh["messungen"], roh.get("laufzeit", 0.0)
    else:
        messungen = messen(kandidaten(), args.budget)
        laufzeit = time.monotonic() - start
        cache_sichern({"zeit": time.time(), "laufzeit": laufzeit,
                       "messungen": messungen})

    print(bericht(frei, gesamt, prozent, messungen, args.schwelle, laufzeit))

    if args.reinigen:
        geloescht, abgewiesen = reinigen(messungen, trocken=args.trocken,
                                          wurzel=WURZEL)
        print("")
        if geloescht:
            print(("KOENNTE LOESCHEN: " if args.trocken else "GELOESCHT: ")
                  + ", ".join(geloescht))
        if abgewiesen:
            print("abgewiesen (ausserhalb der erlaubten Wurzeln "
                  "ODER ohne schreibbares Protokoll): " + ", ".join(abgewiesen))
        print(loeschreport(messungen, geloescht, args.trocken))
        print("Protokoll: " + protokoll_pfad())
        # Absichtlich kein Rueckgabecode 3: wer ausdruecklich --reinigen
        # gesagt hat, hat die Platznot ja schon zur Kenntnis genommen.

    if knapp and not berichten:
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
