#!/usr/bin/env node
// verify_roadtypes.mjs
// ---------------------------------------------------------------------------
// Node-Port der URoadTypeLibrary (GIS/RoadTypeLibrary.cpp): ApplyBuiltInDefaults
// (RASt 06/RAA-Defaults) + LoadFromJsonFile (JSON-Overlay mit Guard-Bedingungen,
// Fallback auf Residential). Der Port bildet das C++-Verhalten 1:1 ab - eine
// Aenderung an der Default-Tabelle, an den JSON-Feldnamen oder an den
// Guard-Bedingungen im C++ MUSS hier mitgezogen werden (Inventar-Drift-Guard
// unten extrahiert die C++-Literale direkt aus der Quelle).
//
// Nutzung:
//   node Tools/verify_roadtypes.mjs                    # Verify-Suite (Exit 0/1)
//   node Tools/verify_roadtypes.mjs --json             # maschinenlesbar
//   node Tools/verify_roadtypes.mjs --parse [pfad]     # Katalog parsen/ausgeben
//                                                       (Default: Content/Config/
//                                                        WiesbadenRoadTypes.json)
//
// Exit-Code: 0 = Vertrag erfuellt, 1 = Abweichung.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_CONFIG = path.resolve(SCRIPT_DIR, '..', 'WiesbadenReal', 'Content', 'Config', 'WiesbadenRoadTypes.json');
const CPP_SOURCE = path.resolve(SCRIPT_DIR, '..', 'WiesbadenReal', 'Source', 'WiesbadenReal', 'GIS', 'RoadTypeLibrary.cpp');

// ---------------------------------------------------------------------------
// Port des Parsers (muss RoadTypeLibrary.cpp semantisch 1:1 entsprechen)
// ---------------------------------------------------------------------------

/** ParseHighwayType-Spiegel: JSON-Schluessel -> kanonischer Typname (null = None). */
const HIGHWAY_TYPE_MAP = Object.freeze({
	motorway: 'motorway',
	motorway_link: 'motorway_link',
	trunk: 'trunk',
	trunk_link: 'trunk_link',
	primary: 'primary',
	primary_link: 'primary_link',
	secondary: 'secondary',
	secondary_link: 'secondary_link',
	tertiary: 'tertiary',
	tertiary_link: 'tertiary_link',
	unclassified: 'unclassified',
	residential: 'residential',
	living_street: 'living_street',
	service: 'service',
	pedestrian: 'pedestrian',
	footway: 'footway',
	cycleway: 'cycleway',
	path: 'path',
	steps: 'steps',
	track: 'track',
	// Haeufige Synonyme und Altbestand in deutschen OSM-Daten (wie C++):
	bridleway: 'path',
	corridor: 'footway',
	busway: 'service'
});

/** ParseSurfaceType-Spiegel: Wert -> kanonischer Oberflaechenname ('' = Unknown). */
const SURFACE_TYPE_MAP = Object.freeze({
	asphalt: 'asphalt',
	paved: 'asphalt',
	concrete: 'concrete',
	'concrete:plates': 'concrete',
	'concrete:lanes': 'concrete',
	paving_stones: 'paving_stones',
	sett: 'sett',
	cobblestone: 'cobblestone',
	unhewn_cobblestone: 'cobblestone',
	gravel: 'gravel',
	fine_gravel: 'gravel',
	compacted: 'compacted',
	unpaved: 'compacted',
	ground: 'ground',
	dirt: 'ground',
	earth: 'ground',
	sand: 'ground',
	grass: 'grass',
	grass_paver: 'grass',
	wood: 'wood',
	metal: 'metal'
});

/** Wert -> kanonischer Typname (TrimStartAndEnd + ToLower wie C++). */
function parseHighwayType(value)
{
	const n = String(value).trim().toLowerCase();
	return HIGHWAY_TYPE_MAP[n] || null;
}

/** Wert -> kanonischer Oberflaechenname ('unknown' wie EOSMSurfaceType::Unknown). */
function parseSurfaceType(value)
{
	const n = String(value).trim().toLowerCase();
	return SURFACE_TYPE_MAP[n] || 'unknown';
}

/**
 * RASt 06/RAA-Defaults - Spiegel der Add(...)-Zeilen in ApplyBuiltInDefaults.
 * [Typ, Spurbreite, Spuren/Richtung, Tempo, Gehwegbreite, Gehweg, Mittellinie,
 *  Prioritaet, Verkehrsdichte, KI-Verkehr]
 */
const DEFAULT_TABLE = Object.freeze([
	['motorway', 3.75, 2, 130.0, 0.0, false, true, 0, 1.00, true],
	['motorway_link', 3.75, 1, 80.0, 0.0, false, false, 1, 0.70, true],
	['trunk', 3.50, 2, 100.0, 0.0, false, true, 1, 0.90, true],
	['trunk_link', 3.50, 1, 60.0, 0.0, false, false, 2, 0.60, true],
	['primary', 3.50, 2, 50.0, 3.0, true, true, 2, 0.95, true],
	['primary_link', 3.50, 1, 50.0, 2.5, true, false, 3, 0.50, true],
	['secondary', 3.25, 1, 50.0, 2.5, true, true, 3, 0.75, true],
	['secondary_link', 3.25, 1, 50.0, 2.5, true, false, 4, 0.40, true],
	['tertiary', 3.25, 1, 50.0, 2.5, true, true, 4, 0.60, true],
	['tertiary_link', 3.25, 1, 50.0, 2.5, true, false, 5, 0.35, true],
	['unclassified', 3.00, 1, 50.0, 2.0, true, false, 6, 0.35, true],
	['residential', 2.75, 1, 30.0, 2.0, true, false, 7, 0.30, true],
	['living_street', 2.50, 1, 7.0, 0.0, false, false, 8, 0.15, true],
	['service', 2.50, 1, 20.0, 0.0, false, false, 9, 0.10, true],
	['pedestrian', 3.00, 1, 7.0, 0.0, false, false, 10, 0.00, false],
	['footway', 1.80, 1, 0.0, 0.0, false, false, 11, 0.00, false],
	['cycleway', 1.60, 1, 20.0, 0.0, false, false, 11, 0.00, false],
	['path', 1.50, 1, 0.0, 0.0, false, false, 12, 0.00, false],
	['steps', 1.50, 1, 0.0, 0.0, false, false, 12, 0.00, false],
	['track', 2.50, 1, 20.0, 0.0, false, false, 12, 0.05, false]
]);

/** DefaultSurface-Ueberrides (C++: Fussgaengerzonen gepflastert, Track/Path natuerlich). */
const DEFAULT_SURFACE_OVERRIDES = Object.freeze({
	pedestrian: 'paving_stones',
	footway: 'paving_stones',
	track: 'compacted',
	path: 'ground'
});

/** JSON-Feldnamen, die LoadFromJsonFile liest (inkl. Root-Feld 'roadTypes'). */
const JSON_FIELD_NAMES = Object.freeze([
	'laneWidthMeters',
	'defaultLanesPerDirection',
	'defaultMaxSpeedKmh',
	'sidewalkWidthMeters',
	'kerbHeightMeters',
	'cyclewayWidthMeters',
	'priority',
	'trafficDensityFactor',
	'sidewalkByDefault',
	'hasCenterLineMarking',
	'trafficEnabled',
	'roadMaterialPath',
	'sidewalkMaterialPath',
	'defaultSurface',
	'roadTypes'
]);

/** Leere FRoadTypeDefinition (C++-Member-Defaults). */
function newDefinition()
{
	return {
		laneWidthMeters: 3.25,
		defaultLanesPerDirection: 1,
		defaultMaxSpeedKmh: 50.0,
		sidewalkWidthMeters: 2.5,
		kerbHeightMeters: 0.12,
		cyclewayWidthMeters: 1.6,
		sidewalkByDefault: true,
		hasCenterLineMarking: true,
		priority: 5,
		roadMaterialPath: '',
		sidewalkMaterialPath: '',
		defaultSurface: 'asphalt',
		trafficEnabled: true,
		trafficDensityFactor: 0.5
	};
}

/** ApplyBuiltInDefaults-Spiegel. Liefert { definitions, fallback }. */
function applyBuiltInDefaults()
{
	const definitions = {};
	for (const [type, laneW, lanes, speed, sideW, bSide, bCenter, prio, density, traffic] of DEFAULT_TABLE)
	{
		const def = newDefinition();
		def.laneWidthMeters = laneW;
		def.defaultLanesPerDirection = lanes;
		def.defaultMaxSpeedKmh = speed;
		def.sidewalkWidthMeters = sideW;
		def.sidewalkByDefault = bSide;
		def.hasCenterLineMarking = bCenter;
		def.priority = prio;
		def.trafficDensityFactor = density;
		def.trafficEnabled = traffic;
		definitions[type] = def;
	}
	for (const [type, surface] of Object.entries(DEFAULT_SURFACE_OVERRIDES))
	{
		if (definitions[type])
		{
			definitions[type].defaultSurface = surface;
		}
	}
	return { definitions, fallback: definitions.residential };
}

/** GetDefinition-Spiegel: unbekannter Typ -> Residential-Fallback. */
function getDefinition(ctx, type)
{
	return ctx.definitions[type] || ctx.fallback;
}

/**
 * LoadFromJsonFile-Spiegel (JSON-Teil). Immer zuerst Defaults, dann ueberschreibt
 * die JSON nur die Felder, die sie tatsaechlich enthaelt (Guard-Bedingungen
 * identisch zum C++: >0.0 / >=1.0 + Clamp 1..6 / >=0.0 / Clamp 0..1 / Runden).
 * Liefert { ok, reason, overriddenCount, warnings, definitions, fallback }.
 */
function loadFromJsonString(jsonText)
{
	const ctx = applyBuiltInDefaults();
	const warnings = [];

	let root;
	try
	{
		root = JSON.parse(jsonText);
	}
	catch (err)
	{
		return { ok: false, reason: 'json', overriddenCount: 0, warnings, ...ctx };
	}

	if (!root || typeof root !== 'object' || !root.roadTypes || typeof root.roadTypes !== 'object')
	{
		return { ok: false, reason: 'noRoadTypes', overriddenCount: 0, warnings, ...ctx };
	}

	let overriddenCount = 0;
	for (const [key, entry] of Object.entries(root.roadTypes))
	{
		const type = parseHighwayType(key);
		if (!type)
		{
			warnings.push(`Unbekannter Strassentyp '${key}' in der Konfiguration - uebersprungen.`);
			continue;
		}
		if (!entry || typeof entry !== 'object')
		{
			continue;
		}

		const def = ctx.definitions[type] || (ctx.definitions[type] = newDefinition());
		const num = (field) => (typeof entry[field] === 'number' && Number.isFinite(entry[field]) ? entry[field] : undefined);
		const bool = (field) => (typeof entry[field] === 'boolean' ? entry[field] : undefined);
		const str = (field) => (typeof entry[field] === 'string' ? entry[field] : undefined);

		const laneWidth = num('laneWidthMeters');
		if (laneWidth !== undefined && laneWidth > 0.0) { def.laneWidthMeters = laneWidth; }
		const lanes = num('defaultLanesPerDirection');
		if (lanes !== undefined && lanes >= 1.0)
		{
			def.defaultLanesPerDirection = Math.min(6, Math.max(1, Math.round(lanes)));
		}
		const speed = num('defaultMaxSpeedKmh');
		if (speed !== undefined && speed >= 0.0) { def.defaultMaxSpeedKmh = speed; }
		const sidewalkWidth = num('sidewalkWidthMeters');
		if (sidewalkWidth !== undefined && sidewalkWidth >= 0.0) { def.sidewalkWidthMeters = sidewalkWidth; }
		const kerb = num('kerbHeightMeters');
		if (kerb !== undefined && kerb >= 0.0) { def.kerbHeightMeters = kerb; }
		const cycleway = num('cyclewayWidthMeters');
		if (cycleway !== undefined && cycleway >= 0.0) { def.cyclewayWidthMeters = cycleway; }
		const prio = num('priority');
		if (prio !== undefined) { def.priority = Math.round(prio); }
		const density = num('trafficDensityFactor');
		if (density !== undefined) { def.trafficDensityFactor = Math.min(1.0, Math.max(0.0, density)); }
		const bSidewalk = bool('sidewalkByDefault');
		if (bSidewalk !== undefined) { def.sidewalkByDefault = bSidewalk; }
		const bCenterLine = bool('hasCenterLineMarking');
		if (bCenterLine !== undefined) { def.hasCenterLineMarking = bCenterLine; }
		const bTraffic = bool('trafficEnabled');
		if (bTraffic !== undefined) { def.trafficEnabled = bTraffic; }
		const roadMat = str('roadMaterialPath');
		if (roadMat !== undefined) { def.roadMaterialPath = roadMat; }
		const sideMat = str('sidewalkMaterialPath');
		if (sideMat !== undefined) { def.sidewalkMaterialPath = sideMat; }
		// Unbedingt zugewiesen (auch 'unknown' bei unbekanntem Wert - wie C++).
		const surface = str('defaultSurface');
		if (surface !== undefined) { def.defaultSurface = parseSurfaceType(surface); }

		++overriddenCount;
	}

	ctx.fallback = ctx.definitions.residential;
	return { ok: true, reason: '', overriddenCount, warnings, ...ctx };
}

/** LoadFromJsonFile-Spiegel (Datei-Teil): fehlende/unlesbare Datei -> Defaults. */
function loadFromJsonFile(filePath)
{
	if (!fs.existsSync(filePath))
	{
		const ctx = applyBuiltInDefaults();
		return { ok: false, reason: 'missing', overriddenCount: 0, warnings: [], ...ctx };
	}
	let content;
	try
	{
		content = fs.readFileSync(filePath, 'utf8');
	}
	catch (err)
	{
		const ctx = applyBuiltInDefaults();
		return { ok: false, reason: 'unreadable', overriddenCount: 0, warnings: [], ...ctx };
	}
	return loadFromJsonString(content);
}

// ---------------------------------------------------------------------------
// Verify-Suite (spiegelt die Erwartungen von Tests/GeneratorTest.cpp,
// RoadTypeLibrary.Config, plus Guard-/Default-Proben)
// ---------------------------------------------------------------------------

const JsonMode = process.argv.includes('--json');
const ParseMode = process.argv.includes('--parse');
const Results = [];
let Failed = 0;

function report(name, ok, detail)
{
	Results.push({ Name: name, Ok: !!ok, Detail: detail || '' });
	if (!ok)
	{
		++Failed;
	}
}

function near(a, b)
{
	return Math.abs(a - b) < 1e-6;
}

// --- Defaults (RASt 06 / RAA) ---------------------------------------------
{
	const ctx = applyBuiltInDefaults();
	report('Defaults: 20 Klassen vorhanden', Object.keys(ctx.definitions).length === 20,
		`${Object.keys(ctx.definitions).length} Klassen`);
	report('Defaults: Fallback = Residential', ctx.fallback === ctx.definitions.residential);

	const spot = (name, type, field, expected) =>
	{
		const def = ctx.definitions[type];
		const actual = def ? def[field] : undefined;
		report(`Defaults: ${name}`, typeof expected === 'number' ? near(actual, expected) : actual === expected,
			`${type}.${field}=${actual} (erwartet ${expected})`);
	};
	spot('Motorway Spurbreite', 'motorway', 'laneWidthMeters', 3.75);
	spot('Motorway Spuren/Richtung', 'motorway', 'defaultLanesPerDirection', 2);
	spot('Motorway Tempo', 'motorway', 'defaultMaxSpeedKmh', 130.0);
	spot('Motorway Prioritaet', 'motorway', 'priority', 0);
	spot('Motorway Dichte', 'motorway', 'trafficDensityFactor', 1.0);
	spot('Primary Spurbreite', 'primary', 'laneWidthMeters', 3.5);
	spot('Primary Spuren/Richtung', 'primary', 'defaultLanesPerDirection', 2);
	spot('Primary Tempo', 'primary', 'defaultMaxSpeedKmh', 50.0);
	spot('Primary Gehwegbreite', 'primary', 'sidewalkWidthMeters', 3.0);
	spot('Primary Gehweg', 'primary', 'sidewalkByDefault', true);
	spot('Residential Spurbreite', 'residential', 'laneWidthMeters', 2.75);
	spot('Residential Tempo', 'residential', 'defaultMaxSpeedKmh', 30.0);
	spot('Residential Dichte', 'residential', 'trafficDensityFactor', 0.3);
	spot('LivingStreet Tempo', 'living_street', 'defaultMaxSpeedKmh', 7.0);
	spot('Service Tempo', 'service', 'defaultMaxSpeedKmh', 20.0);
	spot('Pedestrian Prioritaet', 'pedestrian', 'priority', 10);
	spot('Pedestrian kein KI-Verkehr', 'pedestrian', 'trafficEnabled', false);
	spot('Footway Tempo 0', 'footway', 'defaultMaxSpeedKmh', 0.0);
	spot('Track Tempo', 'track', 'defaultMaxSpeedKmh', 20.0);
	spot('Track Dichte', 'track', 'trafficDensityFactor', 0.05);
	spot('Track kein KI-Verkehr', 'track', 'trafficEnabled', false);
	spot('Cycleway kein KI-Verkehr', 'cycleway', 'trafficEnabled', false);

	report('Defaults: Pedestrian Pflaster', ctx.definitions.pedestrian.defaultSurface === 'paving_stones');
	report('Defaults: Footway Pflaster', ctx.definitions.footway.defaultSurface === 'paving_stones');
	report('Defaults: Track compacted', ctx.definitions.track.defaultSurface === 'compacted');
	report('Defaults: Path ground', ctx.definitions.path.defaultSurface === 'ground');
	report('Defaults: Primary Asphalt', ctx.definitions.primary.defaultSurface === 'asphalt');
	report('Defaults: Steps Asphalt', ctx.definitions.steps.defaultSurface === 'asphalt');
}

// --- Fallback / Typ-Parsing ------------------------------------------------
{
	const ctx = applyBuiltInDefaults();
	const bogus = getDefinition(ctx, 'bogus');
	report('Fallback: unbekannter Typ -> Residential', bogus === ctx.definitions.residential);
	report('Fallback: Residential Tempo 30', near(bogus.defaultMaxSpeedKmh, 30.0));

	report('ParseHighwayType: motorway', parseHighwayType('motorway') === 'motorway');
	report('ParseHighwayType: primary_link', parseHighwayType('primary_link') === 'primary_link');
	report('ParseHighwayType: Grossschreibung', parseHighwayType('Residential') === 'residential');
	report('ParseHighwayType: Alias bridleway -> path', parseHighwayType('bridleway') === 'path');
	report('ParseHighwayType: Alias corridor -> footway', parseHighwayType('corridor') === 'footway');
	report('ParseHighwayType: Alias busway -> service', parseHighwayType('busway') === 'service');
	report('ParseHighwayType: unbekannt -> null', parseHighwayType('autobahn') === null);
	report('ParseHighwayType: leer -> null', parseHighwayType('  ') === null);

	report('ParseSurfaceType: paved -> asphalt', parseSurfaceType('paved') === 'asphalt');
	report('ParseSurfaceType: concrete:plates -> concrete', parseSurfaceType('concrete:plates') === 'concrete');
	report('ParseSurfaceType: unpaved -> compacted', parseSurfaceType('unpaved') === 'compacted');
	report('ParseSurfaceType: dirt -> ground', parseSurfaceType('dirt') === 'ground');
	report('ParseSurfaceType: unbekannt -> unknown', parseSurfaceType('gummi') === 'unknown');
}

// --- JSON laden (eingecheckte Datei) --------------------------------------
{
	const result = loadFromJsonFile(DEFAULT_CONFIG);
	report('JSON: eingecheckte Datei laedt', result.ok === true, `reason=${result.reason}`);
	report('JSON: 20 Klassen ueberschrieben', result.overriddenCount === 20,
		`${result.overriddenCount} ueberschrieben`);
	report('JSON: keine Warnungen', result.warnings.length === 0, result.warnings.join('; '));
	report('JSON: Primary Spurbreite 3.5', near(result.definitions.primary.laneWidthMeters, 3.5));
	report('JSON: Primary 2 Spuren/Richtung', result.definitions.primary.defaultLanesPerDirection === 2);
	report('JSON: Pedestrian Pflaster', result.definitions.pedestrian.defaultSurface === 'paving_stones');
	report('JSON: Residential Tempo 30', near(result.definitions.residential.defaultMaxSpeedKmh, 30.0));
	report('JSON: Kerbhoehe Default 0.12', near(result.definitions.primary.kerbHeightMeters, 0.12));
}

// --- Fehlerfaelle ----------------------------------------------------------
{
	const missing = loadFromJsonFile('/nonexistent/WiesbadenRoadTypes.json');
	report('Fehler: fehlende Datei -> ok=false', missing.ok === false, `reason=${missing.reason}`);
	report('Fehler: fehlende Datei -> Defaults bleiben', near(missing.definitions.residential.defaultMaxSpeedKmh, 30.0));

	const badJson = loadFromJsonString('{ kein json');
	report('Fehler: ungueltiges JSON -> ok=false', badJson.ok === false, `reason=${badJson.reason}`);
	report('Fehler: ungueltiges JSON -> Defaults bleiben', near(badJson.definitions.primary.defaultMaxSpeedKmh, 50.0));

	const noTypes = loadFromJsonString('{ "anders": 1 }');
	report('Fehler: ohne roadTypes -> ok=false', noTypes.ok === false, `reason=${noTypes.reason}`);
}

// --- Guard-Bedingungen der JSON-Felder ------------------------------------
{
	const result = loadFromJsonString(JSON.stringify({
		roadTypes: {
			residential: {
				laneWidthMeters: 0,
				defaultLanesPerDirection: 9,
				defaultMaxSpeedKmh: -5,
				sidewalkWidthMeters: -1,
				trafficDensityFactor: 2.5,
				priority: 2.6,
				trafficEnabled: false,
				defaultSurface: 'gummi'
			},
			"gibtsnicht": { laneWidthMeters: 4.0 }
		}
	}));
	report('Guards: laneWidth 0 -> Default', near(result.definitions.residential.laneWidthMeters, 2.75));
	report('Guards: lanes 9 -> Clamp 6', result.definitions.residential.defaultLanesPerDirection === 6);
	report('Guards: speed -5 -> Default', near(result.definitions.residential.defaultMaxSpeedKmh, 30.0));
	report('Guards: sidewalk -1 -> Default', near(result.definitions.residential.sidewalkWidthMeters, 2.0));
	report('Guards: density 2.5 -> Clamp 1.0', near(result.definitions.residential.trafficDensityFactor, 1.0));
	report('Guards: priority 2.6 -> 3', result.definitions.residential.priority === 3);
	report('Guards: trafficEnabled false uebernommen', result.definitions.residential.trafficEnabled === false);
	report('Guards: unbekannte Oberflaeche -> unknown (wie C++)',
		result.definitions.residential.defaultSurface === 'unknown');
	report('Guards: unbekannter Typ uebersprungen + Warnung',
		result.warnings.length === 1 && result.warnings[0].includes('gibtsnicht'),
		result.warnings.join('; '));
	report('Guards: unbekannter Typ nicht gezaehlt', result.overriddenCount === 1,
		`${result.overriddenCount} ueberschrieben`);
}

// --- Inventar-Drift-Guard gegen RoadTypeLibrary.cpp -------------------------
// Wie bei verify_cityprompt.mjs: Die C++-Quelle ist die Autoritaet. Aendert
// sich die Default-Tabelle, die Oberflaechen-Ueberrides oder die JSON-Feldnamen
// im C++, ohne dass dieser Port mitzieht, schlaegt der Abgleich fehl.

function extractTextLiterals(text)
{
	const out = [];
	const re = /TEXT\("((?:[^"\\]|\\.)*)"\)/g;
	let m;
	while ((m = re.exec(text)) !== null)
	{
		out.push(m[1]);
	}
	return out;
}

function sorted(arr) { return [...arr].sort(); }

{
	let src = null;
	try
	{
		src = fs.existsSync(CPP_SOURCE) ? fs.readFileSync(CPP_SOURCE, 'utf8') : null;
	}
	catch (err)
	{
		src = null;
	}

	if (src === null)
	{
		report('Drift: RoadTypeLibrary.cpp nicht gefunden', false, CPP_SOURCE);
	}
	else
	{
		// 1) Add(...)-Tabelle der Defaults.
		const enumToType = Object.freeze({
			Motorway: 'motorway', MotorwayLink: 'motorway_link', Trunk: 'trunk', TrunkLink: 'trunk_link',
			Primary: 'primary', PrimaryLink: 'primary_link', Secondary: 'secondary', SecondaryLink: 'secondary_link',
			Tertiary: 'tertiary', TertiaryLink: 'tertiary_link', Unclassified: 'unclassified', Residential: 'residential',
			LivingStreet: 'living_street', Service: 'service', Pedestrian: 'pedestrian', Footway: 'footway',
			Cycleway: 'cycleway', Path: 'path', Steps: 'steps', Track: 'track'
		});
		const cppTable = [];
		const addRe = /Add\(EOSMHighwayType::(\w+),\s*([\d.]+),\s*(\d+),\s*([\d.]+),\s*([\d.]+),\s*(true|false),\s*(true|false),\s*(\d+),\s*([\d.]+),\s*(true|false)\)/g;
		let m;
		while ((m = addRe.exec(src)) !== null)
		{
			cppTable.push([enumToType[m[1]], +m[2], +m[3], +m[4], +m[5], m[6] === 'true', m[7] === 'true', +m[8], +m[9], m[10] === 'true'].join('|'));
		}
		const portTable = DEFAULT_TABLE.map((t) => t.join('|'));
		report('Drift Defaults: Add()-Tabelle', cppTable.join('\n') === portTable.join('\n'),
			cppTable.length === portTable.length
				? `C++ ${cppTable.length} Zeilen, Port ${portTable.length} Zeilen`
				: `Anzahl: C++ ${cppTable.length} vs. Port ${portTable.length}`);

		// 2) DefaultSurface-Ueberrides.
		const enumToSurface = Object.freeze({ PavingStones: 'paving_stones', Compacted: 'compacted', Ground: 'ground' });
		const cppSurfaces = [];
		const surfaceRe = /Definitions\.Find\(EOSMHighwayType::(\w+)\)[\s\S]{0,120}?DefaultSurface = EOSMSurfaceType::(\w+)/g;
		while ((m = surfaceRe.exec(src)) !== null)
		{
			cppSurfaces.push(`${enumToType[m[1]]}=${enumToSurface[m[2]]}`);
		}
		const portSurfaces = Object.entries(DEFAULT_SURFACE_OVERRIDES).map(([k, v]) => `${k}=${v}`);
		report('Drift Defaults: Oberflaechen-Ueberrides',
			sorted(cppSurfaces).join('|') === sorted(portSurfaces).join('|'),
			`C++={${sorted(cppSurfaces).join('|')}} Port={${sorted(portSurfaces).join('|')}}`);

		// 3) JSON-Feldnamen im LoadFromJsonFile-Block (inkl. Root-Feld roadTypes).
		const start = src.indexOf('Entry->TryGetNumberField');
		const end = src.indexOf('++OverriddenCount', start);
		const fieldsBlock = start !== -1 && end !== -1 ? src.slice(start, end) : '';
		const cppFields = [...extractTextLiterals(fieldsBlock), 'roadTypes'];
		report('Drift JSON: Feldnamen', sorted(cppFields).join('|') === sorted(JSON_FIELD_NAMES).join('|'),
			`C++={${sorted(cppFields).join('|')}} Port={${sorted(JSON_FIELD_NAMES).join('|')}}`);
	}
}

// --- Ausgabe ----------------------------------------------------------------

if (ParseMode)
{
	const idx = process.argv.indexOf('--parse');
	const filePath = (idx >= 0 && process.argv[idx + 1] !== undefined && !process.argv[idx + 1].startsWith('--'))
		? process.argv[idx + 1]
		: DEFAULT_CONFIG;
	const result = loadFromJsonFile(filePath);
	const out = {
		Config: filePath,
		Ok: result.ok,
		Reason: result.reason,
		OverriddenCount: result.overriddenCount,
		Warnings: result.warnings,
		Fallback: 'residential',
		Definitions: result.definitions
	};
	process.stdout.write(JSON.stringify(out, null, 2) + '\n');
	process.exit(0);
}

if (JsonMode)
{
	process.stdout.write(JSON.stringify({ Checks: Results.length, Failed, Results }, null, 2) + '\n');
}
else
{
	for (const r of Results)
	{
		const mark = r.Ok ? 'ok  ' : 'FAIL';
		process.stdout.write(`[${mark}] ${r.Name}${r.Detail ? ` - ${r.Detail}` : ''}\n`);
	}
	process.stdout.write(`\n${Results.length - Failed}/${Results.length} Checks bestanden.\n`);
}

process.exit(Failed === 0 ? 0 : 1);
