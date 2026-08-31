#!/usr/bin/env node
// verify-worktree-sync.mjs
// ---------------------------------------------------------------------------
// Sync-Verifikation zwischen dem Freebuff-Worktree und dem gebauten Worktree.
//
// Vergleicht byte-genau (SHA-1) die Bereiche Source/Content/Tools beider
// Worktrees und warnt bei jeder Abweichung (nur lokal, nur gebaut,
// inhaltlich verschieden), damit kein Stand auseinanderlaeuft.
//
// Worktree-Layouts (bewusst unterschiedlich):
//   Freebuff : <repo>/WiesbadenReal/{Source,Content,Tools}  +  <repo>/Tools
//   Gebaut   : <built>/{Source,Content,Tools}               (Projekt = Root)
// Tools ist die Vereinigung von <repo>/Tools und <repo>/WiesbadenReal/Tools.
//
// Nutzung:
//   node Tools/verify-worktree-sync.mjs                 # nur vergleichen
//   node Tools/verify-worktree-sync.mjs --sync          # lokal -> gebaut kopieren
//   node Tools/verify-worktree-sync.mjs --built <pfad>  # anderen gebauten Pfad
//
// Exit-Code: 0 = synchron, 1 = Drift gefunden. Mit --sync werden fehlende und
// unterschiedliche Dateien von lokal nach gebaut kopiert (nie geloescht).
//
// Zusaetzlich laeuft ein Drift-Guard: Tools/verify_cityprompt.mjs (Node-Port
// des CityPrompt-Parsers) wird vor dem Vergleich ausgefuehrt und vergleicht
// sein Keyword-Inventar direkt mit GIS/CityPrompt.cpp. Eine Regel-Aenderung im
// C++-Parser ohne paralleles Port-Update bricht hier ab (Exit 1) - auch mit
// --sync, damit der veraltete Port nicht in den gebauten Worktree wandert.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(SCRIPT_DIR, '..');

// Gebauter Worktree (Claude-Original, Build-Kandidat) - per --built ueberschreibbar.
const DEFAULT_BUILT = 'C:\\freebuff\\WiesbadenReal_Sicherung\\WiesbadenReal';

// Bereiche: lokale Quellen (relativ zum Freebuff-Repo) -> Ziel-Ordner im gebauten Worktree.
const DOMAINS = [
	{ name: 'Source',  sources: ['WiesbadenReal/Source'],  target: 'Source' },
	{ name: 'Content', sources: ['WiesbadenReal/Content'], target: 'Content' },
	{ name: 'Tools',   sources: ['WiesbadenReal/Tools', 'Tools'], target: 'Tools' },
];

const IGNORED = new Set(['.DS_Store', 'Thumbs.db', 'desktop.ini']);

// --- Argumente ---------------------------------------------------------------

let bSync = false;
let builtRoot = DEFAULT_BUILT;
for (let i = 2; i < process.argv.length; ++i)
{
	const Arg = process.argv[i];
	if (Arg === '--sync') { bSync = true; }
	else if (Arg === '--built' && i + 1 < process.argv.length) { builtRoot = path.resolve(process.argv[++i]); }
	else { console.error(`Unbekanntes Argument: ${Arg}`); process.exit(2); }
}

// Dieses Skript lebt im Freebuff-Repo und vergleicht dessen Layout
// (WiesbadenReal/… + Root-Tools/) gegen den gebauten Worktree. Die Kopie im
// gebauten Worktree ist reine Paritaet und NICHT zum Ausfuehren gedacht:
// dort fehlt das Freebuff-Layout, ein Lauf wuerde still falsche Ergebnisse
// liefern. Stattdessen klar abbrechen.
const LocalSourceDir = path.join(REPO_ROOT, 'WiesbadenReal', 'Source');
if (!fs.existsSync(LocalSourceDir))
{
	console.error('FEHLER: Dieses Skript muss aus dem Freebuff-Worktree laufen.');
	console.error(`  Erwartetes Layout 'WiesbadenReal/Source' unter: ${REPO_ROOT}`);
	console.error('  Die Kopie unter dem gebauten Worktree ist nur fuer Paritaet (nicht ausfuehren).');
	process.exit(2);
}

// --- Drift-Guard: CityPrompt-Port --------------------------------------------
// Fuehrt den Node-Port des CityPrompt-Parsers aus. Dessen Inventar-Abgleich
// (TEXT-Literale in GIS/CityPrompt.cpp vs. Port-Keywords) schlaegt fehl, wenn
// der C++-Parser Regeln geaendert hat, ohne dass der Port mitgezogen wurde.

function RunCityPromptGuard()
{
	const Port = path.join(REPO_ROOT, 'Tools', 'verify_cityprompt.mjs');
	if (!fs.existsSync(Port))
	{
		return { ok: false, message: `Port fehlt: ${Port}` };
	}
	const Res = spawnSync(process.execPath, [Port, '--json'], { encoding: 'utf8', timeout: 120000 });
	if (Res.error)
	{
		return { ok: false, message: `Port-Aufruf fehlgeschlagen: ${Res.error.message}` };
	}
	try
	{
		const Json = JSON.parse(Res.stdout);
		return {
			ok: Res.status === 0 && Json.Failed === 0,
			checks: Json.Checks,
			failed: Json.Failed,
			failedNames: Json.Results.filter((R) => !R.Ok).map((R) => R.Name),
			message: '',
		};
	}
	catch (Err)
	{
		return { ok: false, message: `Port-Ausgabe unlesbar: ${Err.message}` };
	}
}

console.log('\n[Drift-Guard] CityPrompt-Port vs. C++-Parser (Tools/verify_cityprompt.mjs)');
const Guard = RunCityPromptGuard();
if (!Guard.ok)
{
	if (Guard.failedNames && Guard.failedNames.length > 0)
	{
		for (const Name of Guard.failedNames)
		{
			console.log(`  DRIFT        ${Name}`);
		}
		console.log(`\n${Guard.failed} von ${Guard.checks} Port-Checks fehlgeschlagen.`);
	}
	else
	{
		console.log(`  FEHLER       ${Guard.message}`);
	}
	console.log('\nDer Node-Port passt nicht zum C++-Parser (GIS/CityPrompt.cpp).');
	console.log('Tools/verify_cityprompt.mjs mitziehen, dann erneut pruefen/syncen.');
	process.exit(1);
}
console.log(`  OK (${Guard.checks} Checks, 0 Abweichung)`);

// --- Hilfsfunktionen ----------------------------------------------------------

// Streaming-SHA1 in Chunks: fs.readFileSync wuerde bei grossen Dateien
// (gebackene Map-Packages >2 GB, ERR_FS_FILE_TOO_LARGE) abbrechen.
function Sha1(filePath)
{
	const Hash = crypto.createHash('sha1');
	const Stat = fs.statSync(filePath);
	const Fd = fs.openSync(filePath, 'r');
	const Buf = Buffer.alloc(1 << 20); // 1 MiB
	let Offset = 0;
	try
	{
		while (Offset < Stat.size)
		{
			const Read = fs.readSync(Fd, Buf, 0, Buf.length, Offset);
			if (Read <= 0) { break; }
			Hash.update(Buf.subarray(0, Read));
			Offset += Read;
		}
	}
	finally
	{
		fs.closeSync(Fd);
	}
	return Hash.digest('hex');
}

// Liefert Map<relPath, sha1> fuer einen Ordner (relativ zu base).
function Walk(dir, base)
{
	const Out = new Map();
	if (!fs.existsSync(dir)) { return Out; }
	for (const Entry of fs.readdirSync(dir, { withFileTypes: true }))
	{
		if (IGNORED.has(Entry.name)) { continue; }
		const Full = path.join(dir, Entry.name);
		if (Entry.isDirectory())
		{
			for (const [K, V] of Walk(Full, base)) { Out.set(K, V); }
		}
		else if (Entry.isFile())
		{
			Out.set(path.relative(base, Full).split(path.sep).join('/'), Sha1(Full));
		}
	}
	return Out;
}

// Lokale Dateien eines Bereichs als Map<relPath, { abs, sha1 }>.
function LocalFiles(sources)
{
	const Out = new Map();
	for (const Src of sources)
	{
		const Abs = path.join(REPO_ROOT, Src);
		if (!fs.existsSync(Abs)) { continue; }
		for (const [Rel, Sha] of Walk(Abs, Abs)) { Out.set(Rel, { abs: path.join(Abs, Rel), sha1: Sha }); }
	}
	return Out;
}

// Gebaute Dateien eines Bereichs als Map<relPath, { abs, sha1 }>.
function BuiltFiles(target)
{
	const Abs = path.join(builtRoot, target);
	const Out = new Map();
	if (!fs.existsSync(Abs)) { return Out; }
	for (const [Rel, Sha] of Walk(Abs, Abs)) { Out.set(Rel, { abs: path.join(Abs, Rel), sha1: Sha }); }
	return Out;
}

// --- Vergleich ---------------------------------------------------------------

let TotalDrift = 0;
const SyncOps = [];

for (const Domain of DOMAINS)
{
	const Local = LocalFiles(Domain.sources);
	const Built = BuiltFiles(Domain.target);
	const OnlyLocal = [...Local.keys()].filter((K) => !Built.has(K));
	const OnlyBuilt = [...Built.keys()].filter((K) => !Local.has(K));
	const Different = [...Local.keys()].filter((K) => Built.has(K) && Built.get(K).sha1 !== Local.get(K).sha1);

	const Issues = OnlyLocal.length + OnlyBuilt.length + Different.length;
	TotalDrift += Issues;

	console.log(`\n[${Domain.name}] ${Issues === 0 ? 'OK' : `${Issues} Abweichung(en)`}`);

	for (const Rel of OnlyLocal)
	{
		console.log(`  NUR LOKAL    ${Domain.name}/${Rel}`);
		if (bSync) { SyncOps.push({ src: Local.get(Rel).abs, dst: path.join(builtRoot, Domain.target, Rel) }); }
	}
	for (const Rel of OnlyBuilt)
	{
		console.log(`  NUR GEBAUT   ${Domain.name}/${Rel}`);
	}
	for (const Rel of Different)
	{
		console.log(`  VERSCHIEDEN  ${Domain.name}/${Rel}`);
		if (bSync) { SyncOps.push({ src: Local.get(Rel).abs, dst: path.join(builtRoot, Domain.target, Rel) }); }
	}
}

// --- Ergebnis ----------------------------------------------------------------

if (TotalDrift === 0)
{
	console.log('\nSynchron: Source/Content/Tools beider Worktrees sind byte-identisch.');
	process.exit(0);
}

if (!bSync)
{
	console.log(`\n${TotalDrift} Abweichung(en) - Worktrees laufen auseinander.`);
	console.log('Zum Abgleich: node Tools/verify-worktree-sync.mjs --sync');
	process.exit(1);
}

// --- Sync ausfuehren ----------------------------------------------------------

let Copied = 0;
for (const Op of SyncOps)
{
	fs.mkdirSync(path.dirname(Op.dst), { recursive: true });
	fs.copyFileSync(Op.src, Op.dst);
	++Copied;
}
console.log(`\n${Copied} Datei(en) von lokal nach gebaut kopiert.`);
console.log('Hinweis: Nur-lokal-Dateien (NUR GEBAUT) werden nie geloescht - pruefen und manuell entfernen.');
process.exit(0);
