"""
Makes /Game/UI/M_GroundIndicator: the decal that lays the move area and ability
indicators on the ground, under the units (TMBattleDirectorIndicators.cpp).

The game paints what to show into a picture the size of the board -- the area a
unit can walk, a range circle, a cone, a line -- and this decal projects that
picture straight down onto the ground, so it follows every step and slope and
units stand on top of it, as League of Legends draws its indicators.

  Paint  the picture the game paints (a render target), colour and opacity
  Glow   how brightly it shines, so it reads in shade as well as in sun
  Cliffs white near cliffs, where steep faces lose the marks; black elsewhere

2026-10-06 ("sharper lines", the human's pick C): projected straight down, the
picture also landed on steep faces -- a rock wall, a cliff -- as a long streak.
Now it fades out where the surface it lands on is steeper than about 50 degrees
(its slope worked out from how the surface's position changes pixel to pixel),
so marks paint the ground only.

2026-10-06 (v26 play test, "hard to see the movement ground indicators"): that
fade took the marks off every grass blade too (a blade stands nearly upright),
so in tall grass the walk area vanished. Now it fades by steepness only where
"Cliffs" is white: a mask the game paints within a metre of every tall step
(TMBattleDirectorIndicators.cpp, PaintCliffMask). Black by default, so the fog
decal, which shares this material, never fades.

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
    # Flat ground keeps the mark; a steep face loses it. The surface's normal from
    # the change of its world position across the screen (works in any decal mode).
    where = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1400, 500)
    across = MEL.create_material_expression(mat, unreal.MaterialExpressionDDX, -1200, 450)
    down = MEL.create_material_expression(mat, unreal.MaterialExpressionDDY, -1200, 550)
    MEL.connect_material_expressions(where, "", across, "")
    MEL.connect_material_expressions(where, "", down, "")
    facing = MEL.create_material_expression(mat, unreal.MaterialExpressionCrossProduct, -1000, 500)
    MEL.connect_material_expressions(across, "", facing, "A")
    MEL.connect_material_expressions(down, "", facing, "B")
    normal = MEL.create_material_expression(mat, unreal.MaterialExpressionNormalize, -850, 500)
    MEL.connect_material_expressions(facing, "", normal, "")
    upward = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -700, 500)
    upward.set_editor_property("r", False)
    upward.set_editor_property("g", False)
    upward.set_editor_property("b", True)
    upward.set_editor_property("a", False)
    MEL.connect_material_expressions(normal, "", upward, "")
    level = MEL.create_material_expression(mat, unreal.MaterialExpressionAbs, -550, 500)
    MEL.connect_material_expressions(upward, "", level, "")
    # |up| 0.64 (about 50 degrees) and steeper: gone; 0.8 and flatter: whole.
    less = MEL.create_material_expression(mat, unreal.MaterialExpressionSubtract, -400, 500)
    less.set_editor_property("const_b", 0.64)
    MEL.connect_material_expressions(level, "", less, "A")
    ramp = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 500)
    ramp.set_editor_property("const_b", 6.25)
    MEL.connect_material_expressions(less, "", ramp, "A")
    keep = MEL.create_material_expression(mat, unreal.MaterialExpressionSaturate, -100, 500)
    MEL.connect_material_expressions(ramp, "", keep, "")
    # Only near cliffs: 1 - Cliffs * (1 - keep), so where the mask is black the mark stays whole.
    cliffs = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, 750)
    cliffs.set_editor_property("parameter_name", "Cliffs")
    black = unreal.load_asset("/Engine/EngineResources/Black.Black")
    if black:
        cliffs.set_editor_property("texture", black)
    lost = MEL.create_material_expression(mat, unreal.MaterialExpressionOneMinus, 50, 500)
    MEL.connect_material_expressions(keep, "", lost, "")
    near = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, 200, 600)
    MEL.connect_material_expressions(lost, "", near, "A")
    MEL.connect_material_expressions(cliffs, "R", near, "B")
    kept = MEL.create_material_expression(mat, unreal.MaterialExpressionOneMinus, 350, 600)
    MEL.connect_material_expressions(near, "", kept, "")
    opacity = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, 500, 350)
    MEL.connect_material_expressions(paint, "A", opacity, "A")
    MEL.connect_material_expressions(kept, "", opacity, "B")
    MEL.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    say("made " + path)


try:
    main()
    say("DONE")
except Exception as error:
    say("FAILED: %s" % error)
