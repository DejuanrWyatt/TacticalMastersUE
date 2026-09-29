"""
Gives every ability in the class files a particle effect from its class's own
hero: the effects each Paragon hero ships for its abilities.

A class wears a hero (Content/Data/CharacterMap/characters.json, "classes"),
and that hero's pack has an effect for each of its own abilities, filed by
ability (FX/.../Abilities/<Ability>/) and by skin (FX/.../Skins/<Skin>/). This
scores the hero's effects against what an ability does -- a strike wants an
impact on the targets, an area spell an explosion at the spot, a heal a heal,
an ultimate the hero's ultimate -- and writes the best into the ability's "vfx"
field. A class in a skin prefers that skin's effects. No two abilities of a
class get the same effect.

Only the look of a battle changes: the rules read "vfx" and ignore it
(Content/Data/Classes/README.md). An ability that already names an effect is
left alone unless --replace is given.

Plain Python, no editor:

    python Tools/assign_vfx.py              say what it would choose
    python Tools/assign_vfx.py --write      write the class files
    python Tools/assign_vfx.py --list FILE  also write the chosen effects, one a line,
                                            for Tools\\VfxCatalog.bat to film just those
    python Tools/assign_vfx.py --only heal,revive --avoid --write
                                            only abilities with those motions
"""

import glob
import json
import os
import re
import sys
import zlib

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CONTENT = os.path.join(ROOT, "Content")
CLASSES = os.path.join(CONTENT, "Data", "Classes")
MAP_FILE = os.path.join(CONTENT, "Data", "CharacterMap", "characters.json")

# Effects that are not an ability going off: cameras, trails behind a weapon,
# aiming and warm-up loops, menu poses, emotes, water variants.
SKIP = re.compile(r"camera|screen|trail|recall|levelstart|level_start|jumppad|jump_|frontend|front_end|emote|sculpture|"
                  r"travel|targeting|aimloop|aim_|warmup|warm_up|water|lilypad|decal|mesh_attach|cheap|test|temp|_local|"
                  r"idle|dissolve|respawn|spawn_draw|pose|statue|weighting|minion|placeholder|debug|_old|nohit|miss|"
                  r"teleport|begin|_start$|flash_", re.I)

# For an ability its hero has nothing fitting for: the general effects packs.
FALLBACK = {
    "heal": ("/Game/FreeParticle_SoftTofu/Niagara/NS_Sparkling_Heart.NS_Sparkling_Heart", "targets"),
    "buff": ("/Game/FreeParticle_SoftTofu/Niagara/NS_Sparkling_Star.NS_Sparkling_Star", "targets"),
    "channel": ("/Game/ParticleSystemVFXVol1/Systems/NS_EnergyVortex.NS_EnergyVortex", "user"),
    # Paragon's general hits (the minions' pack): the plain impact burst was too faint to see.
    "strike": ("/Game/ParagonMinions/FX/Particles/Minions/Shared/P_Minion_Melee_Impact.P_Minion_Melee_Impact", "targets"),
    "shot": ("/Game/ParagonMinions/FX/Particles/Minions/Shared/P_Minion_Melee_Impact.P_Minion_Melee_Impact", "targets"),
    "area": ("/Game/ParagonMinions/FX/Particles/Buffs/Buff_Red/FX/P_Buff_Red_BigSmash_Impact.P_Buff_Red_BigSmash_Impact", "point"),
}
# A heal with nothing fitting from its hero: one of the brightest green and
# teal effects in the installed packs (picked from the effects catalogue,
# 2026-09-29), by its damage type, else spread over them by the ability's id,
# so the 48 heals don't all look alike. Paragon's own heal effects (Narbash's
# regen, Zinx's heal shot) film nearly invisible away from their hero.
HEALS = {
    "burst": "/Game/ParagonKhaimera/FX/ParticleSystems/Abilities/WarriorSustain/FX/P_Passive_Activate.P_Passive_Activate",
    "twinkle": "/Game/FreeParticle_SoftTofu/Niagara/NS_Sparkling_Animate_2.NS_Sparkling_Animate_2",
    "motes": "/Game/ParagonSevarog/FX/Particles/Abilities/SoulStackPassive/FX/P_SoulStageEmbersBurst.P_SoulStageEmbersBurst",
    "teal": "/Game/FreeParticle_SoftTofu/Niagara/NS_Sparkling_Glow.NS_Sparkling_Glow",
}
HEAL_BY_ELEMENT = {"holy": "twinkle", "light": "twinkle", "nature": "motes", "earth": "motes", "water": "teal", "ice": "teal"}
# A revive: a golden sunburst.
REVIVE = ("/Game/ParagonSunWukong/FX/Particles/Wukong/Skins/Future/FX/P_Wukong_Future_Toggle_StaffSwirls.P_Wukong_Future_Toggle_StaffSwirls", "targets")
# General effects an earlier run gave out that a later one may replace
# without --replace: they were placeholders, not choices.
SUPERSEDED = {"/Game/FreeParticle_SoftTofu/Niagara/NS_Sparkling_Heart.NS_Sparkling_Heart",
              "/Game/ParticleSystemVFXVol1/Systems/NS_SparkleBurst.NS_SparkleBurst"}


def heal_effect(data, slot, ability):
    element = element_of(data, slot, ability) or ""
    kind = HEAL_BY_ELEMENT.get(element) or sorted(HEALS)[zlib.crc32(ability.get("id", "").encode()) % len(HEALS)]
    return HEALS[kind]
# A damaging ability with nothing fitting from its hero: the general pack's
# effect for its damage type, so a fire bolt burns and a frost blow freezes.
BY_ELEMENT = {
    "fire": "/Game/TorchFire/FX/NS_TorchFire_Wild.NS_TorchFire_Wild",
    "fire_hit": "/Game/ParagonMinions/FX/Particles/Minions/Dragon/FX/P_Dragon_FireBall_CharacterImpact.P_Dragon_FireBall_CharacterImpact",
    "ice": "/Game/TorchFire/FX/NS_TorchFire_Ice.NS_TorchFire_Ice",
    "water": "/Game/TorchFire/FX/NS_TorchFire_Ice.NS_TorchFire_Ice",
    "holy": "/Game/ParticleSystemVFXVol1/Systems/NS_SparkleBurst.NS_SparkleBurst",
    "shadow": "/Game/PotaVFX_Smoke/VFX/System/SmokeBurst/NS_MagicSmokeBurstAir.NS_MagicSmokeBurstAir",
    "earth": "/Game/PotaVFX_Smoke/VFX/System/SmokeBurst/NS_MagicSmokeBurstAir.NS_MagicSmokeBurstAir",
    "nature": "/Game/PotaVFX_Smoke/VFX/System/SmokeBurst/NS_MagicSmokeBurstAir.NS_MagicSmokeBurstAir",
    "lightning": "/Game/TorchFire/FX/NS_TorchFire_Magic.NS_TorchFire_Magic",
    "wind": "/Game/PotaVFX_Smoke/VFX/System/SmokeRing/NS_SmokeRing.NS_SmokeRing",
    "magic": "/Game/ParticleSystemVFXVol1/Systems/NS_EnergyVortex.NS_EnergyVortex",
}
FX_ELEMENT = {"fire": "fire", "meteor": "fire", "blizzard": "ice", "holy_blade": "holy", "sanctuary": "holy",
              "earth_slash": "earth", "cure": "holy", "raise": "holy"}


def element_of(data, slot, ability):
    """The damage type the class creator gave it, else a guess from its borrowed effect, else magic or none."""
    creator = data.get("creator", {})
    plan = creator.get("plan", [])
    if slot < len(plan) and isinstance(plan[slot], dict) and plan[slot].get("damageType"):
        return plan[slot]["damageType"].lower()
    if creator.get("damageTypes"):
        return creator["damageTypes"][0].lower()
    if ability.get("fx") in FX_ELEMENT:
        return FX_ELEMENT[ability["fx"]]
    return "magic" if ability.get("scale") == "mag" else None

# What an ability's motion wants, by words in the effect's name or folder.
WANTS = {
    "strike": {"impact": 5, "hitcharacter": 5, "hitplayer": 5, "hit": 3, "slash": 3, "smash": 3, "sparks": 2, "strike": 3},
    "shot": {"hitcharacter": 5, "hitplayer": 5, "impact": 4, "explode": 4, "explosion": 4, "hit": 3, "boom": 3},
    "area": {"aoe": 5, "explosion": 5, "explode": 5, "blast": 5, "boom": 4, "groundsmash": 5, "seismic": 5,
             "shockwave": 5, "ground": 3, "meteor": 4, "shower": 4, "impact": 3, "nova": 4},
    "heal": {"heal": 6, "regen": 6, "resurrect": 6, "revive": 6, "life": 4, "restore": 4, "applied": 2, "glow": 1},
    "buff": {"buff": 5, "shield": 5, "applied": 4, "aura": 4, "empower": 4, "movespeed": 3, "speed": 3, "haste": 4, "glow": 2},
    "channel": {"beam": 5, "channel": 5, "looping": 4, "loop": 3, "tether": 3, "drain": 4},
}
MOTION_WANTS = {"melee": "strike", "heavy": "strike", "dash": "strike", "shoot": "shot", "bolt": "shot",
                "area": "area", "heal": "heal", "revive": "heal", "buff": "buff", "channel": "channel"}


def shape_of(a):
    if a.get("shape"):
        return a["shape"]
    if float(a.get("max_range", 0)) == 0.0:
        return "self"
    return "circle" if float(a.get("aoe", 0)) > 0 else "unit"


def motion_of(a, slot):
    """TMSim::MotionOf (SimTargeting.cpp:31-81), for a class file's ability."""
    if a.get("anim"):
        return a["anim"]
    kind = a.get("kind", "active")
    if kind in ("toggle", "passive", "aura"):
        return "none"
    if kind == "channeled":
        return "channel"
    shape = shape_of(a)
    effect = a.get("effect", "damage")
    if effect == "revive":
        return "revive"
    if effect == "heal":
        return "heal"
    if effect == "support":
        if shape == "self" or float(a.get("max_range", 0)) == 0.0:
            return "buff"
        return "bolt" if a.get("target", "enemy") == "enemy" else "heal"
    if shape == "vector":
        return "dash"
    if a.get("scale", "att") == "att":
        if float(a.get("max_range", 0)) <= 2.0:
            return "heavy" if slot == 3 or float(a.get("aoe", 0)) > 0 else "melee"
        return "shoot"
    if float(a.get("aoe", 0)) > 0 or shape in ("circle", "cone", "line", "global"):
        return "area"
    return "bolt"


def invisible():
    """Effects the last check filmed as nothing (Saved/VfxCheck/catalog.json):
    they want something the game never gives them, a beam's far end or a body
    to cling to. --avoid passes over them."""
    path = os.path.join(ROOT, "Saved", "VfxCheck", "catalog.json")
    if not os.path.exists(path):
        return set()
    catalog = json.load(open(path, encoding="utf-8"))
    items = next(v for v in catalog.values() if isinstance(v, list))
    try:
        from PIL import Image
    except ImportError:
        return {e["path"] for e in items if not e.get("visible")}
    # Judged from the film itself, leniently: a hit spark is small and gone in
    # a tenth of a second, and the studio's own flag misses many of them. Any
    # frame with any bright pixel counts.
    dark = set()
    for e in items:
        # The brightest colour, not the grey: a blue frost burst is dim in grey.
        strip = Image.open(os.path.join(ROOT, "Saved", "VfxCheck", e["sheet"])).convert("RGB")
        if max(high for low, high in strip.getextrema()) < 60:
            dark.add(e["path"])
    return dark


def other_skins(bodies, hero, wearing):
    """The hero's other skins by name, which an effect may carry outside a Skins folder (FengMao's Undertow)."""
    names = set()
    for body in bodies:
        if body.startswith(hero + "_"):
            skin = body[len(hero) + 1:].lower()
            skin = skin[len(hero) + 1:] if skin.startswith(hero + "_") else skin
            names.add(skin)
    names |= {"undertow", "bone", "novaborn", "northernmystic", "basholantern"}
    names.discard(wearing or "")
    return names


def hero_effects(pack_folder):
    """(object path, lower-case name and folders, skin or None) for every effect in a hero's pack."""
    found = []
    for path in glob.glob(os.path.join(CONTENT, pack_folder, "**", "*.uasset"), recursive=True):
        name = os.path.splitext(os.path.basename(path))[0]
        if not (name.startswith("P_") or name.startswith("NS_")):
            continue
        rel = os.path.relpath(path, CONTENT).replace("\\", "/")
        package = "/Game/" + rel[:-len(".uasset")]
        parts = rel.split("/")
        skin = parts[parts.index("Skins") + 1].lower() if "Skins" in parts and parts.index("Skins") + 1 < len(parts) - 1 else None
        words = rel.lower()
        if SKIP.search(name):
            continue
        found.append((package + "." + name, words, skin))
    return found


def score(words, want, slot, skin, wearing_skin):
    s = 0
    for word, weight in WANTS[want].items():
        if word in words:
            s = max(s, weight)
    if s == 0:
        return 0
    ultimate = "ultimate" in words or "/ult" in words or "_ult" in words
    if slot == 3:
        s += 6 if ultimate else 0
    elif ultimate:
        s -= 4
    if "primary" in words and slot == 0:
        s += 1
    # A strike or a shot lands on someone: the effect for hitting a character, not the ground.
    if want in ("strike", "shot") and "hitworld" in words:
        s -= 2
    if skin is not None:
        s += 3 if skin == wearing_skin else -100
    return s


def main(args):
    # Effect names are plain ASCII, but a class name need not be.
    sys.stdout.reconfigure(encoding="utf-8")
    write = "--write" in args
    replace = "--replace" in args
    listing = args[args.index("--list") + 1] if "--list" in args else None
    # --only heal,revive: touch only abilities with those motions.
    only = set(args[args.index("--only") + 1].split(",")) if "--only" in args else None
    the_map = json.load(open(MAP_FILE, encoding="utf-8"))
    bodies = the_map.get("bodies", {})
    chosen_all = set()
    changed = 0
    avoid = set()
    if "--avoid" in args:
        # Once passed over, always: a later check films only what was chosen, so
        # the list of the invisible grows by what each check finds.
        avoid_file = os.path.join(ROOT, "Saved", "vfx-invisible.txt")
        if os.path.exists(avoid_file):
            avoid = {line.strip() for line in open(avoid_file, encoding="utf-8") if line.strip()}
        avoid |= invisible()
        with open(avoid_file, "w", encoding="utf-8") as f:
            f.write("\n".join(sorted(avoid)) + "\n")
    for path in sorted(glob.glob(os.path.join(CLASSES, "*.tmclass.json"))):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        data = json.loads(text)
        body_name = the_map.get("classes", {}).get(data["id"])
        body = bodies.get(body_name or "")
        if not body or not body.get("mesh", "").startswith("/Game/Paragon"):
            print("%s: wears no hero, left alone" % data["id"])
            continue
        hero = body["animations"]
        pack = body["mesh"].split("/")[2]
        wearing_skin = body_name[len(hero) + 1:].lower() if body_name.startswith(hero + "_") else None
        # A skin's folder names it without the hero prefix the body name may repeat.
        if wearing_skin and wearing_skin.startswith(hero + "_"):
            wearing_skin = wearing_skin[len(hero) + 1:]
        skins_elsewhere = other_skins(bodies, hero, wearing_skin)
        effects = [(p, w, s) for p, w, s in hero_effects(pack)
                   if p not in avoid and not any(skin in os.path.basename(w) for skin in skins_elsewhere)]
        used = set()
        said = []
        for slot, ability in enumerate(data.get("abilities", [])):
            motion = motion_of(ability, slot)
            superseded = ability.get("vfx", {}).get("system") in SUPERSEDED and motion in ("heal", "revive")
            if ability.get("vfx") and not replace and ability["vfx"].get("system") not in avoid and not superseded:
                used.add(ability["vfx"].get("system", ""))
                continue
            want = MOTION_WANTS.get(motion)
            if not want or (only and motion not in only):
                continue
            best = max(((score(w, want, slot, s, wearing_skin), p) for p, w, s in effects if p not in used),
                       default=(0, None))
            at = "point" if motion == "area" else ("user" if motion == "channel" or (motion == "buff" and shape_of(ability) == "self") else "targets")
            if best[0] < 3:
                general = REVIVE if motion == "revive" else FALLBACK[want]
                if motion == "heal":
                    general = (heal_effect(data, slot, ability), general[1])
                element = element_of(data, slot, ability) if want in ("strike", "shot", "area") else None
                if element == "fire" and want in ("strike", "shot"):
                    element = "fire_hit"
                best = (0, BY_ELEMENT.get(element, general[0]))
                at = general[1] if motion != "buff" or shape_of(ability) != "self" else "user"
            ability["vfx"] = {"system": best[1], "at": at, "scale": 1}
            used.add(best[1])
            chosen_all.add(best[1])
            said.append("%d:%s %s" % (slot, motion, best[1].split(".")[-1]))
            changed += 1
        print("%s (%s): %s" % (data["id"], body_name, "; ".join(said)))
        if write:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
                f.write("\n")
    print("%d abilities given an effect, %d different effects" % (changed, len(chosen_all)))
    if listing:
        with open(listing, "w", encoding="utf-8") as f:
            f.write("\n".join(sorted(chosen_all)) + "\n")
    if not write:
        print("not written: add --write to write the class files")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
