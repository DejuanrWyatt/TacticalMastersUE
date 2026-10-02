# Work log

More than one Claude session works on this project at once, in the same folder, often without being
able to message each other. This file is how each knows what the others are doing. **Read it before
you start work; write to it when you start, claim files, commit, or stop.** (The rule is in
`CLAUDE.md`, "Working alongside other sessions".)

How to write here:
- **Active** is one short block per session, which that session keeps current: what it is doing, the
  files it has claimed (it is changing them and has not committed), and anything the others must not do
  meanwhile. Name the session by its first words of work if it has no other name. Remove your block
  when you stop.
- **Log** is newest first, one or two lines an entry, dated: what landed (with the commit), what was
  started and left, what the others need to know.
- Don't edit another session's block. Leave it a line under **Notes for others** instead.

## Active

### Packaging session (tacticalmastersue-6b), 2026-09-29
- Doing: a packaged Windows build for online play (`E:\Builds\TacticalMasters`).
- Claimed: nothing in this folder now (packaging config committed in `a75a859`).
- State: packaging from a clean worktree of `a75a859`, **without watchtowers**, at
  `E:\UnrealProjects\TM_Release` (its `Content` is a junction to this folder's). Log: `E:\Builds\package.log`.
  Started 2026-09-29; the cook takes an hour or more and uses ~10 GB.
- 2026-09-29: the cook ran the PC out of memory again at 82% (9059/11110; "paging file too small").
  The human is enlarging the page file (32 GB on E:) and restarting the PC. **To resume** (keeps the
  82% already cooked), from any session, with the PC otherwise quiet:
  `E:\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat BuildCookRun -project=E:\UnrealProjects\TM_Release\TacticalMasters.uproject -noP4 -platform=Win64 -clientconfig=Development -build -cook -cookincremental -stage -pak -iostore -prereqs -archive -archivedirectory=E:\Builds\TacticalMasters -utf8output -unattended > E:\Builds\package.log 2>&1`
  Then: run `E:\Builds\TacticalMasters\Windows\TacticalMasters.exe` once to check it opens and plays,
  and copy the online guide (`HOW TO PLAY ONLINE.txt`, drafted by this session; rewrite it if lost:
  host = Play Online -> Host Game, TCP port 7777, UPnP or forward by hand; join = Host address -> Join Game)
  into `E:\Builds\TacticalMasters\Windows`.
- Please don't: delete or move `E:\UnrealProjects\TM_Release`, or change Content assets, while it cooks.
  A second heavy build at the same time may run the PC out of memory again.

### Lobby and items session (the watchtower session, continued), 2026-10-01
- Doing: the online lobby (up to four players, sides chosen, the draft), friendly fire, the team stash and
  team items screen, thinner aiming lines, a dimmer walk area, range on ability buttons, Tab for status bars.
  Since: the combat log redesign (one line per action, tabs All/Combat/Mine/Key), the options menu scrolls, camp
  respawns as a setup option (off by default), auto-recenter as an option with V to toggle, and crash fixes
  (Codex hero flipping, fullscreen toggling, render targets no longer resized in place, cloth and morph targets off).
  Then queued orders (Docs/design/feat-move-queue.md): plan a waiting unit's next turn, plan a turn and go in one
  (G), and waypoints (Ctrl+click). Then Go To, Civilization III style (feat-move-queue.md, "Go To"), and the new
  combat text and the health bar that shows over your own units in a fight (feat-combat-text.md).
- Claimed (changed, built, tests passing, not committed): `Source/TacticalMasters/` TMNet.{h,cpp},
  TMBattleDirector.{h,cpp}, TMBattleDirector{Online,Lobby,Draft,Camps,Indicators,Fog,Loading,Showcase,Motion,Plans}.cpp (Plans is new), TMBattleHud.{h,cpp},
  TMBattleHud{Lobby,Panels,Codex,Options}.cpp, TMBattleHudStyle.h, TMBattleDirectorBlows.cpp, TMBattleDirectorAbilityFx.cpp, TMDrawable.{h,cpp} (new), TMSettings.{h,cpp}; `Source/TMSim/` SimOrder.h, SimBattle.h, SimTypes.{h,cpp},
  SimTargeting.cpp, SimAI.cpp, SimCamps.cpp, SimWorld.cpp, SimOrderText.cpp, SimUnit.h, SimMovement.cpp, SimBattle.cpp, SimResolve.cpp; `Tests/` SimCampTest.cpp, SimPlayTest.cpp, SimMoveTest.cpp, SimOrderTextTest.cpp;
  `Docs/design/feat-lobby.md`, `Docs/design/feat-team-items.md`, `Docs/design/feat-move-queue.md`, `Docs/design/feat-defense.md`, `Docs/design/feat-combat-text.md`, `Docs/design/feat-class-balance.md`, `Docs/CHANGELOG.md` (new).
- Online protocol is now 14: builds from before today can't play these.

## Notes for others

- For every session (from the watchtower session): the device bridge can write an older copy of a file when the same
  container path is committed twice. Commit each change from a fresh path, and re-stage to check what landed.
- To the watchtower session (from packaging): online protocol v2 means a build with your changes can't
  play one without them. Both players need the same build either way.

## Log

- 2026-10-02 15:15 (lobby and items session) Built and tested clean at 15:11 (every test passed). Packaging v17 at the
  human's word ("Start v17 release"): `E:\Builds\TacticalMasters-2026-10-02-v17` (protocol 14) by
  `E:\Builds\agent-release-build.bat`; "Play with crash details.bat" copied in from v16. Packaged 15:31: BUILD READY,
  `E:\Builds\TacticalMasters-2026-10-02-v17\Windows\TacticalMasters.exe`. Not committed (not asked).
- 2026-10-02 14:55 (lobby and items session) Not built, not committed: "Turn Left Indicator Mockups" A + D, the
  human's pick with flashing borders: ATMBattleHud::TurnPips (round joined tokens in front of the ring, for our own
  ready units with half a turn or more left; statuses stay beside the ring as rounded squares); the action bar's
  Move tile and usable ability tiles flash (a pulsing edge) once the other half is spent; spent halves read "moved"
  / "acted" (WordTile's under-line, AbilityTile's effect band).
- 2026-10-02 14:40 (lobby and items session) Not built, not committed: a tower just taken catches over
  ATMBattleDirector::TowerKindleSeconds (2 s): FTMTower::KindledAt/FlameTime, LightTower (colour, light and a flare by
  the catch), AdvanceTowers' flames eased from embers; AdvanceFog spreads the tower's sight with it in 16 steps (the
  rest of the side's sight cached in FogSteadySeen, so only the spread is worked out again), and IsSeen/IsPointSeen
  agree. Presentation only: the rules still see the whole circle at once (no protocol change).
- 2026-10-02 14:25 (lobby and items session) Not built, not committed: the action bar, "Action Bar Mockups" B (the
  human's pick): WordTile (TMBattleHud.cpp) draws a stroke icon beside each word (EGlyph: Move, Sprint, Items,
  Capture, End), small tiles 64 -> 74 px; AbilityTile (TMBattleHudPanels.cpp) shows a cooling ability on the bar as
  an hourglass badge with the turns left and a step bar per cooldown turn (the enemy panel keeps the large number).
- 2026-10-02 14:10 (lobby and items session) Not built, not committed: the Wounded status ("wounded", WND):
  FStatusDef::HealTakenPercent (new, last field; Wounded -50), FUnit::HealReceived applied to heal abilities
  (CalcAmount), Regen, springs, undamaged mending and lifesteal; the HUD's look and words (TMBattleHudStyle.h), its
  combat-text colour (Blows), its icon (Tools/StatusIcons, Content/Data/Icons/statuses/wounded.png), a test in
  SimStatusTest (passes here, with every other status test), and the class creator's vocabulary. No ability or item
  uses it yet. Go To stops drawn as rings on the ground (DrawGoToMarks, "Go To Marker Options" B). Mockups: "Action
  Bar Mockups" (icons on Move/Sprint/Items/Capture/End, two cooldown displays).
- 2026-10-02 13:45 (lobby and items session) v16 crashed again on DirectX 11 (-dx11): the same access violation in
  SetShaderParameters, base pass, breadcrumbs "SceneRender - BattleDirector" (likely one of the director's scene
  captures: the turn-card portraits, every frame). A Narbash body (piper) was on the field both times, but a 24-minute
  -dx11 match with two Narbash bodies ended cleanly, so that is not proven. Added "Play with crash details.bat" to the v16
  folder (-ExecCmds r.EmitMeshDrawEvents 1, r.ShowMaterialDrawEvents 1) so the next crash log names the mesh and
  material being drawn.
- 2026-10-02 13:20 (lobby and items session) Not built, not committed: the battle report shows each unit's items
  (ATMBattleHud::ReportGear in TMBattleHudReport.cpp, declared in TMBattleHud.h: an Items column in the tables, beside
  the name on a unit's page, "Carried" on the MVP card); the MVP summary is two short lines (it ran out of the card);
  moments name the class and side, not the unit id. Chest mockups ("Treasure Chest Mockups"): no beam through fog,
  the human's call.
- 2026-10-02 12:40 (lobby and items session) Committed `8180d91` (this session's work since `3ab5f9e`: Source, Tests,
  Docs, Content/Data, Config, Tools; never the Fab packs) and the class creator `fcb5908`. Release
  `E:\Builds\TacticalMasters-2026-10-02-v16` (protocol 14), by Saved/agent-v16.bat. v16 then crashed in play: D3D12
  SetShaderParameters in the base pass, the first tested battle with Narbash bodies (war_drummer, dirge_singer). The v14
  guard (TMDrawable) had never caught a material in v15 or v16: at run time the four cook-broken Fab materials still
  report a shader map. TMDrawable.cpp now refuses them by name too (Narbash's legs drawn with the default surface).
  Not built, not committed, no release: waits on build-and-test and the human's OK. Meanwhile "Play on DirectX 11.bat".
- 2026-10-02 10:50 (lobby and items session) Class creator (E:\TacticsClassCreator): a "Game roster" page
  (app/roster.mjs, sidebar button, #roster): all 101 game classes as cards grouped by role, a sortable table of
  every stat (tinted above/below the role's usual), or a map of budget against win rate; filters for role, rule
  breaks, over/under budget and the 40-60% band; a side panel with stats against the role's usual and "Bring in and
  open" (imports into the library if needed). tests/roster.test.mjs. Mockups: "Class Roster Mockups".
  Earlier, 10:13: build-and-test passed (replays and battle report compile); not committed.
- 2026-10-02 09:50 (lobby and items session) Not committed. End-of-battle stats screen built
  (Docs/design/feat-battle-report.md): new TMBattleDirectorReport.cpp (per-unit tallies, MVP score: damage .1, taken
  .04, mitigated .03, healing .1, kill 12, assist 5 within 60 s, death -10, monster 4, boss 15, buff/debuff 2,
  control 3, revive 10, tower 8) and TMBattleHudReport.cpp. First build-and-test of the replay and report code
  failed with 9 errors (std::vector .Num(); helpers named Initials and Role clashing with TMHudStyle::Initials and
  AActor::Role); fixed (ReportInitials, RoleWord, .size()), not yet rebuilt. Class creator (E:\TacticsClassCreator):
  class rules flagged, not refused (deals damage; a tank protects; a support helps allies) and a stat budget
  (app/rules.mjs, app/budget.mjs). The 7 game tanks that break "a tank protects" (blazeblade, crusader,
  earthshaker, frostblade, hexblade, thunder_spellblade, windblade) and the Spellblade archetype stay flagged; the
  human will design their kits.
- 2026-10-02 09:10 (lobby and items session) Not committed; built by the build-and-test script only: replays
  (Docs/design/feat-replays.md). New TMBattleDirectorReplay.cpp and TMBattleHudReplay.cpp; hooks in
  TMBattleDirector.cpp (BuildBattle records or applies the replay's start before Battle.Start; Submit records every
  applied order and refuses all but the replay's own while watching; Tick plays the replay; ViewerTeam; keys; menu),
  TMBattleDirector.h, TMBattleHud.cpp/h (Replays on the title, Watch replay at the end, the replay bar).
  Saved/agent-replay-check.bat plays a battle and checks its replay with -tmreplaycheck. The end-of-battle
  stats screen waits on the human's answer to "Battle Report Mockups".
- 2026-10-02 08:55 (lobby and items session) Not built, not committed: 15 item files retuned for defense model 1
  (Docs/design/feat-items.md section 13): defensive numbers rescaled so each item takes off about the same share
  of its holder's damage as under the old rules (modelled over every class against every damaging ability).
  Tests/SimItemTest.cpp expects Armor +3 +6 for Chain Vest + Bulwark Plate. Item, camp and status suites pass
  locally. Next in this session: end-of-game stats/MVP mockup, then a replay system.
- 2026-10-02 09:05 (lobby and items session) Not built, not committed: 28 ability descriptions in 24 class files
  (Content/Data/Classes) rewritten to say what the abilities do, numbers untouched: "Slumber"/Sleep say Stun,
  Power -> Crit, seconds -> turns. Each class carries `creator.reworded`; `Tests/SimClassTest.cpp` skips only the
  description comparison with Godot for those (numbers still compared). Local run: 77 compared, every field agrees.
  The creator's library copies of these classes will show "Game's copy differs" until taken.
- 2026-10-02 08:40 (lobby and items session) Class creator: ability descriptions written from the numbers
  (app/describe.mjs). Abilities tab, Description: "From the numbers" keeps a flavour line (creator.flavour[slot])
  and adds what the ability does in the game's terms, rewritten on every change (distances in the flavour follow
  the ranges); "By hand" is checked instead, and disagreements show in the authoring checks. New classes start
  written from their numbers. The check finds 28 of the game's 404 ability descriptions wrong in 24 classes: eight
  "Slumber"/Sleep abilities say sleep and give Stun, ten still name Power, eleven give seconds for turns.
- 2026-10-02 08:05 (lobby and items session) Class creator: a live scorecard heads the right pane. 1.2 s after a
  change the game would see, it plays 20 games (class lab, game rules, Highlands) and shows the win %, the 40-60%
  band, the change since the last run (same seeds, so like for like), rank in the last balance pass, margin, as-four,
  and how often each ability was used ("never" in amber). "+20 games" plays new games on top; Live/Off kept in the
  browser. (In a test library, the old Frost Hexer's Curse showed as never used.)
- 2026-10-02 07:40 (lobby and items session) Class creator (E:\TacticsClassCreator, its own folder): the
  Stats tab uses the game's names (Armor, Resist, Evasion A/M with "AttDef in the file") and marks the lower
  evasion number as unused; a new "In the game" panel shows what lands through Armor/Resist, the one Evasion and
  the damage to knock the class out (app/defense.mjs). The New class dialog shows the class lab's result for each
  archetype (the game's classes made from it, game rules) in place of the old Godot margins. A badge on each class
  says whether the game's copy matches (Same / Changed since install / Game's copy differs), opens a diff with
  Install or "Take the game's copy", and an "Out of step" library filter (app/sync.mjs). Tests: tests/sync.test.mjs.
- 2026-10-01 23:55 (lobby and items session) Not committed, built by the build-and-test script only (no release):
  the human chose option A and zone of control from "Class Rebalance Mockups". `FTuning::ZoneOfControl` (40th rule
  number; the game sets 1 in `TMSim::GameTuning`): within EngageRadius of an enemy whose first role is tank a walk ends
  (SimMovement RunDijkstra; a tank the walk starts beside doesn't hold it; a waypoint in a zone is refused).
  `TMSim::ApplyGameBalance` (SimAbility) changes the built-in Knight and Archer for the game only, so the Godot tests
  hold; the director and the class lab (game rules) call it. The computer no longer aims an area between two targets
  at fogged ground (SimAI). Class files: Berserker, Samurai (descriptions), Frost Hexer, Frost Witch (split), each
  marked `creator.rebalanced`, which SimClassTest's Godot comparison now skips. The setup screen's choices are kept
  between sessions (`FTMSettings::LastSetup`); Dev Tools no longer shows the five rules the setup screen owns.
  `Docs/CHANGELOG.md` is new: player-facing, newest first; the release script copies it next to the exe. Keep it
  current with every change a player would notice. Protocol 14. Docs/design/feat-class-balance.md.

- 2026-10-01 22:00 (lobby and items session) Release `E:\Builds\TacticalMasters-2026-10-01-v15` (protocol 13): combat
  text, the fight health bar, Go To, the portrait-capture crash (deferred captures). Then the class lab measures on the
  game's rules: `TMSim::GameTuning` (SimTypes) is now the one place the game's rules are set (the director calls it);
  `Tools/ClassLab` fights on them at hard, 40 games, the class standing in for the reference member with its first role,
  plus four of it ("stack"), `--maps` round every map, and a `tournament` command for every class. Its Build.bat now
  compiles every Sim*.cpp (the list had fallen behind, so the lab last built 2026-09-29). The class creator
  (`E:\TacticsClassCreator`, its own repository): 40-game playtests, an "Every map" switch, the stack figure, a rank, and
  "Among every class" (the tournament, kept in `Saved/ClassLab/balance.json`); the stale "auras do nothing" warning
  removed. Not committed.

- 2026-10-01 20:45 (lobby and items session) Go To (click beyond the walk area: walks there over several turns,
  walk-and-end by default, stops when an enemy comes into sight, when hurt or blocked; G keeps going, Backspace cancels;
  numbers on the ground for each turn's end, a ">N" corner on the turn square, a strip with its buttons) and the combat
  text redesign ("Combat Text Mockups": red damage, big bold crits with "!", dark red ember burn, crimson bleed, green
  heals, statuses in their own colours; own units' health bar for 3 s on dealing or taking damage, the lost part
  flashing as it drains over 2 s). Built with the build-and-test script; no release packaged (the human's rule: no
  release without their OK). Not committed.
- 2026-10-01 20:01 (lobby and items session) Release `E:\Builds\TacticalMasters-2026-10-01-v14` (protocol 13): the play-test
  crash. Four Fab materials are cooked with no shaders (textures missing from the packs; package.log "Shadermap pointer is
  null"): ParagonMorigesh bugs (M_Bug_Mesh, M_Bug_ParticleSubUV_Trans), Narbash's M_Narbash_Legs_Drumsticks (on the Narbash
  body), Rampage's M_Rock_To_Throw. Drawing one crashed the render thread (SetShaderParameters). New TMDrawable.{h,cpp}: in a
  packaged game a body slot with such a material gets the default surface, and a Cascade effect holding one isn't played.
  Fixing the materials themselves is editor work (human-only). Also the new defense rules (entry below).
- 2026-10-01 19:40 (lobby and items session) Not committed: new defense rules (Docs/design/feat-defense.md). Armor
  (AttDef) and Resist (MagDef) take a share, 30 / (30 + it), instead of being subtracted; one Evasion (the higher of
  A-Eva and M-Eva, plus bonuses to either); an evaded hit is dodged 1 in 10, grazed for half otherwise. Behind
  `FTuning::DefenseModel` (0, Godot's, stays the rules' default so the Godot comparisons hold; the game sets 1 in
  `GameTuning`). Protocol 13. Also: the setup screen's Start no longer covers its last rule; "Not ported yet" gone.
- 2026-10-01 17:30 (lobby and items session) Not committed: queued orders (Docs/design/feat-move-queue.md). Plan a
  waiting unit's next turn (click it): a walk and an ability aimed from where it ends, run when its turn begins; plan a
  turn and go in one (G); waypoints on any walk (Ctrl+click, up to 4; the move order carries them, protocol 12).
  New TMBattleDirectorPlans.cpp; sim tests pass here (SimMoveTest has waypoint checks).
- 2026-10-01 16:10 (lobby and items session) Built and tested, not committed: crash fixes from the play tests. Picking
  Berserker crashed every time (engine morph buffers, `FMorphVertexBufferPool::GetReadingIndex` "Index == 1", on Grux's
  mesh): morph targets are now off on every unit and guide body, and cloth too (WearBody). Also render targets are made
  new rather than resized (FilmOfSize), the Codex guide hero swaps at most every 0.25 s, fullscreen is debounced.
  Earlier today: the combat log redesign, a scrolling options menu, camp respawns (setup, off by default), auto-recenter
  (option, V). Protocol 11. Release `E:\Builds\TacticalMasters-2026-10-01-v11` (has the Berserker crash); v12 next.
- 2026-10-01 10:30 (lobby and items session) Built and tested, not committed: the online lobby for up to four
  (sides chosen, units shared out by order of joining, the computer on an empty side) and a LoL-style draft with bans
  (Docs/design/feat-lobby.md; protocol 8); friendly fire as a setup option (9); the team stash: items picked up go
  to the side, equipped from the team items screen, taking one off is a turn, the fallen's go back (10;
  Docs/design/feat-team-items.md); thin aiming lines for every shape, a dimmer walk area, range on ability
  buttons, Tab shows/hides status bars (rebindable; "next unit" moved to N). Free-for-all (3-4 sides) is not started.
- 2026-10-01 01:34 (watchtower session) LAN play-test build of `3ab5f9e` packaged:
  `E:\Builds\TacticalMasters-2026-09-30\Windows\TacticalMasters.exe` (online protocol 7; the 2026-09-29 build is kept in
  `E:\Builds\TacticalMasters`). `Tools/cook_list.py` now also cooks the `/Game/...` paths named in the C++; it and the
  regenerated `Config/DefaultGame.ini` are changed in this folder, not committed. `TM_Release`'s `Source` and `Config`
  were mirrored from here (its git HEAD still reads `a75a859`). Script `E:\Builds\agent-release-build.bat`. Block removed.
- 2026-09-30 19:45 (watchtower session) `3114b33`: everything this session had claimed since 2026-09-29, committed at
  the human's ask after a clean build and every test passing (19:12): watchtowers, items, neutral camps and bosses, the
  second set of statuses, four 64 x 64 maps, lighting and foliage, ability looks, fog of war over everything, the red
  hover outline, cliffs and ruins, walking onto items picks them up, item icons, outlined world text, the Runic Coffer
  chest and the Signal Beacon tower. Later the same day, in the same commit: lag fixes (looks and sounds preloaded, frame
  timers in the log), no sight lines, the comet trail round a toggle that is on, ground indicators and tower/centre rings
  painted over the land, taller towers that see their whole radius, blue hover for allies, the camera following to the
  next unit, the ten hazard looks (TMBattleDirectorHazards.cpp), and the Codex (TMBattleHudCodex.cpp). Online protocol 7.
  Block removed. Not committed, nobody's claim: the Fab content
  packs under `Content/`, `Content/{SampleMap,Lighting,ErodingCircle}`, `SnippingSS/`, `scripts/`, `Tools/MapAnalyzer`
  (but its four new maps), `Docs/{AgentTeam,AssetShortlist}.md`, `Docs/{qa,tech}/`, `Docs/design/_TEMPLATE.md`.
  The class creator's changes went into its own repository.
- 2026-09-29 20:48 (watchtower session, resuming packaging) Packaged build done: `E:\Builds\TacticalMasters\Windows\TacticalMasters.exe`,
  from `TM_Release` at `a75a859` (no watchtowers). The cook finished 11110/11110 but UAT failed on 1025 `LogAssetManager`
  "PrimaryAssetId TMDataNamed:... does not match object's real id" errors; an incremental rerun with `-ignorecookerrors`
  (`E:\Builds\agent-finish-package.bat`, log `E:\Builds\package.log`) finished it. Online guide copied next to the exe.
  Packaging session: those errors will fail the next normal cook too. (This entry was lost once when the file was rewritten.)

- 2026-09-29 (packaging) `a75a859` packaging config (startup map, cook list, data staged, cook memory cap);
  `08883a5`/`a177e4c` this work log and its rule in CLAUDE.md.
- 2026-09-29 (packaging) `d9622b5` three new maps committed (Caldera Crown, Frostwall Town, Riverwatch Fords).
- 2026-09-29 (packaging) `b61609b` background hero loading with a bar, map-button fix, dodges, turn cards,
  overhead gauges, damage number size, Terra's cloth off, every icon drawn by the class creator.
