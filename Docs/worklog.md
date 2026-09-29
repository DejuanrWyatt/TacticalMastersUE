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

### Watchtower session (claude-fd), 2026-09-29
- Doing: watchtowers (`Docs/design/feat-objectives.md` section 13). Rules done and tested on Linux (every
  test passes, `SimWatchtowerTest` new); the Unreal side is written but not yet compiled. Now: running
  `scripts\build.bat` and `scripts\test.bat` for the human.
- Claimed, uncommitted: `Source/TMSim/Public/{SimAI,SimBattle,SimOrder,SimTypes}.h`,
  `Source/TMSim/Private/{SimAI,SimBattle,SimOrderText,SimTypes,SimWorld}.cpp`,
  `Source/TacticalMasters/{TMBattleDirector.cpp,TMBattleDirector.h,TMBattleDirectorBoard.cpp,TMBattleDirectorOnline.cpp,TMBattleHud.cpp,TMBattleHud.h,TMBattleHudOptions.cpp,TMNet.h,TMRobotPlayer.cpp}`,
  `Tests/{RunTests.bat,SimPlayTest.cpp,SimWatchtowerTest.cpp}`, my rows in `Docs/backlog.md` and
  `CLAUDE.md` (watchtower command + porting-status paragraph), `Docs/design/feat-objectives.md`, `feat-items.md`.
- Not safe to ship yet: until build.bat and test.bat pass on this PC, package from the last commit. I'll
  say here when they pass (and commit only these files).

## Notes for others

- To the watchtower session (from packaging): online protocol v2 means a build with your changes can't
  play one without them. Both players need the same build either way.

## Log

- 2026-09-29 (packaging) `a75a859` packaging config (startup map, cook list, data staged, cook memory cap);
  `08883a5`/`a177e4c` this work log and its rule in CLAUDE.md.
- 2026-09-29 (packaging) `d9622b5` three new maps committed (Caldera Crown, Frostwall Town, Riverwatch Fords).
- 2026-09-29 (packaging) `b61609b` background hero loading with a bar, map-button fix, dodges, turn cards,
  overhead gauges, damage number size, Terra's cloth off, every icon drawn by the class creator.
