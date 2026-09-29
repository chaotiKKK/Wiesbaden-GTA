# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""Oeffentliches Schaufenster der Meilensteine (GitHub Pages).

    python Tools/schaufenster.py --ziel C:\\pfad\\zum\\oeffentlichen\\repo [--ref origin/main]

Liest docs/meilensteine.md samt Bildern aus <ref> (git archive, nicht aus dem
Arbeitsbaum - dort kann fremde, unfertige Arbeit liegen) und schreibt nach
<ziel>: index.html, bilder/, .nojekyll, README.md. Ins oeffentliche Repo
kommt NUR das - kein Code, keine Werkzeuge, keine Pfade des privaten Repos.

Gestaltung: ein Liniennetzplan. Jeder Meilenstein ist ein Halt auf einer
Linie, neueste zuerst - Wiesbaden faehrt ESWE-Bus und Nerobergbahn.

Schutz: Links ins private Repo (github.com/chaotiKKK/Wiesbaden-GTA) fallen
samt ihrem Satz weg - oeffentlich fuehren sie ins Leere und nennen den
privaten Repo-Namen. Der Lauf bricht ab, wenn trotzdem einer im HTML steht
oder ein Bild fehlt.
"""
import argparse
import datetime
import html
import io
import os
import re
import shutil
import subprocess
import tarfile

PRIVAT = re.compile(r'github\.com/chaotiKKK/Wiesbaden-GTA', re.I)
BILD = re.compile(r'!\[([^\]]*)\]\(([^)]+)\)')


# ---------------------------------------------------------------------------
# Markdown der Meilenstein-Seite lesen (genau der Teil, den sie benutzt)
# ---------------------------------------------------------------------------

def ohne_privat(absatz):
    """Saetze mit einem Link ins private Repo entfernen."""
    if not PRIVAT.search(absatz):
        return absatz
    saetze = re.split(r'(?<=[.!?])\s+', absatz)
    return ' '.join(s for s in saetze if not PRIVAT.search(s)).strip()


def absaetze(zeilen):
    """Zeilen -> Absaetze (Leerzeile trennt); Bild- und Tabellenzeilen fallen weg."""
    out, akt = [], []
    for z in zeilen:
        s = z.strip()
        if not s or s.startswith('|') or s.startswith('!['):
            if akt:
                out.append(' '.join(akt))
                akt = []
            continue
        akt.append(s)
    if akt:
        out.append(' '.join(akt))
    return out


def lesen(text):
    """-> dict(einleitung, hinweis, abschnitte=[...]) aus docs/meilensteine.md."""
    teile = re.split(r'^## (\d+)\. (.+)$', text, flags=re.M)
    kopf = teile[0]

    # Uebersichtstabelle: | 14 | [Titel](#anker) | Zeitraum | Stand |
    stand = {}
    for m in re.finditer(r'^\|\s*(\d+)\s*\|\s*\[([^\]]+)\]\([^)]*\)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*$',
                         kopf, flags=re.M):
        stand[int(m.group(1))] = m.group(4)

    kopfzeilen = [z for z in kopf.splitlines() if not z.startswith('# ')]
    hinweis = ' '.join(z[1:].strip() for z in kopfzeilen if z.startswith('>'))
    einleitung = [ohne_privat(a) for a in absaetze(z for z in kopfzeilen if not z.startswith('>'))
                  if a.strip() not in ('---',)]
    einleitung = [a for a in einleitung if a]

    abschnitte = []
    for i in range(1, len(teile), 3):
        nr, titel, rumpf = int(teile[i]), teile[i + 1].strip(), teile[i + 2].split('\n---')[0]
        zeilen = rumpf.strip().splitlines()
        datum = ''
        if zeilen and re.fullmatch(r'\*[^*]+\*', zeilen[0].strip()):
            datum = zeilen[0].strip().strip('*')
            zeilen = zeilen[1:]
        gross, klein = [], []
        for z in zeilen:
            ziel = klein if z.strip().startswith('|') else gross
            ziel.extend(BILD.findall(z))
        abschnitte.append({
            'nr': nr, 'titel': titel, 'datum': datum, 'stand': stand.get(nr, ''),
            'absaetze': [ohne_privat(a) for a in absaetze(zeilen)],
            'gross': gross, 'klein': klein,
        })
    return {'einleitung': einleitung, 'hinweis': hinweis, 'abschnitte': abschnitte}


def inline(text):
    """Escapen, dann **fett**, *kursiv* und [Link](url) (nur externe Ziele)."""
    t = html.escape(text, quote=False)
    t = re.sub(r'\[([^\]]+)\]\(([^)]+)\)',
               lambda m: ('<a href="%s">%s</a>' % (html.escape(m.group(2)), m.group(1))
                          if m.group(2).startswith('http') and not PRIVAT.search(m.group(2))
                          else m.group(1)), t)
    t = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', t)
    t = re.sub(r'\*([^*]+)\*', r'<em>\1</em>', t)
    return t


def ist_fertig(stand):
    return stand.strip().lower() == 'fertig'


def bildpfad(src):
    """meilensteine/bilder/x.gif -> bilder/x.gif"""
    return 'bilder/' + os.path.basename(src)


# ---------------------------------------------------------------------------
# HTML
# ---------------------------------------------------------------------------

CSS = r"""
:root{
  --nacht:#0d1822; --nacht2:#132330; --nacht3:#1b3142;
  --sand:#ecdcc0; --sand2:#b9a88b; --sand3:#7f725e;
  --signal:#f3c431; --koralle:#ff7b5c; --linie:6px;
}
*{box-sizing:border-box}
html{scroll-behavior:smooth}
body{margin:0;background:var(--nacht);color:var(--sand);
  font-family:"Newsreader",Georgia,serif;font-size:19px;line-height:1.6;
  background-image:radial-gradient(1200px 700px at 85% -10%,#1d3a4f 0,transparent 60%),
                   radial-gradient(900px 600px at -10% 40%,#16283a 0,transparent 65%);}
body::after{content:"";position:fixed;inset:0;pointer-events:none;opacity:.07;mix-blend-mode:overlay;
  background-image:url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='160' height='160'><filter id='n'><feTurbulence type='fractalNoise' baseFrequency='.9' numOctaves='3' stitchTiles='stitch'/></filter><rect width='100%' height='100%' filter='url(%23n)'/></svg>");}
a{color:var(--signal);text-decoration-thickness:1px;text-underline-offset:3px}
.mono{font-family:"Martian Mono",ui-monospace,monospace;font-size:.72rem;letter-spacing:.08em;text-transform:uppercase}
.schild{font-family:"Big Shoulders Display",Impact,sans-serif;font-weight:800;letter-spacing:.01em;line-height:.92}

/* Kopf */
header{max-width:1240px;margin:0 auto;padding:72px 32px 40px;display:grid;
  grid-template-columns:minmax(0,1.35fr) minmax(0,1fr);gap:56px;align-items:end}
.ueber{color:var(--signal);display:flex;gap:14px;align-items:center}
.ueber::before{content:"";width:34px;height:var(--linie);background:var(--signal);border-radius:3px}
h1{font-size:clamp(4rem,11vw,9.5rem);margin:.18em 0 .1em;color:var(--sand)}
h1 span{color:var(--signal)}
.einleitung p{max-width:38em;color:var(--sand2);margin:.6em 0}
.einleitung em{color:var(--sand)}
.hinweis{margin-top:22px;padding:12px 16px;border-left:3px solid var(--sand3);color:var(--sand3);font-size:.86rem}

/* Fahrplan-Tafel */
.tafel{background:linear-gradient(180deg,var(--nacht3),var(--nacht2));border:1px solid #2b4659;border-radius:14px;
  padding:22px 22px 14px;box-shadow:0 30px 60px -30px #000;position:relative}
.tafel h2{margin:0 0 12px;font-size:1.9rem;color:var(--signal)}
.tafel ol{list-style:none;margin:0;padding:0}
.tafel li a{display:grid;grid-template-columns:2.4em 1fr auto;gap:10px;align-items:baseline;
  padding:7px 4px;border-top:1px dashed #2e4a5e;color:var(--sand);text-decoration:none;font-size:.93rem}
.tafel li a:hover{background:#ffffff08}
.tafel .nr{font-family:"Martian Mono",monospace;font-size:.72rem;color:var(--signal)}
.tafel .st{font-family:"Martian Mono",monospace;font-size:.62rem;text-transform:uppercase;letter-spacing:.06em;color:var(--sand3);text-align:right}
.tafel .st.offen{color:var(--koralle)}

/* Linie */
main{max-width:1240px;margin:0 auto;padding:30px 32px 80px;position:relative}
main::before{content:"";position:absolute;left:calc(32px + 27px);top:0;bottom:120px;width:var(--linie);
  background:var(--signal);border-radius:3px;box-shadow:0 0 30px #f3c43133}
.halt{display:grid;grid-template-columns:60px minmax(0,1fr);gap:34px;padding:46px 0;
  opacity:0;transform:translateY(24px);transition:opacity .8s ease,transform .8s cubic-bezier(.2,.7,.2,1)}
.halt.sichtbar{opacity:1;transform:none}
.punkt{width:60px;height:60px;border-radius:50%;background:var(--nacht);border:var(--linie) solid var(--signal);
  display:grid;place-items:center;position:sticky;top:24px;font-family:"Martian Mono",monospace;font-weight:700;
  font-size:.95rem;color:var(--signal);z-index:1}
.halt.offen .punkt{border-color:var(--koralle);color:var(--koralle)}
.kopfzeile{display:flex;flex-wrap:wrap;gap:12px 18px;align-items:center;margin-bottom:6px;color:var(--sand3)}
.status{padding:4px 10px;border-radius:999px;border:1px solid currentColor}
.status.fertig{color:#8fd694}
.status.offen{color:var(--koralle)}
h3{font-size:clamp(2.4rem,5vw,4.1rem);margin:.08em 0 .35em;color:var(--sand)}
.text{max-width:40em}
.text p{margin:.5em 0}
.text em{color:var(--sand);font-style:italic}
.gross{display:grid;gap:14px;margin:26px 0 12px}
.gross.zwei{grid-template-columns:repeat(2,minmax(0,1fr))}
.gross.drei{grid-template-columns:repeat(3,minmax(0,1fr))}
.klein{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:12px;margin-top:14px}
figure{margin:0;position:relative;border-radius:10px;overflow:hidden;background:#000;cursor:zoom-in;
  box-shadow:0 18px 40px -22px #000;border:1px solid #ffffff14}
figure img{display:block;width:100%;height:100%;object-fit:cover;transition:transform .6s ease}
.klein figure{aspect-ratio:16/10}
figure:hover img{transform:scale(1.03)}
figcaption{position:absolute;left:0;right:0;bottom:0;padding:26px 12px 9px;font-size:.8rem;color:var(--sand);
  background:linear-gradient(transparent,#000c)}
.gif::before{content:"GIF";position:absolute;top:10px;left:10px;z-index:1;font-family:"Martian Mono",monospace;
  font-size:.6rem;letter-spacing:.1em;background:var(--signal);color:var(--nacht);padding:3px 7px;border-radius:4px}

/* Fuss */
footer{max-width:1240px;margin:0 auto;padding:40px 32px 70px;color:var(--sand3);font-size:.85rem;
  border-top:1px solid #2b4659;display:grid;grid-template-columns:1fr 1fr;gap:40px}
footer h4{margin:0 0 8px;color:var(--sand2)}
footer p{margin:.3em 0}
.ende{font-size:3.2rem;color:var(--nacht3);margin:0}

dialog{border:0;padding:0;background:transparent;max-width:94vw;max-height:92vh}
dialog::backdrop{background:#050b10ee}
dialog img{max-width:94vw;max-height:84vh;display:block;border-radius:8px}
dialog p{color:var(--sand);text-align:center;margin:10px 0 0;font-size:.95rem}

@media (max-width:900px){
  header{grid-template-columns:1fr;padding-top:44px;gap:30px}
  main::before{left:calc(18px + 19px)}
  main,header,footer{padding-left:18px;padding-right:18px}
  .halt{grid-template-columns:44px minmax(0,1fr);gap:18px}
  .punkt{width:44px;height:44px;font-size:.78rem}
  .gross.zwei,.gross.drei{grid-template-columns:1fr}
  footer{grid-template-columns:1fr}
}
@media (prefers-reduced-motion:reduce){
  html{scroll-behavior:auto}.halt{opacity:1;transform:none;transition:none}figure img{transition:none}
}
"""

JS = r"""
const halte=document.querySelectorAll('.halt');
if('IntersectionObserver' in window){
  const io=new IntersectionObserver(es=>es.forEach(e=>{if(e.isIntersecting){e.target.classList.add('sichtbar');io.unobserve(e.target)}}),{threshold:.12});
  halte.forEach(h=>io.observe(h));
}else{halte.forEach(h=>h.classList.add('sichtbar'))}
const dlg=document.getElementById('gross');
document.querySelectorAll('figure').forEach(f=>f.addEventListener('click',()=>{
  const img=f.querySelector('img');dlg.querySelector('img').src=img.src;dlg.querySelector('img').alt=img.alt;
  dlg.querySelector('p').textContent=img.alt;dlg.showModal();}));
dlg.addEventListener('click',()=>dlg.close());
"""


def figur(alt, src):
    gif = ' class="gif"' if src.lower().endswith('.gif') else ''
    return ('<figure%s><img src="%s" alt="%s" loading="lazy" decoding="async"><figcaption>%s</figcaption></figure>'
            % (gif, html.escape(bildpfad(src)), html.escape(alt), html.escape(alt)))


def seite(daten, stand_text):
    halte = daten['abschnitte']
    tafel = '\n'.join(
        '<li><a href="#halt-%d"><span class="nr">%02d</span><span>%s</span><span class="st%s">%s</span></a></li>'
        % (h['nr'], h['nr'], html.escape(h['titel']), '' if ist_fertig(h['stand']) else ' offen',
           html.escape(h['stand'] or '')) for h in halte)
    teile = []
    for h in halte:
        fertig = ist_fertig(h['stand'])
        gross = h['gross']
        spalten = ' drei' if len(gross) >= 3 else (' zwei' if len(gross) == 2 else '')
        teile.append(
            '<section class="halt%s" id="halt-%d"><div class="punkt">%02d</div><div>'
            '<div class="kopfzeile"><span class="mono">Halt %d</span><span class="mono">%s</span>'
            '<span class="mono status %s">%s</span></div>'
            '<h3 class="schild">%s</h3><div class="text">%s</div>%s%s</div></section>'
            % ('' if fertig else ' offen', h['nr'], h['nr'], h['nr'], html.escape(h['datum']),
               'fertig' if fertig else 'offen', html.escape(h['stand'] or ''),
               html.escape(h['titel']), ''.join('<p>%s</p>' % inline(a) for a in h['absaetze']),
               ('<div class="gross%s">%s</div>' % (spalten, ''.join(figur(*b) for b in gross))) if gross else '',
               ('<div class="klein">%s</div>' % ''.join(figur(*b) for b in h['klein'])) if h['klein'] else ''))
    return """<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wiesbaden Real – Meilensteine</title>
<meta name="description" content="Wiesbaden als Open-World-Spiel in Unreal Engine 5: %(anzahl)d Meilensteine mit GIFs und Fotos aus dem echten Spiel.">
<meta property="og:title" content="Wiesbaden Real – Meilensteine">
<meta property="og:description" content="Wiesbaden als Open-World-Spiel in Unreal Engine 5 – mit GIFs und Fotos aus dem echten Spiel.">
<meta property="og:image" content="%(ogbild)s">
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Big+Shoulders+Display:wght@700;800&family=Martian+Mono:wght@400;700&family=Newsreader:ital,opsz,wght@0,6..72,400;0,6..72,500;1,6..72,400&display=swap" rel="stylesheet">
<style>%(css)s</style>
</head>
<body>
<header>
  <div>
    <div class="ueber mono">Linie W · %(anzahl)d Halte · neueste zuerst</div>
    <h1 class="schild">Wiesbaden<br><span>Real</span></h1>
    <div class="einleitung">%(einleitung)s</div>
    %(hinweis)s
  </div>
  <nav class="tafel" aria-label="Alle Meilensteine">
    <h2 class="schild">Fahrplan</h2>
    <ol>%(tafel)s</ol>
  </nav>
</header>
<main>
%(halte)s
</main>
<footer>
  <div>
    <h4 class="mono">Datenquellen</h4>
    <p>Kartendaten © <a href="https://www.openstreetmap.org/copyright">OpenStreetMap-Mitwirkende</a>, Open Database License (ODbL).</p>
    <p>Gebäudegrundrisse: ALKIS Hessen, Hessische Verwaltung für Bodenmanagement und Geoinformation (HVBG), <a href="https://www.govdata.de/dl-de/by-2-0">Datenlizenz Deutschland – Namensnennung 2.0</a>. Gebäudehöhen und Gelände: HVBG, Datenlizenz Deutschland – Zero.</p>
    <p>Höhendaten: NASA SRTM, gemeinfrei.</p>
  </div>
  <div>
    <h4 class="mono">Über diese Seite</h4>
    <p>Alle Bilder stammen aus dem laufenden Spiel – keine Renderings, keine Montagen. Der Quellcode des Spiels ist nicht öffentlich; hier liegen nur Text und Bilder.</p>
    <p class="mono">%(stand)s</p>
    <p class="ende schild">Endstation. Vorerst.</p>
  </div>
</footer>
<dialog id="gross"><img alt=""><p></p></dialog>
<script>%(js)s</script>
</body>
</html>
""" % {
        'anzahl': len(halte), 'css': CSS, 'js': JS, 'tafel': tafel, 'halte': '\n'.join(teile),
        'einleitung': ''.join('<p>%s</p>' % inline(a) for a in daten['einleitung']),
        'hinweis': ('<p class="hinweis">%s</p>' % inline(daten['hinweis'])) if daten['hinweis'] else '',
        'stand': html.escape(stand_text),
        'ogbild': html.escape(bildpfad(halte[0]['gross'][0][1])) if halte and halte[0]['gross'] else '',
    }


README = """# Wiesbaden Real – Meilensteine

Öffentliches Schaufenster eines Open-World-Spiels, das Wiesbaden in Unreal
Engine 5 nachbaut: **https://chaotikkk.github.io/wiesbaden-real-meilensteine/**

Hier liegen nur die Schaufenster-Seite und Bilder aus dem echten Spiel. Der
Quellcode des Spiels ist nicht öffentlich. Die Seite wird aus der
Meilenstein-Liste des Projekts erzeugt – Änderungen bitte nicht von Hand.

Kartendaten © OpenStreetMap-Mitwirkende (ODbL); Gebäudegrundrisse ALKIS
Hessen, HVBG, Datenlizenz Deutschland – Namensnennung 2.0.
"""


# ---------------------------------------------------------------------------
# Lauf
# ---------------------------------------------------------------------------

def erzeugen(text, bilder_quelle, ziel, stand_text):
    """Schreibt das Schaufenster nach ziel; Rueckgabe: Zahl der Bilder."""
    daten = lesen(text)
    inhalt = seite(daten, stand_text)
    if PRIVAT.search(inhalt):
        raise RuntimeError('Link ins private Repo im HTML - Abbruch.')
    alle = [b for h in daten['abschnitte'] for b in h['gross'] + h['klein']]
    fehlt = [src for _, src in alle if not os.path.isfile(os.path.join(bilder_quelle, os.path.basename(src)))]
    if fehlt:
        raise RuntimeError('Bilder fehlen: %s' % fehlt)
    bilder_ziel = os.path.join(ziel, 'bilder')
    if os.path.isdir(bilder_ziel):
        shutil.rmtree(bilder_ziel)      # nur unser eigener Unterordner
    os.makedirs(bilder_ziel)
    for _, src in dict.fromkeys(alle):
        shutil.copy2(os.path.join(bilder_quelle, os.path.basename(src)), bilder_ziel)
    with open(os.path.join(ziel, 'index.html'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(inhalt)
    with open(os.path.join(ziel, 'README.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(README)
    open(os.path.join(ziel, '.nojekyll'), 'w').close()
    return len(dict.fromkeys(alle))


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    p.add_argument('--ziel', required=True, help='Klon des oeffentlichen Repos')
    p.add_argument('--ref', default='origin/main')
    a = p.parse_args()
    projekt = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    tar = subprocess.run(['git', 'archive', a.ref, 'docs/meilensteine.md', 'docs/meilensteine'],
                         cwd=projekt, capture_output=True, check=True).stdout
    # errors="replace": Git-Metadaten dekodier-tolerant lesen. Ohne Handler
    # dekodiert der Textmodus ab Python 3.15 (PEP 686) UTF-8/strict - ein
    # einziges Fremd-Byte wuerde den Leser still sterben lassen.
    sha = subprocess.run(['git', 'rev-parse', '--short', a.ref], cwd=projekt, capture_output=True,
                         text=True, errors="replace", check=True).stdout.strip()
    with tarfile.open(fileobj=io.BytesIO(tar)) as t:
        text = t.extractfile('docs/meilensteine.md').read().decode('utf-8')
        tmp = os.path.join(a.ziel, '.quelle_tmp')
        os.makedirs(tmp, exist_ok=True)
        try:
            t.extractall(tmp, filter='data')
            stand = 'Stand %s' % datetime.date.today().strftime('%d.%m.%Y')
            n = erzeugen(text, os.path.join(tmp, 'docs', 'meilensteine', 'bilder'), a.ziel, stand)
        finally:
            shutil.rmtree(tmp)
    print('Schaufenster nach %s: index.html + %d Bilder (Quelle %s @ %s).' % (a.ziel, n, a.ref, sha))


if __name__ == '__main__':
    main()
