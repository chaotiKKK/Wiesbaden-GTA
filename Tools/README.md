# Tools — Datenbeschaffung

Die GIS-Pipeline (Phasen 1–3) liest zwei externe Datenquellen ein. Beide werden
**offline** beschafft und als Dateien im Projekt abgelegt (NICHT zur Laufzeit
heruntergeladen — die Overpass-Runtime-Abfrage in `UOSMDataParser` ist nur für
den Editor-Workflow gedacht und rate-limitiert).

## 1. OSM-Daten (Straßen, Gebäude, POIs)

Die exakte Overpass-Query steht in `UOSMDataParser::BuildOverpassQuery`
(Spezifikation 4.1). `overpass_fetch.mjs` baut sie identisch nach (bleibt bei
Query-Aenderungen dort synchron), laedt mit Retry/Backoff + User-Agent von
overpass-api.de (Fallback: overpass.kumi.systems), validiert das JSON und
speichert es atomar. `python` ist auf diesem Rechner ein Windows-Store-Alias
und funktioniert nicht — daher node statt des frueher referenzierten
`overpass_fetch.py`:

```bash
node overpass_fetch.mjs
# Optionen: --out <pfad>  --bbox south,west,north,east  --timeout <s>
# Default-BBox: 49.995,8.08,50.16,8.42 (Wiesbaden inkl. Vororte)
```

Ablage: `Data/Raw/OSM/wiesbaden.osm.json` (gitignored, gross — aktueller
Stand ~243 MB, pretty-printed, 1,97 Mio. elements). In `WiesbadenWorldBuilder`
bzw. `WiesbadenGameInstance` unter `OsmFilePath` auf diese Datei zeigen.

## 2. Höhendaten (DEM)

Quelle: **SRTM 1-Arc-Second (30 m)** aus dem AWS-Open-Data-Bucket „Skadi“
(Mapzen/AWS Terrain Tiles) — liefert fertige `.hgt.gz`-Kacheln ohne API-Key:

```bash
# Kachel je 1°x1°, benannt nach Nordwest-Ecke: N50E008.hgt.gz = 50N/8E
curl -o N50E008.hgt.gz https://elevation-tiles-prod.s3.amazonaws.com/skadi/N50/N50E008.hgt.gz
gunzip -k N50E008.hgt.gz   # -> N50E008.hgt (3601x3601 int16 big-endian, ~25,9 MB)
```

Wiesbaden-BBox (49.995-50.16 N, 8.08-8.42 E): `N50E008` (Kern) + `N49E008`
(Südrand) — **beide liegen bereits entpackt in `Data/Raw/DEM/`**.
`UHeightmapImporter` liest `.hgt` direkt (Auflösung aus Dateigröße, Ecke aus
Dateinamen); `DemFilePath` im WorldBuilder auf `N50E008.hgt` zeigen.

Nicht verfügbar/abgeschaltet: `srtm.kurviger.de` (keine .hgt-Downloads mehr),
`dds.cr.usgs.gov` (offline), OpenTopography (API-Key nötig),
`gdal_translate` (kein GDAL auf dem Rechner).

Ablage: `Data/Raw/DEM/` (gitignored, groß).

## 3. Satellitenbilder / LoD2-Gebäude (später)

- Sentinel-2: `scihub.copernicus.eu`
- LoD2-Gebäude Hessen: `geoportal.hessen.de`

Diese Quellen werden erst ab den Textur-/Landmarken-Phasen (10/11) benötigt.
