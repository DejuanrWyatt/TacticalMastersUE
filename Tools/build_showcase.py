"""
Builds a lit tactical board with a Paragon hero standing on it.

Run inside the Unreal editor. This is the first time the rebuilt game is
something you can look at, so the point is the difference in fidelity: a proper
character under real lighting, seen from the angle the game is played at.

It is written to be run again. Everything it makes is named, and anything of
that name is cleared out first, so re-running it after a change replaces the
level rather than piling a second one on top.
"""

import unreal

LEVEL_PATH = "/Game/Maps/Showcase"
HERO_MESH = "/Game/ParagonSparrow/Characters/Heroes/Sparrow/Meshes/Sparrow"
# A standing idle, so she is alive rather than posed in reference.
HERO_IDLE = "/Game/ParagonSparrow/Characters/Heroes/Sparrow/Animations/idle_relaxed"

# The board, in Unreal's centimetres. The Godot game thinks in metres and one
# tile is one metre, so a tile is 100 units here.
TILE = 100.0
BOARD = 8
TILE_THICKNESS = 20.0
# How much a tile rises per height level.
STEP = 45.0

# Tag put on everything this script spawns, so a re-run knows what to remove.
TAG = "Showcase.Generated"


def log(message):
    unreal.log("[showcase] {}".format(message))


def editor_actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def level_editor():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def clear_generated():
    """Removes what a previous run left, so this can be run over and over."""
    removed = 0
    for actor in editor_actors().get_all_level_actors():
        if actor and actor.actor_has_tag(TAG):
            editor_actors().destroy_actor(actor)
            removed += 1
    if removed:
        log("cleared {} actors from a previous run".format(removed))


def spawn(actor_class, location, rotation=None, label=None):
    # Note the order: unreal.Rotator is (roll, pitch, yaw).
    rotation = rotation or unreal.Rotator(0.0, 0.0, 0.0)
    actor = editor_actors().spawn_actor_from_class(actor_class, location, rotation)
    if actor:
        actor.tags = [TAG]
        if label:
            actor.set_actor_label(label)
    return actor


def build_lighting():
    """
    Sun, sky and fog. Lumen needs something to bounce light off and a sky to
    take its ambient from; without the atmosphere and the skylight a Paragon
    character reads as flat and grey, which is the thing we are trying to fix.
    """
    sun = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 600),
                unreal.Rotator(0.0, -40.0, 20.0), "Sun")
    if sun:
        # Actors do not all expose their component as a named property, so ask
        # for it by class -- that works the same way for every one of them.
        light = sun.get_component_by_class(unreal.DirectionalLightComponent)
        light.set_intensity(5.0)
        light.set_light_color(unreal.LinearColor(1.0, 0.96, 0.88, 1.0))
        # Long shadows across the board read the height differences that the
        # rules already care about.
        light.set_editor_property("dynamic_shadow_distance_movable_light", 20000.0)

    spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), None, "Sky")

    sky_light = spawn(unreal.SkyLight, unreal.Vector(0, 0, 800), None, "SkyLight")
    if sky_light:
        component = sky_light.get_component_by_class(unreal.SkyLightComponent)
        component.set_editor_property("real_time_capture", True)
        component.set_intensity(3.0)

    fog = spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), None, "Fog")
    if fog:
        component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        component.set_editor_property("fog_density", 0.02)

    post = spawn(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), None, "PostProcess")
    if post:
        post.set_editor_property("unbound", True)
        settings = post.get_editor_property("settings")
        # Lumen, and a little exposure control so the board is not blown out.
        settings.set_editor_property("override_auto_exposure_min_brightness", True)
        settings.set_editor_property("auto_exposure_min_brightness", 0.6)
        settings.set_editor_property("override_auto_exposure_max_brightness", True)
        settings.set_editor_property("auto_exposure_max_brightness", 1.4)
        post.set_editor_property("settings", settings)

    log("lighting placed")


def build_board():
    """
    A board of cubes, one per tile, with a little height variation so the
    lighting has something to work with and the tactical read is visible.
    """
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    if not cube:
        log("could not load the engine cube; board not built")
        return

    half = (BOARD - 1) * 0.5
    for x in range(BOARD):
        for y in range(BOARD):
            # A low ridge across one corner, so height is not uniform.
            level = 0
            if x + y > BOARD + 1:
                level = 1
            if x + y > BOARD + 3:
                level = 2

            # A column from the ground up to this tile's height, not a slab
            # hovering at it: a raised slab leaves daylight under its edges.
            top = TILE_THICKNESS + level * STEP
            centre_z = top * 0.5

            world_x = (x - half) * TILE
            world_y = (y - half) * TILE

            tile = spawn(unreal.StaticMeshActor, unreal.Vector(world_x, world_y, centre_z),
                         None, "Tile_{}_{}".format(x, y))
            if not tile:
                continue
            component = tile.get_component_by_class(unreal.StaticMeshComponent)
            component.set_static_mesh(cube)
            # The engine cube is 100 units across; a hair under a tile leaves a
            # seam so the grid reads without drawing lines on it.
            component.set_world_scale3d(unreal.Vector(0.97, 0.97, top / 100.0))
            tile.set_mobility(unreal.ComponentMobility.STATIC)

    log("board built: {0}x{0} tiles".format(BOARD))


def place_hero():
    """
    The hero, standing on the board at the height of the tile under her.

    The yaw looks arbitrary and is not: the camera looks along yaw 45, and a
    Paragon mesh does not face along its actor's +X, so facing the camera
    means 135 rather than the -135 the geometry alone would suggest.
    """
    mesh = unreal.load_asset(HERO_MESH)
    if not mesh:
        log("could not load {} -- is the Paragon content in this project?".format(HERO_MESH))
        return None

    top_of_tile = TILE_THICKNESS
    hero = spawn(unreal.SkeletalMeshActor, unreal.Vector(0.0, 0.0, top_of_tile),
                 unreal.Rotator(0.0, 0.0, 135.0), "Hero_Sparrow")
    if hero:
        component = hero.get_component_by_class(unreal.SkeletalMeshComponent)
        component.set_skinned_asset_and_update(mesh)

        idle = unreal.load_asset(HERO_IDLE)
        if idle:
            component.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
            component.set_animation(idle)
            component.play(True)
        else:
            log("no idle animation found; she will stand in reference pose")
        log("hero placed")
    return hero


def place_camera():
    """
    The angle the game is played from: high enough to read the board, low
    enough that the character is a character rather than a token.
    """
    distance = BOARD * TILE * 1.15
    camera = spawn(unreal.CameraActor,
                   unreal.Vector(-distance * 0.7, -distance * 0.7, distance * 0.75),
                   unreal.Rotator(0.0, -32.0, 45.0), "TacticalCamera")
    if camera:
        component = camera.get_component_by_class(unreal.CameraComponent)
        component.set_editor_property("field_of_view", 40.0)
        # Put the editor viewport where the camera is, so opening the level
        # shows the shot rather than wherever the view was left.
        try:
            unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)                 .set_level_viewport_camera_info(
                    camera.get_actor_location(), camera.get_actor_rotation())
        except Exception as error:
            log("could not move the viewport: {}".format(error))
    log("camera placed")


def run():
    level_editor().new_level(LEVEL_PATH)
    log("new level at {}".format(LEVEL_PATH))

    clear_generated()
    build_lighting()
    build_board()
    place_hero()
    place_camera()

    level_editor().save_current_level()
    log("saved. Open {} to look at it.".format(LEVEL_PATH))


run()
