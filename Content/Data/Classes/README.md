# Class data

The 81 classes the game reads at startup, copied from the Godot project. These
are Astra library exports: each file holds one class as a `profile` ability
carrying its stats, plus four abilities tagged `slot:1` to `slot:4`.

These files are **a copy, not a link**. The Godot project at
`D:\ProgramsByMe\TacticalMasters\data\classes\` is where they are authored; this
directory is what Unreal ships. To refresh after changing a class there:

    cp D:\ProgramsByMe\TacticalMasters\data\classes\*.astra.json .

Nothing checks that the two are in step, so a class changed in one and not the
other will quietly differ. Worth a test once the importer is ported: read both
directories and compare.

## What is not here

The six built-in jobs — Squire, Knight, Archer, Monk, Black Mage and White
Mage — are not data. They are written out in `scripts/core/jobs.gd` in the Godot
project, along with every built-in ability, and will be ported as C++ with the
rest of the rules. 81 files here plus those 6 is the 87 classes the game offers.

## Reading them

The format is Astra's, not the game's. `astra_import.gd` in the Godot project
turns a profile into stats and four abilities, and that is the piece to port
next to this data. Two details in it are easy to miss:

- A profile may still declare `power`. The Power stat was removed; what it was
  worth is folded into each of the class's own abilities as they are read, so
  the numbers come out the same as before.
- `buff_power` on an ability becomes a Crit buff at twice the number, since
  Crit multiplies the ability's own damage rather than adding to it.

## Packaging

Raw files under `Content/` are not cooked into a packaged build by default. Add
`Data/Classes` to **Project Settings → Packaging → Additional Non-Asset
Directories to Package** before shipping, or the game will start with six
classes and no explanation.
