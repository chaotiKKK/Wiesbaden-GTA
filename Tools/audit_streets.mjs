// Vergleicht das gebaute Strassennetz mit den OSM-Quelldaten.
//
// Anlass: "Platter Strasse stadtauswaerts fehlt in Spielwelt und Minikarte".
// Am Bild ist so etwas nicht zu pruefen - man sieht immer nur den Ausschnitt,
// in dem man gerade steht, und ein Fehlen faellt nur dort auf, wo man
// zufaellig hinschaut. Dieser Abgleich prueft die GESAMTE Karte.
//
// Verglichen wird ueber die OSM-Way-Id, nicht ueber den Namen. "Platter
// Strasse" steht in Wiesbaden an 60 Wegen, davon einige als Wirtschaftsweg
// und Fussweg; ein fehlender Fahrstreifen ginge zwischen den vorhandenen
// unter.
//
// Aufruf:
//   node Tools/audit_streets.mjs [Abzug.csv] [wiesbaden.osm.json]
//
// Der Abzug entsteht im Spiel mit -WbDumpStreets.

import fs from 'node:fs';

const DUMP = process.argv[2] || 'Saved/Diagnose/Strassen.csv';
const OSM = process.argv[3] || 'Data/Raw/OSM/wiesbaden.osm.json';

// Wege, die das Projekt bewusst NICHT als Fahrbahn baut, wuerden den Bericht
// sonst mit hunderten Treppen und Trampelpfaden fluten. Sie werden getrennt
// gezaehlt statt verschwiegen - "nicht gebaut" und "unbemerkt verloren" sind
// zwei verschiedene Dinge.
const FAHRBAHN = new Set([
  'motorway', 'motorway_link', 'trunk', 'trunk_link',
  'primary', 'primary_link', 'secondary', 'secondary_link',
  'tertiary', 'tertiary_link', 'unclassified', 'residential',
  'living_street', 'service', 'pedestrian', 'track',
]);

if (!fs.existsSync(DUMP)) {
  console.error(`Abzug fehlt: ${DUMP}\nErst im Spiel mit -WbDumpStreets erzeugen.`);
  process.exit(1);
}

// Kodierung am BOM erkennen, nicht annehmen.
//
// Unreals FFileHelper waehlt die Kodierung nach Inhalt: Sobald ein Zeichen
// ausserhalb von ASCII vorkommt - und deutsche Strassennamen sind voll davon -
// schreibt es UTF-16. Als UTF-8 gelesen ergab die Datei GENAU EINE Zeile, und
// der Bericht meldete daraufhin 39.863 fehlende Wege und 5.272 km Loecher.
// Ein Werkzeug, das bei unlesbarer Eingabe "alles kaputt" meldet statt
// "Eingabe unlesbar", ist gefaehrlicher als gar keins.
function readText(path) {
  const buf = fs.readFileSync(path);
  if (buf.length >= 2 && buf[0] === 0xff && buf[1] === 0xfe) return buf.toString('utf16le', 2);
  if (buf.length >= 3 && buf[0] === 0xef && buf[1] === 0xbb && buf[2] === 0xbf) return buf.toString('utf8', 3);
  return buf.toString('utf8');
}

const built = new Set();
const builtLength = new Map();
for (const line of readText(DUMP).split(/\r?\n/).slice(1)) {
  if (!line.trim()) continue;
  const f = line.split(';');
  const id = Number(f[0]);
  built.add(id);
  builtLength.set(id, (builtLength.get(id) || 0) + Number(f[4] || 0));
}
console.log(`Abzug: ${built.size} Way-Ids, ${builtLength.size} mit Laenge.`);
if (built.size < 100) {
  console.error(`ABBRUCH: nur ${built.size} Way-Ids gelesen - Datei offenbar unlesbar.`);
  process.exit(1);
}

const osm = JSON.parse(fs.readFileSync(OSM, 'utf8'));
const nodes = new Map();
for (const e of osm.elements) if (e.type === 'node') nodes.set(e.id, [e.lon, e.lat]);

const metersPerDegLat = 111320;
function wayLength(w) {
  let m = 0;
  for (let i = 1; i < (w.nodes || []).length; i++) {
    const a = nodes.get(w.nodes[i - 1]);
    const b = nodes.get(w.nodes[i]);
    if (!a || !b) continue;
    const dy = (b[1] - a[1]) * metersPerDegLat;
    const dx = (b[0] - a[0]) * metersPerDegLat * Math.cos((a[1] * Math.PI) / 180);
    m += Math.hypot(dx, dy);
  }
  return m;
}

const missing = new Map();   // Name -> {ways, meter, art, mitte}
let missingCount = 0;
let missingMeters = 0;
let skippedKind = 0;
let builtCount = 0;

for (const e of osm.elements) {
  if (e.type !== 'way' || !e.tags || !e.tags.highway) continue;
  if (!FAHRBAHN.has(e.tags.highway)) { skippedKind++; continue; }
  if (built.has(e.id)) { builtCount++; continue; }

  const m = wayLength(e);
  missingCount++;
  missingMeters += m;

  const key = e.tags.name || `(ohne Namen, ${e.tags.highway})`;
  const entry = missing.get(key) || { ways: [], meter: 0, arten: new Set() };
  entry.ways.push(e.id);
  entry.meter += m;
  entry.arten.add(e.tags.highway);
  const mid = nodes.get(e.nodes[Math.floor(e.nodes.length / 2)]);
  if (mid && !entry.mitte) entry.mitte = mid;
  missing.set(key, entry);
}

console.log(`OSM-Fahrbahnwege gebaut: ${builtCount}, fehlend: ${missingCount} (${(missingMeters / 1000).toFixed(1)} km)`);
console.log(`Andere Wegearten (Fussweg/Treppe/Radweg/Pfad), nicht als Fahrbahn vorgesehen: ${skippedKind}`);

const named = [...missing.entries()]
  .filter(([name]) => !name.startsWith('(ohne Namen'))
  .sort((a, b) => b[1].meter - a[1].meter);

console.log(`\nFehlende BENANNTE Strassen (${named.length}), laengste zuerst:`);
for (const [name, e] of named.slice(0, 40)) {
  const ort = e.mitte ? `${e.mitte[1].toFixed(5)},${e.mitte[0].toFixed(5)}` : '?';
  console.log(
    `  ${(e.meter / 1000).toFixed(2)} km  ${name}  [${[...e.arten].join(',')}]  ` +
    `${e.ways.length} Wege  Mitte ${ort}`);
}

const anon = [...missing.entries()].filter(([n]) => n.startsWith('(ohne Namen'));
const anonMeters = anon.reduce((s, [, e]) => s + e.meter, 0);
console.log(`\nFehlende Wege ohne Namen: ${anon.reduce((s, [, e]) => s + e.ways.length, 0)} (${(anonMeters / 1000).toFixed(1)} km)`);
