"""
Makes /Game/UI/M_GroundIndicator: the decal that lays the move area and ability
indicators on the ground, under the units (TMBattleDirectorIndicators.cpp).

The game paints what to show into a picture the size of the board -- the area a
unit can walk, a range circle, a cone, a line -- and this decal projects that
picture straight down onto the ground, so it follows every step and slope and
units stand on top of it, as League of Legends draws its indicators.

  Paint  the picture the game paints (a render target), colour and opacity
  Glow   how brightly it shines, so it reads in shade as well as in sun

Made with the human's say-so (materials are otherwise human-only work here).
Run it without opening the editor; it replaces the material if it is there:

    UnrealEditor-Cmd.exe TacticalMasters.uproject -run=pythonscript
        -script="<full path>/Tools/make_indicator_material.py" -unattended -nosplash
"""

import unreal

FOLDER = "/Game/UI"
NAME = "M_GroundIndicator"
MEL = unreal.MaterialEditingLibrary


def say(text):
    unreal.log("INDICATOR: " + text)


def main():
    path = FOLDER + "/" + NAME
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # Decals written straight through: colour, and emissive so it glows.
    for name in ("DBM_TRANSLUCENT", "DBM_DBUFFER_TRANSLUCENT_COLOR"):
        if hasattr(unreal, "DecalBlendMode") and hasattr(unreal.DecalBlendMode, name):
            try:
                mat.set_editor_property("decal_blend_mode", getattr(unreal.DecalBlendMode, name))
                break
            except Exception:
                pass

    paint = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    paint.set_editor_property("parameter_name", "Paint")
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")
    if white:
        paint.set_editor_property("texture", white)
    glow = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 300)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 2.0)
    shine = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -350, 150)
    MEL.connect_material_expressions(paint, "RGB", shine, "A")
    MEL.connect_material_expressions(glow, "", shine, "B")
    MEL.connect_material_property(paint, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(shine, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(paint, "A", unreal.MaterialProperty.MP_OPACITY)

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    say("made " + path)


try:
    main()
    say("DONE")
except Exception as error:
    say("FAILED: %s" % error)
