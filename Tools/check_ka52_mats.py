import unreal

DEST = "/Game/Vehicles/Ka52"
def log(m): unreal.log_warning("###WBKA52C### %s" % m)
EAL = unreal.EditorAssetLibrary

paths = EAL.list_assets(DEST, recursive=True)
log("Anzahl Assets: %d" % len(paths))
for p in sorted(paths):
    a = EAL.load_asset(p)
    log("%s  class=%s" % (p, a.get_class().get_name() if a else "LOAD-FEHLER"))
log("ENDE")
