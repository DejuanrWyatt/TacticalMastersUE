"""
What a packaged game needs cooked, written into Config/DefaultGame.ini.

The game finds most of its assets by name at run time -- a class's hero, its
clips, an ability's effect, a sound, a theme's meshes -- from the data files
under Content/Data. The cooker only cooks what a level refers to, so without
this none of those would be in a packaged game. This reads every data file,
takes every /Game path in it, and has the folders holding them always cooked
(with everything they use: materials, textures, skeletons).

Folders, not the assets one by one: listed as primary assets they were named
by their short names, and packs reuse those (every Paragon hero has an
"Ability_Q"), so the editor warned "Duplicate Asset ID" for each at startup
and the cook failed on them. A folder cooks a little more than was named
(a hero's other animations, say), which is the price of no clashes.

Only the bodies a class, a look or the default wears are listed from the
character map, not every skin in it: a skin nobody wears would only make the
build bigger. Run it again after installing classes or heroes, before packaging.

The data files themselves (JSON, PNG) are copied beside the game as they are
(DirectoriesToAlwaysStageAsNonUFS), since the game reads them as files.

Plain Python, no editor:

    python Tools/cook_list.py          write the list into Config/DefaultGame.ini
"""

import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "Content", "Data")
INI = os.path.join(ROOT, "Config", "DefaultGame.ini")
BEGIN = "; ---- written by Tools/cook_list.py: do not edit by hand ----"
END = "; ---- end of Tools/cook_list.py ----"

# Asset paths the game's code names itself (code_paths finds the rest in Source/TacticalMasters).
IN_CODE = [
    "/Game/UI/M_GroundIndicator.M_GroundIndicator",
    "/Game/UI/M_TeamOutline.M_TeamOutline",
    "/Game/UI/M_GroundVertex.M_GroundVertex",
]


def folders(paths):
    """The folders holding these assets, leaving out any inside another listed (a folder cooks all below it)."""
    dirs = sorted({p.split(".")[0].rsplit("/", 1)[0] for p in paths})
    kept = []
    for d in dirs:
        if not any(d == k or d.startswith(k + "/") for k in kept):
            kept.append(d)
    return kept


def code_paths(into):
    """Every "/Game/..." asset path written in the game's own C++ (ability looks, props, materials)."""
    for path in glob.glob(os.path.join(ROOT, "Source", "TacticalMasters", "*.*")):
        if not path.endswith((".cpp", ".h")):
            continue
        text = open(path, encoding="utf-8", errors="replace").read()
        for found in re.findall(r'"(/Game/[^"%*]+\.[^"%*]+)"', text):
            into.add(found)


def game_paths(value, into):
    """Every /Game asset path in a piece of JSON, however deep."""
    if isinstance(value, str):
        if value.startswith("/Game/"):
            into.add(value)
    elif isinstance(value, list):
        for item in value:
            game_paths(item, into)
    elif isinstance(value, dict):
        for item in value.values():
            game_paths(item, into)


def asset_exists(path):
    """Whether a /Game/Folder/Name.Name path (or a class, Name_C) is a file in Content."""
    package = path.split(".")[0]
    return os.path.exists(os.path.join(ROOT, "Content", package[len("/Game/"):] + ".uasset")) \
        or os.path.exists(os.path.join(ROOT, "Content", package[len("/Game/"):] + ".umap"))


def main():
    wanted = set(IN_CODE)
    code_paths(wanted)
    # The character map: the bodies worn, and their animation sets.
    the_map = json.load(open(os.path.join(DATA, "CharacterMap", "characters.json"), encoding="utf-8"))
    bodies = the_map.get("bodies", {})
    worn = set(the_map.get("classes", {}).values()) | set(the_map.get("looks", {}).values())
    worn.add(the_map.get("default", ""))
    sets = set()
    for name in sorted(worn):
        body = bodies.get(name)
        if body:
            game_paths(body.get("mesh"), wanted)
            sets.add(body.get("animations", name))
    for name in sorted(sets):
        game_paths(the_map.get("animations", {}).get(name), wanted)
    # Everything else under Content/Data: classes, sounds, themes, maps.
    for path in sorted(glob.glob(os.path.join(DATA, "**", "*.json"), recursive=True)):
        if os.path.basename(path) == "characters.json":
            continue
        try:
            game_paths(json.load(open(path, encoding="utf-8")), wanted)
        except (OSError, ValueError) as error:
            print("skipped %s: %s" % (os.path.relpath(path, ROOT), error))
    missing = sorted(p for p in wanted if not asset_exists(p))
    for path in missing:
        print("not in the project, left out: " + path)
    wanted = sorted(p for p in wanted if asset_exists(p))
    write_ini(wanted)
    print("%d assets named (%d bodies worn, %d animation sets)" % (len(wanted), len(worn), len(sets)))
    return 0


def write_ini(wanted):
    cook = folders(wanted)
    lines = [BEGIN,
             "[/Script/UnrealEd.ProjectPackagingSettings]",
             "; The game reads these as files: classes, maps, themes, icons, the character and sound maps.",
             "+DirectoriesToAlwaysStageAsNonUFS=(Path=\"Data\")",
             "+MapsToCook=(FilePath=\"/Game/Maps/Showcase\")",
             "; %d folders holding the %d assets the data files name, cooked with everything they use." % (len(cook), len(wanted))]
    lines += ["+DirectoriesToAlwaysCook=(Path=\"%s\")" % d for d in cook]
    lines += [END]
    text = open(INI, encoding="utf-8").read() if os.path.exists(INI) else ""
    if BEGIN in text:
        text = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END), lambda _: "\n".join(lines), text, flags=re.S)
    else:
        text = (text.rstrip() + "\n\n" if text.strip() else "") + "\n".join(lines) + "\n"
    with open(INI, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("%d folders always cooked -> %s" % (len(cook), os.path.relpath(INI, ROOT)))


if __name__ == "__main__":
    sys.exit(main())
