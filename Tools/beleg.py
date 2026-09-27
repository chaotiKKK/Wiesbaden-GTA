# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Geteilte Helfer fuer BELEGdateien: Dateien, aus denen spaeter ein Messwert
# oder ein Gate-Ergebnis gelesen wird. Das Python-Pendant zu
# Tools/beleg.ps1 - dieselbe Regel, dieselbe Begründung.
#
#   loesche_beleg(LOG)                  # loeschen und NACHPRUEFEN, sonst Fehler
#   if not ist_frisch(LOG, start): ...  # ist das wirklich dieser Lauf?
#
# DIE FEHLERKLASSE, die hier behoben wird - dreimal still gruenes Gate:
#
#   if os.path.exists(LOG):
#       try:
#           os.remove(LOG)
#       except OSError:
#           pass
#   ... Minuten spaeter wird genau diese Datei geparst und behauptet, sie
#   enthalte das Ergebnis des aktuellen Laufs.
#
# "except: pass" ist das "SilentlyContinue" in Python. Zwei Loeschfehler
# sind im Projekt nachgemessen (27.09.2026): das Read-only-Flag am
# Ordner-Eintrag und der offene Handle eines haengenden Editors (hier ist
# die Datei die LEBENDE Logdatei, die ein laufender Editor haelt). Bleibt die
# Datei liegen, meldet die Auswertung deren ZAHLEN als Ergebnis des neuen
# Laufs - ein Gate, das nichts gemessen hat, sieht aus wie ein bestandenes.
#
# Regel: DEM WEGSEIN WIRD NICHT GLAUBT, ES WIRD GEPRUEFT. Und wo ein Messwert
# aus einer Datei kommt, zaehlt nicht nur ihr Inhalt, sondern auch, WANN sie
# geschrieben wurde.
#
# __all__ = ["BelegFehler", "loesche_beleg", "ist_frisch", "beleg_hinweis"]

import datetime
import os
import shutil
import stat

# Dateizeitstempel sind je nach Dateisystem nur sekundengenau. Zwei Sekunden
# Toleranz reichen, damit ein Beleg dieses Laufs nicht als "alt" gilt; mehr
# waere ein Schlupfloch, denn eine alte Log ist Minuten alt.
TOLERANZ_S = 2.0


class BelegFehler(RuntimeError):
    """Ein Beleg liess sich nicht loeschen - die Auswertung waere die des
    letzten Laufs. Aufrufer sollen den Lauf abbrechen, nicht weitergehen."""


def _weg(pfad):
    return not os.path.exists(pfad)


def loesche_beleg(pfad, ordner=False):
    """Loescht eine Belegdatei und prueft danach NACH.

    Gibt True zurueck, wenn der Pfad danach weg ist (oder schon vorher weg
    war). Wirft BelegFehler, wenn er liegen bleibt: dann ist die spaetere
    Auswertung nicht die dieses Laufs, und der Aufrufer muss abbrechen.

    `ordner=True` loescht rekursiv (fuer Rollback-Ziele in der Release).
    """
    if not pfad:
        return True
    if _weg(pfad):
        return True

    try:
        if ordner:
            shutil.rmtree(pfad)
        else:
            os.remove(pfad)
    except OSError:
        pass

    if not _weg(pfad):
        # Read-only steckt am Ordner-Eintrag, nicht am Inhalt. Das ist einer
        # der beiden nachgemessenen Faelle - und der billigere von beiden.
        try:
            os.chmod(pfad, stat.S_IWRITE)
        except OSError:
            pass
        try:
            if ordner:
                shutil.rmtree(pfad)
            else:
                os.remove(pfad)
        except OSError:
            pass

    if not _weg(pfad):
        raise BelegFehler(
            "der Beleg %s laesst sich nicht loeschen - er ist entweder "
            "schreibgeschuetzt oder von einem haengenden Prozess offen "
            "gehalten (dann blockiert auch das Log-Setzen darauf). Pfad: %s"
            % (os.path.basename(pfad.rstrip("\\/")) or pfad, pfad))
    return True


def ist_frisch(pfad, ab):
    """Ist der Beleg ein Ergebnis DIESES Laufs?

    `ab` ist ein Zeitstempel oder time.time()-Wert, gemessen VOR dem Start der
    Sitzung. Datei existieren und nach `ab` (minus Toleranz) geschrieben
    worden sein - mehr nicht. Fehlt die Datei, ist sie nicht frisch: das ist
    der Fall, in dem ein Lauf gar nichts geschrieben hat.
    """
    if not pfad or _weg(pfad):
        return False
    try:
        geschrieben = os.path.getmtime(pfad)
    except OSError:
        return False
    if ab is None:
        return True
    if isinstance(ab, datetime.datetime):
        grenze = ab.timestamp()
    else:
        grenze = float(ab)
    return geschrieben >= grenze - TOLERANZ_S


def beleg_hinweis(pfad):
    """Text fuer die Fehlermeldung: WANN wurde der Beleg geschrieben?"""
    if not pfad or _weg(pfad):
        return "Der Beleg fehlt ganz - der Lauf hat ihn nie geschrieben."
    try:
        zeit = datetime.datetime.fromtimestamp(os.path.getmtime(pfad))
    except OSError:
        return "Der Beleg ist nicht lesbar."
    return ("Der Beleg ist vom %s - er ist AELTER als dieser Lauf. "
            "Wahrscheinlich hat der Lauf nicht angefangen, oder ein "
            "haengender Prozess haelt die alte Datei." % zeit.strftime(
                "%Y-%m-%d %H:%M:%S"))
