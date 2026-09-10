// Einmaliges Werkzeug: schreibt die absoluten Pfade des alten Rechners
// (Benutzer ssonn, aivideo-Ordner, Engine unter Program Files) auf den
// neuen Standort um. Literale String-Ersetzung (backslash-sicher), Bytes
// ausserhalb der Treffer bleiben unangetastet.
import { readFileSync, writeFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

const ROOT = 'C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal';

// Reihenfolge: spezifischere/doppelt-escapte Muster zuerst.
const PAIRS = [
  // .mjs: doppelte Backslashes (JS-Quelltext)
  ['C:\\\\Users\\\\ssonn\\\\aivideo\\\\WiesbadenReal', 'C:\\\\freebuff\\\\WiesbadenReal_Sicherung\\\\WiesbadenReal'],
  // Quellen (frueher unter Downloads)
  ['C:/Users/ssonn/Downloads/vw-beetle-1969', 'C:/freebuff/WiesbadenReal_Sicherung/Quellen/vw-beetle-1969'],
  ['C:\\Users\\ssonn\\Downloads\\wbnracing', 'C:\\freebuff\\WiesbadenReal_Sicherung\\Quellen\\wbnracing'],
  // Projekt (beide Slash-Formen)
  ['C:\\Users\\ssonn\\aivideo\\WiesbadenReal', 'C:\\freebuff\\WiesbadenReal_Sicherung\\WiesbadenReal'],
  ['C:/Users/ssonn/aivideo/WiesbadenReal', 'C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal'],
  // Engine (beide Slash-Formen)
  ['C:\\Program Files\\Epic Games\\UE_5.8', 'C:\\freebuff\\WiesbadenReal_Sicherung\\UE_5.8'],
  ['C:/Program Files/Epic Games/UE_5.8', 'C:/Program Files/Epic Games/UE_5.8'],
];

const files = [];
for (const f of readdirSync(ROOT)) {
  if (f.toLowerCase().endsWith('.cmd')) files.push(join(ROOT, f));
}
files.push(join(ROOT, 'Tools/analyze_city_builds.mjs'));
files.push(join(ROOT, 'Tools/verify-worktree-sync.mjs'));
files.push(join(ROOT, 'Tools/Blender/make_herbie.py'));

let total = 0;
for (const path of files) {
  let text;
  try { text = readFileSync(path, 'utf8'); } catch { continue; }
  let n = 0;
  for (const [from, to] of PAIRS) {
    const before = text;
    text = text.split(from).join(to);
    if (text !== before) {
      // Anzahl der Treffer grob zaehlen
      n += before.split(from).length - 1;
    }
  }
  if (n > 0) {
    writeFileSync(path, text, 'utf8');
    total += n;
    console.log(`${n.toString().padStart(3)}  ${path.slice(ROOT.length + 1)}`);
  }
}
console.log(`\nGesamt ${total} Ersetzungen in ${files.length} geprueften Dateien.`);
