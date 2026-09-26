# Characters: one skeleton, many sources

The game wants the look of a modern Unreal title, characters from Paragon to
start with, and the freedom to make new ones later in Tripo or Blender. Those
three pull in different directions unless one decision is made up front.

**Every character in this game is bound to the same skeleton, and every
animation is authored against that skeleton.**

The canonical skeleton is Unreal's own **SK_Mannequin (Manny)**.

## Why that one

- One animation set, not thirty-nine. A class is a mesh and a set of materials;
  how it moves is shared. Adding the 87th class costs no animation work.
- One Animation Blueprint drives every unit, so the state machine that decides
  idle, walk, attack, cast, hurt and fall is written once.
- It is the best-supported retarget target in Unreal. Anything humanoid — a
  Paragon hero, a Tripo generation, a Blender rig, a Mixamo download — has a
  documented path onto it through the IK Retargeter.

The cost is honest: retargeting a Paragon hero onto Manny does not preserve
every nuance of a rig built for that character. For a tactical game seen from
a camera that shows the board, that trade is worth taking. If a particular hero
ever looks wrong, it can keep its own skeleton as an exception — but it then
needs its own animations, so exceptions should be rare and deliberate.

## Where things live

    Content/
      Characters/
        Skeleton/        SK_Mannequin, its skeleton asset, the shared physics asset
        Animations/      the shared set, all on the canonical skeleton
        Paragon/         heroes as downloaded, untouched
        Retargeted/      their meshes bound to the canonical skeleton
        Custom/          Tripo and Blender characters
        Materials/       the variants that let one body serve several classes
      Data/
        Classes/         the 81 class files (already here)
        CharacterMap/    which class wears which body and which materials

Paragon content stays exactly as downloaded in `Paragon/`. Nothing is edited in
place, so a hero can be re-downloaded without losing work; everything derived
lives in `Retargeted/`.

## 39 heroes, 87 classes

They do not map one to one and should not. A body plus a material variant plus
a colour is a class. A Paragon knight in three liveries is three classes that
read as related, which suits a game whose classes come in elemental families.

`Data/CharacterMap` holds that mapping, so changing which body a class wears is
data rather than a code change, and a class can start on a stand-in and be
upgraded later without touching the rules.

## Bringing in a Paragon hero

1. In the editor, **Fab** → filter **Price: Free** → search Paragon. Each hero is
   its own listing. **Add to Project**, choosing this project.
2. It lands under `Content/ParagonX/`. Move it to `Content/Characters/Paragon/`.
3. Make an **IK Rig** for the hero's skeleton and one for the canonical skeleton
   if it does not exist yet.
4. Make an **IK Retargeter** from the hero to the canonical skeleton, and export
   the retargeted skeletal mesh into `Retargeted/`.
5. Point a class at it in `CharacterMap`.

The first hero is the slow one. After that the retargeter is a copy-and-change.

## Bringing in a character from Tripo

Tripo generates a mesh and can rig it. What matters is what comes out the other
end, so check these before spending time in Unreal:

- Export **FBX** with the skeleton included, not a bare mesh.
- **Z-up, metres**. Unreal is centimetres and Z-up; a character that arrives a
  hundred times too large or lying on its face is almost always a units or
  up-axis mismatch, not a broken file.
- A **humanoid bone hierarchy** — one root, a spine, two arms, two legs. The
  retargeter matches by structure, so extra bones are fine and missing limbs
  are not.
- Triangulate before export. Unreal triangulates anyway, and doing it yourself
  means what you see in Tripo is what you get.

Then: IK Rig for the imported skeleton → IK Retargeter onto the canonical
skeleton → the shared animations work on it.

Generated meshes are usually heavier and less tidy than hand-authored ones.
Expect to decimate in Blender and expect the first attempt at weights to need
help around the shoulders and hips.

## Bringing in a character from Blender

Same destination, shorter road, and worth using Blender to clean up whatever
Tripo produces rather than treating them as alternatives.

- Export **FBX**, armature and mesh only, **Apply Transform** on.
- Armature in **rest pose**, scale applied (Ctrl+A), rotation applied.
- **Z-up, -Y forward**, which is what Unreal expects from Blender's FBX exporter.
- Name bones to match the canonical skeleton where you can. The retargeter can
  be told a mapping by hand, but matching names make it automatic.

## What this buys

A class can begin as a grey Paragon body, become a proper hero, and later be
replaced by something generated and cleaned up — and at no point does the
animation work, the Animation Blueprint, or anything in the rules change. The
rules already know nothing about how a unit looks; this keeps it that way.
