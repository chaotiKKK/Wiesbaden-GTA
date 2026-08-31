#!/usr/bin/env node
// overpass_fetch.mjs — Offline-Datenbeschaffung des OSM-Datensatzes.
//
// Ersetzt das in UOSMDataParser::BuildOverpassQuery referenzierte
// overpass_fetch.py: `python` ist auf diesem Rechner ein Windows-Store-Alias
// (funktioniert nicht), node (v24) ist verfuegbar.
//
// Baut EXAKT die Query aus UOSMDataParser::BuildOverpassQuery (Spezifikation
// 4.1) und speichert das Ergebnis als Overpass-JSON unter Data/Raw/OSM/
// (gitignored). Die Ablage- und Format-Konventionen stehen in Tools/README.md.
//
// Verwendung:
//   node overpass_fetch.mjs [--out <pfad>] [--bbox south,west,north,east] [--timeout <s>]
// Default-BBox: Wiesbaden inkl. Vororte (UGeoCoordinateConverter::GetWiesbadenBounds).

import { writeFileSync, renameSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ToolDir = dirname(fileURLToPath(import.meta.url));
const DefaultOut = resolve(ToolDir, '../Data/Raw/OSM/wiesbaden.osm.json');

// Overpass-Nutzungsbedingungen verlangen einen identifizierbaren User-Agent.
const UserAgent = 'WiesbadenReal-DataFetcher/0.1 (offline GIS-Datenbeschaffung; lokal)';

// Primär + Fallback-Mirror (overpass-api.de rate-limitt aggressiv, HTTP 429).
const Endpoints = [
  'https://overpass-api.de/api/interpreter',
  'https://overpass.kumi.systems/api/interpreter',
];

const args = process.argv.slice(2);
const opt = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
};

const OutPath = opt('--out', DefaultOut);
const TimeoutSeconds = Number(opt('--timeout', '600'));
const BBox = opt('--bbox', '49.995,8.08,50.16,8.42'); // south,west,north,east

// Identisch zu UOSMDataParser::BuildOverpassQuery — bei Aenderungen dort
// hier synchron halten.
// WICHTIG: KEINE relation["type"="route"]-Abfrage! Der Recurse ">;" zieht die
// gesamten Mitglieds-Geometrien der Linien in die Antwort — eine Bus-/Zuglinie,
// die Wiesbaden beruehrt, bringt ihre komplette Strecke (teils bis Barcelona)
// mit. Die Pipeline konsumiert Route-Relations nicht (nur type=restriction).
function buildQuery() {
  return `[out:json][timeout:${TimeoutSeconds}][bbox:${BBox}];
(
  way["highway"];
  way["building"];
  way["building:part"];
  way["landuse"];
  way["natural"];
  way["waterway"];
  way["barrier"];
  way["railway"];
  way["bridge"];
  way["tunnel"];
  way["leisure"];
  way["amenity"];
  node["highway"~"traffic_signals|crossing|stop|give_way|bus_stop|street_lamp|turning_circle"];
  node["amenity"~"fuel|parking|restaurant|cafe|bank|pharmacy|hospital|police|fire_station"];
  node["shop"];
  node["public_transport"];
  node["natural"="tree"];
  relation["type"="multipolygon"]["building"];
  relation["type"="restriction"];
);
out body;
>;
out skel qt;`;
}

async function fetchOnce(endpoint, query, attempt) {
  // curl -d 'data=...': Overpass erwartet den Query-Parameter form-encoded.
  const body = new URLSearchParams({ data: query });
  const res = await fetch(endpoint, {
    method: 'POST',
    headers: { 'User-Agent': UserAgent, 'Content-Type': 'application/x-www-form-urlencoded' },
    body,
  });
  if (res.status === 429) {
    const retryAfter = Number(res.headers.get('retry-after') ?? 0);
    throw new RateLimitError(endpoint, res.status, retryAfter);
  }
  if (!res.ok) {
    throw new Error(`HTTP ${res.status} von ${endpoint} (Versuch ${attempt})`);
  }
  return res;
}

class RateLimitError extends Error {
  constructor(endpoint, status, retryAfterSeconds) {
    super(`Rate-Limit (HTTP ${status}) von ${endpoint}`);
    this.retryAfterSeconds = retryAfterSeconds;
  }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function main() {
  const query = buildQuery();
  console.log(`Query (${query.length} Zeichen) fuer BBox ${BBox}, Timeout ${TimeoutSeconds}s`);
  console.log(`Ziel: ${OutPath}`);

  let lastError = null;
  for (let attempt = 1; attempt <= 5; ++attempt) {
    for (const endpoint of Endpoints) {
      try {
        const res = await fetchOnce(endpoint, query, attempt);
        const text = await res.text();
        console.log(`Antwort empfangen: ${(text.length / (1024 * 1024)).toFixed(1)} MB von ${endpoint}`);

        const data = JSON.parse(text);
        if (!Array.isArray(data.elements)) {
          throw new Error('JSON enthaelt kein elements-Array — vermutlich Overpass-Fehlerseite');
        }
        const counts = { node: 0, way: 0, relation: 0 };
        for (const el of data.elements) counts[el.type] = (counts[el.type] ?? 0) + 1;
        if (counts.node === 0) {
          throw new Error('Overpass-JSON enthielt keine Nodes (leere Bounding-Box?)');
        }

        mkdirSync(dirname(OutPath), { recursive: true });
        const Tmp = `${OutPath}.tmp`;
        writeFileSync(Tmp, text, 'utf8');
        renameSync(Tmp, OutPath);

        console.log('OK: gespeichert.');
        console.log(`elements: ${data.elements.length} (${counts.node} nodes, ${counts.way} ways, ${counts.relation} relations)`);
        if (data.remark) console.log(`Hinweis der API: ${data.remark}`);
        return 0;
      } catch (err) {
        lastError = err;
        const wait = err instanceof RateLimitError && err.retryAfterSeconds > 0
          ? err.retryAfterSeconds * 1000
          : Math.min(30000, 5000 * Math.pow(2, attempt - 1));
        console.warn(`Versuch ${attempt} fehlgeschlagen: ${err.message} — warte ${(wait / 1000).toFixed(0)}s`);
        await sleep(wait);
      }
    }
  }
  console.error(`FEHLGESCHLAGEN nach 5 Versuchen: ${lastError?.message}`);
  return 1;
}

process.exit(await main());
