"""
Watches the computer play a battle inside Unreal, and says what happened.

The plain-C++ tests already check the rules against the Godot game, and they
run in seconds without an engine. This does the thing they cannot: it proves the
rules are actually wired to Unreal. The director is a real actor in a real level,
its orders go through the same door a person's would, and what comes out is the
positions of eight units that moved because something decided they should.

Run it without opening the editor:

    UnrealEditor-Cmd.exe TacticalMasters.uproject -run=pythonscript
        -script="Tools/play_battle.py" -unattended -nosplash

It changes nothing and saves nothing. Failing here means the C++ is sound and
the wiring is not, which is a different bug in a different place.
"""

import unreal

LEVEL_PATH = "/Game/Maps/Showcase"
# Ten ticks is a second of battle. Long enough that several units get a turn.
SECONDS = 20
TICKS_PER_SECOND = 10


def _director():
    """The battle director in the level, spawning one if there is not one."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.TMBattleDirector):
            return actor
    return actors.spawn_actor_from_class(
        unreal.TMBattleDirector, unreal.Vector(0.0, 0.0, 0.0)
    )


def main():
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
    director = _director()
    if director is None:
        raise RuntimeError("could not put a battle director in the level")

    # Both sides to the computer, so the battle plays itself.
    director.set_editor_property("computer_plays_team0", True)
    director.set_editor_property("computer_plays_team1", True)
    director.set_editor_property("computer_skill", "hard")

    director.build_battle()
    opening = director.describe_battle()
    print("--- the opening ---")
    print(opening)

    # In the editor the clock does not run on its own, which is deliberate: a
    # battle sitting in a level must not quietly play itself while it is being
    # built. So step it, and let the computer take whatever turns have come up.
    orders = 0
    for _ in range(SECONDS):
        director.step_ticks(TICKS_PER_SECOND)
        orders += director.play_computer_turns(64)

    closing = director.describe_battle()
    print("--- after %d seconds and %d orders ---" % (SECONDS, orders))
    print(closing)

    if orders <= 0:
        raise RuntimeError("the computer gave no orders at all")
    if closing == opening:
        raise RuntimeError("the battle is exactly where it started, so nothing was wired up")

    # Every unit standing where it started would mean orders were accepted and
    # then quietly did nothing, which is the failure worth catching here.
    moved = sum(
        1
        for before, after in zip(opening.splitlines(), closing.splitlines())
        if before != after
    )
    print("--- %d lines of the battle changed, over %d orders ---" % (moved, orders))
    if moved == 0:
        raise RuntimeError("orders were accepted but nothing about the battle changed")
    print("THE COMPUTER PLAYS A BATTLE INSIDE UNREAL")


main()
