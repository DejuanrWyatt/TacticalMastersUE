"""
Turns a Paragon hero from Fab into a body and an animation set in the
character map, so classes can wear it.

A Paragon hero comes with its own skeleton, mesh and a full set of clips, so it
needs no retargeting: it only has to be written down which clip means what.
That is what this does, by the names Epic gave the clips (Jog_Fwd, Primary_Fire,
Death_Bwd...). It never edits an asset; it reads them and writes
Content/Data/CharacterMap/characters.json.

Run it without opening the editor (Tools\\AddHero.bat wraps this):

    UnrealEditor-Cmd.exe TacticalMasters.uproject -run=pythonscript
        -script="Tools/add_hero.py <hero folder> <body name> [look ...] [--write]"

    <hero folder>  where the hero's files are, e.g.
                   /Game/ParagonGreystone/Characters/Heroes/Greystone
    <body name>    what the character map calls it, e.g. greystone
    look ...       the looks that should wear it (knight, squire, monk, archer,
                   black_mage, white_mage)
    --write        write the map; without it, only says what it would write

Then film it (Tools\\AnimCatalog.bat) and watch the strips in the class
creator: a guess by name can be wrong, and the map is plain JSON to correct.
"""

import json
import os
import re
import sys

import unreal

MAP_FILE = os.path.join(unreal.Paths.project_content_dir(), "Data", "CharacterMap", "characters.json")


def say(text):
    unreal.log("ADD HERO: " + text)


def object_path(asset_data):
    return str(asset_data.package_name) + "." + str(asset_data.asset_name)


def is_additive(sequence):
    # Made to be layered over another pose; played alone it is a broken pose.
    return sequence.get_editor_property("additive_anim_type") != unreal.AdditiveAnimationType.AAT_NONE


def find(folder):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([folder], True)
    found = registry.get_assets_by_path(folder, recursive=True)
    meshes = [a for a in found if str(a.asset_class_path.asset_name) == "SkeletalMesh"]
    clips = {}
    skipped = []
    for a in found:
        if str(a.asset_class_path.asset_name) != "AnimSequence":
            continue
        sequence = unreal.load_asset(object_path(a))
        if sequence is None:
            continue
        if is_additive(sequence):
            skipped.append(str(a.asset_name))
            continue
        clips[str(a.asset_name)] = object_path(a)
    return meshes, clips, skipped


def pick_mesh(meshes, hero):
    # The hero's own mesh is named after it; skins and parts come with suffixes.
    def score(a):
        name = str(a.asset_name).lower()
        return (name != hero.lower(), "skin" in name or "_" in name, len(name))
    return object_path(sorted(meshes, key=score)[0]) if meshes else None


def first(clips, *patterns):
    """The first clip whose name matches, trying the patterns in order."""
    for pattern in patterns:
        for name in sorted(clips):
            if re.fullmatch(pattern, name, re.IGNORECASE):
                return clips[name]
    return None


def every(clips, pattern, limit=4):
    return [clips[n] for n in sorted(clips) if re.fullmatch(pattern, n, re.IGNORECASE)][:limit]


def build_set(clips):
    """What each role plays, guessed from Paragon's names for things."""
    s = {}

    def put(key, value):
        if value:
            s[key] = value

    put("idle", first(clips, r"idle", r"Idle_Combat", r"Idle.*"))
    put("walk", first(clips, r"Jog_Fwd", r"Walk_Fwd", r".*Jog_Fwd"))
    put("run", first(clips, r"Sprint_Fwd", r"Run_Fwd", r"Jog_Fwd"))
    put("attack", every(clips, r"(Primary_Attack|Primary_Fire|Attack)_.*(?<!Montage)") or None)
    put("cast", first(clips, r"Cast", r"E_Ability", r"Q_Ability", r"Ability_E.*", r"Ability_Q.*"))
    put("death", every(clips, r"Death_.*") or None)
    put("rise", first(clips, r"Respawn", r"LevelStart"))
    put("deathFront", first(clips, r"Death_Bwd", r"Death_Back.*"))
    put("deathBack", first(clips, r"Death_Fwd", r"Death_Front.*"))
    put("deathLeft", first(clips, r"Death_Right"))
    put("deathRight", first(clips, r"Death_Left"))
    put("stunned", first(clips, r"Stunned_Loop", r"Stun.*Loop"))
    put("victory", every(clips, r"Emote_.*", 3) or None)

    motions = {}

    def motion(name, release=None, intro=None, windup=None, cast_release=None):
        clips_for = {}
        if release:
            clips_for["release"] = release if isinstance(release, list) else [release]
        if intro:
            clips_for["intro"] = intro
        if windup:
            clips_for["windup"] = windup
        if cast_release:
            clips_for["castRelease"] = cast_release
        if clips_for.get("release") or clips_for.get("windup"):
            motions[name] = clips_for

    motion("melee", every(clips, r"Primary_Attack_.*(?<!Montage)", 3))
    motion("heavy", first(clips, r"R_Ability.*Fire", r"R_Ability", r"Ability_R.*", r"Ultimate.*"))
    motion("shoot", every(clips, r"Primary_Fire_.*(?<!Montage)(?<!MSA)", 3),
           intro=first(clips, r"RMB_Drawback", r"RMB_Start", r"Charge_Start"),
           windup=first(clips, r"RMB_Loop", r"Charge_Loop"),
           cast_release=first(clips, r"RMB_Fire", r"Charge_Fire"))
    motion("bolt", first(clips, r"Q_Ability", r"Ability_Q.*"))
    motion("area", first(clips, r"E_Ability", r"R_Ability.*Fire", r"Ability_E.*"))
    motion("heal", first(clips, r"Cast", r"E_Ability"))
    if motions:
        s["motions"] = motions
    # A guess at the pace of the clips: Paragon heroes jog about this fast.
    # Feet that slide mean the numbers want changing.
    s["walkSpeed"] = 360
    s["runSpeed"] = 600
    return s


def main(args):
    write = "--write" in args
    args = [a for a in args if a != "--write"]
    if len(args) < 2:
        say("usage: add_hero.py <hero folder> <body name> [look ...] [--write]")
        return 1
    folder, body, looks = args[0].rstrip("/"), args[1], args[2:]
    hero = folder.split("/")[-1]
    meshes, clips, skipped = find(folder)
    mesh = pick_mesh(meshes, hero)
    if not mesh:
        say("no skeletal mesh under " + folder + ": is the hero installed from Fab?")
        return 1
    anim_set = build_set(clips)
    say("%s: mesh %s, %d clips that can play alone, %d additive left out" % (hero, mesh, len(clips), len(skipped)))
    # A line a role, since the log keeps only the first line of a message.
    for role, clips_for in anim_set.items():
        say("  %s: %s" % (role, json.dumps(clips_for)))

    with open(MAP_FILE, encoding="utf-8") as f:
        the_map = json.load(f)
    the_map.setdefault("animations", {})[body] = anim_set
    # Paragon meshes face along +Y, a quarter turn from the board's +X.
    the_map.setdefault("bodies", {})[body] = {"mesh": mesh, "yaw": -90, "animations": body}
    for look in looks:
        the_map.setdefault("looks", {})[look] = body
    if write:
        with open(MAP_FILE, "w", encoding="utf-8") as f:
            json.dump(the_map, f, indent=2)
            f.write("\n")
        say("written to " + MAP_FILE + (" for " + ", ".join(looks) if looks else ""))
    else:
        say("not written: add --write to write it")
    return 0


# The commandlet passes what follows the script's name as sys.argv.
result = main(sys.argv[1:])
say("DONE" if result == 0 else "FAILED")
