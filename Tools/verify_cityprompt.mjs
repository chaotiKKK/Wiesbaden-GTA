#!/usr/bin/env node
// verify_cityprompt.mjs
// ---------------------------------------------------------------------------
// Node-Port des CityPrompt-Parsers (GIS/CityPrompt.cpp, CityPromptParser::Parse).
// Der C++-Parser ist rein regelbasiert und deterministisch - dieser Port bildet
// ihn 1:1 ab (Normalisierung, ContainsWord-Wortgrenzen, ParseNumberAfter/
// ParseSpanAfter, alle Regelbloecke) und faehrt dieselben Erwartungen wie der
// C++-Automation-Test (Tests/CityPromptTest.cpp: Parsing/Determinism/
// ExtendedRules/TerrainThresholds) darueber. Aenderst du Regeln im C++-Parser,
// MUSS dieser Port mitgezogen werden (sonst schlaegt der Vertrag fehl).
//
// Nutzung:
//   node Tools/verify_cityprompt.mjs              # alle Checks (Exit 0/1)
//   node Tools/verify_cityprompt.mjs --json       # maschinenlesbar
//   node Tools/verify_cityprompt.mjs --parse "dichte Innenstadt, wolkig"
//                                                 # ein Prompt headless parsen
//
// Exit-Code: 0 = Vertrag erfuellt, 1 = Abweichung.
//
// Drift-Guard: Neben der Verify-Suite (Selbsttest) vergleicht dieses Skript
// sein Keyword-Inventar direkt mit den TEXT("...")-Literalen in
// GIS/CityPrompt.cpp. Aenderst du Regeln im C++-Parser, MUSS der Port
// mitgezogen werden - sonst schlaegt der Inventar-Abgleich fehl (und der
// Sync-Workflow Tools/verify-worktree-sync.mjs bricht ab).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const CPP_SOURCE = path.resolve(SCRIPT_DIR, '..', 'WiesbadenReal', 'Source', 'WiesbadenReal', 'GIS', 'CityPrompt.cpp');

// ---------------------------------------------------------------------------
// Port des Parsers (muss CityPrompt.cpp semantisch 1:1 entsprechen)
// ---------------------------------------------------------------------------

const WEATHER = Object.freeze({
	Clear: 0,
	Cloudy: 1,
	Rain: 2,
	Thunderstorm: 3,
	Fog: 4,
	Snow: 5
});

const WEATHER_NAMES = Object.freeze(['Clear', 'Cloudy', 'Rain', 'Thunderstorm', 'Fog', 'Snow']);

const STYLE_NAMES = Object.freeze([
	'Gruenderzeit', 'Backstein', 'Sandstein', 'Moderne', 'Industrie', 'Fachwerk'
]);

/**
 * Zentrales Regel-Inventar. Einmal definiert, wird es von Parse() UND vom
 * Drift-Check gegen den C++-Parser genutzt - damit kann der Abgleich nicht
 * gegen eine zweite, abweichende Kopie laufen. Reihenfolge innerhalb der
 * Gruppen ist semantisch: weatherGroups/timeGroups/trafficGroups/heightGroups
 * sind if/else-if-Ketten (erste Uebereinstimmung gewinnt), facadeRules/
 * landmarkRules sind Listen, ratioKeywords/spanKeywords Schleifen mit break.
 */
const RULES = Object.freeze({
	denseWords: Object.freeze(['dicht', 'dichte', 'dichter', 'dichtes', 'innenstadt', 'urban',
		'zentrum', 'dense', 'city center']),
	sparseWords: Object.freeze(['locker', 'lockere', 'lockeren', 'lockerer', 'lockeres', 'vorort',
		'vororte', 'gruenflaeche', 'sparse', 'suburb', 'gruen', 'gruene', 'gruener', 'gruenen']),
	facadeRules: Object.freeze([
		['gruenderzeit', 0], ['putz', 0], ['plaster', 0],
		['putzfassade', 0], ['putzfassaden', 0],
		['backstein', 1], ['ziegel', 1], ['brick', 1],
		['backsteinfassade', 1], ['backsteinfassaden', 1],
		['ziegelfassade', 1], ['ziegelfassaden', 1],
		['sandstein', 2], ['sandstone', 2],
		['sandsteinfassade', 2], ['sandsteinfassaden', 2],
		['glas', 3], ['moderne', 3], ['buerohaus', 3],
		['glass', 3], ['modern', 3], ['office', 3],
		['moderner', 3], ['modernen', 3], ['modernes', 3],
		['glasfassade', 3], ['glasfassaden', 3],
		['buerohaeuser', 3],
		['beton', 4], ['industrie', 4], ['concrete', 4],
		['betonbau', 4], ['betonbauten', 4],
		['fachwerk', 5], ['timber', 5],
		['fachwerkfassade', 5], ['fachwerkfassaden', 5]
	]),
	landmarkRules: Object.freeze([
		['marktkirche', 'Marktkirche'],
		['kurhaus', 'Kurhaus'],
		['neroberg', 'Neroberg'],
		['stadtschloss', 'Stadtschloss'],
		['hauptbahnhof', 'Hauptbahnhof'],
		['rathaus', 'Rathaus'],
		['schloss biebrich', 'Schloss Biebrich'],
		['landtag', 'Hessischer Landtag'],
		['staatstheater', 'Hessisches Staatstheater'],
		['museum', 'Museum Wiesbaden']
	]),
	weatherGroups: Object.freeze([
		{ preset: WEATHER.Thunderstorm, words: ['gewitter', 'thunderstorm'] },
		{ preset: WEATHER.Fog, words: ['nebel', 'neblig', 'neblige', 'nebliger', 'nebliges', 'nebligen',
			'fog', 'foggy'] },
		{ preset: WEATHER.Snow, words: ['schnee', 'schneit', 'schneien', 'schneefall', 'verschneit',
			'verschneite', 'verschneiter', 'verschneites', 'snow', 'snowy', 'snowing'] },
		{ preset: WEATHER.Rain, words: ['regen', 'regnerisch', 'regnerische', 'regnerischer', 'regnerisches',
			'regnet', 'regnen', 'regenwetter', 'rain', 'rainy', 'raining',
			'sturm', 'stuermisch', 'stuermische', 'stuermischer', 'storm', 'stormy'] },
		{ preset: WEATHER.Cloudy, words: ['wolkig', 'wolkige', 'wolkiger', 'wolkiges', 'bewoelkt', 'bewoelkte',
			'bewoelkter', 'bewoelktes', 'trueb', 'truebe', 'trueber', 'truebes', 'cloudy'] },
		{ preset: WEATHER.Clear, words: ['klar', 'sonne', 'sonnig', 'clear', 'sunny'] }
	]),
	timeGroups: Object.freeze([
		{ hours: 0.0, words: ['nacht', 'nachts', 'night'] },
		{ hours: 21.0, words: ['spaet', 'spaete', 'spaeter', 'late'] },
		{ hours: 19.0, words: ['abend', 'abends', 'evening'] },
		{ hours: 16.0, words: ['nachmittag', 'nachmittags', 'afternoon'] },
		{ hours: 10.0, words: ['vormittag', 'vormittags', 'forenoon'] },
		{ hours: 12.0, words: ['mittag', 'mittags', 'noon'] },
		{ hours: 8.0, words: ['morgen', 'morgens', 'frueh', 'early', 'morning'] }
	]),
	trafficGroups: Object.freeze([
		{ density: 0.25, words: ['wenig verkehr', 'little traffic'] },
		{ density: 0.85, words: ['viel verkehr', 'heavy traffic'] },
		{ density: 0.95, words: ['stau', 'staus', 'staue', 'traffic jam'] },
		{ density: 0.8, words: ['belebt', 'belebte', 'belebten', 'belebter', 'belebtes', 'busy'] },
		{ density: 0.3, words: ['ruhig', 'ruhige', 'ruhigen', 'ruhiger', 'ruhiges', 'quiet'] },
		{ density: 0.2, words: ['leer', 'leere', 'leeren', 'leerer', 'leeres', 'empty'] },
		{ density: 0.7, words: ['verkehr', 'traffic'] }
	]),
	heightGroups: Object.freeze([
		{ scale: 2.0, words: ['hochhaus', 'hochhaeuser', 'hochhaeusern', 'wolkenkratzer', 'skyscraper'] },
		{ scale: 0.6, words: ['niedrig', 'niedrige', 'niedrigen', 'niedriger', 'niedriges', 'low'] },
		{ scale: 1.4, words: ['hoch', 'hohe', 'hohen', 'hoher', 'hohes', 'high'] },
		{ scale: 1.0, words: ['mittel', 'medium'] }
	]),
	ratioKeywords: Object.freeze(['tile faktor', 'tile-faktor', 'crop faktor', 'crop-faktor',
		'vergroesserungsfaktor', 'tile ratio', 'tile factor']),
	spanKeywords: Object.freeze(['hoehenspanne', 'hoehen min', 'hoehenbereich',
		'height span', 'height range'])
});

/** Normalisiert einen Text fuer den Regel-Abgleich (lowercase, sz/ae/oe/ue). */
function normalizePrompt(text)
{
	let n = text.toLowerCase();
	n = n.replaceAll('\u00DF', 'ss'); // ß
	n = n.replaceAll('\u00E4', 'ae'); // ä
	n = n.replaceAll('\u00F6', 'oe'); // ö
	n = n.replaceAll('\u00FC', 'ue'); // ü
	return n;
}

/** ASCII-Alnum wie FChar::IsAlnum auf dem normalisierten Text. */
function isAlnum(ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

/** Enthalten mit Wortgrenzen (wie ContainsWord im C++-Parser). */
function containsWord(normalized, keyword)
{
	if (!keyword)
	{
		return false;
	}
	let startIndex = 0;
	while (startIndex < normalized.length)
	{
		const found = normalized.indexOf(keyword, startIndex);
		if (found === -1)
		{
			return false;
		}
		const boundaryBefore = (found === 0) || !isAlnum(normalized[found - 1]);
		const endIndex = found + keyword.length;
		const boundaryAfter = (endIndex >= normalized.length) || !isAlnum(normalized[endIndex]);
		if (boundaryBefore && boundaryAfter)
		{
			return true;
		}
		startIndex = found + 1;
	}
	return false;
}

/** Liest die erste Zahl direkt hinter einem Schluesselwort. -1.0 wenn keine. */
function parseNumberAfter(normalized, keyword)
{
	const found = normalized.indexOf(keyword);
	if (found === -1)
	{
		return -1.0;
	}
	let p = found + keyword.length;
	while (p < normalized.length && (normalized[p] === ' ' || normalized[p] === ':' || normalized[p] === '='))
	{
		++p;
	}
	const start = p;
	if (p < normalized.length && (normalized[p] === '-' || normalized[p] === '+'))
	{
		++p;
	}
	while (p < normalized.length && normalized[p] >= '0' && normalized[p] <= '9')
	{
		++p;
	}
	if (p + 1 < normalized.length
		&& (normalized[p] === '.' || normalized[p] === ',')
		&& normalized[p + 1] >= '0' && normalized[p + 1] <= '9')
	{
		++p;
		while (p < normalized.length && normalized[p] >= '0' && normalized[p] <= '9')
		{
			++p;
		}
	}
	if (p === start)
	{
		return -1.0;
	}
	const number = normalized.slice(start, p).replaceAll(',', '.');
	return parseFloat(number);
}

/** Parst ein "min..max"-Paar; normalisiert umgekehrte Bereiche (min > max). */
function parseSpanAfter(normalized, keyword)
{
	const found = normalized.indexOf(keyword);
	if (found === -1)
	{
		return null;
	}
	let p = found + keyword.length;

	const skipSpace = () =>
	{
		while (p < normalized.length
			&& (normalized[p] === ' ' || normalized[p] === ':' || normalized[p] === '='))
		{
			++p;
		}
	};
	const readNumber = () =>
	{
		skipSpace();
		const start = p;
		if (p < normalized.length && (normalized[p] === '-' || normalized[p] === '+'))
		{
			++p;
		}
		while (p < normalized.length && normalized[p] >= '0' && normalized[p] <= '9')
		{
			++p;
		}
		if (p + 1 < normalized.length
			&& (normalized[p] === '.' || normalized[p] === ',')
			&& normalized[p + 1] >= '0' && normalized[p + 1] <= '9')
		{
			++p;
			while (p < normalized.length && normalized[p] >= '0' && normalized[p] <= '9')
			{
				++p;
			}
		}
		if (p === start)
		{
			return null;
		}
		return parseFloat(normalized.slice(start, p).replaceAll(',', '.'));
	};

	const min = readNumber();
	if (min === null)
	{
		return null;
	}

	// Trenner zwischen min und max ueberspringen (C++: StartsWith mit
	// Default-ESearchCase::IgnoreCase - daher hier case-insensitiv vergleichen).
	skipSpace();
	const rest = normalized.slice(p).toLowerCase();
	if (rest.startsWith('..')) { p += 2; }
	else if (rest.startsWith('.') || rest.startsWith('-')) { ++p; }
	else if (rest.startsWith('maximal')) { p += 7; }
	else if (rest.startsWith('max')) { p += 3; }
	else if (rest.startsWith('bis')) { p += 3; }
	else if (rest.startsWith('to')) { p += 2; }

	const max = readNumber();
	if (max === null)
	{
		return null;
	}

	// Umgekehrte Bereiche (min > max) normalisieren.
	return min <= max ? { min, max } : { min: max, max: min };
}

/** Haupt-Parse-Funktion (Port von CityPromptParser::Parse). */
function parsePrompt(text)
{
	const spec = {
		RawPrompt: text,
		DisplayName: '',
		BuildingDensity: 0.5,
		FacadeVariantWeights: {},
		DetectedLandmarks: [],
		Weather: WEATHER.Clear,
		TimeOfDayHours: -1.0,
		TrafficDensity: 0.5,
		BuildingHeightScale: 1.0,
		TerrainMaxTileToOsmRatio: 2.0,
		TerrainMinHeightSpanMeters: 1.0,
		TerrainMaxHeightSpanMeters: 3000.0,
		MatchedKeywordCount: 0
	};

	const normalized = normalizePrompt(text);
	if (!normalized)
	{
		spec.DisplayName = 'Standard-Stadt';
		return spec;
	}

	// -- Bebauungsdichte ------------------------------------------------------
	let density = 0.5;
	if (RULES.denseWords.some((w) => containsWord(normalized, w)))
	{
		density = Math.max(density, 0.85);
	}
	if (RULES.sparseWords.some((w) => containsWord(normalized, w)))
	{
		density = Math.min(density, 0.3);
	}
	spec.BuildingDensity = density;

	// -- Fassadenstile (Gewichte je Materialvariante 0-5) --------------------
	for (const [keyword, variant] of RULES.facadeRules)
	{
		if (containsWord(normalized, keyword))
		{
			spec.FacadeVariantWeights[variant] = (spec.FacadeVariantWeights[variant] || 0) + 1.0;
			++spec.MatchedKeywordCount;
		}
	}

	// -- Landmarken (kanonische Namen) ---------------------------------------
	for (const [keyword, canonical] of RULES.landmarkRules)
	{
		if (containsWord(normalized, keyword))
		{
			if (!spec.DetectedLandmarks.includes(canonical))
			{
				spec.DetectedLandmarks.push(canonical);
			}
			++spec.MatchedKeywordCount;
		}
	}

	// -- Wetterlage (if/else-if: erste Uebereinstimmung gewinnt) -------------
	for (const group of RULES.weatherGroups)
	{
		if (group.words.some((w) => containsWord(normalized, w)))
		{
			spec.Weather = group.preset;
			break;
		}
	}

	// -- Tageszeit (spezifischste Lage zuerst) -------------------------------
	for (const group of RULES.timeGroups)
	{
		if (group.words.some((w) => containsWord(normalized, w)))
		{
			spec.TimeOfDayHours = group.hours;
			break;
		}
	}

	// -- Verkehrsdichte (spezifischste Formulierung zuerst) ------------------
	for (const group of RULES.trafficGroups)
	{
		if (group.words.some((w) => containsWord(normalized, w)))
		{
			spec.TrafficDensity = group.density;
			break;
		}
	}

	// -- Gebaeudehoehen (spezifischste Formulierung zuerst) ------------------
	for (const group of RULES.heightGroups)
	{
		if (group.words.some((w) => containsWord(normalized, w)))
		{
			spec.BuildingHeightScale = group.scale;
			break;
		}
	}

	// -- Terrain-Qualitaetsschwellen (numerisch) -----------------------------
	for (const keyword of RULES.ratioKeywords)
	{
		const ratio = parseNumberAfter(normalized, keyword);
		if (ratio > 0.0)
		{
			spec.TerrainMaxTileToOsmRatio = ratio;
			break;
		}
	}
	for (const keyword of RULES.spanKeywords)
	{
		const span = parseSpanAfter(normalized, keyword);
		if (span)
		{
			spec.TerrainMinHeightSpanMeters = span.min;
			spec.TerrainMaxHeightSpanMeters = span.max;
			break;
		}
	}

	// -- Anzeigename ----------------------------------------------------------
	let styleName = 'Standard';
	let bestVariant = -1;
	let bestWeight = 0.0;
	for (const [variant, weight] of Object.entries(spec.FacadeVariantWeights))
	{
		const key = Number(variant);
		// Bei Gleichstand gewinnt die kleinere Variantennummer (deterministisch).
		if (weight > bestWeight || (weight === bestWeight && key < bestVariant))
		{
			bestWeight = weight;
			bestVariant = key;
		}
	}
	if (bestVariant >= 0 && bestVariant < STYLE_NAMES.length)
	{
		styleName = STYLE_NAMES[bestVariant];
	}
	const densityName = (density >= 0.7) ? 'Innenstadt' : ((density <= 0.3) ? 'Vorort' : 'Stadt');
	spec.DisplayName = `${styleName}-${densityName}`;

	return spec;
}

// ---------------------------------------------------------------------------
// Verify-Suite (spiegelt die Erwartungen von CityPromptTest.cpp)
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

function parse(prompt)
{
	return parsePrompt(prompt);
}

// ---------------------------------------------------------------------------
// Fixture-Suite (gemeinsame Erwartungen mit dem C++-Test)
// ---------------------------------------------------------------------------
// Die Erwartungen liegen NICHT mehr im Code, sondern in der gemeinsamen
// JSON-Fixture Content/Config/CityPromptSpecFixture.json, die auch der
// C++-Test (CityPrompt.SpecFixture) konsumiert. Damit ist der Parser-Abgleich
// bidirektional: beide Seiten muessen gegen dieselbe Fixture gruen sein.

const FIXTURE_PATH = path.resolve(SCRIPT_DIR, '..', 'WiesbadenReal', 'Content', 'Config', 'CityPromptSpecFixture.json');

function compareSpec(s, expect)
{
	const issues = [];
	const near = (a, b) => Math.abs(a - b) < 1e-3;
	const nearList = (label, actual, expected) =>
	{
		if (!near(actual, expected))
		{
			issues.push(`${label}: erwartet ${expected}, erhalten ${actual}`);
		}
	};

	if (s.DisplayName !== expect.DisplayName)
	{
		issues.push(`DisplayName: erwartet "${expect.DisplayName}", erhalten "${s.DisplayName}"`);
	}
	nearList('BuildingDensity', s.BuildingDensity, expect.BuildingDensity);

	const expWeights = {};
	for (const [k, v] of Object.entries(expect.FacadeVariantWeights || {}))
	{
		expWeights[Number(k)] = v;
	}
	const actKeys = Object.keys(s.FacadeVariantWeights).map(Number).sort((a, b) => a - b);
	const expKeys = Object.keys(expWeights).map(Number).sort((a, b) => a - b);
	if (actKeys.join(',') !== expKeys.join(','))
	{
		issues.push(`FacadeVariantWeights-Keys: erwartet [${expKeys}], erhalten [${actKeys}]`);
	}
	else
	{
		for (const k of expKeys)
		{
			if (!near(s.FacadeVariantWeights[k], expWeights[k]))
			{
				issues.push(`FacadeVariantWeights[${k}]: erwartet ${expWeights[k]}, erhalten ${s.FacadeVariantWeights[k]}`);
			}
		}
	}

	const actLm = [...s.DetectedLandmarks].sort();
	const expLm = [...(expect.DetectedLandmarks || [])].sort();
	if (actLm.join('|') !== expLm.join('|'))
	{
		issues.push(`DetectedLandmarks: erwartet [${expLm}], erhalten [${actLm}]`);
	}

	if (s.Weather !== expect.Weather)
	{
		issues.push(`Weather: erwartet ${expect.Weather}, erhalten ${s.Weather}`);
	}
	nearList('TimeOfDayHours', s.TimeOfDayHours, expect.TimeOfDayHours);
	nearList('TrafficDensity', s.TrafficDensity, expect.TrafficDensity);
	nearList('BuildingHeightScale', s.BuildingHeightScale, expect.BuildingHeightScale);
	nearList('TerrainMaxTileToOsmRatio', s.TerrainMaxTileToOsmRatio, expect.TerrainMaxTileToOsmRatio);
	nearList('TerrainMinHeightSpanMeters', s.TerrainMinHeightSpanMeters, expect.TerrainMinHeightSpanMeters);
	nearList('TerrainMaxHeightSpanMeters', s.TerrainMaxHeightSpanMeters, expect.TerrainMaxHeightSpanMeters);
	if (s.MatchedKeywordCount !== expect.MatchedKeywordCount)
	{
		issues.push(`MatchedKeywordCount: erwartet ${expect.MatchedKeywordCount}, erhalten ${s.MatchedKeywordCount}`);
	}
	return issues;
}

// Fixture laden und jeden Fall gegen den Port-Parser pruefen.
{
	let fixture = null;
	if (!fs.existsSync(FIXTURE_PATH))
	{
		report('Fixture: CityPromptSpecFixture.json nicht gefunden', false, FIXTURE_PATH);
	}
	else
	{
		try
		{
			fixture = JSON.parse(fs.readFileSync(FIXTURE_PATH, 'utf8'));
		}
		catch (err)
		{
			report('Fixture: JSON unlesbar', false, err.message);
		}
	}
	if (fixture && Array.isArray(fixture.Cases))
	{
		for (const fixtureCase of fixture.Cases)
		{
			const issues = compareSpec(parsePrompt(fixtureCase.Prompt), fixtureCase.Expect);
			report(`Fixture: ${fixtureCase.Name}`, issues.length === 0, issues.join('; '));
		}
	}
	else if (fixture)
	{
		report('Fixture: kein Cases-Array', false, FIXTURE_PATH);
	}
}

// Determinismus-Smoke (pure Funktion: doppelter Parse identisch).
{
	const prompt = 'dichte Backstein-Innenstadt mit Rathaus und Neroberg, Nebel';
	const a = parsePrompt(prompt);
	const b = parsePrompt(prompt);
	report('Determinism: doppelter Parse identisch',
		JSON.stringify(a) === JSON.stringify(b),
		'zwei Parse-Aufrufe desselben Prompts divergieren');
}

// ---------------------------------------------------------------------------
// Drift-Guard: Keyword-Inventar gegen den C++-Parser
// ---------------------------------------------------------------------------
// Die Verify-Suite oben testet nur, dass der Port zu sich selbst konsistent
// ist. Damit eine Regel-Aenderung in GIS/CityPrompt.cpp ohne paralleles
// Port-Update auffaellt, werden hier die TEXT("...")-Literale direkt aus der
// C++-Quelle extrahiert und gegen das RULES-Inventar verglichen. Reihenfolge-
// sensibel sind nur ratioKeywords/spanKeywords (Schleife mit break) und
// STYLE_NAMES (Varianten-Index); alle OR-Gruppen werden als Mengen verglichen.

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

function balancedParens(text, openParenIdx)
{
	let depth = 0;
	for (let i = openParenIdx; i < text.length; ++i)
	{
		if (text[i] === '(') { ++depth; }
		else if (text[i] === ')')
		{
			--depth;
			if (depth === 0) { return text.slice(openParenIdx + 1, i); }
		}
	}
	return null;
}

// Holt die Bedingung des if/else-if-Zweigs, dessen erste Bedingung das
// Anker-Keyword enthaelt (eindeutig pro Gruppe, s. RULES).
function branchLiterals(src, anchor)
{
	const literal = `TEXT("${anchor}")`;
	const idx = src.indexOf(literal);
	if (idx === -1) { return null; }
	let open = -1;
	const re = /(?:else if|if) \(/g;
	let m;
	while ((m = re.exec(src)) !== null && m.index < idx) { open = m.index; }
	if (open === -1) { return null; }
	const paren = src.indexOf('(', open);
	const cond = balancedParens(src, paren);
	if (cond === null) { return null; }
	return extractTextLiterals(cond);
}

function blockText(src, marker, endMarker)
{
	const start = src.indexOf(marker);
	if (start === -1) { return null; }
	const end = src.indexOf(endMarker, start + marker.length);
	if (end === -1) { return null; }
	return src.slice(start + marker.length, end);
}

function sorted(arr) { return [...arr].sort(); }

function toKey(arr) { return sorted(arr).join('|'); }

function checkSet(name, src, anchor, portWords)
{
	const cpp = branchLiterals(src, anchor);
	if (cpp === null)
	{
		report(name, false, `Branch fuer Anker "${anchor}" nicht gefunden`);
		return;
	}
	const cppKey = toKey(cpp);
	const portKey = toKey(portWords);
	report(name, cppKey === portKey,
		`C++={${cppKey}} Port={${portKey}}`);
}

function checkOrdered(name, src, blockMarker, portWords)
{
	const block = blockText(src, blockMarker, '};');
	if (block === null)
	{
		report(name, false, `Block "${blockMarker}" nicht gefunden`);
		return;
	}
	const cpp = extractTextLiterals(block);
	report(name, cpp.join('|') === portWords.join('|'),
		`C++={${cpp.join('|')}} Port={${portWords.join('|')}}`);
}

const CppSource = (() =>
{
	try
	{
		return fs.existsSync(CPP_SOURCE) ? fs.readFileSync(CPP_SOURCE, 'utf8') : null;
	}
	catch (err)
	{
		return null;
	}
})();

if (CppSource === null)
{
	report('Drift: GIS/CityPrompt.cpp nicht gefunden', false, CPP_SOURCE);
}
else
{
	// Dichte (zwei unabhaengige if-Zweige).
	checkSet('Drift Dichte: dicht (hoch)', CppSource, 'dicht', RULES.denseWords);
	checkSet('Drift Dichte: locker (niedrig)', CppSource, 'locker', RULES.sparseWords);

	// Fassaden: { TEXT("kw"), Variante }-Paare aus dem Array-Block.
	{
		const block = blockText(CppSource, 'FacadeRules[] = {', '};');
		if (block === null)
		{
			report('Drift Fassaden: FacadeRules[]-Block nicht gefunden', false);
		}
		else
		{
			const cpp = [];
			const re = /\{\s*TEXT\("([^"]*)"\)\s*,\s*(\d+)\s*\}/g;
			let m;
			while ((m = re.exec(block)) !== null) { cpp.push(`${m[1]}=${m[2]}`); }
			const port = RULES.facadeRules.map(([k, v]) => `${k}=${v}`);
			const cppKey = sorted(cpp).join('|');
			const portKey = sorted(port).join('|');
			report('Drift Fassaden: FacadeRules (Keyword,Variante)',
				cppKey === portKey, `C++={${cppKey}} Port={${portKey}}`);
		}
	}

	// Landmarken: { TEXT("kw"), TEXT("Kanonisch") }-Paare.
	{
		const block = blockText(CppSource, 'LandmarkRules[] = {', '};');
		if (block === null)
		{
			report('Drift Landmarken: LandmarkRules[]-Block nicht gefunden', false);
		}
		else
		{
			const cpp = [];
			const re = /\{\s*TEXT\("([^"]*)"\)\s*,\s*TEXT\("([^"]*)"\)\s*\}/g;
			let m;
			while ((m = re.exec(block)) !== null) { cpp.push(`${m[1]}=>${m[2]}`); }
			const port = RULES.landmarkRules.map(([k, v]) => `${k}=>${v}`);
			const cppKey = sorted(cpp).join('|');
			const portKey = sorted(port).join('|');
			report('Drift Landmarken: LandmarkRules (Keyword=>Kanonisch)',
				cppKey === portKey, `C++={${cppKey}} Port={${portKey}}`);
		}
	}

	// Wetterlage: jeder if/else-if-Zweig (Anker = erstes Keyword der Gruppe).
	for (const group of RULES.weatherGroups)
	{
		checkSet(`Drift Wetter: ${group.words[0]}`, CppSource, group.words[0], group.words);
	}
	for (const group of RULES.timeGroups)
	{
		checkSet(`Drift Zeit: ${group.words[0]}`, CppSource, group.words[0], group.words);
	}
	for (const group of RULES.trafficGroups)
	{
		checkSet(`Drift Verkehr: ${group.words[0]}`, CppSource, group.words[0], group.words);
	}
	for (const group of RULES.heightGroups)
	{
		checkSet(`Drift Hoehen: ${group.words[0]}`, CppSource, group.words[0], group.words);
	}

	// Terrain-Schwellen: Keyword-Arrays (Reihenfolge relevant, Schleife mit break).
	checkOrdered('Drift Terrain: RatioKeywords', CppSource, 'RatioKeywords[] = {', RULES.ratioKeywords);
	checkOrdered('Drift Terrain: SpanKeywords', CppSource, 'SpanKeywords[] = {', RULES.spanKeywords);

	// Stil-Namen: Reihenfolge = Materialvarianten 0-5.
	{
		const start = CppSource.indexOf('GetFacadeStyleNames()');
		if (start === -1)
		{
			report('Drift Stilnamen: GetFacadeStyleNames() nicht gefunden', false);
		}
		else
		{
			const block = CppSource.slice(start, CppSource.indexOf('};', start));
			const cpp = extractTextLiterals(block);
			report('Drift Stilnamen: STYLE_NAMES (Varianten 0-5)',
				cpp.join('|') === STYLE_NAMES.join('|'),
				`C++={${cpp.join('|')}} Port={${STYLE_NAMES.join('|')}}`);
		}
	}
}

// --- Ausgabe ----------------------------------------------------------------

if (ParseMode)
{
	const idx = process.argv.indexOf('--parse');
	const promptText = idx >= 0 && process.argv[idx + 1] !== undefined
		? process.argv.slice(idx + 1).join(' ')
		: '';
	const spec = parsePrompt(promptText);
	const out = {
		RawPrompt: spec.RawPrompt,
		DisplayName: spec.DisplayName,
		BuildingDensity: spec.BuildingDensity,
		FacadeVariantWeights: spec.FacadeVariantWeights,
		DetectedLandmarks: spec.DetectedLandmarks,
		Weather: WEATHER_NAMES[spec.Weather],
		TimeOfDayHours: spec.TimeOfDayHours,
		TrafficDensity: spec.TrafficDensity,
		BuildingHeightScale: spec.BuildingHeightScale,
		TerrainMaxTileToOsmRatio: spec.TerrainMaxTileToOsmRatio,
		TerrainMinHeightSpanMeters: spec.TerrainMinHeightSpanMeters,
		TerrainMaxHeightSpanMeters: spec.TerrainMaxHeightSpanMeters,
		MatchedKeywordCount: spec.MatchedKeywordCount
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
