# Picks from Cire's Spell Codex

Written 2026-10-05 from "Cire's Team Survival: Spell Codex" (607 abilities, 152 live and 455 cut). This doc is
a shortlist, not a plan. Each pick is translated into this game's class-file format, given to a class that has room
for it, and sorted by the work it needs.

**Built (2026-10-06, not yet in a release; online protocol 27):** A1-A9, and from B the four the suggested order
names first -- B8 `warned` (Faultline), B4 `ricochet` (Shield Toss), B9 `execute` (Verdict) and B13 `crowd` (Echo
Slam). Thirteen class files, each with the ability as written below, in place of the one named. The rules are in
`SimSpells.cpp` (ricochet), `SimZones.cpp` (warned: a zone that touches nobody, drawn with hazard stripes, landing as
its caster's next turn begins), `SimBattle.cpp` (execute), `SimTargeting.cpp` (crowd) and `SimResolve.cpp`; tested
by `Tests/SimCodexTest.cpp`. Each has its own Cast Studio look with a signature effect no other ability uses
(Brambles for Thornweave, a gold shield for Shield Toss, owl eyes for Owl Scout, standing stones for Stone Henge...)
and its own icon. A10 (Blood Pool) waits: it needs a choice -- in place of Leap Smash, or for a new class.

**How the codex maps onto this game**

| Codex | Here |
|---|---|
| damage at level 1 | `power`: x0.45 for a single target; area spells come out near the existing medians (unit ~30, circle ~50, line ~38, cone ~40) |
| cooldown in seconds | `cooldown` in turns: 6-9 s is 2, 10-14 s is 3, longer is 4 |
| cast time in seconds | `cast` unchanged |
| range and radius in metres | `max_range` and `aoe` unchanged (both games use metres) |
| duration in seconds | status `turns`: 2-3 s is 1 turn, 4-6 s is 2, 8 s and up is 3 |
| "warned" / "after 0.6s" | a telegraph: drawn on the ground now, lands at the start of the caster's next turn (B8) |
| ultimate | slot 3 (cooldown 0, paid with the gauge) |

**Left out:** dodge rolls, i-frames, attack speed, basic-attack buffs, mana and energy, gold and bounties. They
belong to a real-time action game and have nothing to hook onto here. Also left out:
- the ~200 plain "deal X damage, maybe slow or stun" entries, which the roster already covers;
- anything that repeats a spell the game already has (Gravity Well, Hook, Shove, Time Bomb, Chain Mend, Life
  Tether, Echo, Contagion and the rest from feat-new-spells.md);
- the flat passives (+8% damage, +10% move and so on).

Every replacement below is a slot 1 or slot 2 ability that is bland or repeated across classes (a Stance, a Ward,
a Ballad, a Pact, a Curse with no status, an Edge). Slot 0, the basic attack, is never touched.

---

## A. Works today: class-file changes only

Ten picks. Each can go straight into a class file, through the class creator, with no code change.

| # | Codex pick | Class | Replaces | What it does here |
|---|---|---|---|---|
| A1 | Blight Sigil (poison) | Dust Hexer | Stone Curse (no status) | 2.5 m circle at range: damage and Wounded (healing halved) |
| A2 | Crimson Crystals (blood) | Hexblade | Shadow Edge | crystals erupt under a group: damage and Bleed |
| A3 | Storm Slash (storm) | Thunder Fist | Thunder Stance | cone in front: damage and Silence |
| A4 | Night Spear (shadow) | Night Hunter | Shadow Snipe | long line: damage and Silence |
| A5 | Thornweave (nature) | Sylvan Muse | Thorn Ballad | a lasting line of thorns: hurts and Slows whoever stands on it |
| A6 | Owl Scout (nature) | Roc Caller | Roc Pact | send the owl: sight and reveal round a point, Marked on enemies found |
| A7 | Beacon of Return (holy) | Cantor | Holy Ballad | a holy beacon: allies in it Regen and get Stride |
| A8 | Frost Pirouette (cold) | Frost Brawler | Frost Stance | spin: damage and Slow all round |
| A9 | Stone Henge (earth) | Mountain Sentinel | Stone Ward | stones slam up all round: damage and Taunt |
| A10 | Blood Pool (blood) | Berserker | (alternative to Leap Smash) | a lasting pool: hurts and Wounds enemies in it |

```json
{"id": "dust_hexer_blight_sigil", "name": "Blight Sigil", "kind": "active", "effect": "damage", "scale": "mag",
 "target": "enemy", "shape": "circle", "power": 40, "min_range": 0, "max_range": 8, "aoe": 2.5, "cooldown": 3,
 "cast": 0, "status": {"id": "wounded", "turns": 3},
 "desc": "A blight sigil 2.5 m wide up to 8 m away: damage, and those caught heal half as much for 3 turns."}

{"id": "hexblade_crimson_crystals", "name": "Crimson Crystals", "kind": "active", "effect": "damage", "scale": "mag",
 "target": "enemy", "shape": "circle", "power": 42, "min_range": 0, "max_range": 6, "aoe": 2, "cooldown": 2,
 "cast": 0, "status": {"id": "bleed", "turns": 2},
 "desc": "Blood crystals burst from the ground 2 m wide up to 6 m away: damage and Bleed for 2 turns."}

{"id": "thunder_fist_storm_slash", "name": "Storm Slash", "kind": "active", "effect": "damage", "scale": "att",
 "target": "enemy", "shape": "cone", "angle": 90, "power": 34, "min_range": 0, "max_range": 3, "aoe": 0,
 "cooldown": 3, "cast": 0, "status": {"id": "silence", "turns": 1}, "element": "lightning",
 "desc": "A crackling slash in a 3 m cone: damage, and those hit are Silenced for a turn."}

{"id": "night_hunter_night_spear", "name": "Night Spear", "kind": "active", "effect": "damage", "scale": "att",
 "target": "enemy", "shape": "line", "power": 34, "min_range": 0, "max_range": 12, "aoe": 0.6, "cooldown": 2,
 "cast": 0, "status": {"id": "silence", "turns": 1},
 "desc": "A narrow spear of night down a 12 m line: damage and Silence for a turn to everyone on it."}

{"id": "sylvan_muse_thornweave", "name": "Thornweave", "kind": "active", "effect": "support", "scale": "mag",
 "target": "enemy", "shape": "line", "power": 0, "min_range": 1, "max_range": 9, "aoe": 0.9, "cooldown": 3,
 "cast": 0, "special": "zone", "zone": {"turns": 3, "percent": 6}, "status": {"id": "slow", "turns": 1},
 "desc": "Weave a 9 m line of thorns for 3 turns: an enemy that starts or stops on it loses 6% of its health and is Slowed."}

{"id": "roc_caller_owl_scout", "name": "Owl Scout", "kind": "active", "effect": "support", "scale": "mag",
 "target": "enemy", "shape": "circle", "power": 0, "min_range": 0, "max_range": 12, "aoe": 4.5, "cooldown": 3,
 "cast": 0, "special": "zone", "zone": {"turns": 2, "sight": 4.5, "reveal": true, "once": true},
 "status": {"id": "marked", "turns": 2},
 "desc": "Send the owl up to 12 m, even into the fog: your side sees 4.5 m round it for 2 turns, units hiding there are found, and enemies in it are Marked."}

{"id": "cantor_beacon_of_return", "name": "Beacon of Return", "kind": "active", "effect": "support", "scale": "mag",
 "target": "ally", "shape": "circle", "power": 0, "min_range": 0, "max_range": 6, "aoe": 2, "cooldown": 3,
 "cast": 0, "special": "zone", "zone": {"turns": 3, "also": {"id": "stride", "turns": 1}},
 "status": {"id": "regen", "turns": 1},
 "desc": "Raise a beacon 4 m wide for 3 turns: allies who start or stop in it Regenerate and walk further."}

{"id": "frost_brawler_frost_pirouette", "name": "Frost Pirouette", "kind": "active", "effect": "damage",
 "scale": "att", "target": "enemy", "shape": "self", "power": 30, "min_range": 0, "max_range": 0, "aoe": 2.5,
 "cooldown": 2, "cast": 0, "status": {"id": "slow", "turns": 1}, "element": "ice",
 "desc": "Spin in a ring of frost: damage and Slow to every enemy within 2.5 m."}

{"id": "mountain_sentinel_stone_henge", "name": "Stone Henge", "kind": "active", "effect": "damage",
 "scale": "att", "target": "enemy", "shape": "self", "power": 26, "min_range": 0, "max_range": 0, "aoe": 3,
 "cooldown": 3, "cast": 0, "status": {"id": "taunt", "turns": 1},
 "desc": "Standing stones slam up around you: damage to enemies within 3 m, and they must come at you next turn."}

{"id": "berserker_blood_pool", "name": "Blood Pool", "kind": "active", "effect": "support", "scale": "att",
 "target": "enemy", "shape": "circle", "power": 0, "min_range": 0, "max_range": 5, "aoe": 2, "cooldown": 3,
 "cast": 0, "special": "zone", "zone": {"turns": 3, "percent": 8}, "status": {"id": "wounded", "turns": 1},
 "desc": "Spill a pool of blood 4 m wide for 3 turns: enemies in it lose 8% of their health a turn and heal half as much."}
```

A10 would make the Berserker's ability list one too long. Use it in place of Leap Smash if he should hold ground
rather than jump in, or give it to a new class.

---

## B. One new rule each

Each of these needs one new `special` (in `SimSpells.cpp`, the same way as the thirty in feat-new-spells.md) or one
new status (a `SimTypes.cpp` row). The class-file side is ready below.

| # | Codex pick | New rule | Class | Replaces |
|---|---|---|---|---|
| B1 | Merciful Censer | `censer`: the blow heals allies within 4 m of the target for half the damage dealt | Templar | Holy Bash |
| B2 | Grove Javelin | `bloom`: where it hits, a 2.5 m healing zone for allies, 2 turns | Treant Caller | Treant Pact |
| B3 | Returning Axes | `boomerang`: a line that hits once going out and again coming back | Shadow Assassin | Kunai |
| B4 | Shield Toss / Bouncing Glaive | `ricochet`: the blow bounces to 2 more enemies within 5 m, 20% weaker each time; with a `status`, each one gets it | Bastion | Challenge |
| B5 | Relic Vow | status `vow`: a barrier, and 30% of what the ally takes goes to the caster instead | Holy Guardian | Ward |
| B6 | Banishment | status `banished`: out of play for 1 turn (can't act, move or be hurt), then returns for damage; bosses are Silenced and Slowed instead | Exorcist | Banish (today a Silence) |
| B7 | Polymorph | status `critter`: no abilities, half Move, any damage ends it; bosses immune | Tempest Hexer | Gale Curse (no status) |
| B8 | Faultline / Chronofield / Upheaval | `warned`: drawn on the ground now, lands at the start of the caster's next turn, so it hits harder | Earthshaker | Stone Lance |
| B9 | Executioner's Verdict / Collect the Bounty | `execute`: +25% of the target's missing health; a kill refills half the caster's gauge | Inquisitor | Holy Execution (slot 3) |
| B10 | Arcane Blunderbuss / Shield Bash | `interrupt`: a unit mid-cast loses its cast and takes 40% more | Storm Bulwark | Thunder Ward |
| B11 | Radiant Orb / Purge / Hexbane | `dispel`: strips every helpful status from each enemy hit | Lumimancer | Prism Ward |
| B12 | Seed Mend | `seed`: the heal lands at the start of the caster's next turn, 50% more if the ally is then below 40% | Herbalist | Thorn Mend |
| B13 | Echo Slam | `crowd`: 20% harder for each enemy caught beyond the first | Stone Brawler | Stance |
| B14 | Dawn Beam | target `both`: a line that heals allies and hurts enemies (Wounded) | Dawn Aegis | Dawnlight |
| B15 | Hallowed Cage | zone `cage`: enemies inside can't walk out while it lasts; allies pass | Crusader | Holy Edge |
| B16 | Wing Rebuke / Totem Sweep | `repel`: everyone hit is pushed 3 m straight away from the user (Shove for a cone) | Sky Warden | Gale Bash |

```json
{"id": "templar_merciful_censer", "name": "Merciful Censer", "kind": "active", "effect": "damage", "scale": "att",
 "target": "enemy", "shape": "unit", "power": 32, "max_range": 1.8, "cooldown": 2, "special": "censer",
 "desc": "Strike with the censer: damage, and allies within 4 m of the target heal half of it."}

{"id": "treant_caller_grove_javelin", "name": "Grove Javelin", "kind": "active", "effect": "damage", "scale": "att",
 "target": "enemy", "shape": "unit", "power": 36, "max_range": 9, "cooldown": 3, "special": "bloom",
 "zone": {"turns": 2}, "desc": "A root-tipped javelin: damage, and a healing bloom 2.5 m wide grows where it struck for 2 turns."}

{"id": "shadow_assassin_returning_kunai", "name": "Returning Blades", "kind": "active", "effect": "damage",
 "scale": "att", "target": "enemy", "shape": "line", "power": 24, "max_range": 10, "aoe": 0.7, "cooldown": 2,
 "special": "boomerang", "desc": "Hurl blades down a 10 m line; they come back: everyone on it is hit twice."}

{"id": "bastion_shield_toss", "name": "Shield Toss", "kind": "active", "effect": "damage", "scale": "att",
 "target": "enemy", "shape": "unit", "power": 24, "max_range": 6, "cooldown": 3, "special": "ricochet",
 "status": {"id": "taunt", "turns": 1},
 "desc": "Hurl your shield up to 6 m: it bounces to 2 more enemies within 5 m, and each must come at you."}

{"id": "holy_guardian_relic_vow", "name": "Relic Vow", "kind": "active", "effect": "support", "scale": "mag",
 "target": "ally", "shape": "unit", "power": 0, "max_range": 5, "cooldown": 3, "status": {"id": "vow", "turns": 3},
 "desc": "Bind an ally for 3 turns: a barrier, and 30% of every blow it takes is yours instead."}

{"id": "exorcist_banishment", "name": "Banishment", "kind": "active", "effect": "damage", "scale": "mag",
 "target": "enemy", "shape": "unit", "power": 36, "max_range": 8, "cooldown": 4,
 "status": {"id": "banished", "turns": 1},
 "desc": "Exile an enemy for a turn: it can't act, move or be hurt, then comes back for damage. Bosses are Silenced and Slowed instead."}

{"id": "tempest_hexer_featherform", "name": "Featherform", "kind": "active", "effect": "support", "scale": "mag",
 "target": "enemy", "shape": "unit", "power": 0, "max_range": 7, "cooldown": 4, "cast": 1.5,
 "status": {"id": "critter", "turns": 2},
 "desc": "Turn an enemy into a sparrow for 2 turns: no abilities, half its walk; any harm breaks it. Bosses are immune."}

{"id": "earthshaker_faultline", "name": "Faultline", "kind": "active", "effect": "damage", "scale": "att",
 "target": "enemy", "shape": "line", "power": 58, "max_range": 9, "aoe": 1.1, "cooldown": 3, "special": "warned",
 "status": {"id": "stun", "turns": 1},
 "desc": "Crack a 9 m faultline: it erupts at the start of your next turn, damaging and Stunning whoever is still on it."}

{"id": "inquisitor_verdict", "name": "Verdict", "kind": "active", "effect": "damage", "scale": "att",
 "target": "enemy", "shape": "unit", "power": 45, "max_range": 1.8, "cooldown": 0, "special": "execute",
 "desc": "Damage plus a quarter of the health the target is missing; a kill gives back half your gauge."}

{"id": "storm_bulwark_shield_bash", "name": "Silencing Bash", "kind": "active", "effect": "damage", "scale": "att",
 "target": "enemy", "shape": "unit", "power": 20, "max_range": 1.8, "cooldown": 2, "special": "interrupt",
 "status": {"id": "silence", "turns": 1},
 "desc": "Bash with your shield: a cast in progress is lost and the blow lands 40% harder; Silenced for a turn."}

{"id": "lumimancer_radiant_orb", "name": "Radiant Orb", "kind": "active", "effect": "damage", "scale": "mag",
 "target": "enemy", "shape": "line", "power": 28, "max_range": 9, "aoe": 0.6, "cooldown": 3, "special": "dispel",
 "desc": "A radiant orb down a 9 m line: damage, and every helpful status on those it passes is stripped."}

{"id": "herbalist_seed_mend", "name": "Seed Mend", "kind": "active", "effect": "heal", "scale": "mag",
 "target": "ally", "shape": "unit", "power": 34, "max_range": 6, "cooldown": 1, "special": "seed",
 "desc": "Plant a healing seed on an ally: it blooms at the start of your next turn, half again as strong if they are below 40%."}

{"id": "stone_brawler_echo_slam", "name": "Echo Slam", "kind": "active", "effect": "damage", "scale": "att",
 "target": "enemy", "shape": "self", "power": 30, "aoe": 3, "cooldown": 3, "special": "crowd",
 "status": {"id": "stun", "turns": 1},
 "desc": "Slam the ground: damage to enemies within 3 m, 20% more for each one caught beyond the first, and a short Stun."}

{"id": "dawn_aegis_dawn_beam", "name": "Dawn Beam", "kind": "active", "effect": "heal", "scale": "mag",
 "target": "both", "shape": "line", "power": 40, "max_range": 10, "aoe": 1, "cooldown": 3, "cast": 1.2,
 "status": {"id": "wounded", "turns": 2},
 "desc": "A beam of dawn down a 10 m line: allies on it heal; enemies take 40% of it as damage and heal half as much."}

{"id": "crusader_hallowed_cage", "name": "Hallowed Cage", "kind": "active", "effect": "support", "scale": "mag",
 "target": "enemy", "shape": "circle", "power": 0, "max_range": 7, "aoe": 2, "cooldown": 4, "special": "zone",
 "zone": {"turns": 2, "percent": 5, "cage": true}, "status": {"id": "slow", "turns": 1},
 "desc": "A ring of holy blades 4 m wide for 2 turns: enemies inside can't walk out, lose 5% a turn and are Slowed."}

{"id": "sky_warden_wing_rebuke", "name": "Wing Rebuke", "kind": "active", "effect": "damage", "scale": "att",
 "target": "enemy", "shape": "cone", "angle": 120, "power": 18, "max_range": 4, "cooldown": 3, "special": "repel",
 "desc": "A sweep of wings in a 4 m cone: light damage, and everyone hit is thrown 3 m back."}
```

Notes for the build:
- **B8 `warned`** lands on whoever is in the area at the start of the caster's next turn, so a dodge is a choice.
  It is the best fit for this game in the codex: telegraphs are what turn-based tactics live on. Once it exists,
  the same rule carries Chronofield (Time Mage), Storm Call (Stormcaller), Upheaval (Geomancer) and Starfall
  (Oracle) for free.
- **B14 `both`** needs the targeting preview to paint allies and enemies differently along one line.
- **B6 `banished`** removes the unit from the board for a turn. It is the same idea as `stop`, without the
  ability to be hit.

---

## C. Bigger systems

Each needs real work, but each unlocks a family of codex spells.

| System | What it needs | Codex spells it unlocks | Suggested classes |
|---|---|---|---|
| **Walls** | a destructible object with health that blocks walking and shots (line of sight) | Glacier Wall, Shadow Palisade, Runestone Wall, Pavise, Totem Bulwark, Aegis Dome | Glacier Guard (for Frost Ward), Geomancer (for Stone Skin), Dread Knight (Shadow Palisade, for Shadow Bash) |
| **Constructs** | a pet with Move 0 and an aura or turret attack: today's `pet` special plus a "never moves" flag and an aura field on the monster file | Photon Turret, Frost Sentry, Siege Ballista, War Drum, Venom Totem, Warding Obelisk, Aegis/Haste/Gravity Pylons | War Drummer: War Drum (for Flame Ballad); Golem Master: Photon Turret (for Golem Pact); Chemist: Warding Obelisk (for Potion) |
| **Traps** | a zone that only its side can see, set off by the first enemy to enter, then gone (`hidden` and `trigger` on zones) | Frost Snare, Stasis Snare, Arc Mine, Spirit Lantern, Hex Lantern, Thunder Coil, Caltrop Mine | Snare Hunter: Frost Snare (for Hawk Eye); Ninja: Caltrops (for Smoke Bomb); Chemist: Arc Mine |
| **Ally links** | a status tied to two units that breaks past a distance | Spirit Tether (heal while near), Kindred Constellation (ultimate) | Sylvan Muse, Seraph Caller |

Traps and constructs fit the zone and pet code the game already has, so they are the cheapest of the four.

---

## D. Passives worth adapting

Most codex passives are flat stat bumps. These six do something a turn-based game can use:

| Codex passive | Here | Suggested class |
|---|---|---|
| Ancestral Weight | didn't move this turn: 15% less damage taken until it moves | Mountain Sentinel |
| Unbroken Clan | 5% less damage taken for each ally within 3 m (up to 3) | War Marshal |
| Last Light | an ally within 8 m that drops below 30% gets a Barrier (once per ally per battle) | Seraph Caller |
| Opportunist | 15% more damage to Stunned, Rooted or Slowed targets | Snare Hunter |
| Reaper's Instinct | 20% more damage to targets below 30% health | Shadow Assassin |
| Riposte | a blow it dodges (Evasion) is answered with a counter for half its attack | Samurai |

---

## Suggested order

1. **A1-A10:** data only. They can go in through the class creator, then a lab balance check.
2. **B8 `warned`**, then **B4 `ricochet`**, **B9 `execute`** and **B13 `crowd`**: small rules that reuse the
   blow code.
3. **Traps**, then **constructs**: new families built on the zone and pet code.
4. The rest of B, then walls.
