#!/usr/bin/env node
/**
 * Extrahiert Gebaeudegrundrisse aus amtlichen ALKIS-NAS-Daten und schreibt sie
 * in der Overpass-JSON-Form heraus.
 *
 * WARUM DIESE FORM: Der vorhandene UOSMDataParser liest Overpass-JSON, und der
 * UBuildingGenerator baut daraus Gebaeude. Indem die ALKIS-Daten in dieselbe
 * Form gebracht werden, ist auf der Engine-Seite kein einziger neuer Parser
 * noetig - die komplette bestehende Kette (Triangulierung, Daecher, Loecher,
 * Materialwahl, Einebnung, Chunking) gilt unveraendert.
 *
 * WARUM NODE: Die Quelldatei ist mehrere Gigabyte gross. Unreal wuerde sie als
 * UTF-16-FString mit doppeltem Speicherbedarf laden; FFastXml arbeitet zwar
 * callback-basiert, braucht den Inhalt aber am Stueck. Node streamt die Datei
 * in Bloecken und haelt nur das aktuelle Gebaeude im Speicher.
 *
 * Aufruf:
 *   node Tools/alkis_extract.mjs <alkis.xml> <ausgabe.json> [--limit N] [--bbox w,s,e,n]
 */

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

// --------------------------------------------------------------------------
// UTM -> geographisch (ETRS89/UTM32 -> WGS84)
//
// Zahlengleiche Portierung von Source/WiesbadenReal/GIS/UtmCoordinateConverter.cpp.
// Aendert sich dort die Mathematik, muss sie hier nachgezogen werden - beide
// Seiten muessen dieselben Koordinaten liefern, sonst liegen ALKIS-Gebaeude
// und in-Engine berechnete Positionen versetzt zueinander.
// --------------------------------------------------------------------------

const A = 6378137.0;                 // grosse Halbachse GRS80/WGS84
const E_SQ = 6.69437999014e-3;       // erste Exzentrizitaet^2
const E_PRIME_SQ = E_SQ / (1.0 - E_SQ);
const K0 = 0.9996;                   // Massstabsfaktor
const FALSE_EASTING = 500000.0;

function zoneCentralMeridian(zone) {
  return zone * 6.0 - 183.0;
}

function utmToGeographic(easting, northing, zone) {
  const x = easting - FALSE_EASTING;
  const y = northing;

  const M = y / K0;
  const mu = M / (A * (1 - E_SQ / 4 - (3 * E_SQ ** 2) / 64 - (5 * E_SQ ** 3) / 256));

  const sqrtOneMinusE = Math.sqrt(1 - E_SQ);
  const e1 = (1 - sqrtOneMinusE) / (1 + sqrtOneMinusE);
  const e1_2 = e1 * e1;
  const e1_3 = e1_2 * e1;
  const e1_4 = e1_3 * e1;

  const phi1 =
    mu +
    ((3 * e1) / 2 - (27 * e1_3) / 32) * Math.sin(2 * mu) +
    ((21 * e1_2) / 16 - (55 * e1_4) / 32) * Math.sin(4 * mu) +
    ((151 * e1_3) / 96) * Math.sin(6 * mu) +
    ((1097 * e1_4) / 512) * Math.sin(8 * mu);

  const sinPhi1 = Math.sin(phi1);
  const cosPhi1 = Math.cos(phi1);
  const tanPhi1 = Math.tan(phi1);

  const C1 = E_PRIME_SQ * cosPhi1 * cosPhi1;
  const T1 = tanPhi1 * tanPhi1;
  const oneMinusESinSq = 1 - E_SQ * sinPhi1 * sinPhi1;
  const N1 = A / Math.sqrt(oneMinusESinSq);
  const R1 = (A * (1 - E_SQ)) / (oneMinusESinSq * Math.sqrt(oneMinusESinSq));

  const D = x / (N1 * K0);
  const D2 = D * D, D3 = D2 * D, D4 = D3 * D, D5 = D4 * D, D6 = D5 * D;
  const C1_2 = C1 * C1;
  const T1_2 = T1 * T1;

  const latRad =
    phi1 -
    ((N1 * tanPhi1) / R1) *
      (D2 / 2 -
        ((5 + 3 * T1 + 10 * C1 - 4 * C1_2 - 9 * E_PRIME_SQ) * D4) / 24 +
        ((61 + 90 * T1 + 298 * C1 + 45 * T1_2 - 252 * E_PRIME_SQ - 3 * C1_2) * D6) / 720);

  const lonOffsetRad =
    (D -
      ((1 + 2 * T1 + C1) * D3) / 6 +
      ((5 - 2 * C1 + 28 * T1 - 3 * C1_2 + 8 * E_PRIME_SQ + 24 * T1_2) * D5) / 120) /
    cosPhi1;

  return {
    lon: zoneCentralMeridian(zone) + (lonOffsetRad * 180) / Math.PI,
    lat: (latRad * 180) / Math.PI,
  };
}

// --------------------------------------------------------------------------
// ALKIS-Gebaeudefunktion -> OSM-building-Wert
//
// Schluessel nach AAA-Objektartenkatalog (AX_Gebaeude_Gebaeudefunktion). Die
// Zuordnung ist bewusst grob: der BuildingGenerator unterscheidet ohnehin nur
// eine Handvoll Typen, und eine feinere Abbildung wuerde Genauigkeit
// vortaeuschen, die die Darstellung nicht einloest.
// --------------------------------------------------------------------------
const FUNCTION_TO_OSM = {
  1000: 'residential',      // Wohngebaeude
  1010: 'house',            // Wohnhaus
  1020: 'residential',      // Wohnheim
  1021: 'apartments',       // Kinderheim
  1024: 'apartments',       // Studenten-/Schuelerwohnheim
  1120: 'house',            // Wohngebaeude mit Gewerbe
  1220: 'commercial',
  2000: 'commercial',       // Gebaeude fuer Wirtschaft oder Gewerbe
  2010: 'industrial',       // Produktion
  2020: 'industrial',       // Werkstatt
  2050: 'industrial',
  2070: 'warehouse',        // Lagerhalle
  2071: 'warehouse',
  2081: 'industrial',
  2100: 'retail',           // Handel und Dienstleistung
  2110: 'retail',
  2120: 'retail',
  2143: 'retail',
  2170: 'commercial',
  2310: 'industrial',       // Versorgungsanlage
  2460: 'commercial',       // Betriebsgebaeude
  2463: 'garage',
  2512: 'garage',           // Garage
  2513: 'garage',
  2523: 'garage',           // Garage/Carport
  2700: 'commercial',
  2740: 'industrial',       // Gebaeude fuer Versorgungsanlage
  3000: 'civic',            // Gebaeude fuer oeffentliche Zwecke
  3010: 'civic',            // Verwaltung
  3012: 'civic',            // Rathaus
  3020: 'civic',
  3040: 'school',           // Bildung und Forschung
  3041: 'university',
  3042: 'school',
  3044: 'school',
  3050: 'civic',            // Kultur
  3060: 'civic',
  3065: 'civic',            // Museum
  3070: 'church',           // Religioese Zwecke
  3071: 'church',           // Kirche
  3072: 'church',           // Synagoge
  3073: 'church',           // Kapelle
  3075: 'church',           // Moschee
  3080: 'hospital',         // Gesundheitswesen
  3090: 'civic',            // Soziale Zwecke
  3100: 'civic',            // Sicherheit und Ordnung
  3110: 'civic',            // Polizei
  3120: 'civic',            // Feuerwehr
  3200: 'civic',
  3210: 'train_station',    // Verkehrswesen
  3211: 'train_station',
  3281: 'civic',
  9998: 'yes',              // nach Quellenlage nicht zu spezifizieren
};

function osmBuildingValue(code) {
  return FUNCTION_TO_OSM[code] ?? 'yes';
}

// --------------------------------------------------------------------------
// Streaming-Extraktion
// --------------------------------------------------------------------------

// --------------------------------------------------------------------------
// Semantik aus OSM uebernehmen
//
// ALKIS liefert Geometrie, aber keine Namen: der Katasterdatensatz kennt
// "Gebaeudefunktion 3071 (Kirche)", nicht "Marktkirche". Damit verlieren
// Landmarken-Erkennung und GPS-Ziele ihre Grundlage.
//
// Loesung: raeumlicher Abgleich. Fuer jedes ALKIS-Gebaeude wird das OSM-Gebaeude
// mit dem naechstgelegenen Schwerpunkt gesucht; liegt es nah genug, wandern
// dessen beschreibende Tags (Name, Adresse, Nutzung) auf das ALKIS-Gebaeude.
// Ergebnis: amtliche Geometrie mit der Semantik der Freiwilligenkartierung.
// --------------------------------------------------------------------------

/** Tags, die aus OSM uebernommen werden. Geometrie-Tags bleiben aussen vor. */
const SEMANTIC_TAGS = [
  'name', 'addr:street', 'addr:housenumber', 'addr:postcode', 'addr:city',
  'amenity', 'shop', 'tourism', 'office', 'historic', 'operator',
  'building:levels', 'height', 'roof:shape', 'building:material', 'start_date',
];

/** Zellgroesse des Suchgitters in Grad (rund 60 m). */
const GRID_CELL_DEG = 0.0006;

function gridKey(lon, lat) {
  return `${Math.floor(lon / GRID_CELL_DEG)}:${Math.floor(lat / GRID_CELL_DEG)}`;
}

/**
 * Liest die OSM-Datei in zwei Durchlaeufen und liefert ein Suchgitter der
 * Gebaeudeschwerpunkte mit ihren beschreibenden Tags.
 *
 * Zwei Durchlaeufe, weil Overpass erst alle Knoten und dann die Ways ausgibt:
 * welche Knoten gebraucht werden, steht erst nach dem Lesen der Ways fest.
 * Die Alternative - alle 1,1 Mio Knoten im Speicher halten - waere deutlich
 * teurer als die Datei ein zweites Mal zu lesen.
 */
async function buildOsmSemanticIndex(osmPath) {
  const wayNodes = new Map();   // wayId -> [nodeId]
  const wayTags = new Map();    // wayId -> {tag: wert}
  const neededNodes = new Set();

  const readLines = async (onLine) => {
    const stream = fs.createReadStream(osmPath, { encoding: 'utf8', highWaterMark: 1 << 22 });
    let rest = '';
    for await (const chunk of stream) {
      const lines = (rest + chunk).split('\n');
      rest = lines.pop();
      for (const line of lines) onLine(line);
    }
    if (rest) onLine(rest);
  };

  // Durchlauf 1: Gebaeude-Ways mit beschreibenden Tags einsammeln.
  {
    let current = null;
    let inNodes = false;

    await readLines((line) => {
      const t = line.trim();

      if (t.startsWith('"type": "way"')) {
        current = { id: 0, nodes: [], tags: {} };
        return;
      }
      if (t.startsWith('"type": "node"') || t.startsWith('"type": "relation"')) {
        current = null;
        return;
      }
      if (!current) return;

      const idM = /^"id":\s*(\d+)/.exec(t);
      if (idM) { current.id = Number(idM[1]); return; }

      if (t.startsWith('"nodes"')) { inNodes = true; return; }
      if (inNodes) {
        const n = /^(\d+),?$/.exec(t);
        if (n) { current.nodes.push(Number(n[1])); return; }
        if (t.startsWith(']')) { inNodes = false; return; }
      }

      const tagM = /^"([^"]+)":\s*"([^"]*)"/.exec(t);
      if (tagM) {
        current.tags[tagM[1]] = tagM[2];
        // Ende des Elements erkennen wir am Auftreten von "building" o. ae.
      }

      if (t === '},' || t === '}') {
        if (current.id && current.nodes.length >= 3 && current.tags.building) {
          const kept = {};
          let any = false;
          for (const k of SEMANTIC_TAGS) {
            if (current.tags[k] !== undefined) { kept[k] = current.tags[k]; any = true; }
          }
          if (any) {
            wayNodes.set(current.id, current.nodes);
            wayTags.set(current.id, kept);
            for (const n of current.nodes) neededNodes.add(n);
          }
        }
        current = null;
      }
    });
  }

  // Durchlauf 2: Koordinaten der gebrauchten Knoten.
  const nodeCoords = new Map();
  {
    let curId = 0;
    let curLat = null;
    let curLon = null;

    await readLines((line) => {
      const t = line.trim();
      const idM = /^"id":\s*(\d+)/.exec(t);
      if (idM) { curId = Number(idM[1]); curLat = null; curLon = null; return; }
      const latM = /^"lat":\s*(-?[\d.]+)/.exec(t);
      if (latM) { curLat = Number(latM[1]); return; }
      const lonM = /^"lon":\s*(-?[\d.]+)/.exec(t);
      if (lonM) {
        curLon = Number(lonM[1]);
        if (curId && curLat !== null && neededNodes.has(curId)) {
          nodeCoords.set(curId, [curLon, curLat]);
        }
      }
    });
  }

  // Schwerpunkte bilden und ins Gitter einhaengen.
  const grid = new Map();
  let indexed = 0;

  for (const [wayId, nodes] of wayNodes) {
    let sx = 0, sy = 0, n = 0;
    for (const nid of nodes) {
      const c = nodeCoords.get(nid);
      if (c) { sx += c[0]; sy += c[1]; n += 1; }
    }
    if (n === 0) continue;

    const lon = sx / n;
    const lat = sy / n;
    const key = gridKey(lon, lat);

    if (!grid.has(key)) grid.set(key, []);
    grid.get(key).push({ lon, lat, tags: wayTags.get(wayId) });
    indexed += 1;
  }

  console.log(`  OSM-Semantik: ${indexed} Gebaeude mit beschreibenden Tags indiziert`);
  return grid;
}

/** Sucht die naechstgelegene OSM-Semantik zu einem Punkt. */
function lookupSemantics(grid, lon, lat, maxMeters) {
  const cx = Math.floor(lon / GRID_CELL_DEG);
  const cy = Math.floor(lat / GRID_CELL_DEG);

  // Grad -> Meter bei 50 Grad Breite.
  const mPerDegLat = 111320;
  const mPerDegLon = 71700;

  let best = null;
  let bestDistSq = maxMeters * maxMeters;

  for (let dx = -1; dx <= 1; dx += 1) {
    for (let dy = -1; dy <= 1; dy += 1) {
      const cell = grid.get(`${cx + dx}:${cy + dy}`);
      if (!cell) continue;

      for (const entry of cell) {
        const ex = (entry.lon - lon) * mPerDegLon;
        const ey = (entry.lat - lat) * mPerDegLat;
        const d = ex * ex + ey * ey;
        if (d < bestDistSq) { bestDistSq = d; best = entry.tags; }
      }
    }
  }

  return best;
}

function parseArgs(argv) {
  const args = { limit: 0, bbox: null, mergeOsm: '', mergeRadius: 20 };
  const positional = [];

  for (let i = 2; i < argv.length; i += 1) {
    const a = argv[i];
    if (a === '--limit') {
      args.limit = Number.parseInt(argv[++i], 10) || 0;
    } else if (a === '--merge-osm') {
      args.mergeOsm = argv[++i] || '';
    } else if (a === '--merge-radius') {
      args.mergeRadius = Number.parseFloat(argv[++i]) || 20;
    } else if (a === '--bbox') {
      const parts = (argv[++i] || '').split(',').map(Number);
      if (parts.length === 4 && parts.every(Number.isFinite)) {
        args.bbox = { w: parts[0], s: parts[1], e: parts[2], n: parts[3] };
      }
    } else {
      positional.push(a);
    }
  }

  args.input = positional[0];
  args.output = positional[1];
  return args;
}

async function main() {
  const args = parseArgs(process.argv);

  if (!args.input || !args.output) {
    console.error('Aufruf: node Tools/alkis_extract.mjs <alkis.xml> <ausgabe.json> [--limit N] [--bbox w,s,e,n]');
    process.exit(2);
  }

  if (!fs.existsSync(args.input)) {
    console.error(`Eingabedatei fehlt: ${args.input}`);
    process.exit(2);
  }

  const totalBytes = fs.statSync(args.input).size;
  console.log(`ALKIS-Extraktion: ${(totalBytes / 1024 / 1024).toFixed(0)} MB`);

  let semanticGrid = null;
  if (args.mergeOsm) {
    if (!fs.existsSync(args.mergeOsm)) {
      console.error(`OSM-Datei fuer den Tag-Abgleich fehlt: ${args.mergeOsm}`);
      process.exit(2);
    }
    console.log(`OSM-Semantik wird eingelesen (${args.mergeOsm})`);
    semanticGrid = await buildOsmSemanticIndex(args.mergeOsm);
  }

  const out = fs.createWriteStream(args.output, { encoding: 'utf8' });
  out.write('{\n  "version": 0.6,\n  "generator": "alkis_extract.mjs",\n  "elements": [\n');

  // Synthetische IDs. Weit oberhalb realer OSM-IDs (< 1.5e10), damit sich
  // ALKIS- und OSM-Daten im selben Datensatz nicht ueberschreiben.
  const ID_BASE = 900000000000;
  let nextNodeId = ID_BASE;
  let nextWayId = ID_BASE;

  let nextRelationId = ID_BASE;

  let buildingCount = 0;
  let courtyardCount = 0;
  let mergedCount = 0;
  let skippedDegenerate = 0;
  let skippedOutOfBbox = 0;
  let firstElement = true;

  // Zustand des aktuellen Gebaeudes.
  let inBuilding = false;
  let inInterior = false;
  let currentId = '';
  let currentFunction = 0;
  let currentRing = [];
  let exteriorRing = [];
  let interiorRings = [];

  let bytesRead = 0;
  let lastReport = 0;

  const stream = fs.createReadStream(args.input, { encoding: 'utf8', highWaterMark: 1 << 22 });
  let buffer = '';

  const flushBuilding = () => {
    if (exteriorRing.length < 4) {
      skippedDegenerate += 1;
      return;
    }

    const coords = exteriorRing.map(([e, n]) => utmToGeographic(e, n, 32));

    if (args.bbox) {
      const inside = coords.some(
        (c) => c.lon >= args.bbox.w && c.lon <= args.bbox.e && c.lat >= args.bbox.s && c.lat <= args.bbox.n,
      );
      if (!inside) {
        skippedOutOfBbox += 1;
        return;
      }
    }

    const emit = (text) => {
      out.write(`${firstElement ? '' : ',\n'}    ${text}`);
      firstElement = false;
    };

    // Schreibt einen Ring als Knotenfolge + Way heraus, liefert die Way-Id.
    const writeRing = (ringCoords, tags) => {
      const nodeIds = [];

      for (const c of ringCoords) {
        const id = nextNodeId++;
        nodeIds.push(id);
        emit(`{"type":"node","id":${id},"lat":${c.lat.toFixed(7)},"lon":${c.lon.toFixed(7)}}`);
      }

      // Ring schliessen: OSM erwartet ersten == letzten Knoten.
      if (nodeIds[0] !== nodeIds[nodeIds.length - 1]) {
        nodeIds.push(nodeIds[0]);
      }

      const wayId = nextWayId++;
      const tagPart = tags ? `,"tags":${JSON.stringify(tags)}` : '';
      emit(`{"type":"way","id":${wayId},"nodes":[${nodeIds.join(',')}]${tagPart}}`);
      return wayId;
    };

    const buildingTags = {
      building: osmBuildingValue(currentFunction),
      'alkis:id': currentId,
      'alkis:gebaeudefunktion': String(currentFunction),
      source: 'ALKIS',
    };

    // Semantik aus OSM ergaenzen, wenn ein Gebaeude dort wiedergefunden wird.
    if (semanticGrid) {
      let sx = 0, sy = 0;
      for (const c of coords) { sx += c.lon; sy += c.lat; }
      const cLon = sx / coords.length;
      const cLat = sy / coords.length;

      const found = lookupSemantics(semanticGrid, cLon, cLat, args.mergeRadius);
      if (found) {
        // Die ALKIS-Gebaeudefunktion bleibt fuehrend: sie ist amtlich, das
        // OSM-building-Tag geraten. Uebernommen wird nur, was ALKIS nicht hat.
        for (const [k, v] of Object.entries(found)) {
          if (buildingTags[k] === undefined) buildingTags[k] = v;
        }
        mergedCount += 1;
      }
    }

    // Innenringe (Innenhoefe) in Weltkoordinaten. Zu kleine verwerfen - der
    // Gebaeudegenerator tut das ohnehin, und ein Ring aus zwei Punkten
    // destabilisiert nur die Brueckenbildung bei der Triangulierung.
    const usableInterior = interiorRings.filter((r) => r.length >= 3);

    if (usableInterior.length === 0) {
      // Einfaches Gebaeude: ein Way mit den Gebaeude-Tags genuegt.
      writeRing(coords, buildingTags);
    } else {
      // Gebaeude mit Innenhof: als type=multipolygon herausschreiben.
      // Der vorhandene UBuildingGenerator setzt solche Relationen bereits
      // zusammen (AssembleMultipolygon) und ueberspringt die Member-Ways,
      // damit im Hof kein zweites Haus steht. Ohne diesen Weg waeren
      // Gruenderzeit-Bloecke massive Kloetze statt Ringbebauung.
      const outerWayId = writeRing(coords, null);

      const memberParts = [`{"type":"way","ref":${outerWayId},"role":"outer"}`];

      for (const ring of usableInterior) {
        const innerCoords = ring.map(([e, n]) => utmToGeographic(e, n, 32));
        const innerWayId = writeRing(innerCoords, null);
        memberParts.push(`{"type":"way","ref":${innerWayId},"role":"inner"}`);
      }

      const relTags = { ...buildingTags, type: 'multipolygon' };
      const relId = nextRelationId++;
      emit(
        `{"type":"relation","id":${relId},"members":[${memberParts.join(',')}],"tags":${JSON.stringify(relTags)}}`,
      );

      courtyardCount += 1;
    }

    buildingCount += 1;
  };

  const RE_GEBAEUDE_OPEN = /<adv:AX_Gebaeude\s+gml:id="([^"]+)"/g;
  const RE_TOKEN = /<(\/?)(adv:AX_Gebaeude|gml:exterior|gml:interior)\b|<adv:gebaeudefunktion>(\d+)<|<gml:posList[^>]*>([^<]*)</g;

  for await (const chunk of stream) {
    bytesRead += Buffer.byteLength(chunk, 'utf8');
    buffer += chunk;

    // Bis zur letzten sicher abgeschlossenen Stelle verarbeiten; der Rest
    // bleibt im Puffer, damit ueber Blockgrenzen gespaltene Tags nicht
    // zerfallen.
    const safeEnd = buffer.lastIndexOf('>');
    if (safeEnd < 0) continue;

    const work = buffer.slice(0, safeEnd + 1);
    buffer = buffer.slice(safeEnd + 1);

    RE_TOKEN.lastIndex = 0;
    let m;
    while ((m = RE_TOKEN.exec(work)) !== null) {
      const closing = m[1] === '/';
      const tag = m[2];

      if (tag === 'adv:AX_Gebaeude') {
        if (closing) {
          if (inBuilding) flushBuilding();
          inBuilding = false;
          exteriorRing = [];
          interiorRings = [];
          currentFunction = 0;
          currentId = '';
        } else {
          inBuilding = true;
          exteriorRing = [];
          interiorRings = [];
          currentRing = [];
          currentFunction = 0;

          // gml:id steht als Attribut im selben Tag.
          RE_GEBAEUDE_OPEN.lastIndex = Math.max(0, m.index);
          const idMatch = RE_GEBAEUDE_OPEN.exec(work);
          currentId = idMatch && idMatch.index === m.index ? idMatch[1] : '';
        }
        continue;
      }

      if (tag === 'gml:exterior' || tag === 'gml:interior') {
        if (closing) {
          if (currentRing.length >= 3) {
            if (inInterior) interiorRings.push(currentRing);
            else exteriorRing = currentRing;
          }
          currentRing = [];
        } else {
          inInterior = tag === 'gml:interior';
          currentRing = [];
        }
        continue;
      }

      if (m[3] !== undefined) {
        currentFunction = Number.parseInt(m[3], 10) || 0;
        continue;
      }

      if (m[4] !== undefined && inBuilding) {
        const nums = m[4].trim().split(/\s+/);
        for (let i = 0; i + 1 < nums.length; i += 2) {
          const e = Number.parseFloat(nums[i]);
          const n = Number.parseFloat(nums[i + 1]);
          if (!Number.isFinite(e) || !Number.isFinite(n)) continue;

          // Aufeinanderfolgende Segmente teilen sich ihre Endpunkte -
          // Duplikate hier verwerfen statt spaeter in der Engine.
          const last = currentRing[currentRing.length - 1];
          if (!last || Math.abs(last[0] - e) > 1e-4 || Math.abs(last[1] - n) > 1e-4) {
            currentRing.push([e, n]);
          }
        }
      }
    }

    if (args.limit > 0 && buildingCount >= args.limit) break;

    if (bytesRead - lastReport > 200 * 1024 * 1024) {
      lastReport = bytesRead;
      const pct = ((bytesRead / totalBytes) * 100).toFixed(1);
      console.log(`  ${pct} % — ${buildingCount} Gebaeude`);
    }
  }

  if (inBuilding) flushBuilding();

  out.write('\n  ]\n}\n');
  await new Promise((resolve) => out.end(resolve));

  const outSize = fs.statSync(args.output).size;
  console.log('---');
  console.log(`Gebaeude:        ${buildingCount}`);
  console.log(`davon mit Innenhof (multipolygon): ${courtyardCount}`);
  if (args.mergeOsm) console.log(`Semantik aus OSM uebernommen: ${mergedCount}`);
  console.log(`verworfen (Geometrie): ${skippedDegenerate}`);
  if (args.bbox) console.log(`verworfen (ausserhalb Bbox): ${skippedOutOfBbox}`);
  console.log(`Ausgabe:         ${path.basename(args.output)} (${(outSize / 1024 / 1024).toFixed(1)} MB)`);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
