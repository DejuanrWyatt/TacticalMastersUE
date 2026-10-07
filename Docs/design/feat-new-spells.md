# The unique and mobility spells

Status: built 2026-10-05 (rules, game, tests); not yet compiled in Unreal or played. Online protocol 25.

The human's ask (2026-10-04): "Start adding unique spells like one that infects a unit and does dot damage per
turn. If they end a turn near an ally, it spreads. Mock up 10-20 abilities with unique behaviors. Create 10-20 more
spells with mobility in mind" (pull yourself to an ally, swap with an ally or enemy, dash 3 spaces). Mocked up on
the "v20 Play Test Mockups" canvas, then: "Create the skills and add them to a suggested class and remove an ability
from them that makes sense."

Rules in `Source/TMSim/Private/SimSpells.cpp`, the statuses in `SimTypes.cpp` (after "boon"), the hooks where each
acts (`TickStatuses`, `EndTurn`, `Hurt`, `KnockOut`, `ResolveAbility`, `ZonesAfterWalk`). Nothing runs unless one of
these statuses or specials is in the battle: every baseline holds. Tests: `Tests/SimSpellTest.cpp`.

## Unique

| Spell | Class (swapped out) | Rule |
|---|---|---|
| Contagion | Lich Caller (Lich Pact) | 20 damage and Plague 3: 5% max HP a turn. A carrier ending its turn within 2 m of allies passes it on with its turns left; twice at most from the first (status Amount counts) |
| Soul Link | Hexblade (Shadow Lance) | special `link`: the unit struck and the nearest ally of it within 5 m, Linked 4 turns (By = partner); half of each ability hit on one lands on the other within 8 m |
| Time Bomb | Arc Warlock (Thunder Slumber) | status `bomb` 2 holding 45 (the power): when it runs out, as the carrier's second turn begins, 45 to everyone within 4 m |
| Gravity Well | Time Mage (Slowga) | special `gravity`, circle 4.5 m: each enemy slid up to 2 m toward the aim (not bosses), Slow 1 |
| Life Tether | Warlock (Shadow Slumber) | Tethered 3: as each of its turns begins, 5% max HP to the caster while within 8 m; snaps otherwise |
| Echo | Bard (Song of Haste) | Echo 2 on an ally: its next damage or heal ability resolves again at half power (`EchoScale`) |
| Retribution | Paladin (Aegis) | Retribution 2 on an ally: the first enemy whose ability hurts it is Stunned |
| Purge Transfer | Exorcist (Holy Slumber) | special `transfer`: an ally's harmful statuses (not Charmed) onto the nearest enemy within 6 m of it |
| Overcharge | War Drummer (Flame Anthem) | Turn Gauge +100% on an ally, then Slow 2 |
| Blood Pact | Dread Knight (Shadow Ward) | special `pact`: 15% of the user's health (never the last) clears an ally's cooldowns |
| Chain Mend | Tide Cleric (Mend) | special `chain`: the heal leaps twice to the nearest unhealed ally within 3 m, x0.75 each leap |
| Undying | Holy Guardian (Bash) | Undying 1: Hurt never takes the last health |
| Spirit Swap | Sylvan Muse (Thorn Anthem) | special `spiritswap`: user and ally trade health shares |
| Reckoning | Berserker (Rage) | special `reckoning`: power x (1 + share of the user's health lost) |
| Death Mark | Shadow Assassin (Veil) | Death Mark 3: if it falls marked, the marker's side gets 30% Turn Gauge |

## Mobility

| Spell | Class (swapped out) | Rule |
|---|---|---|
| Dash | Wind Dancer (Gale Veil) | special `dash`: up to 6 m straight at the aim, half a metre a step, through allies, stopping short of enemies, rock, cliffs; an action, not a walk, so zones of control don't hold it |
| Grapple | Sky Lancer (Take Wing) | `leap` aimed at an ally: lands within 1.5 m of it |
| Rally Call | War Marshal (Shield Slam) | special `rally`: the ally lands beside the user |
| Lure | Siren (Tide Ballad) | `swap` aimed at an enemy |
| Hook | Tide Brawler (Tide Stance) | special `hook`: a light hit, the enemy lands beside the user (not bosses) |
| Shove | Stone Fist (Stone Stance) | special `shove`: slid 4 m straight back; stopped more than 0.6 m short, Stunned |
| Vault | Gale Dancer (Gale Stance) | special `vault`: to the far side of the enemy aimed at, the blow from there |
| Charge | Crusader (Holy Lance) | special `charge`: to the near side of the enemy, damage x (1 + 0.05 per metre past 1.5) from where it began |
| Disengage | Steel Ranger (Barb) | special `disengage`: 4 m straight away from the nearest enemy |
| Fair Winds | Aeromancer (Tailwind) | allies within 3 m, Move +4 for 2 of their turns |
| Rift Gate | Summoner (Carbuncle) | zone with `portal`: a mouth at the caster's feet and one at the aim, 2 turns; its side's units ending a walk within 1 m of one come out of the other |
| Recall | Chrono Sage (Quicken) | special `recall` with a zone of 3 turns: first use marks the spot; used again, back to it, mark spent |
| Ice Slide | Frost Stalker (Frost Veil) | a zone line for allies: Stride 1 to whoever starts a turn or stops on it |
| Riptide | Leviathan Caller (Leviathan Pact) | special `riptide`: a line hit, then places traded with the furthest enemy struck (not bosses) |
| Shadow Hop | Night Hunter (Shadow Barb) | special `shadowhop`: to a free spot in tall grass or own smoke within 10 m, not spotted |

## The computer

Damage, healing and statuses it values as always (a harmful status it would lay on its own side now counts
against it). `SpellWorth` adds rough worths for Disengage, Purge Transfer, Blood Pact, Spirit Swap and Rally Call.
It doesn't use Dash, Grapple, Lure, Recall, Rift Gate or Shadow Hop: they need a plan it doesn't make.
`SpecialProblem` (no landing, no cover...) is asked by both Validate and the computer.

## Looks

Made in the class creator's Cast Studio (data/caststudio.json revision 119, published to
Content/Data/CastStudio/AbilityLooks.json). Every one replaces today's look (strip `all`), so the effect a class
file inherited from the ability it replaced no longer plays. Effects come from the effect filming's catalogue, sized
by what each one shows of its film (most fill only part of the frame they were filmed in); sounds from the sound
catalogue (the magic, whoosh, horn and small kits). Where they play:

- Cast and swing: on the hand (a glow, sparks, notes), or on the ground where a mover leaves.
- Flight: what flies for a shot or a reach (a poison cloud, a soul ring, a skull, a chain, hearts); something flies
  even for Echo and Blood Pact, which throw nothing.
- Landing: on each unit struck or given a status (`impact`), and once at the spot aimed (`area`): the cleansed ally
  of Purge Transfer, the bitten point of Grapple, where Lure and Shadow Hop come out.
- Statuses: lasting on whoever wears one (the plague cloud, the soul ring, the ticking core, golden wings, the skull
  over a head), a puff on each tick, and an end (Time Bomb's blast and jolt is its end).
- The user's own moves (Dash, Grapple, Vault, Charge, Disengage, Recall, Shadow Hop): a trail on the swing that
  follows the body as it goes, and dust or a flash on the release where it lands. A unit moved by someone else's
  spell is not touched by the blow (Teleported is not a consequence, GatherBlows), so nothing plays on it there.

## Not done

- Moves of other units (hook, shove, pull, rally, gates, riptide) snap; only the user's own moves are animated.
- Nothing plays on a unit another's spell moved (Lure's enemy, Rally's ally, Spirit Swap needs none): Teleported
  closes the blow and is shown at once, before the blow lands.
- The class creator's library still holds the old abilities (it accepts the new ones now: tmclass.mjs, vocab.mjs).
