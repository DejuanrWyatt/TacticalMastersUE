# Backlog

Owned by the **producer** agent. This first version was seeded from the codebase survey
(`Docs/tech/_overview.md`) and is a **draft**. Run `/plan-milestone` to have the producer refine it, then
reorder it yourself.

## Milestone 0: Housekeeping (small, optional, low risk)
| id | goal | depends on | owner | status |
|----|------|-----------|-------|--------|
| chore-runtests-srclist | One `SIM_SRC` list in `Tests\RunTests.bat` instead of five copies (finding F3) | – | qa-engineer | Todo |
| chore-scale-naming | Rename director `TileSize` → `UnitsPerMetre`, fix the "one tile is one metre" comments (F1) | – | unreal-engineer | Todo |
| chore-stale-comments | Fix comments that say the map isn't ported, and misplaced doc comments in `SimBattle.h` (F2) | – | sim-engineer | Todo |

## Milestone 1: Abilities in a fight
| id | goal | depends on | owner | status |
|----|------|-----------|-------|--------|
| chore-checksum-gaps | Mix RNG state, `KoTicks`, `bHustling`, `UnharmedTurns` into `Checksum()`, with a `SimPlayTest` probe each (F7) | – | sim-engineer | Done |
| chore-replay-per-step | `SimPlayTest` compares checksums after every step, not only at the end (F9) | – | qa-engineer | Done |
| port-resolve-parity | Godot parity test for targeting and resolution, including roll order and RNG state after (F6) | – | qa-engineer | Done |
| port-cast-and-orders | Cast countdown in `Tick`/`TicksToReady`, `UseAbility` through `Validate`/`Apply` (cooldown logic exists) | port-resolve-parity, chore-checksum-gaps | sim-engineer | Done |
| feat-time-as-orders | Director steps time via `Submit(MakeAdvance)` so a battle is fully recordable (F8) | – | unreal-engineer | Done |
| port-turn-start | Start-of-turn effects: statuses tick/expire, hazards, regen, auras, channel continuation | port-cast-and-orders | sim-engineer | Done except auras — no built-in ability is one, so there is nothing to refresh yet; the gap is named in `BecomeReady` |
| port-ai-abilities | AI chooses and aims abilities (plus the `Randf` tie-break, verified bit-exact) | port-cast-and-orders | sim-engineer | Done for hard, parity-tested (`SimAIActionTest`). `Randf` is still unported, so easy and medium cannot be replayed bit-for-bit; and Godot settles a tie with an unstable sort, so tied options may differ — the test proves they are ties |
| feat-class-creator | A creator that makes a class from the ground up: stats, four abilities, role, look, lore. Research and ideas: https://claude.ai/code/artifact/7a75f6fe-cd93-4771-bd60-2e0c8260342a | port-astra-importer | game-designer | Built as its own tool, `E:\TacticsClassCreator` (separate git repo, Node, runs against the Godot game): reads/writes class files, lints for fields the importer would silently drop, reads back through `tests/check_class.gd`, measures with `balance.gd`. 16 archetypes: 10 ship; 6 wait on engine mechanics (Cast Catch, Held Aim, Gauge Field, Finisher, Gauge Theft, Lone Fight). Unreal only receives its output in `Content/Data/Classes`, so it still needs the importer port here |
| feat-event-playback | Director plays back `FTickReport` events: movement, casts, hits, misses, KOs | port-cast-and-orders | unreal-engineer | Done — `Narrate` builds the log, `ShowEvents` puts up rising numbers and flashes a struck unit (game world only); a cast bar is text in `DescribeBattle`. Projectiles and animations belong with the character pipeline |

### port-resolve-parity
Godot source: game_state.gd `_resolve_ability` (:1730-1867), `_hurt` (:1339), `_take_from_shield` (:1354),
`_add_status` (:1414), `_knock_out` (:1439), `_stun_interrupt` (:1644), `_check_winner`, `shape_of`/`in_shape` (:809-845), `in_ability_range`.
Acceptance criteria:
- Godot dump covers every built-in ability, each shape, evade/crit/no-roll paths, shields, statuses, KO, immunity/invulnerable.
- Per row: every hit's amount, evaded/critical flags, statuses applied, and the RNG state after.
- The test fails if two rolls are swapped or a skipped roll is taken.

### port-cast-and-orders
Godot source: game_state.gd order validation/apply for abilities, cast tick handling, cooldown handling.
Acceptance criteria:
- A cast started by an order lands after exactly `CastTicks` ticks, at the target Godot would use (following units included).
- Cooldowns behave as in Godot once reachable through orders.
- `SimPlayTest` gains a wrong-seed probe that fails, which proves abilities consume dice.
- Casting state is in the checksum, and the replay test still passes with abilities in play.

## Milestone 2: Playable (chosen 2026-09-27: playable before the roster)
Not yet broken down; run `/plan-milestone` to split it into slices.
| id | goal | depends on | owner | status |
|----|------|-----------|-------|--------|
| feat-player-input | In Play, the human picks a ready unit's move and ability with the mouse; every choice goes through `Submit()` with the unit's real `Serial` | – | unreal-engineer | Built, untested by hand. See `Docs/design/feat-player-input.md`. Tests, build, wiring and battle tests pass |
| feat-hud-forecast | A HUD: turn gauges and whose turn it is, ability slots with cooldowns, and a forecast from `Preview` before committing | feat-player-input | unreal-engineer | Built; seen in screenshots except aiming. See `Docs/design/feat-hud-forecast.md` |
| feat-match-flow | Start a match, see who won, play again | feat-player-input | unreal-engineer | Built: title, setup, in-battle menu, rematch. See `Docs/design/feat-match-flow.md` |

## Milestone 3: The whole roster
| id | goal | depends on | owner | status |
|----|------|-----------|-------|--------|
| port-passives-auras | Passive/aura stat contributions in `FUnit::Stat`, auras applied | port-turn-start | sim-engineer | Todo |
| class-files | Tactical Masters' own class format, made by the class creator; the 81 Astra classes converted once; Unreal loads `*.tmclass.json` (no Astra) | – | sim-engineer | Built: loads, refuses bad files, all 81 play legally. Waiting on `Tests/GodotClassTable.txt` (human runs `dump_class_table.gd`) for the field-for-field match. Replaces port-astra-import |
| creator-native | The class creator keeps and writes classes in the new format, and playtests with Unreal's rules (a headless class lab) instead of Godot | class-files | – | Todo |
| chore-class-sync-check | Replaced by SimClassTest's match against Godot's own reading (F4) | class-files | qa-engineer | Superseded |
| chore-packaging-classes | Add `Data/Classes` to non-asset directories to package (F5) | port-astra-import | human | Todo |

## Later (not yet broken down)
Ultimates · animation from events on the canonical skeleton · CharacterMap data · networking over `Submit` + checksums.
