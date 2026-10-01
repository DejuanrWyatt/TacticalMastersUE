# Maps and themes

A map is ground the rules fight on; a theme is how that ground looks. They are
separate files, so any map can be shown in any theme, and a theme changes
nothing a battle does.

## Maps: `Content/Data/Maps/<id>.tmmap.json`

```json
{
  "format": "tactical-masters-map",
  "version": 1,
  "id": "crown_keep",
  "name": "Crown Keep",
  "desc": "Shown on the setup screen.",
  "theme": "ruined_keep",
  "boss": "helix_prime",
  "top": ["11111111111111111111", "..."],
  "spawns": [[2.75, 4.75], [0.75, 6.75], [4.75, 5.75], [2.75, 8.75]]
}
```

- **top** is the top half of the map, a character per 2 m tile, as Godot's
  `map_data.gd` writes them: `1`-`9` a height (0.7 m a level), `~` water (can't
  be walked, can be seen and shot across), `#` rock (blocks walking and sight),
  `x` embers (burn a unit that starts its turn there), `+` a spring (heals).
  The bottom half is the top turned 180 degrees, so neither side has better
  ground. 3 to 20 rows (the map is twice as tall), 8 to 40 tiles wide.
- **spawns** are blue's four starting spots in metres; red starts at the same
  spots turned about. The first is the side's spawn point: the planning area
  is 6 m around it, and a unit that sees nobody walks toward the enemy's.
- **theme** is the look it is shown in unless the setup screen picks another.
- **boss** is the monster class (`Content/Data/Monsters`) that wakes at the
  map's boss camp in the middle when neutral camps are Standard or Wild, unless
  the setup screen's Boss is Random. Optional; without it the boss camp is left
  out. See `Docs/design/feat-neutral-camps.md`.

The rules read the file (`TMSim::ReadMapFile`) and refuse, with the reason, a
map with ground they don't know, uneven rows, spawns off the ground or on top
of each other, or any blue spawn that cannot walk to every red one. A refused
map is left out and the log says why. `Tests/SimMapTest.cpp` checks every map
file, and plays a whole computer-vs-computer battle on each.

Highlands is built into the rules exactly as Godot has it, and the parity tests
stand on it; a file can't replace it. Godot's other four maps (River Crossing,
Fortress, Ashfields, Open Plains) are not ported yet.

**Crown Keep** (20 x 16 tiles, 40 x 32 m): a level-4 keep in the middle with a
sheer drop all round and one ramp for each side, so whoever holds the middle
gets the full +30% height bonus and has to hold it against one approach. Ruined
pillars shelter one flank, a stream with fords splits the other, springs sit out
in the open and embers pay for the short way. Computer against computer, the
first blow lands about 10 seconds in (1.7 on Highlands) and a battle lasts four
to five minutes.

## Themes: `Content/Data/Themes/<id>.theme.json`

```json
{
  "format": "tactical-masters-theme",
  "name": "Winter Pass",
  "ground": {"tops": ["#e6ecf2", "..."], "side": "#7d8590", "jitter": 0.03},
  "rock": {"colour": "#8f9aa8", "style": "boulders"},
  "water": {"colour": "#9fd6ea", "opacity": 0.85, "glows": false},
  "embers": {"colour": "#2f2220", "glow": "#ff8a3a"},
  "spring": {"colour": "#5fd0d8", "glow": "#9ffcff"},
  "around": {"ground": "#dfe7ee", "trees": "pine", "treeCount": 80, "leaves": "#2f4a3a", "trunk": "#3d2e25", "rockCount": 25},
  "sun": {"pitch": -30, "yaw": -20, "intensity": 6, "colour": "#e6efff"},
  "sky": {"intensity": 5},
  "fog": {"density": 0.035, "colour": "#cfdcea"}
}
```

- **ground.tops**: the colour of the ground's top at each height from level 1
  up (the last is used above); **side** the cliff faces; **jitter** how much
  each tile wanders from its colour, so the ground isn't a chessboard.
- **rock.style**: `pillars` (a ruin), `boulders` or `crystals`.
- **water.glows** makes the water give off light: lava.
- **around**: the land past the board. `trees` is `pine`, `round`, `dead` or
  `none`. Nothing is placed close to the camera's side of the board.
- **sun**, **sky**, **fog** set the level's own sun, sky light and height fog
  while a battle is played (never in the editor).

Four ship with the game: Summer Meadow (Highlands' own), Autumn Ruins (Crown
Keep's own), Winter Pass and Ashen Caldera (lava for water, crystals for rock).

Everything is built from the engine's basic shapes and material, so a theme
needs no editor work. Real meshes from Fab (the Paragon Agora and Monolith
environment, say) can replace the shapes later without changing the files.

## Real meshes from Fab: a theme's kit

A theme can name a **kit**: meshes to build with instead of the basic shapes.

```json
"kit": {
  "top": ["/Game/Pack/Meshes/SM_Floor_01.SM_Floor_01"],
  "rock": ["/Game/Pack/Meshes/SM_Pillar_01.SM_Pillar_01", "..."],
  "tree": ["/Game/Pack/Meshes/SM_Tree_01.SM_Tree_01"],
  "boulder": ["/Game/Pack/Meshes/SM_Rock_03.SM_Rock_03"],
  "rockFill": 0.9, "treeHeight": 6, "boulderSize": 2
}
```

- **top**: the walkable ground's surface, stretched to exactly a tile. The
  painted slab under it stays, hidden, because it is what a click on the board
  finds. Embers and springs keep their own look.
- **rock**: what stands on a rock tile, scaled to fill `rockFill` of the tile
  and never shorter than 1.8 m, so it always reads as cover.
- **tree**, **boulder**: what stands around the board, `treeHeight` and
  `boulderSize` metres.
- **cliff**: rock faces for the tall steps (two heights or more) between tiles, stretched along the
  edge and as tall as the drop; **structure**: ruins that stand on about one rock tile in five
  instead of a rock, and larger ones in the land round the board. Without them, Paragon's Monolith
  rocks and ruins are used (`TMBattleDirectorCliffs.cpp`).
- Heights are drawn 1.8 times taller than the rules' 0.7 m a step (`ViewLevelScale`), so hills
  read from the camera; only the look, the rules count steps. Foliage grows only from a kit:
  with none named, nothing of that kind grows (no painted stand-in shapes).

Each piece is fitted by the mesh's own size and stood on its own base,
wherever its pivot is, and turned at random. A list gives variety: one is
picked per piece. A mesh that is not in the project is said once in the log,
and the basic shape stands in for it. The cliff faces and the land around stay
painted in the theme's colours.

What grows has a kit of its own, under `"foliage"`:

```json
"foliage": { ..., "kit": {
  "grass": ["/Game/KiteDemo/Environments/Foliage/Grass/FieldGrass/SM_FieldGrass_01.SM_FieldGrass_01"],
  "flower": ["..."], "bush": ["..."],
  "grassHeight": 0.45, "flowerHeight": 0.4, "bushHeight": 1.1
} }
```

Each is fitted to its height (metres) by its own bounds and stood on its base,
like the pieces above, and drawn instanced, one draw per mesh. With a kit the
theme's grass and flower colours are not used. Today the themes use the Kite
Demo's nature meshes (`Content/KiteDemo`: field grass, buttercups, scabious,
yarrow, heather, bog myrtle, ferns, hill trees, Scots pines, boulders) and the
Free Stylized Lava Rock pack for Ashen Caldera. Winter Pass keeps its pale
stand-in grass, since the Kite Demo's is summer green.

**Your part: getting a pack.** Downloads are yours (editor work is human-only
here). Good free starting points:

| pack on Fab | what it gives | theme to start from |
|---|---|---|
| **Paragon: Agora and Monolith Environment** (Epic) | stone arena pieces: floors, pillars, ruins, rocks | `ruined_keep` |
| any free nature pack with trees and rocks (search Fab with the Free filter; Fab's limited-time-free rotation changes every two weeks) | trees, bushes, boulders | `meadow` or `winter` |

Then, with the editor closed:

1. Add the pack to this project from Fab. Note its folder under Content, e.g.
   `Content/ParagonAgora`.
2. See what the tool finds, writing nothing:
   `Tools\AddKit.bat /Game/ParagonAgora agora --base ruined_keep --name "Agora"`
   It lists the meshes it would use for each part, chosen by their names
   (Floor, Pillar, Rock, Tree...) and shapes (floors are flat, cover is tall).
3. Add `--write` to write `Content/Data/Themes/agora.theme.json`.
4. Play a battle in it: Theme on the setup screen, or `-tmtheme=agora`.
   A guess by name can be wrong; the theme is plain JSON to correct.

## Making maps: the map maker

The class creator (`E:\TacticsClassCreator`) has a map maker: **Map maker** in
its sidebar. Paint heights, rock, water, embers and springs on a grid (the other
half paints itself), put blue's four spawns, and pick the theme. It shows:

- **The rules' verdict** as you paint: the same checks the game makes.
- **The numbers:** size, walking distance between the sides, standing room,
  high ground, rock, water, embers and springs.
- **How it looks**, in its theme, from the corner the camera starts at.
- **Play it:** the class lab (`TMClassLab map <file> [games]`) fights 8 to 64
  computer-vs-computer battles on the game's rules and reports the win rate by
  side, how long a battle takes, when the first blow lands and any refused
  order. The grid then shows where units walked and where they fell, so ground
  nobody uses, or one spot everyone dies on, shows up at once.
- **Install** checks it again, plays two battles, and writes it here; the game
  offers it on the setup screen when it next starts.

## Choosing them

The setup screen has **Map** and **Theme**; Theme cycles through "the map's
own" and each theme. For a run with nobody at the setup screen:
`-tmmap=crown_keep -tmtheme=winter`.
