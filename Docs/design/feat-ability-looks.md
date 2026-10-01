# Feature: ability looks

Status: Built 2026-09-30, as the human asked ("the ability visual effects ... need to match the ability
and be visually impactful; right now it's hard to distinguish between abilities from effects alone").
Code: `Source/TacticalMasters/TMBattleDirectorAbilityFx.cpp`, hooked from `TMBattleDirectorBlows.cpp`.
Presentation only: nothing here is read by the rules.

## What an ability is made of

Each ability gets one of eleven looks: fire, ice, lightning, water, wind, earth, nature, holy, shadow,
arcane, or plain steel. It is read, first match wins, from:

1. the words of its name ("Flame Lance" is fire, "Frost Dagger" ice, "Gale Bolt" wind);
2. the built-in effect it borrows (`fx`: fire, meteor, blizzard, holy_blade, sanctuary, cure, raise);
3. the status it leaves (burn fire, freeze/chilled/slow ice, wet water, stun lightning, regen holy,
   doom/silence/terrified shadow, sleep/charmed/stop arcane, root/decay nature);
4. the element the rules give it (`ElementOf`);
5. the words of its id, which begins with its class ("frost_stalker_...");
6. otherwise steel, which is split by weapon: blade, blunt (mace, bash, punch...), point (arrow, lance,
   jab...) or a thrown stone.

The word lists are `GLookWords` in the source. On the 102 class and monster files: 398 of 476
abilities get an element, 78 are steel.

## What shows

| Moment | What |
| --- | --- |
| The swing begins | Magic: a flare of its look in the user's hands, and a light of its colour. A buff or heal on itself: an aura round it. |
| It flies (a single target past 2.2 m, or a line) | Its look's particle (a fireball, a spark, a shard of ice...) instead of the plain ball, lit in its colour; earth throws a stone. |
| It lands on an area | Two layers of its look's burst, sized to the area (circle: its aoe; cone: its reach; round the user for a burst on itself), a big flash of its colour, the camera jolts. |
| On each unit it touches | Damage: its look's hit (steel: sparks for a blade, a crushing flash and dust for blunt, a puff for a point), bigger on a critical. Heal: a green-gold rising glow. Revive: a golden burst. A buff: its look's aura. A curse: its hit, and dark smoke for shadow and steel. |
| A heavy blow | The camera jolts as hard as it hurt (a third of the target's health is a proper jolt), more on a critical or an ultimate; the strongest of what lands together, not the sum. |

The class file's own `vfx` still plays as before. Each effect is scaled from the size Unreal filmed it
at (`Saved/VfxCatalog/catalog.json`, measured from the strips) to the size wanted.

To see it: `Saved\agent-looks.bat [roster]` plays a windowed battle and takes a picture 0.2 s after each
blow lands (`Saved\Match`).
