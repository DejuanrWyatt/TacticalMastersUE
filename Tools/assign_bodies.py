"""
Gives every class its own body from the heroes that are installed.

Content/Data/CharacterMap/characters.json holds a plan, "heroPlan": the hero
each class should wear (class id -> hero body name, e.g. "paladin": "greystone"),
or a list of them, best first, of which the first installed is worn -- so a
class wears a stand-in until its own hero is downloaded, and moves to it when
this is run again.
A hero comes with skins, and Tools/add_hero.py registers each skin as a body
of its own named "<hero>_<skin>" that shares the hero's animations. This hands
the classes planned for a hero its skins in turn -- the plain hero first, then
each skin -- so classes wearing the same hero still look different. The class
lands under "classes" in the map, which the director reads before "looks".

A class whose hero is not installed is left alone: it keeps wearing what its
look says. Running this again after downloading more heroes fills them in.
"heroLooks" says which hero each look should wear once installed, for classes
the plan does not name (a class made later in the class creator, say).

Plain Python, no editor needed:

    python Tools/assign_bodies.py            say what it would do
    python Tools/assign_bodies.py --write    write the map

Tools/add_hero.py runs it after adding heroes.
"""

import json
import os
import sys

MAP_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content", "Data", "CharacterMap", "characters.json")


def skins_of(the_map, hero):
    """The hero's bodies: the plain one first, then its skins by name."""
    bodies = the_map.get("bodies", {})
    if hero not in bodies:
        return []
    skins = sorted(b for b in bodies if b.startswith(hero + "_") and bodies[b].get("animations") == bodies[hero].get("animations"))
    return [hero] + skins


def chosen(the_map, wanted):
    """The first of the wanted heroes that is installed, else the first wanted."""
    wanted = wanted if isinstance(wanted, list) else [wanted]
    bodies = the_map.get("bodies", {})
    return next((hero for hero in wanted if hero in bodies), wanted[0])


def assign(the_map):
    """Fills "classes" and "looks" from the plan; returns lines saying what changed."""
    said = []
    plan = the_map.get("heroPlan", {})
    classes = the_map.setdefault("classes", {})
    by_hero = {}
    for job in sorted(plan):
        by_hero.setdefault(chosen(the_map, plan[job]), []).append(job)
    for hero in sorted(by_hero):
        wearers = by_hero[hero]
        bodies = skins_of(the_map, hero)
        if not bodies:
            said.append("%s: not installed, %d classes keep their look's body" % (hero, len(wearers)))
            continue
        for i, job in enumerate(wearers):
            classes[job] = bodies[i % len(bodies)]
        said.append("%s: %d classes over %d bodies (%s)" % (hero, len(wearers), len(bodies), ", ".join(bodies)))
    for look, wanted in sorted(the_map.get("heroLooks", {}).items()):
        hero = chosen(the_map, wanted)
        if hero in the_map.get("bodies", {}):
            the_map.setdefault("looks", {})[look] = hero
            said.append("look %s wears %s" % (look, hero))
    return said


def main(args):
    with open(MAP_FILE, encoding="utf-8") as f:
        the_map = json.load(f)
    for line in assign(the_map):
        print(line)
    if "--write" in args:
        with open(MAP_FILE, "w", encoding="utf-8") as f:
            json.dump(the_map, f, indent=2)
            f.write("\n")
        print("written to " + os.path.normpath(MAP_FILE))
    else:
        print("not written: add --write to write it")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
