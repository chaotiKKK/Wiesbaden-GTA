#!/usr/bin/env node
// verify_buildsummary_csv.mjs
// ---------------------------------------------------------------------------
// Vertrags-Check der Build-Historie (Saved/BuildHistory/CityBuilds.csv):
// Die CSV wird von GIS/WiesbadenBuildSummary.cpp geschrieben (22 Spalten,
// RFC-4180-Quoting) und von Tools/analyze_city_builds.mjs gelesen. Ein neues
// Feld zwischen Kopfzeile und Zeilenformatierer wuerde still alle Spalten
// verschieben - dieses Tool erzeugt eine Fixture im exakten C++-Format,
// faehrt den echten Parser darueber und prueft Spaltenzahl, Statistiken,
// Quoting und Trend.
//
// Nutzung:
//   node Tools/verify_buildsummary_csv.mjs          # alle Checks (Exit 0/1)
//   node Tools/verify_buildsummary_csv.mjs --json   # maschinenlesbar
//
// Exit-Code: 0 = Vertrag erfuellt, 1 = Abweichung.

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(SCRIPT_DIR, '..');
const ANALYZER = path.join(SCRIPT_DIR, 'analyze_city_builds.mjs');
const TMP_CSV = path.join(SCRIPT_DIR, '.verify_buildsummary_tmp.csv');

// Kopfzeile MUSS mit BuildSummaryCsvHeader() in GIS/WiesbadenBuildSummary.cpp
// uebereinstimmen (24 Spalten). Spaltenzaehler hier absichtlich NICHT hart:
// Der Check vergleicht Row gegen Header und erwartet beide == 24.
const HEADER = 'Timestamp,Source,DurationSeconds,RoadSegments,Intersections,Buildings,Signs,'
	+ 'TerrainGrid,TerrainMode,TerrainTileTooLarge,TerrainHeightRangeSuspicious,'
	+ 'RegionsWater,RegionsGreen,RegionsResidential,RegionsCommercial,'
	+ 'RegionsIndustrial,RegionAssetsTotal,RegionAssetsTrees,RegionAssetsWaterfront,'
	+ 'RegionAssetsIndustrial,Chunks,ChunkSizeMeters,MapPath,Result';

// Zwei Fixture-Zeilen: Erfolg (Editor, ok) und Fehlschlag (Runtime) mit Komma
// im Fehlertext - der gequotete Fall deckt den Spalten-Shift-Fehlerklassentyp ab.
// Terrain-Qualitaetswarnung (Spalten 10/11): OK-Fall TileTooLarge=1 (Crop
// fehlt), Hoehenspanne ok=0; FAIL-Fall 0/0.
const OK_ROW = '2026-08-16 10:00:00,Editor,83.4,1234,96,5678,123,513,Landscape,1,0,2,5,3,1,1,342,300,22,20,57,500,/Game/Maps/WiesbadenCity,ok';
const FAIL_ROW = '2026-08-16 11:00:00,Runtime,12.1,"fehlgeschlagen: OSM-Parserfehler, Zeile 42",10,2,0,0,,0,0,0,0,0,0,0,0,0,0,0,,,,';

const JsonMode = process.argv.includes('--json');
const Results = [];
let Failed = 0;

function report(Name, Ok, Detail)
{
	Results.push({ Name, Ok, Detail: Detail || '' });
	if (!Ok) { ++Failed; }
}

function headerCount(Line)
{
	// Einfache Komma-Zaehlung reicht fuer die Kopfzeile (keine Quoting-Faelle).
	return Line.split(',').length;
}

function runAnalyzer(args)
{
	return execFileSync('node', [ANALYZER, ...args], { encoding: 'utf8' });
}

// --- Fixture schreiben --------------------------------------------------------

try
{
	if (headerCount(HEADER) !== 24)
	{
		report('Kopfzeile: 24 Spalten', false, `gefunden: ${headerCount(HEADER)} - Kopfzeile im Tool weicht von der C++-Vorlage ab`);
	}
	else
	{
		report('Kopfzeile: 24 Spalten', true, '');
	}

	fs.writeFileSync(TMP_CSV, HEADER + '\n' + OK_ROW + '\n' + FAIL_ROW + '\n', 'utf8');

	// Spaltenzahl der Datenzeilen gegen die Kopfzeile (RFC-4180-Parser).
	const Lines = fs.readFileSync(TMP_CSV, 'utf8').split(/\r?\n/).filter((L) => L.trim().length > 0);
	const HeaderCols = Lines[0].split(',').length;
	for (const [Index, Line] of Lines.slice(1).entries())
	{
		// Anzahl der Kommata ist kein zuverlaessiger Spaltenzaehler bei
		// gequoteten Feldern - hier zaehlen wir per Parser-Simulation: eine
		// gueltige Zeile hat genauso viele Top-Level-Kommata wie die Kopfzeile.
		let InQuotes = false;
		let Commas = 0;
		for (let i = 0; i < Line.length; ++i)
		{
			const Ch = Line[i];
			if (InQuotes)
			{
				// In Anfuehrungszeichen: doppeltes "" = escapetes Anfuehrungs-
				// zeichen (konsumieren), sonst schliessendes Anfuehrungszeichen.
				if (Ch === '"') { if (Line[i + 1] === '"') { ++i; } else { InQuotes = false; } }
			}
			else if (Ch === '"') { InQuotes = true; }
			else if (Ch === ',') { ++Commas; }
		}
		report(`Zeile ${Index + 1}: ${HeaderCols} Spalten`, Commas === HeaderCols - 1,
			`Top-Level-Kommata: ${Commas}, erwartet ${HeaderCols - 1}`);
	}

	// --- Parser-Roundtrip: Statistiken --------------------------------------
	const Out = runAnalyzer(['--csv', TMP_CSV]);

	report('Analyzer: liest Fixture (2 Laeufe)', Out.includes('Gesamt: 2 Laeufe'), '');
	report('Analyzer: avg korrekt (47.8)', Out.includes('avg 47.8 s'), '');
	report('Analyzer: median bei gerader Anzahl (47.8)', Out.includes('median 47.8 s'), '');
	report('Analyzer: Quelle Editor 83.4', Out.includes('Editor: 1 Laeufe, avg 83.4 s'), '');
	report('Analyzer: Quelle Runtime 12.1', Out.includes('Runtime: 1 Laeufe, avg 12.1 s'), '');
	report('Analyzer: Ergebnis ok 1 Lauf', Out.includes('ok: 1 Laeufe'), '');
	report('Analyzer: Ergebnis nicht ok 1 Lauf', Out.includes('nicht ok: 1 Laeufe'), '');

	// Quoting: Der Fehlschlag mit Komma im Fehlertext muss als Runtime/nicht-ok
	// zugeordnet werden. Waere das Komma fehlgeparst, verschöben sich die
	// Spalten und der Lauf erschiene mit falschen Zahlen als ok.
	report('Quoting: Fehlschlag als Runtime/nicht-ok (kein Spalten-Shift)',
		Out.includes('nicht ok: 1 Laeufe, avg 12.1 s') && Out.includes('Runtime: 1 Laeufe, avg 12.1 s'), '');

	// --- Trend -----------------------------------------------------------------
	const Trend = runAnalyzer(['--csv', TMP_CSV, '--trend', '5']);
	const TrendLines = Trend.split('\n').filter((L) => L.includes('s  '));
	report('Trend: 2 Laeufe sichtbar', TrendLines.length === 2, '');
	const OkTrend = TrendLines.find((L) => L.includes('83.4'));
	const FailTrend = TrendLines.find((L) => L.includes('12.1'));
	report('Trend: ok-Lauf ohne !-Flag', !!OkTrend && !OkTrend.includes('!'), OkTrend || 'fehlt');
	report('Trend: Fehlschlag mit !-Flag', !!FailTrend && FailTrend.includes('!'), FailTrend || 'fehlt');

	// --- Randfaelle ------------------------------------------------------------
	// Nur Kopfzeile -> Exit 0, keine Eintraege.
	fs.writeFileSync(TMP_CSV, HEADER + '\n', 'utf8');
	let HeaderOnlyExit = 0;
	try { runAnalyzer(['--csv', TMP_CSV]); } catch (E) { HeaderOnlyExit = E.status; }
	report('Randfall: Kopfzeile-only -> Exit 0', HeaderOnlyExit === 0, `Exit=${HeaderOnlyExit}`);

	// Fehlende Datei -> Exit 1.
	let MissingExit = 0;
	try { runAnalyzer(['--csv', path.join(SCRIPT_DIR, '.definitiv_nicht_da.csv')]); } catch (E) { MissingExit = E.status; }
	report('Randfall: fehlende Datei -> Exit 1', MissingExit === 1, `Exit=${MissingExit}`);
}
finally
{
	if (fs.existsSync(TMP_CSV)) { fs.unlinkSync(TMP_CSV); }
}

// --- Ausgabe -------------------------------------------------------------------

if (JsonMode)
{
	console.log(JSON.stringify({ ok: Failed === 0, checks: Results }, null, 2));
}
else
{
	for (const R of Results)
	{
		console.log(`${R.Ok ? 'PASS' : 'FAIL'} ${R.Name}${R.Detail ? ' -- ' + R.Detail : ''}`);
	}
	console.log(Failed === 0
		? `\n=== ALLE ${Results.length} CHECKS BESTANDEN (CityBuilds.csv-Vertrag ok) ===`
		: `\n=== ${Failed} VON ${Results.length} CHECKS FEHLGESCHLAGEN ===`);
}

process.exit(Failed ? 1 : 0);
