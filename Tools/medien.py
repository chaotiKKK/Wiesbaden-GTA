# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""Kleine GIFs und Fotos aus dem laufenden Spiel - fuer docs/meilensteine.md
und die Meilenstein-Releases auf GitHub.

    python Tools/medien.py aufnahme --log <abslog> --start-bei "<Text>" --sekunden 12 --ziel x.mp4
    python Tools/medien.py gif x.mp4 x.gif [--von 0 --bis 6 --breite 480 --fps 12]
    python Tools/medien.py foto bild.png bild.jpg [--breite 1280]

AUFNAHME: wartet, bis im Spiel-Log (-abslog) die Zeile mit <Text> erscheint,
holt das Spielfenster nach vorn (fuer die Dauer der Aufnahme "immer oben"),
filmt nur dessen Bildbereich per Desktop-Duplikation (ffmpeg ddagrab - gdigrab
liefert bei D3D-Fenstern oft Schwarz) und gibt das Fenster danach wieder frei.
Die Desktop-Duplikation filmt, was auf dem Bildschirm SICHTBAR ist: ohne
"immer oben" landet ein davor liegendes Fenster im Film.

GIF: zweistufig mit eigener Palette (128 Farben, Bayer-Dithering) - so bleibt
ein 6-s-GIF mit 480 px Breite bei ~1-3 MB. Groesser ist fuer eine
Meilenstein-Seite nicht noetig und laesst das Repo wachsen.

ffmpeg kommt aus imageio-ffmpeg (auf diesem Rechner gibt es kein System-ffmpeg).
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import datetime
import os
import subprocess
import sys
import time

import imageio_ffmpeg

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from beleg import ist_frisch    # "ist das wirklich dieser Lauf?" - nicht das Wegsehen

FFMPEG = imageio_ffmpeg.get_ffmpeg_exe()
user32 = ctypes.windll.user32

# Ohne --ab-seit: wie frisch muss die Log sein, damit sie als laufende
# Sitzung gilt? Eine laufende Sitzung schreibt im Sekundentakt, eine alte
# Log ist Minuten alt. 120 s ist gross genug fuer einen langsamen
# Kartenaufbau und trotzdem weit unter der Frist, in der ein Alterungs-
# fehler auftreten wuerde.
MAX_LOGALTER_S = 120


def _zeitpunkt(text):
    """--ab-seit als ISO-8601 (das, was to_string('o') in PowerShell liefert)."""
    try:
        return datetime.datetime.fromisoformat(text)
    except ValueError:
        raise argparse.ArgumentTypeError(
            'kein ISO-Zeitpunkt (2026-09-27T18:04:11): %r' % (text,))

# Physische Pixel statt skalierter - sonst passt der Ausschnitt nicht zum Bildschirm.
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except (AttributeError, OSError):
    user32.SetProcessDPIAware()

HWND_TOPMOST, HWND_NOTOPMOST = -1, -2
SWP_NOMOVE, SWP_NOSIZE, SWP_SHOWWINDOW = 0x0002, 0x0001, 0x0040


def prozessname(hwnd):
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid.value)   # QUERY_LIMITED_INFORMATION
    if not handle:
        return ''
    try:
        puffer = ctypes.create_unicode_buffer(1024)
        groesse = wt.DWORD(1024)
        ctypes.windll.kernel32.QueryFullProcessImageNameW(handle, 0, puffer, ctypes.byref(groesse))
        return os.path.basename(puffer.value).lower()
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


def spielfenster(titel_teil='WiesbadenReal'):
    """Groesstes sichtbares Fenster eines UnrealEditor-Prozesses, dessen Titel
    titel_teil enthaelt (ein Explorer-Ordner gleichen Namens zaehlt nicht)."""
    gefunden = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def je_fenster(hwnd, _):
        if not user32.IsWindowVisible(hwnd) or prozessname(hwnd) != 'unrealeditor.exe':
            return True
        laenge = user32.GetWindowTextLengthW(hwnd)
        puffer = ctypes.create_unicode_buffer(laenge + 1)
        user32.GetWindowTextW(hwnd, puffer, laenge + 1)
        if titel_teil.lower() in puffer.value.lower():
            r = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(r))
            gefunden.append((r.right * r.bottom, hwnd, puffer.value))
        return True

    user32.EnumWindows(je_fenster, 0)
    return max(gefunden)[1:] if gefunden else (None, None)


def bildbereich(hwnd):
    """(x, y, breite, hoehe) des Client-Bereichs in Bildschirmpixeln, gerade Masse."""
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    p = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(p))
    return p.x, p.y, (r.right - r.left) // 2 * 2, (r.bottom - r.top) // 2 * 2


def liegt_oben(hwnd, x, y, w, h):
    """Gehoeren Mitte und vier Ecken des Bereichs zum Spielfenster (nicht verdeckt)?"""
    user32.WindowFromPoint.restype = wt.HWND
    user32.WindowFromPoint.argtypes = [wt.POINT]
    user32.GetAncestor.restype = wt.HWND
    for px, py in ((x + w // 2, y + h // 2), (x + 4, y + 4), (x + w - 5, y + 4),
                   (x + 4, y + h - 5), (x + w - 5, y + h - 5)):
        oben = user32.WindowFromPoint(wt.POINT(px, py))
        if not oben or user32.GetAncestor(oben, 2) != hwnd:     # GA_ROOT
            return False
    return True


def warte_auf_zeile(log, text, frist, ab=None):
    """Wartet, bis die Zeile in der Log steht - aber nur, wenn die Log auch
    wirklich die LAUFENDE Sitzung ist.

    Ohne diese Pruefung ist die Bedingung nach 0,5 s erfuellt, sobald die
    Startzeile aus dem VORLETZTEN Lauf noch in der Datei steht: der Aufrufer
    loescht die Log mit `Remove-Item ... -ErrorAction SilentlyContinue`, und
    genau das schlaegt still fehl, wenn die Datei schreibgeschuetzt ist oder
    ein haengender Prozess sie offen haelt. Die Aufnahme beginnt dann, bevor
    das Spiel ueberhaupt geladen ist, und filmt einFenster, in dem es nichts
    zu sehen gibt.

    `ab` ist der Zeitpunkt, zu dem die Sitzung gestartet wurde, wenn der
    Aufrufer ihn kennt (--ab-seit, ISO-Format). Ohne ihn gilt die
    Naeherung: die Log muss in den letzten MAX_LOGALTER_S Sekunden
    geschrieben worden sein - eine laufende Sitzung schreibt im Sekundentakt,
    eine alte Log ist Minuten alt.
    """
    ende = time.time() + frist
    grenze = ab if ab is not None else time.time() - MAX_LOGALTER_S
    while time.time() < ende:
        if os.path.isfile(log) and ist_frisch(log, grenze):
            with open(log, encoding='utf-8', errors='replace') as f:
                if text in f.read():
                    return True
        time.sleep(0.5)
    return False


def aufnahme(a):
    ab = a.ab_seit.timestamp() if a.ab_seit else None
    if a.log and not warte_auf_zeile(a.log, a.start_bei, a.frist, ab):
        sys.exit('Startzeile "%s" kam nicht in %d s (%s).%s' % (
            a.start_bei, a.frist, a.log,
            '' if ab is None else '  Hinweis: --ab-seit ist aelter als die Log '
                                 '(siehe Log-Zeitstempel).'))
    time.sleep(a.verzoegerung)
    hwnd, titel = spielfenster(a.titel)
    if not hwnd:
        sys.exit('Kein Fenster mit "%s" im Titel.' % a.titel)
    x, y, w, h = bildbereich(hwnd)
    user32.ShowWindow(hwnd, 9)                       # SW_RESTORE
    user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW)
    user32.SetForegroundWindow(hwnd)
    for _ in range(25):
        if liegt_oben(hwnd, x, y, w, h):
            break
        time.sleep(0.2)
    else:
        user32.SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)
        sys.exit('Spielfenster bleibt verdeckt - keine Aufnahme.')
    # Startzeit in UTC wie die Zeitstempel im Unreal-Log - daraus lassen sich
    # die GIF-Ausschnitte aus den Log-Zeilen der Szene schneiden.
    print('Aufnahmebeginn UTC %s' % time.strftime('%Y.%m.%d-%H.%M.%S', time.gmtime()) + ('%.3f' % (time.time() % 1))[1:])
    sys.stdout.flush()
    # WACHE: Die Desktop-Duplikation filmt den BILDSCHIRMBEREICH, nicht das
    # Fenster. Endet das Spiel vor der Aufnahme (am 26.09.2026 beendete sich
    # ein -WbShotWhenReady-Lauf nach seinem Bild), filmt sie, was dahinter
    # liegt - private Fenster des Nutzers. Darum: Fenster weg, verdeckt oder
    # verschoben -> sofort anhalten und die letzte Sekunde verwerfen.
    roh = a.ziel + '.roh.mp4'
    abbruch = ''
    start = time.time()
    try:
        quelle = ('ddagrab=output_idx=0:framerate=%d:offset_x=%d:offset_y=%d:video_size=%dx%d,'
                  'hwdownload,format=bgra' % (a.fps, x, y, w, h))
        cmd = [FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i', quelle,
               '-t', str(a.sekunden), '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '18',
               '-pix_fmt', 'yuv420p', roh]
        lauf = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        while lauf.poll() is None:
            time.sleep(0.2)
            if not user32.IsWindow(hwnd) or not user32.IsWindowVisible(hwnd):
                abbruch = 'Spielfenster geschlossen'
            elif not liegt_oben(hwnd, x, y, w, h):
                abbruch = 'Spielfenster verdeckt'
            elif bildbereich(hwnd) != (x, y, w, h):
                abbruch = 'Spielfenster verschoben'
            if abbruch:
                try:
                    lauf.communicate('q', timeout=10)      # ffmpeg sauber beenden
                except (subprocess.TimeoutExpired, OSError, ValueError):
                    lauf.kill()
                break
        fehler = lauf.stderr.read() if lauf.stderr and not lauf.stderr.closed else ''
        lauf.wait()
    finally:
        user32.SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)
    dauer = time.time() - start
    if not os.path.isfile(roh):
        sys.exit('ffmpeg: %s' % (fehler or '').strip()[-400:])
    behalten = a.sekunden if not abbruch else max(0.0, dauer - 1.5)
    if behalten < 1.0:
        os.remove(roh)
        sys.exit('Aufnahme verworfen (%s nach %.1f s).' % (abbruch, dauer))
    subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-i', roh, '-t', '%.2f' % behalten,
                    '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '18', '-pix_fmt', 'yuv420p', a.ziel], check=True)
    os.remove(roh)
    print('Aufnahme %s: %d x %d, %.1f s aus "%s"%s (%.1f MB)' % (
        a.ziel, w, h, behalten, titel, (' - ABBRUCH: %s' % abbruch) if abbruch else '',
        os.path.getsize(a.ziel) / 1e6))


def gif(a):
    zeit = ['-ss', str(a.von)] + (['-to', str(a.bis)] if a.bis else [])
    filt = ('fps=%d,scale=%d:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=%d:stats_mode=diff[p];'
            '[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle' % (a.fps, a.breite, a.farben))
    cmd = [FFMPEG, '-hide_banner', '-loglevel', 'error', '-y'] + zeit + ['-i', a.quelle,
           '-filter_complex', filt, '-loop', '0', a.ziel]
    subprocess.run(cmd, check=True)
    print('GIF %s: %.2f MB' % (a.ziel, os.path.getsize(a.ziel) / 1e6))


def foto(a):
    cmd = [FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-i', a.quelle,
           '-vf', 'scale=%d:-1:flags=lanczos' % a.breite, '-q:v', '4', a.ziel]
    subprocess.run(cmd, check=True)
    print('Foto %s: %.0f KB' % (a.ziel, os.path.getsize(a.ziel) / 1e3))


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    s = p.add_subparsers(dest='cmd', required=True)
    q = s.add_parser('aufnahme')
    q.add_argument('--log', help='Spiel-Log (-abslog), auf dessen Zeile gewartet wird')
    q.add_argument('--ab-seit', type=_zeitpunkt, default=None,
                   help='ISO-Zeitpunkt des Sitzungsstarts. Ohne diese Angabe gilt '
                        'die Log nur dann als die laufende Sitzung, wenn sie in den '
                        'letzten %d s geschrieben wurde.' % MAX_LOGALTER_S)
    q.add_argument('--start-bei', default='')
    q.add_argument('--frist', type=int, default=600, help='so lange auf die Startzeile warten (s)')
    q.add_argument('--verzoegerung', type=float, default=0.0)
    q.add_argument('--sekunden', type=float, default=10.0)
    q.add_argument('--fps', type=int, default=20)
    q.add_argument('--titel', default='WiesbadenReal')
    q.add_argument('--ziel', required=True)
    q.set_defaults(fn=aufnahme)
    g = s.add_parser('gif')
    g.add_argument('quelle')
    g.add_argument('ziel')
    g.add_argument('--von', type=float, default=0.0)
    g.add_argument('--bis', type=float, default=0.0)
    g.add_argument('--breite', type=int, default=480)
    g.add_argument('--fps', type=int, default=12)
    g.add_argument('--farben', type=int, default=128)
    g.set_defaults(fn=gif)
    f = s.add_parser('foto')
    f.add_argument('quelle')
    f.add_argument('ziel')
    f.add_argument('--breite', type=int, default=1280)
    f.set_defaults(fn=foto)
    a = p.parse_args()
    a.fn(a)


if __name__ == '__main__':
    main()
