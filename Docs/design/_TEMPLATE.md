# Rules Spec: <id>

Status: Draft  <!-- Draft | Approved (human only) | Superseded -->
Mode: Port | Feature
Backlog goal: <copy from Docs/backlog.md>

## 1. Summary
Two or three sentences: what this governs and why it matters in a fight.

## 2. Godot source (Mode: Port)
| Function | File:lines | Notes |
|----------|-----------|-------|

## 3. Rules, in the order they are applied
Number them. Cite `file.gd:line` for each (Port). Say what state is read and what is written.
1. …

## 4. Randomness
| When | Generator | Range | Decides | Skipped when |
|------|-----------|-------|---------|--------------|
Say "none" if nothing is rolled. Roll order is a rule.

## 5. Numbers
- Formulas, with the types Godot uses (float / double / int) and rounding (`roundi`, integer division, `floor`…).
- Worked examples, at least two, one of them an edge case.
- Tunables: `FTuning` fields / constants involved (name, default, meaning).

## 6. Iteration order and ties
What is iterated, in what order, and how ties are broken.

## 7. Edge cases
| Situation | What happens (cite Godot) |
|-----------|---------------------------|
| Unit KO'd / gone mid-effect | |
| Casting / channelling / stunned | |
| Out of bounds / blocked / level 0 ground | |
| Zero, negative, capped values | |
| Simultaneous events in one tick | |

## 8. Godot quirks (Port)
Behavior that looks unintended but is what Godot does. Kept as-is unless the human says otherwise.
- …

## 9. State and events
New or changed state (must go in `Checksum()`), and events the view should see.

## 10. Proof
What the Godot dump must record (inputs → outputs per row) and which scenarios it must cover.
Feature mode: how correctness will be tested without a Godot reference.

## 11. Player-facing feedback (Feature, or where the view is affected)

## 12. Open questions for the human
- …
