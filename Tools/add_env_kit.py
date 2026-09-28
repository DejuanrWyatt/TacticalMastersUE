"""
Turns an environment pack from Fab into a theme that builds the battlefield
from the pack's own meshes.

A theme (Content/Data/Themes, Docs/Maps.md) can name a "kit": meshes for the
ground's surface, for what stands on a rock tile, for trees and for boulders
around the board. The board fits each mesh to its place by the mesh's own size,
so any pack works; this finds the pack's candidates by the names artists give
things (Floor, Pillar, Rock, Tree...) and by their shape, and writes a theme
from an existing one with the kit added. It never edits an asset.

Run it without opening the editor (Tools\\AddKit.bat wraps this):

    UnrealEditor-Cmd.exe TacticalMasters.uproject -run=pythonscript
        -script="<path>/Tools/add_env_kit.py <pack folder> <theme id> [--base ruined_keep] [--name "Agora"] [--write]"

    <pack folder>  where the pack's meshes are, e.g. /Game/ParagonAgora
    <theme id>     the new theme's id, e.g. agora
    --base         the theme whose colours, sun and fog it starts from
    --name         what the setup screen calls it
    --write        write the theme; without it, only says what it found

Then play a battle in it (-tmtheme=<id>) and look: a guess by name can be wrong,
and the theme is plain JSON to correct.
"""

import json
import os
import re
import sys

import unreal

THEMES = os.path.join(unreal.Paths.project_content_dir(), "Data", "Themes")
SKIP = re.compile(r"(^UCX_|_LOD\d|collision|proxy|decal|_dummy|_blockout|hlod|_Cap$)", re.IGNORECASE)
ROLES = {
    "top": re.compile(r"(floor|tile|ground|pave|paving|slab|flagstone|platform|plate|cobble)", re.IGNORECASE),
    "rock": re.compile(r"(pillar|column|ruin|statue|obelisk|monolith|rock|boulder|stone|cliff|crystal|block|wall_?broken)", re.IGNORECASE),
    "tree": re.compile(r"(tree|pine|oak|palm|birch|willow|spruce|cypress|conifer)", re.IGNORECASE),
    "boulder": re.compile(r"(rock|boulder|stone|pebble)", re.IGNORECASE),
}
MOST = 8


def say(text):
    unreal.log("ADD KIT: " + text)


def meshes(folder):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([folder], True)
    out = []
    for a in registry.get_assets_by_path(folder, recursive=True):
        if str(a.asset_class_path.asset_name) != "StaticMesh" or SKIP.search(str(a.asset_name)):
            continue
        path = str(a.package_name) + "." + str(a.asset_name)
        mesh = unreal.load_asset(path)
        if mesh is None:
            continue
        box = mesh.get_bounding_box()
        size = box.max - box.min
        out.append((str(a.asset_name), path, size.x, size.y, size.z))
    return out


def sort(found):
    kit = {"top": [], "rock": [], "tree": [], "boulder": []}
    for name, path, x, y, z in sorted(found):
        wide = max(x, y, 1.0)
        flat = z < 0.25 * min(x, y)
        tall = z > 0.8 * wide
        if ROLES["tree"].search(name) and tall:
            kit["tree"].append(path)
        elif ROLES["top"].search(name) and flat and 50 < wide < 1000:
            kit["top"].append(path)
        elif ROLES["rock"].search(name) and 60 < wide < 800 and z > 100:
            # Cover: something a unit could hide behind.
            kit["rock"].append(path)
        elif ROLES["boulder"].search(name) and not tall and wide > 50:
            kit["boulder"].append(path)
    return {k: v[:MOST] for k, v in kit.items()}


def main(args):
    write = "--write" in args
    args = [a for a in args if a != "--write"]
    base, name = "ruined_keep", None
    rest = []
    i = 0
    while i < len(args):
        if args[i] == "--base" and i + 1 < len(args):
            base = args[i + 1]
            i += 2
        elif args[i] == "--name" and i + 1 < len(args):
            name = args[i + 1]
            i += 2
        else:
            rest.append(args[i])
            i += 1
    if len(rest) < 2 or not re.fullmatch(r"[a-z0-9_]{1,40}", rest[1]):
        say("usage: add_env_kit.py <pack folder> <theme id> [--base ruined_keep] [--name Name] [--write]")
        return 1
    folder, theme_id = rest[0].rstrip("/"), rest[1]
    base_file = os.path.join(THEMES, base + ".theme.json")
    if not os.path.exists(base_file):
        say("no theme called " + base + " to start from")
        return 1
    found = meshes(folder)
    if not found:
        say("no static meshes under " + folder + ": is the pack installed from Fab?")
        return 1
    kit = sort(found)
    say("%d meshes under %s" % (len(found), folder))
    for role, paths in kit.items():
        say("  %s: %d" % (role, len(paths)))
        for p in paths:
            say("    " + p)
    with open(base_file, encoding="utf-8") as f:
        theme = json.load(f)
    theme["name"] = name or theme_id.replace("_", " ").title()
    theme["kit"] = {k: v for k, v in kit.items() if v}
    theme["kit"].update({"rockFill": 0.9, "treeHeight": 6, "boulderSize": 2})
    if write:
        out = os.path.join(THEMES, theme_id + ".theme.json")
        with open(out, "w", encoding="utf-8") as f:
            json.dump(theme, f, indent=2)
            f.write("\n")
        say("written to " + out)
    else:
        say("not written: add --write to write it")
    return 0


result = main(sys.argv[1:])
say("DONE" if result == 0 else "FAILED")
