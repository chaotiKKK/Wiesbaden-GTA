import unreal

def log(m):
    unreal.log("###GEO### %s" % m)

try:
    conv = unreal.GeoCoordinateConverter()
    conv.initialize_with_wiesbaden_origin()
    pts = [("Platte_shuttle", 50.13390, 8.22010),
           ("Platte_bldg", 50.13430, 8.22014),
           ("P140_check", 50.0929531, 8.2244511)]
    for (name, lat, lon) in pts:
        g = unreal.GeoCoordinate()
        g.set_editor_property("latitude", lat)
        g.set_editor_property("longitude", lon)
        g.set_editor_property("height", 0.0)
        w = conv.geo_to_unreal_ground(g)
        log("%s -> X=%.1f Y=%.1f Z=%.1f" % (name, w.x, w.y, w.z))
except Exception as e:
    log("FEHLER: %s" % e)
log("ENDE")