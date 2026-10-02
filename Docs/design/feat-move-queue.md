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

## Rules (TMSim)

- `FOrder::Via`: up to `MaxWaypoints` (4) node centres. Order text: `move <unit> <serial> <x> <y> <sprint>`, then,
  only when there are any, `<count> <x1> <y1> ...`, so old orders and replays read the same.
- `ValidateMove` walks the legs (`WalkVia`): each a shortest way from the last spot with what is left of the move,
  engagement included, so a walk by waypoints is never longer than the unit's move. `ReachableVia`, `PathVia`.
- `ApplyMove` faces the last step of the whole way, picks up at each waypoint, and keeps `FUnit::WalkVia` and
  `WalkFrom` (screen only, not in the checksum) so the walk is shown going the way it was told.
- Online protocol 12.

## The game (TacticalMasters)

- `TMBattleDirectorPlans.cpp`: `FTMPlan`, `Plans` (this machine's only; what goes out is ordinary orders),
  `FPlanStandIn` (while aiming a plan, the unit stands where its walk ends and as at the start of its turn, then is put
  back), `RunDuePlans` each frame.
- Keys (Options): Waypoint (Left Ctrl, held), Plan a turn / go (G), Undo the plan's last step (Backspace).

Tests: `SimMoveTest` (waypoints: costs, refusals, the way, the walk), `SimOrderTextTest` (round trips, malformed
waypoints).
