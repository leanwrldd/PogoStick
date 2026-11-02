# Imports the Pogo Stick art from C:\SF\PogoStick-assets into /PogoStick/ at the paths the
# C++ loads from (Source/PogoStick/Public/PogoStickAssets.h). Re-runnable: existing assets are
# reimported in place. Run headlessly with:
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript
#     -script=C:\SF\PogoStick\Tools\import_art.py -unattended -nosplash
import os
import unreal

SOURCE_DIR = r"C:\SF\PogoStick-assets"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(message):
    unreal.log(f"[PogoStick] {message}")


def import_file(filename, destination, options=None):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SOURCE_DIR, filename)
    task.destination_path = destination
    task.automated = True
    task.replace_existing = True
    task.save = False
    if options is not None:
        task.options = options
    asset_tools.import_asset_tasks([task])
    paths = list(task.imported_object_paths)
    if not paths:
        unreal.log_error(f"[PogoStick] Import failed: {filename}")
        return None
    return eal.load_asset(paths[0])


def import_texture(filename, destination, srgb, compression, group=None):
    texture = import_file(filename, destination)
    if texture is None:
        return None
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("compression_settings", compression)
    if group is not None:
        texture.set_editor_property("lod_group", group)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    eal.save_loaded_asset(texture, only_if_is_dirty=False)
    log(f"Texture {texture.get_path_name()}")
    return texture


# --- Textures -------------------------------------------------------------------------------
base_color = import_texture("T_PogoStick_BC.png", "/PogoStick/Textures", True, unreal.TextureCompressionSettings.TC_DEFAULT)
normal = import_texture("T_PogoStick_N.png", "/PogoStick/Textures", False, unreal.TextureCompressionSettings.TC_NORMALMAP)
orm = import_texture("T_PogoStick_ORM.png", "/PogoStick/Textures", False, unreal.TextureCompressionSettings.TC_MASKS)

# Item icons: UI textures, no mips, so they stay crisp in the inventory.
for icon in ("IconPogoStick_64.png", "IconPogoStick_256.png"):
    import_texture(icon, "/PogoStick/Icons", True, unreal.TextureCompressionSettings.TC_EDITOR_ICON,
                   unreal.TextureGroup.TEXTUREGROUP_UI)

# --- Material -------------------------------------------------------------------------------
material_path = "/PogoStick/Materials/M_PogoStick"
# In a commandlet the asset registry may not have scanned the mod yet, so does_asset_exist can
# say no for an asset that is on disk; try loading it directly. If it exists it is rewired in
# place: deleting and recreating it fails once the meshes reference it.
material = unreal.load_asset(material_path)
if material is not None:
    mel.delete_all_material_expressions(material)
else:
    material = asset_tools.create_asset("M_PogoStick", "/PogoStick/Materials", unreal.Material, unreal.MaterialFactoryNew())


def texture_sample(texture, sampler_type, y):
    expression = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, y)
    expression.set_editor_property("texture", texture)
    expression.set_editor_property("sampler_type", sampler_type)
    return expression


bc_sample = texture_sample(base_color, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -200)
mel.connect_material_property(bc_sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)

orm_sample = texture_sample(orm, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 100)
mel.connect_material_property(orm_sample, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
mel.connect_material_property(orm_sample, "G", unreal.MaterialProperty.MP_ROUGHNESS)
mel.connect_material_property(orm_sample, "B", unreal.MaterialProperty.MP_METALLIC)

n_sample = texture_sample(normal, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 400)
mel.connect_material_property(n_sample, "RGB", unreal.MaterialProperty.MP_NORMAL)

# No first-person (panini) projection here, on purpose. Wiring MF_PaniniProjection to the SML
# project's MPC_Panini made the cooked material unusable in game ("Tried to access an uncooked
# shader map ID", default checker material instead): the placeholder collection's ID doesn't
# match the game's, so the cooked shader map is never found. Keep this material free of
# base-game parameter collections.

mel.recompile_material(material)
eal.save_loaded_asset(material, only_if_is_dirty=False)
log(f"Material {material.get_path_name()}")

# --- Meshes ---------------------------------------------------------------------------------
def mesh_options():
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_animations", False)
    data = options.static_mesh_import_data
    data.set_editor_property("combine_meshes", True)
    data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("generate_lightmap_u_vs", False)
    # The Blender export is in metres (1 unit = 1 m), so scale x100 to Unreal's cm. At 1:1 the
    # parts come in 1/100 size and the thin spring wire is discarded as all-degenerate polygons.
    # Origins (each part's top edge, the body's at the grip) are kept exactly.
    data.set_editor_property("import_uniform_scale", 100.0)
    data.set_editor_property("import_translation", unreal.Vector(0, 0, 0))
    data.set_editor_property("import_rotation", unreal.Rotator(0, 0, 0))
    return options


for part in ("Body", "Spring", "Foot"):
    # Reimporting over an existing mesh reuses the import settings stored on it, not the
    # options passed in, so bring the stored scale in line first.
    existing = unreal.load_asset(f"/PogoStick/Meshes/SM_PogoStick_{part}")
    if existing is not None:
        existing.get_editor_property("asset_import_data").set_editor_property("import_uniform_scale", 100.0)
    mesh = import_file(f"SM_PogoStick_{part}.fbx", "/PogoStick/Meshes", mesh_options())
    if mesh is None:
        continue
    for index in range(len(mesh.static_materials)):
        mesh.set_material(index, material)
    eal.save_loaded_asset(mesh, only_if_is_dirty=False)
    box = mesh.get_bounding_box()
    log(f"Mesh {mesh.get_path_name()} slots={len(mesh.static_materials)} "
        f"min=({box.min.x:.1f},{box.min.y:.1f},{box.min.z:.1f}) max=({box.max.x:.1f},{box.max.y:.1f},{box.max.z:.1f})")

log("Import finished.")
