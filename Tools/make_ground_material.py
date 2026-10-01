"""
Makes /Game/UI/M_GroundVertex: the material the smooth ground is drawn with
(TMBattleDirectorGround.cpp). The game works out every colour itself -- the
theme's ground by height, its bank colour on slopes, a slow mottle -- and puts
it in the mesh's vertex colours; this material simply shows them, matte.

  Base colour  the vertex colour
  Roughness    0.9, so the ground reads as earth and stone rather than plastic

Made with the human's say-so (materials are otherwise human-only work here).
Run it without opening the editor; it replaces the material if it is there:

    UnrealEditor-Cmd.exe TacticalMasters.uproject -run=pythonscript
        -script="<full path>/Tools/make_ground_material.py" -unattended -nosplash
"""

import unreal

FOLDER = "/Game/UI"
NAME = "M_GroundVertex"
MEL = unreal.MaterialEditingLibrary


def say(text):
    unreal.log("GROUND: " + text)


def main():
    path = FOLDER + "/" + NAME
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())

    colour = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -500, 0)
    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 200)
    rough.set_editor_property("r", 0.9)
    spec = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 300)
    spec.set_editor_property("r", 0.25)
    MEL.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    say("made " + path)


try:
    main()
    say("DONE")
except Exception as error:
    say("FAILED: %s" % error)
