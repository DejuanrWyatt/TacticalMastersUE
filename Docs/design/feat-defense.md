# Defense: Armor, Resist, one Evasion that dodges or grazes

2026-10-01. Asked for: "a deep dive into the stats ... improve upon or simplify the Def stats". Mockups: the "Defense
Stat Mockups" canvas (today, A percent, B Armor/Resist/Evasion, C one Defense, D Guard, and a calculator). The human
picked B, with their own evasion rule, and the higher of the two old evasions for a class's Evasion.

## Before (Godot's, still the rules' default: `FTuning::DefenseModel` 0)

- damage = max(1, (raw - AttDef or MagDef) x 0.5), raw being power x height x flank x statuses.
- A-Eva or M-Eva: % chance the hit misses.
- Trouble: four numbers; a point of defense worth half a point of damage; flat subtraction is worth more against
  small hits and stacks to immunity (Bastion + Guard + Leaden Mantle, AttDef 33: any physical ability of 35 power or
  less does 1); evasion all or nothing.

## Now (the game's: `DefenseModel` 1, set by `ATMBattleDirector::GameTuning`)

- **Armor** (the class file's `attdef`) and **Resist** (`magdef`) take a share:
  damage = max(1, raw x 0.5 x S / (S + defense)), S = `defense_scale` (30). 10 takes a quarter, 30 half. Defense
  below 0 counts as 0. The middle matchup lands where it did.
- **Evasion**, one stat against any hit: the higher of the class's `aeva` and `meva`, plus everything items, buffs and
  passives add to either (Nightcloak, which adds to both, once). `FBattle::EvasionOf`.
- The roll: Evasion % (x `evade_multiplier`, + Blind, at most 95) that the hit is evaded. Of those, **1 in 10 dodges**
  (no damage, nothing that comes with it: the `Evaded` event), **9 in 10 graze**: half damage, never critical, its
  statuses and the rest still land (`Grazed`, then the `Hit`).
- On average an evasion takes 0.55 of a hit (`Combat::EvadedShare`), which is what the computer player counts.

Class files, items and the order text are unchanged; `aeva`/`meva` are read as before and combined by the rules.

## The game

- Names everywhere (HUD style `ShownStatName`): Armor, Resist, Evasion. The Codex's class table has one Evasion
  column (9 stats), the class page one Evasion row.
- The unit card: "Armor 19 (-39% physical)  Resist 13 (-30% magic)  Evasion 5% (1 in 10 dodged, the rest grazed for
  half)". The aim preview: "-14  12% evade (graze -7)".
- Log: a GRAZE tag on the hit, "dodged" for a dodge. Floaters: "graze", "dodge".
- Developer Tools: "Defense rules" (0 classic, 1 new) and "Defense for half damage" (30). Online protocol 13.

Tests: `SimPlayTest` (every built-in damaging ability against every class, both models; no immunity from a +60
buff; one Evasion; dodges about one in ten over three battles, each replaying). The Godot comparisons run on model 0
and are unchanged.
