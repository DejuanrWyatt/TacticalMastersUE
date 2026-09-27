# feat-player-input: a person plays blue

Status: Implemented ahead of approval, at the user's request (2026-09-27). Needs a play-through by a human.

## What it does
In Play, the person at the machine plays blue (team 0) and the computer plays red. It follows the Godot
game's `scripts/battle/battle.gd`, cited line by line in `TMBattleDirector.cpp`.

| Input | Does | Godot |
|---|---|---|
| Left click on a ready blue unit | select it (walking is offered at once) | `_on_click` :796-798, `_select_unit` :912 |
| Space | walk: blue dots are every spot it can reach; click one | `_toggle_move` :966 |
| Shift | sprint: further, orange dots, no ability after | `_toggle_sprint` :977 |
| 1-4 | aim that ability; click a unit or the ground | `_select_ability` :990, `_aim` :810 |
| Enter | end the turn | `_on_end_turn_pressed` :1026 |
| Tab | next ready blue unit | `_cycle_ready` :940 |
| Esc / right click | cancel the aim | `_cancel` :1009 |
| P | pause (local only) | `_toggle_pause` :1030 |
| R | a new battle, once one is decided | (match flow, early) |

After an order the next step is offered as Godot does (:468-485). A unit that has acted and walked (or
started a cast) has its turn ended for it. A turn that runs out while the person thinks is lost, as in Godot.

## Rules it keeps
- Every order goes through `Submit` with the unit's real `Serial`. Nothing new is rules state.
- Whether an aim is allowed, and why not, comes from `FBattle::ValidateAbility` / `AbilityBlockedReason`,
  so the words shown are the words a refused order gets.
- The forecast over each target is `FBattle::Preview` plus `EvadeChance`, the same numbers resolution uses.
- Pause stops Advance orders being sent. It is not an order. Online it would need both machines to agree.
- `-unattended` or `-tmwatch` gives both sides to the computer, so headless runs still end themselves.

## Not done yet (named where the gap is)
- Walk-into-range-then-fire (`_walk_into_range`, :840). Out of range just says so.
- Fog of war in the view: hidden enemies are still drawn.
- Board markings use debug drawing and the panel uses on-screen debug messages. Both are absent from a
  Shipping build. The HUD slice (`feat-hud-forecast`) replaces them.
- Camera movement (WASD/QE in Godot) and centring on a unit.
