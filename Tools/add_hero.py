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
    --replace      write a new animation set even if the body has one (a set
                   already in the map is kept otherwise: it may have been
                   corrected by hand after watching the film)

Or bring in every Paragon hero in the project at once, each named after
itself (Greystone -> greystone, TheFey -> fey):

    -script="Tools/add_hero.py --all [--write]"

Every skin the hero ships (Skins/<skin>/Meshes) becomes a body of its own,
"<body>_<skin>", on the hero's clips. Then Tools/assign_bodies.py hands the
classes their heroes and skins from the plan in the map.

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
        # Read off the asset registry rather than loading every clip: fifteen
        # heroes' worth of loaded clips ran the machine out of memory.
        additive = str(a.get_tag_value("AdditiveAnimType") or "")
        if additive and additive != "AAT_None":
            skipped.append(str(a.asset_name))
            continue
        if not additive and is_additive(unreal.load_asset(object_path(a))):
            skipped.append(str(a.asset_name))
            continue
        clips[str(a.asset_name)] = object_path(a)
    return meshes, clips, skipped


def pick_mesh(meshes, hero):
    # The hero's own mesh is named after it; skins and parts come with suffixes.
    def score(a):
        name = str(a.asset_name).lower()
        return (name != hero.lower(), "skin" in name or "_" in name, len(name))
    meshes = [a for a in meshes if "/Skins/" not in str(a.package_name)] or meshes
    return object_path(sorted(meshes, key=score)[0]) if meshes else None


def skeleton_of(path, asset_data=None):
    # The registry says which skeleton a mesh is on; load it only if it doesn't.
    if asset_data is not None:
        tag = str(asset_data.get_tag_value("Skeleton") or "")
        if tag and tag != "None":
            return tag.split("'")[1] if "'" in tag else tag
    mesh = unreal.load_asset(path)
    return mesh.get_editor_property("skeleton").get_path_name() if mesh and mesh.get_editor_property("skeleton") else None


def find_skins(meshes, main_mesh):
    """Each skin's mesh, by skin name -- only those on the hero's own skeleton,
    since only they can play its clips."""
    main_data = next((a for a in meshes if object_path(a) == main_mesh), None)
    skeleton = skeleton_of(main_mesh, main_data)
    by_skin = {}
    for a in meshes:
        parts = str(a.package_name).split("/")
        # Skins/<skin>/Meshes, or in later packs Skins/<tier>/<skin>/Meshes:
        # the skin is named by the folder that holds its Meshes.
        if "Skins" not in parts or "Meshes" not in parts or parts.index("Meshes") - 1 <= parts.index("Skins"):
            continue
        by_skin.setdefault(parts[parts.index("Meshes") - 1], []).append(a)
    skins = {}
    for skin, found in sorted(by_skin.items()):
        path = object_path(sorted(found, key=lambda a: len(str(a.asset_name)))[0])
        chosen = next(a for a in found if object_path(a) == path)
        if skeleton is not None and skeleton_of(path, chosen) == skeleton:
            skins[skin] = path
        else:
            say("  skin %s left out: not on the hero's skeleton" % skin)
    return skins


def body_name(hero):
    name = hero.lower()
    return name[3:] if name.startswith("the") and len(name) > 3 else name


def installed_heroes():
    """Every Paragon hero folder in the project: /Game/Paragon*/Characters/Heroes/<Hero>."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(["/Game"], True)
    heroes = []
    for pack in sorted(registry.get_sub_paths("/Game", False)):
        if not str(pack).split("/")[-1].startswith("Paragon"):
            continue
        for folder in sorted(registry.get_sub_paths(str(pack) + "/Characters/Heroes", False)):
            # A hero has its own meshes and clips; a pack also keeps sounds and
            # parts beside its heroes (ParagonCountess/.../Countess_Sounds).
            subs = [str(p).split("/")[-1] for p in registry.get_sub_paths(str(folder), False)]
            if "Meshes" in subs and "Animations" in subs:
                heroes.append(str(folder))
    return heroes


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
    put("hitFront", first(clips, r"HitReact_Front", r"Hit_Front.*"))
    put("hitBack", first(clips, r"HitReact_Back", r"Hit_Back.*"))
    put("hitLeft", first(clips, r"HitReact_Left", r"Hit_Left.*"))
    put("hitRight", first(clips, r"HitReact_Right", r"Hit_Right.*"))
    # An emote's own clip, not the intro, loop and outro it is sometimes cut into.
    put("victory", every(clips, r"Emote_(?!.*_(Intro|Loop|Outro)$).*", 3) or None)

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


HERO_CLIPS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hero_clips.json")


def corrected(anim_set, body, clips):
    """The guesses with Tools/hero_clips.json's choices for this hero laid over them."""
    try:
        with open(HERO_CLIPS, encoding="utf-8") as f:
            fixes = json.load(f).get(body, {})
    except (OSError, ValueError):
        fixes = {}
    by_name = {name.lower(): path for name, path in clips.items()}

    def path_of(value):
        if isinstance(value, list):
            return [p for p in (path_of(v) for v in value) if p]
        found = by_name.get(str(value).lower())
        if not found:
            say("  hero_clips.json names %s for %s, which is not one of its clips that can play alone" % (value, body))
        return found

    # An ultimate's own clip for each motion ("<motion>_ult", which the game
    # plays for slot 4): the hero's big blow, or for a healer its big blessing.
    for key, motions in (("ult", ["melee", "heavy", "dash", "area", "bolt", "shoot", "channel"]),
                         ("ult_support", ["heal", "buff", "revive"])):
        found = path_of(fixes[key]) if key in fixes else None
        if found:
            for motion in motions:
                anim_set.setdefault("motions", {})[motion + "_ult"] = {"release": [found]}
    for role, value in fixes.items():
        if role in ("ult", "ult_support"):
            continue
        if role == "motions":
            motions = anim_set.setdefault("motions", {})
            for motion, parts in value.items():
                chosen = {part: path_of(v) for part, v in parts.items()}
                chosen = {part: v for part, v in chosen.items() if v}
                if chosen.get("release") or chosen.get("windup"):
                    motions[motion] = chosen
        else:
            # A clip that can't play alone is left out, and the guess stands.
            found = path_of(value)
            if found:
                anim_set[role] = found
    if fixes:
        say("  %d roles set from hero_clips.json" % len(fixes))
    return anim_set


def add_one(the_map, folder, body, looks, replace):
    hero = folder.split("/")[-1]
    meshes, clips, skipped = find(folder)
    mesh = pick_mesh(meshes, hero)
    if not mesh:
        say("no skeletal mesh under " + folder + ": is the hero installed from Fab?")
        return False
    say("%s: mesh %s, %d clips that can play alone, %d additive left out" % (hero, mesh, len(clips), len(skipped)))
    sets = the_map.setdefault("animations", {})
    if body in sets and not replace:
        say("  keeping the animation set already in the map for %s (--replace writes a new one)" % body)
    else:
        anim_set = corrected(build_set(clips), body, clips)
        # A line a role, since the log keeps only the first line of a message.
        for role, clips_for in anim_set.items():
            say("  %s: %s" % (role, json.dumps(clips_for)))
        sets[body] = anim_set
    # Paragon meshes face along +Y, a quarter turn from the board's +X.
    bodies = the_map.setdefault("bodies", {})
    bodies[body] = {"mesh": mesh, "yaw": -90, "animations": body}
    for skin, path in find_skins(meshes, mesh).items():
        bodies[body + "_" + skin.lower()] = {"mesh": path, "yaw": -90, "animations": body}
        say("  skin %s: %s" % (skin, path))
    for look in looks:
        the_map.setdefault("looks", {})[look] = body
    return True


def main(args):
    write = "--write" in args
    replace = "--replace" in args
    every_hero = "--all" in args
    args = [a for a in args if a not in ("--write", "--replace", "--all")]
    if not every_hero and len(args) < 2:
        say("usage: add_hero.py <hero folder> <body name> [look ...] [--write] [--replace], or add_hero.py --all [--write]")
        return 1

    with open(MAP_FILE, encoding="utf-8") as f:
        the_map = json.load(f)
    if every_hero:
        folders = installed_heroes()
        say("%d Paragon heroes in the project: %s" % (len(folders), ", ".join(f.split("/")[-1] for f in folders)))
        for folder in folders:
            add_one(the_map, folder, body_name(folder.split("/")[-1]), [], replace)
            # Every clip was loaded to see whether it can play alone; let a
            # hero's go before the next one's come in, or fifteen heroes do not fit.
            unreal.SystemLibrary.collect_garbage()
    elif not add_one(the_map, args[0].rstrip("/"), args[1], args[2:], replace):
        return 1

    # The classes' bodies, from the plan in the map, now that there are more to choose from.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import assign_bodies
    for line in assign_bodies.assign(the_map):
        say(line)

    if write:
        with open(MAP_FILE, "w", encoding="utf-8") as f:
            json.dump(the_map, f, indent=2)
            f.write("\n")
        say("written to " + MAP_FILE)
    else:
        say("not written: add --write to write it")
    return 0


# The commandlet passes what follows the script's name as sys.argv.
result = main(sys.argv[1:])
say("DONE" if result == 0 else "FAILED")
