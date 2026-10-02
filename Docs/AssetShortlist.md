# Free Asset Shortlist (Fab)

Free assets on Fab that suit Tactical Masters, researched 2026-09-27. Prices and availability change,
so check a listing before relying on it. Epic's own content is free for good; community freebies can go
back to paid. Anything already claimed stays in your Fab library.

Everything here should end up on the **one canonical skeleton** (SK_Mannequin), as `Docs/CharacterPipeline.md` requires.
Bodies that aren't on it get retargeted.

**Already in the project:** Paragon: Sparrow (`Content/ParagonSparrow`), a free Niagara pack
(`Content/FreeParticle_SoftTofu`), and the UE5 Mannequins.

## Time-limited: claim before Mon 2026-10-06, 6:59 AM Pacific
| Asset | What | Fits |
|---|---|---|
| [Paladin RPG Set](https://www.fab.com/listings/59fb644c-2d7f-401f-ab48-4cf02dce7392?lang=en) (Polyphoria) | Paladin armour for MetaHuman bodies on the UE5 skeleton. Needs UE 5.6+ | The paladin class |

Fab's free rotation changes every two weeks: check [fab.com/limited-time-free](https://www.fab.com/limited-time-free?lang=en)
every other Monday. Recent rotations have had fantasy towns, a medieval village and temple ruins.

## Characters and combat animations: Paragon (Epic, free, Unreal-only)
Each hero comes with its mesh, skins, Animation Blueprints, its **own attack, ability, hit-react and death
clips**, and effects. This is the best free source of combat animation. Retarget with `Tools\AddHero.bat`
(see `Docs/CharacterSetup.md`). Hero-to-class fits, from the heroes' designs (check each listing):

| Hero | Fits | Hero | Fits |
|---|---|---|---|
| Aurora (ice) | frost / cryo classes | Muriel (healer, flies) | cleric, oracle *(in backlog: more-heroes)* |
| Kwang (lightning sword) | thunder / storm blades *(in backlog)* | Morigesh (witch) | hexers, witches |
| Gideon (sorcerer) | mages, warlocks *(in backlog)* | The Fey (nature) | druid, sylvan muse |
| Greystone (sword knight) | knights, paladins *(in backlog)* | Narbash (war drum) | war drummer |
| Terra (axe and shield) | earth / sentinel classes | Kallari | ninja |
| Serath (winged) | seraph caller, templar | Yin / Crunch | brawlers *(in backlog)* |
| Countess (shadow) | shadow stalker, hexblade | Sparrow | archers *(have)* |

- [Paragon: Agora and Monolith Environment](https://www.fab.com/listings/6f401fb5-88b5-41b4-bf1b-62321414e1f0):
  500+ stone arena set pieces and props, for dressing battle maps.
- Paragon effects are mostly the older Cascade particle systems.
- Licence: Unreal-only, and you may not use the name "Paragon" to market the game.

## Effects
| Asset | What | Fits |
|---|---|---|
| [Niagara Examples Pack](https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600) (Epic) | 50+ systems: buffs/debuffs, fire, smoke, lightning, impacts, hit dissolves, pings and markers | Status looks, KOs, the ready unit and target markers |
| [18 Free Spells Niagara](https://www.fab.com/listings/967b7892-d46c-4d75-8121-33418a6899ce?lang=en) (UrtanoVFX) | Spark, buff and explosion spells with colour and size parameters | One effect recoloured per element. Paid ice, fire, water and poison packs from the same seller if needed |

## Animations already on the UE5 Mannequin
| Asset | What |
|---|---|
| [Mage Collection Samples](https://www.fab.com/listings/78f85a52-4132-4ce1-bb87-0c9e3a45a42a?lang=en) (Rapa Motion) | 10 spellcasting and conjuration clips (air, water, fire) |
| [MoCap Online Free Animation Pack](https://www.fab.com/listings/64c53af0-dcb7-4483-9d65-5cbc84bd9a93?lang=en) | Idles, walks, **deaths** |
| [Game Animation Sample](https://www.fab.com/listings/880e319a-a59e-4ed2-b268-b32dac7fa016) (Epic) | 500+ movement and traversal clips, no combat. [A community retarget to Manny](https://www.fab.com/listings/259f8545-f820-47b3-8fc1-e8ec5458214d?lang=en) exists, but retargeting Epic's original yourself avoids relying on a third party |

## Gaps and cautions
- **Melee attacks and hit reactions on Manny:** apart from Paragon, the good packs are paid
  (for example JKMotion's [96-clip hit-reaction pack](https://www.fab.com/listings/effa9c6e-1571-4a53-b235-a5411ddf5401)).
- **Megascans** have mostly been paid since 2025. Only what was claimed before then stays free.
- **Infinity Blade** packs (Effects, Warriors, Weapons, Fire Lands) are reported unobtainable through Fab.
  Skip the unofficial copies.
- **"Allows usage with AI: No"** appears on many listings, including Epic's. Don't feed those meshes into Tripo
  or other generators (the pipeline mentions Tripo).
- **Cropout, Lyra and Valley of the Ancient** (Epic, free) are good references (Cropout's top-down camera and
  CommonUI, for instance) but not art that matches this game.
