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

## The 2026-10-03 tuning pass

The class lab measured all 101 class files (80 games each, all 9 maps). Only 36 were in the 40-60% band. The damage
classes' median was 30% (the Black Mage is a strong yardstick), the specials' 68%, supports 49%, tanks 53%. A
search then found, per class outside the band, the smallest change that brought it near 45% (weak) or 55% (strong):
first the class creator's stat-budget suggestion where the budget agreed with the lab, then every damage and heal
ability's power times k with HP moved by half as much. Classes that did not answer to power got an effect changed
instead (status turns, cooldowns, cast times). The full proposal and its numbers: the spreadsheet
`class-tuning-before-after.xlsx` (kept outside the repo).

The research report "Balancing tactical games" then showed why only part of it should ship. At 80 games a class
that truly wins 50% shows anywhere from about 39% to 61%, wider than the band, and the search was checked on the
seeds it was tuned on. So only these went in ("implement now"):

- Classes that started at 30% or below, or 70% or above (clearly outside chance), with their full change, unless
  the change was a power increase of 50% or more (Samurai, Stone Brawler, Stormcaller, Thunder Fist, Herbalist): held.
- The stat-budget correction alone for classes nearer the band, and for Herbalist.
- Frost Witch, Oracle and Sea Witch: the shorter slow on their ultimates only; the 20-25% power cut is held.

Every change was then measured again at 200 games (120 of them on seeds the tuning never saw), old file and new.
Three were dropped on that evidence: Leviathan Caller (Speed 6->8 took it from 38% to 74%), Windblade (Speed
12->10 took it from 60% to 70%) and Flame Sorcerer (HP 64->67 changed nothing). 35 class files changed; protocol 18.

| Class | Role | Old (200 games) | New (200 games) | Change |
|---|---|---|---|---|
| Golem Master | damage | 6% | 51% | HP 75->90, Speed 6->9, Move 5->6, power +40%, Summon Golem cooldown 3->2 |
| Lich Caller | damage | 12% | 47% | HP 67->81, Speed 6->8, power +42% |
| Treant Caller | damage | 12% | 50% | HP 73->86, Speed 6->8, power +36% |
| Sky Lancer | damage | 13% | 43% | HP 70->87, power +48% |
| Stone Fist | damage | 13% | 40% | HP 90->109, power +42% |
| Summoner | damage | 14% | 44% | HP 63->68, Speed 6->8, power +17% |
| Snare Hunter | damage | 16% | 45% | HP 78->88, power +25% |
| Mirage Dancer | damage | 18% | 45% | HP 72->90, power +49% |
| Herbalist | support | 19% | 32% | Speed 8->10 |
| Gale Dancer | damage | 20% | 46% | HP 75->91, power +42% |
| Salamander Caller | damage | 21% | 53% | HP 67->77, Speed 6->8, power +31% |
| Seraph Caller | damage | 21% | 40% | Speed 6->8 |
| Shade Brawler | damage | 21% | 43% | HP 82->100, power +45% |
| Thorn Summoner | damage | 21% | 43% | HP 68->73, Speed 7->8, power +14% |
| Thunderbird Caller | damage | 21% | 43% | HP 60->68, Crit 10->12, power +25% |
| Geomancer | damage | 22% | 48% | HP 78->90, Speed 8->9, power +30%, Quake cast 2->1 s, Stone Skin cast 1->0 s, Tectonic Rift cast 3->2 s |
| Cryomancer | damage | 23% | 49% | Speed 6->8 |
| Berserker | damage | 26% | 48% | HP 115->136, Evasion 8->12, power +36% |
| Roc Caller | damage | 26% | 52% | HP 60->68, power +28% |
| Temple Fist | damage | 26% | 47% | HP 84->97, power +31% |
| Yeti Caller | damage | 27% | 56% | Speed 4->6 |
| Ember Pugilist | damage | 29% | 43% | HP 82->93, power +26% |
| Thorn Brawler | damage | 33% | 47% | HP 88->101, power +31% |
| Dragoon | damage | 35% | 52% | HP 95->107, power +25% |
| Dawn Aegis | support | 37% | 47% | Speed 8->10 |
| Thunder Herald | support | 60% | 46% | Speed 14->12 |
| Siren | support | 67% | 57% | HP 74->70, power -11% |
| Sea Witch | special | 68% | 62% | Tide Doom slow 2->1 turns |
| Blazeblade | tank | 70% | 54% | HP 90->78, power -26% |
| Hexblade | tank | 70% | 68% | Evasion 13->10 |
| Time Mage | special | 72% | 62% | HP 58->51, Speed 16->14, power -24% |
| Oracle | special | 77% | 63% | Divination slow 3->1 turns |
| Frost Hexer | special | 86% | 70% | HP 72->63, power -26% |
| Rime Warden | special | 87% | 60% | HP 88->79, power -21% |
| Frost Witch | special | 95% | 90% | Frost Doom slow 2->1 turns |

What the numbers say:

- Speed is the strongest lever by far, more than power. A summoner's +2 Speed is a third more turns, so a third
  more pets; the Leviathan Caller went from weak to dominant on +2 alone. Price Speed as a multiplier, not a stat.
- Crowd-control length is much of the specials' strength: shortening only the slow on Divination, Tide Doom and
  Frost Doom (power untouched) brought Oracle, Sea Witch and Frost Witch down 14, 6 and 5 points.
- Still out of the band, left for the restructured lab: Frost Witch 90%, Frost Hexer 70%, Hexblade 68%, Time
  Mage, Oracle and Sea Witch 62-63% (strong); Herbalist 32% (weak). Held, not changed: Samurai, Stone Brawler,
  Stormcaller, Thunder Fist (weak, need +50% power or more: first check the computer plays them well) and the
  classes that were within chance of the band.

Next: the lab restructure the report recommends (several reference teams, fresh seeds for every check, a
role round-robin with ratings, a stronger search computer, ability-use logging), and the White Mage and Black
Mage as yardsticks.

## The lab (2026-10-04)

The restructure the research report recommended ("Balancing tactical games": one yardstick and one set of seeds made
the verdicts partly luck and partly the yardstick's). `Tools/ClassLab/ClassLab.cpp`; with none of the new options
every command gives exactly what it gave before (checked: a playtest's old fields are identical).

- **Several reference teams** (`--teams standard`, with `--classes Content/Data/Classes`): the classic Black Mage,
  Knight, Archer, White Mage, and three made of classes the tuning pass measured mid-role (Dragoon, Paladin, Warlock,
  Tide Cleric; Geomancer, Reef Guardian, Tempest Hexer, Cantor; Cryomancer, Earthshaker, Exorcist, Piper). Each pair
  of battles plays the next team and the next map; with four teams and nine maps the pairings cycle through 36.
- **Fresh seeds** (`--seed-base N`): battles no earlier run played, so a check is not graded on the battles it was
  tuned on.
- **The numbers that say how far to trust it**: a playtest gives `wins` and a 95% Wilson interval (`ci`). At 80
  battles a class that truly wins 50% can show 39-61%.
- **Ability use**: `perGame` (uses a battle per slot) and `unused` (actives the computer chose less than once in ten
  battles). `--ban <slot>` makes one ability inert, to see what the class is worth without it.
- **The search player** (`--skill search`, `--search candidates,seconds`, default 8,15): the class under test tries
  several whole turns on copies of the battle (the hard computer's own, and others from the medium and easy computers'
  next-best options), plays each on for 15 seconds with the hard computer everywhere, and keeps the best. It uses
  nothing hidden. Everyone else plays at hard. Better play lifts nearly every class (about +17 points on in-band
  classes), so a class is under-played when it gains well beyond that. First readings at 80 battles: Thunder Fist
  7% -> 23% (the hard computer under-plays it, though it is weak either way), Warlock 58% -> 74%, Herbalist 35% ->
  33% (genuinely weak, not misplayed).
- **Role round-robin** (`rating <classes dir> <role> [games a pair] [--sample K]`): every class of a role against
  every other (or K others), in the same seat of the reference teams; Bradley-Terry ratings (Hunter's MM, one win
  and one loss against an average opponent each so none runs off), shown Elo-style with standard errors (0 = the
  role's average; +100 wins about 64% against it). The supports, 6 battles a pair: Chrono Sage +161, ...,
  Chemist -149, standard error about 35. Herbalist rates average (51%) head to head although it wins 32% in the
  classic team: the yardstick, not the class.
- **The pass** (`Tools/ClassLab/balance.py`, `Balance.bat`): screen every class (80 battles, standard teams); shrink
  each rate toward its role's average by as much as the noise says (empirical Bayes); flag those at least 80% likely
  outside 40-60%; confirm them on fresh seeds 80 battles at a time until the 95% interval settles it (three rounds
  at most); with `--search` play the confirmed weak ones with the search player against six in-band controls; with
  `--ratings` each role's round-robin. Writes `Saved/Balance/balance.md`, `.csv`, `.json`. It changes no class file.

First pass (2026-10-04, the files after the tuning pass; 80 battles a class on the standard teams, search and
ratings on; Saved/Balance/balance.md): 23 of 101 flagged, then on fresh seeds 4 confirmed weak, 16 strong, 3 unclear.

- Weak: Stormcaller 14%, Stone Brawler 22%, Thunder Fist 24% (gains +19 with the search player against +8 for
  in-band classes: under-played as well as under-powered), Lumimancer 28% (gains +2: genuinely weak).
- Strong: mostly damage classes (Frost Stalker 84%, Tide Brawler 77%, Frost Brawler 75%, ...), plus Frostblade,
  Time Mage, Null Monk and Frost Witch. The damage classes look strong partly because the new teams' damage members
  (Dragoon, Geomancer, Cryomancer) are a softer bar than the Black Mage: the yardstick still matters. The round-robin
  is the yardstick-free check, and agrees with the screen (correlation 0.67-0.84 by role): Ember Pugilist, Frost
  Stalker, Frost Ranger and Treant Caller top the damage role (+186 to +210), Stone Brawler, Thunder Fist and the
  Archer the bottom; Null Monk and Time Mage top the specials, Tempest Hexer and Dust Hexer the bottom; Tide Cleric
  and Chemist the supports' bottom.
- 15 classes have an ability the computer almost never uses, most of them a caster's third slot (a weak melee or
  staff strike) or a hexer's curse.

Next pass (not done yet): tune toward the ratings (role average = 0) rather than one yardstick; look first at the
never-used abilities and the under-played Thunder Fist, then the confirmed outliers.

Not done here: the class creator's Balance tab still shows the classic tournament; it can read the new fields when it
is next worked on.

## Casters' signatures and hexers' status curses (2026-10-04)

The first balance pass found 15 abilities the computer almost never picked. Two families were most of them: six
casters' staff strike (a weaker Bolt, power 25 on Armor against the Bolt's 47 on magic, so never the better choice)
and five hexers' curse (the computer prices any stat curse at a flat 8 points, a Hex hit at about 37). Option C of
both, from the "Staff and Curse Alternatives" canvas, chosen by the human:

| Class | Was | Now | What it does |
|---|---|---|---|
| Aeromancer | Gale Staff | Tailwind | Haste on every ally within 3 m (self too), 1 turn; every 4 turns, 1 s |
| Cryomancer | Frost Staff | Frost Armor | Protect on every ally within 3 m (self too), 1 turn; every 4 turns, 1 s |
| Druid | Thorn Staff | Entangle | Root, 1 turn, every enemy within 2 m of a point 2-7 m away; power 30; every 3 turns, 1 s |
| Flame Sorcerer | Staff | Kindling | Oiled, 2 turns, an enemy 2-6 m away; power 30; every 3 turns |
| Necromancer | Shadow Staff | Wither | Wounded, 2 turns, an enemy 2-7 m away; power 28; every 3 turns |
| Stormcaller | Thunder Staff | Downpour | Wet, 2 turns, every enemy within 2 m of a point 2-7 m away; power 28; every 3 turns, 1 s |
| Ash Witch | Flame Curse | Pitch Curse | Oiled, 2 turns; power 10; 2-8 m; every 3 turns |
| Sea Witch | Tide Curse | Drown Curse | Wet, 2 turns; power 10 |
| Exorcist | Holy Curse | Banish | Silence, 1 turn; power 15 |
| Warlock | Shadow Curse | Rot Curse | Decay, 2 turns; power 10 (and Shadow Hex 37 to 33) |
| Oracle | Curse | Foretell | Marked, 1 turn; power 10 (and Hex 32 to 28) |

Made with the class creator's own modules (E:\TacticsClassCreator): the descriptions checked by its
describe.mjs, the icons its designs (icons.mjs, kept in creator.icons) drawn at 128 px, and each ability's look and
sounds as Cast Studio events (castlooks.mjs; data/caststudio.json, published to Content/Data/CastStudio/
AbilityLooks.json): a cast sound, an effect and a tinted light on the hit or the ground, a sound as it lands, and an
effect that stays on the unit while its status lasts. Sounds are FreeModularMagicSFX; effects from the filmed
catalogue (Saved/VfxCatalog) chosen for being visible on their own.

Measured (class lab, standard teams, 160 battles on fresh seeds, old file and new): every new ability is used, 1-2
times a battle (the old ones 0.01-0.09), no illegal order. Win rates: Aeromancer 52 to 52, Ash Witch 62 to 67,
Cryomancer 64 to 60, Druid 49 to 48, Exorcist 54 to 55, Flame Sorcerer 44 to 40, Necromancer 51 to 44, Oracle 59
to 54, Sea Witch 66 to 66, Stormcaller 17 to 16, Warlock 55 to 62 (95% intervals about +-8). The first versions
moved more (Tailwind and Frost Armor at 2 turns every 3 made Aeromancer 79%; the casters' light hits at power 15-20
cost them their Bolts' turns), so the numbers above are the third round. Worth watching: Warlock (+7), Necromancer
(-7); Stormcaller is still the weakest damage class.

## The held buffs and nerfs (2026-10-04)

The tuning pass held back its biggest changes: +55-70% power for five weak classes, and a 20-25% cut to the
ultimates of Frost Witch, Sea Witch and Oracle. Asked to put them in, they went in by steps instead of all at once
(about half the proposed buff, measured, then more), each step measured with the restructured lab (standard teams,
hard computer) on seeds 7 (160 battles), and the result confirmed on seeds 11 (200 battles) that the tuning never saw.

| Class | Before (seeds 7) | After (seeds 11) | Change |
|---|---|---|---|
| Thunder Fist | 23% | 49% | HP 75->92; Thunder Jab 32->45, Uppercut 44->62, Fury 49->69 |
| Stone Brawler | 18% | 49% | HP 100->120; Jab 32->45, Uppercut 44->62, Fury 49->69 |
| Stormcaller | 16% | 44% | HP 57->74; Thunder Bolt 47->70, Chain Lightning 36->54, Tempest 40->60 |
| Samurai | 38% | 48% | HP 85->95; Iaido Slash 37->43, Draw Out 32->37, Masamune 26->30 |
| Frost Witch | 83% (seeds 11) | 68% | Speed 8->7; Shatter 58->50; Frost Doom 54->43 |
| Sea Witch | 64% (seeds 11) | 61-65% | Tide Doom 46->39 |
| Oracle | 59% | 55% | Divination 27->22 |

About 40% of the proposed buff was enough: the proposal was sized against the old single team, and the standard
teams already lifted these classes (Thunder Fist 7% there, 23% here). Stormcaller's Speed 10->11 was not needed.
Herbalist, the fifth held buff, is 55% against the standard teams and was left alone.

The nerfs did less. Frost Witch's power cuts barely moved her (83 to 71-73% for -20% on Frost Doom, Frost Hex and
Shatter, even with her HP or magic defence cut); Speed 8->7 was the lever that worked. The ice element's Chilled
(three ice hits Freeze) was suspected and ruled out: an everyday Hex with no element measured the same. Sea Witch
did not answer to power or Speed (9 gave 62% and cost her Tide Slumber's casts). Both are still above the band,
like the other witches and hexers (58-67%); the next step is to check them against the role ratings, since all of
them stand in for the Archer, and a weak yardstick would make every one look strong.

All new files: every rules test passes, no baseline moved, no illegal order in any battle.

