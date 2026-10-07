# Ground zones (area denial)

Status: built 2026-10-04 (rules, game, tests); not yet compiled in Unreal or played. Online protocol 24.

The human's ask (2026-10-04): "Using the mechanics within the game, create area denial skills that target the
ground and persist for turns. Some that deal damage, some that apply debuffs, some that give sight of an area."
Mocked up on the "v20 Play Test Mockups" canvas ("Area denial: ground skills that last"), then: "Implement them
and choose one of the suggested classes ... suggest 1 to swap out for the new one."

## The rule

An ability with `"special": "zone"` and a `"zone"` object (Docs: the class creator's FORMAT.md) touches nobody as
it goes off. It lays its own shape (circle, or line from the caster) where it was aimed, as `FBattle::FZone`, for
`turns` of its caster's turns (counted down as each of the caster's turns begins). If the caster falls it does
nothing more, and is gone as the next turn begins (`ZoneLive`). One of each ability per caster: laid again, the old one goes.

A unit of the side the zone is for (`target`: enemy, or ally for smoke) is touched when its turn begins inside the
zone, or when it ends a walk inside it; once a turn per zone (`FZone::Touched`, by `FUnit::Serial`). Flying units
are above it. A touch:
- `percent` of max HP lost, as burning ground takes it (no roll, no defence). Event: `Hit`, Id `"zone"`, By the caster.
- the ability's `element` meets the unit (`ElementReactions`): lightning shocks the Wet, ice freezes the Wet, fire
  lights the Oiled, water wets.
- the ability's `status`, and `also` (a second status) -- unless the element already gave it. `once`: statuses only
  the first time each unit is touched. Statuses that hold a unit back during its own turn (root, silence, slow
  walking, blind) get a turn more when it was touched mid-turn (a walk ending there), so they reach its next turn.
- a shock, freeze or anything else that leaves the unit able to do nothing, given as its turn begins, takes that
  turn and is spent on it (`ZonesAtTurnStart`).

Sight: `sight` metres round the aim point, for the caster's side (`CanSee` -> `ZoneSees`); such a zone may be thrown
where the side can't see and without a line (`ValidateAbility`). `reveal`: the side finds units hiding in tall grass
or smoke in it. `hide`: its own side hides in it as in tall grass (`Hidden`).

Fire and water: any fire ability reaching a `flammable` zone lights it (`bIgnited`: 6% and Burn, at least 2 more
turns; event `ZoneIgnited`); any water ability reaching a burning zone (lit tar, or one whose element is fire) puts
it out (`ZoneEnded`).

The computer: values a zone by what it does to those standing in it now (twice its harm, its statuses, a waiting
reaction, light where it is blind), more for every turn it lasts (`FAIPlayer::ZoneScore`), and keeps its units out
of the other side's zones (`GroundValue`). Nothing here runs in a battle where no zone is laid: every baseline holds.

## The twelve

| Skill | Class (swapped out) | Shape | Turns | Does |
|---|---|---|---|---|
| Ember Field | Flame Sorcerer (Burst) | circle 2.5 m, 2-8 m | 3 | 6%, Burn 1, fire |
| Static Mire | Stormcaller (Chain Lightning) | circle 2 m, 2-8 m | 3 | 3%, lightning |
| Caustic Pool | Necromancer (Umbral Burst) | circle 1.5 m, 2-8 m | 4 | 4%, Decay 1 |
| Frost Patch | Cryomancer (Hailstorm) | circle 2.5 m, 2-8 m | 3 | Chilled 2, ice |
| Tar Slick | Snare Hunter (Hamstring) | line 1.5 m wide, 1-6 m | 3 | Oiled 2, Slow 1, flammable |
| Bramble Thicket | Druid (Entangle) | circle 2 m, 2-7 m | 2 | Root 1, once |
| Hush Circle | Null Monk (Hush) | circle 2 m, 1-6 m | 2 | Silence 1, Shred 1 |
| Tide Pool | Sea Witch (Drown Curse) | circle 2 m, 2-8 m | 3 | Wet 2, water |
| Scout Flare | Sun Archer (Holy Barb) | sees 4 m, 3-12 m | 2 | sight, reveal |
| Watcher's Eye | Oracle (Sleep) | sees 3.5 m, 1-10 m | 4 | sight |
| Lantern Glow | Lumimancer (Radiance) | sees 2.5 m, 2-8 m | 2 | sight, reveal, Blind 1 |
| Smoke Veil | Shadow Stalker (Shadow Veil) | circle 2 m, 0-5 m | 2 | hide (allies) |

Each class file's `creator.rebalanced` says what was swapped. Tests: `Tests/SimZoneTest.cpp`.

## On the board

Each zone this screen may show (its side's, or where the middle is seen) is painted on the ground in its colour
(`GroundZoneColour`): fire orange, lightning blue, ice white-blue, water blue, decay green, roots green, silence
violet, tar dark with an amber rim, sight gold with a dashed rim, smoke grey. Harmful ground has an inner ring. Its
turns left are pips over its middle; the pointer on them names it and says what it does. The log says when one is
laid, catches fire or fades. Zone sight lifts the fog.

## Not done

- Watcher's Eye can't be broken (the mockup said one hit would).
- No lasting particle effect on a zone: the ground paint is the whole look. Worth adding from the Fab packs.
- The class creator's own library still holds the old abilities for these twelve classes: installing from the
  creator would put them back. The creator now accepts `"special": "zone"` (app/tmclass.mjs, FORMAT.md).
