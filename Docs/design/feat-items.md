# Rules Spec: feat-items

Status: Draft (2026-09-29). Written for the human to approve, change or cut. Nothing is built.
**Revised by `feat-neutral-camps.md` (2026-09-29, 21:55 brief):** items now come from neutral monster camps,
tiered common/uncommon/rare/epic; each unit has **three open slots** (not weapon/armour/trinket); any item
may carry an ability (slots 4-6); new effects `tg_percent` and `jump`, and the Surge, Take Flight and Phase
abilities. That spec's catalog (5.4) replaces section 5.3 here, and the points budget (3.1.2) is an open
question there (12.3). The formulas (3.3, 3.4), Blink (3.5), Veil (3.6) and the proof (10) stand.
Mode: Feature
Backlog goal: Units can carry items, chosen on the setup screen within a points budget (or won in battle
from a reliquary, `feat-objectives`), that add to their stats, add flat or percent power to their damage
and healing, or give them a fifth ability such as Blink (teleport) or Vanish (go unseen). Items are files
made by the class creator, as classes are.

## 1. Summary
A class says what a unit *is*; an item says what it has *brought*. Three slots per unit (weapon, armour,
trinket), a team budget of points to spend across all four units, and a small catalog to start from. The
weapon and armour slots move numbers; the trinket slot is where new verbs live (blink, vanish). A team
with no items plays bit for bit as today, so every Godot parity test still passes.

## 2. What other games do, and what we take from them
| Game | Mechanic | What we take | What we leave |
|---|---|---|---|
| **Final Fantasy Tactics** | Typed slots (weapon, shield, head, body, accessory). Accessories carry the odd effects: Move +1, Jump +1, permanent Float, auto-Reraise, starting Invisible. | **Typed slots**, so a unit can't stack three of the best stat item; the **accessory as the place for odd effects** (our trinket). | Five slots; equipment tied to jobs. |
| **Fire Emblem** | Few, legible stat items; boosters that are permanent. | Items that are **easy to read at a glance**: one or two lines each. | Durability. |
| **Dota 2, Blink Dagger** | Teleport a short way; **can't be used for 3 s after taking damage** from a hero. | **Blink**, with the same lock: a unit that was just hurt can't blink away. That keeps it for starting fights and repositioning, not for escaping every one. | – |
| **Dota 2, Shadow Blade** | Invisibility that breaks on attack; cheap detection (dust, sentries) counters it, which is why it's called a "trap item" when the other side answers it. | **Vanish** that breaks when you deal or take damage; **answers built in** (an enemy close by sees you; a watchtower reveals you; area attacks still hit you). | Detection as a thing to buy (our answers are on the map). |
| **League of Legends** | Flat and percent bonuses; flat first, then percent. | **Flat, then percent**, one rounding. | Hundreds of items. |

## 3. Rules, in the order they are applied
### 3.1 Owning items
1. Each unit has three slots: **weapon**, **armour**, **trinket**, each empty or one item of that slot.
2. Before a battle each side spends a **budget** of points (setup screen, default **12** per side, a new
   `FTuning` field `ItemBudget`) on items for its four units. The rules refuse a battle start whose
   loadouts cost more, or put an item in the wrong slot ("Blink Stone is a trinket.").
3. A **relic** is an item with `"relic": true`: never on sale on the setup screen, only won from a
   reliquary (`feat-objectives` 3.3), and it takes the slot its file says, replacing what is there for the
   rest of the battle.
4. What a unit carries is part of the battle's starting data, like its class: sent to the joiner online,
   recorded by a replay, in the checksum.

### 3.2 What an item can do
An item file (section 9) may hold any mix of:
1. **Stats** — whole numbers added to the class's own ten (`hp`, `attdef`, `magdef`, `aeva`, `meva`,
   `crit`, `speed`, `move`, `patience`, `sight`). May be negative (a trade-off: heavy plate, −1 Move).
2. **Power bonuses** — `damage_flat`, `damage_percent`, `heal_flat`, `heal_percent`, and which abilities
   they reach: `scale` = `att`, `mag` or `any`.
3. **An ability** — only a trinket may carry one, so a unit has at most one. Same schema as a class
   ability (FORMAT.md), checked by the same rules, plus two new things only items can use: the `blink`
   shape and the `veil` status (3.5, 3.6). It becomes the unit's **fifth slot** (index 4).

### 3.3 Stats
1. `FUnit::Stat(S)` becomes: class stat **+ items' stat** + buffs + switched-on passives (as now), then
   the defence factor (as now). Items add before buffs so a Shred or Freeze scales them like the rest.
2. **Max HP includes items.** Today `MaxHp()` reads the class's HP alone (and so do its 12 callers: KO,
   revive, regen, hazards, ultimate meter, `HealthShare`). It becomes class HP + items' HP, and a unit
   starts at that. The time-limit rule's `HealthShare` compares against what each side *started* with, so
   paying for HP doesn't count as being "healthier" at the whistle unless it's still there.
3. Floors after everything: Speed and Move at least 1, Sight at least 3, the rest at least 0.

### 3.4 Power bonuses
1. A unit's bonuses are the **sums** over its items whose `scale` matches the ability's scale (or is
   `any`), plus team bonuses from objectives (the war shrine's +15% goes into `damage_percent`).
2. They reach **damage** and **heal** abilities only: not revives (a share of max HP), not a shield's
   soak, not a burn or regen ticking later (those are statuses, not the hit).
3. The bonus is **flat first, then percent, then everything the rules already do**, with the one rounding
   today's rule already has. The damage line in `FBattle::CalcAmount` becomes:
   `Raw = RoundToInt((Power + Flat) * (1 + Pct / 100) * DamageScale * Height * Flank)`
   and nothing after it changes: `max(MinimumDamage, RoundToInt((Raw − Defence) × DamageMultiplier))`.
   With no items, `Flat` is 0 and `Pct` is 0, so `(Power + 0) × 1.0` is exactly `Power` in double and every
   number is bit for bit today's.
4. Heal: `Full = RoundToInt((Power + HealFlat) * (1 + HealPct / 100) * HealScale * HealMultiplier)`, then
   capped at what the target is missing, as now.
5. Area abilities: the flat bonus is added **per target** (it's part of the hit). That makes flat damage
   strong on wide areas; the catalog prices it that way. `Pct` is capped at 100 in total.
6. Because `CalcAmount` is also what `Preview` uses, **the forecast shows the bonus** with no extra work.

### 3.5 Blink (a new shape, for items only)
`"shape": "blink"`, `"effect": "support"`, `"target": "ally"` (the user), `max_range` in metres.
1. **Aim:** a spot within `max_range` of the user, on a node a unit can stand on, with no unit on it
   (`UnitSpacing`), that **the side can see**. It ignores paths, height and walls: it's a teleport.
2. **Refused when:** the unit is Rooted or Frozen ("It can't move."), or **shaken**: it has taken HP
   damage since its previous turn ended ("It was hurt too recently to blink."). Shaken is a new bool on
   the unit, set when HP damage lands (after shields), cleared when its own turn ends. This is Dota's
   Blink Dagger lock: blink starts fights and repositions; it doesn't dodge every one.
3. **Resolves:** the unit is put on the spot (snapped to its node, as the `vector` dash does today) and
   faces the way it went. It counts as the turn's **ability**, not its walk, so a unit may walk and blink
   in one turn — that's the point of it, and the cooldown pays for it.
4. The unit keeps its statuses and is not hit by anything on arrival (embers burn at its next turn start
   as normal).
5. "2 hexes" in the brief is **2 tiles = 4 m** here (tiles are 2 m squares; units stand on 0.5 m nodes).

### 3.6 Veil (a new status, for items only) and Vanish
Status `veil`: the unit is **unseen** by the other side (and by guardians).
1. **Seen anyway when:** an enemy unit is within **3 m** of it with line of sight (`RevealRadius`, new
   `FTuning`), or an enemy-owned watchtower's reveal circle covers it (`feat-objectives`).
2. **Unseen means:** the other side's view doesn't draw it; it can't be chosen as a unit target
   ("follow") or aimed at as a unit ("You can't see that unit."); and the other side's computer player
   doesn't know it's there — all of its reasoning goes through the new `CanSeeUnit(Team, Unit)`. **Area
   abilities aimed at the ground still hit it**, so a side that guesses right catches it (Dota's answer to
   invisibility, and ours).
3. **Ends when:** its turns run out, **or it deals damage** (after the ability resolves), **or it takes
   damage**. Walking, healing and support abilities don't break it.
4. **Vanish** (the Veil Cloak's ability): `effect: support`, `shape: self`, `status: veil` for 2 turns,
   cooldown 5.
5. Veil adds one flag to the status table (`bHidden`); `FORMAT.md` and `validateTmClass` list it, but the
   class creator refuses it on class abilities in this version (items only), so no class changes balance.

### 3.7 The fifth slot
1. Slot 4 exists only when the unit's trinket carries an ability. Orders for slot 4 on a unit without one
   are refused ("No such ability.").
2. It is never the ultimate (slot 3 stays the ultimate), never needs the ultimate meter, and has its own
   cooldown. Using it gains ultimate meter like any action (`UltPerAction`).
3. `Cooldowns`, `Toggled`, `ToggledTurn` grow from 4 to 5. Every `for (Slot = 0; Slot < 4 …)` in the rules
   (passives in `Stat`, `ApplyAuras`, cooldown ticking, the computer player's search) goes to the unit's
   slot count, and `JobAbility(Job, Slot)` gives way to `AbilityOf(Unit, Slot)` so slot 4 comes from the
   unit's trinket.

## 4. Randomness
None added. Items only change numbers that dice are already rolled against (evasion, crit chance) and
the amounts they're rolled for. Roll order is unchanged: targets in unit-id order, evade then crit, damage
only. Blink and Vanish are support abilities, which roll nothing. The setup screen's budget has no dice.

## 5. Numbers
### 5.1 Formulas
As 3.4: `Raw = RoundToInt((Power + Flat) × (1 + Pct/100) × 1.0 × Height × Flank)`, damage
`max(1, RoundToInt((Raw − Def) × 0.5))`. `Flat` and `Pct` are `double` (GDScript float); item file values
are whole numbers. `RoundToInt` is Godot's `roundi` (halves away from zero), as everywhere.

### 5.2 Worked examples (checked by running the formula)
| Case | Power | Flat | Pct | Def | Height, flank | Raw | Damage |
|---|---|---|---|---|---|---|---|
| Frost Rod, no items (today) | 34 | 0 | 0 | 4 | level, front | 34 | **15** |
| + Whetstone Blade | 34 | 4 | 0 | 4 | level, front | 38 | **17** |
| + Executioner's Axe | 34 | 0 | 10 | 4 | level, front | 37 | **17** (16.5 rounds up) |
| + Whetstone and shrine | 34 | 4 | 15 | 4 | level, front | 44 | **20** |
| + both, two levels up, from behind | 34 | 4 | 15 | 4 | ×1.2, ×1.25 | 66 | **31** |
| **Edge:** weak hit into a wall of defence | 10 | 4 | 0 | 14 | level, front | 14 | **1** (the minimum; flat can't beat it) |
| Median class hit (power 36, def 7) | 36 | 4 | 0 | 7 | level, front | 40 | **17** (was 15) |

Heal: power 30 today heals `RoundToInt(30 × 1.5) = 45`. With the Censer of Mercy (+6 flat, +10%):
`RoundToInt(36 × 1.10 × 1.5) = RoundToInt(59.4) = 59`.

Speed: the gauge fills in proportion to Speed (`BaseTgGain = Speed × TgPerSpeed`), so **+1 Speed on the
median class (Speed 10) is about 10% more turns** for the whole battle, and 20% on a Speed-5 class. That
is why the Quicksilver Charm is expensive and Speed items are capped at +1 each.

### 5.3 The starter catalog
Priced against the 27 classes in the creator's library (median: HP 76, Speed 10, Move 7, Sight 10, damage
power 36). Roughly, 1 point ≈ 8-10% more of one thing a unit does. Budget 12 a side ≈ 3 a unit.

| Item | Slot | Cost | Gives |
|---|---|---|---|
| Whetstone Blade | weapon | 2 | +4 damage power (att) |
| Focus Crystal | weapon | 2 | +4 damage power (mag) |
| Executioner's Axe | weapon | 3 | +10% damage (att), +4 Crit |
| Archmage Staff | weapon | 3 | +12% damage (mag) |
| Censer of Mercy | weapon | 2 | +6 heal power, +10% healing |
| Padded Jerkin | armour | 1 | +15 HP |
| Chain Hauberk | armour | 2 | +20 HP, +3 AttDef |
| Warded Robe | armour | 2 | +3 MagDef, +5 M-Eva |
| Bulwark Plate | armour | 3 | +40 HP, +5 AttDef, **−1 Move** |
| Eagle Feather | trinket | 1 | +4 Sight |
| Worry Beads | trinket | 1 | +3 Patience (longer to decide before the turn is lost) |
| Trail Boots | trinket | 2 | +1 Move |
| Quicksilver Charm | trinket | 3 | +1 Speed |
| Warden's Key | trinket | 1 | Counts as two units when capturing a site (`feat-objectives`) |
| **Blink Stone** | trinket | 3 | Ability *Blink*: teleport up to 4 m (2 tiles), cooldown 3 |
| **Veil Cloak** | trinket | 3 | Ability *Vanish*: veil for 2 turns, cooldown 5 |
| *Dragonheart* (relic) | armour | – | +40 HP, +10% damage, +1 Speed |
| *Phase Edge* (relic) | trinket | – | Ability *Phase*: teleport up to 6 m, cooldown 2 |

These are starting guesses. The class creator's measuring (below) sets them properly.

## 6. Iteration order and ties
Items are summed in slot order (weapon, armour, trinket), which can't change a sum but fixes the order
for anything that reads the list. Loadouts are listed in unit-id order in the battle's starting data and in
the checksum. Nothing new is iterated during a tick.

## 7. Edge cases
| Situation | What happens |
|---|---|
| Unit knocked out and revived | Keeps its items. Revive's share is of max HP *with* items. |
| Negative stat takes a stat to 0 or below | Floors in 3.3.3. |
| Shred / Freeze with item defence | Scale the total, items included (3.3.1). |
| Blink onto a spot a unit walks onto in the same tick | Can't: an ability resolves on the unit's own turn, and the spot is checked as it resolves; if taken by then, the unit stays where it is and the event says so (as `vector` does now). |
| Blink while casting / channelling | Refused as any ability is. |
| Blink with a cast time | Allowed by the schema; the catalog's have none. The spot is checked on landing. |
| Veiled unit stands in a capture ring | It counts for its side, but the other side sees the ring filling — a hint, not its position. |
| Veiled unit caught in an area attack it wasn't aimed at | Hit, damaged, and so revealed (veil ends). |
| Two sources of `damage_percent` | Added, capped at 100. |
| Item file names a stat, status or key the rules don't know | Refused with the reason, as class files are. |
| Relic replaces an item with an ability, mid-cooldown | The new ability starts ready; the old cooldown is dropped. |
| Loadout over budget or wrong slot | The battle doesn't start; the setup screen says why. |

## 8. Godot quirks
None: Godot has no items. A battle where nobody carries anything must be exactly Godot's, and the parity
tests prove it by passing unchanged.

## 9. State and events
**Item file** `Content/Data/Items/<id>.tmitem.json`, as strict as a class file (unknown key, bad range or
unknown status: refused with the reason; checked by the same rules in the creator and in the game):
```json
{
  "format": "tactical-masters-item",
  "version": 1,
  "id": "blink_stone",
  "name": "Blink Stone",
  "desc": "Step through the air to a spot you can see, up to two tiles away.",
  "icon": "blink",
  "slot": "trinket",
  "cost": 3,
  "stats": {},
  "bonus": {"damage_flat": 0, "damage_percent": 0, "heal_flat": 0, "heal_percent": 0, "scale": "any"},
  "ability": {"id": "item_blink_stone", "name": "Blink", "desc": "Teleport up to 4 m.",
              "kind": "active", "effect": "support", "scale": "mag", "target": "ally", "shape": "blink",
              "power": 0, "min_range": 0, "max_range": 4, "aoe": 0, "cooldown": 3, "cast": 0,
              "anim": "dash"}
}
```
| Key | Rules |
|---|---|
| `slot` | `weapon`, `armour`, `trinket` |
| `cost` | 1–6 (ignored for a relic) |
| `relic` | optional, default false |
| `stats` | any of the ten; `hp` −50…+100, `speed` −1…+1, `move` −2…+2, others −10…+15 |
| `bonus` | flats 0–30, percents 0–50, `scale` `att`/`mag`/`any` |
| `ability` | trinket only; the class-ability schema; `shape: blink` and `status: veil` allowed here only |

**New state (in `Checksum()`, one `SimPlayTest` probe each):** each unit's three item ids; the fifth
cooldown and toggle; `bShaken`; the side's budget. Item stats are read from files like class stats, so the
checksum mixes the ids, not the numbers (as it does for classes).

**Events for the view:** Blinked(from, to), Veiled, Revealed(by what), ItemGained(unit, item) (relics).

## 10. Proof (Feature mode, no Godot reference)
- **Unchanged battles:** every parity test (`SimTraceTest`'s 14 Godot battles and 1667 decisions above
  all) passes untouched with empty loadouts.
- **`SimItemTest`** (new): every catalog file loads; broken files are refused with the right reason; the
  table in 5.2 exactly; max HP with items through KO, revive, regen and hazards; Blink refused while
  rooted and while shaken, lands on the node, refused on water, rock, an occupied or unseen spot; Veil
  hides from `CanSeeUnit`, is seen within 3 m, ends on dealing and on taking damage, and area attacks still
  land; slot 4 refused on a unit without a trinket ability.
- **Determinism:** `SimPlayTest` plays and replays a battle with items (both new abilities in use) to the
  same checksum; the wrong-seed probe still fails; `Tests\OnlineTest.bat` with loadouts.
- **Balance (the class creator):** an **Items tab** makes and installs item files (checked by
  `validateTmItem`, a twin of the game's reader), and **measures** one: the class lab plays the reference
  team with and without it over 64+ games, alternating sides, and reports the win-rate change per point
  of cost. An item worth much more or less than its price is flagged, as classes are against their role.

## 11. Player-facing feedback
- Setup screen: under each unit, three slot boxes; clicking one lists the items for that slot with cost
  and a one-line effect; the side's points left at the top ("Items 9 / 12").
- Unit panel and tooltip: stats show the item part in a second colour ("Move 7 **+1**").
- Forecast: already includes the bonus (3.4.6). A small "+4 / +15%" tag beside the number says why.
- Blink: the reachable circle as a ground decal; spots it can't take are drawn red with the reason.
- Veil: for its own side the unit is drawn translucent; for the other side it's gone, and a shimmer shows
  only when it's revealed. The log says "Rogue vanished." / "Rogue was revealed by Knight."
- HUD: the fifth ability sits apart from the four, with the trinket's icon.

## 12. Open questions for the human
- **Budget or free?** A per-side points budget (written) keeps choices meaningful. Alternatives: a fixed
  number of items per unit with no costs, or items only from objectives (no pre-battle choice at all).
- **Should the computer side get items?** Written: when you play the computer, it gets the same budget
  and picks from presets per class role (a tank takes armour, a damage class a weapon). Or: none.
- **Blink through walls?** Written: yes, any spot the side can see within range. The stricter version
  needs line of sight from the unit, which makes it a gap-closer rather than a wall-hopper.
- **Veil and online fairness.** Each game still holds the whole battle (that's how both stay in step), so a
  modified client could draw hidden units. That's already true of fog today; worth knowing before veil
  makes it matter more.
- **Consumables** (a potion used once) fit the same file with `"uses": 1`, but are left out of this
  version.

## 13. Retuned for the new defense rules (2026-10-02)

The items were sized when AttDef and MagDef were subtracted from each hit and A-Eva and M-Eva were separate
chances to miss. In the game's rules now (defense model 1) Armor and Resist take a share off, 30 / (30 + it),
and one Evasion -- the higher of A-Eva and M-Eva, plus anything added to either -- is evaded 1 in 10 outright
and grazed for half otherwise. The same +2 AttDef protects less than it did, and an M-Eva item now helps
against weapons too.

Each item's defensive numbers were rescaled so it takes off about the same share of the damage its holder
takes as before: worked out over every class in the game as the holder against every damaging ability of every
class, on the class files' own numbers. One point of Armor or Resist is worth about 0.6 of an old
point; A-Eva points are worth about the same; M-Eva points about 1.25 old ones.

| Item | Before | After |
|---|---|---|
| Padded Vest, Chain Vest | AttDef 2 | Armor 3 |
| Hide Wrap | AttDef 1 | unchanged (1 is the nearest) |
| Bulwark Plate | AttDef 4 | Armor 6 |
| Warden's Plate | AttDef 4, MagDef 4 | Armor 6, Resist 6 |
| Aegis of Dawn | AttDef 5, MagDef 5 | Armor 8, Resist 8 |
| Leaden Mantle | AttDef 6, MagDef 6 | Armor 10, Resist 10 |
| Warded Sash | MagDef 2 | Resist 3 |
| Skink Scale | MagDef 3 | Resist 4 |
| Mirror Cloak | MagDef 2, M-Eva 6 | Resist 3, Evasion (M) 5 |
| Spirit Bangle | M-Eva 4 | Evasion (M) 3 |
| Duelist's Ribbon, Deserter's Boots | A-Eva 4, 3 | unchanged |
| Anchor Stone | +4 both defences when still | +6 Armor and Resist |
| Last Stand Band | +8 A-Eva below 30% | +9 Evasion |
| Nightcloak | +8 A-Eva and M-Eva while unseen | +15 Evasion (the cap) |

Protect and Shell (Warding and Spellward Talismans) cut the hit itself and were not changed; their words say
"weapon" and "spell" damage rather than the old stat names.
