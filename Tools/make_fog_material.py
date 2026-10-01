"""
Makes /Game/UI/M_FogOfWar: the post-process that shows fog of war over
everything on the board -- ground, trees, rocks and walls alike -- rather than
only on the ground as the decal did (TMBattleDirectorFog.cpp).

The game paints what its side sees into a picture the size of the board
(FogTex): red is how deep the fog is there (0 seen now, about 0.7 seen before,
about 0.93 never seen), green the edge of what the side sees, blue the edge of
what the unit being ordered sees. For every pixel on screen this finds the
spot of the board it shows (from its depth), reads the picture there, and:

  - in the fog: darkens it and drains most of its colour towards a cold blue,
    so what is out of sight reads as night;
  - in sight: brightens it a little (SeenBoost), so sight stands out as lit;
  - on an edge: a soft glowing line, pale for the side, gold for the unit.

  FogTex      the picture the game paints (a render target)
  BoardOrigin where the board's corner is, in the world (cm)
  AxisX/AxisY the board's two axes in the world, each divided by its length (1/cm)
  FogTint     the colour the fog drains towards
  SeenBoost   how much brighter what is in sight is drawn
  SideEdge / UnitEdge  the edge colours

Before the team outline (blendable priority -1), so outlines stay bright in
the fog. Made with the human's say-so (2026-09-30: "Yes, both"). Run it without
opening the editor; it replaces the material if it is there:

    UnrealEditor-Cmd.exe TacticalMasters.uproject -run=pythonscript
        -script="<full path>/Tools/make_fog_material.py" -unattended -nosplash
"""

import unreal

FOLDER = "/Game/UI"
NAME = "M_FogOfWar"
MEL = unreal.MaterialEditingLibrary


def say(text):
    unreal.log("FOG: " + text)


def main():
    path = FOLDER + "/" + NAME
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    for location in ("BL_SCENE_COLOR_AFTER_TONEMAPPING", "BL_AFTER_TONEMAPPING"):
        if hasattr(unreal.BlendableLocation, location):
            mat.set_editor_property("blendable_location", getattr(unreal.BlendableLocation, location))
            break
    try:
        mat.set_editor_property("blendable_priority", -1)
    except Exception as error:
        say("no blendable priority (%s): the outline may be fogged too" % error)

    def node(cls, col, row, **props):
        n = MEL.create_material_expression(mat, cls, -2000 + col * 260, row * 140)
        for key, value in props.items():
            n.set_editor_property(key, value)
        return n

    def link(a, a_out, b, b_in):
        if not MEL.connect_material_expressions(a, a_out, b, b_in):
            raise RuntimeError("could not connect %s.%s to %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))

    # Where on the board this pixel is: its place in the world, from the corner,
    # along each axis.
    world = node(unreal.MaterialExpressionWorldPosition, 0, 0)
    origin = node(unreal.MaterialExpressionVectorParameter, 0, 1, parameter_name="BoardOrigin",
                  default_value=unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    origin_rgb = node(unreal.MaterialExpressionComponentMask, 1, 1, r=True, g=True, b=True, a=False)
    link(origin, "", origin_rgb, "")
    local = node(unreal.MaterialExpressionSubtract, 2, 0)
    link(world, "", local, "A")
    link(origin_rgb, "", local, "B")
    uv_parts = []
    for i, name in enumerate(("AxisX", "AxisY")):
        axis = node(unreal.MaterialExpressionVectorParameter, 2, 2 + i * 2, parameter_name=name,
                    default_value=unreal.LinearColor(0.0001 if i == 0 else 0.0, 0.0 if i == 0 else 0.0001, 0.0, 0.0))
        axis_rgb = node(unreal.MaterialExpressionComponentMask, 3, 2 + i * 2, r=True, g=True, b=True, a=False)
        link(axis, "", axis_rgb, "")
        along = node(unreal.MaterialExpressionDotProduct, 4, 1 + i * 2)
        link(local, "", along, "A")
        link(axis_rgb, "", along, "B")
        uv_parts.append(along)
    uv = node(unreal.MaterialExpressionAppendVector, 5, 2)
    link(uv_parts[0], "", uv, "A")
    link(uv_parts[1], "", uv, "B")

    fog = node(unreal.MaterialExpressionTextureSampleParameter2D, 6, 2, parameter_name="FogTex")
    black = unreal.load_asset("/Engine/EngineResources/Black.Black")
    if black:
        fog.set_editor_property("texture", black)
    link(uv, "", fog, "UVs")
    depth = node(unreal.MaterialExpressionComponentMask, 7, 1, r=True, g=False, b=False, a=False)
    side = node(unreal.MaterialExpressionComponentMask, 7, 2, r=False, g=True, b=False, a=False)
    unit = node(unreal.MaterialExpressionComponentMask, 7, 3, r=False, g=False, b=True, a=False)
    for m in (depth, side, unit):
        link(fog, "RGB", m, "")

    # On the board or not: past its edge (the land round it, the sky) there is
    # nothing to have seen, so it reads as never seen, with no edge lines --
    # and no reading of the picture's border smeared across it.
    offset = node(unreal.MaterialExpressionSubtract, 6, 10, const_b=0.5)
    link(uv, "", offset, "A")
    away = node(unreal.MaterialExpressionAbs, 7, 10)
    link(offset, "", away, "")
    room = node(unreal.MaterialExpressionSubtract, 8, 10, const_a=0.5)
    link(away, "", room, "B")
    sharp = node(unreal.MaterialExpressionMultiply, 9, 10, const_b=400.0)
    link(room, "", sharp, "A")
    inside2 = node(unreal.MaterialExpressionSaturate, 10, 10)
    link(sharp, "", inside2, "")
    inside_u = node(unreal.MaterialExpressionComponentMask, 11, 10, r=True, g=False, b=False, a=False)
    inside_v = node(unreal.MaterialExpressionComponentMask, 11, 11, r=False, g=True, b=False, a=False)
    link(inside2, "", inside_u, "")
    link(inside2, "", inside_v, "")
    on_board = node(unreal.MaterialExpressionMin, 12, 10)
    link(inside_u, "", on_board, "A")
    link(inside_v, "", on_board, "B")
    outside_dark = node(unreal.MaterialExpressionScalarParameter, 12, 12, parameter_name="OutsideDark", default_value=0.7)
    depth_all = node(unreal.MaterialExpressionLinearInterpolate, 13, 10)
    link(outside_dark, "", depth_all, "A")
    link(depth, "", depth_all, "B")
    link(on_board, "", depth_all, "Alpha")
    side_on = node(unreal.MaterialExpressionMultiply, 13, 11)
    link(side, "", side_on, "A")
    link(on_board, "", side_on, "B")
    unit_on = node(unreal.MaterialExpressionMultiply, 13, 12)
    link(unit, "", unit_on, "A")
    link(on_board, "", unit_on, "B")

    # The picture as it was, and a cold, drained version of it for the fog.
    scene = node(unreal.MaterialExpressionSceneTexture, 6, 6, scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    scene_rgb = node(unreal.MaterialExpressionComponentMask, 7, 6, r=True, g=True, b=True, a=False)
    link(scene, "Color", scene_rgb, "")
    grey = node(unreal.MaterialExpressionDotProduct, 8, 7)
    weights = node(unreal.MaterialExpressionConstant3Vector, 7, 8, constant=unreal.LinearColor(0.3, 0.59, 0.11, 1.0))
    link(scene_rgb, "", grey, "A")
    link(weights, "", grey, "B")
    tint = node(unreal.MaterialExpressionVectorParameter, 8, 9, parameter_name="FogTint",
                default_value=unreal.LinearColor(0.2, 0.24, 0.36, 1.0))
    tint_rgb = node(unreal.MaterialExpressionComponentMask, 9, 9, r=True, g=True, b=True, a=False)
    link(tint, "", tint_rgb, "")
    night = node(unreal.MaterialExpressionMultiply, 9, 7)
    link(grey, "", night, "A")
    link(tint_rgb, "", night, "B")

    # In sight, a little brighter: scene * (1 + SeenBoost * (1 - depth)).
    boost = node(unreal.MaterialExpressionScalarParameter, 8, 4, parameter_name="SeenBoost", default_value=0.12)
    clear = node(unreal.MaterialExpressionOneMinus, 8, 3)
    link(depth_all, "", clear, "")
    lift = node(unreal.MaterialExpressionMultiply, 9, 4)
    link(boost, "", lift, "A")
    link(clear, "", lift, "B")
    gain = node(unreal.MaterialExpressionAdd, 10, 4, const_a=1.0)
    link(lift, "", gain, "B")
    lit = node(unreal.MaterialExpressionMultiply, 11, 5)
    link(scene_rgb, "", lit, "A")
    link(gain, "", lit, "B")

    fogged = node(unreal.MaterialExpressionLinearInterpolate, 12, 6)
    link(lit, "", fogged, "A")
    link(night, "", fogged, "B")
    link(depth_all, "", fogged, "Alpha")

    # The edges, glowing: added on top.
    side_colour = node(unreal.MaterialExpressionVectorParameter, 11, 9, parameter_name="SideEdge",
                       default_value=unreal.LinearColor(0.55, 0.8, 1.0, 1.0))
    unit_colour = node(unreal.MaterialExpressionVectorParameter, 11, 11, parameter_name="UnitEdge",
                       default_value=unreal.LinearColor(1.0, 0.78, 0.3, 1.0))
    side_rgb = node(unreal.MaterialExpressionComponentMask, 12, 9, r=True, g=True, b=True, a=False)
    unit_rgb = node(unreal.MaterialExpressionComponentMask, 12, 11, r=True, g=True, b=True, a=False)
    link(side_colour, "", side_rgb, "")
    link(unit_colour, "", unit_rgb, "")
    side_add = node(unreal.MaterialExpressionMultiply, 13, 9)
    link(side_rgb, "", side_add, "A")
    link(side_on, "", side_add, "B")
    unit_add = node(unreal.MaterialExpressionMultiply, 13, 11)
    link(unit_rgb, "", unit_add, "A")
    link(unit_on, "", unit_add, "B")
    with_side = node(unreal.MaterialExpressionAdd, 14, 8)
    link(fogged, "", with_side, "A")
    link(side_add, "", with_side, "B")
    with_unit = node(unreal.MaterialExpressionAdd, 15, 9)
    link(with_side, "", with_unit, "A")
    link(unit_add, "", with_unit, "B")
    MEL.connect_material_property(with_unit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    say("made " + path)


try:
    main()
    say("DONE")
except Exception as error:  # said, so a headless run shows why
    say("FAILED: %s" % error)
