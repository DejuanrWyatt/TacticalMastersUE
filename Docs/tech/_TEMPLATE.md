# Tech Spec: <id>

Rules spec: Docs/design/<id>.md (must be Approved)

## 1. Files and functions
| File | New/Changed | Function (signature) | Godot counterpart |
|------|-------------|----------------------|-------------------|

`Tests\RunTests.bat` needs updating: yes/no (every compile line, if yes).

## 2. State
| Owner (FUnit/FBattle/…) | Field | Type (matches Godot) | Checksum line |
|------|-------|------|----------------|

## 3. Where it runs
Position within `Tick` / `BecomeReady` / `EndTurn` / `Validate` / `Apply` / `ResolveAbility`, relative to what is
already there. For dice: the full roll sequence before → after.

## 4. Parity test
- Golden table: `Tests/Godot<Name>Table.txt`, row format:
  `…`
- Godot dump script: `Docs/qa/godot-dumps/dump_<name>.gd` → copied to the Godot project's `tests/`
- Scenarios covered:
- Deliberate breakages the test must catch:
- What it cannot pin down:

## 5. Replay / checksum
Changes to `SimPlayTest` probes.

## 6. View hooks
| Event (EEventKind) | Fields used | What ATMBattleDirector shows |
|--------------------|-------------|------------------------------|

## 7. Human tasks
1. …

## 8. Risks
- …
