# Movement skills, stealth, and statuses put to use

2026-10-03. Asked for: "utilizing some of the status ailments that aren't in use. I also would like to add movement
based skills. For example berserkers leap skill should move him from his current location to the leap's target
destination. It should include an animation with it. I would also like stealth to be utilized. Assign to the ninja
class and create effects that include smoke on cast." The human chose: a fitting 10 statuses; Smoke Bomb reworked;
Leap Smash, Dragoon's Jump and Sky Lancer's dive animated, a Ninja Shadow Step, and dashes for melee classes.

## Rules (TMSim)

- Two new `special`s, both done **before** anything is hit, so the blow comes from where the user ends up:
  - `leap`: the user lands on the aim point, or the free walkable spot nearest it within 1.5 m (or the ability's area,
    if wider). Berserker's Leap Smash.
  - `behind`: the user lands on the free spot round the unit aimed at nearest to a metre straight behind it, never on
    top of it. Ninja's Shadow Step; striking from behind takes the back-attack multiplier.
  - `FBattle::LandingFor` finds the spot (nodes tried in a fixed order: the same on every machine);
    `ValidateAbility` refuses with nowhere to land ("There's nowhere to land there." / "There's no room behind that
    target."). The move is a `Teleported` event with `Id` "leap" or "behind". The user faces where it aimed (not when
    Off-Balance).
- `self_status` on an ability: a status the user gives itself as it goes off, after its hits (`FAbility::SelfStatusId`).
  The Ninja's Smoke Bomb leaves it Vanished (`veil`: unseen by the other sides until it deals or takes damage).
- A `vector` shape already carried the caster along its line (Dragoon's Jump, Sky Lancer's Diving Charge); the new
  dashes use it.
- Nothing new in the checksum (no new state). Online protocol 17. No baseline moved; `SimClassTest` gained "movement
  skills": Leap Smash lands by where it smashes, Shadow Step lands behind its target, Smoke Bomb leaves the Ninja
  Vanished and unseen.

## The classes (Content/Data/Classes; `creator.rebalanced` 2026-10-03)

| Class | Ability | Now |
|---|---|---|
| Berserker | Leap Smash | `leap`: leaps 2-5 m and smashes round where he lands; Stun 1 turn |
| Ninja | Shadow Step (was Shuriken) | `behind`, 2-6 m, power 32, cooldown 2, smoke where it leaves and lands |
| Ninja | Smoke Bomb | round itself: Vanished 2 turns (`self_status`), enemies within 2 m Slowed 2 turns; smoke burst |
| Windblade | Gale Dash (was Gale Lance) | `vector` dash-strike, 2-6 m |
| Wind Dancer | Gale Rush (was Gale Kunai) | `vector` dash-strike, 2-6 m, TG -25% |
| Paladin | Aegis | Protect 2 turns (physical hits a third less), in place of AttDef +8 |
| Cantor | Holy Anthem | also Shell 2 turns (spells a third less) |
| Lumimancer | Prism Ward (was Holy Staff) | Reflect on an ally, 2 turns |
| Seraph Caller | Seraph Pact | one ally: the buffs, and Reraise 3 turns |
| Time Mage | Time Stop (ultimate) | Stop in place of Stun |
| Siren | Siren Song (was Tide Anthem) | enemies within 2 m Charmed 1 turn |
| Dread Knight | Shadow Bash | also Terrified 1 turn |
| Necromancer | Umbral Burst | also Decay 2 turns |
| Tide Cleric | Rod | Wet 2 turns |
| Chemist | Oil Bomb (was Fire Bomb) | Oiled 2 turns in place of Burn: fire hits harder and burns twice as long |

The class creator's library (E:\TacticsClassCreator) has the Berserker's and the Tide Cleric's changes too (its own
effects kept); its format accepts `self_status` and the two specials.

## The view (TacticalMasters)

- `FTMMotion::Flight` (TMBattleDirectorMotion.cpp): a movement skill isn't walked. A leap goes up in an arc (height by
  distance) playing the body's jump clip, and the blow lands on landing, with a small camera thump. A dash slides fast
  and low, the swing playing as it goes. A step behind vanishes in a smoke burst where it stood and appears where it
  lands (its ability's own smoke plays there), then strikes. Dragoon's Jump arcs; charges and rushes dash.
- Jump clips: a set's `jump` / `land` (new extra keys in characters.json), or else the Paragon hero's own
  `Jump_Start` / `Jump_Land` found beside its idle clip.
- Status glows for Vanished (seen only by its own side), Reraise, Wet and Oiled.

## Not done

- Sky Lancer's Skyfall and Dragoon's Highwind still hit from where they stand; they could be leaps too.
- Suppressed, Guarded, Off-Balance and Wounded still have no class.
