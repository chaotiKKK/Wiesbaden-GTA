"""Legt die Mischpult-Assets an: SoundClass-Hierarchie + Basis-SoundMix.

  /Game/Audio/Mix/SC_Master  (Eltern)
    -> SC_Music, SC_SFX, SC_Ambience, SC_UI, SC_Voice, SC_Vehicle
  /Game/Audio/Mix/SM_WbMaster  (leerer Basis-Mix; Overrides setzt das
     UWiesbadenAudioSubsystem zur Laufzeit)

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script="Tools/make_audio_mixer.py" -unattended -nop4 -nosplash
"""
import unreal

def log(m): unreal.log("###AUDIOMIX### %s" % m)

EAL = unreal.EditorAssetLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
DEST = "/Game/Audio/Mix"
CHILDREN = ["SC_Music", "SC_SFX", "SC_Ambience", "SC_UI", "SC_Voice", "SC_Vehicle"]

if not EAL.does_directory_exist(DEST):
    EAL.make_directory(DEST)

def make_asset(name, ue_class, factory):
    path = DEST + "/" + name
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    asset = ATH.create_asset(name, DEST, ue_class, factory)
    if asset is None:
        log("FEHLER: %s konnte nicht erzeugt werden" % name)
    return asset

# --- SoundClasses ---
sc_factory = unreal.SoundClassFactory()
classes = {}
for name in ["SC_Master"] + CHILDREN:
    classes[name] = make_asset(name, unreal.SoundClass, sc_factory)

master = classes.get("SC_Master")
child_objs = [classes[n] for n in CHILDREN if classes.get(n) is not None]
if master is not None:
    # Hierarchie: Master ist Elternklasse -> Master-Lautstaerke wirkt auf alle.
    master.set_editor_property("child_classes", child_objs)

for name, obj in classes.items():
    if obj is not None:
        EAL.save_loaded_asset(obj)

# --- Basis-SoundMix (leer) ---
mix = make_asset("SM_WbMaster", unreal.SoundMix, unreal.SoundMixFactory())
if mix is not None:
    EAL.save_loaded_asset(mix)

ok = all(classes.get(n) is not None for n in ["SC_Master"] + CHILDREN) and mix is not None
log("ENDE ok=%s Klassen=%d Mix=%s" % (ok, len([c for c in classes.values() if c]), mix is not None))
