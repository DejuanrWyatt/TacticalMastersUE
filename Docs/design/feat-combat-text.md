# Combat text and the fight health bar

2026-10-01. Asked for: "Burn status should be dark red, healing should be green, damage should be red, critical strikes
should be bold etc. Also when a unit you control deals damage or takes damage, show the unit's health bar for 3 seconds
then it disappears again. The damage taken should be represented over 2 seconds with the damage taken flashing during
the duration." Mocked up in the "Combat Text Mockups" canvas; the human said to build it with the suggestions there.

## Floating text (TMBattleDirectorBlows.cpp `ShowOne`, drawn in TMBattleHudPanels.cpp `DrawWorldWords`)

| Kind | Look |
| --- | --- |
| Damage | red (255,74,61), bold |
| Critical strike | "-96!", heavier, 1.45x, pops in at 140% and settles; replaces the separate "critical!" word |
| Graze | the halved hit a size smaller, with a pale blue "graze" after it; no separate "graze" word |
| Dodge | pale blue-white "dodge" |
| Burn tick | dark red (163,21,15) with a pale ember edge and an orange glow, 0.85x |
| Bleed tick | crimson (214,51,108), 0.85x |
| Other ticks (ground, decay) | red, 0.85x |
| Healing | green (63,212,106), bold; a critical heal "+64!" 1.4x with the pop |
| Regen, mending, healing ground | pale green, 0.8x |
| Shield soak | shield blue "soaked 12", 0.8x |
| Status applied | its name in capitals, 0.7x, bold, in its own colour (`StatusWordTint`: set by hand for the common ones, else its glow colour lightened) |
| Knocked out / revived | "DOWN" red 1.1x / "UP AGAIN" gold |

"Bold" is the word drawn twice a pixel apart. The older words (item pickups, sleep, ability names) are drawn as before.
A Critical or Grazed event marks the unit (`BlowMarks`) and the Hit after it is drawn accordingly.

## The health bar in a fight (`PopHealth`, `ATMBattleHud::PopBar`)

- Only this side's own units. When one takes damage, deals damage, or is healed, its bar shows over its head for
  3 seconds (`HpPopSeconds`), fading in and out; each new blow restarts the 3 seconds.
- Damage: the green drops at once; the part lost stays in the bar, flashing pale and red, and drains to the green over
  2 seconds (`HpDrainSeconds`, eased). A hit while it drains adds on from where the drain had got to.
- Healing: the part gained flashes pale green for 0.9 s.
- Enemies keep only their ring on the ground; pointing at any unit still shows the full read, which takes the place of
  this bar while pointed at.
- Real seconds (`FPlatformTime`), so a slowed big hit does not hold the bar up. Screen only: nothing in the rules.
