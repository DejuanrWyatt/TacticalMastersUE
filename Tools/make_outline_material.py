"""
Makes /Game/UI/M_TeamOutline: the post-process that outlines allies in blue and
enemies in red (Docs/TeamOutline.md says how it works, and how to make it by
hand instead).

Each unit's body writes a stencil value, 1 for an ally and 2 for an enemy
(TMBattleDirectorLooks.cpp). A pixel is on the outline when it has no stencil
of its own but one of its four neighbours, Thickness pixels away, has; the
neighbour's value picks the colour. The game sets AllyColour and EnemyColour.

2026-10-06 (the human's pick A of "Sharper lines"): the outline stops at a hard
edge instead of fading, and a thin near-black rim (RimWidth pixels) runs just
outside it, so it reads on bright grass and dark ground alike.

The friend selected writes 4 (2026-10-06, "Selected unit" A): its line is the
ally colour breathing to near-white and back, by the Pulse the game sets each
frame (0 to 1 and back every 1.2 s).

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
    # 2026-10-06 (v24 play test, "jittery and less defined"): before the
    # temporal anti-aliasing, not after tonemapping. The stencil is drawn with
    # the anti-aliasing's jitter, so an outline added after it hopped a pixel
    # from frame to frame and was never smoothed; added before, TSR steadies and
    # smooths it like any other edge. (It is tonemapped now, so its colours are
    # lifted a little below.)
    for location in ("BL_SCENE_COLOR_AFTER_DOF", "BL_SCENE_COLOR_BEFORE_DOF", "BL_AFTER_DOF", "BL_BEFORE_DOF",
                     "BL_SCENE_COLOR_AFTER_TONEMAPPING", "BL_AFTER_TONEMAPPING"):
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

    # The stencil here and around it.
    def stencil_at(uv_node, uv_out, row):
        look = node(unreal.MaterialExpressionSceneTexture, 6, row, scene_texture_id=unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
        link(uv_node, uv_out, look, "UVs")
        red = node(unreal.MaterialExpressionComponentMask, 7, row, r=True, g=False, b=False, a=False)
        link(look, "Color", red, "")
        return red

    directions = ((1.0, 0.0), (-1.0, 0.0), (0.0, 1.0), (0.0, -1.0),
                  (0.7071, 0.7071), (-0.7071, 0.7071), (0.7071, -0.7071), (-0.7071, -0.7071))

    def ring_values(thickness_node, share, row0):
        """The stencil in eight directions, thickness * share pixels away."""
        reach = node(unreal.MaterialExpressionMultiply, 2, row0)
        link(pixel, "", reach, "A")
        scaled = node(unreal.MaterialExpressionMultiply, 1, row0 + 1, const_b=share)
        link(thickness_node, "", scaled, "A")
        link(scaled, "", reach, "B")
        values = []
        for k, (dx, dy) in enumerate(directions):
            way = node(unreal.MaterialExpressionConstant2Vector, 3, row0 + k, r=dx, g=dy)
            offset = node(unreal.MaterialExpressionMultiply, 4, row0 + k)
            link(reach, "", offset, "A")
            link(way, "", offset, "B")
            moved = node(unreal.MaterialExpressionAdd, 5, row0 + k)
            link(screen, "ViewportUV", moved, "A")
            link(offset, "", moved, "B")
            values.append(stencil_at(moved, "", row0 + k))
        return values

    def ring_max(thickness_node, share, row0):
        """The strongest stencil in eight directions, thickness * share pixels away."""
        best = None
        for k, value in enumerate(ring_values(thickness_node, share, row0)):
            if best is None:
                best = value
            else:
                larger = node(unreal.MaterialExpressionMax, 8, row0 + k)
                link(best, "", larger, "A")
                link(value, "", larger, "B")
                best = larger
        return best

    centre = stencil_at(screen, "ViewportUV", 0)
    # 2026-10-06: sixteen taps, eight ways at the full thickness and eight at half,
    # instead of four at the full thickness. A pixel's outline strength is how
    # many of them land on a body, so the edge fades over a pixel or two rather
    # than stepping, and thin parts (a bowstring, a staff) are no longer missed
    # between the four straight taps.
    taps = ring_values(thickness, 1.0, 50) + ring_values(thickness, 0.5, 60)
    nearby = None
    count = None
    for k, value in enumerate(taps):
        if nearby is None:
            nearby = value
        else:
            larger = node(unreal.MaterialExpressionMax, 9, 50 + k)
            link(nearby, "", larger, "A")
            link(value, "", larger, "B")
            nearby = larger
        present = node(unreal.MaterialExpressionSaturate, 10, 50 + k)
        link(value, "", present, "")
        if count is None:
            count = present
        else:
            more = node(unreal.MaterialExpressionAdd, 11, 50 + k)
            link(count, "", more, "A")
            link(present, "", more, "B")
            count = more
    # 2026-10-06 ("sharper"): any tap of the sixteen on a body is the full outline,
    # a hard edge; the taps still catch thin parts. (Three for full made it fade.)
    coverage = node(unreal.MaterialExpressionMultiply, 12, 50, const_b=1.0)
    link(count, "", coverage, "A")

    # On the outline: nothing here, something beside it.
    here = node(unreal.MaterialExpressionSaturate, 8, 0)
    link(centre, "", here, "")
    empty = node(unreal.MaterialExpressionOneMinus, 9, 0)
    link(here, "", empty, "")
    beside = node(unreal.MaterialExpressionSaturate, 10, 2)
    link(coverage, "", beside, "")
    mask = node(unreal.MaterialExpressionMultiply, 11, 1)
    link(empty, "", mask, "A")
    link(beside, "", mask, "B")

    # Its colour: an enemy's stencil is 2, an ally's 1 (and 4, the one selected).
    minus_one = node(unreal.MaterialExpressionSubtract, 10, 4, const_b=1.0)
    link(nearby, "", minus_one, "A")
    above_one = node(unreal.MaterialExpressionSaturate, 11, 4)
    link(minus_one, "", above_one, "")
    three_less = node(unreal.MaterialExpressionSubtract, 10, 3, const_a=3.0)
    link(nearby, "", three_less, "B")
    below_three = node(unreal.MaterialExpressionSaturate, 11, 3)
    link(three_less, "", below_three, "")
    enemy_part = node(unreal.MaterialExpressionMultiply, 12, 4)
    link(above_one, "", enemy_part, "A")
    link(below_three, "", enemy_part, "B")
    ally = node(unreal.MaterialExpressionVectorParameter, 10, 5, parameter_name="AllyColour",
                default_value=unreal.LinearColor(0.2, 0.55, 1.0, 1.0))
    enemy = node(unreal.MaterialExpressionVectorParameter, 10, 7, parameter_name="EnemyColour",
                 default_value=unreal.LinearColor(1.0, 0.18, 0.15, 1.0))
    colour = node(unreal.MaterialExpressionLinearInterpolate, 12, 5)
    link(ally, "", colour, "A")
    link(enemy, "", colour, "B")
    link(enemy_part, "", colour, "Alpha")
    colour_mask = node(unreal.MaterialExpressionComponentMask, 13, 5, r=True, g=True, b=True, a=False)
    link(colour, "", colour_mask, "")
    team_rgb = node(unreal.MaterialExpressionMultiply, 13, 6, const_b=1.35)
    link(colour_mask, "", team_rgb, "A")
    # The selected (4): toward near-white by Pulse.
    less_three = node(unreal.MaterialExpressionSubtract, 10, 66, const_b=3.0)
    link(nearby, "", less_three, "A")
    selected_near = node(unreal.MaterialExpressionSaturate, 11, 66)
    link(less_three, "", selected_near, "")
    pulse = node(unreal.MaterialExpressionScalarParameter, 11, 67, parameter_name="Pulse", default_value=0.0)
    breath = node(unreal.MaterialExpressionMultiply, 12, 66)
    link(selected_near, "", breath, "A")
    link(pulse, "", breath, "B")
    breath_amount = node(unreal.MaterialExpressionMultiply, 13, 66, const_b=0.85)
    link(breath, "", breath_amount, "A")
    near_white = node(unreal.MaterialExpressionConstant3Vector, 13, 67, constant=unreal.LinearColor(1.3, 1.32, 1.35, 1.0))
    colour_rgb = node(unreal.MaterialExpressionLinearInterpolate, 14, 66)
    link(team_rgb, "", colour_rgb, "A")
    link(near_white, "", colour_rgb, "B")
    link(breath_amount, "", colour_rgb, "Alpha")

    # The dark rim: off the body, off the outline, but a body within Thickness + RimWidth.
    rim_width = node(unreal.MaterialExpressionScalarParameter, 1, 70, parameter_name="RimWidth", default_value=1.25)
    rim_reach = node(unreal.MaterialExpressionAdd, 2, 70)
    link(thickness, "", rim_reach, "A")
    link(rim_width, "", rim_reach, "B")
    rim_count = None
    for k, value in enumerate(ring_values(rim_reach, 1.0, 72)):
        present = node(unreal.MaterialExpressionSaturate, 10, 72 + k)
        link(value, "", present, "")
        if rim_count is None:
            rim_count = present
        else:
            more = node(unreal.MaterialExpressionAdd, 11, 72 + k)
            link(rim_count, "", more, "A")
            link(present, "", more, "B")
            rim_count = more
    rim_near = node(unreal.MaterialExpressionSaturate, 12, 72)
    link(rim_count, "", rim_near, "")
    not_line = node(unreal.MaterialExpressionOneMinus, 12, 73)
    link(beside, "", not_line, "")
    rim_a = node(unreal.MaterialExpressionMultiply, 13, 72)
    link(empty, "", rim_a, "A")
    link(not_line, "", rim_a, "B")
    rim_b = node(unreal.MaterialExpressionMultiply, 14, 72)
    link(rim_a, "", rim_b, "A")
    link(rim_near, "", rim_b, "B")
    rim_opacity = node(unreal.MaterialExpressionScalarParameter, 14, 74, parameter_name="RimOpacity", default_value=0.85)
    rim_mask = node(unreal.MaterialExpressionMultiply, 15, 72)
    link(rim_b, "", rim_mask, "A")
    link(rim_opacity, "", rim_mask, "B")
    rim_colour = node(unreal.MaterialExpressionConstant3Vector, 15, 74, constant=unreal.LinearColor(0.01, 0.01, 0.015, 1.0))

    # Over the picture as it was: the rim, then the line.
    scene = node(unreal.MaterialExpressionSceneTexture, 12, 8, scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    scene_rgb = node(unreal.MaterialExpressionComponentMask, 13, 8, r=True, g=True, b=True, a=False)
    link(scene, "Color", scene_rgb, "")
    rimmed = node(unreal.MaterialExpressionLinearInterpolate, 14, 9)
    link(scene_rgb, "", rimmed, "A")
    link(rim_colour, "", rimmed, "B")
    link(rim_mask, "", rimmed, "Alpha")
    final = node(unreal.MaterialExpressionLinearInterpolate, 14, 6)
    link(rimmed, "", final, "A")
    link(colour_rgb, "", final, "B")
    link(mask, "", final, "Alpha")

    # The marked unit (stencil 3): the strongest stencil among eight neighbours
    # at HoverThickness and eight at half of it, less 2, is 1 only near a 3.
    hover_thickness = node(unreal.MaterialExpressionScalarParameter, 1, 10, parameter_name="HoverThickness", default_value=5.0)
    glow_thickness = node(unreal.MaterialExpressionScalarParameter, 1, 11, parameter_name="GlowThickness", default_value=11.0)
    hover_colour = node(unreal.MaterialExpressionVectorParameter, 10, 12, parameter_name="HoverColour",
                        default_value=unreal.LinearColor(1.0, 0.08, 0.05, 1.0))
    hover_mask_rgb = node(unreal.MaterialExpressionComponentMask, 11, 12, r=True, g=True, b=True, a=False)
    link(hover_colour, "", hover_mask_rgb, "")
    hover_rgb = node(unreal.MaterialExpressionMultiply, 11, 13, const_b=1.35)
    link(hover_mask_rgb, "", hover_rgb, "A")

    near_full = ring_max(hover_thickness, 1.0, 14)
    near_half = ring_max(hover_thickness, 0.5, 24)
    near_glow = ring_max(glow_thickness, 1.0, 34)
    near_both = node(unreal.MaterialExpressionMax, 9, 20)
    link(near_full, "", near_both, "A")
    link(near_half, "", near_both, "B")

    def is_marked(value, col, row):
        """1 for a 3 only: not for 4, the selected unit, which breathes instead."""
        less = node(unreal.MaterialExpressionSubtract, col, row, const_b=2.0)
        link(value, "", less, "A")
        clamp = node(unreal.MaterialExpressionSaturate, col + 1, row)
        link(less, "", clamp, "")
        upto = node(unreal.MaterialExpressionSubtract, col, row + 1, const_a=4.0)
        link(value, "", upto, "B")
        upto_clamp = node(unreal.MaterialExpressionSaturate, col + 1, row + 1)
        link(upto, "", upto_clamp, "")
        both = node(unreal.MaterialExpressionMultiply, col + 2, row)
        link(clamp, "", both, "A")
        link(upto_clamp, "", both, "B")
        return both

    # Not on a body at all (stencil 0 here), beside a marked one.
    hover_mask = node(unreal.MaterialExpressionMultiply, 11, 20)
    link(empty, "", hover_mask, "A")
    link(is_marked(near_both, 10, 21), "", hover_mask, "B")
    glow_mask = node(unreal.MaterialExpressionMultiply, 11, 34)
    link(empty, "", glow_mask, "A")
    link(is_marked(near_glow, 10, 35), "", glow_mask, "B")
    # 2026-10-06 ("sharper"): the glow beyond the marked unit's line is now faint.
    glow_soft = node(unreal.MaterialExpressionMultiply, 12, 34, const_b=0.12)
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
