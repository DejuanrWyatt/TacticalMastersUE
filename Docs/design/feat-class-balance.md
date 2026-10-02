# Class balance

2026-10-01. From the class analysis and the "Class Rebalance Mockups" canvas. The human chose: retune the Knight and
the Archer (option A), zone of control for tanks, and the data and computer fixes.

## How classes are measured

The class lab (`Tools/ClassLab`) on the game's rules (`TMSim::GameTuning`, `TMSim::ApplyGameBalance`), hard computer:
a class stands in for the reference-team member with its first role (tank: Knight, support: White Mage, special:
Archer, damage: Black Mage) against the reference team. A class as good at its job as the one it replaces wins half.
The band to aim for is 40-60%. `TMClassLab tournament` measures every class; the class creator shows it.

## The yardstick (option A)

The built-ins are written in code and checked against Godot, so the game's changes are made by
`TMSim::ApplyGameBalance()`, which the game and the lab call and the Godot tests do not.

| | Before | After |
|---|---|---|
| Knight | Speed 6, HP 105, MagDef 6; Shield Bash stuns; Guard buffs itself | Speed 8, HP 115, MagDef 9; Shield Bash taunts 2 turns; Guard puts Guarded on an ally within 4 m |
| Archer | Speed 12, Sight 13, Crit 15; Bow Shot 10 m; Aimed Shot every 2 | Speed 10, Sight 11, Crit 10; Bow Shot 8 m; Aimed Shot every 3 |

Measured (20 games a class, the Highlands), before and after:

| Stands in for | Before | After |
|---|---|---|
| Knight (tanks) | 77% | 46% |
| Archer (specials) | 23% | 54% |
| White Mage (supports) | 62% | 18% |
| Black Mage (damage) | 47% | 30% |

Tanks and specials are now fair against their yardstick. Supports and damage dealers fell: with a slower Archer and a
sturdier Knight fights last longer, which favours the White Mage's healing and the Black Mage's area spells. Zone of
control is not the cause (without it: 49 / 54 / 19 / 32). The White Mage and Black Mage are next, before the auto-tuner.

## Zone of control

`FTuning::ZoneOfControl` (rule key `zone_of_control`; 0 in Godot's rules, 1 in the game's). A walk that comes within
`EngageRadius` (1.8 m) of an enemy whose first role is tank ends there (`FBattle::RunDijkstra`). A tank the walk starts
beside does not hold it, so a unit can walk away (paying to break off as usual). A waypoint inside a zone is refused
(`WalkVia`), or a walk could go on past where it had to stop. The walk area simply ends at the zone. Protocol 14.

## Data and computer fixes

- The computer aimed an area ability between two targets even when the midpoint was in the fog; the rules refused it
  ("You can't see that spot"). Now only seen ground, or ground the caster will see from where it stands (SimAI).
- Berserker's Rage and Samurai's Meditate described an AttPwr stat that no longer exists.
- Frost Hexer and Frost Witch shared every stat: the Hexer now controls (Chill on its bolt and ultimate, Marked with
  its curse), the Witch hits hard (HP 66, Crit 16, M-Eva 14; a 58-power Shatter in place of the sleep).
- Class files changed on purpose carry `creator.rebalanced` (`{on, why}`); `SimClassTest` compares the others with
  Godot and lists these.
- The 33 abilities without a particle effect are always-on auras and passives (nothing goes off to play one) and
  eleven stance toggles; the toggles' effects are a choice for the creator's Effects tab.
