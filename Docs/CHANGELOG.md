# Tactical Masters: what changed

Newest first. Each version lists what changed since the one before it. Both players in an online match need the
same version (the online protocol number must match).

## v20 (not released yet, protocol 21)

From the v19 play test
- Turns: no light under a unit whose turn is up (it washed out the ground); a gold READY tag over its health bar instead.
  Double click a unit's chip on the turn order, or its portrait on the left, and the camera goes to it.
- Units are drawn half as big again; watchtowers are taller and stouter, their fire lights more ground.
- Tanks: the zone of control is three big shields on a broken ring turning round its edge, for an enemy tank in
  sight and yours under the pointer.
- Casting: a card over the caster with the ability, the seconds left and a thick bar; a sigil turning under it;
  every caster's target area on the ground with a dashed line to it (before, only a monster's wind-up); and a
  banner across the top for an enemy cast that will catch units of yours.
- Tall grass: every map has some. A unit in it can't be seen from more than 3 m away until it strikes out or is
  struck, and hides again from its next turn. Yours says "hidden in grass".
- Wells (healing springs): their own healing (Developer Tools, "Spring healing", 8%) and, once used, they run dry
  for 3 of that unit's turns ("Spring rest"); a dry one says so on the board. Burning ground keeps "hazard_percent".
- Chests: a column of light over each in its tier's colour (never through the fog); the pointer on one lists every
  item in it with its numbers.
- Vanish: your vanished unit is drawn as an outline only.
- Setup: "Duplicate classes" off gives one of each class in the battle (online, the host's). The class picker and
  the draft show the class under the pointer in full: its numbers and its four abilities.
- Summons: each pet has a body like its name (the Golem a rock elemental, the Seraph an angel, the Yeti a great
  ape...), and the summoner's page in the Unit Guide shows the pet in full.
- Battle report: Healing received, per unit.
- Online protocol 21.

Fixed
- The crash about two minutes into an idle menu, and now and then mid-battle (every crash of the v19 play test, both
  PCs: the renderer reading freed memory in SetShaderParameters). A material the cook left without shaders
  (M_Bug_Mesh, read in with Morigesh's clips) was unloaded by the garbage collector and drawn from afterwards. Such
  materials are now kept loaded for good. The heroes, effects and sounds loaded for a battle also stay loaded until
  the next battle's are in, so nothing is read from disk mid-battle.

Balance (the held changes)
- Four weak melee and caster classes hit harder and have more HP: Thunder Fist (HP 92, Thunder Jab 45, Uppercut 62,
  Fury 69), Stone Brawler (HP 120, Jab 45, Uppercut 62, Fury 69), Stormcaller (HP 74, Thunder Bolt 70, Chain
  Lightning 54, Tempest 60) and Samurai (HP 95, Iaido Slash 43, Draw Out 37, Masamune 30).
- Three strong casters are weaker: Frost Witch (Speed 7, Shatter 50, Frost Doom 43), Sea Witch (Tide Doom 39) and
  Oracle (Divination 22).
- Online protocol 20: builds from before this can't play these.

New abilities
- Six casters' staff strike (a weaker Bolt the computer never used) is now a signature ability: Aeromancer's
  Tailwind (Haste on allies within 3 m, 1 turn), Cryomancer's Frost Armor (Protect on allies within 3 m, 1 turn),
  Druid's Entangle (Roots enemies in an area), Flame Sorcerer's Kindling (Oils an enemy: fire lasts twice as long),
  Necromancer's Wither (Wounded: healing halved) and Stormcaller's Downpour (Wet in an area: lightning shocks them).
- Five hexers' curse (a stat curse the computer never cast) is now a status of its own, with a light hit: Ash Witch's
  Pitch Curse (Oiled), Sea Witch's Drown Curse (Wet), Exorcist's Banish (Silence), Warlock's Rot Curse (Decay) and
  Oracle's Foretell (Marked). Oracle's Hex 32 to 28, Warlock's 37 to 33.
- Each has its own look and sounds (Cast Studio) and icon.
- Online protocol 19: builds from before this can't play these.

Balance
- A tuning pass over the classes the class lab found clearly too weak or too strong (35 classes). Many damage classes,
  above all the summoners and callers, hit harder and have more HP; most summoners and the Cryomancer are faster
  (Speed 6 to 8), the Golem Master faster still with a quicker Summon Golem; the Geomancer casts faster. Too strong
  and now weaker: Rime Warden, Frost Hexer, Time Mage, Blazeblade, Siren. Frost Witch, Sea Witch and Oracle's
  ultimates slow for 1 turn, not 2-3. Full list: Docs/design/feat-class-balance.md, "The 2026-10-03 tuning pass".
- Online protocol 18: builds from before this can't play these.

## v19 (packaged 2026-10-03, protocol 17)

New
- Movement skills. The Berserker's Leap Smash now leaps: he flies through the air (his jump animation) and smashes
  round where he lands. Dragoon's Jump arcs through the air too; Sky Lancer's dive, and new dashes for the Windblade
  (Gale Dash, in place of Gale Lance) and Wind Dancer (Gale Rush, in place of Gale Kunai), rush along the ground.
- Stealth for the Ninja. Smoke Bomb now bursts round the Ninja in smoke: it Vanishes for 2 turns (unseen by the
  enemy until it strikes or is hit) and Slows enemies within 2 m. New Shadow Step (in place of Shuriken): vanish in
  smoke and reappear behind an enemy 2-6 m away, striking its back.
- Ten statuses put to use: Paladin's Aegis gives Protect, Cantor's Holy Anthem Shell, Lumimancer's new Prism Ward
  Reflect, Seraph Caller's Seraph Pact Reraise, Time Mage's Time Stop Stop, Siren's new Siren Song Charm, Dread
  Knight's Shadow Bash Terror, Necromancer's Umbral Burst Decay, Tide Cleric's Rod Wet, and Chemist's Oil Bomb Oil.
- Online protocol 17: builds from before this can't play these.
- Camera rules: the camera no longer jumps while you are giving an order. It moves by itself only when your hands
  are off the controls and only because one of your units just ended its turn, never late, and not at all if the next
  unit is already on screen. A unit that becomes ready while you are busy with another (or planning) doesn't take
  over: a note at the top names it with its time left and a Go button, an arrow at the screen's edge points to it, and
  N goes to it. Options: the camera follows Always / When I'm idle (default) / Never (V cycles), plus switches for the
  walk lead and a small "Camera held" note.
- Facing on arrival: press where a unit should walk, drag the way it should face, and let go. It ends its walk facing
  that way (one of eight; the ghost shows them), so it can keep its front to the enemy and its back off a flank. A
  plain click walks and faces its last step, as before. Works for planned walks too. Online protocol 16: builds
  from before this can't play these.
- Quick Cast (Options, a box next to each ability key, off by default): hold the key to aim, let go to use the ability
  where the pointer is, no click. Let go over a button to put it down.
- Combat feels heavier: a big hit holds the world still for an instant, a critical slows it for a beat, a unit going
  down slows it and shakes the camera, the camera kicks by how much a hit took, and knockback and the struck unit's
  flash grow with the wound. An ultimate brings the camera in close on it for a moment (Options; any key skips it).
- Fast-forward: hold X (rebindable) while none of your units is ready, or turn on "Fast-forward the other side's
  turns" in Options, and the battle runs three times as fast until one of yours is. Not online.
- A click on the board answers: a ring spreads where it was taken (gold a walk, orange a Go To or a walk into range,
  violet an ability, red refused) with a short tick (sounds.json event `order`). The pointer shows what a click would
  do: a hand, crosshairs, a barred circle, the Go To's arrows.
- Walks curve round corners instead of turning on each node, ease in and out, and long ones go up to a third faster.
  While aiming a walk, a ghost of the unit stands where it would end, facing the way it would face, and the path is
  orange where breaking away from an enemy costs extra and red where a tank's zone would stop it.
- The selected unit's portrait frame is a clock: it drains as the turn's time runs out, red in the last five seconds.
- Camera: the wheel zooms toward the pointer, the pointer at the window's edge pans, and a walk to the edge of the
  screen brings the camera along (Options for the first two). The camera moves at the same speed in a slow beat.
- Options: "End a unit's turn by itself when it has nothing left to use" (off by default).
- Online: an order given while the last is still with the host is kept and given when the answer comes, and your own
  walks set off at once instead of waiting for the host (and walk back if it refuses).
- Sound effects in Cast Studio. The class creator's Effects and sound panel has a Sound effects section: pick any
  sound from the Unreal project (search by name, filter by pack, hear it first), put it at any moment of an ability
  (as the swing begins, at the release, in flight, on each unit hit, on the ground, as it ends...), and set its volume,
  pitch, delay, where it is heard from, and whether it plays always or only on a critical hit. A sound on the swing can
  be timed to the wind-up, the release or the recover, a share of the way through, by dragging it along a ruler of the
  swing or with "At the contact"; the game times it the same way on whichever body swings. The sketch plays the sounds
  in time with the filmed body (with a Sound on/off switch). An ability with a sound of its own plays it in place of
  its usual swing and landing sounds; its voice still plays. Nothing published: every ability sounds as before.
- `Tools\SoundCatalog.bat` copies every sound in the project out as a .wav for the class creator (a browser cannot
  play Unreal's sound files); the creator's Copy the sounds button runs it.
- A sound when you try to move a unit with no movement left (the move key, sprint, or a click on open ground), and
  a different one when you try to use an ability after the unit has acted. Both are in sounds.json (events noMove,
  noAction) to swap.
- Status icons are a quarter bigger by default (a size you chose before is made a quarter bigger too), and so are
  the move and act tokens. The flashing border round what is left of a turn (the move tile, or the abilities) is
  thicker, glows, beats faster and never goes fully dark.
- Queued moves, smoother. A Go To whose next walk would be short (the last stretch, or a blocked way) hands you the
  unit before it walks, so no turn is spent on a step: Keep going (G) walks it, or give it something better to do.
- Abilities out of reach can be queued. Aiming at a target the unit can't reach this turn says OUT OF RANGE plainly,
  in orange, with the way and how many turns; a click sends it into range over as many turns as it takes, and it uses
  the ability the turn it gets there (following a unit target, and ending if the target dies or is lost from sight).
- A unit with queued orders (a Go To, or a plan for its next turn) that sees an enemy it hadn't seen has the orders
  cancelled and is handed back to you at once. Seen on a walk-and-end step, the turn is not ended: it can still act.
- Ground fills under targeting (walk area, ranges, areas, zones, wind-ups) are nearly see-through; their edges carry
  the shapes.
- Cast Studio's effects and sounds are now laid out like a music sequencer: every effect, light, shake, sound and
  the animation's wind-up, release and recover is a card on a track under a ruler in seconds, with the phases and the
  swing's parts above it and a playhead. Drag effects and sounds from the library (Effects, Sounds, ★ favourites,
  Presets) onto a track; drag a card to move it, its right edge to trim; click it to edit; ▶ plays it alone with the
  body. Cards snap to the swing's parts, the contact and the phases (Shift places freely); tracks can be muted or
  soloed; zoom widens the timeline.
- Copy & presets in Cast Studio (the right-hand pane of the Effects tab): copy an ability's animation, effects and
  sounds (any of the three), paste them onto another ability in place of its own (with Undo), or save them as a named
  preset to apply to any ability later. Effects and sounds can be starred as favourites (☆), shown first and on their
  own with Favourites in the effect and sound browsers.
- The class creator's library: every ability now names an effect that fits its name (32 given one, 44 changed).

## v18 (2026-10-02, protocol 15)

New
- Nothing needs the Godot version any more. The rules tests compare against the game's own recorded baselines
  (`Tests\Baselines`), and `scripts\test.bat --rebaseline` records them again after a rule is changed on purpose.
  The class lab's `--rules godot` is now `--rules classic`.
- Cast Studio, phase 2: each ability's look can be authored in the class creator's Cast Studio tab, moment by moment
  -- as the cast begins, while it charges, as the swing starts, at the release, on each shot in flight, on each unit
  it touches, on the ground it covers, on whoever wears a status it gave, each turn that status ticks, as it ends, on
  the pet it calls -- with effects Unreal filmed, coloured lights and camera shakes, each placed on a hand, the head,
  the body, the ground or the area, sized in centimetres or to match the ability's area, and played always or only on
  a critical hit. "Import today's look" starts from what the game shows now; "Compare with today's look" plays it
  beside. Each ability can keep today's look and add to it, drop its class file's effect, or replace today's look.
  Publish writes `Content/Data/CastStudio/AbilityLooks.json`; the next battle plays it. Nothing published: every
  ability looks as before.
- The class creator's effect sketch shows the body the class wears (its filmed animation), with a Select model button
  when the class has none.
- Cast Studio, phase 1: each ability's animation can be chosen in the class creator's new Cast Studio tab -- a
  wind-up, the release and the frame it connects on, a loop while it charges, a recover -- from clips Unreal filmed,
  with Unreal closed. Publish writes `Content/Data/CastStudio/AbilityAnimation.json`; the next battle plays the picks,
  and hits, numbers and shots land on the chosen frame. Nothing published: every ability moves as before.
- The animation filming (`Tools\AnimCatalog.bat`) films at 32 frames a second from one fixed camera, records where
  each body's hands, head, weapon and root are on every frame, lists every clip each body's skeleton can play, and
  films the published picks. `Tools\AnimClips.bat` films just the picked clips (the creator's "Film the picked clips").
- Camps hear fighting. Area blows, fire and kills within 12 m of a camp still to wake fill its noise (three steps,
  shown on its label; it fades one step for each 10 s of quiet). Full, the camp gives its warning and wakes, and goes
  for the side that made the most noise. A monster killed from full health in one blow gives its killer 20% of a gauge
  (a clean kill).
- Boss wind-ups can be read and broken. A monster's charged attack is drawn where it will land, filling as the cast runs
  out, with the seconds over it, a mark over everyone it would catch, a purple ghost on the turn order where it lands,
  and, round a boss, a gold arc behind it. Three hits from behind break a boss's wind-up and stagger it.
- The boss bar, under the turn order while a boss is in sight: its health, phase, wind-up and stagger.
- Two new setup options, off by default: Bosses hunt (a boss goes for whoever has hurt it most, marked Hunted, until
  they fall or it loses sight of them for 3 of its turns; the boss bar lists who it remembers) and Claim the boss (the
  boss bar splits its health by each side's share; the last blow gives its side the Boss's Boon, +10% damage for 3
  turns, and the other side, if it dealt 30% of the boss's health, a rare item in its stash).
- Online protocol 15: both players need this version.
- Cooldowns on the turn order. On the turn squares, a dot in a square's corner for each ability cooling down;
  point at the portrait and the unit's next three turns drop down as ghost squares, each showing which abilities
  are back on it and when. On the sliding bars, each ability cooling down is a small tile on a stem at the time it
  comes back (above the blue bar, below the red). While aiming an ability, it shows in gold where it would come
  back if used now.
- Point at a unit's chip, square or cooldown tile and the unit lights up on the board; point at the unit and its
  chip or square lights up.
- The squad strip, down the left edge under the log: each of your units as a health ring round its face, with its
  statuses and the turns left on each. The unit acting now, and one you point at (here, on its square or on the
  board), opens to its full row: name, move and act left, health as a number, when it acts, and its abilities with
  the turns until each is back. Click a row to pick the unit. It can be moved in Edit layout and turned off in Options.
- The turn squares show each unit's statuses under it, with turns left (three at most, then "+N"), for both sides,
  and its health bar goes from green to gold to red as it falls.
- A status's turns-left number turns red when it ends as the unit's next turn begins.
- Open odds. Aiming a damaging ability shows a card over each unit it would hit: a bar split by the chance of a hit,
  a crit, a graze and a dodge, the damage of each, the unit's health with where each would leave it, and the chance
  it is knocked out (shields counted). Area blows card the three likeliest to fall; the rest keep one line, with
  their KO chance.
- The enemy's odds. Point at an enemy (on the board, or its square or chip) to see its best blow on each of your
  units on its next turn, with the same numbers: a line to each unit it reaches (solid to the one most at risk,
  dotted where it would have to walk first), the odds over each, and a card at the top right listing all of them.
  While you plan a walk, the end of the path says which enemies can reach you there.

- Zones of control on the ground. Every tank you can see wears its zone: a bold dashed orange ring round an enemy
  tank (a walk into it ends there, labelled "holds the line" while you plan a walk), a quieter blue one round yours. A
  tank in the fog shows nothing.
- Planning a walk: a thin ring round each enemy in sight is its reach (breaking away costs a metre, said at the start
  of the walk), ground the enemy's tanks take away from the walk is hatched, and pointing at it marks where a walk
  that way would end and which tank ends it.
- Planning a tank's walk: its zone shows where the walk ends, with each seen enemy's way to your back line drawn as
  cut or open, and how many it cuts.

- Summoners call up pets. The eleven summoner classes' summons now bring a pet (a Golem, Yeti, Lich, Ifrit,
  Seraph, Treant, Salamander, Thunderbird, Roc, Thorn Beast or Leviathan) where they land. The computer plays it for
  its caller's side for 3 of its turns; then it leaves. Nothing raises a pet and it never counts towards a win.

Changed
- Class balance (from 8,080 test battles): the summoners' rods reach 1-7 m, their summons cast in 1 s every 3
  turns, and their ultimates 1 s sooner; Frost Witch and Rime Warden's Hex 38 (was 44), Oracle's 32 and Frost
  Hexer's 33; Gale Stance gives Speed +2 (it did nothing); seven mages' staffs strike from 1-5 m; Mountain Sentinel
  Speed 8 and Glacier Guard Speed 6; Storm Bulwark and Thunder Herald's Speed auras +1 (was +2); Herbalist's Thorn
  Bolt reaches 1-7 m; Dirge Singer's Ballad Crit +12 and Finale heals 22.
- Watchtowers: taking one takes 1 turn by default (was 2), and a held tower sees 28 m by default (was 14; the
  Developer Tools slider now goes to 60). A rule number you changed yourself in Developer Tools stays as you set it.

Fixed
- Watchtowers no longer stand against the edge of the map: each keeps at least 6 m (three tiles) from every edge.
- A crash in battle (v17, twice, on DirectX 12): heroes' animations no longer change their materials as
  they play (Sparrow's bowstring, for one), which is where the game fell over. They look the same.
- Long tooltips (a map's description on the setup screen, for one) wrap onto several lines and stay on the
  screen, instead of running off its edge in one long line.

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
