"""
Writes Content/Data/Sounds/sounds.json: what every ability, body and moment of a
battle sounds like. Read by the director (TMBattleDirectorSound.cpp); never by
the rules -- a battle is the same heard or silent.

  sfx        the sound effects, by name; each name lists takes of one sound,
             one picked at random each time. Filled from the sound packs in
             SFX_PACKS below -- none yet: the Godot game's sounds were removed
             (2026-09-29), and the game's own come from Fab
  motions    each motion's sound as it goes off and where it lands -- the Godot
             game's table (scripts/battle/fx.gd:23-33) by motion
  abilities  a class file ability's own, where its damage type says more than
             its motion: a fire bolt lands with a boom, a frost one with ice
  events     casting, a unit falling, a turn coming or lost, a battle won or
             lost, footsteps, a click
  voices     each hero's voice by what it is doing, from its pack's Audio/Cues:
             the effort of each ability, pain, a death cry, a cheer

Plain Python, no editor:  python Tools/assign_sounds.py
"""

import glob
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CONTENT = os.path.join(ROOT, "Content")
OUT = os.path.join(CONTENT, "Data", "Sounds", "sounds.json")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from assign_vfx import element_of, motion_of  # noqa: E402  the same reading of an ability

# fx.gd:23-33, by motion: [as it goes off, where it lands]; "" is silence.
MOTIONS = {
    "melee": ["swing", "hit_metal"], "heavy": ["swing", "hit_heavy"], "dash": ["swing", "hit_heavy"],
    "shoot": ["shoot", "hit_arrow"], "bolt": ["", "magic"], "area": ["", "boom"], "channel": ["charge", "magic"],
    "heal": ["", "heal"], "buff": ["", "buff"], "revive": ["", "revive"],
}
# A spell's landing by its damage type. Weapons keep their clash of steel.
ELEMENT_LANDS = {"fire": "boom", "ice": "ice", "water": "ice", "earth": "hit_stone", "nature": "magic",
                 "holy": "magic", "shadow": "magic", "lightning": "magic", "wind": "magic", "magic": "magic"}
SPELLS = {"bolt", "area", "channel"}
EVENTS = {"cast": "charge", "knockout": "knock_out", "ready": "ready", "turnLost": "turn_lost",
          "victory": "victory", "defeat": "defeat", "step": "step", "click": "click", "select": "select"}
# A hero's voice lines by role, from its cues' names (Greystone_Effort_Pain...),
# best first.
VOICE_ROLES = {
    "primary": [r"Effort_Ability_Primary$", r"Effort_Attack$", r"Effort_Swing$"],
    "q": [r"Effort_Ability_Q$", r"Effort_Ability_E$", r"Effort_Attack$"],
    "e": [r"Effort_Ability_RMB$", r"Effort_Ability_E$", r"Effort_Ability_Q$"],
    "ultimate": [r"Ability_Ultimate_Self$", r"Effort_Ability_Ultimate.*", r"Effort_Ability_R$"],
    "pain": [r"Effort_Pain$"],
    "painHeavy": [r"Effort_PainHeavy$", r"Effort_Pain$"],
    "death": [r"Effort_Death$", r"_Death$"],
    "cheer": [r"Effort_Cheer$", r"Effort_Laugh$", r"Kill_Enemy$"],
}


# Where each named sound comes from: a sound pack installed from Fab, by the
# names of its files. Empty until one is installed, and the battle's effects
# are silent (the heroes' voices still speak).
SFX_PACKS = {}


def sfx():
    out = {}
    for name, patterns in SFX_PACKS.items():
        for pattern in patterns:
            for path in sorted(glob.glob(os.path.join(CONTENT, pattern + ".uasset"))):
                rel = os.path.relpath(path, CONTENT).replace("\\", "/")[:-len(".uasset")]
                out.setdefault(name, []).append("/Game/%s.%s" % (rel, os.path.basename(rel)))
    return out


def abilities():
    out = {}
    for path in sorted(glob.glob(os.path.join(CONTENT, "Data", "Classes", "*.tmclass.json"))):
        data = json.load(open(path, encoding="utf-8"))
        for slot, ability in enumerate(data.get("abilities", [])):
            motion = motion_of(ability, slot)
            if motion not in SPELLS:
                continue
            land = ELEMENT_LANDS.get(element_of(data, slot, ability) or "")
            if land and land != MOTIONS[motion][1]:
                out[ability["id"]] = [MOTIONS[motion][0], land]
    return out


def voices(the_map):
    out = {}
    for hero, sets in sorted(the_map.get("animations", {}).items()):
        body = the_map.get("bodies", {}).get(hero)
        if not body or not body.get("mesh", "").startswith("/Game/Paragon"):
            continue
        pack = body["mesh"].split("/")[2]
        cues = glob.glob(os.path.join(CONTENT, pack, "Audio", "Cues", "*.uasset"))
        names = {os.path.splitext(os.path.basename(c))[0]: c for c in cues}
        chosen = {}
        for role, patterns in VOICE_ROLES.items():
            for pattern in patterns:
                hit = next((n for n in sorted(names) if re.search(pattern, n)), None)
                if hit:
                    rel = os.path.relpath(names[hit], CONTENT).replace("\\", "/")[:-len(".uasset")]
                    chosen[role] = "/Game/%s.%s" % (rel, hit)
                    break
        if chosen:
            out[hero] = chosen
    return out


def main():
    the_map = json.load(open(os.path.join(CONTENT, "Data", "CharacterMap", "characters.json"), encoding="utf-8"))
    effects = sfx()
    missing = sorted(({n for pair in MOTIONS.values() for n in pair if n} | set(EVENTS.values())) - set(effects))
    if missing:
        print("no sound yet for: " + ", ".join(missing))
    sounds = {
        "format": "tactical-masters-sounds",
        "version": 1,
        "about": "What a battle sounds like; written by Tools/assign_sounds.py and read by the director, never the rules.",
        "sfx": effects,
        "motions": MOTIONS,
        "abilities": abilities(),
        "events": EVENTS,
        "voices": voices(the_map),
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(sounds, f, indent=2)
        f.write("\n")
    print("%d sounds, %d abilities with their own landing, %d heroes with voices" % (
        len(effects), len(sounds["abilities"]), len(sounds["voices"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
