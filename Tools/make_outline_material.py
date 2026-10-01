"""
Makes /Game/UI/M_TeamOutline: the post-process that outlines allies in blue and
enemies in red (Docs/TeamOutline.md says how it works, and how to make it by
hand instead).

Each unit's body writes a stencil value, 1 for an ally and 2 for an enemy
(TMBattleDirectorLooks.cpp). A pixel is on the outline when it has no stencil
of its own but one of its four neighbours, Thickness pixels away, has; the
neighbour's value picks the colour. The game sets AllyColour and EnemyColour.

A unit under the pointer, or one an ability being aimed would touch, writes 3:
it gets a thick outline in HoverColour (HoverThickness pixels, eight
neighbours, so it stays round at the corners), with a soft glow beyond it and a
faint tint on the body, as League of Legends marks what the pointer is on.

Made with the human's say-so (materials are otherwise human-only work here).
Run it without opening the editor; it replaces the material if it is there:

    UnrealEditor-Cmd.exe TacticalMasters.uproject -run=pythonscript
        -script="<full path>/Tools/make_outline_material.py" -unattended -nosplash
"""

import unreal

FOLDER = "/Game/UI"
NAME = "M_TeamOutline"
MEL = unreal.MaterialEditingLibrary


def say(text):
    unreal.log("OUTLINE: " + text)


def main():
    path = FOLDER + "/" + NAME
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    # After tonemapping, so the outline is exactly the colour asked for.
    for location in ("BL_SCENE_COLOR_AFTER_TONEMAPPING", "BL_AFTER_TONEMAPPING"):
        if hasattr(unreal.BlendableLocation, location):
            mat.set_editor_property("blendable_location", getattr(unreal.BlendableLocation, location))
            break

    x = [-1800]

    def node(cls, col, row, **props):
        n = MEL.create_material_expression(mat, cls, -1800 + col * 260, row * 140)
        for key, value in props.items():
            n.set_editor_property(key, value)
        return n

    def link(a, a_out, b, b_in):
        if not MEL.connect_material_expressions(a, a_out, b, b_in):
            raise RuntimeError("could not connect %s.%s to %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))

    # One pixel's size, times the thickness.
    screen = node(unreal.MaterialExpressionScreenPosition, 0, 0)
    size = node(unreal.MaterialExpressionViewSize, 0, 2)
    pixel = node(unreal.MaterialExpressionDivide, 1, 2, const_a=1.0)
    link(size, "", pixel, "B")
    thickness = node(unreal.MaterialExpressionScalarParameter, 1, 3, parameter_name="Thickness", default_value=2.0)
    step = node(unreal.MaterialExpressionMultiply, 2, 2)
    link(pixel, "", step, "A")
    link(thickness, "", step, "B")
    step_x = node(unreal.MaterialExpressionComponentMask, 3, 1, r=True, g=False, b=False, a=False)
    step_y = node(unreal.MaterialExpressionComponentMask, 3, 3, r=False, g=True, b=False, a=False)
    link(step, "", step_x, "")
    link(step, "", step_y, "")
    zero = node(unreal.MaterialExpressionConstant, 3, 5, r=0.0)
    across = node(unreal.MaterialExpressionAppendVector, 4, 1)
    link(step_x, "", across, "A")
    link(zero, "", across, "B")
    down = node(unreal.MaterialExpressionAppendVector, 4, 3)
    link(zero, "", down, "A")
    link(step_y, "", down, "B")

    # The stencil here and at the four neighbours.
    def stencil_at(uv_node, uv_out, row):
        look = node(unreal.MaterialExpressionSceneTexture, 6, row, scene_texture_id=unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
        link(uv_node, uv_out, look, "UVs")
        red = node(unreal.MaterialExpressionComponentMask, 7, row, r=True, g=False, b=False, a=False)
        link(look, "Color", red, "")
        return red

    centre = stencil_at(screen, "ViewportUV", 0)
    around = []
    for i, (op, offset) in enumerate(((unreal.MaterialExpressionAdd, across), (unreal.MaterialExpressionSubtract, across),
                                       (unreal.MaterialExpressionAdd, down), (unreal.MaterialExpressionSubtract, down))):
        moved = node(op, 5, 1 + i)
        link(screen, "ViewportUV", moved, "A")
        link(offset, "", moved, "B")
        around.append(stencil_at(moved, "", 1 + i))
    max_a = node(unreal.MaterialExpressionMax, 8, 1)
    link(around[0], "", max_a, "A")
    link(around[1], "", max_a, "B")
    max_b = node(unreal.MaterialExpressionMax, 8, 3)
    link(around[2], "", max_b, "A")
    link(around[3], "", max_b, "B")
    nearby = node(unreal.MaterialExpressionMax, 9, 2)
    link(max_a, "", nearby, "A")
    link(max_b, "", nearby, "B")

    # On the outline: nothing here, something beside it.
    here = node(unreal.MaterialExpressionSaturate, 8, 0)
    link(centre, "", here, "")
    empty = node(unreal.MaterialExpressionOneMinus, 9, 0)
    link(here, "", empty, "")
    beside = node(unreal.MaterialExpressionSaturate, 10, 2)
    link(nearby, "", beside, "")
    mask = node(unreal.MaterialExpressionMultiply, 11, 1)
    link(empty, "", mask, "A")
    link(beside, "", mask, "B")

    # Its colour: an enemy's stencil is 2, an ally's 1.
    minus_one = node(unreal.MaterialExpressionSubtract, 10, 4, const_b=1.0)
    link(nearby, "", minus_one, "A")
    enemy_part = node(unreal.MaterialExpressionSaturate, 11, 4)
    link(minus_one, "", enemy_part, "")
    ally = node(unreal.MaterialExpressionVectorParameter, 10, 5, parameter_name="AllyColour",
                default_value=unreal.LinearColor(0.2, 0.55, 1.0, 1.0))
    enemy = node(unreal.MaterialExpressionVectorParameter, 10, 7, parameter_name="EnemyColour",
                 default_value=unreal.LinearColor(1.0, 0.18, 0.15, 1.0))
    colour = node(unreal.MaterialExpressionLinearInterpolate, 12, 5)
    link(ally, "", colour, "A")
    link(enemy, "", colour, "B")
    link(enemy_part, "", colour, "Alpha")
    colour_rgb = node(unreal.MaterialExpressionComponentMask, 13, 5, r=True, g=True, b=True, a=False)
    link(colour, "", colour_rgb, "")

    # Over the picture as it was.
    scene = node(unreal.MaterialExpressionSceneTexture, 12, 8, scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    scene_rgb = node(unreal.MaterialExpressionComponentMask, 13, 8, r=True, g=True, b=True, a=False)
    link(scene, "Color", scene_rgb, "")
    final = node(unreal.MaterialExpressionLinearInterpolate, 14, 6)
    link(scene_rgb, "", final, "A")
    link(colour_rgb, "", final, "B")
    link(mask, "", final, "Alpha")

    # The marked unit (stencil 3): the strongest stencil among eight neighbours
    # at HoverThickness and eight at half of it, less 2, is 1 only near a 3.
    hover_thickness = node(unreal.MaterialExpressionScalarParameter, 1, 10, parameter_name="HoverThickness", default_value=5.0)
    glow_thickness = node(unreal.MaterialExpressionScalarParameter, 1, 11, parameter_name="GlowThickness", default_value=11.0)
    hover_colour = node(unreal.MaterialExpressionVectorParameter, 10, 12, parameter_name="HoverColour",
                        default_value=unreal.LinearColor(1.0, 0.08, 0.05, 1.0))
    hover_rgb = node(unreal.MaterialExpressionComponentMask, 11, 12, r=True, g=True, b=True, a=False)
    link(hover_colour, "", hover_rgb, "")

    directions = ((1.0, 0.0), (-1.0, 0.0), (0.0, 1.0), (0.0, -1.0),
                  (0.7071, 0.7071), (-0.7071, 0.7071), (0.7071, -0.7071), (-0.7071, -0.7071))

    def ring_max(thickness_node, share, row0):
        """The strongest stencil in eight directions, thickness * share pixels away."""
        reach = node(unreal.MaterialExpressionMultiply, 2, row0)
        link(pixel, "", reach, "A")
        scaled = node(unreal.MaterialExpressionMultiply, 1, row0 + 1, const_b=share)
        link(thickness_node, "", scaled, "A")
        link(scaled, "", reach, "B")
        best = None
        for k, (dx, dy) in enumerate(directions):
            way = node(unreal.MaterialExpressionConstant2Vector, 3, row0 + k, r=dx, g=dy)
            offset = node(unreal.MaterialExpressionMultiply, 4, row0 + k)
            link(reach, "", offset, "A")
            link(way, "", offset, "B")
            moved = node(unreal.MaterialExpressionAdd, 5, row0 + k)
            link(screen, "ViewportUV", moved, "A")
            link(offset, "", moved, "B")
            value = stencil_at(moved, "", row0 + k)
            if best is None:
                best = value
            else:
                larger = node(unreal.MaterialExpressionMax, 8, row0 + k)
                link(best, "", larger, "A")
                link(value, "", larger, "B")
                best = larger
        return best

    near_full = ring_max(hover_thickness, 1.0, 14)
    near_half = ring_max(hover_thickness, 0.5, 24)
    near_glow = ring_max(glow_thickness, 1.0, 34)
    near_both = node(unreal.MaterialExpressionMax, 9, 20)
    link(near_full, "", near_both, "A")
    link(near_half, "", near_both, "B")

    def is_marked(value, col, row):
        less = node(unreal.MaterialExpressionSubtract, col, row, const_b=2.0)
        link(value, "", less, "A")
        clamp = node(unreal.MaterialExpressionSaturate, col + 1, row)
        link(less, "", clamp, "")
        return clamp

    # Not on a body at all (stencil 0 here), beside a marked one.
    hover_mask = node(unreal.MaterialExpressionMultiply, 11, 20)
    link(empty, "", hover_mask, "A")
    link(is_marked(near_both, 10, 21), "", hover_mask, "B")
    glow_mask = node(unreal.MaterialExpressionMultiply, 11, 34)
    link(empty, "", glow_mask, "A")
    link(is_marked(near_glow, 10, 35), "", glow_mask, "B")
    glow_soft = node(unreal.MaterialExpressionMultiply, 12, 34, const_b=0.35)
    link(glow_mask, "", glow_soft, "A")

    # The body itself, faintly tinted: its own stencil is 3.
    body_mask = node(unreal.MaterialExpressionMultiply, 12, 36, const_b=0.18)
    link(is_marked(centre, 10, 36), "", body_mask, "A")

    with_glow = node(unreal.MaterialExpressionLinearInterpolate, 15, 8)
    link(final, "", with_glow, "A")
    link(hover_rgb, "", with_glow, "B")
    link(glow_soft, "", with_glow, "Alpha")
    with_body = node(unreal.MaterialExpressionLinearInterpolate, 16, 8)
    link(with_glow, "", with_body, "A")
    link(hover_rgb, "", with_body, "B")
    link(body_mask, "", with_body, "Alpha")
    marked = node(unreal.MaterialExpressionLinearInterpolate, 17, 8)
    link(with_body, "", marked, "A")
    link(hover_rgb, "", marked, "B")
    link(hover_mask, "", marked, "Alpha")
    MEL.connect_material_property(marked, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    say("made " + path)


try:
    main()
    say("DONE")
except Exception as error:  # said, so a headless run shows why
    say("FAILED: %s" % error)
