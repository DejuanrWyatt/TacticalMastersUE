"""
Brings the Godot game's sound effects into the project as Unreal sound waves,
under /Game/Audio/SFX, with the names they have there (swing_1, hit_metal_2...).

They are Kenney's CC0 packs (RPG Audio, Impact Sounds, Interface Sounds, Music
Jingles; see the Godot project's assets/audio/CREDITS.txt), which the Godot game
plays for every ability: a sound as it goes off and one where it lands
(scripts/battle/fx.gd:23-33). The Godot project is only read.

Run it with the editor closed (Tools\\ImportSounds.bat), then
Tools/assign_sounds.py writes Content/Data/Sounds/sounds.json from them.
"""

import glob
import os

import unreal

GODOT_SFX = r"D:\ProgramsByMe\TacticalMasters\assets\audio\sfx"
DESTINATION = "/Game/Audio/SFX"


def main():
    files = sorted(glob.glob(os.path.join(GODOT_SFX, "*.ogg")))
    if not files:
        unreal.log_error("IMPORT SOUNDS: nothing at " + GODOT_SFX)
        return 1
    tasks = []
    for path in files:
        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = DESTINATION
        task.destination_name = os.path.splitext(os.path.basename(path))[0]
        task.replace_existing = True
        task.automated = True
        task.save = True
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    made = [t for t in tasks if t.imported_object_paths]
    unreal.log("IMPORT SOUNDS: %d of %d imported into %s" % (len(made), len(tasks), DESTINATION))
    return 0 if len(made) == len(tasks) else 1


result = main()
unreal.log("IMPORT SOUNDS: DONE" if result == 0 else "IMPORT SOUNDS: FAILED")
