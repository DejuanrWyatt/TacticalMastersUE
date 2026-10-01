# Rules Spec: feat-neutral-camps

Status: Approved (2026-09-29 22:19, section 12; the bestiary, behaviours, bosses and item catalog approved
2026-09-30 07:20, section 14). Items built (step 1). 2026-09-30: camps, monsters, bosses, item abilities and Tamer's Collar written; built; the rules pass their tests (Tests/SimCampTest.cpp) and an online match with camps plays the same on both machines (Tests/OnlineTest.bat). Not yet looked at by a person in the game. Written for the human to
approve, change or cut; the choices this spec makes on its own are marked **(default)** and listed again
in section 12.
Mode: Feature
Backlog goal: Neutral monster camps of four tiers (easy, medium, hard, epic) appear at random, mirrored
spots around the map during a battle. Beating a camp leaves a loot cache holding an item of the camp's
tier. Any unit can carry up to three items, which add stats, raise ability damage, healing or Turn Gauge
speed, or give it a new ability (teleport, go unseen, fly, a burst of power). A battle with camps off is
bit for bit today's.

Builds on two Draft specs and replaces parts of them:
- `feat-objectives.md` 3.2: **guardians** (team 2, leash and reset, the dice-free `FNeutralPlayer`) are
  used here exactly as written; a camp is a guardian site with a loot reward instead of a capture ring.
  Its **reliquary** is superseded by the camps.
- `feat-items.md`: its **formulas and new mechanics** (flat-then-percent power, Blink, Veil, the extra
  ability slot) stand. Its **slots and how items are got** are replaced: three open slots instead of
  weapon/armour/trinket, and items come from camps rather than a points budget on the setup screen
  (the budget becomes an option, 12.3). Its catalog is replaced by the tiered one in 5.4.

## 1. Summary
Today every unit fights with what its class gave it. With camps on, the map fills with side quests: an
easy camp near each side's start that one unit can clear in a couple of turns for a small item; medium
and hard camps out on the flanks that take two or three units; and, later, one epic monster in the middle
that takes a whole side and drops the best items in the game. Each is a choice: go for the enemy now, or
go for the camp and come back stronger. And because the loot sits on the ground until someone picks it
up, a side that does the work can be robbed by one that times its arrival. Three item slots per unit mean
the same class can end two battles built two different ways.

## 2. What other games do, and what we take from them
| Game | Mechanic | What we take | What we leave |
|---|---|---|---|
| **League of Legends**, jungle camps | Fixed camps with timers; a large epic monster (Baron, Dragon) that needs a team. Items: flat and percent bonuses, "unique" effects that don't stack. | **Tiers by size**, epic at a contested spot, respawn timers shown to both sides; **caps** so stacked percents can't run away. | Gold and a shop; hundreds of items. |
| **Dota 2**, neutral camps and neutral items | Camps drop tiered neutral items; tiers unlock as the game goes on; an item can be passed around or left on the ground. | **Tiered drops, tiers unlocking over time**, items as objects on the ground that anyone may take. | Stashes and couriers. |
| **Dota 2**, Roshan | One boss, late, in the middle; the fight around it decides games. | The **epic camp**: one, centred, arrives late, best loot. | Aegis (an extra life). |
| **Heroes of the Storm**, mercenary camps | The side that beats a camp can be robbed of the reward by the side that arrives last. Camps leash and reset. | **Stealable loot** and **leash and reset** (from `feat-objectives`). | Mercenaries pushing lanes. |
| **Final Fantasy Tactics**, accessories | Small items that change how a unit moves (Move +1, Jump +1, Float). | **Movement items**: +Move, +Jump, flight. | Equipment tied to jobs. |

The lessons: a camp is only interesting if its **reward is worth the time**, its **risk is visible**
(what it is, how strong, when it's back), and **the other side can answer** (steal, contest, punish).

## 3. Rules, in the order they are applied
Camps are ticked inside `FBattle::Tick` after the planning check and before units' gauges, where
`TickCapture` runs today, so everything is ordered and reproducible.

### 3.1 Camps
1. A camp has a **tier** (easy, medium, hard, epic), a **spot** (a node centre, metres), a **state**
   (waiting, awake, cleared) and a **timer** (ticks until it next wakes).
2. **How many.** The setup screen's **Neutral camps** setting (a new `FTuning` field `CampLevel`, 0-3)
   picks a layout. Pairs are mirrored, one on each side's half; the epic camp is one, on the mirror point.

   | Setting | Easy | Medium | Hard | Epic |
   |---|---|---|---|---|
   | 0 Off **(default in the rules, so Godot battles are untouched)** | – | – | – | – |
   | 1 Light | 1 pair | 1 pair | – | – |
   | 2 Standard **(what setup offers the first time)** | 2 pairs | 1 pair | 1 pair | 1 |
   | 3 Wild | 3 pairs | 2 pairs | 1 pair | 1 |

3. **When they wake** (seconds of battle clock after planning, all Developer Tools sliders):

   | Tier | First wakes | Back after cleared | Warning before waking |
   |---|---|---|---|
   | Easy | 0 s | 60 s | 5 s |
   | Medium | 40 s | 90 s | 10 s |
   | Hard | 100 s | 150 s | 10 s |
   | Epic | 180 s | 300 s | 15 s |

   The warning is an event both sides get at once ("A hard camp stirs in the west, 10 s"). While waiting,
   the camp's spot shows a marker and a countdown to both sides (12.4 asks whether that should be hidden).
4. **Where (random, fair).** Spots come from the camps' own generator, `FSimRandom(Seed ^ Camp::Salt)`,
   exactly as watchtowers use theirs, so the battle's dice are never touched. For each pair, try spots
   drawn uniformly over walkable node centres on blue's half until one passes, and put the twin at the
   mirror point:
   - its **band** (below) by how far along the way from blue's start to red's it lies,
     `t = Dist(Blue) / (Dist(Blue) + Dist(Red))`: easy 0.20-0.40, medium 0.30-0.45, hard 0.40-0.48;
   - at least **8 m** from every other camp, watchtower and spawn point;
   - **room to fight**: the camp's own tile and its 8 neighbours walkable (not rock or water);
   - **reachable** on foot from both sides' spawns (the same walk check the map maker does).
   The epic camp takes the walkable node nearest the mirror point. 200 failed tries means that pair is
   left out and the log says so, rather than a camp in a bad place.
5. **Random each time.** When a cleared camp wakes again, it wakes at a **new** spot drawn the same way
   (its twin moves with it), so the map's points of interest move over a battle. Easy camps near a side's
   start stay in that side's band, so neither side is ever further from its easy camps.

### 3.2 Monsters
1. Monsters are **guardians** as `feat-objectives` 3.2 writes them: ordinary units on **team 2**, enemies
   of both sides, driven through ordinary orders by the dice-free `FNeutralPlayer`, with a home (the camp
   spot), a leash (8 m), and a reset (walk home, heal to full, lose statuses) when nobody is in reach.
2. They are made from class files with the new role `monster` and a `tier`, so the class creator makes
   and measures them like any class. A camp's roster is fixed by tier (5.3). Every camp's monsters are
   made **when the battle starts**, after both sides' units, and kept off the board until their camp
   wakes, so unit ids never depend on timing (the rule `feat-objectives` 4 explains).
3. Monsters don't count toward anything a side wins by (last side standing, the time limit's health
   share, holding the middle, capturing a watchtower).
4. A monster that falls is gone at once: no knock-out countdown, no revive.
5. **Fog:** monsters are units, so a side sees them only when it can see their spot. The camp's marker
   and countdown are always shown (3.1.3).

### 3.3 Loot
1. When a camp's last monster falls, a **loot cache** appears on the camp's spot holding items rolled
   from that tier's pool (5.4): one item for easy, medium and hard; **two** for epic (one epic, one hard).
2. **The roll.** The loot generator, `FSimRandom(Seed ^ Loot::Salt)`, its own and not the battle's,
   picks uniformly from the tier's pool. An item already on the board (carried, or in another cache) is
   skipped and the next draw taken, so the same item never exists twice in a battle **(default)**.
   Rolls happen in the order camps are cleared, ties by camp index, so both machines online roll the same.
3. **Anyone can take it.** A cache belongs to nobody. Any unit of either side standing within **1.5 m**
   of it may **Take** one item from it (3.4). Who beat the camp doesn't matter; the side that did the work
   has to hold the ground to collect **(default; 12.1)**.
4. A cache stays until empty. It is seen like a unit: only when a side can see its spot.

### 3.4 Carrying items
1. **Three slots per unit**, open: any item in any slot. A unit may not carry two of the same item.
2. **Picking up by walking (2026-09-30, the human's ask; online protocol 6).** A unit of either side whose
   move ends within reach of a cache takes, free, what fits its empty slots: best tier first, never a second
   of one it carries (`FBattle::PickUpAt`, run by every move, a flee included; monsters still take by order).
   A full unit takes nothing this way: swapping is still a Take.
   **Take** (a new order): the unit's **action** for the turn, like using an ability, so a unit may walk
   and take in one turn. It names the cache and the item. If the unit's slots are full, the order must
   also name a slot to empty: that item drops into a cache at the unit's feet, where anyone can take it.
   Refused, with the reason: not in reach; no such item; slots full and no slot named; already carrying
   one; stunned, asleep or otherwise unable to act.
3. **Drop** (a new order, free, doesn't use the action): puts an item from a slot into a cache at the
   unit's feet, for an ally to pick up. There is no handing items across the board.
4. **Knocked out:** a unit keeps its items. **Finished off** (its knock-out time runs out): its items
   drop into a cache where it fell **(default; 12.2)**. So carrying the best items makes a unit a target.
5. Cooldowns belong to the item: an item dropped mid-cooldown keeps it, so an ability can't be passed
   around to reset it.

### 3.5 What an item can do
Any mix of the following (the item file, 9.1, says which):
1. **Stats**, added to the class's own: max HP, AttDef, MagDef, A-Eva, M-Eva, Crit, Move ("movement"),
   Speed ("initiative"), Patience, Sight. May be negative on a trade-off item. Added before buffs and
   statuses, so Shred or Freeze scales them with the rest (`feat-items` 3.3).
2. **Ability damage %**: raises damage from its abilities by a percent (`feat-items` 3.4's `Pct`).
3. **Healing %**: raises healing from its abilities by a percent.
4. **Turn Gauge %**: its gauge fills that much faster, on top of what Speed gives (5.1).
5. **Jump**: levels of height it can climb in one step, on top of the usual 2.
6. **An ability**, used like a class ability, in a slot of its own after the class's four (slots 4, 5
   and 6, in item-slot order). New abilities for items:

   | Ability | Does | Built from |
   |---|---|---|
   | **Blink** | Teleport to a spot the side can see, up to 4 m | `feat-items` 3.5 (the `blink` shape, the hurt-lock) |
   | **Vanish** | Unseen by the other side for 2 turns; broken by dealing or taking damage | `feat-items` 3.6 (the `veil` status) |
   | **Take Flight** | The existing `fly` status for 2 turns: climbs any height, which today only fliers can | the status table as it is |
   | **Surge** | +35% ability damage for 2 turns | a new status, `surge`, adding to the damage percent |
   | **Phase** | Blink's longer cousin, 6 m | the `blink` shape |

   An item's ability gains ultimate meter when used like any action, never needs the meter, and is
   never the ultimate.
7. **Caps**, after adding every item and team bonus: ability damage % at most 60, healing % at most 60,
   Turn Gauge % at most 30; the item part of A-Eva, M-Eva and Crit at most 20 each; of Move, Speed and
   Jump at most 2 each. Floors: Speed and Move at least 1, Sight at least 3, the rest at least 0.

## 4. Randomness
Two new generators, both separate from the battle's `FSimRandom` so no existing roll moves:
- **Camp spots** (`Camp::Salt`): drawn at battle start for the first spots, then each time a camp wakes
  again, in camp-index order.
- **Loot** (`Loot::Salt`): one draw per item placed in a cache, in the order camps are cleared.
Monsters' own attacks roll on the battle's generator in the normal order (targets in unit-id order,
evade then crit, damage only), as every unit's do. `FNeutralPlayer` rolls nothing. Both generators'
states are in the checksum.

## 5. Numbers
### 5.1 Formulas
- **Damage** (as `feat-items` 5.1): `Raw = RoundToInt((Power + Flat) × (1 + Pct/100) × Height × Flank)`,
  then `max(1, RoundToInt((Raw − Def) × DamageMultiplier))`. `Pct` = items + Surge + team bonuses,
  capped at 60.
- **Healing**: `RoundToInt(Power × (1 + HealPct/100) × HealScale × HealMultiplier)`, capped at what the
  target is missing.
- **Turn Gauge**: today `TgGain = RoundToInt(BaseTgGain × TgFactor × Hustle)`. It becomes
  `RoundToInt(BaseTgGain × TgFactor × Hustle × (1 + TgPct/100))`. With `TgPct` 0 the extra factor is
  exactly 1.0, so today's numbers are unchanged bit for bit.
- **Jump**: `JumpOf(Unit)` becomes `Flies() ? FlyJump : Ground::Jump + ItemJump` (item part capped at 2).

### 5.2 Worked examples (run, not guessed)
Ability damage: Frost Rod, power 34, into AttDef 4, level ground, from the front.

| Ability damage % | Raw | Damage |
|---|---|---|
| 0 (today) | 34 | **15** |
| +10 (Focus Crystal) | 37 | **17** |
| +15 (Executioner's Edge) | 39 | **18** |
| +25 (Crystal + Edge) | 43 | **20** |
| +35 (Surge) | 46 | **21** |
| +60 (the cap) | 54 | **25** |

Turn Gauge (`TgMax` 4000, Speed multiplier 1.0). Gains are whole points per tick, so small percents land
in steps:

| Speed | +0% | +8% (Hourglass Pin) | +15% (Chrono Gear) | +30% (cap) |
|---|---|---|---|---|
| 5 | 10/tick, 400 ticks | 11, 364 (−9%) | 12, 334 (−17%) | 13, 308 (−23%) |
| 10 | 20, 200 | 22, 182 (−9%) | 23, 174 (−13%) | 26, 154 (−23%) |
| 14 | 28, 143 | 30, 134 (−6%) | 32, 125 (−13%) | 36, 112 (−22%) |

So +8% can be worth 6-10% more turns depending on Speed; the item says "about 8%".

### 5.3 The monsters
Measured against the median class in the creator's library (HP 76, Speed 10, damage power 36, defences
about 6). "Hits" is how many median hits clear the camp; a unit acts about every 20 s at Speed 10.

| Tier | Camp | Each | Hits to clear | Their hit on a median unit | Loot |
|---|---|---|---|---|---|
| **Easy** | 1 Scuttler | HP 30, AttDef 3, MagDef 3, Speed 8, Move 4; Nip (melee, power 16) | 2 (one unit, 2 turns) | 5 | 1 common |
| **Medium** | 2 Dire Wolves | HP 40, 4/4, Speed 10, Move 6; Bite (power 22); one has Howl (2.5 m circle, Slow 1 turn, cooldown 3) | 6 (two units, ~3 turns each) | 8 | 1 uncommon |
| **Hard** | Stone Ogre and a Shaman | Ogre HP 110, 9/6, Speed 8, Move 4; Club (power 34), Ground Slam (2.5 m circle, power 26, Knockdown, cooldown 3). Shaman HP 35, 3/6, Speed 9; Mend (heals the ogre 25, cooldown 2) | 11 (three units) | Club 14, Slam 10 to each | 1 rare |
| **Epic** | Ancient Drake | HP 240, 10/10, Speed 9, Move 5, flies; Bite (power 40), Fire Breath (60° cone, 6 m, power 32, Burn 2 turns, cooldown 3), ultimate Inferno below half health (4 m circle round itself, power 38); can't be stunned, slept or frozen | 19 (the whole side, ~5 turns each) | Bite 17, Breath 13 + burn, Inferno 16 | 1 epic + 1 rare |

Monsters are class files, so these are starting points; the class creator's lab measures each camp
(10.3) and the numbers move from there. A Developer Tools slider, **Monster strength** (50-200%), scales
their HP and power together.

### 5.4 The items, by tier
Roughly, a common is worth a small edge, an epic a big one. Actives are rare and above.

**Common** (easy camps)
| Item | Gives |
|---|---|
| Iron Charm | +12 max HP |
| Padded Vest | +2 AttDef |
| Warded Sash | +2 MagDef |
| Duelist's Ribbon | +4 A-Eva |
| Spirit Bangle | +4 M-Eva |
| Lucky Coin | +4 Crit |
| Scout's Feather | +3 Sight |
| Worry Beads | +3 Patience |

**Uncommon** (medium camps)
| Item | Gives |
|---|---|
| Trail Boots | +1 Move |
| Chain Vest | +20 max HP, +2 AttDef |
| Focus Crystal | +10% ability damage |
| Censer of Mercy | +15% healing |
| Hourglass Pin | +8% Turn Gauge speed |
| Spring Greaves | +1 Jump (climbs 3 levels) |
| Mirror Cloak | +2 MagDef, +6 M-Eva |
| Hunter's Eye | +4 Sight, +4 Crit |

**Rare** (hard camps, and the epic camp's second item)
| Item | Gives |
|---|---|
| Quicksilver Charm | +1 Speed |
| Executioner's Edge | +15% ability damage, +6 Crit |
| Bulwark Plate | +45 max HP, +4 AttDef, −1 Move |
| Chrono Gear | +15% Turn Gauge speed |
| Saint's Chalice | +25% healing, +20 max HP |
| Blink Stone | **Blink**: teleport up to 4 m, cooldown 3 |
| Veil Cloak | **Vanish**: unseen for 2 turns, cooldown 5 |
| Berserker Totem | **Surge**: +35% ability damage for 2 turns, cooldown 4 |

**Epic** (the epic camp)
| Item | Gives |
|---|---|
| Dragonheart | +50 max HP, +15% ability damage, +1 Speed |
| Wings of the Roc | +1 Move; **Take Flight**: fly for 2 turns, cooldown 5 |
| Phase Edge | +10% ability damage; **Phase**: teleport up to 6 m, cooldown 2 |
| Crown of Ages | +20% Turn Gauge speed, +3 Patience |
| Aegis of Dawn | +60 max HP, +5 AttDef, +5 MagDef |

## 6. Iteration order and ties
Camps tick in camp index order (pairs blue then red, tiers easy to epic, epic last). Caches are listed in
the order made. Items on a unit are summed in slot order. Monsters act in the normal unit order by gauge,
ties by id, as every unit does. Nothing new iterates over a container whose order could differ between
machines.

## 7. Edge cases
| Situation | What happens |
|---|---|
| A unit stands on a camp's spot when it wakes | Monsters step onto the nearest free nodes to the spot, in id order (as units are placed at battle start). |
| A camp wakes where a cache still lies | The cache stays; the camp wakes beside it (the spot draw treats a cache like a camp: 8 m apart). |
| Both sides fighting the same camp | Allowed. Monsters are everyone's enemy; area attacks hit monsters and enemies alike. |
| A unit leashes a monster away and runs | The monster walks home and resets to full (`feat-objectives` 3.2.5). |
| Last monster falls to a burn or a hazard | The cache appears as usual; nobody needs to have landed the blow. |
| Take and Drop in the same turn | Drop is free, Take is the action: drop first, then take, is allowed. |
| Finished off while carrying an item with an ability mid-cooldown | The cache holds the item with its cooldown still running (3.4.5). |
| Max HP falls because an HP item is dropped | Current HP is capped at the new max; it never rises from the drop. |
| Taking an HP item | Max HP and current HP both rise by the item's HP (so taking it heals that much). |
| The battle ends with caches on the ground | Nothing; they're gone with the battle. |
| Camps on, but the map has no room for a band | That pair is left out and the log says so (3.1.4). |

## 8. Godot quirks
None: Godot has no camps and no items. A battle with `CampLevel` 0 must be exactly Godot's, and the parity
tests prove it by passing unchanged.

## 9. State and events
### 9.1 Item file
`Content/Data/Items/<id>.tmitem.json`, as in `feat-items` 9, with these changes: no `slot` or `cost`;
a `tier` (`common`, `uncommon`, `rare`, `epic`); `bonus` gains `tg_percent` (0-30); `stats` gains `jump`
(0-2); an `ability` is allowed on any item (rare and epic in the catalog). Refused with the reason, as
class files are, for any unknown key, range or status.

### 9.2 Monster class files
Ordinary class files with `"roles": ["monster"]` and `"tier": "easy"|"medium"|"hard"|"epic"`, hidden from
the team picker and the random team. `Content/Data/Classes/monsters/`.

### 9.3 New state (all in `Checksum()`, one `SimPlayTest` probe each)
Per camp: tier, spot, state, timer. Per cache: spot and item ids (with each item's cooldown). Per unit:
three item ids and their cooldowns; `bShaken` (Blink's hurt-lock). The camp and loot generators' states.
`CampLevel` and the new tuning keys (appended after the existing ones, so Godot's indices don't move).

### 9.4 New orders
`Take(unit, serial, cache, item, slot-to-empty or −1)`, `Drop(unit, serial, slot)`. Item abilities use
the existing `UseAbility` with slots 4-6. Online protocol version goes up by one.

### 9.5 Events for the view
CampWarning(camp, seconds), CampAwake(camp), CampCleared(camp), CacheAppeared(cache, items),
ItemTaken(unit, item), ItemDropped(unit, item, cache), plus `feat-items`' Blinked, Veiled, Revealed.

## 10. Proof
1. **Unchanged battles:** every parity test (`SimTraceTest`'s 14 Godot battles and 1667 decisions above
   all) passes untouched with camps off.
2. **`SimCampTest`** (new): the same seed gives the same spots, every time; spots are mirrored, in their
   bands, 8 m apart, reachable; a cleared camp wakes at a new spot on time; the loot roll is the same on
   replay and never duplicates an item; Take refused for each reason in 3.4.2; full slots drop the named
   item; finished-off units drop everything; the table in 5.2 exactly; caps hold with everything stacked;
   monsters never count toward any win; the battle's own dice are untouched by camps (a battle with camps
   on and nobody going near them rolls exactly as with camps off until the first monster acts).
3. **Determinism:** `SimPlayTest` plays and replays a battle with camps, loot and every item ability in
   use to the same checksum; `Tests\OnlineTest.bat` with camps on.
4. **Balance, in the class creator:** a **Monsters** tab makes monster classes and measures a camp (how
   many median-unit turns to clear it, and how much health it costs); an **Items** tab makes item files
   and measures one (win-rate change over 64+ lab games with and without it). A camp that takes more than
   its tier's target, or an item well above its tier, is flagged.

## 11. Player-facing feedback
- **On the board:** each camp has a marker in its tier's colour (easy green, medium blue, hard purple,
  epic gold) with a countdown while it waits ("0:12"), and the monsters stand in a ring of that colour.
  Both sides see a marker at every camp; its tier colour and countdown only once the side has seen the
  spot (12, answered), and the monsters only in sight. A cache is a glowing chest in its tier's colour;
  resting the pointer on it shows its items.
- **Action bar:** a **TAKE** button when a cache is in reach, opening a small picker (and, with full
  slots, which item to leave). Item abilities sit after the class's four, with their items' icons.
- **Unit panel:** three item squares under the abilities; stats show the items' part in a second colour
  ("Move 7 **+1**"). Tooltips say what each item does and its tier.
- **Log:** "A hard camp stirs in the west (10 s).", "Blue cleared the Stone Ogre.", "Knight took the
  Blink Stone.", "Rogue dropped the Veil Cloak."
- **Unit Guide:** two new tabs, **Monsters** (each camp, its monsters, what it drops) and **Items** (every
  item by tier), in the same layout as a class's page.
- **Setup screen:** Neutral camps: Off / Light / Standard / Wild. **Developer Tools:** first wake and
  respawn times per tier, Monster strength, and the caps.

## 12. Open questions for the human
**Answered 2026-09-29 22:19:**
- **Loot** sits on the ground until a unit picks it up (3.3.3 as written).
- **Death:** a unit that dies drops its items (3.4.4 as written: finished off, not merely knocked out).
- **Budget:** items come from camps **and** from a points budget on the setup screen, both. Three open
  slots per unit. Budget items cost by tier (common 1, uncommon 2, rare 3); epic items can't be bought, only
  won from the epic camp **(default)**. Default budget 6 points per side **(default)**, a setup option.
- **Fog:** camp markers always show on the map, but a camp's **tier is hidden** (a plain marker) until the
  side has seen the camp's spot; after that it shows the tier it was when last seen, and its countdown.

Still open:
1. **Who gets the loot?** Written: it drops on the ground and whoever takes it keeps it, so a side can be
   robbed. The alternative: it goes straight to the unit that landed the last blow.
2. **Items dropped on death?** Written: a unit that is finished off (not just knocked out) drops its
   items. The alternative: items are lost with it, or kept for nobody.
3. **The setup budget in `feat-items`.** Written: gone; items come only from camps. It could stay as a
   separate option ("start with items") for players who want to build before the fight.
4. **Show camps through the fog?** Written: markers and countdowns always shown, monsters only in sight.
   Hiding the markers too makes scouting matter more but makes camps easy to miss.
5. **One epic, or a pair?** Written: one, in the middle, as Roshan is. A pair would make it less of a
   single decisive fight.
6. **Should the computer go for camps?** Written for the first version: it clears camps near it when it
   can't see an enemy, always takes loot in reach, and contests the epic camp when its side is ahead.
7. **Passing items between allies.** Written: only by dropping and picking up. A direct Give order is
   easy to add.
8. **Passive "on death" items** (a feather that revives once, say) aren't in the catalog; they need a
   rule for when they fire. Worth it later?

## 13. Build order (when approved)
1. Items in the rules: item files, three slots, stats with caps, ability damage %, healing %, Turn Gauge %,
   Jump; `SimItemTest`. (No way to get items yet except tests.)
2. Team 2 and `FNeutralPlayer` from `feat-objectives` 3.2 (audit every `[Team]` array first).
3. Camps: placement, timers, monsters, caches, Take and Drop; `SimCampTest`; the computer player.
4. Item abilities: Blink, Vanish (veil), Take Flight, Surge, Phase.
5. The view: markers, chests, TAKE, item squares, log lines, the guide's tabs, setup and Developer Tools.
6. The class creator: Monsters and Items tabs, with measuring.

## 14. Bestiary, behaviours, bosses and more items (approved 2026-09-30)

Designed in the doc "Neutral Camps: Bestiary, Behaviours & Items" (Claude Docs), which holds the tables:
eight temperaments (docile, skittish, provoked, territorial, aggressive, guardian of a place, guardian of a
unit, patrol) and four traits (ambush, pack hunter, scavenger, lookout); 14 creatures on Paragon jungle and
minion models and unworn hero skins; 13 camps; three bosses (Helix Prime, Chronos, Magma Colossus); 33 more
items in seven kinds. It replaces 5.3's four camps. The human's answers:

1. **Scope:** camps and all three bosses are built together, not a first cut.
2. **Monsters never fight each other.** All are team 2 and allies of one another. (Rival monsters that do
   may come later.)
3. **The Treasure Runner escapes** with its item if it reaches the board's edge.
4. **Tamer's Collar is kept:** a monster joins the user's side for 3 of its turns.
5. **The boss belongs to the map,** not the theme: a map file names its boss (`"boss"`), which wakes at the
   map's boss spot (the walkable node nearest the middle). A setup setting, **Random boss**, spawns one of the
   bosses at random there instead (drawn by the camps' own generator, so both machines agree).
6. **A monster's temperament icon shows before the monster is seen,** with its camp's marker.
