import unreal, os, json
def log(m): unreal.log("###L6W### %s" % m)
line = json.load(open(os.path.join(unreal.Paths.project_dir(),"Data","Raw","Bus","line6.json"), encoding="utf-8"))
conv = unreal.GeoCoordinateConverter(); conv.initialize_with_wiesbaden_origin()
stops = line["stops"]
for i, s in enumerate(stops):
    g = unreal.GeoCoordinate(); g.set_editor_property("latitude", s[0]); g.set_editor_property("longitude", s[1]); g.set_editor_property("height", 0.0)
    w = conv.geo_to_unreal_ground(g)
    log("stop %2d  lat=%.5f lon=%.5f -> X=%.0f Y=%.0f" % (i, s[0], s[1], w.x, w.y))
log("ENDE")