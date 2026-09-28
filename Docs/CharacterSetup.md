# Giving the classes their own characters

Where things stand, what the code already does, and the editor steps that are
yours (editor work is human-only in this project). Read
[CharacterPipeline.md](CharacterPipeline.md) first: it explains the one
decision everything here rests on. Every character is bound to one skeleton,
Unreal's mannequin, and every animation is authored for it.

## What already works, with no editor work

- **Bodies come from data.** `Content/Data/CharacterMap/characters.json` says
  which body each class wears. A class is looked up by its id under `classes`,
  then by its look (`squire`, `knight`, `archer`, `monk`, `black_mage`,
  `white_mage`) under `looks`, then `default`. Today squires, knights and monks
  wear Manny; archers and mages wear Quinn.
- **Units animate from the battle.** A unit idles when it stands. It walks its
  actual path, and runs if the walk is long. It swings a weapon for a physical
  attack and makes the charged two-handed move for everything else. It flinches
  when hit, falls when knocked out and lies there until raised or gone, then
  stands again. The clips are the mannequin's own, already in
  `Content/Characters/Mannequins/Anims`.
- **Nothing here touches the rules.** The director plays the clips on each
  mesh because the rules said a unit moved, hit or fell. A battle is the same
  whether or not anything animates.

No Animation Blueprint is needed for any of that. The director plays one clip
at a time directly on each unit's mesh. An Animation Blueprint becomes worth
having only when you want blending, such as a walk that eases into a swing, or
an upper body that aims while the legs walk. The last section covers that, and
it's optional.

## Step 1: Paragon heroes, animated (retargeting)

Sparrow is already in the project, but on Paragon's own skeleton. That's why
the mannequin's animations can't play on her, and why units stood in a T-pose
while she was the default. The fix is to copy the mannequin's clips onto her
skeleton. It's done once per hero, for the dozen or so clips the game uses,
in one batch.

(This is Unreal's well-trodden route: the retargeter copies animations
between skeletons. Moving a mesh onto another skeleton only works when the
bones match, and Paragon's don't. So each hero keeps its own skeleton and
gets its own copy of the set. That's a small cost next to the alternative.)

1. **IK Rig for the mannequin**, once. In `Content/Characters/Skeleton`:
   right-click → Animation → Retargeting → **IK Rig**, and pick
   `SKM_Manny_Simple`. Use **Auto Create Retarget Chains** and save it as
   `IKR_Mannequin`.
2. **IK Rig for Sparrow**: the same with Sparrow's skeletal mesh (under
   `Content/ParagonSparrow/Characters/Heroes/Sparrow/Meshes`). Auto-create the
   chains, and check the arms, legs, spine and head were found. Save as
   `IKR_Sparrow`.
3. **IK Retargeter**: right-click → Animation → Retargeting → **IK Retargeter**,
   source `IKR_Mannequin`, target `IKR_Sparrow`. Save as
   `RTG_Mannequin_To_Sparrow`. If the two stand in different poses (T against
   A), use **Auto Align** in the retarget pose editor until they match.
4. **Copy the clips.** In the Content Browser, select every clip named in the
   `mannequin` set of `Content/Data/CharacterMap/characters.json`: idle, walk,
   run, the three attacks, cast, the hits, the deaths and rise. Right-click →
   **Retarget Animations**. Choose the retargeter from step 3, and put the
   copies in a new folder `Content/Characters/Retargeted/Sparrow/`.
5. **Check it.** Open one of the copies: Sparrow should do the move.

### Then give her to classes, in data

Add a set made of the copies, and a body that uses it:

```json
"animations": {
  "sparrow": {
    "idle": "/Game/Characters/Retargeted/Sparrow/MM_Idle.MM_Idle",
    "walk": "...", "run": "...", "attack": ["...", "...", "..."],
    "cast": "...", "hit": ["...", "..."], "death": ["...", "...", "..."], "rise": "...",
    "walkSpeed": 170, "runSpeed": 380
  }
},
"bodies": {
  "sparrow": {
    "mesh": "<Sparrow's skeletal mesh, as its object path>",
    "yaw": 180,
    "animations": "sparrow"
  }
},
"looks": { "archer": "sparrow" },
"classes": { "sun_archer": "sparrow" }
```

- **Finding the object path:** in the Content Browser, right-click the asset →
  Copy Reference, then keep the part inside the quotes.
- **`yaw`:** turns the mesh to face the way the unit faces. Paragon heroes
  want `180`, and the mannequins want `-90`. If a body walks sideways, try the
  other two.
- **No build needed:** the map is read when a battle is built, so a restart of
  Play is enough.

Every further hero goes the same way: an IK Rig, a retargeter from the
mannequin, a batch of copied clips, a set and a body.

## Step 2 (optional): telling the sides apart by colour

For now, blue and red are told apart by their name plates and the lights at
their feet. The director already tags every unit's mesh with its side, as
custom primitive data slot 0 (0 for blue, 1 for red). A material only has to
read it:

1. Open the body's material. For Manny that's under
   `Content/Characters/Mannequins/Materials`. Work on a copy, e.g.
   `M_Unit_Team`, so the template stays untouched.
2. Add a **Custom Primitive Data** node, index 0. **Lerp** between a blue and a
   red tint with it as the alpha, and multiply the base colour by the result.
3. Assign the copy to the mesh, or to a copy of the mesh for the game.

## Step 3 (optional): more and better animations

Any clip made for the mannequin's skeleton can be dropped into an animation
set in the character map. That includes Fab animation packs, Mixamo (via the
retargeter above) and Lyra's set. Each role has a key:

| key | when it plays | what suits it |
|---|---|---|
| `idle` | standing | a relaxed or combat idle |
| `walk`, `run` | walking; `run` for walks over 3.5 m | in place, forward; set `walkSpeed`/`runSpeed` (cm/s) to the clip's pace so feet don't slide |
| `attack` | a physical (weapon-scale) ability, one per slot | swings, stabs, shots |
| `cast` | every other ability: spells, heals, songs | a two-handed cast |
| `hit` | taking damage | short flinches |
| `death` | knocked out; the last frame is held | falls that end lying down |
| `rise` | raised again | getting up, or a landing |

Several clips in a list are spread across slots or hits so it doesn't look
repetitive. A different skeleton gets its own set, and its bodies name that set.

### Motions: a different move for each kind of ability

Every ability has a **motion**: `melee`, `heavy`, `shoot`, `bolt`, `area`,
`heal`, `buff`, `revive`, `channel`, `dash` or `none`. A class file can name one
with `"anim"`. If it doesn't, the game works one out: a weapon at arm's length
is `melee`, and `heavy` if it's the ultimate or hits an area. A weapon at range
is `shoot`. A spell at one target is `bolt`, and over an area it's `area`.
Healing, raising and blessing each have their own motion, a channelled spell
is `channel` and a charge is `dash`. Toggles, passives and auras are `none`.

An animation set maps motions to clips under `"motions"`:

```json
"motions": {
  "shoot": {
    "release": ["/Game/.../Primary_Fire_Fast.Primary_Fire_Fast"],
    "intro": "/Game/.../RMB_Drawback.RMB_Drawback",
    "windup": "/Game/.../RMB_Loop.RMB_Loop",
    "castRelease": "/Game/.../RMB_Fire.RMB_Fire"
  }
}
```

| key | when it plays |
|---|---|
| `release` | the ability goes off (a list is spread across slots) |
| `intro` | an ability with a cast time starts charging |
| `windup` | loops for as long as it charges, or while a channel goes on |
| `castRelease` | a *charged* ability goes off (instead of `release`) |

A set doesn't need every motion. A missing one falls back to a close relative:
`heavy` to `melee`, `dash` to `heavy` or `melee`, `shoot` and `area` to `bolt`,
`heal`, `buff` and `revive` to each other and then `bolt`, and `channel` to
`area` or `bolt`. If none of those has clips either, a weapon motion uses the
set's `attack` clips and anything else uses `cast`. A cast that fizzles stops,
and the set's first `hit` clip plays. Additive clips are refused here too; the
log says which.

## Step 4 (optional): an Animation Blueprint

Only if you want blending: a swing that starts mid-walk, aim offsets, or
additive flinches over a walk. The director would then set variables on it
(speed, is-down, what to play) rather than playing clips directly. That's a
code change on my side once the blueprint exists, so ask when you get there.

## If something looks wrong

| you see | why | fix |
|---|---|---|
| a T-pose | the body's skeleton isn't its set's skeleton | copy the clips onto its skeleton (step 1) and give it that set |
| walking sideways or backwards | the mesh's forward axis | change `yaw` for that body |
| feet sliding | walk speed doesn't match the clip | tune `walkSpeed` / `runSpeed` |
| nothing changed | the map is read once per run | stop and start Play |
| a unit invisible | the mesh path is wrong | the log says `character map: nothing at …` with the path |
