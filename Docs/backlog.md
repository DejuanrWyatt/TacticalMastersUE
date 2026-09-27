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
| feat-event-playback | Director plays back `FTickReport` events: movement, casts, hits, misses, KOs | port-cast-and-orders | unreal-engineer | Part done — the log and a text cast bar read from the events (`Narrate`, `BattleLog`, `DescribeBattle`); floating numbers and a hit flash on screen are not done |

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

## Milestone 2: The whole roster
| id | goal | depends on | owner | status |
|----|------|-----------|-------|--------|
| port-passives-auras | Passive/aura stat contributions in `FUnit::Stat`, auras applied | port-turn-start | sim-engineer | Todo |
| port-astra-import | Port `astra_import.gd`: load the 81 class files into the registry | port-passives-auras | sim-engineer | Todo |
| chore-class-sync-check | Test that `Content/Data/Classes` matches the Godot copy (F4) | port-astra-import | qa-engineer | Todo |
| chore-packaging-classes | Add `Data/Classes` to non-asset directories to package (F5) | port-astra-import | human | Todo |

## Later (not yet broken down)
Ultimates · player input and order submission in Play · camera · HUD with forecasts from `Preview` ·
animation from events on the canonical skeleton · CharacterMap data · networking over `Submit` + checksums.
