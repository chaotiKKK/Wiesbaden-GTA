#!/usr/bin/env node
// Vertragscheck fuer Content/Config/WeatherFXCatalog.json (headless, node).
//
// Prueft, ob die editorfaehige Blaupause der fuenf NS_-Wetter-Assets mit dem
// C++-Vertrag zusammenpasst:
//   1. userParameters je Typ == FWiesbadenWeatherFXParams::GetRequiredUserParameters
//   2. path-Feld je Asset == UWiesbadenWeatherFXComponent::GetDefaultAssetPath
//   3. Modulnamen der Emitter sind reale UE-5.8-Module (Engine-Inventar)
//   4. bounds: Fixed + Ausdehnung > 0 (HasUsableFixedBounds-Kriterium)
//   5. top-level requiredUserParameters == Vereinigung aller Typ-Vertraege
//
// Lauf: node Tools/verify_weatherfx_catalog.mjs  (Exit-Code 0 = alles konform)
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const DirName = path.dirname(fileURLToPath(import.meta.url));
const CatalogPath = path.join(DirName, '..', 'WiesbadenReal', 'Content', 'Config', 'WeatherFXCatalog.json');

// --- C++-Vertrag (Spiegel von World/WiesbadenWeatherFX) ----------------------
// FWiesbadenWeatherFXParams::GetRequiredUserParameters (praefixfrei).
const COMMON = ['WindSpeed', 'SunLightColor'];
const CONTRACT = {
  NS_WeatherRain:   [...COMMON, 'RainSpawnRate'],
  NS_WeatherSnow:   [...COMMON, 'SnowSpawnRate'],
  NS_WeatherFog:    [...COMMON, 'FogDensity'],
  NS_WeatherClouds: [...COMMON, 'CloudOpacity'],
  NS_WeatherStorm:  [...COMMON, 'LightningInterval', 'RainSpawnRate'],
};
const CONTRACT_BY_ID = { Rain: 'NS_WeatherRain', Snow: 'NS_WeatherSnow', Fog: 'NS_WeatherFog', Clouds: 'NS_WeatherClouds', Storm: 'NS_WeatherStorm' };

// UWiesbadenWeatherFXComponent::GetDefaultAssetPath.
const DEFAULT_PATH = (id) => `/Game/Niagara/${id}.${id}`;

// Reale UE-5.8-Module (gegen Engine/Plugins/FX/Niagara/Content/Modules verifiziert).
const ENGINE_MODULES = new Set([
  'SpawnRate', 'SpawnBurst_Instantaneous', 'AddVelocity', 'GravityForce',
  'Drag', 'Color', 'ParticleState', 'RibbonWidth',
]);

// --- Runner ----------------------------------------------------------------
let pass = 0, fail = 0;
function check(what, cond, detail = '') {
  if (cond) { pass++; console.log(`  OK   ${what}${detail ? '  [' + detail + ']' : ''}`); }
  else { fail++; console.log(`  FAIL ${what}${detail ? '  [' + detail + ']' : ''}`); }
}
const sorted = (arr) => [...arr].sort();

let data;
try {
  data = JSON.parse(fs.readFileSync(CatalogPath, 'utf8'));
} catch (e) {
  console.error(`Katalog nicht lesbar/ungueltig: ${e.message}`);
  process.exit(1);
}

console.log(`Katalog: ${CatalogPath}`);
check('JSON hat formatVersion', typeof data.formatVersion === 'number', String(data.formatVersion));
check('Genau 5 Assets', data.assets && data.assets.length === 5, String(data.assets?.length));

const ids = new Set(Object.keys(CONTRACT));
const seenPaths = new Set();
const allParams = new Set();

for (const asset of data.assets) {
  const section = `[${asset.id}]`;
  check(`${section} bekannte Id`, ids.has(asset.id));

  // 2) path == GetDefaultAssetPath
  const expectedPath = DEFAULT_PATH(asset.id);
  check(`${section} path == kanonischer Pfad`, asset.path === expectedPath, asset.path);
  check(`${section} path eindeutig`, !seenPaths.has(asset.path));
  seenPaths.add(asset.path);

  // 1) userParameters == Typ-Vertrag
  const catalogParams = (asset.userParameters || []).map((p) => p.name);
  const contractParams = CONTRACT[asset.id];
  const missing = contractParams.filter((n) => !catalogParams.includes(n));
  const extra = catalogParams.filter((n) => !contractParams.includes(n));
  check(`${section} userParameters == C++-Vertrag (${contractParams.join(', ')})`,
    missing.length === 0 && extra.length === 0,
    missing.length ? `fehlt: ${missing.join(', ')}` : extra.length ? `extra: ${extra.join(', ')}` : 'exakt');
  for (const p of asset.userParameters || []) {
    allParams.add(p.name);
    // Vertrag: Float-Parameter brauchen Typ+Default+Range (min/max);
    // LinearColor-Parameter (z. B. SunLightColor) nur Typ+Default (RGBA).
    if (p.type === 'LinearColor') {
      check(`${section} Param ${p.name}: Typ+Default(RGBA)`, p.type && Array.isArray(p.default) && p.default.length === 4);
    } else {
      check(`${section} Param ${p.name}: Typ+Default+Range`, p.type && typeof p.default !== 'undefined' && p.min !== undefined && p.max !== undefined);
    }
  }

  // 3) Modulnamen sind reale Engine-Module
  for (const emitter of asset.emitters || []) {
    for (const stage of ['spawnStage', 'updateStage']) {
      for (const m of emitter[stage] || []) {
        check(`${section} Modul '${m.module}' im UE-5.8-Inventar`, ENGINE_MODULES.has(m.module), `${emitter.name}/${stage}`);
      }
    }
  }

  // 4) bounds: Fixed + Ausdehnung > 0 (HasUsableFixedBounds-Kriterium)
  const b = asset.bounds || {};
  check(`${section} bounds: Fixed + Center`, b.type === 'Fixed' && Array.isArray(b.center) && b.center.length === 3);
  check(`${section} bounds: Ausdehnung > 0`, Array.isArray(b.extent) && b.extent.length === 3 && b.extent.every((v) => v > 0),
    `extent ${b.extent?.join('x')} cm`);
}

// 5) top-level requiredUserParameters == Vereinigung
const union = sorted([...new Set(Object.values(CONTRACT).flat())]);
check('requiredUserParameters == Vereinigung aller Typ-Vertraege',
  sorted(data.requiredUserParameters || []).join(',') === union.join(','), union.join(', '));

// 6) Pfad-/Id-Konsistenz: jeder Contract-Id hat ein Asset
for (const id of ids) {
  check(`Asset ${id} vorhanden`, data.assets.some((a) => a.id === id));
}

console.log(`\nErgebnis: ${pass}/${pass + fail} PASS`);
process.exit(fail === 0 ? 0 : 1);
