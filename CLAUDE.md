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
- **Class data:** Tactical Masters' own format, `Content/Data/Classes/<id>.tmclass.json` (81 files + 6 built-in
  jobs in code = 87 classes). Made and installed by the class creator (`E:\TacticsClassCreator`, its own repo;
  `FORMAT.md` there). The 81 were converted once from Godot's Astra files; **nothing in Unreal reads Astra**.
  Godot keeps its Astra files as the reference, and `SimClassTest` compares against
  `Tests/GodotClassTable.txt` (from Godot's `tests/dump_class_table.gd`) when present.
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

**A headless game session must be able to end itself.** The director asks to exit once a battle is
decided and `FApp::IsUnattended()`. Without that a run sits there for ever holding
`UnrealEditor-TacticalMasters.dll` open, and the next build fails with `LNK1104` — and the session can
be hard to kill (`Stop-Process` and `taskkill` may both refuse; WMI `Terminate` worked).

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
5. **`FEvent`/`FTickReport` are presentation only.** Nothing in the rules may read them back. The
   director reads them twice, for two separate things: `Narrate` builds the battle log, and `ShowEvents`
   puts up the rising numbers, flashes a struck unit's light, plays an ability's particle effect and
   animates the bodies (`TMBattleDirectorMotion.cpp`: walks, swings, flinches, falls). What an ability
   did is held back until its swing connects or its arrow arrives (`TMBattleDirectorBlows.cpp`), which
   changes when it is seen, never what happened. Losing either would not change a
   battle by a hair. The numbers exist only in a game world, because nothing advances them in the
   editor and they would pile up in the level.
6. **Any new state field must be mixed into `FBattle::Checksum()`** (`SimWorld.cpp`), or desyncs go undetected.
   The gaps F7 listed (RNG state, `KoTicks`, `bHustling`, `UnharmedTurns`) are closed, along with casting,
   channelling and toggles. `SimPlayTest` has a probe per field that changes it and insists the checksum
   notices — 36 of them. Add one with any new field.
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
| The class lab (the rules alone, for the class creator: check + playtest a class file) | `Tools\ClassLab\Build.bat` → `Binaries\ClassLab\TMClassLab.exe` |
| The map analyser (the rules alone: routes, sight, height, hazards of a map file; `--battles N` adds computer-vs-computer games with heatmaps) | `scripts\map-analyze.bat [map.txt] [--battles N] [--capture S]` (builds `Tools\MapAnalyzer`; maps in `Tools\MapAnalyzer\maps`; see its README) |
| Film every particle effect in the project for the class creator | `Tools\VfxCatalog.bat` (runs the game off-screen with `-tmvfxcatalog`; writes `Saved\VfxCatalog`; about 2 minutes; ends itself) |
| Film every animation clip on every body, for the class creator's motion picker | `Tools\AnimCatalog.bat` (`-tmanimcatalog`; writes `Saved\AnimCatalog`; about 3 minutes; ends itself; add `-tmanimyaw=90` to a game run to film from the side) |
| Make a Paragon hero from Fab into a body classes can wear | `Tools\AddHero.bat <hero folder> <body> [look ...] [--write]` (reads the hero's clips by name, writes only `characters.json`; see `Docs/CharacterSetup.md`) |
| Bring the Godot game's class and ability icons in as PNGs for the HUD | `python Tools/import_icons.py` (reads Godot's `assets/icons`, writes `Content/Data/Icons`; needs Edge and Pillow) |
| Robot playtester: plays three sessions through the real controls (title → setup → battle, vs computer as blue and red with planning and hold-the-middle, two players), checking every step did what it should | add `-tmrobot` to a windowed game run (not `-nullrhi`); report and a picture per problem in `Saved\Robot\` (about 10 minutes; ends itself) |
| Make a Fab environment pack into a theme built from its meshes | `Tools\AddKit.bat <pack folder> <theme id> [--base ruined_keep] [--name Name] [--write]` (reads the pack's meshes by name and shape, writes only a theme file; `Docs/Maps.md`) |
| Play a chosen map and theme | add `-tmmap=crown_keep -tmtheme=winter` to a game run (`Docs/Maps.md`) |
| Watch chosen classes fight | add `-tmroster=a,b,c,d` to a game run: both sides field those four; `-tmhold=30` / `-tmtime=180` switch on the other ways to win, `-tmplan=30` planning time |
| Online: two copies of the game play each other over 127.0.0.1, headless; same battle on both, and a split caught | `Tests\OnlineTest.bat` (close the editor first; about five minutes; `Docs/design/feat-online.md`) |
| A whole battle in a real game world | `scriptsattle-test.bat` (no window, no rendering; ends itself) |

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
  sim sources, and to `Tools\ClassLab\Build.bat` and `Tools\MapAnalyzer\Build.bat`. All but `SimRandomTest` do (it compiles alone). A failed compile stops the whole run
  there, so later tests don't run. `RunTests.bat` hides compiler output, so check `/W4` on new files by
  compiling them once by hand.
- New rules need a parity test from a Godot dump, not only hand-written expectations.
- Unreal Automation Tests are not used. Don't introduce them without asking.
- **Computer-vs-computer measurements must alternate which side is listed first.** Units are taken in id
  order, and the side listed first loses noticeably more (Highlands: 64–126 over 200 games; alternating gives
  97–93). The class lab and the map analyser both alternate. Even then some maps lean
  (`sim-side-bias` in the backlog), so compare a change against the same map, not against 50%.

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
Class files (`SimClassTest`): all 81 load, broken files are refused, every class matches Godot's own reading
field for field (`GodotClassTable.txt`), and every one plays a battle with no refused order.
Particle effects are presentation only. An ability's `vfx` field names a Niagara or Cascade system under
`/Game` (a Fab pack, say). The rules read it and ignore it, and the director plays it when the ability
resolves. `ATMVfxStudio` films every effect for the class creator.
Whole battles (`SimTraceTest`): 14 battles replayed from Godot's own orders
(`tests/dump_battle_trace.gd` → `Tests/GodotBattleTrace.txt`, Godot 4.7.2-stable ed1daf0bf), every unit
compared after every step, and the port's own computer player asked for every order first: it must ask
for exactly what Godot's did — 1667 decisions at hard, medium and easy, all agree. The battles cover
passive and toggle buffs, auras (20 of the 81 classes have one, 11 a toggle), burn, regen, stun,
channelling, the battle time limit, holding the middle and planning time (placing units; setup screen:
Victory, Time, Planning). Units face the middle when a battle starts, set by `FBattle::Start` as Godot's
`setup` does.

The computer player sorts its options with a transcription of Godot's own sort (`SimSort.h`, from
`core/templates/sort_array.h`). Godot's sort is not stable, but it is deterministic, so ties are settled
exactly as Godot settles them. Easy and medium make Godot's random mistakes with `FSimRandom::Randf` and
`RandiRange`, both measured against Godot (`SimRandomTest`): randf is two draws, and randi_range draws
nothing for equal ends and rejects biased draws as PCG's bounded draw does.

Maps are files now (`Content/Data/Maps`, read and checked by `TMSim::ReadMapFile`; `SimMapTest`), with Highlands
built in as Godot has it. The port's own large map, Crown Keep (20x16), and four view-only themes
(`Content/Data/Themes`) are described in `Docs/Maps.md`.
Online play (`Docs/design/feat-online.md`, from Godot's `net.gd`): direct IP, the host plays blue and is the
referee, only the host moves time, checksums every 50 ticks, chat, rematch. Orders cross as text
(`SimOrderText`, bit for bit; `SimOrderTextTest`), and `Tests\OnlineTest.bat` plays two copies against each other.
TCP rather than Godot's ENet, so a Godot build and this one can't play each other.
Not yet ported (see `Docs/backlog.md`): Godot's other four maps, and saved teams.
**Deliberate divergence from Godot** (the human's decision, 2026-09-28): the computer player respects
Root, Freeze, Knockdown and Taunt (`FAIPlayer::NextCommand`, `BestAction`). Godot's (`ai_player.gd:43-50`)
ignores them, asks for orders the rules refuse, and stalls until its turn runs out. No recorded Godot
battle has these statuses in play, so every parity test still matches decision for decision.
Kept bug-for-bug from Godot, with a comment at each: a toggle in slot 3 skips the ultimate-meter and
cooldown gates (`AbilityBlockedReason`; reachable now that loaded classes have toggles).

## Documents
- `Docs/backlog.md`: port slices and features with status (producer).
- `Docs/design/<id>.md`: rules specs. For port slices these are *extracted from Godot* with line refs (game-designer).
- `Docs/tech/<id>.md` and `Docs/tech/_overview.md`: technical specs (technical-architect).
- `Docs/qa/<id>-testplan.md`, `Docs/qa/bugs.md` (qa-engineer). `Docs/reviews/<id>.md` (code-reviewer).
- Existing: `Docs/CharacterPipeline.md` (one canonical skeleton, SK_Mannequin; how heroes are brought in),
  `Content/Data/Classes/README.md` (class data format).
- `Docs/AssetShortlist.md`: free Fab assets that suit the game (Paragon heroes by class, effects, animations on Manny).
- `Tools/MapAnalyzer/README.md`: the map file format and how to read the analyser's report.

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
