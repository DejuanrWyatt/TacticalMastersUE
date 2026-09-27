# feat-hud-forecast: the battle HUD

Status: Implemented ahead of approval, at the user's request (2026-09-27). Seen in screenshots; aiming and
the forecast not yet seen, because nobody clicks in a captured run.

## What it shows
Laid out as the Godot HUD (`scripts/battle/hud.gd:2-13`). `ATMBattleHud` draws it on the canvas every frame,
so it needs no editor-made assets and is present in a shipping build. It reads the director and decides
nothing. A click on one of its buttons goes back to the director, which turns it into an order.

| Where | What | Godot |
|---|---|---|
| Top | Turn order: a bar per team. Ready units sit in the gold zone at the left. The rest slide along a square-root time scale to 30 s. Outlines are gold (ready), red (5 s or less), purple (casting) and white (selected). Clicking a chip selects that unit. | hud.gd:52-74, 307-311, 1088-1107 |
| Left | The last 10 lines of the log, older ones fading | log window |
| Bottom left | The unit card: name, READY countdown or "ready in", statuses, HP/TG/ULT gauges, stats. It shows the selected unit; while watching, whoever's turn it is; otherwise the unit under the pointer. | hud.gd:1390-1427 |
| Bottom centre | The action bar: Move, Sprint (with its distance), the four abilities and End Turn. Each ability shows its key, then the ULT %, the wait or the cast time. A blocked ability is dimmed and a ready ultimate is gold. | hud.gd:866-909, 1429-1474 |
| Above the bar | The hover preview: an ability's shape, range, cast, radius and cooldown; "Walk here: x m of y m"; or why an aim is refused | hud.gd:1523-1536 |
| On the board | Walking spots, the path, range rings, the aim ring (green or red), and over each target the damage and miss chance (or healing), plus "KO" if it would fall | battle.gd:1394-1440 |
| Centre | Pause banner. The game-over panel with an "Another battle" button. | hud.gd:911 |

The forecast numbers are `FBattle::Preview` and `FBattle::EvadeChance`, the numbers the dice are rolled
against. The blocked reasons are `AbilityBlockedReason` and `ValidateAbility`.

## Test switches
- `-tmplayblue`: blue stays with a person even in an unattended run. Nobody clicks, so red wins and the run
  ends. Used with `-tmcapture=N` to take pictures of the player's HUD.
- An unattended run now waits 1.5 s after the battle is decided before exiting. It takes `frame_end.png`
  when capturing.

## Not done yet
- Godot merges turn chips that overlap into a framed group. Here they overlap.
- Class icons: chips show two letters of the class name.
- The movable/resizable log window, the Units list, the stats card of a clicked enemy, "incoming" casts on a
  card, tooltips that explain each calculation, and the menu, Options and Unit Guide overlays.
- The board markings are drawn over the scene, so they are not hidden behind raised tiles.
