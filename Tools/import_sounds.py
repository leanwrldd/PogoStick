# Imports the Pogo Stick sounds from C:\SF\PogoStick-assets into /PogoStick/Sounds, at the paths
# the C++ loads from (Source/PogoStick/Public/PogoStickAssets.h). Separate from import_art.py so
# a sound change doesn't reimport the meshes. Re-runnable: existing sounds are reimported in place.
# Make the charge clicks first with Tools/slice_charge_clicks.py. Run headlessly with:
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript
#     -script=C:\SF\PogoStick\Tools\import_sounds.py -unattended -nosplash
import glob
import os
import unreal

SOURCE_DIR = r"C:\SF\PogoStick-assets"
SOUNDS = sorted(os.path.basename(path) for path in glob.glob(os.path.join(SOURCE_DIR, "S_Pogo_Charge_Click_*.wav")))

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary

for filename in SOUNDS:
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SOURCE_DIR, filename)
    task.destination_path = "/PogoStick/Sounds"
    task.automated = True
    task.replace_existing = True
    task.save = False
    asset_tools.import_asset_tasks([task])
    paths = list(task.imported_object_paths)
    if not paths:
        unreal.log_error(f"[PogoStick] Import failed: {filename}")
        continue
    sound = eal.load_asset(paths[0])
    # One-shots, never looped.
    sound.set_editor_property("looping", False)
    # The project default is Bink Audio, which has no decoder here ("Decoder for AudioFormat
    # 'BINKA' not found") and may not in the game either. PCM always decodes; these are tiny.
    sound.set_editor_property("sound_asset_compression_type", unreal.SoundAssetCompressionType.PCM)
    eal.save_loaded_asset(sound, only_if_is_dirty=False)
    unreal.log(f"[PogoStick] Sound {sound.get_path_name()} {sound.get_editor_property('duration'):.2f} s")

unreal.log("[PogoStick] Sound import finished.")
