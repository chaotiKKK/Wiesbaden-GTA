# Liest die AMTLICHE Dachform (bldg:roofType, AdV-Code) + Dachhoehe aus der
# nativen HVBG-LoD2-CityGML. Ersetzt das prozedurale/heuristische Dach-Raten.
#
# Jedes <bldg:Building gml:id="DEHE..."> traegt:
#   <bldg:roofType>CODE</bldg:roofType>       AdV-Dachform-Codeliste
#   <bldg:measuredHeight>..</bldg:measuredHeight>   Gebaeudehoehe (m)
#   gen:stringAttribute Firsthoehe / MittlereTraufHoehe (absolut, m) -> Dachhoehe
#   gen:stringAttribute ALKISOID = DEHE...  (== gml:id)
#
# Ausgabe je Gebaeude: {alkis_id: {"roof": osm_shape, "rh": dachhoehe_m, "h": hoehe_m}}
import re

# AdV-Dachform-Codeliste -> OSM roof:shape (die EOSMRoofShape des Spiels kennt:
# flat/gabled/hipped/pyramidal/skillion/dome; Mansarde/Mischform -> naechste Form).
ROOFTYPE_MAP = {
    "1000": "flat",        # Flachdach
    "2100": "skillion",    # Pultdach
    "2200": "skillion",    # versetztes Pultdach
    "3100": "gabled",      # Satteldach
    "3200": "hipped",      # Walmdach
    "3300": "hipped",      # Kruppelwalmdach (half-hipped -> hipped)
    "3400": "gabled",      # Mansardendach (keine eigene Form -> gabled)
    "3500": "pyramidal",   # Zeltdach
    "3600": "pyramidal",   # Kegeldach
    "3700": "dome",        # Kuppeldach
    "3800": "dome",        # Bogendach
    "3900": "pyramidal",   # Turmdach
    "4000": "gabled",      # Mischform (haeufigste geneigte Form)
    "5000": "flat",        # Sonstiges
    "9999": "flat",        # Sonstiges
}

_ID = re.compile(r'<bldg:Building\b[^>]*\bgml:id="([^"]+)"')
_ROOF = re.compile(r'<bldg:roofType[^>]*>\s*([0-9]+)\s*</bldg:roofType>')
_MH = re.compile(r'<bldg:measuredHeight[^>]*>\s*([-\d.]+)\s*</bldg:measuredHeight>')
_ALKISOID = re.compile(r'name="ALKISOID">\s*<gen:value>\s*(DEHE[0-9A-Za-z]+)')


def _attr(block, name):
    m = re.search(r'name="%s">\s*<gen:value>\s*([-\d.]+)\s*</gen:value>' % name, block)
    return float(m.group(1)) if m else None


def roof_shape_from_code(code):
    """AdV-roofType-Code -> OSM roof:shape (Fallback flat)."""
    return ROOFTYPE_MAP.get(str(code).strip(), "flat")


def _segment_info(seg):
    """Aus einem Gebaeude(teil)-Segment: (measuredHeight, roofCode, roofHeight)."""
    mhm = _MH.search(seg)
    mh = float(mhm.group(1)) if mhm else None
    rc = _ROOF.search(seg)
    code = rc.group(1) if rc else None
    first = _attr(seg, "Firsthoehe")
    traufe = _attr(seg, "MittlereTraufHoehe")
    rh = round(first - traufe, 2) if (first is not None and traufe is not None) else None
    # Ohne measuredHeight: aus First-Boden ableiten (fuer Segment-Vergleich).
    if mh is None and first is not None:
        ab = _attr(seg, "AbsoluteHoehe")
        if ab is not None:
            mh = round(first - ab, 2)
    return mh, code, rh


def parse_building_block(block):
    """Ein <bldg:Building>...</bldg:Building>-Block -> (alkis_id, info) oder (None, None).

    Mehrteil-Gebaeude (BuildingPart, ~12%): laut Produktbeschreibung 3DGM hat der
    HOECHSTE Teil (max measuredHeight = Boden->hoechster First) die massgebliche
    Hoehe/Dachform - NICHT der erste im XML. Wir waehlen daher das Segment mit der
    groessten measuredHeight."""
    idm = _ID.search(block)
    aid = idm.group(1) if idm else None
    if not aid:
        am = _ALKISOID.search(block)
        aid = am.group(1) if am else None
    if not aid:
        return None, None

    # Segmente: je BuildingPart eins; sonst der ganze Block (einfaches Gebaeude).
    segs = re.split(r'<bldg:BuildingPart\b', block)
    segs = segs[1:] if len(segs) > 1 else segs

    best = None  # (mh, code, rh)
    for seg in segs:
        mh, code, rh = _segment_info(seg)
        if mh is None:
            continue
        if best is None or mh > best[0]:
            best = (mh, code, rh)
    if best is None:
        best = _segment_info(block)  # kein mh gefunden -> Fallback ganzer Block

    mh, code, rh = best
    info = {"roof": roof_shape_from_code(code) if code else "flat"}
    if code:
        info["code"] = code
    if rh is not None and rh > 0.0:
        info["rh"] = rh
    if mh is not None and mh > 1.0:
        info["h"] = round(float(mh), 2)
    return aid, info
