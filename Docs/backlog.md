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
| port-ai-abilities | AI chooses and aims abilities (plus the `Randf` tie-break, verified bit-exact) | port-cast-and-orders | sim-engineer | Done at every difficulty: `SimTraceTest` holds the port's computer player to Godot's in 1667 decisions (hard, medium, easy). Ties settled by a transcription of Godot's sort (`SimSort.h`); `Randf` and `RandiRange` measured against Godot |
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
| port-passives-auras | Passive/aura stat contributions in `FUnit::Stat`, auras applied | port-turn-start | sim-engineer | Done: passives and switched-on toggles in `FUnit::Stat`, auras at turn start (buffs tagged by aura name, in the checksum), a toggle once a turn. `SimTraceTest`: 8 Godot battles agree order for order |
| port-planning | Planning time: placing units in the spawn area before the fighting, ready orders, as in Godot | – | sim-engineer | Done: `Place`/`Ready` orders, `CanPlace`/`PlaceableNodes`, Godot's smoke-test cases, two `SimTraceTest` battles (both ready; time runs out), every placeable spot compared. Setup row, banner, Ready button, spots shown |
| port-victory-rules | Battle time limit and holding the middle, chosen on the setup screen as in Godot | – | sim-engineer | Done: rules in `FBattle::Tick`, capture timers in the checksum, Godot's own smoke-test cases pass, and two `SimTraceTest` battles run with them on (one won by holding the middle, one called on time). Setup rows, clock and hold meter, ring on the board |
| class-files | Tactical Masters' own class format, made by the class creator; the 81 Astra classes converted once; Unreal loads `*.tmclass.json` (no Astra) | – | sim-engineer | Done: all 81 match Godot field for field, load, refuse bad files, and play legally. Replaces port-astra-import |
| creator-native | The class creator keeps and writes classes in the new format, and playtests with Unreal's rules (a headless class lab) instead of Godot | class-files | – | Done: creator v2 (Astra-style screen), `Tools/ClassLab`, 144 archetype×element proposals proven identical to the old route |
| creator-fab-vfx | Particle effects from the project (Fab packs) usable in the class creator: an ability's `vfx` field, played by the director, and filmed by Unreal (`Tools/VfxCatalog.bat`, `ATMVfxStudio`) so the creator can show them | creator-native | – | Done: 76 effects filmed (14 Niagara, 62 Cascade); 26 played in a real battle; SimClassTest reads and refuses the field |
| chore-class-sync-check | Replaced by SimClassTest's match against Godot's own reading (F4) | class-files | qa-engineer | Superseded |
| chore-packaging-classes | Add `Data/Classes` to non-asset directories to package (F5) | port-astra-import | human | Todo |

## Found along the way
| id | goal | depends on | owner | status |
|----|------|-----------|-------|--------|
| parity-status-ticks | (Done by `SimTraceTest` for burn, regen and stun; bleed and sleep only appear in creator-made classes Godot does not have.) A Godot parity dump of statuses ticking with damage (burn, bleed) and waking (sleep), with the dice and orders of a whole battle, so TickStatuses is measured against Godot and not only against itself | – | qa-engineer | Todo. TickStatuses read freed memory on every damage tick until 2026-09-27 (found by AddressSanitizer through the class lab); no parity test covered it |
| sim-side-bias | Find out why identical teams on a turned-around map don't split the wins: Crown 156–39 for blue over 200 games with the listing order alternating, and Highlands 97–3 for blue with hold-the-middle at 30 s. The halves, teams and listing are all even, so the rules or the computer player treat the sides differently (ties taken in grid order is the likely suspect). Decide whether Godot does the same before changing anything | – | sim-engineer | Todo. Repro: `scripts\map-analyze.bat Tools\MapAnalyzer\maps\crown.txt --battles 200` and `...\highlands.txt --battles 100 --capture 30`. Found 2026-09-28 by the map analyser |
| feat-map-files | The game loads battle maps from files (the analyser's format: rows, name, optional spawn spots) instead of the hard-coded Highlands and start spots, so a map measured by `scripts\map-analyze.bat` is the map that's played | – | sim-engineer, unreal-engineer | Todo. Today `HighlandsRows()` and the start spots are in code (`SimMap.cpp`, `ATMBattleDirector::BuildBattle`, `ClassLab`). Sketch maps waiting: `Tools/MapAnalyzer/maps/crown.txt`, `two_fords.txt`, `canyon.txt` |

| ai-root-taunt | The computer player ignores Root and Taunt on its own units: it orders a rooted unit to walk ("It can't walk.") and a taunted one to attack someone else. The rules refuse, and the turn is lost | – | sim-engineer | Todo. The same in Godot (ai_player.gd never checks either), so fixing it is a Godot change first or a deliberate divergence. Found 2026-09-27 playtesting the Interrupter (Pin roots) and Duellist (Challenge taunts): 44 and 50 refused orders in 32 games. No built-in class roots or taunts, which is why it never showed |

## Playtesting and looks
| id | goal | depends on | owner | status |
|----|------|-----------|-------|--------|
| robot-playtester | A robot that plays through the real controls and reports what a person would trip over | feat-player-input | qa-engineer | Done: `-tmrobot` (`ATMRobotPlayer`), report in `Saved/Robot`. First find fixed: clicking a unit read the ground behind it, so picking up another unit walked the selected one there (`PickUnderCursor` now takes a unit only when the pointer is on its body, as Godot's ray does) |
| unit-animation | Units walk their paths, swing, cast, flinch, fall and rise, from the battle's events; which body each class wears is data | – | unreal-engineer | Built: `TMBattleDirectorMotion.cpp`, `Content/Data/CharacterMap/characters.json` (Manny and Quinn, the mannequin's own clips). No Animation Blueprint needed |
| map-maker-and-kits | The class creator's map maker (paint, the rules' verdict, numbers, an isometric preview, the lab's battles with where units walked and fell, install); themes built from Fab meshes (a kit), and a tool that makes a pack into a kit theme | maps-and-themes | unreal-engineer | Done: creator `app/maps.mjs`, `app/mapmaker.mjs`; `TMClassLab map`; `KitPiece` in `TMBattleDirectorBoard.cpp`; `Tools/add_env_kit.py`. Waiting on the human: a Fab environment pack |
| maps-and-themes | Maps as files, checked by the rules; Crown Keep, a 20x16 map with a keep, ramps, ruins, a stream, springs and embers; four themes (meadow, autumn ruins, winter, volcanic) setting the ground, rock, water, land around the board, sun, sky and fog; Map and Theme on the setup screen | - | sim-engineer, unreal-engineer | Done: `SimMap.cpp`, `SimMapTest`, `TMBattleDirectorBoard.cpp`, `Docs/Maps.md`. Next: port Godot's other four maps, a map analyser, a map maker in the creator, Fab meshes for the shapes |
| creator-icons | Class and ability icons designed in the class creator (Icons tab) and installed as PNGs to `Content/Data/Icons`, which the HUD reads; turn squares centred, a row per side | atlas-hud | unreal-engineer | Done: creator `app/icons.mjs`; a worked-out icon never replaces one the game already has |
| atlas-hud | Atlas Reactor-style battle HUD: ability icons (coloured when usable, grey with turns left when not), ally panel bottom left and enemy panel bottom right with live portrait, HP, statuses, TG and ultimate; name, health and statuses over each unit; a ring under each unit filling with its Turn Gauge; allies blue, enemies red | ability-motions | unreal-engineer | Done: `TMBattleHudPanels.cpp`, `TMBattleDirectorLooks.cpp`; icons from Godot (`Tools/import_icons.py`). The outline is `/Game/UI/M_TeamOutline`, made by `Tools/make_outline_material.py` at the human's request (`Docs/TeamOutline.md`) |
| blows-land | The numbers, flinches and falls wait for the swing to connect or the arrow, bolt or stone to arrive; reactions by where the blow came from (shove, sidestep, block, fall away); status looks (glows, stun reel, freeze, slow, fly, Zs); ultimates slow the world for a beat with their name in gold; winners celebrate; heals now read + and burns - | ability-motions | unreal-engineer | Done: `TMBattleDirectorBlows.cpp`; reaction/status/moment clips are optional per set (`characters.json` about). Both packs' hit-react clips are additive, so hits are shown by the shove |
| anim-studio | Unreal films every clip on every body (`Tools\AnimCatalog.bat`); the class creator's Effects tab picks each ability's motion and plays it on the class's body | ability-motions | unreal-engineer | Done: `TMAnimStudio.*`; creator `app/anim.mjs`, whose motion rule is checked against the class lab's on all 81 classes |
| more-heroes | One Paragon hero per look (Gideon, Greystone, Kwang, Yin/Crunch, Muriel) | anim-studio | human | Todo: Fab downloads, then `Tools\AddHero.bat` does the rest (`Docs/CharacterSetup.md`) |
| ability-motions | Each ability has a motion (class file `anim`, or worked out by `TMSim::MotionOf`); sets map motions to release clips, a cast's intro and wind-up loop, and a charged release; a channel loops; a fizzle flinches | unit-animation | unreal-engineer | Done: `SimClassTest` checks every ability of all 87 classes has a known motion and refuses unknown ones; the creator validates `anim` the same way. Next ideas: impact timing and projectiles, reactions, status bodies, an animation studio |
| camera-options-devtools | Godot's free camera, Options (camera speed, UI scale, fullscreen, colour-blind colours, rebindable keys) and Developer Tools (every rule number) | – | unreal-engineer | Done: camera in the director, `TMSettings` (saved to `Saved/TacticalMasters/settings.json`), `TMBattleHudOptions.cpp`. Rule numbers reach a running battle only as a `Tune` order (in the checksum, clamped as Godot's clean_tuning). Turn order as fixed squares is the default (Options toggles the bars back). Edit layout (F2 or the Layout button) moves every panel and reorders the squares, kept per machine. Not yet: sound volumes |
| character-setup | Paragon heroes (and later Tripo/Blender characters) animated and worn by classes | unit-animation | human | Todo: steps in `Docs/CharacterSetup.md` (IK Rigs, a retargeter from the mannequin, copied clips, a set and a body in the character map) |

## Later (not yet broken down)
Ultimates · animation from events on the canonical skeleton · CharacterMap data · networking over `Submit` + checksums.
