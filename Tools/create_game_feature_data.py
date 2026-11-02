# Creates /PogoStick/PogoStick, the FGGameFeatureData asset a game feature mod needs in its
# content root (named exactly like the plugin). Re-runnable. Run headlessly with:
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript
#     -script=C:\SF\PogoStick\Tools\create_game_feature_data.py -unattended -nosplash
import unreal

PACKAGE_PATH = "/PogoStick"
ASSET_NAME = "PogoStick"
full_path = f"{PACKAGE_PATH}/{ASSET_NAME}"

if unreal.EditorAssetLibrary.does_asset_exist(full_path):
    unreal.log(f"[PogoStick] {full_path} already exists; nothing to do.")
else:
    feature_class = unreal.load_class(None, "/Script/FactoryGame.FGGameFeatureData")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", feature_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(ASSET_NAME, PACKAGE_PATH, feature_class, factory)
    if asset is None:
        unreal.log_error(f"[PogoStick] Failed to create {full_path}.")
    else:
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
        unreal.log(f"[PogoStick] Created {full_path} ({asset.get_class().get_name()}).")
