# Checks whether the SML project's copy of the Pioneer's first-person skeleton and animations
# hold real bone data, or are stand-ins. Run headlessly (no rendering needed):
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript
#     -script=C:\SF\PogoStick\Tools\probe_skeleton.py -unattended -nosplash
import unreal


def log(message):
    unreal.log(f"[PogoProbe] {message}")


base = "/Game/FactoryGame/Character/Player"
skeleton = unreal.load_asset(f"{base}/Mesh/1PCharacter_Skeleton")
mesh = unreal.load_asset(f"{base}/Mesh/SK_Pioneer_1P_01")
log(f"skeleton={skeleton}")

# Bones the skeleton knows about, via any animation on it (the skeleton itself exposes little to Python).
for name in ("RebargunIdle_01", "RebargunPrimary_02", "RebargunReload_02"):
    anim = unreal.load_asset(f"{base}/Animation/FirstPerson/{name}")
    if anim is None:
        log(f"{name}: missing")
        continue
    try:
        tracks = unreal.AnimationLibrary.get_animation_track_names(anim)
    except Exception as e:
        tracks = f"error {e}"
    log(f"{name}: length={anim.get_play_length():.2f} tracks={len(tracks) if isinstance(tracks, list) else tracks} "
        f"first={list(tracks)[:12] if isinstance(tracks, list) else ''}")
    try:
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, 0.0, unreal.AnimPoseEvaluationOptions())
        bones = unreal.AnimPoseExtensions.get_bone_names(pose)
        log(f"  pose bones={len(bones)} sample={[str(b) for b in bones[:15]]}")
        for bone in ("armbase", "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r", "hand_rSocket", "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l", "ik_hand_gun", "ik_hand_r", "ik_hand_l"):
            if bone in [str(x) for x in bones]:
                t = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD)
                r = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.LOCAL)
                log(f"  {bone} world={t.translation} rot={t.rotation.rotator()} | local={r.translation} rot={r.rotation.rotator()} parent={unreal.AnimPoseExtensions.get_parent_bone_name(pose, bone) if hasattr(unreal.AnimPoseExtensions, 'get_parent_bone_name') else '?'}")
    except Exception as e:
        log(f"  pose error {e}")

if mesh is not None:
    try:
        log(f"mesh lods={mesh.get_lod_num() if hasattr(mesh, 'get_lod_num') else '?'} bounds={mesh.get_bounds()}")
    except Exception as e:
        log(f"mesh info error {e}")
log("done")
