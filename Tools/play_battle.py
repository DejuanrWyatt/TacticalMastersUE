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
SECONDS = 75
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

    # The fight as a person would read it. This is the part that says whether
    # abilities are wired through to Unreal at all: the log is built from the
    # events the rules report, so a silent one means nothing went off.
    print("--- the log ---")
    print(director.battle_log())

    # One ability by hand, through the same door the computer's orders go.
    #
    # What this proves is the wiring: that an order reaches the rules, is accepted,
    # costs the caster its action and its meter, and comes back out as something
    # readable. It does not prove a blow lands, and often none does -- the computer
    # walks to the distance it likes to fight from but has no reason yet to press
    # an attack, so the enemy is usually out of reach and the only legal target is
    # a friend. An ability that reaches nobody is legal in the original too.
    # Whether a blow lands correctly is SimAbilityTest's job, and it checks 336 of
    # them against Godot. Choosing a target worth hitting arrives with the AI.
    #
    # The clock has to be run on until somebody is actually waiting to act.
    cast = None
    why = ""
    for _ in range(40):
        for unit in range(8):
            for slot in range(4):
                # The other side first, so what comes out is a blow landing
                # rather than a spell aimed at a friend and reaching nobody.
                enemies = [t for t in range(8) if (t < 4) != (unit < 4)]
                friends = [t for t in range(8) if (t < 4) == (unit < 4) and t != unit]
                for target in enemies + friends:
                    refused = director.order_ability_at(unit, slot, target)
                    if refused == "":
                        cast = (unit, slot, target)
                        break
                    why = refused
                if cast:
                    break
            if cast:
                break
        if cast:
            break
        director.step_ticks(TICKS_PER_SECOND)
    print("--- an ability ordered by hand: %s ---"
          % (str(cast) if cast else "none was legal (%s)" % why))
    if cast is None:
        raise RuntimeError("no ability could be used at all: " + why)
    print(director.describe_battle())
    print("--- the log after it ---")
    print(director.battle_log())

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
