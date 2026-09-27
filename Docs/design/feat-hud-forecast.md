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

## Added in the second pass (2026-09-27)
| What | Godot |
|---|---|
| Turn chips that would touch merge into a framed group, centred on their average time. Far-off chips are smaller. Chips glide instead of jumping. A time shows only when ready, casting, within 3 s, or under the pointer. | hud.gd:1226-1387 |
| Corner buttons: Log, Field, Units (the guide), Pause, Menu | hud.gd:608-623 |
| Log window: L or the button shows it, + makes it taller, the wheel scrolls back | log window |
| Field list: every unit with name, HP bar and state. Click a row to pick or inspect that unit. | hud.gd:515-606 |
| Inspect card: click a unit that isn't taking orders. It shows its gauges, stats and four abilities coloured by what they are for, with a legend. For an enemy, its reachable ground and longest attack range are drawn on the board. | hud.gd:708-863, battle.gd:509-527 |
| Incoming: on both cards, who is casting what at that unit and how long is left | hud.gd:1112-1165 |
| Tooltips that explain the numbers: turn gauge, countdown, move, sight, HP, ultimate, each ability, and buffs | game_state.gd:952-1016 |
| Unit Guide (U, or the Units button; also on the title screen): every class's stats; one class's abilities worked out against a chosen target by the rules' own `CalcAmount` and `EvadeChance`. It pauses a local battle while open. | unit_guide.gd |
| Fog of war: against the computer, units your side can't see are hidden on the board, "?" on the turn order and in the field list, and give no damage numbers | battle.gd:302-307 |

## Test switches
- `-tmplayblue`: blue stays with a person even in an unattended run. Nobody clicks, so red wins and the run
  ends. Used with `-tmcapture=N` to take pictures of the player's HUD.
- An unattended run now waits 1.5 s after the battle is decided before exiting. It takes `frame_end.png`
  when capturing.
- `-tmhudshots` (with `-tmplayblue`): once an enemy is in sight, pauses and opens the field list, that
  enemy's card and the Unit Guide, taking `hud_panels.png` and `hud_guide.png`.

## Not done yet
- **Class and ability icons.** Godot's are SVG files, which Unreal can't load at runtime, and turning them
  into texture assets is editor work. Chips and rows show two letters instead.
- **Options** (key rebinding, turn-order style, team colours) and **Developer Tools** (tuning numbers live).
- Dragging and resizing the log window. It can be shown, made taller and scrolled.
- Fog shading on the ground itself. Hidden units are hidden, but unseen tiles aren't darkened.
- Editing class stats from the Unit Guide. Godot allows this from the main menu.
- The board markings are drawn over the scene, so they aren't hidden behind raised tiles.
