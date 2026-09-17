# Holt das amtliche DGM1 (HVBG WCS, ALS/LiDAR) fuer Wiesbaden und schreibt ein
# ESRI-ASCII-Grid (.asc) in WGS84 - genau das Format+CRS, das der Bake-DEM-Reader
# (UHeightmapImporter, .asc) erwartet. Ersetzt das grobe SRTM ~30m.
#
# Der WCS reprojiziert SERVER-seitig nach EPSG:4326 (OUTPUTCRS), daher keine
# eigene Umprojektion noetig. Coverage-Stufen: he_dgm1 (1m) .. he_dgm1_32 (32m).
#
# Aufruf: python fetch_dgm1_asc.py [coverage] [E0 N0 E1 N1] [out.asc]
import urllib.request, urllib.error, ssl, io, sys
import numpy as np
from PIL import Image
Image.MAX_IMAGE_PIXELS = None

WCS = "https://inspire-hessen.de/raster/dgm1/ows"
CTX = ssl.create_default_context()


def fetch_wgs84_tiff(coverage, e0, n0, e1, n1):
    u = ("%s?SERVICE=WCS&VERSION=2.0.1&REQUEST=GetCoverage&COVERAGEID=%s"
         "&SUBSET=E(%d,%d)&SUBSET=N(%d,%d)&FORMAT=image/tiff"
         "&OUTPUTCRS=http://www.opengis.net/def/crs/EPSG/0/4326"
         % (WCS, coverage, e0, e1, n0, n1))
    r = urllib.request.urlopen(urllib.request.Request(u, headers={"User-Agent": "Mozilla/5.0"}),
                               timeout=300, context=CTX)
    return r.read()


def tiff_to_asc(tiff_bytes, out_path):
    im = Image.open(io.BytesIO(tiff_bytes))
    a = np.array(im).astype(np.float32)            # Zeile 0 = Nord (oben)
    scale = im.tag_v2.get(33550)                   # (dx, dy, dz) in Grad
    tie = im.tag_v2.get(33922)                     # (i,j,k, lon,lat,h) Eckbezug
    dx = float(scale[0]); dy = float(scale[1])
    top_lon = float(tie[3]); top_lat = float(tie[4])
    nrows, ncols = a.shape
    cell = round((dx + dy) / 2.0, 12)              # .asc verlangt quadratische Zellen
    xll = top_lon
    yll = top_lat - nrows * dy
    nodata = -9999.0
    a = np.where(np.isfinite(a), a, nodata)
    with open(out_path, "w", encoding="ascii") as f:
        f.write("ncols %d\n" % ncols)
        f.write("nrows %d\n" % nrows)
        f.write("xllcorner %.10f\n" % xll)
        f.write("yllcorner %.10f\n" % yll)
        f.write("cellsize %.12f\n" % cell)
        # Kleinschreibung: der C++-Reader (UHeightmapImporter) matcht den Key
        # case-sensitiv gegen "nodata_value"/"nodata" - "NODATA_value" wuerde
        # uebersehen und -9999 als echte Hoehe interpretiert.
        f.write("nodata_value %d\n" % int(nodata))
        np.savetxt(f, a, fmt="%.2f")
    valid = a[a != nodata]
    return ncols, nrows, cell, (float(valid.min()), float(valid.max())) if valid.size else (0, 0)


def main():
    coverage = sys.argv[1] if len(sys.argv) > 1 else "he_dgm1_8"
    if len(sys.argv) > 5:
        e0, n0, e1, n1 = (int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]))
    else:
        e0, n0, e1, n1 = 436000, 5538000, 456000, 5556000   # Wiesbaden UTM32
    out = sys.argv[6] if len(sys.argv) > 6 else "wiesbaden_dgm1.asc"
    print("WCS %s  E(%d,%d) N(%d,%d) -> WGS84 ..." % (coverage, e0, e1, n0, n1), flush=True)
    b = fetch_wgs84_tiff(coverage, e0, n0, e1, n1)
    print("GeoTIFF %.1f MB, schreibe %s ..." % (len(b) / 1e6, out), flush=True)
    ncols, nrows, cell, (zmin, zmax) = tiff_to_asc(b, out)
    print("WROTE %s  %dx%d  cell %.2e Grad (~%.1f m)  Hoehe %.1f..%.1f m"
          % (out, ncols, nrows, cell, cell * 111320, zmin, zmax))


main()
