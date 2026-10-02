# Codebase Overview — Tactical Masters (UE 5.8)

*Written from a read of the source on 2026-09-26. `SimResolve.cpp` and `SimBattle.h` were being
edited while this was written, so re-check those two against this page.*

## 1. Shape of the project

```
Godot project (D:\ProgramsByMe\TacticalMasters)      ← source of truth for rules + class data
        │  ported by hand, line-referenced             │  dumps golden tables
        ▼                                              ▼
Source/TMSim  (rules, plain C++17)  ◄──── parity ──── Tests/ (standalone MSVC, RunTests.bat)
        │  FBattle, FOrder, FTickReport
        ▼
Source/TacticalMasters  (ATMBattleDirector: view + order door)  ◄── Tools/play_battle.py (wiring test)
        │  meshes, lights, tiles
        ▼
Content/Maps/Showcase.umap   (built by Tools/build_showcase.py)
```

- `TacticalMasters.uproject`: EngineAssociation 5.8. Modules `TMSim` (Runtime, PreDefault) and
  `TacticalMasters` (Runtime). Plugins: RemoteControl, PythonScriptPlugin.
- Targets: `TacticalMastersEditor` / `TacticalMasters`, `BuildSettingsVersion.V7` (required by the installed engine).
- The last recorded build (`build.log`) succeeded: 13 actions, about 48 s, MSVC 14.44, UBA local executor.

## 2. TMSim: the rules

| File | Contents |
|---|---|
| `SimTypes.h/.cpp` | `FVec2` (float, Godot-matching), `EStat`, `FStatusDef` table (all statuses as data, from `Jobs.STATUSES`), `Pace` (10 ticks/s, TG 4000), `FTuning` (doubles = `GameState.TUNING`), `Combat` constants, `RoundToInt`. |
| `SimRandom.h` | `FSimRandom`: bit-exact Godot 4 PCG32 (XSH-RR). `Randi`, `RandiRange`. No `Randf` yet, on purpose. |
| `SimUnit.h/.cpp` | `FUnit`: position, facing, HP, TG, ready/clock/serial, ult, statuses, buffs, cooldowns, casting, channelling, toggles. Stat queries fold in buffs and status factors. |
| `SimAbility.h/.cpp` | `FAbility`, `FJobDef`, registry (`FindJob`, `FindAbility`, `JobAbility`, `AllJobs`). Currently holds only the **6 built-in jobs**, hand-written from `jobs.gd`. |
| `SimMap.h/.cpp` | `FMap`: rows of characters → heights/hazards/cover, mirrored build, 2 m tiles split into 4×4 nav nodes of 0.5 m. `HighlandsRows()`. |
| `SimOrder.h` | `FOrder`: Advance / Move / UseAbility / EndTurn, carrying `Serial`. |
| `SimBattle.h/.cpp` | `FBattle`: the clock (`Start`, `Tick`, `Advance`), gauge math, readiness, end of turn, `CalcAmount`/`FlankBonus`/`EvadeChance`/`CritChance`. |
| `SimMovement.cpp` | Dijkstra over nav nodes (transcribed for identical tie order), `ReachableNodes`, `PathTo`, `ValidateMove`, `ApplyMove`, engagement cost. |
| `SimTargeting.cpp` | Shapes (unit/point/circle/self/line/cone/global/vector), range, `Preview` (unit-id order). `NeedsLineOfSight` is defined here but nothing calls it yet. `Preview`/`InAbilityRange` don't check sight. |
| `SimResolve.cpp` | `UseAbility` (pay, toggle, channel, or start cast), `ResolveAbility` (**the only place dice are rolled**), `Hurt`, shields, `AddStatus`, `KnockOut`, `StunInterrupt`, `CheckWinner`. |
| `SimWorld.cpp` | `Validate`/`Apply` (the one door), sight (`CanSee`, `SightOf`, `HasLineOfSight`), ground queries, distance fields, `Checksum()`. |
| `SimAI.h/.cpp` | `FAIPlayer`: a player that only returns orders. Difficulty via `FSkill`. Own `Rng`. Currently positions only (approach/retreat/ground value). |

**One battle step:** director (or test) → `FOrder` → `Validate` → `Apply` → state changes + `FTickReport`
events → view plays the events back. In tests, time passes through `Advance` orders. The director
currently calls `Battle.Advance()` directly (`StepTicks`, `Tick`), outside `Submit` (see F8).

## 3. TacticalMasters: the view

`ATMBattleDirector` (actor, placed in `Showcase`):
- `BuildBattle()` builds the Highlands map, 8 units (4 per team) from built-in jobs, board tiles
  (`UStaticMeshComponent` per tile), a skeletal mesh per unit, and a point light per unit that shows only while that unit is READY.
- `Tick` accumulates real time and steps the sim at 10 ticks/s (only in a game world, so it doesn't run in the editor).
  `RefreshVisuals` snaps meshes to sim positions.
- `Submit(FOrder)` (private) is the door for human and AI orders, and later network orders. Time steps bypass it (F8).
- `CallInEditor` controls (`StepTicks`, `MoveUnitTo`, `EndUnitTurn`, `TakeComputerTurn`,
  `PlayComputerTurns`, `DescribeBattle`) let it be driven without Play-In-Editor, which is how
  `Tools/play_battle.py` tests the wiring.
- Scale: `TileSize` (default 100) is really **Unreal units per sim metre**. See finding F1.

No input handling, HUD, camera controller, animation state or event playback exists yet. Units teleport to their positions.

## 4. Data and content
- `Content/Data/Classes/*.astra.json`: 81 Astra exports (one `profile` ability with stats + 4
  `slot:N` abilities). **Not read by anything yet.** Waiting on the `astra_import.gd` port.
  About 30 KB each. README documents two conversion quirks (`power` folding, `buff_power` → Crit ×2).
- `Content/Data/CharacterMap/`: empty. Will hold class → body/material mapping.
- `Content/Characters/`: canonical-skeleton layout per `Docs/CharacterPipeline.md` (Skeleton, Animations,
  Retargeted, Custom, Materials are empty placeholders; Mannequins present).
- `Content/ParagonSparrow/`: first Paragon hero (gitignored, re-downloadable).
- Maps: `Content/Maps/Showcase.umap`.

## 5. Tests
`Tests\RunTests.bat` compiles each test with `cl /std:c++17 /W4` straight from the TMSim sources
(no UBT, no engine) into `%TEMP%\tmsim_tests`, and exits 1 on any failure. A compile failure stops the run
immediately. Run time is seconds.

| Test | Checks | Golden data |
|---|---|---|
| `SimRandomTest` | PCG32 outputs for seeds 1/42/12345 | inline |
| `SimTickTest` | gauge/ready/countdown per unit per tick, 400 ticks, no orders | `GodotTickTrace.txt` |
| `SimCalcTest` | amount/evade/crit for every built-in ability × class pair, heights, facings | `GodotCalcTable.txt` (≈790 KB) |
| `SimMoveTest` | ground, reachability, costs, exact paths | `GodotMoveTable.txt` |
| `SimAITest` | AI spot choice in 4 states (opening, contact, hazards, springs) | `GodotAITable.txt` |
| `SimPlayTest` | full AI-vs-AI battle via orders, replayed from the order list, checksums compared once at the end; 27 probes that the checksum notices single-field changes | self |

Golden tables come from `tests/dump_*.gd` scripts in the Godot project.

## 6. Porting status

| Area | State |
|---|---|
| Dice, clock (no orders), calc, ground/movement/paths, AI positioning | Ported, parity-tested |
| Order path, replay, checksum | Ported, self-tested |
| Targeting shapes/range, resolution, statuses, shields, KO | Ported, **no Godot parity test yet** |
| Abilities as orders | `Validate` returns "Abilities cannot be ordered yet." Cooldowns are already set (`UseAbility`), counted down (`BecomeReady`) and checked (`AbilityBlockedReason`), but can't be reached yet. |
| Cast countdown | `UseAbility` starts casts, but `Tick` never advances `Casting.Ticks` (`bWasCasting` is hard-coded `false`; `TicksToReady` uses 0). A cast would never land. Must ship together with abilities-as-orders. |
| Start-of-turn effects | Statuses ticking/expiring, hazards, regen, auras, channel continuation: not ported (`BecomeReady` says so). |
| Passive/aura stat contributions | Not ported (`FUnit::Stat`). |
| Class importer (`astra_import.gd`) | Not ported, so only 6 of 87 classes are playable. |
| AI ability use, `Randf` tie-break | Not ported. |
| Ultimates | Meter exists; use not ported. |
| View: input, camera, HUD, event playback, animation | Not started. |
| Networking | Designed for (serials, checksums, `Submit`), not started. |

## 7. Findings from this read

None of these break the build or the tests today.

- **F1. Scale naming.** `ATMBattleDirector::TileSize` says "one tile is one metre in the rules", but the
  sim's tile is 2 m (`Ground::TileSize`). The code multiplies metres by it, so it is actually
  cm-per-metre and the board is correct. `Tools/build_showcase.py` has the same stale "one tile is one
  metre" comment on `TILE = 100.0`. That no longer affects anything, because the director builds the board now.
  *Suggest:* rename the property to `UnitsPerMetre` and fix both comments.
- **F2. Stale comments.** `SimBattle.h` (`CalcAmount`) and `SimCalcTest.cpp` say the map is not
  ported, but it is. In `SimBattle.h` the doc comment above `AbilityBlockedReason` belongs to
  `Validate`, and `ResolveAbility`'s comment is stacked above `UseAbility`'s.
- **F3. RunTests.bat duplication.** The TMSim source list is repeated in 5 compile lines, and
  `E:\VS2022` is hard-coded. A new `.cpp` must be added 5 times. *Suggest:* one `SIM_SRC` variable.
- **F4. Class data drift.** Nothing checks `Content/Data/Classes` against the Godot copy (the README
  says so). *Suggest:* a sync test when the importer lands.
- **F5. Packaging.** `Data/Classes` must be added to *Additional Non-Asset Directories to Package*.
  There is no `DefaultGame.ini` yet, so it isn't set.
- **F6. Untested resolution.** Resolution holds the dice order, the part that desyncs matches. It
  should get a Godot parity dump (per-hit amount/evade/crit/status and RNG state after) before abilities become orders.
- **F7. Checksum gaps.** `Checksum()` (`SimWorld.cpp:185`) leaves out the battle RNG state, `KoTicks`,
  `bHustling` (which changes gauge speed) and `UnharmedTurns` (which will drive regen). Two machines could
  differ in any of these and still agree until it shows up somewhere else. This matters most for the RNG once
  abilities roll dice. *Suggest:* mix them in, and add a probe for each to `SimPlayTest`.
- **F8. Time isn't an order in the view.** `ATMBattleDirector::StepTicks` and `Tick` call `Battle.Advance()`
  directly, not through `Submit(FOrder::MakeAdvance(n))`. The sim and tests are fine, but a recorded
  replay or a networked match would need the Advance steps in the order list. *Suggest:* route time through
  `Submit` before replays or networking are built on the director.
- **F9. SimPlayTest is weaker than its header says.** Its header describes comparing after every step and a
  wrong-seed probe. The code compares checksums once, after the full replay, and has no wrong-seed probe.
  *Suggest:* compare per step, and add the seed probe once abilities roll.
- **F10. Serial check bypass.** `Validate` only checks `Serial` when it is `>= 0`. That's fine for tests,
  but player/network orders must never carry `-1`.
