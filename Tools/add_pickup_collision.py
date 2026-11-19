# Gives SM_PogoStick_Body a simple box collision. The dropped item (AFGItemPickup_Spawnable)
# draws the descriptor's mConveyorMesh, which is this mesh, and the "pick up" prompt comes from
# a trace against it: with no collision there is nothing to hit and the drop can't be picked up.
# The held stick is unaffected, since its components are set to NoCollision.
# import_art.py calls add_box_collision() after each reimport; run this on its own with:
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript
#     -script=C:\SF\Mods\PogoStick\Tools\add_pickup_collision.py -unattended -nosplash
import unreal

MESH_PATH = "/PogoStick/Meshes/SM_PogoStick_Body"


def add_box_collision(mesh):
    # StaticMeshEditorSubsystem (and the deprecated EditorStaticMeshLibrary, which forwards to
    # it) doesn't exist under -run=pythonscript, so write the box into the body setup directly:
    # one box over the mesh's bounds. The cook builds the physics data from it.
    body_setup = mesh.get_editor_property("body_setup")
    if body_setup is None:
        unreal.log_error(f"[PogoStick] {mesh.get_path_name()} has no body setup")
        return False
    bounds = mesh.get_bounding_box()
    size = bounds.max - bounds.min
    box = unreal.KBoxElem()
    box.set_editor_property("center", (bounds.min + bounds.max) * 0.5)
    # Thin parts (the shaft) are padded so the drop is easy to aim at.
    box.set_editor_property("x", max(size.x, 8.0))
    box.set_editor_property("y", max(size.y, 8.0))
    box.set_editor_property("z", max(size.z, 8.0))
    geometry = body_setup.get_editor_property("agg_geom")
    geometry.set_editor_property("box_elems", [box])
    geometry.set_editor_property("sphere_elems", [])
    geometry.set_editor_property("sphyl_elems", [])
    geometry.set_editor_property("convex_elems", [])
    body_setup.set_editor_property("agg_geom", geometry)
    body_setup.modify()
    mesh.modify()
    boxes = body_setup.get_editor_property("agg_geom").get_editor_property("box_elems")
    unreal.log(f"[PogoStick] Box collision on {mesh.get_path_name()}: {len(boxes)} box, "
               f"size ({size.x:.1f}, {size.y:.1f}, {size.z:.1f})")
    return len(boxes) == 1


if __name__ == "__main__":
    mesh = unreal.load_asset(MESH_PATH)
    if mesh is None:
        unreal.log_error(f"[PogoStick] {MESH_PATH} not found")
    elif add_box_collision(mesh):
        unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    else:
        unreal.log_error("[PogoStick] Could not add the box collision")
