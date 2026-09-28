# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""Ein fertiges Thema in einem Zug als Meilenstein veroeffentlichen.

    python Tools/meilenstein.py thema.json              # alles
    python Tools/meilenstein.py thema.json --trocken    # nur lokal: Aufnahme, Medien, Seite, Commit-Objekt

Schritte (jeder prueft, ob er schon erledigt ist - ein zweiter Aufruf nach
einem Abbruch macht dort weiter):

1. Aufnahme   -WbClip-Lauf dieses Projekts unter dem Engine-Lock
              (Saved/Clips/meilenstein-<nr>-<stamm>/), Fenster darf verdeckt sein
2. Medien     GIF + Foto aus dem Clip (docs/meilensteine/bilder/<nr>-<stamm>.*)
3. Seite      Tabellenzeile + Abschnitt in docs/meilensteine.md, README-Block
              (Anzahl + die drei neuesten GIFs) - auf dem Stand von origin/main
4. Doku-PR    Commit direkt auf origin/main (ohne Checkout), Push aus dem
              HAUPTordner (dort verlinkt das Push-Gate die Stadt), PR, Squash-Merge
5. Release    meilenstein-<nr>-<stamm> auf den Themen-Commit, Bilder eingebettet
              (blob/main - das Repo ist privat) und angehaengt, "Latest"
6. Schaufenster  oeffentlicher Klon neu erzeugen (schaufenster.py von origin/main),
              committen mit der noreply-Adresse, pushen

Themen-Datei (JSON, unbekannte Schluessel sind ein Fehler - ein Tippfehler
soll nicht still eine Vorgabe ergeben):

    {
      "stamm": "zweiter-hubschrauber",          Tag- und Dateiname, a-z0-9-
      "titel": "Zweiter Hubschrauber",
      "zeitraum": "20.-27.09.2026",
      "stand": "fertig",                        Spalte "Stand" der Tabelle
      "commit": "dac241e",                      Ziel des Releases, muss auf GitHub liegen
      "text": "Absatz eins.\\n\\nAbsatz zwei.",   Markdown des Abschnitts
      "nr": 15,                                 optional, sonst hoechste + 1
      "clip": {
        "goto": "-117159,-118324",              -WbGoto (optional)
        "pose": "3, -117159, -118324, 116, 0, 13, 296, -6",   optional, sonst Spielkamera
        "at": 0, "delay": 5, "sekunden": 8, "fps": 25, "tempo": 1, "uhrzeit": 13,
        "ohne_hud": false, "schalter": "-WbZuFuss=20",
        "gif": {"von": 0, "bis": 6, "breite": 480, "fps": 12, "farben": 96, "crop": ""},
        "gif_alt": "Bildbeschreibung", "foto_bei": 3.0, "foto_alt": "Bildbeschreibung"
      }
    }

Nie ohne Gates: der Push laeuft durch den pre-push-Hook. Ist er rot, bricht
der Befehl ab, bevor irgendetwas gemergt oder veroeffentlicht ist.
"""
import argparse
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

PROJEKT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_URL = 'https://github.com/chaotiKKK/Wiesbaden-GTA'
BLOB = REPO_URL + '/blob/main/docs/'
SEITE_URL = REPO_URL + '/blob/main/docs/meilensteine.md'
SCHAUFENSTER_KLON = r'C:\freebuff\wiesbaden-real-meilensteine'
SEITE = 'docs/meilensteine.md'
BILDER = 'docs/meilensteine/bilder'
GIF_GRENZE_MB = 3.0

PFLICHT = ('stamm', 'titel', 'zeitraum', 'stand', 'commit', 'text', 'clip')
SCHLUESSEL = set(PFLICHT) | {'nr'}
CLIP_VORGABEN = {
    'goto': '', 'pose': '', 'at': 0, 'delay': 5, 'sekunden': 8, 'fps': 25, 'tempo': 1,
    'uhrzeit': 13, 'ohne_hud': False, 'schalter': '', 'gif': {}, 'gif_alt': '',
    'foto_bei': None, 'foto_alt': '',
}
GIF_VORGABEN = {'von': 0.0, 'bis': 0.0, 'breite': 480, 'fps': 12, 'farben': 96, 'crop': ''}


class Abbruch(Exception):
    pass


# ---------------------------------------------------------------------------
# Reine Teile (getestet in test_meilenstein.py)
# ---------------------------------------------------------------------------

def thema_laden(daten):
    """Themen-Dict pruefen und mit Vorgaben auffuellen."""
    fehlt = [k for k in PFLICHT if k not in daten]
    fremd = sorted(set(daten) - SCHLUESSEL)
    if fehlt or fremd:
        raise Abbruch('Themen-Datei: %s%s' % ('fehlt %s' % fehlt if fehlt else '',
                                              ' unbekannt %s' % fremd if fremd else ''))
    if not re.fullmatch(r'[a-z0-9]+(-[a-z0-9]+)*', daten['stamm']):
        raise Abbruch('stamm "%s": nur a-z, 0-9 und Bindestriche' % daten['stamm'])
    clip = dict(daten['clip'])
    fremd = sorted(set(clip) - set(CLIP_VORGABEN))
    if fremd:
        raise Abbruch('Themen-Datei, clip: unbekannt %s' % fremd)
    clip = dict(CLIP_VORGABEN, **clip)
    fremd = sorted(set(clip['gif']) - set(GIF_VORGABEN))
    if fremd:
        raise Abbruch('Themen-Datei, clip.gif: unbekannt %s' % fremd)
    clip['gif'] = dict(GIF_VORGABEN, **clip['gif'])
    if clip['foto_bei'] is None:
        clip['foto_bei'] = clip['sekunden'] / 2.0
    thema = dict(daten, clip=clip)
    thema['text'] = thema['text'].strip()
    return thema


def anker(ueberschrift):
    """GitHub-Anker einer Ueberschrift: klein, Satzzeichen weg, Leerzeichen -> '-'."""
    return re.sub(r'[^\w\- ]', '', ueberschrift.lower()).replace(' ', '-')


def abschnitte(text):
    """{nr: (titel, rumpf)} der Seite; rumpf endet vor dem naechsten '---'."""
    teile = re.split(r'^## (\d+)\. (.+)$', text, flags=re.M)
    return {int(teile[i]): (teile[i + 1].strip(), teile[i + 2].split('\n---')[0].strip())
            for i in range(1, len(teile), 3)}


def naechste_nummer(text):
    return max(abschnitte(text) or {0: None}) + 1


def dateinamen(nr, stamm):
    stamm = '%02d-%s' % (nr, stamm)
    return '%s/%s.gif' % (BILDER, stamm), '%s/%s.jpg' % (BILDER, stamm)


def abschnitt_text(thema, nr):
    gif, jpg = [p[len('docs/'):] for p in dateinamen(nr, thema['stamm'])]
    clip = thema['clip']
    return ('## %d. %s\n\n*%s*\n\n![%s](%s)\n\n%s\n\n![%s](%s)\n' % (
        nr, thema['titel'], thema['zeitraum'], clip['gif_alt'] or thema['titel'], gif,
        thema['text'], clip['foto_alt'] or thema['titel'], jpg))


def seite_einfuegen(text, thema, nr):
    """Tabellenzeile oben in die Uebersicht, Abschnitt vor den bisher neuesten."""
    if nr in abschnitte(text):
        raise Abbruch('Meilenstein %d steht schon auf der Seite.' % nr)
    zeilen = text.split('\n')
    try:
        kopf = next(i for i, z in enumerate(zeilen) if z.startswith('| # |'))
    except StopIteration:
        raise Abbruch('Uebersichtstabelle ("| # | ...") nicht gefunden.')
    titel = '%d. %s' % (nr, thema['titel'])
    zeile = '| %d | [%s](#%s) | %s | %s |' % (nr, thema['titel'], anker(titel), thema['zeitraum'], thema['stand'])
    zeilen.insert(kopf + 2, zeile)
    text = '\n'.join(zeilen)
    m = re.search(r'^## \d+\. ', text, flags=re.M)
    if not m:
        raise Abbruch('Kein Meilenstein-Abschnitt ("## <nr>. ...") auf der Seite.')
    return text[:m.start()] + abschnitt_text(thema, nr) + '\n---\n\n' + text[m.start():]


def readme_aktualisieren(text, thema, nr, anzahl):
    """README-Block: Anzahl nachziehen, neues GIF vorn in die Dreierreihe."""
    text = re.sub(r'Alle \d+ Meilensteine', 'Alle %d Meilensteine' % anzahl, text)
    zeilen = text.split('\n')
    try:
        start = next(i for i, z in enumerate(zeilen) if z.startswith('## ') and 'Meilensteine' in z)
        kopf = next(i for i in range(start + 1, len(zeilen)) if zeilen[i].startswith('|'))
    except StopIteration:
        return text
    if kopf + 2 >= len(zeilen) or not zeilen[kopf + 2].startswith('|'):
        return text
    zellen = lambda z: [c.strip() for c in z.strip().strip('|').split('|')]
    namen, bilder = zellen(zeilen[kopf]), zellen(zeilen[kopf + 2])
    gif = dateinamen(nr, thema['stamm'])[0]
    namen = [thema['titel']] + namen[:len(namen) - 1]
    bilder = ['![%s](%s)' % (thema['clip']['gif_alt'] or thema['titel'], gif)] + bilder[:len(bilder) - 1]
    zeilen[kopf] = '| %s |' % ' | '.join(namen)
    zeilen[kopf + 2] = '| %s |' % ' | '.join(bilder)
    return '\n'.join(zeilen)


def release_text(rumpf, sha):
    notes = re.sub(r'\((meilensteine/bilder/[^)]+)\)', lambda m: '(%s%s?raw=true)' % (BLOB, m.group(1)), rumpf)
    return notes + '\n\n---\nStand im Code: %s · alle Meilensteine: [docs/meilensteine.md](%s)\n' % (sha, SEITE_URL)


def tag_name(nr, stamm):
    return 'meilenstein-%02d-%s' % (nr, stamm)


def clip_name(nr, stamm):
    return 'meilenstein-%02d-%s' % (nr, stamm)


def spiel_argumente(clip, name, log, posen_datei, uproject):
    """Startzeile des Aufnahmelaufs als LISTE - Kommas und '=' bleiben ganz
    (ein .cmd-Wrapper zerlegt -WbGoto=-95000,-60000 in drei Argumente)."""
    quit_nach = int(clip['at'] + clip['delay'] + clip['sekunden'] * clip['tempo'] * 20 + 600)
    args = [uproject, '-game', '-windowed', '-ResX=1280', '-ResY=720', '-WbKeinIntro',
            '-abslog=%s' % log, '-WbTime=%s' % clip['uhrzeit'],
            '-WbClip=%s' % name, '-WbClipSekunden=%s' % clip['sekunden'], '-WbClipFps=%s' % clip['fps'],
            '-WbClipTempo=%s' % clip['tempo'], '-WbClipDelay=%s' % clip['delay'], '-WbClipAt=%s' % clip['at'],
            '-WbQuitAfter=%d' % quit_nach,
            '-ini:GameUserSettings:[WiesbadenReal.Optionen]:StatEinblendung=False,'
            '[WiesbadenReal.Optionen]:KollisionsOverlay=False']
    if clip['goto']:
        args.append('-WbGoto=%s' % clip['goto'])
    if posen_datei:
        args.append('-WbClipPoseFile=%s' % posen_datei)
    if clip['ohne_hud']:
        args.append('-WbClipOhneHud')
    return args + clip['schalter'].split()


def foto_bild(info, sekunde):
    """Dateiname des Clip-Bildes zur Sekunde (auf der Clip-Zeitachse)."""
    index = max(0, min(int(info['bilder']) - 1, int(round(sekunde * float(info['fps'])))))
    return info['muster'] % index


# ---------------------------------------------------------------------------
# Werkzeuge
# ---------------------------------------------------------------------------

def schritt(name):
    print('\n== %s' % name, flush=True)


def lauf(*args, cwd=PROJEKT, eingabe=None, env=None, pruefen=True):
    r = subprocess.run(list(args), cwd=cwd, capture_output=True, input=eingabe, env=env,
                       text=True, encoding='utf-8', errors='replace')
    if pruefen and r.returncode != 0:
        raise Abbruch('%s: %s' % (' '.join(args[:3]), (r.stderr or r.stdout).strip()[-600:]))
    return r


def git(*args, **kw):
    return lauf('git', *args, **kw).stdout.strip()


def gh(*args, **kw):
    return lauf('gh', *args, **kw)


def hauptordner():
    """Erster Eintrag von `git worktree list` - der Haupt-Arbeitsordner."""
    for z in git('worktree', 'list', '--porcelain').splitlines():
        if z.startswith('worktree '):
            return os.path.normpath(z[len('worktree '):])
    raise Abbruch('Hauptordner nicht gefunden.')


def aus_main(pfad):
    return lauf('git', 'show', 'origin/main:%s' % pfad).stdout


def auf_github(sha):
    return gh('api', 'repos/{owner}/{repo}/commits/%s' % sha, '-q', '.sha', pruefen=False).returncode == 0


def aufnahme_build_pruefen(spiel, sha):
    """Das Thema muss im aufnehmenden Build stecken - sonst filmt der Clip
    einen Stand ohne es (27.09.2026: Clip-Worktree ohne den Hubschrauber-Fix,
    der zweite Hubschrauber fehlte im Bild, alles andere lief durch)."""
    if not os.path.isfile(os.path.join(spiel, 'Source', 'WiesbadenReal', 'World', 'WiesbadenClipRecorder.h')):
        raise Abbruch('%s hat keinen Clip-Modus (-WbClip) - --spiel <Ordner> mit Clip-Modus angeben.' % spiel)
    if lauf('git', 'merge-base', '--is-ancestor', sha, 'HEAD', cwd=spiel, pruefen=False).returncode != 0:
        zweig = git('rev-parse', '--abbrev-ref', 'HEAD', cwd=spiel)
        raise Abbruch('Der Aufnahme-Build %s (%s) enthaelt den Themen-Commit %s nicht - das Thema waere '
                      'nicht im Bild. Commit dorthin bringen und bauen, oder --spiel <Ordner> mit beidem.'
                      % (spiel, zweig, sha[:9]))


def vorpruefung(thema, nr, trocken, schaufenster, spiel):
    """Alles, was spaeter scheitern koennte, VOR dem Aufnahmelauf pruefen."""
    schritt('Vorpruefung')
    sha = git('rev-parse', '%s^{commit}' % thema['commit'])
    aufnahme_build_pruefen(spiel, sha)
    if not trocken:
        gh('auth', 'status')
        if not auf_github(sha):
            raise Abbruch('Themen-Commit %s liegt nicht auf GitHub - erst pushen.' % sha[:9])
        tag = tag_name(nr, thema['stamm'])
        if gh('release', 'view', tag, pruefen=False).returncode == 0:
            print('Release %s gibt es schon - Schritt 5 entfaellt.' % tag)
        if schaufenster:
            if not os.path.isdir(os.path.join(SCHAUFENSTER_KLON, '.git')):
                raise Abbruch('Schaufenster-Klon fehlt: %s' % SCHAUFENSTER_KLON)
            mail = git('-C', SCHAUFENSTER_KLON, 'config', 'user.email')
            if not mail.endswith('@users.noreply.github.com'):
                raise Abbruch('Schaufenster-Klon committet mit "%s" - dort nur die noreply-Adresse.' % mail)
            if git('-C', SCHAUFENSTER_KLON, 'status', '--porcelain'):
                raise Abbruch('Schaufenster-Klon ist nicht sauber: %s' % SCHAUFENSTER_KLON)
    print('Meilenstein %d "%s", Themen-Commit %s.' % (nr, thema['titel'], sha[:9]))
    return sha


# ---------------------------------------------------------------------------
# 1. Aufnahme
# ---------------------------------------------------------------------------

def clip_info(ordner):
    datei = os.path.join(ordner, 'clip.json')
    if not os.path.isfile(datei):
        return None
    with open(datei, encoding='utf-8') as f:
        return json.load(f)


def aufnehmen(thema, nr, arbeit, neu, spiel):
    schritt('1. Aufnahme')
    name = clip_name(nr, thema['stamm'])
    ordner = os.path.join(spiel, 'Saved', 'Clips', name)
    info = clip_info(ordner)
    if info and info.get('vollstaendig') and not neu:
        print('Clip liegt schon vor (%s Bilder) - kein neuer Lauf.' % info['bilder'])
        return ordner, info
    from engine import editor
    clip = thema['clip']
    posen = ''
    if clip['pose']:
        posen = os.path.join(arbeit, 'pose.txt')
        with open(posen, 'w', encoding='utf-8') as f:
            f.write(clip['pose'].strip() + '\n')
    log = os.path.join(arbeit, 'aufnahme.log')
    uproject = os.path.join(spiel, 'WiesbadenReal.uproject')
    sperre = os.path.join(hauptordner(), 'Tools', 'engine_run_lock.ps1')   # nur diese Fassung wartet
    ps = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', sperre]
    r = subprocess.run(ps + ['-Modus', 'Nehmen', '-Name', name, '-WarteSekunden', '3600'])
    if r.returncode != 0:
        raise Abbruch('Engine-Lock nicht bekommen (Exit %d).' % r.returncode)
    try:
        for _ in range(120):                 # ZenServer eines vorigen Laufs abwarten
            if 'zenserver.exe' not in lauf('tasklist', '/FI', 'IMAGENAME eq zenserver.exe', pruefen=False).stdout.lower():
                break
            time.sleep(5)
        args = [editor()] + spiel_argumente(clip, name, log, posen, uproject)
        print('Spiel startet (%s s Clip, Fenster nicht minimieren) ...' % clip['sekunden'], flush=True)
        spiel = subprocess.Popen(args)
        try:
            spiel.wait(timeout=1800)
        except subprocess.TimeoutExpired:
            spiel.kill()
            raise Abbruch('Aufnahmelauf nach 30 min nicht beendet - abgebrochen (Log %s).' % log)
    finally:
        subprocess.run(ps + ['-Modus', 'Freigeben'])
    info = clip_info(ordner)
    if not info:
        raise Abbruch('Kein Clip entstanden - hat dieser Build den Clip-Modus? Log: %s' % log)
    if not info.get('vollstaendig'):
        raise Abbruch('Clip unvollstaendig (%s von %s Bildern) - Fenster minimiert? Log: %s'
                      % (info.get('bilder'), info.get('soll'), log))
    soll, ist = info.get('spielzeit_je_bild_soll'), info.get('spielzeit_je_bild_gemessen')
    if soll and ist and abs(ist - soll) > 0.05 * soll:
        print('WARNUNG: Spielzeit je Bild %.4f statt %.4f - Clip laeuft ungleichmaessig.' % (ist, soll))
    print('Clip: %s Bilder, %sx%s, %s fps.' % (info['bilder'], info['breite'], info['hoehe'], info['fps']))
    return ordner, info


# ---------------------------------------------------------------------------
# 2. Medien
# ---------------------------------------------------------------------------

def medien_bauen(thema, nr, ordner, info, arbeit):
    schritt('2. GIF und Foto')
    import medien
    gif_pfad, jpg_pfad = [os.path.join(arbeit, os.path.basename(p)) for p in dateinamen(nr, thema['stamm'])]
    g = thema['clip']['gif']
    cmd, _ = medien.clip_befehl(ordner, gif_pfad, g['von'], g['bis'], g['breite'], g['fps'], g['farben'], g['crop'])
    subprocess.run(cmd, check=True)
    mb = os.path.getsize(gif_pfad) / 1e6
    print('GIF %s: %.2f MB' % (os.path.basename(gif_pfad), mb))
    if mb > GIF_GRENZE_MB:
        raise Abbruch('GIF %.1f MB > %.0f MB - kuerzer schneiden (gif.von/bis) oder gif.fps/farben/breite senken.'
                      % (mb, GIF_GRENZE_MB))
    bild = os.path.join(ordner, foto_bild(info, thema['clip']['foto_bei']))
    subprocess.run([medien.FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-i', bild,
                    '-vf', 'scale=1280:-1:flags=lanczos', '-q:v', '4', jpg_pfad], check=True)
    print('Foto %s: %.0f KB (Clip-Bild %s)' % (os.path.basename(jpg_pfad), os.path.getsize(jpg_pfad) / 1e3,
                                                os.path.basename(bild)))
    return gif_pfad, jpg_pfad


# ---------------------------------------------------------------------------
# 3. + 4. Seite, Doku-Commit, PR
# ---------------------------------------------------------------------------

def seite_bauen(thema, nr, arbeit):
    schritt('3. Seite und README')
    seite = seite_einfuegen(aus_main(SEITE), thema, nr)
    readme = readme_aktualisieren(aus_main('README.md'), thema, nr, len(abschnitte(seite)))
    for name, inhalt in (('meilensteine.md', seite), ('README.md', readme)):
        with open(os.path.join(arbeit, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(inhalt)
    print('Vorschau: %s' % os.path.join(arbeit, 'meilensteine.md'))
    return seite, readme


def doku_commit(thema, nr, arbeit, gif_pfad, jpg_pfad):
    """Commit auf origin/main aus einem Wegwerf-Index - kein Checkout, keine
    fremde Arbeit, kein Platz fuer eine zweite Kopie des Repos."""
    gif_ziel, jpg_ziel = dateinamen(nr, thema['stamm'])
    dateien = [(SEITE, os.path.join(arbeit, 'meilensteine.md')), ('README.md', os.path.join(arbeit, 'README.md')),
               (gif_ziel, gif_pfad), (jpg_ziel, jpg_pfad)]
    index = os.path.join(arbeit, 'doku.index')
    if os.path.exists(index):
        os.remove(index)
    env = dict(os.environ, GIT_INDEX_FILE=index)
    lauf('git', 'read-tree', 'origin/main', env=env)
    for ziel, quelle in dateien:
        blob = lauf('git', 'hash-object', '-w', '--path=%s' % ziel, quelle).stdout.strip()
        lauf('git', 'update-index', '--add', '--cacheinfo', '100644,%s,%s' % (blob, ziel), env=env)
    baum = lauf('git', 'write-tree', env=env).stdout.strip()
    os.remove(index)
    nachricht = ('Meilenstein %d: %s\n\nSeite, README-Block, GIF und Foto aus dem Spiel '
                 '(Tools/meilenstein.py).\n' % (nr, thema['titel']))
    sha = lauf('git', 'commit-tree', baum, '-p', 'origin/main', eingabe=nachricht).stdout.strip()
    print('Doku-Commit %s:' % sha[:9])
    print(git('diff', '--stat', 'origin/main', sha))
    return sha


def doku_pr(thema, nr, sha):
    schritt('4. Doku-PR nach main')
    zweig = 'docs/%s' % tag_name(nr, thema['stamm'])
    haupt = hauptordner()
    titel = 'Meilenstein %d: %s' % (nr, thema['titel'])
    offen = gh('pr', 'list', '--head', zweig, '--state', 'open', '--json', 'url', '-q', '.[].url',
               cwd=haupt).stdout.split()
    if offen:
        # Ein frueherer Lauf kam bis zur PR - die nehmen, statt neu zu pushen.
        print('Offene PR aus einem frueheren Lauf: %s' % offen[0])
        return pr_mergen(offen[0], nr, thema, zweig, haupt)
    print('Push %s -> %s (Push-Gate laeuft, das dauert) ...' % (sha[:9], zweig), flush=True)
    # "+": der Zweig gehoert diesem Befehl; ein Rest eines abgebrochenen Laufs
    # (Push ok, PR nicht angelegt) wird ersetzt - commit-tree traegt die Uhrzeit.
    r = subprocess.run(['git', 'push', 'origin', '+%s:refs/heads/%s' % (sha, zweig)], cwd=haupt)
    if r.returncode != 0:
        raise Abbruch('Push abgewiesen (Gate rot oder Engine-Lock belegt, siehe oben) - nichts gemergt, '
                      'nichts veroeffentlicht. Danach denselben Befehl erneut aufrufen.')
    koerper = ('Neuer Meilenstein auf docs/meilensteine.md, README-Block, GIF und Foto aus dem Spiel.\n\n'
               'Erzeugt mit `python Tools/meilenstein.py`.\n')
    url = gh('pr', 'create', '--base', 'main', '--head', zweig, '--title', titel, '--body', koerper,
             cwd=haupt).stdout.strip().splitlines()[-1]
    print('PR: %s' % url)
    return pr_mergen(url, nr, thema, zweig, haupt)


def pr_mergen(url, nr, thema, zweig, haupt):
    gh('pr', 'merge', url, '--squash', cwd=haupt)
    gh('api', '-X', 'DELETE', 'repos/{owner}/{repo}/git/refs/heads/%s' % zweig, pruefen=False)
    git('fetch', '-q', 'origin')
    gif_ziel = dateinamen(nr, thema['stamm'])[0]
    if lauf('git', 'cat-file', '-e', 'origin/main:%s' % gif_ziel, pruefen=False).returncode != 0:
        raise Abbruch('Nach dem Merge fehlt %s auf origin/main.' % gif_ziel)
    print('Gemergt, main = %s.' % git('rev-parse', '--short', 'origin/main'))
    return url


# ---------------------------------------------------------------------------
# 5. Release, 6. Schaufenster
# ---------------------------------------------------------------------------

def release(thema, nr, sha, gif_pfad, jpg_pfad):
    schritt('5. Release')
    tag = tag_name(nr, thema['stamm'])
    if gh('release', 'view', tag, pruefen=False).returncode == 0:
        print('%s gibt es schon.' % tag)
        return '%s/releases/tag/%s' % (REPO_URL, tag)
    titel, rumpf = abschnitte(aus_main(SEITE))[nr]
    with tempfile.NamedTemporaryFile('w', suffix='.md', delete=False, encoding='utf-8') as f:
        f.write(release_text(rumpf, sha))
    try:
        url = gh('release', 'create', tag, gif_pfad, jpg_pfad, '--target', sha,
                 '--title', 'Meilenstein %d: %s' % (nr, titel), '--notes-file', f.name,
                 '--latest').stdout.strip()
    finally:
        os.unlink(f.name)
    print('Release: %s' % url)
    return url


def schaufenster_nachziehen(nr, titel):
    schritt('6. Schaufenster')
    git('-C', SCHAUFENSTER_KLON, 'pull', '-q', '--ff-only')
    tmp = tempfile.mkdtemp(prefix='schaufenster_')
    try:
        # Generator und Quelle von origin/main - dort liegt die gueltige Fassung.
        modul_pfad = os.path.join(tmp, 'schaufenster.py')
        with open(modul_pfad, 'w', encoding='utf-8') as f:
            f.write(aus_main('Tools/schaufenster.py'))
        spec = importlib.util.spec_from_file_location('schaufenster_main', modul_pfad)
        modul = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(modul)
        tar = subprocess.run(['git', 'archive', 'origin/main', SEITE, 'docs/meilensteine'], cwd=PROJEKT,
                             capture_output=True, check=True).stdout
        with tarfile.open(fileobj=io.BytesIO(tar)) as t:
            t.extractall(tmp, filter='data')
        with open(os.path.join(tmp, SEITE), encoding='utf-8') as f:
            text = f.read()
        stand = 'Stand %s' % time.strftime('%d.%m.%Y')
        modul.erzeugen(text, os.path.join(tmp, BILDER), SCHAUFENSTER_KLON, stand)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    git('-C', SCHAUFENSTER_KLON, 'add', '-A')
    if not git('-C', SCHAUFENSTER_KLON, 'status', '--porcelain'):
        print('Schaufenster unveraendert.')
        return
    git('-C', SCHAUFENSTER_KLON, 'commit', '-q', '-m', 'Meilenstein %d: %s' % (nr, titel))
    git('-C', SCHAUFENSTER_KLON, 'push', '-q')
    print('Schaufenster gepusht (GitHub Pages baut in ~1 min).')


# ---------------------------------------------------------------------------

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    p.add_argument('thema', help='Themen-Datei (JSON)')
    p.add_argument('--trocken', action='store_true',
                   help='nur lokal: Aufnahme, Medien, Seitenvorschau, Commit-Objekt - kein Push/PR/Release')
    p.add_argument('--neu-aufnehmen', action='store_true', help='vorhandenen Clip verwerfen')
    p.add_argument('--ohne-schaufenster', action='store_true')
    p.add_argument('--spiel', default=PROJEKT,
                   help='Ordner, dessen Build aufnimmt (braucht Clip-Modus UND den Themen-Commit); Vorgabe: dieser')
    a = p.parse_args(argv)
    try:
        with open(a.thema, encoding='utf-8') as f:
            thema = thema_laden(json.load(f))
        git('fetch', '-q', 'origin')
        seite_main = aus_main(SEITE)
        nr = thema.get('nr') or naechste_nummer(seite_main)
        schon_da = nr in abschnitte(seite_main)
        if schon_da and abschnitte(seite_main)[nr][0] != thema['titel']:
            raise Abbruch('Nummer %d ist auf main schon "%s".' % (nr, abschnitte(seite_main)[nr][0]))
        spiel = os.path.abspath(a.spiel)
        sha = vorpruefung(thema, nr, a.trocken, not a.ohne_schaufenster, spiel)
        arbeit = os.path.join(PROJEKT, 'Saved', 'Meilensteine', '%02d-%s' % (nr, thema['stamm']))
        os.makedirs(arbeit, exist_ok=True)

        ordner, info = aufnehmen(thema, nr, arbeit, a.neu_aufnehmen, spiel)
        gif_pfad, jpg_pfad = medien_bauen(thema, nr, ordner, info, arbeit)
        if schon_da:
            schritt('3./4. Seite und Doku-PR')
            print('Meilenstein %d steht schon auf main - entfaellt.' % nr)
            pr = '(schon gemergt)'
        else:
            seite_bauen(thema, nr, arbeit)
            doku = doku_commit(thema, nr, arbeit, gif_pfad, jpg_pfad)
            if a.trocken:
                print('\nTROCKEN: kein Push, keine PR, kein Release, kein Schaufenster.')
                print('Arbeitsordner: %s' % arbeit)
                return 0
            pr = doku_pr(thema, nr, doku)
        if a.trocken:
            return 0
        rel = release(thema, nr, sha, gif_pfad, jpg_pfad)
        if not a.ohne_schaufenster:
            schaufenster_nachziehen(nr, thema['titel'])
        print('\nFertig: Meilenstein %d "%s"\n  PR      %s\n  Release %s' % (nr, thema['titel'], pr, rel))
        return 0
    except Abbruch as e:
        print('\nABBRUCH: %s' % e, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
