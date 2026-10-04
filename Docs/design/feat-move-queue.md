# Queued orders: plans and waypoints

2026-10-01. Asked for: "Add feature where movements can be queued up." The mockups ("Move Queue Mockups" canvas)
showed five ways. The human picked A, B and C.

## A · Plan ahead

- Click one of your units while it waits for its turn (on the board or its turn square). That selects it for
  **planning**: the walk area shows, as for a turn.
- Click where it should walk. It is noted, not done: a dashed gold line to a gold ring. Then an ability (1-4, or the
  bar) can be aimed **from that ring**, and a dashed violet line marks what it will hit.
- The moment its turn begins, the plan runs: the walk, then the ability on arrival, then the turn ends. A walk-only
  plan walks, and the turn is then the player's.
- If by then the plan can't be done (spot taken, out of reach, target gone, stunned), it is dropped with a word why,
  and the turn is the player's as usual.
- A turn of the player's that begins while they plan a waiting unit takes over (its clock is running). What was
  planned is kept.
- Its turn square carries a gold **P** while it has a plan.

## B · Move, then act, in one go

- In a unit's own turn, the plan key (**G**) plans the turn instead of ordering it: the walk is set without being
  done, and the ability is aimed from where the walk ends, so the preview shows who it will really reach.
- **G** again (or Go on the strip) does both, back to back. A plan made inside a turn that ends without Go is dropped.
- The plan strip above the action bar shows the steps (Walk 5.2 m > Fireball > End turn) and has Go/Done, Undo and
  Clear.

## C · Waypoints

- Hold **Ctrl** and click to add a waypoint to the walk being aimed (planned or not). Up to 4. The walk area then
  shows what is left from the last waypoint, and the preview gives the metres used of the move.
- **Backspace** takes back the last waypoint; Esc or a right click drops them all.
- Still one walk: its whole length counts against the unit's move. Walking by a cache picks its items up on the way.

## Go To (several turns)

2026-10-01. Asked for: "the ability to queue up multiple moves like Civilization 3." The "Multi-Turn Move Mockups" canvas;
the human chose walk-and-end as the default and single trips for now.

- Click beyond the walk area (not on a unit) and the unit is sent there: each turn it walks as far as its move allows
  along the way, then **ends the turn**. Its strip can switch that to **walk, then wait for me** (the turn is the
  player's after each walk). The turn it arrives on is always the player's.
- Before the click the route shows with a ring and number where each turn's walk ends, and the preview says how many
  turns and metres. Once set, the route stays on the ground (bright for the selected unit, faint for others) and the
  unit's turn square carries ">N", the turns left.
- It **stops** and hands the turn back when an enemy comes into sight that it could not see when told, when it has
  been hurt since it last walked, or when there is no way any more or the way is blocked. The strip says why;
  **Keep going (G)** carries on, **Backspace** or Cancel ends it. Any other order from the player replaces it.
- Next unit and auto-select skip units that are marching. Ctrl+click waypoints work for a Go To as for a walk.
- This machine's only, like plans: each turn sends an ordinary walk (and end turn), so nothing new goes online.
- Code: `FTMGoTo`, `GoTos`, `SetGoTo`/`StepGoTo`/`StopGoTo`/`CancelGoTo`/`RunDueGoTos` in TMBattleDirectorPlans.cpp;
  `FBattle::RouteTo` (an unbounded way by the waypoints) in SimMovement.cpp; the ground route in `PaintPlans`, the
  numbers in `ATMBattleHud::DrawGoToMarks`, the strip in `DrawGoToStrip`.

## Polish (2026-10-03)

Asked for: "it's clunky ... I feel like I wasted a turn if the last movement of the queue only moves a short distance
ending my turn"; "the queue system needs to also work for abilities that are out of range"; "when units are queued up
and they encounter another unit in their sight range, the queue should be cancelled".

- **No turn spent on a step.** When a Go To's walk this turn would be short -- the last stretch, or as far as a blocked
  way lets it, more than max(1 m, 15% of the move) short of a full move -- the unit is handed to the player *before*
  it walks, the strip saying why. **Keep going (G)** walks it; any other order replaces it.
- **Abilities out of reach.** Aiming an ability at a target the unit can't get in range of this turn shows, in orange,
  "OUT OF RANGE: ... Click to go into range (N turns, X m), then use it there", with the way and the turn rings on the
  ground. A click makes a Go To carrying the ability (`SetAbilityGoTo`): each turn it walks towards the nearest spot
  the ability can be used from (in range, in sight where it needs it, not stood on: `ApproachRoute`), worked out again
  each turn from where a unit target stands then; the turn it can use it from where it stands, or from a spot this
  turn's walk reaches, it does (`StepAbilityGoTo`), and the order is over. A unit target that dies or goes out of sight
  ends it. Its strip reads "Into range for <ability>". Planning a waiting unit makes the same order.
- **Seeing an enemy cancels.** Each frame (`CancelQueuesOnSight`), a unit with a Go To, or a plan for a coming turn,
  that has an enemy come into its own view (within its sight, nothing in the way, and seen; not monsters in their
  camps; not the unit an ability Go To is going after) has the order cancelled -- not stopped -- and is handed back.
  If it saw it on a walk-and-end step, the turn is not ended: the unit can still act. Being hurt still only stops a
  Go To.
- A Go To's road and turn rings show only while its unit is pointed at, on the board or on its turn square
  (`GoToShown`); the ">N" on its turn square stays. The way under the pointer before a click still shows.
- Ground fills (walk area, ranges, areas, zones, wind-ups) keep 30% of their opacity: nearly see-through, the edges
  carrying the shapes (`GroundFillOpacity`, TMBattleDirectorIndicators.cpp).

## Rules (TMSim)

- `FOrder::Via`: up to `MaxWaypoints` (4) node centres. Order text: `move <unit> <serial> <x> <y> <sprint>`, then,
  only when there are any, `<count> <x1> <y1> ...`, so old orders and replays read the same.
- `ValidateMove` walks the legs (`WalkVia`): each a shortest way from the last spot with what is left of the move,
  engagement included, so a walk by waypoints is never longer than the unit's move. `ReachableVia`, `PathVia`.
- `ApplyMove` faces the last step of the whole way, picks up at each waypoint, and keeps `FUnit::WalkVia` and
  `WalkFrom` (screen only, not in the checksum) so the walk is shown going the way it was told.
- Online protocol 12.
- 2026-10-03, facing on arrival: `FOrder::Face`, written last as `f <way>` (0-7) when a walk was told which way to
  face (Docs/design/feat-combat-feel.md). Protocol 16.

## The game (TacticalMasters)

- `TMBattleDirectorPlans.cpp`: `FTMPlan`, `Plans` (this machine's only; what goes out is ordinary orders),
  `FPlanStandIn` (while aiming a plan, the unit stands where its walk ends and as at the start of its turn, then is put
  back), `RunDuePlans` each frame.
- Keys (Options): Waypoint (Left Ctrl, held), Plan a turn / go (G), Undo the plan's last step (Backspace).

Tests: `SimMoveTest` (waypoints: costs, refusals, the way, the walk), `SimOrderTextTest` (round trips, malformed
waypoints).
