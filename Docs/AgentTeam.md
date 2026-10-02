# The Agent Team

A team of Claude Code agents set up for this project. They know the port's rules: Godot is the source
of truth, determinism, parity tests, and the sim deciding while Unreal only shows.

## Setup
1. Install Claude Code and start it in `E:\UnrealProjects\TacticalMastersUE`.
2. Run `/agents` and check that all seven are listed.
3. Claude Code will ask once to trust the project settings (`.claude/settings.json`). They pre-approve the test,
   build and git commands, grant **read-only** access to the Godot project at `D:\ProgramsByMe\TacticalMasters`,
   and block edits to golden tables, `.uasset`/`.umap` files and the Godot project.
4. Try `scripts\test.bat`. It should print ALL TESTS PASSED.

## The team
| Agent | Does |
|---|---|
| producer | Keeps `Docs/backlog.md`: port slices and features in dependency order |
| game-designer | Extracts rules from Godot with line refs (port), or designs new mechanics (feature) |
| technical-architect | Plans where code goes, new state + checksum, roll order, the parity test |
| sim-engineer | Ports the rules into `Source/TMSim`, bit-exact |
| unreal-engineer | The view: director, event playback, input, HUD, editor Python |
| qa-engineer | Godot dump scripts, parity tests, replay probes, wiring test, bugs |
| code-reviewer | Reviews determinism first, then Godot fidelity, tests and boundaries |

## Commands
```
/plan-milestone 1 Abilities in a fight   refine and reorder the backlog
/port port-resolve-parity                a Godot slice: extract → plan → dump → port → parity → review
/feature feat-event-playback             something new or Unreal-side
/playtest "the archer backs off too early; can't tell who's casting"
```

## Where you come in
- **Approving specs.** For port slices, read the "Godot quirks" list. That's where you decide whether
  Unreal copies a Godot oddity.
- **Running Godot dumps.** QA writes `Docs/qa/godot-dumps/dump_<name>.gd`. You copy it into the Godot
  project's `tests/`, run it, and put the output table in `Tests/`.
- **Closing the editor** before builds. `scripts\build.bat` refuses while it's open.
- **Editor work** from the agents' checklists, and merging.

## Starting point
`Docs/tech/_overview.md` is the codebase map and lists ten findings (F1–F10). `Docs/backlog.md` has a draft
plan built from them. The first real slices are closing the checksum gaps and `port-resolve-parity`,
because resolution is where the dice live and it has no Godot parity test yet.
