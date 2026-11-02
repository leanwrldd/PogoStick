# Inspects the Pioneer's first-person rig and a stock equipment montage, and exports the rig plus
# reference animations as FBX for authoring the Pogo Stick charge animation in Blender.
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript
#     -script=C:\SF\PogoStick\Tools\inspect_anim.py -unattended -nosplash
import os
import unreal

EXPORT_DIR = r"C:\SF\PogoStick-assets\rig"
eal = unreal.EditorAssetLibrary


def log(message):
    unreal.log(f"[PogoAnim] {message}")


skeleton = unreal.load_asset("/Game/FactoryGame/Character/Player/Mesh/1PCharacter_Skeleton")
mesh = unreal.load_asset("/Game/FactoryGame/Character/Player/Mesh/SK_Pioneer_1P_01")
log(f"skeleton={skeleton} mesh={mesh}")
if mesh is not None:
    log(f"mesh skeleton={mesh.skeleton.get_path_name() if mesh.skeleton else None}")
    bones = unreal.SkeletalMeshEditorSubsystem if hasattr(unreal, "SkeletalMeshEditorSubsystem") else None

for path in ("/Game/FactoryGame/Character/Player/Animation/FirstPerson/RebargunPrimary_02_Montage",
             "/Game/FactoryGame/Character/Player/Animation/FirstPerson/RebargunReload_02_Montage",
             "/Game/FactoryGame/Character/Player/Animation/FirstPerson/RebargunEquip_01_Montage"):
    montage = unreal.load_asset(path)
    if montage is None:
        log(f"missing {path}")
        continue
    log(f"{path}: members with slot/track/group: {[m for m in dir(montage) if any(k in m.lower() for k in ('slot', 'track', 'group', 'blend'))]}")
    for getter in ("get_group_name", "get_slot_names", "get_anim_composite_section_names"):
        try:
            log(f"  {getter} = {getattr(montage, getter)()}")
        except Exception as e:
            log(f"  {getter} failed: {e}")
    try:
        log(f"  AnimMontageLibrary? {[m for m in dir(unreal) if 'Montage' in m]}")
    except Exception as e:
        log(f"  {e}")

for path in ("/Game/FactoryGame/Character/Player/Animation/FirstPerson/RebargunIdle_01",
             "/Game/FactoryGame/Character/Player/Animation/FirstPerson/RebargunPrimary_02"):
    anim = unreal.load_asset(path)
    if anim is None:
        log(f"missing {path}")
        continue
    log(f"{path}: frames={anim.get_editor_property('number_of_sampled_frames') if hasattr(anim, 'number_of_sampled_frames') else '?'} "
        f"length={anim.get_play_length():.2f} additive={anim.get_editor_property('additive_anim_type')}")

# FBX exports for Blender: the rig with its mesh, and the idle the charge should start from.
os.makedirs(EXPORT_DIR, exist_ok=True)


def export(asset, filename, options):
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = os.path.join(EXPORT_DIR, filename)
    task.automated = True
    task.replace_identical = True
    task.prompt = False
    task.options = options
    ok = unreal.Exporter.run_asset_export_task(task)
    log(f"export {filename}: {ok}")


fbx = unreal.FbxExportOption()
fbx.set_editor_property("ascii", False)
fbx.set_editor_property("export_morph_targets", False)
if mesh is not None:
    export(mesh, "SK_Pioneer_1P_01.fbx", fbx)
for name in ("RebargunIdle_01", "RebargunPrimary_02"):
    anim = unreal.load_asset(f"/Game/FactoryGame/Character/Player/Animation/FirstPerson/{name}")
    if anim is not None:
        export(anim, f"{name}.fbx", fbx)
log("done")
