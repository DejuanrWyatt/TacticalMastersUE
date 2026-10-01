# Rules Spec: feat-objectives

Status: Draft (2026-09-29). The **watchtower** is built, to the human's choices of 2026-09-29 (section 13);
the rest of this spec (guardians, the other kinds of site, timed events) is not built and still wants approval.
Mode: Feature
Backlog goal: Maps can hold points of interest (watchtowers, shrines, wells, reliquaries), some guarded by
neutral monsters, that a side captures for a bonus; and timed map events (fire lines, fog banks, crumbling
walls) that change the ground part way through a battle. All of it deterministic, mirrored and fair.

## 1. Summary
Today a battle is two sides of four and the ground they stand on. This adds a third kind of thing on the
board: **objectives** worth leaving the fight for. A watchtower that shows a side a wide circle of the map,
a shrine that makes its holders hit harder for a while, a guardian monster that has to be beaten first
and can be stolen by whoever lands last. Each one is a choice between going for the enemy and going for
the edge, which is what makes MOBA maps (Heroes of the Storm's watchtowers and mercenary camps, Dota's
outposts and Roshan) interesting to play on. The rules are written so a map *without* objectives plays
bit for bit as it does today, and every Godot parity test still passes.

## 2. What other games do, and what we take from them
| Game | Mechanic | What we take | What we leave |
|---|---|---|---|
| Heroes of the Storm, **watchtowers** | Stand in the circle until your side owns it; it gives vision over a major path. Enemies retake it the same way. | Capture by standing uncontested; vision as the reward; placed over routes, not in corners. | Nothing: this is the example the human gave. |
| Heroes of the Storm, **mercenary camps** | Beat the camp, then stand on the point uncontested for 1.5 s to claim it. Either side can claim it, even the side that didn't fight. Monsters leash home and heal if pulled away. Respawn on a timer. | Guard → capture split (so a side can be robbed); leash and reset; respawn timer; the camp grants its captor something. | Monsters pushing lanes (we have no lanes). |
| Dota 2, **outposts** | Channel 6 s to capture (split between heroes channelling); interrupted means nothing gained. Owned outposts give vision. | Capture is faster with more units; being hit while capturing matters. | Teleporting to it; XP. |
| Dota 2, **Roshan** | A boss that drops an item to whoever's side kills it. | The **reliquary**: a hard guardian whose reward is an item (ties into `feat-items`). | Aegis-style extra lives. |
| Final Fantasy Tactics, **map hazards** | Height, water and terrain decide fights, not timers. | Hazards are *ground*: they reuse our embers/springs/rock rather than inventing a damage system. | – |

The lesson from each: a good objective has **a clear reward, a visible cost (time, position, health), a
way for the other side to answer**, and it sits where the armies' routes already cross.

## 3. Rules, in the order they are applied
Everything below happens inside `FBattle::Tick`, after the planning check and before units' gauges
(the same place `TickCapture` runs today), so it is ordered and reproducible.

### 3.1 Points of interest (POIs)
1. A map file may list `objectives` (section 9). Each is a **site**: a kind, a spot in metres on blue's
   half (red's twin is made by turning it about, as spawns are) or `"centre"` for one site on the mirror
   point, and a few numbers. A site on blue's half is always two sites in the battle, one each side, so
   neither side has a nearer one.
   Every site's guardians are made **when the battle starts**, after both sides' units, and kept **off
   the board** (not alive, not ticked, not counted, not seen) until their site wakes. A respawn brings the
   same units back at full health. So unit ids never depend on when anything happened.
2. **Dormant → active.** A site is dormant (drawn greyed, does nothing) until `appears` seconds of battle
   have passed. Then it becomes active: its guardians (if any) step onto the board at home, and an event says so. Ten
   seconds before, an event warns both sides ("A watchtower wakes in 10 s").
3. **Guarded.** While a site's guardians are standing it cannot be captured (step 4).
4. **Capturable.** With no guardian standing, the **capture ring** (radius `ring`, default 3 m) works
   like holding the middle does today, per site:
   - Count living units of each side inside the ring (knocked-out units don't count; guardians don't).
   - Both sides present: contested, nothing changes.
   - One side alone: that side's progress goes up by **1 tick per unit there, at most 3**, so two units
     capture twice as fast (Dota's 6/n). If the *other* side owns the site, the progress first takes the
     owner's hold down to nothing (the site becomes neutral), then builds up for the newcomer.
   - Nobody there: progress stays where it is (nothing drains back).
   - Progress reaching `capture` seconds × 10 ticks makes that side the **owner**. Event: Captured.
5. **Owned.** The owner gets the site's reward (3.3) until one of these:
   - the other side captures it (watchtower, well, totem),
   - its `holds` seconds run out (shrine, reliquary: one-off rewards) — then it goes dormant and
     `respawn` seconds later it becomes active again, guardians and all.
6. A unit **being hit** while in a capture ring does not stop capture in this version (see open
   questions). Being knocked out does, because it no longer counts.

### 3.2 Guardians (neutral monsters)
1. A guardian is a **unit** like any other (`FUnit`), made from an ordinary class file, on **team 2
   (neutral)**. It has a gauge, turns, statuses, facing and abilities, and is hit, evades and crits by the
   same dice in the same order as anyone. So the class creator already makes guardians: a guardian is a
   class with the new role `guardian` (hidden from the team picker).
2. Guardians are **enemies of both sides and allies of each other.** An ability aimed at "enemy" hits
   them whoever uses it; an area attack that catches a guardian and an enemy hurts both.
3. Guardians do **not** count for anything a side wins by: `CheckWinner`, `HealthShare` (time limit) and
   holding the middle ignore team 2. A side with only guardians left standing near it has still lost.
4. A guardian that falls is **gone at once** (no knocked-out countdown, no revive) and an event says
   which side landed the blow. The side that lands it gets nothing extra *except* being next to the
   ring first; the site still has to be captured, so the other side can steal it (the HotS rule).
5. **Home and leash.** Each guardian has a home (its site) and a leash radius (`leash`, default 8 m).
   - **Aggro:** at the start of its turn it picks a target from the living units of teams 0 and 1 that
     are within `leash` of home *and* that it can see (the guardian sees as a side of its own). Of those,
     the one that damaged it most recently; else the nearest; ties by unit id.
   - With a target it walks toward it (never ending a move outside the leash) and uses the first of its
     slots 0-3 that is ready and reaches, in slot order. The ultimate only when below half health.
   - **Reset:** with no target, it walks home. If it begins a turn at home with no target, it heals to
     full and loses its statuses (HotS: pulling a camp and running away doesn't whittle it down).
6. **Who gives a guardian its orders.** Rule 4 of the determinism rules says every change goes through
   `Validate` + `Apply` and the rules never special-case a player. So guardians are driven by an
   **`FNeutralPlayer`**, a sibling of `FAIPlayer`, that turns 3.2.5 into ordinary orders. It uses **no
   random numbers at all** (every choice above has a tie-break), so it needs no generator and can't
   disturb the battle's dice. Whoever steps time runs it: the director offline, the host online (its
   orders are sent like the host's own), the class lab and the map analyser in their battles. A replay
   records its orders like anyone's.

### 3.3 Rewards (the kinds of site)
| Kind | Guarded? | Reward to the owner | Lasts | Default numbers |
|---|---|---|---|---|
| **Watchtower** | optional | The side **sees** everything within `radius` of the tower, from the tower's eye (ground + 3 levels), with line of sight as usual. Also **reveals veiled units** (`feat-items`) within 6 m of the tower. | until retaken | radius 14 m, capture 3 s |
| **War shrine** | yes | Every unit of the side: **+15% damage** (the percent bonus `feat-items` adds) | 45 s, then dormant | capture 2 s, respawn 60 s |
| **Wellspring** | optional | Every unit of the side mends **4% max HP** at the start of each of its turns, wherever it is | until retaken | capture 3 s |
| **Haste totem** | yes | Every unit of the side: **+1 Speed** | 40 s, then dormant | capture 2 s, respawn 75 s |
| **Reliquary** | yes (a hard one) | The **capturing unit** (the one of the capturing side nearest the site, ties by id) is given the site's **item** for the rest of the battle, into its item slot. If it already has one, it is replaced. | the battle | capture 2 s, respawn never |

Rewards reuse what the rules already have, so each is a small change:
- **Vision:** `CanSee(Team, Point)` gains one more way to say yes: an owned watchtower in range with line
  of sight from the tower's eye. Everything that asks what a side can see (the computer player's
  positioning, "You can't see that spot.", fog in the view) follows automatically.
- **Team buffs (shrine, totem, well):** given the way auras are. At the start of each turn of a unit whose
  side owns the site, it gets (or has refreshed) a timed buff tagged with the site's id, exactly as
  `ApplyAuras` tags a buff with an aura's name. When ownership ends, the next turn start removes it. So
  they are already in the checksum and already read by `FUnit::Stat`.

### 3.4 Timed map events (hazards)
A map file may list `events`: changes to the **ground** at set times. Each has a tile list (blue's half;
twins made by turning about), a kind, `at` seconds, `lasts` seconds (0 = for good) and a `warn` time.
1. At `at - warn` an event announces it, and the view marks the tiles (a ground decal, like the ability
   indicators). Both sides are told the same thing at the same time.
2. At `at`, the tiles' ground changes. When `lasts` runs out it changes back.
3. Allowed changes, and only these (each is a ground kind the rules already know):
   | Kind | From → to | What it does in a fight |
   |---|---|---|
   | **fire line** | any walkable → embers `x` | Burns anyone who starts a turn on it (the existing `HazardPercent`) |
   | **bloom** | any walkable → spring `+` | Heals anyone who starts a turn on it |
   | **collapse** | rock `#` → height 1 (for good) | A wall falls and a new route opens mid-battle |
   | **fog bank** | none; a circle | Units *inside* the circle see at most 4 m (their `SightOf` is capped) |
4. **Never ground → rock or water.** Nothing may ever appear under a unit that it can't stand on, so no
   unit is ever trapped, pushed or deleted by an event.
5. The map maker checks (as `checkMap` does now) that every event keeps every blue spawn able to walk to
   every red one at every moment.

## 4. Randomness
None. Sites wake, guardians choose and events fire on fixed schedules from `TickCount`, with tie-breaks
by unit id and site order. The battle's `FSimRandom` is touched only by guardians' own abilities, in
`SimResolve.cpp`, in the normal order (targets in unit-id order, evade then crit, damage only). A
guardian with id 8 rolls where unit 8 would; that is the rule, and it is why guardians are made at a
fixed point (battle start, dormant) rather than when a site wakes, so ids never depend on timing.

## 5. Numbers
- Capture progress per tick for the side alone in the ring: `min(3, UnitsInRing)`. Needed:
  `RoundToInt(CaptureSeconds * Pace::TicksPerSecond)` (int ticks, as holding the middle does).
- Watchtower sight: a point P is seen if `Dist(Tower, P) <= Radius` (double, metres) and
  `HasLineOfSight(TowerEye, P)` where TowerEye is the tower tile's ground height plus 3 levels (2.1 m).
- Well: `RoundToInt(MaxHp * WellPercent / 100)` at turn start, after `GroundEffect`, never above max.
- Shrine: +`ShrinePercent` into the damage percent term of `feat-items` 5.1.

**Worked examples**
1. *Two capture a shrine.* Capture 2 s = 20 ticks. Blue has 2 units in the ring, red none: 2 per tick, so
   10 ticks (1 s). On tick 6 a red unit steps in: contested, blue's progress stays at 12. On tick 9 the red
   unit is knocked out: blue resumes at 12 and owns the shrine 4 ticks later.
2. *Stealing a watchtower.* Red owns it (hold 30). One blue unit alone in the ring: 30 ticks to take red's
   hold to 0 (neutral), 30 more to own it. If a red unit arrives after 40 ticks, blue's 10 progress is kept
   and it's contested.
3. *Shrine damage.* Frost Rod, power 34, into a target with AttDef 4 on level ground from the front:
   today `max(1, round((34 - 4) × 0.5)) = 15`. Blue holds the shrine (+15%): raw `round(34 × 1.15) = 39`,
   damage `round((39 - 4) × 0.5) = 18`.
4. *Edge: a guardian with nobody in reach at home.* It begins its turn home with no target: it heals to
   full, statuses go, and it ends its turn without an order (an `EndTurn` from `FNeutralPlayer`).

**Tunables** (new `FTuning` fields, so Developer Tools and `Tune` orders reach them, and they are in the
checksum through `Tuning`): `ObjectiveCaptureScale` (1.0, multiplies every site's capture time),
`GuardianLeash` (8 m), `ShrinePercent` (15), `WellPercent` (4), `TowerRadius` (14 m). Per-site numbers
in the map file override the defaults.

## 6. Iteration order and ties
- Sites in the order the map file lists them, blue's copy before red's twin.
- Units in `Units` order (id order). Guardians are appended **after** both sides' units at battle start,
  site by site, so today's ids 0-7 never move.
- Guardian target ties: most recently damaged it → nearest → lowest id.
- Reliquary item goes to the capturing side's unit nearest the site; ties by lowest id.
- Events fire in the order listed; an event and a capture on the same tick: events first (the ground
  changes, then who stands where is counted).

## 7. Edge cases
| Situation | What happens |
|---|---|
| Unit knocked out inside a ring | Not counted from that tick. |
| Guardian knocked out by a burn tick | Gone at once; the side whose ability put the burn on it is named as the side that beat it. |
| Guardian taunted / rooted / stunned | Obeys the status like any unit (the computer player already respects Root, Freeze, Knockdown, Taunt). |
| Guardian's target leaves the leash | It drops the target; a new one or home. |
| Guardian pulled onto embers | Burns like anyone. It doesn't avoid hazards in this version. |
| Site wakes with a unit standing on it | Guardians are placed on the nearest free nodes to home (search in node order), never on a unit. |
| Fire line fires under a unit | Allowed: the unit simply burns at its next turn start. It is never moved. |
| Collapse under nobody | Always safe: rock had nobody on it. |
| Fog bank over a watchtower | The tower sees from outside the bank; units inside the bank see 4 m. The tower's reveal of veiled units still works. |
| Both sides would finish capture on one tick | Can't happen: capture only moves while one side is alone. |
| Time limit ends while a site is owned | Nothing: `HealthShare` counts teams 0 and 1 only. |
| Map without `objectives`/`events` | Nothing new exists; the battle is bit for bit today's. |
| `Standing[Unit.Team]` style arrays | **Every `[Team]` index must be audited**: `TickCapture`'s `Standing[2]`, `CaptureTicks[2]`, `SpawnPoints[2]`, `PlanningDone[2]`, and the director's per-side HUD lists. Team 2 must be skipped there, not indexed (an out-of-bounds write, otherwise). |

## 8. Godot quirks
None: Godot has no objectives. Maps without them must stay exactly Godot's game, and the parity tests
prove it by passing unchanged.

## 9. State and events
**Map file** (`*.tmmap.json`, still version 1; two new optional keys, refused if malformed, as now):
```json
"objectives": [
  {"kind": "watchtower", "at": [30.75, 18.75], "appears": 20, "capture": 3, "radius": 14},
  {"kind": "shrine", "at": "centre", "appears": 45, "holds": 45, "respawn": 60,
   "guards": [{"class": "stone_warden"}]},
  {"kind": "reliquary", "at": [8.75, 30.25], "appears": 90, "item": "blink_stone",
   "guards": [{"class": "ember_drake"}, {"class": "ember_whelp"}]}
],
"events": [
  {"kind": "collapse", "tiles": [[17, 13], [18, 13]], "at": 120},
  {"kind": "fire_line", "tiles": [[10, 14], [11, 14], [12, 14]], "at": 60, "lasts": 30, "warn": 10}
]
```
**New state (all in `Checksum()`, one `SimPlayTest` probe each):** per site: kind, state
(dormant/active/owned), owner, capture progress and whose, timer (ticks to wake / hold / respawn), last
side to hit each guardian; guardian units are ordinary `FUnit`s (already checksummed) plus `Home` and
`LastHitBy`; the ground overlay (tile → temporary kind, ticks left); event timers.

**Events for the view:** SiteWarning, SiteAwake, GuardianBeaten(side), CaptureProgress(site, side,
share), Captured(site, side), SiteDormant, EventWarning(tiles), GroundChanged(tiles, kind).

## 10. Proof (Feature mode, no Godot reference)
- **Unchanged battles:** `SimTraceTest` (14 Godot battles, 1667 decisions) and every other parity test
  pass untouched. This is the test that matters most.
- **`SimObjectiveTest`** (new, standalone, no engine): each worked example above as a scripted battle;
  capture speed with 1-4 units; stealing; contested; knocked out in the ring; guardian aggro, leash,
  reset-heal and slot choice; team 2 absent from `CheckWinner`/`HealthShare`/capture; tower vision
  changes `CanSee`; events never put rock or water under a unit; a map with a site on the centre gets one
  site, off-centre gets two.
- **Mirror fairness:** the map analyser plays 200 guardian-map battles alternating which side is listed
  first; blue's win share must be within what the same map *without* objectives gives (compare against
  the map, not 50%, per `sim-side-bias`).
- **Determinism:** `SimPlayTest` replays an objectives battle from its orders (guardian orders included)
  to the same checksum; the wrong-seed probe still fails; `Tests\OnlineTest.bat` on an objectives map
  (the host runs `FNeutralPlayer`, the joiner only applies).

## 11. Player-facing feedback
- Sites on the board: a tower/shrine/well/totem model per kind, greyed while dormant, in the owner's
  colour when owned; the ring drawn as a ground decal (the existing indicator material) filling with the
  capturing side's colour.
- Top of the screen: a small icon per site with its state and a countdown ("Shrine 0:12").
- Guardians: an amber outline (neither blue nor red), their leash circle shown while one is selected.
- Watchtower vision: the fog lifts in the tower's circle for the owner, with a faint beam from the tower.
- Log lines: "Blue beat the Stone Warden." "Red is taking the Watchtower." "Blue holds the War Shrine
  (+15% damage, 45 s)." "The east wall will collapse in 10 s."
- The computer player: first version values a capturable site like an enemy worth walking to when no enemy
  is in reach, and avoids guardians' leash unless it means to fight them (scoring to be written in the
  tech spec; measured with the class lab, not Godot).

## 12. Open questions for the human
- **Should being hit interrupt capturing** (Dota's outposts) or only being knocked out (as written)?
  Interrupting rewards harassment; not interrupting keeps capture simple in a continuous-time game.
- **Which kinds first?** Suggested order: watchtower (no guardian needed, uses `CanSee` only) → guardians
  + shrine → events → reliquary (needs `feat-items`).
- **Should objectives be a setup-screen switch** ("Objectives: on/off") so the same map can be played
  plain, or always on when the map has them?
- Guardians and **hold the middle**: should an active guardian on the centre site block the middle from
  being held (it stands there)? Written: no, guardians are ignored by that rule.
- Online and the class lab both need to know guardian classes: send them with the battle as class files
  are sent now (written that way).

## 13. Built: the watchtower (2026-09-29)
The human's choices, which replace the watchtower parts of sections 3-5 where they differ:
- **Capturing costs turns, not seconds.** A unit standing next to a tower (within 2.5 m, on ground it could
  step to from the tower's) spends its turn on a **Capture order**. It is the turn's action and then the end
  of the turn: it may walk there first, but not act as well. After **N** such turns by its side the side
  holds the tower. N is the rule number `watchtower_turns` (default 2, 1-6), a slider in **Developer Tools**
  ("Watchtower capture (turns)"), changed mid-battle by a Tune order like any rule.
- Refused when: too far, an enemy is standing at it (contested), the side already holds it, the unit has
  acted, or the order is for another turn. Progress belongs to one side at a time: the other side starting
  on it wipes the first side's progress out. A held tower is taken the same way, N turns, straight over.
- **Placed at random, fairly.** The rules place them when the battle starts, from the battle's seed through a
  generator of their own (`FSimRandom` salted with "WATCHTOW"), so the battle's dice are untouched, and both
  machines of a match and a replay place the same towers. They come in **mirrored pairs**: a tower on a tile
  of blue's half and its twin turned about the middle on red's, as the ground itself is. An odd one stands
  on the exact middle if that ground is open, else the battle has one fewer. Every tower: on walkable,
  hazard-free, rock-free ground both sides can walk to; at least 12 m from where anybody starts; at least
  10 m from every other tower (a pair's two included).
- **How many is chosen on the setup screen** ("Watchtowers", 0-8; the rule number `watchtower_count`, not
  offered in Developer Tools). 2 the first time a person opens the setup screen; 0 for a battle nobody set
  up, which is therefore still Godot's battle. `-tmtowers=N` on the command line. Online, the host sends the
  count with the battle; protocol version 2.
- **Vision:** a side holding a tower sees every spot within `watchtower_sight` metres (default 14, 4-30, a
  Developer Tools slider) that has line of sight from 4 m above the tower's ground. It goes through
  `CanSee`, so fog, "You can't see that spot." and the computer player's positioning all follow.
- **The computer player** (divergence from Godot, as Root/Freeze/Taunt are; nothing runs without towers):
  with nothing worth hitting it captures if it is standing at a tower it can take; with no enemy in sight
  it walks (not sprints: the capture needs the action) to the nearest tower its side does not hold.
- **In the game:** a stone tower with a roof and banner in the holder's colour (grey for nobody); a ring on
  the ground showing the reach, in the holder's colour; "Blue 1/2" over a tower part-way taken; a
  **CAPTURE** button on the action bar (lit when the rules would take it now, otherwise its tip says why);
  log lines for each turn spent and each tower taken.
- **Tests:** `SimWatchtowerTest` (placement on every map, 1800 battles: mirrored, legal ground, spacing,
  same seed same places, the dice untouched; capture cost at 2 and 3 turns and every refusal; vision only
  what the tower sees; the order as text; 15 computer-vs-computer battles, every order legal, towers taken,
  each replayed to the same checksum). `SimPlayTest`: five more checksum probes. Every Godot parity test
  passes unchanged.
- **Room:** Highlands (12x12) has room for 2; the 30+ maps take all 8; Crown Keep 6; Frostwall Town's middle
  is its fountain, so an odd count there loses the middle tower.
