# The battle report and the MVP

2026-10-02. The human asked for an end-of-game stats screen with an MVP, graded on damage and damage taken,
buffs and debuffs, knock-outs, neutral kills and healing. Mockup: "Battle Report Mockups" 1-3; the weights below
are the human's (assists over a minute, damage mitigated at 0.3 per 10).

## What is counted

Tallied from the battle's events as every order is applied (`ATMBattleDirector::TallyEvents`,
TMBattleDirectorReport.cpp), so a replay tallies the same numbers. Per unit of the two sides:

| | How it is read |
|---|---|
| Damage | Hits of damaging abilities on the other side or on monsters; a status's ticks (Burn, Bleed) are whoever put the status there; Thorns are the wearer's |
| Damage taken | Every damaging hit that landed on it |
| Damage mitigated | What never landed: Armor or Resist's share, worked back from the hit (landed x defense / 30); the other half of a graze; all of a dodge (the ability's amount as the rules would work it out); Protect and Shell's third; what a Shield or Barrier soaked |
| Healing | Only health that was missing (the units' health is noted before each order); Regen's ticks are its caster's |
| Knock-outs | The last to hurt the unit that fell |
| Assists | Anyone else on that side who hurt, debuffed or held it in the minute before it fell |
| Monsters, bosses | The last blow on a camp monster; the battle's boss counts as a boss |
| Buffs, debuffs | A status put on an ally (not itself) or an enemy; a stat raise on an ally or cut on an enemy |
| Turns of control | Stun, Sleep, Taunt, Root, Charm and the like, for the turns the ability gives |
| Revives, towers, biggest hit, crits, taken for allies (Guard) | As named |

## Points

1 per 10 damage, 0.4 per 10 taken, 0.3 per 10 mitigated, 1 per 10 healing; 12 a knock-out, 5 an assist,
-10 knocked out; 4 a monster, 15 a boss; 2 a buff, 2 a debuff, 3 a turn of control, 10 a revive, 8 a tower.
Kept to the tenth. The MVP has the most; a tie goes to fewer falls, then more damage. Grades: S the MVP, A within
15% of its points, B within 30%, C the rest.

## The screen

When a battle (or a replay) is decided: the result, map and time; the MVP card with where its points came from;
the moments (click one to watch the replay from a few seconds before it); tabs Overview, Damage, Support,
Control with both sides' units, the battle's best of each column marked in gold; a unit's own page (its numbers,
damage by ability, damage taken from whom). Buttons: Watch replay, Rematch, Change setup, Main menu (online:
Back to lobby for the host), and See the board, which puts the report away until Battle report is pressed.
