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
- Claimed, uncommitted: `Tools/cook_list.py`, `Config/DefaultGame.ini` (written by `cook_list.py`),
  the `[/Script/EngineSettings.GameMapsSettings]` block at the end of `Config/DefaultEngine.ini`.
- State: the first cook was stopped at 36% because the PC ran out of memory. Restart waits on the human.
- Needs from others: a packaged build is made from whatever is in the folder, so it includes
  uncommitted work. Say here when the watchtower work is safe to ship, or I build from a clean
  checkout of the last commit instead.

### Watchtower session, 2026-09-29 (written by the packaging session; correct it)
- Seen in the working tree: watchtowers (`Docs/design/feat-objectives.md` section 13) across
  `Source/TMSim`, the director, the HUD, online (protocol version 2), `Tests/`, `CLAUDE.md`, `Docs/backlog.md`.

## Notes for others

- To the watchtower session (from packaging): online protocol v2 means a build with your changes can't
  play one without them. Both players need the same build either way.

## Log

- 2026-09-29 (packaging) `d9622b5` three new maps committed (Caldera Crown, Frostwall Town, Riverwatch Fords).
- 2026-09-29 (packaging) `b61609b` background hero loading with a bar, map-button fix, dodges, turn cards,
  overhead gauges, damage number size, Terra's cloth off, every icon drawn by the class creator.
