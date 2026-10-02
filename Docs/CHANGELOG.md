# Tactical Masters: what changed

Newest first. Each version lists what changed since the one before it. Both players in an online match need the
same version (the online protocol number must match).

## v17 (2026-10-02, protocol 14)

New
- A new status, Wounded: a Wounded unit receives only half of any healing (heal abilities, Regen, healing
  springs, mending when left alone, lifesteal). Nothing applies it yet; abilities and items can now use it.
- Go To marks each turn's stop with a ring on the ground and its number, the last ring gold, in place of the
  boxes that sat on the screen.
- The action bar: Move, Sprint, Items, Capture and End have an icon beside their word. An ability cooling down
  is greyed, with an hourglass badge for the turns left and a step bar that fills as they pass.
- Taking a watchtower: its fire grows from embers to full flame over two seconds, and what it reveals spreads
  out from it with the flames instead of appearing all at once. Enemies in its reach show as the light reaches them.
- What is left of a unit's turn: two round tokens in front of its ring, a footprint for the move and a star for
  the action, ringed while still to use and grey and struck through once spent. On the action bar, what is left
  flashes once half the turn is spent (Move after acting, the abilities after moving), and the spent half says
  "moved" or "acted".
- The battle report shows the items each unit carried: in the tables, on each unit's page and on the MVP card.
  Point at one to see what it does.

Fixed
- The battle report: the MVP's summary no longer runs out of its card, and the moments name the class
  ("War Drummer (Blue) falls") rather than its internal name.
- A crash when Narbash's units (War Drummer, Dirge Singer, Cantor, Piper, Gale Minstrel, Winter Skald) were on
  the field: the one part of their outfit the game can't draw (the legs and drumsticks) is drawn plain instead.

## v16 (2026-10-02, protocol 14)

New
- Tanks hold the line: an enemy that walks within 1.8 m of a unit whose first role is tank has to stop there
  (zone of control). The walk area ends where a walk would be stopped. A unit already beside a tank can walk away.
- The setup screen remembers your last battle: teams, difficulty, map and look, victory and time limits,
  watchtowers, items, camps, elements, friendly fire and respawns are kept after the game is closed.
- Developer Tools: the setup screen's own options (camps, random boss, elements, friendly fire, respawns) are no
  longer shown there as well, where a change was silently overruled by the setup screen. They are kept with the
  setup screen's choices instead.
- This changelog, next to the game in every new version.
- Replays: every battle that is decided is kept (the newest 50). Watch replay at the end of a battle, or
  Replays on the title screen. Play, pause, 1/2x to 4x speed, step from order to order, jump anywhere on the
  timeline (falls, revives, towers and camps are marked on it), and watch with everything shown or through
  either side's fog. A replay made before the rules changed says where it stops matching.
- Battle report at the end of every battle: an MVP chosen on damage dealt, damage taken and damage stopped by
  Armor and Resist, healing, kills and assists (within the last minute), deaths, camp monsters and bosses, buffs,
  debuffs, control, revives and towers, with why it was chosen. Tabs for Overview, Damage, Support and Control
  compare every unit; click one to see its own battle (damage by ability, who hit it). The key moments are listed,
  and each one jumps to that moment in the replay.

Changed
- Knight: Speed 6 to 8, HP 105 to 115, MagDef 6 to 9. Shield Bash taunts the enemy for 2 turns instead of
  stunning it. Guard is cast on an ally: single-target hits aimed at that ally go to the Knight.
- Archer: Speed 12 to 10, Sight 13 to 11, Bow Shot reach 10 m to 8 m, Aimed Shot every 3 turns instead of 2,
  Crit 15 to 10.
- Berserker's Rage and Samurai's Meditate describe what they really do (Crit, not the old AttPwr).
- Frost Hexer and Frost Witch are no longer the same class: the Hexer controls (Chill, Mark, sleep), the Witch
  hits hard (less health, more Crit, a heavy Shatter in place of the sleep).
- 28 ability descriptions in 24 classes now say what the abilities do (nothing about how they play changed):
  the Slumber abilities and the Oracle's Sleep stun rather than put to sleep; ten auras, stances, pacts and curses
  give Crit, not the old Power; Lullaby, Hymn of Life, Leap Smash, Fire Bomb, Quake, Tectonic Rift, Smoke Bomb,
  Masamune, Ifrit and Time Stop give their statuses' lengths in turns, not seconds.
- Items retuned for the new Armor, Resist and Evasion rules, so each protects about as much as before: Padded
  Vest and Chain Vest Armor 3; Bulwark Plate Armor 6; Warden's Plate 6 and 6; Aegis of Dawn 8 and 8; Leaden
  Mantle 10 and 10; Warded Sash Resist 3; Skink Scale Resist 4; Mirror Cloak Resist 3 and Evasion 5; Spirit
  Bangle Evasion 3; Anchor Stone +6; Last Stand Band +9 Evasion; Nightcloak +15 Evasion.

Fixed
- The computer no longer aims area abilities at ground it cannot see (Golem Master, Roc Caller, Thunderbird
  Caller lost turns to it).

## v15 (2026-10-01 21:28, protocol 13)

New
- Combat text: red damage, big bold critical hits with a "!", dark red burn ticks with an ember glow, crimson
  bleed, green healing, pale green regen, status names in capitals in their own colours, a pale "graze" tag.
- Your units' health bar shows over their heads for 3 seconds whenever they deal or take damage or are healed;
  the health just lost flashes and drains away over 2 seconds.
- Go To: click beyond the walk area to send a unit there over several turns. Each turn it walks as far as it can
  and ends its turn (or waits for you, chosen on its strip). It stops if an enemy comes into sight, it is hurt, or
  the way is blocked: G keeps going, Backspace cancels. Numbers on the ground mark where each turn ends.

Fixed
- A crash on the joining player's machine when a burst effect finished as unit portraits were being drawn.

## v14 (2026-10-01 20:00, protocol 13)

New
- Defense rules: Armor (AttDef) and Resist (MagDef) take a share off every hit (30 halves it) instead of being
  subtracted. One Evasion stat: an evaded hit is dodged outright 1 time in 10 and grazed for half damage otherwise.

Changed
- The setup screen's Start button no longer covers the last option, and the "not ported yet" note is gone.

Fixed
- A crash when certain effects and bodies were drawn (four materials shipped without their shaders: Morigesh's
  bugs, Narbash's legs, Rampage's rock). They now draw plainly instead.

## v13 (2026-10-01 17:45, protocol 12)

New
- Queued orders: click one of your units while it waits to plan its next turn (a walk, then an ability aimed from
  where it ends); G plans the current turn and goes in one; Ctrl+click adds waypoints to any walk (up to 4).

## v12 (2026-10-01 16:10, protocol 11)

Fixed
- Picking the Berserker crashed the game every time.
- Crashes when switching fullscreen, and when the Codex hero changed too quickly.

## v11 (2026-10-01, protocol 11)

New
- Combat log redesign: one line per action, with All, Combat, Mine and Key moments tabs.
- The options menu scrolls; camp respawns as a setup option (off by default); auto-recenter as an option (V).

## 2026-10-01 lobby build (protocol 10)

New
- Online lobby for up to four players: choose sides, units shared out in joining order, the computer fills an
  empty side; a draft with bans.
- Friendly fire as a setup option.
- Team stash: items picked up go to the side and are equipped from the team items screen.
- Thinner aiming lines for every shape, a dimmer walk area, range on ability buttons, Tab shows or hides status
  bars ("next unit" moved to N).

## 2026-09-30 build (protocol 7)

New
- Watchtowers, items, neutral camps and bosses, the second set of statuses, four large maps, lighting and
  foliage, ability effects, fog of war, cliffs and ruins, item pickups by walking, the Codex, hazard looks.

## 2026-09-29 build (first play-test build)

New
- The first packaged build for online play: 87 classes, three new maps (Caldera Crown, Frostwall Town,
  Riverwatch Fords), background hero loading, turn cards, dodges, overhead gauges.
