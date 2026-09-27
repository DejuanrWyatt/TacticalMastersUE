# feat-match-flow: title, setup, and back again

Status: Implemented ahead of approval, at the user's request (2026-09-27). The title and setup screens were
seen in screenshots, and Start Battle was driven by `-tmmenushots`. The other buttons have not been clicked
by a person yet.

## Flow
After the Godot game's `scripts/main_menu.gd` and `scripts/ui/battle_setup.gd`.

1. **Title** (main_menu.gd:38-116): Play vs Computer, Two Players (Same Device), Computer vs Computer, Quit.
   The board stands dimmed behind it with the clock stopped.
2. **Setup** (battle_setup.gd):
   - Each side's four classes: click a slot to cycle through the six built-in classes, or use Random team
     (a tank, two damage, a support; class_list.gd:62-77) or Default.
   - Vs Computer: which side you play.
   - The difficulty of each computer side.
   - The seed: new each battle, or fixed. Fixing it keeps the seed of the battle just played.
   - Map: Highlands, the only map ported.
   - The board behind the panel shows the teams as they are chosen.
3. **Battle.** The log opens with the seed and who plays each side.
4. **Esc** cancels an aim. With nothing to cancel, it opens the menu, which pauses a local game: Resume,
   Restart battle (same teams, same seed), Change setup, Main menu.
5. **End**: Rematch (R, with a new seed unless the seed is fixed), Change setup, Main menu. The panel shows the
   seed. With two people or two computers it says "Blue wins" or "Red wins".

## Rules it keeps
- The menus decide only what a battle starts from: the rosters, who plays each side, the difficulties and
  the seed. The seed goes to `FBattle::Start`. Everything after that is orders through `Submit`.
- Each side has its own `FAIPlayer`, seeded `seed + team`, so two computers at different difficulties can
  play each other. Godot leaves its computer's generator unseeded (battle.gd:199). That only matters for easy
  and medium, and hard never draws from it.
- A fresh seed is a number from 1 to 999999, so it can be read off the screen and set again.
- Unattended runs (`scripts\battle-test.bat`) skip the menus exactly as before. `-tmmenushots` takes
  pictures of the title and setup screens and then plays the battle.

## Not done yet
- Online play, How to Play, the Unit Guide, Options, Developer Tools.
- Other maps, saved teams, hold-the-middle, time limits, and planning time (placing units before the fight).
- The other 81 classes, which wait on the importer.
- Easy and medium don't yet make Godot's random mistakes (`Randf` isn't ported).
- Typing in a seed. It can only be "new each battle" or "fixed to the last one played".
