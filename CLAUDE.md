# Tactical Masters (Unreal port) — Project Brief for Agents

Tactical Masters is being **ported from the Godot version** to Unreal Engine 5.8. The rules are being
rebuilt as a deterministic, engine-free C++ simulation, and Unreal is only the view.
Read this whole file before doing anything. `Docs/tech/_overview.md` explains the code in more depth.

## The one idea everything rests on
**A battle is exactly the list of orders applied to it.** Same seed + same orders = same battle,
bit for bit, on every machine. Online matches compare checksums every step, and replays re-run
recorded fights. Anything that breaks determinism breaks multiplayer, often seconds after the
mistake and somewhere else. When in doubt, preserve determinism over elegance or speed.

## Source of truth
- **Rules:** the Godot project at `D:\ProgramsByMe\TacticalMasters` (`scripts/core/game_state.gd`,
  `unit.gd`, `jobs.gd`, `ai_player.gd`, `astra_import.gd`, …). Until a rule is ported *and* verified
  against Godot, Godot is right and the port is wrong.
- **Class data:** authored in `D:\ProgramsByMe\TacticalMasters\data\classes\`, copied (not linked) to
  `Content/Data/Classes/*.astra.json` (81 files + 6 built-in jobs in code = 87 classes).
- Port comments cite the Godot source with line numbers (e.g. `game_state.gd:1730-1867`). Keep doing that.

## Game in one paragraph
Two sides of units fight on a mirrored height map. Time is continuous: every unit's **Turn Gauge**
(TG, max 4000) fills by Speed at 10 ticks/second; a full gauge makes the unit READY with a
Patience-based countdown to act (move and/or use one of 4 ability slots), or lose the turn. Units
stand on 0.5 m navigation nodes (4×4 = 16 per 2 m tile), not tiles. Height, cover and hazards are per tile.
Facing matters (side/back hits). Statuses, casting, channelling, toggles, auras, KO/revive,
ultimates, sight/fog and an AI opponent that plays through the same order path as a human.

## Modules
| Module | What | Rules |
|---|---|---|
| `Source/TMSim` | The rules. Plain C++17, depends on **Core only** (and in practice only the std library). Namespace `TMSim`. | No Engine, no UObjects, no `TArray`/`FString`/`FMath`. Must compile standalone with `cl` (the tests do this). Exports via `TMSIM_API` (fallback defined for standalone). |
| `Source/TacticalMasters` | The view. `ATMBattleDirector` owns an `FBattle`, steps its clock, mirrors units as meshes, submits orders. | Decides nothing about the game. Reads the sim and shows it. Player and AI orders go through `Submit()`. (The clock is currently stepped by calling `Advance` directly, not as an order. See overview finding F8.) |
| `Tests/` | Standalone parity tests, built with MSVC directly, no engine. | See Testing. |
| `Tools/` | Editor Python: `build_showcase.py` (builds the Showcase level) and `play_battle.py` (wiring test). | Run through `UnrealEditor-Cmd -run=pythonscript`. |

## Determinism rules (non-negotiable in TMSim)
1. **The dice are rolled only in `SimResolve.cpp`**, via `FBattle::Rng` (`FSimRandom`, a bit-exact copy
   of Godot 4's PCG32). The roll order is part of the rules: targets in unit-id order (as `Preview` returns
   them); only for **damage** abilities, an evade roll per target, then a crit roll only if it wasn't evaded.
   Healing, revives and support roll nothing. Never reorder, add or skip rolls.
   The AI has its own `FAIPlayer::Rng`. Never share generators.
2. **Match Godot's numeric types.** Positions are `float` (Godot `Vector2`). Tuning values are `double`
   (GDScript float). Round with `TMSim::RoundToInt` (half away from zero, like `roundi`). Do not widen,
   narrow or "clean up" a type without a parity test proving it is identical.
3. **No unordered iteration** in anything that affects state or rolls: no `std::unordered_map`
   iteration, no pointer-address ordering. Iterate units in `Units` order (id order).
4. **Every state change goes through `FBattle::Validate` + `FBattle::Apply`** with an `FOrder`. The AI,
   the player and replays use the same door. The AI must never get a special case inside the rules.
5. **`FEvent`/`FTickReport` are presentation only.** Nothing in the rules may read them back.
6. **Any new state field must be mixed into `FBattle::Checksum()`** (`SimWorld.cpp`), or desyncs go undetected.
   The gaps F7 listed (RNG state, `KoTicks`, `bHustling`, `UnharmedTurns`) are closed, along with casting,
   channelling and toggles. `SimPlayTest` has a probe per field that changes it and insists the checksum
   notices — 31 of them. Add one with any new field.
7. Orders carry the unit's `Serial`, and stale orders are refused. Note that `Validate` skips the check when
   `Serial < 0`, so never send orders with `-1` from player or network paths.

## Code style (match what is there)
- Unreal-style naming (`F` structs, `E` enums, `b` bools, PascalCase) even inside TMSim.
- Tabs for indentation, braces on their own line.
- Comments explain **why** a rule is the way it is and cite Godot. Write plain English; no jargon comments.
  When something is deliberately not ported yet, say so where the gap is. Never leave a silent gap.
- `/W4` clean in the standalone test build, zero warnings in the UBT build.
- Build settings `BuildSettingsVersion.V7` must stay (the installed engine refuses targets that change warning levels).

## Commands (Windows)
| Purpose | Command |
|---|---|
| Rules parity tests (seconds, no engine) | `scripts\test.bat` (wraps `Tests\RunTests.bat`) |
| Build the editor target | `scripts\build.bat` (**close the editor first**, or Live Coding blocks the build) |
| Wiring test: sim ↔ Unreal, in a real level | `scripts\wiring-test.bat` (runs `Tools/play_battle.py` headless) |

Paths on this machine: engine `E:\UE_5.8`, Visual Studio `E:\VS2022`.

## Testing
- **Parity tests** (`Tests/Sim*Test.cpp`) measure the port against Godot, not against itself. Golden
  tables (`Tests/Godot*Table.txt`, `GodotTickTrace.txt`) are dumped from the Godot project by its
  `tests/dump_*.gd` scripts. **Never edit a golden table to make a test pass.** Regenerate from Godot
  only, and say which script and Godot version produced it.
- `SimPlayTest` plays an AI-vs-AI battle through the order path, replays the recorded orders into a fresh
  battle, and compares the checksums **once, at the end**, plus 27 probes that each change one field and
  check the checksum notices. There is no wrong-seed probe in the code yet. Once abilities consume dice,
  add one: replaying with a different seed must fail.
- **Adding a `.cpp` to TMSim means adding it to every compile line in `Tests\RunTests.bat`** that carries the
  sim sources. Five of the six do (`SimRandomTest` compiles alone). A failed compile stops the whole run
  there, so later tests don't run.
- New rules need a parity test from a Godot dump, not only hand-written expectations.
- Unreal Automation Tests are not used. Don't introduce them without asking.

## Porting status (keep this section current)
Ported and parity-tested against Godot: dice (`SimRandomTest`), the clock with no orders
(`SimTickTest`), damage/heal/evade/crit numbers (`SimCalcTest`), ground + reachability + paths
(`SimMoveTest`), AI positioning, including sight (`SimAITest`), what the AI does with a turn
(`SimAIActionTest` — the scoring arithmetic and the search over spots and targets, hard only), and
abilities end to end
(`SimAbilityTest`) — targeting and all eight shapes, the forecast, resolution with the dice in
Godot's order, shields, invulnerability, statuses, gauge changes, buffs, KO, and what an ability
costs to use. The order path + replay + checksum is self-checked (`SimPlayTest`), which now plays a
battle to a decision, and **replaying it with a different seed fails** — the check the determinism
rules rest on.
Not yet ported (see `Docs/backlog.md`): auras and passive/toggle stat contributions (`FUnit::Stat`
says so where the gap is — no built-in ability is one), the Astra class importer, AI ability choice
(and `Randf`), the class-data sync check, the battle time limit and the hold-the-middle rule, and
the director playing the `FTickReport` events back on screen.
Kept bug-for-bug from Godot, with a comment at each: a toggle in slot 3 skips the ultimate-meter and
cooldown gates (`AbilityBlockedReason`, unreachable while no built-in class has a toggle).

**One deliberate divergence**, in the AI's choice of action. Godot sorts its scored options and takes
the first, with `sort_custom`, which is *not* a stable sort — so when two options score identically it
may take either, and no transcription can promise to match that. The port takes the first of equals in
build order. `SimAIActionTest` does not paper over it: where the two disagree it prices Godot's own
choice and requires it to be worth *exactly* what the port's best is worth, so a disagreement is proven
to be a tie rather than a worse decision. It reports how many were settled that way. This costs
nothing for multiplayer, where both machines run this build and agree with each other; it only means a
Godot battle and an Unreal battle can diverge on an option they both consider equally good.

Easy and medium cannot yet be replayed bit-for-bit: their "settle for a worse option" roll needs
Godot's `randf`, which is not ported. `SimRandom.h` records what was measured about it. Hard makes no
random draw at all, which is why the parity tests use it.

## Documents
- `Docs/backlog.md`: port slices and features with status (producer).
- `Docs/design/<id>.md`: rules specs. For port slices these are *extracted from Godot* with line refs (game-designer).
- `Docs/tech/<id>.md` and `Docs/tech/_overview.md`: technical specs (technical-architect).
- `Docs/qa/<id>-testplan.md`, `Docs/qa/bugs.md` (qa-engineer). `Docs/reviews/<id>.md` (code-reviewer).
- Existing: `Docs/CharacterPipeline.md` (one canonical skeleton, SK_Mannequin; how heroes are brought in),
  `Content/Data/Classes/README.md` (class data format).

## Definition of done
1. Spec approved by the human (`Status: Approved`).
2. For port slices: behavior matches Godot, proven by a parity test against a Godot dump.
3. `scripts\test.bat` passes (all tests, not just the new one) with `/W4` clean.
4. `scripts\build.bat` succeeds with no new warnings. If the view changed, `scripts\wiring-test.bat` passes.
5. New state is in `Checksum()`. The dice order is unchanged unless the slice *is* the dice order.
6. code-reviewer verdict **APPROVE**. `Docs/backlog.md` and the porting status above updated.

## Human-only work
Agents write step-by-step instructions instead of attempting these:
- Anything in the editor UI: Fab downloads, IK Rigs/Retargeters, materials, Animation Blueprints, level art
  (see `Docs/CharacterPipeline.md`).
- Running the Godot project to regenerate golden tables (agents may write the `dump_*.gd` script).
- Editing `.uasset`/`.umap` files. Agents may change levels only through editor Python in `Tools/`.
- Packaging settings, and anything touching `D:\ProgramsByMe\TacticalMasters` other than reading.

## Team
Agents live in `.claude/agents/`. The main session orchestrates with `/plan-milestone`, `/port <slice>`,
`/feature <id>` and `/playtest` (see `.claude/commands/`). Subagents cannot call each other, so every
hand-off goes through the main session.
