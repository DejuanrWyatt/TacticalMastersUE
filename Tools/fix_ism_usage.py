"""
Turns on "Used with Instanced Static Meshes" for every material that the
themes' meshes wear, where it is off. Trees, grass and rocks round the board are
drawn as instanced meshes (TMBattleDirectorFoliage.cpp). The editor quietly
turns the flag on for itself, but a packaged game cannot: a material without
it is drawn with the default grey material instead. The packaged build of
2026-09-30 showed three such rocks (Paragon Monolith: M_ScanRock_Nordic_02_Inst,
M_ScanRock_Nordic_03_Inst, M_RockNordic_07).

A material instance has no flags of its own, so the flag is set on the base
material it comes from. Only what is under /Game is touched, and only materials
whose flag is off. Written at the human's request (2026-10-01: "write the
script"). Run it with the editor closed:

    UnrealEditor-Cmd.exe TacticalMasters.uproject -run=pythonscript
        -script="<full path>/Tools/fix_ism_usage.py" -unattended -nosplash
"""

import glob
import json
import os

import unreal

FLAG = "used_with_instanced_static_meshes"
ROOT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
THEMES = os.path.join(ROOT, "Content", "Data", "Themes")
# Named by the packaged game's log as drawn grey, in case no theme lists them.
NAMED = [
    "/Game/ParagonProps/Monolith/Rocks/Materials/M_ScanRock_Nordic_02_Inst.M_ScanRock_Nordic_02_Inst",
    "/Game/ParagonProps/Monolith/Rocks/Materials/M_ScanRock_Nordic_03_Inst.M_ScanRock_Nordic_03_Inst",
    "/Game/ParagonProps/Monolith/Rocks/Materials/M_RockNordic_07.M_RockNordic_07",
]


def say(text):
    unreal.log("ISM: " + text)


def game_paths(value, into):
    if isinstance(value, str):
        if value.startswith("/Game/"):
            into.add(value)
    elif isinstance(value, list):
        for item in value:
            game_paths(item, into)
    elif isinstance(value, dict):
        for item in value.values():
            game_paths(item, into)


def base_of(material):
    """The Material a material (or instance of one, however deep) comes from."""
    seen = 0
    while isinstance(material, unreal.MaterialInstance) and seen < 16:
        material = material.get_editor_property("parent")
        seen += 1
    return material if isinstance(material, unreal.Material) else None


def main():
    paths = set(NAMED)
    for path in sorted(glob.glob(os.path.join(THEMES, "*.json"))):
        try:
            game_paths(json.load(open(path, encoding="utf-8")), paths)
        except (OSError, ValueError) as error:
            say("skipped %s: %s" % (os.path.basename(path), error))
    bases = {}
    meshes = 0
    for path in sorted(paths):
        asset = unreal.load_asset(path)
        if asset is None:
            continue
        materials = []
        if isinstance(asset, unreal.StaticMesh):
            meshes += 1
            for slot in asset.get_editor_property("static_materials"):
                materials.append(slot.get_editor_property("material_interface"))
        elif isinstance(asset, unreal.MaterialInterface):
            materials.append(asset)
        for material in materials:
            base = base_of(material) if material else None
            if base and base.get_path_name().startswith("/Game/"):
                bases[base.get_path_name()] = base
    fixed = 0
    for name in sorted(bases):
        base = bases[name]
        if base.get_editor_property(FLAG):
            continue
        base.set_editor_property(FLAG, True)
        unreal.MaterialEditingLibrary.recompile_material(base)
        if unreal.EditorAssetLibrary.save_loaded_asset(base, only_if_is_dirty=False):
            fixed += 1
            say("turned on for " + name)
        else:
            say("COULD NOT SAVE " + name)
    say("%d meshes and materials looked at, %d base materials, %d changed" % (meshes, len(bases), fixed))


main()
