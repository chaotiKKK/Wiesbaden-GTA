/**
 * Prueft, ob die OSM-Ampelknoten ueberhaupt Kreuzungsknoten sind.
 *
 * Hintergrund: Der Stadt-Build meldet "20213 Kreuzungen (0 Ampeln)", obwohl
 * die Quelldatei tausende highway=traffic_signals enthaelt. Bevor der
 * C++-Pfad verdaechtigt wird, muss die Datenfrage geklaert sein: Liegen die
 * Ampeln auf Knoten, die von MEHREREN Strassen geteilt werden? Nur solche
 * werden zu Kreuzungen.
 *
 * Streamt zeilenweise - die Datei ist 170 MB und passt nicht bequem in den
 * Speicher.
 */
import { createReadStream } from 'node:fs';
import { createInterface } from 'node:readline';

const file = process.argv[2] ?? 'Data/Raw/OSM/wiesbaden.osm.json';

const signalNodes = new Set();
const nodeUseCount = new Map();     // NodeId -> Zahl der Strassen, die ihn nutzen
const nodeEndpointCount = new Map(); // NodeId -> Zahl der Strassen, die dort ENDEN

let elementType = null;
let currentId = null;
let inTags = false;
let inNodes = false;
let sawTrafficSignal = false;
let isHighway = false;
let wayNodes = [];

const rl = createInterface({
  input: createReadStream(file, { encoding: 'utf8' }),
  crlfDelay: Infinity,
});

function finishElement() {
  if (elementType === 'node' && currentId !== null && sawTrafficSignal) {
    signalNodes.add(currentId);
  }
  if (elementType === 'way' && isHighway && wayNodes.length > 0) {
    for (const id of wayNodes) {
      nodeUseCount.set(id, (nodeUseCount.get(id) ?? 0) + 1);
    }
    // Nur Endpunkte erzeugen im Generator einen Kreuzungsarm.
    for (const id of [wayNodes[0], wayNodes[wayNodes.length - 1]]) {
      nodeEndpointCount.set(id, (nodeEndpointCount.get(id) ?? 0) + 1);
    }
  }
  elementType = null;
  currentId = null;
  sawTrafficSignal = false;
  isHighway = false;
  wayNodes = [];
}

for await (const line of rl) {
  const trimmed = line.trim();

  if (trimmed === '{') { finishElement(); continue; }

  let m;
  if ((m = trimmed.match(/^"type":\s*"(\w+)"/))) { elementType = m[1]; continue; }
  if ((m = trimmed.match(/^"id":\s*(\d+)/))) { currentId = Number(m[1]); continue; }

  if (trimmed.startsWith('"tags"')) { inTags = true; inNodes = false; }
  if (trimmed.startsWith('"nodes"')) { inNodes = true; inTags = false; }

  if (inTags && trimmed.includes('"traffic_signals"')) { sawTrafficSignal = true; }
  if (inTags && /^"highway":/.test(trimmed) && elementType === 'way') { isHighway = true; }

  if (inNodes && elementType === 'way') {
    for (const num of trimmed.match(/\d+/g) ?? []) { wayNodes.push(Number(num)); }
    if (trimmed.includes(']')) { inNodes = false; }
  }
}
finishElement();

let onRoad = 0;
let atJunction = 0;
let asEndpoint3 = 0;
for (const id of signalNodes) {
  const uses = nodeUseCount.get(id) ?? 0;
  if (uses >= 1) { ++onRoad; }
  if (uses >= 2) { ++atJunction; }
  if ((nodeEndpointCount.get(id) ?? 0) >= 3) { ++asEndpoint3; }
}

console.log(`Ampelknoten gesamt:            ${signalNodes.size}`);
console.log(`davon auf einer Strasse:       ${onRoad}`);
console.log(`davon von >= 2 Strassen geteilt: ${atJunction}`);
console.log(`davon Endpunkt von >= 3 Strassen: ${asEndpoint3}   <- NUR diese werden Kreuzungen`);
