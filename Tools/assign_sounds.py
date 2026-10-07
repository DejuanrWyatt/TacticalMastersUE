"""
Writes Content/Data/Sounds/sounds.json: what every ability, body and moment of a
battle sounds like. Read by the director (TMBattleDirectorSound.cpp); never by
the rules -- a battle is the same heard or silent.

  sfx        the sound effects, by name; each name lists takes of one sound,
             one picked at random each time, from the Fab sound packs in
             SFX_PACKS below
  motions    each motion's sound as it goes off and where it lands
  abilities  a class file ability's own, where its damage type says more than
             its motion: a fire bolt casts and lands as fire, a frost one as ice
  events     casting, a unit falling, a turn coming or lost, a battle won or
             lost, footsteps (step_<theme> for a battlefield's own ground), a click
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
    "melee": ["swing", "hit_metal"], "heavy": ["swing_heavy", "hit_heavy"], "dash": ["swing_heavy", "hit_heavy"],
    "shoot": ["shoot", "hit_arrow"], "bolt": ["cast_bolt", "magic"], "area": ["cast_area", "boom"], "channel": ["beam", "magic"],
    "heal": ["", "heal"], "buff": ["", "buff"], "revive": ["", "revive"],
}
# A spell's cast and landing by its damage type ("cast_fire", "land_fire"...).
# Weapons keep their clash of steel. "magic" is plain arcane: the motion's own.
ELEMENTS = {"fire", "ice", "water", "earth", "nature", "holy", "shadow", "lightning", "wind"}
SPELLS = {"bolt", "area", "channel"}
EVENTS = {"cast": "charge", "knockout": "knock_out", "ready": "ready", "turnLost": "turn_lost", "clockWarning": "clock_warning",
          "victory": "victory", "defeat": "defeat", "step": "step", "click": "click", "select": "select", "battleStart": "horn",
          "step_meadow": "step_meadow", "step_winter": "step_winter", "step_volcanic": "step_volcanic",
          "step_ruined_keep": "step_ruined_keep"}
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


# Where each named sound comes from: the sound packs installed from Fab
# (2026-09-29), by the names of their files, under Content. Each is a list of
# file-name patterns; every file matching is a take of the sound. Sound cues
# that only wrap a wave (*_Cue) and stereo copies are left out: the waves are
# the same sounds, and a sound placed in the world wants to be mono.
#   FreeModularMagicSFX   Free Modular Magic SFX (the pack's rendered waves)
#   General_Whoosh_SK     General Whoosh SFX
#   Free_Sounds_Pack      50 Free Game Sounds Pack
#   SmallSoundKit         Small Sound Kit
#   Essential_Foosteps_SK Essential Footsteps SFX
#   RealisticSwordSoundEffects  Free Realistic Sword Sound Effects Pack
#   VikingWarHorns        Viking War Horn
MAGIC = "FreeModularMagicSFX/**/"
SFX_PACKS = {
    # Weapons.
    "swing": ["RealisticSwordSoundEffects/**/Swoosh_*", "General_Whoosh_SK/**/Swooshes_Swish_Sword_Swing_Heavy_*",
              "General_Whoosh_SK/**/GW_Whoosh_Combo_Medium_*"],
    "swing_heavy": ["General_Whoosh_SK/**/GW_Whoosh_Combo_Heavy_*", MAGIC + "SW_MagicWeapon_Swing_*"],
    # Steel on steel: the sword pack's blade strikes, and a blow taken on a shield.
    "hit_metal": ["RealisticSwordSoundEffects/**/Sword_Attack_*", "Free_Sounds_Pack/**/Shield_Metal_Impact_*"],
    "hit_heavy": ["Free_Sounds_Pack/**/Punch_*", "Free_Sounds_Pack/**/Rock_Impact_*", "Free_Sounds_Pack/**/Hit_Generic_*"],
    "shoot": [MAGIC + "SW_MagicCast_Arrow_*", MAGIC + "SW_MagicCast_HeavyArrowShoot_*"],
    "hit_arrow": ["Free_Sounds_Pack/**/Hit_Generic_*", "Free_Sounds_Pack/**/Stab_*"],
    # Spells.
    "cast_bolt": [MAGIC + "SW_MagicCast_Projectile1_*", MAGIC + "SW_MagicCast_Projectile2_*", MAGIC + "SW_MagicCast_Generic1_*"],
    "magic": [MAGIC + "SW_MagicImpact_Crystal_*", MAGIC + "SW_MagicImpact_Barrier_*"],
    "cast_area": [MAGIC + "SW_MagicCast_Multi1_*", MAGIC + "SW_MagicCast_Multi2_*", MAGIC + "SW_MagicCast_StaffStrikeGround_*"],
    "boom": ["Free_Sounds_Pack/**/Explosion_Medium_*", "Free_Sounds_Pack/**/Explosion_Large_*", MAGIC + "SW_MagicSpell_RockExplosion1_*"],
    "charge": [MAGIC + "SW_MagicChant_Charge1_*", MAGIC + "SW_MagicChant_Charge2_*", MAGIC + "SW_MagicRiser_Charge1_*"],
    "beam": [MAGIC + "SW_MagicBeam_Light1_*", MAGIC + "SW_MagicBeam_Light3_*"],
    "heal": [MAGIC + "SW_MagicCast_Healing_*"],
    "buff": [MAGIC + "SW_MagicCast_Buff1_*", MAGIC + "SW_MagicCast_Buff2_*", MAGIC + "SW_MagicCast_Buff3_*"],
    "revive": [MAGIC + "SW_MagicFade_Appear1_*", MAGIC + "SW_MagicRiser_FullyCharge1_*"],
    # A spell's cast and landing by its damage type.
    "cast_fire": [MAGIC + "SW_MagicCast_FireBall1_*", MAGIC + "SW_MagicCast_FireBall2_*"],
    "land_fire": [MAGIC + "SW_MagicImpact_Fire_*", "SmallSoundKit/**/Mgc_Fire_Impact_*"],
    "cast_ice": [MAGIC + "SW_MagicCast_IceCrystal1_*", MAGIC + "SW_MagicCast_IceCrystal2_*"],
    "land_ice": [MAGIC + "SW_MagicImpact_Crystal_*", "SmallSoundKit/**/Mgc_Ice_Arrow_Hit_*", "SmallSoundKit/**/Mgc_Glacier_Impact_*"],
    "cast_water": [MAGIC + "SW_MagicCast_Liquid_Water_*"],
    "land_water": [MAGIC + "SW_MagicImpact_Water_*", "SmallSoundKit/**/Mgc_Water_Impact_*"],
    "cast_lightning": [MAGIC + "SW_MagicCast_Lightning1_*", MAGIC + "SW_MagicCast_ThrowLightningSpear_*"],
    "land_lightning": [MAGIC + "SW_MagicImpact_LightningStrike_*", "SmallSoundKit/**/Mgc_Electric_Impact_*"],
    "cast_earth": [MAGIC + "SW_MagicCast_GroundStrike_*"],
    "land_earth": [MAGIC + "SW_MagicSpell_RockExplosion1_*", "Free_Sounds_Pack/**/Rock_Impact_*"],
    "cast_wind": [MAGIC + "SW_MagicChant_WindCharge_*"],
    "land_wind": [MAGIC + "SW_MagicSpell_FreezingWind_*"],
    "cast_nature": [MAGIC + "SW_MagicCast_ThrowToxin_*"],
    "land_nature": [MAGIC + "SW_MagicCast_Liquid_Gore_*"],
    "cast_shadow": [MAGIC + "SW_MagicFade_Shoot1_*", MAGIC + "SW_MagicFade_Shoot2_*"],
    "land_shadow": [MAGIC + "SW_MagicFade_Bass1_*"],
    "cast_holy": [MAGIC + "SW_MagicBeam_Light2_*"],
    "land_holy": [MAGIC + "SW_MagicFade_LightingAppear_*"],
    # Moments.
    "knock_out": ["Free_Sounds_Pack/**/Hit_Generic_5-*", "Free_Sounds_Pack/**/Rock_Large_Debris_*"],
    "ready": ["Free_Sounds_Pack/**/Magical_Interface_5-*"],
    "turn_lost": ["Free_Sounds_Pack/**/Interface_3-*"],
    # 2026-10-06: a beep each of the last three seconds of your unit's turn.
    "clock_warning": ["Free_Sounds_Pack/**/Sci-Fi_Interface_8-*"],
    "victory": ["Free_Sounds_Pack/**/Special_Collectible_26-*"],
    "defeat": [MAGIC + "SW_MagicFade_FadeOut1_*"],
    "click": ["Free_Sounds_Pack/**/Interface_1-*"],
    "select": ["Free_Sounds_Pack/**/Magical_Interface_8-*"],
    # A war horn as a battle begins.
    "horn": ["VikingWarHorns/**/SW_Intro_*"],
    # Footsteps, by the battlefield's look (step_<theme id>), else "step".
    "step": ["Essential_Foosteps_SK/**/Footstep_Dirt_Boots_Walk_[0-9]"],
    "step_meadow": ["Essential_Foosteps_SK/**/Footstep_FootstepLeaves_Boots_Walk_[0-9]"],
    "step_winter": ["Essential_Foosteps_SK/**/Footstep_Snow_Walk_[0-9]"],
    "step_volcanic": ["Essential_Foosteps_SK/**/Footstep_Gravel_Boots_Walk_[0-9]"],
    "step_ruined_keep": ["Essential_Foosteps_SK/**/Footstep_Concrete_Boots_Walk_[0-9]"],
}


def sfx():
    out = {}
    for name, patterns in SFX_PACKS.items():
        for pattern in patterns:
            for path in sorted(glob.glob(os.path.join(CONTENT, pattern + ".uasset"), recursive=True)):
                if path.endswith("_Cue.uasset") or "Stereo" in path:
                    continue
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
            element = element_of(data, slot, ability)
            if element in ELEMENTS:
                # A channel keeps its beam going; its landing is the element's.
                cast = MOTIONS[motion][0] if motion == "channel" else "cast_" + element
                out[ability["id"]] = [cast, "land_" + element]
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
