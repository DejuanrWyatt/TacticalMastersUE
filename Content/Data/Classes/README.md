# Class data

The classes the game loads at startup, one `<id>.tmclass.json` file each, in
Tactical Masters' own class format. With the six built-in classes written in
code, that is the 87 the game offers.

## Where they come from

The **class creator** (`E:\TacticsClassCreator`) makes them and installs them
here. Its `FORMAT.md` describes the format. Every number in a file is the number
the rules use: ten stats, the roles, a look, and four abilities in the engine's
own terms. There are no formulas and no tags to interpret, and nothing is quietly
ignored. The rules' reader (`Source/TMSim/Private/SimClassFile.cpp`) refuses a
file with a key it doesn't know, rather than skipping it.

The 81 files here were converted once from the Godot project's Astra class files
by the creator's `tools/convert-astra.mjs`. The Godot project keeps its Astra
files, and they remain the reference the port is checked against. Nothing in
Unreal reads an Astra file, and nothing needs Astra to make a class.

## How they are checked

`Tests/SimClassTest.cpp` (in `scripts\test.bat`):

- **Read:** loads every file here through the rules' own reader.
- **Refuse:** checks that broken files are refused.
- **Play:** plays each class in a battle and requires every order the computer
  gives to be legal.
- **Match:** compares each class, field for field, with how the Godot game's
  importer reads the Astra file it came from (`Tests/GodotClassTable.txt`, printed
  by the Godot project's `tests/dump_class_table.gd`). This runs only when that
  table is present.

## Particle effects

An ability may name a particle effect from the project, such as one installed
from Fab, in its `vfx` field:
`{"system": "/Game/Pack/NS_Thing.NS_Thing", "at": "targets", "scale": 1}`.

- **Where it plays (`at`):** on the `user`, at the `point` aimed at, or once on
  each of the `targets` it touched.
- **What plays it:** the director, when the ability goes off.
- **The rules:** they read the field and ignore it. It is presentation only, like
  the damage numbers, and it is not in the checksum.
- **Looping effects:** the director switches them off after `VfxSeconds`.

The creator shows these effects because the game films them:
`Tools\VfxCatalog.bat` starts the game with `-tmvfxcatalog`. The game then:

1. plays every Niagara and Cascade system under `/Game` alone in front of a
   camera (`ATMVfxStudio`);
2. writes `Saved/VfxCatalog/catalog.json`, with a strip of frames per effect;
3. exits.

The creator's **Film again** button runs the same script. Run it again after
installing more effects from Fab.

## Not ported yet

Passive, aura and toggle abilities load, but what they add to a unit's stats is
not applied yet (`FUnit::Stat` says so where the gap is). Classes that rely on
them play weaker in Unreal than in Godot until that is ported.

## Packaging

Raw files under `Content/` are not cooked into a packaged build by default. Add
`Data/Classes` to **Project Settings → Packaging → Additional Non-Asset
Directories to Package** before shipping, or the game will start with six
classes and no explanation.

The same goes for particle effects that only a class file names. The cooker
follows references between assets, not paths written in a JSON file, so the
effects' folders need adding to **Additional Asset Directories to Cook**. If they
are missing, the game logs that an ability "names a particle effect that is not
in the project", and the ability goes off without it.
