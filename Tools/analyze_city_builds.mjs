#!/usr/bin/env node
// analyze_city_builds.mjs
// ---------------------------------------------------------------------------
// Auswertung der Build-Historie (Saved/BuildHistory/CityBuilds.csv, geschrieben
// von GIS/WiesbadenBuildSummary je BuildCity-/Laufzeit-Build):
// Durchschnitt / Median / Minimum / Maximum der Build-Zeiten ueber alle Laeufe,
// getrennt nach Quelle (Editor/Runtime) und Ergebnis (ok vs. nicht ok).
//
// Nutzung:
//   node Tools/analyze_city_builds.mjs                  # Standard-Pfade suchen
//   node Tools/analyze_city_builds.mjs --csv <pfad>     # bestimmte CSV auswerten
//   node Tools/analyze_city_builds.mjs --trend <n>      # Trend-Fenster (Default 10)
//
// Exit-Code: 0 = ok (auch wenn die CSV noch keine Eintraege hat),
//            1 = CSV nicht gefunden / unlesbar.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(SCRIPT_DIR, '..');

// Standard-Standorte: Freebuff-Worktree und gebauter Worktree.
const DEFAULT_CSV_PATHS = [
	path.join(REPO_ROOT, 'WiesbadenReal', 'Saved', 'BuildHistory', 'CityBuilds.csv'),
	'C:\\freebuff\\WiesbadenReal_Sicherung\\WiesbadenReal\\Saved\\BuildHistory\\CityBuilds.csv',
];

// --- RFC-4180-Zeilenparser ---------------------------------------------------
// Kommata und Anfuehrungszeichen in gequoteten Feldern werden korrekt
// aufgeloest ("" innerhalb eines Feldes = ein Anfuehrungszeichen).
function parseCsvLine(Line)
{
	const Fields = [];
	let Field = '';
	let InQuotes = false;
	for (let i = 0; i < Line.length; ++i)
	{
		const Ch = Line[i];
		if (InQuotes)
		{
			if (Ch === '"')
			{
				if (Line[i + 1] === '"') { Field += '"'; ++i; }
				else { InQuotes = false; }
			}
			else { Field += Ch; }
		}
		else if (Ch === '"') { InQuotes = true; }
		else if (Ch === ',') { Fields.push(Field); Field = ''; }
		else { Field += Ch; }
	}
	Fields.push(Field);
	return Fields;
}

// --- Statistik ----------------------------------------------------------------

function statsOf(Durations)
{
	if (Durations.length === 0) { return null; }

	const Sorted = [...Durations].sort((A, B) => A - B);
	const Sum = Sorted.reduce((A, B) => A + B, 0);
	const Mid = Math.floor(Sorted.length / 2);
	const Median = Sorted.length % 2 === 1
		? Sorted[Mid]
		: (Sorted[Mid - 1] + Sorted[Mid]) / 2;

	return { Count: Sorted.length, Avg: Sum / Sorted.length, Median, Min: Sorted[0], Max: Sorted[Sorted.length - 1] };
}

function printStats(Title, Stats)
{
	if (!Stats)
	{
		console.log(`${Title}: keine Eintraege`);
		return;
	}
	console.log(`${Title}: ${Stats.Count} Laeufe, avg ${Stats.Avg.toFixed(1)} s, median ${Stats.Median.toFixed(1)} s, min ${Stats.Min.toFixed(1)} s, max ${Stats.Max.toFixed(1)} s`);
}

// --- Argumente ----------------------------------------------------------------

let CsvPath = null;
let TrendCount = 10;
for (let i = 2; i < process.argv.length; ++i)
{
	const Arg = process.argv[i];
	if (Arg === '--csv' && i + 1 < process.argv.length) { CsvPath = path.resolve(process.argv[++i]); }
	else if (Arg === '--trend' && i + 1 < process.argv.length)
	{
		const N = parseInt(process.argv[++i], 10);
		if (Number.isInteger(N) && N > 0) { TrendCount = N; }
		else { console.error(`--trend erwartet eine positive Zahl, bekam: ${process.argv[i]}`); process.exit(2); }
	}
	else { console.error(`Unbekanntes Argument: ${Arg}`); process.exit(2); }
}

if (!CsvPath)
{
	for (const Candidate of DEFAULT_CSV_PATHS)
	{
		if (fs.existsSync(Candidate)) { CsvPath = Candidate; break; }
	}
}

if (!CsvPath || !fs.existsSync(CsvPath))
{
	console.error('CityBuilds.csv nicht gefunden - entweder noch kein Build gelaufen oder anderer Pfad (--csv <pfad>).');
	process.exit(1);
}

const Text = fs.readFileSync(CsvPath, 'utf8');
const Lines = Text.split(/\r?\n/).filter((L) => L.trim().length > 0);
if (Lines.length <= 1)
{
	console.log(`Keine Eintraege in ${CsvPath} (nur Kopfzeile).`);
	process.exit(0);
}

const Header = parseCsvLine(Lines[0]);
const DurationIdx = Header.indexOf('DurationSeconds');
const SourceIdx = Header.indexOf('Source');
const ResultIdx = Header.indexOf('Result');
const TimestampIdx = Header.indexOf('Timestamp');
// Terrain-Warnspalten (seit 24-Spalten-Vertrag); -1 bei alten 22-Spalten-CSVs.
const TileTooLargeIdx = Header.indexOf('TerrainTileTooLarge');
const HeightRangeIdx = Header.indexOf('TerrainHeightRangeSuspicious');
if (DurationIdx < 0)
{
	console.error('Spalte DurationSeconds fehlt - ist das wirklich eine CityBuilds.csv?');
	process.exit(1);
}

const Rows = [];
for (const Line of Lines.slice(1))
{
	const Fields = parseCsvLine(Line);
	if (Fields.length <= DurationIdx) { continue; }

	const Duration = parseFloat(Fields[DurationIdx]);
	if (!Number.isFinite(Duration)) { continue; }

	Rows.push({
		Duration,
		Source: SourceIdx >= 0 && Fields[SourceIdx] ? Fields[SourceIdx] : '?',
		Result: ResultIdx >= 0 && Fields[ResultIdx] ? Fields[ResultIdx] : '?',
		Timestamp: TimestampIdx >= 0 && Fields[TimestampIdx] ? Fields[TimestampIdx] : '?',
		// Terrain-Warnungen (0/1-Spalten; fehlende Spalte = keine Warnung).
		TileTooLarge: TileTooLargeIdx >= 0 && Fields[TileTooLargeIdx] === '1',
		HeightRangeSuspicious: HeightRangeIdx >= 0 && Fields[HeightRangeIdx] === '1',
	});
}

if (Rows.length === 0)
{
	console.log(`Keine verwertbaren Eintraege in ${CsvPath}.`);
	process.exit(0);
}

// --- Auswertung ----------------------------------------------------------------

const BySource = new Map();
const ByOutcome = new Map();
for (const R of Rows)
{
	BySource.set(R.Source, [...(BySource.get(R.Source) || []), R.Duration]);
	const Outcome = R.Result.startsWith('ok') ? 'ok' : 'nicht ok';
	ByOutcome.set(Outcome, [...(ByOutcome.get(Outcome) || []), R.Duration]);
}

console.log(`Quelle: ${CsvPath}`);
console.log('');
printStats('Gesamt', statsOf(Rows.map((R) => R.Duration)));
console.log('');
console.log('Je Quelle:');
for (const [Source, Durations] of [...BySource.entries()].sort())
{
	printStats(`  ${Source}`, statsOf(Durations));
}
console.log('');
console.log('Je Ergebnis:');
for (const [Outcome, Durations] of [...ByOutcome.entries()].sort())
{
	printStats(`  ${Outcome}`, statsOf(Durations));
}

// --- Terrain-Qualitaet -----------------------------------------------------------
// Zaehlt die Warnspalten (TerrainTileTooLarge/TerrainHeightRangeSuspicious) ueber
// alle Laeufe - ein dauerhaftes 'Tile zu gross' deutet auf fehlenden Crop hin.

const WarnedRows = Rows.filter((R) => R.TileTooLarge || R.HeightRangeSuspicious);
if (WarnedRows.length > 0)
{
	const TileCount = Rows.filter((R) => R.TileTooLarge).length;
	const HeightCount = Rows.filter((R) => R.HeightRangeSuspicious).length;
	console.log('');
	console.log(`Terrain-Qualitaet: ${WarnedRows.length} von ${Rows.length} Laeufen mit Warnung (Tile zu gross: ${TileCount}, Hoehenspanne: ${HeightCount})`);
}

// --- Trend (ASCII-Balken) -------------------------------------------------------
// Die letzten TrendCount Laeufe (chronologisch, aelteste zuerst) als Balken
// proportional zur Dauer - Beschleunigungen/Regressionen auf einen Blick.
// Nicht-ok-Laeufe tragen ein '!' am Balkenende.

const TrendRows = Rows.slice(-TrendCount);
const MaxDuration = Math.max(...TrendRows.map((R) => R.Duration));
const BAR_MAX = 24; // Zeichen bei MaxDuration

console.log('');
console.log(`Trend (letzte ${TrendRows.length} Laeufe, aelteste zuerst; Skala: ${MaxDuration.toFixed(1)} s = ${BAR_MAX} Zeichen, ! = nicht ok):`);
TrendRows.forEach((R, Index) =>
{
	const BarLen = MaxDuration > 0 ? Math.max(1, Math.round((R.Duration / MaxDuration) * BAR_MAX)) : 0;
	const Flag = R.Result.startsWith('ok') ? '' : ' !';
	console.log(`  #${String(Index + 1).padStart(2)} ${R.Timestamp.padEnd(19)} ${R.Source.padEnd(7)} ${R.Duration.toFixed(1).padStart(7)} s  ${'#'.repeat(BarLen)}${Flag}`);
	// Crop-Probleme als eigene Zeile unter dem Lauf (einzeln oder kombiniert).
	const Warnings = [];
	if (R.TileTooLarge) { Warnings.push('Tile zu gross'); }
	if (R.HeightRangeSuspicious) { Warnings.push('Hoehenspanne'); }
	if (Warnings.length > 0)
	{
		console.log(`      -> Terrain-Warnung: ${Warnings.join(' + ')}`);
	}
});
