"""Sucht die Niagara-Emitter-Vorlagen und prueft, ob Python ein System daraus bauen kann.

Hintergrund: die Wetter-Systeme muessen von Hand gebaut werden, weil Unreal die
Niagara-Editor-API nicht nach Python exportiert. Bevor ich den Nutzer durch den
Editor schicke, will ich zwei Dinge schwarz auf weiss haben: wie die Vorlagen im
Assistenten WIRKLICH heissen (geraten ist geraten), und ob es doch eine
Abkuerzung ueber die Factory gibt.
"""

import unreal

OUT = r"C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Saved/probe_niagara_templates.txt"
lines = []


def log(s):
    lines.append(str(s))


AR = unreal.AssetRegistryHelpers.get_asset_registry()
AR.scan_paths_synchronous(["/Niagara"], True)

log("== Emitter-Vorlagen unter /Niagara/DefaultAssets/Templates/Emitters ==")
for data in AR.get_assets_by_path("/Niagara/DefaultAssets/Templates/Emitters", recursive=True):
    name = str(data.asset_name)
    asset = data.get_asset()
    spec = desc = cat = "?"
    for prop, dst in (("template_specification", "spec"),
                      ("template_asset_description", "desc"),
                      ("category", "cat")):
        try:
            v = asset.get_editor_property(prop)
        except Exception as e:
            v = f"<{e!r}>"
        if dst == "spec":
            spec = v
        elif dst == "desc":
            desc = v
        else:
            cat = v
    log(f"  {name}")
    log(f"      Art={spec}  Kategorie={cat}")
    log(f"      Beschreibung={str(desc)[:300]}")

log("")
log("== System-Vorlagen ==")
for data in AR.get_assets_by_path("/Niagara/DefaultAssets/Templates/Systems", recursive=True):
    log(f"  {data.asset_name}")

log("")
log("== Was Python an der System-Factory anfassen kann ==")
try:
    fac = unreal.NiagaraSystemFactoryNew()
    props = []
    for n in ("system_to_copy", "emitters_to_add_to_new_system", "emitter_to_copy"):
        try:
            props.append(f"{n} lesbar = {fac.get_editor_property(n)!r}")
        except Exception as e:
            props.append(f"{n}: {e!r}")
    for p in props:
        log("  " + p)

    # Schreibprobe: laesst sich ein Vorlagen-Emitter setzen?
    em = unreal.EditorAssetLibrary.load_asset(
        "/Niagara/DefaultAssets/Templates/Emitters/RecycleParticlesInView")
    log(f"  Vorlagen-Emitter geladen: {em!r}")
    try:
        fac.set_editor_property("emitters_to_add_to_new_system", [em])
        log("  SCHREIBEN GELUNGEN -> Abkuerzung moeglich")
    except Exception as e:
        log(f"  Schreiben verweigert: {e!r}")
except Exception as e:
    log(f"  Factory nicht nutzbar: {e!r}")

log("")
log("== NiagaraSystem: was Python sieht ==")
try:
    log("  " + ", ".join(sorted(n for n in dir(unreal.NiagaraSystem)
                                if not n.startswith("_")))[:2000])
except Exception as e:
    log(f"  {e!r}")

open(OUT, "w", encoding="utf-8").write("\n".join(lines))
for l in lines:
    unreal.log(l)
