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
- **Whole battles:** `Tests/SimTraceTest.cpp` replays the battles Godot recorded with Godot's own
  classes, read from `Tests/GodotClassTable.txt`, so rebalancing a class here never breaks it.
- **Match:** compares each class, field for field, with how the Godot game's
  importer reads the Astra file it came from (`Tests/GodotClassTable.txt`, printed
  by the Godot project's `tests/dump_class_table.gd`). This runs only when that
  table is present.
  A class whose creator notes carry `rebalanced` (changed on purpose) is left
  out; one with `reworded` (its ability descriptions rewritten to say what the
  abilities do, 2026-10-02) is compared on everything but those words.

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

## Pets

An ability with `"special": "pet"` calls up a pet when it goes off (2026-10-02):
`"pet": {"job": "golem_pet", "turns": 3}`.

- **What it is:** a class file in `Content/Data/Monsters` (so no side can field it),
  waiting off the board from the start, one per unit whose class calls it.
- **Where it comes:** where the ability was aimed, or the nearest open spot round
  there or round its caller. Called again while it is out, it moves to the new
  spot, whole, its time started afresh.
- **Who plays it:** the computer, for its caller's side (online, the host's).
- **How long:** the given number of its own turns; then it leaves. Nothing
  raises it, and it never counts towards a win.
- **Tests:** `SimCampTest` plays whole battles with three summoners.

## Passive, toggle and aura abilities

They work: a passive's buffs, and a toggle's while it is on, are added in
`FUnit::Stat`; an aura reaches every unit in its radius as a timed buff at the
start of each turn (`FBattle::ApplyAuras`).

## How strong a class is

The class lab (`Tools/ClassLab`, built by its `Build.bat`) plays a class on the
rules the game plays (`TMSim::GameTuning`) against the game's hard computer,
standing in for the reference team's member with its first role, and four of it
against that team. `TMClassLab tournament Content/Data/Classes` measures them
all; the class creator shows the result in its Playtest tab ("Among every
class"). A class as good at its job as the one it replaces wins half.

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
