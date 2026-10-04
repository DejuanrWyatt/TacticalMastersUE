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
- Claimed: nothing in this folder now (packaging config committed in `a75a859`).
- State: packaging from a clean worktree of `a75a859`, **without watchtowers**, at
  `E:\UnrealProjects\TM_Release` (its `Content` is a junction to this folder's). Log: `E:\Builds\package.log`.
  Started 2026-09-29; the cook takes an hour or more and uses ~10 GB.
- 2026-09-29: the cook ran the PC out of memory again at 82% (9059/11110; "paging file too small").
  The human is enlarging the page file (32 GB on E:) and restarting the PC. **To resume** (keeps the
  82% already cooked), from any session, with the PC otherwise quiet:
  `E:\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat BuildCookRun -project=E:\UnrealProjects\TM_Release\TacticalMasters.uproject -noP4 -platform=Win64 -clientconfig=Development -build -cook -cookincremental -stage -pak -iostore -prereqs -archive -archivedirectory=E:\Builds\TacticalMasters -utf8output -unattended > E:\Builds\package.log 2>&1`
  Then: run `E:\Builds\TacticalMasters\Windows\TacticalMasters.exe` once to check it opens and plays,
  and copy the online guide (`HOW TO PLAY ONLINE.txt`, drafted by this session; rewrite it if lost:
  host = Play Online -> Host Game, TCP port 7777, UPnP or forward by hand; join = Host address -> Join Game)
  into `E:\Builds\TacticalMasters\Windows`.
- Please don't: delete or move `E:\UnrealProjects\TM_Release`, or change Content assets, while it cooks.
  A second heavy build at the same time may run the PC out of memory again.

### Lobby and items session (the watchtower session, continued), 2026-10-01
- Doing: the online lobby (up to four players, sides chosen, the draft), friendly fire, the team stash and
  team items screen, thinner aiming lines, a dimmer walk area, range on ability buttons, Tab for status bars.
  Since: the combat log redesign (one line per action, tabs All/Combat/Mine/Key), the options menu scrolls, camp
  respawns as a setup option (off by default), auto-recenter as an option with V to toggle, and crash fixes
  (Codex hero flipping, fullscreen toggling, render targets no longer resized in place, cloth and morph targets off).
  Then queued orders (Docs/design/feat-move-queue.md): plan a waiting unit's next turn, plan a turn and go in one
  (G), and waypoints (Ctrl+click). Then Go To, Civilization III style (feat-move-queue.md, "Go To"), and the new
  combat text and the health bar that shows over your own units in a fight (feat-combat-text.md).
- 2026-10-04 21:40: all of this session's work to date (the list below and every Log entry since) is being
  committed to git by `E:\Builds\agent-commit.bat`, at the human's word; result and hash in
  `E:\Builds\agent-commit-status.txt`. Anything that script's after-status still lists was not this session's.
- Claimed before that commit: `Source/TacticalMasters/` TMNet.{h,cpp},
  TMBattleDirector.{h,cpp}, TMBattleDirector{Online,Lobby,Draft,Camps,Indicators,Fog,Loading,Showcase,Motion,Plans,Feel}.cpp (Plans and Feel are new), TMBattleHudFeel.cpp (new), TMBattleHud.{h,cpp},
  TMBattleHud{Lobby,Panels,Codex,Options}.cpp, TMBattleHudStyle.h, TMBattleDirectorBlows.cpp, TMBattleDirectorAbilityFx.cpp, TMDrawable.{h,cpp} (new), TMSettings.{h,cpp}; `Source/TMSim/` SimOrder.h, SimBattle.h, SimTypes.{h,cpp},
  SimTargeting.cpp, SimAI.cpp, SimCamps.cpp, SimWorld.cpp, SimOrderText.cpp, SimUnit.h, SimMovement.cpp, SimBattle.cpp, SimResolve.cpp; `Tests/` SimCampTest.cpp, SimPlayTest.cpp, SimMoveTest.cpp, SimOrderTextTest.cpp;
  `Docs/design/feat-lobby.md`, `Docs/design/feat-team-items.md`, `Docs/design/feat-move-queue.md`, `Docs/design/feat-defense.md`, `Docs/design/feat-combat-text.md`, `Docs/design/feat-class-balance.md`, `Docs/design/feat-combat-feel.md` (new), `Docs/CHANGELOG.md` (new).
- Online protocol is now 16: builds from before today can't play these.

## Notes for others

- For every session (from the watchtower session): the device bridge can write an older copy of a file when the same
  container path is committed twice. Commit each change from a fresh path, and re-stage to check what landed.
- To the watchtower session (from packaging): online protocol v2 means a build with your changes can't
  play one without them. Both players need the same build either way.

## Log

- 2026-10-04 21:40 (lobby and items session) v20 built and packaged from today's code
  (`E:\Builds\TacticalMasters-2026-10-04-v20`, 14:25 local; every rules and cast test passed). Then, at the human's
  word, the session's uncommitted work committed to git: `Source`, `Tests`, `Tools`, `Docs`, `Content/Data` (by path,
  not `-A`), and the creator's `data/library.json`. Script `E:\Builds\agent-commit.bat`; hash in agent-commit-status.txt.
- 2026-10-04 20:05 (lobby and items session) The human's picks from "Battle Indicator Alternatives": Ready D (READY
  tag), Tank B (three shields, broken ring), Cast A and B (sigil and enemy-cast banner added). TMBattleHud.{h,cpp},
  TMBattleHudPanels.cpp; not built. Content/Data/Icons/hud/ready_gem.png is no longer used.
- 2026-10-04 19:40 (lobby and items session) The v19 play test findings, not built, not committed to git. Game
  (Source/TacticalMasters): TMBattleDirector.{h,cpp}, TMBattleDirector{Chest,Foliage,Indicators,Loading,Lobby,Looks,
  Motion,Report,Tower}.cpp, TMBattleHud.{h,cpp}, TMBattleHud{Codex,Lobby,Panels,Report}.cpp, TMRobotPlayer.cpp,
  TMNet.h (protocol 21). Rules (Source/TMSim): SimTypes.{h,cpp}, SimBattle.{h,cpp}, SimMap.{h,cpp}, SimUnit.h,
  SimWorld.cpp, SimAI.cpp, SimResolve.cpp, SimCamps.cpp; Tests SimPlayTest.cpp, SimStatusTest.cpp (springs, tall
  grass); Tools/ClassLab/ClassLab.cpp (map grass). Data: the 8 map files ("grass"), CharacterMap/characters.json
  (pet bodies), Icons/hud/{zone_shield,ready_gem}.png (new). Every rules test passes built with g++ here; the game
  code was reviewed but not compiled. Mockups: "Battle Indicator Alternatives" (ready, tank, cast); B, A, A built.
- 2026-10-04 18:40 (lobby and items session) The v19 crash fixed, not built, not committed to git: TMDrawable.{h,cpp}
  (KeepBrokenLoaded, WatchBrokenMaterials: materials cooked without shaders are rooted, checked before every garbage
  collection), TMBattleDirector.{h,cpp} (BodyKept; WatchBrokenMaterials in BeginPlay), TMBattleDirectorLoading.cpp,
  CHANGELOG v20. All 9 crash reports from the v19 play test (5 the partner's, 4 the host's) were one crash, the same
  stack; "gc.CollectGarbageEveryFrame 1" made it at once (17 s), so: an unloaded asset. To check a build: that same
  console command should no longer crash it, menu or battle.
- 2026-10-04 17:05 (lobby and items session) The held buffs and nerfs, not built, not committed to git: 7 class files
  (Thunder Fist, Stone Brawler, Stormcaller, Samurai buffed; Frost Witch, Sea Witch, Oracle nerfed), protocol 20
  (TMNet.h), Docs/design/feat-class-balance.md ("The held buffs and nerfs"), CHANGELOG v20. Class creator (its own
  repo): data/library.json revision 113 (Stone Brawler). Every rules test passes built with g++ here.
- 2026-10-04 11:10 (lobby and items session) Casters' signatures and hexers' status curses, not built, not committed
  to git: 11 class files (Content/Data/Classes), 11 icons (Content/Data/Icons/abilities), Content/Data/CastStudio/
  AbilityLooks.json, protocol 19 (TMNet.h), Docs/design/feat-class-balance.md, CHANGELOG v20. Class creator (its own
  repo): data/caststudio.json revision 118 (eleven looks), data/library.json revision 112 (Flame Sorcerer). Every
  rules test and the Cast Studio test pass built with g++ here. The old staff and curse icons are left in place.
- 2026-10-04 08:40 (lobby and items session) The class lab restructured, not built, not committed to git:
  Tools/ClassLab/ClassLab.cpp (`--teams`, `--classes`, `--seed-base`, `--ban`, `--skill search`, `rating`; defaults
  unchanged), Tools/ClassLab/balance.py and Balance.bat (new), Docs/design/feat-class-balance.md ("The lab"). Rebuild
  the lab with Tools\ClassLab\Build.bat. No rules file changed. First balance pass (run here, on the files after
  the tuning pass): Saved/Balance/balance.md -- 4 confirmed weak, 16 strong, 3 unclear; nothing changed from it yet.
- 2026-10-04 08:00 (lobby and items session) Class tuning pass, not built, not committed to git: 35 class files
  (Content/Data/Classes, each with `creator.rebalanced`), protocol 18 (TMNet.h), Docs/design/feat-class-balance.md
  ("The 2026-10-03 tuning pass"), CHANGELOG v20. Class creator (its own repo): data/library.json revision 111 (ten
  of these classes). Every rules test passes built with g++ here; no baseline moved. Measured with the class lab
  built from today's source, 200 games a class, old and new.
- 2026-10-03 23:59 (lobby and items session) Movement skills, stealth and statuses, not built, not committed to git
  (Docs/design/feat-movement-skills.md): rules SimAbility.h (`SelfStatusId`), SimBattle.h (`LandingFor`),
  SimClassFile.cpp (`self_status`, specials `leap`/`behind`), SimResolve.cpp, SimWorld.cpp; Tests/SimClassTest.cpp
  ("movement skills"); every rules test passes built with g++ here, no baseline moved. 15 class files rebalanced
  (Content/Data/Classes). The view: TMBattleDirectorMotion.cpp (`Flight`), TMBattleDirectorBlows.cpp,
  TMBattleDirector.h. Protocol 17 (TMNet.h). Class creator (its own repo): app/tmclass.mjs, FORMAT.md,
  data/library.json (Berserker, Tide Cleric).
- 2026-10-03 23:00 (lobby and items session) Camera rules (A + B + C + D of "Camera Rules Mockups"), not built, not
  committed to git: TMBattleDirector.{h,cpp}, TMBattleDirectorFeel.cpp, TMBattleHudFeel.cpp,
  TMBattleDirectorPlans.cpp, TMBattleDirectorBlows.cpp, TMSettings.{h,cpp} (`camera_follow`, `lead_camera`,
  `camera_held_note`), TMBattleHudOptions.cpp, TMBattleHud.h (`ReadyGo`). Docs/design/feat-combat-feel.md.
- 2026-10-03 22:00 (lobby and items session) Facing on arrival, not built, not committed to git: a rule.
  `FOrder::Face` (SimOrder.h, `FacingWay`), `ApplyMove(..., Face)` (SimMovement.cpp, SimBattle.h), checked in
  SimWorld.cpp, order text `f <way>` (SimOrderText.cpp), tests in SimOrderTextTest.cpp and SimMoveTest.cpp (both pass
  built with g++ here; no baseline moved), protocol 16 (TMNet.h). The game: walks are pressed and given on release,
  dragged for facing (TMBattleDirector.{h,cpp}, TMBattleDirectorFeel.cpp, TMBattleHudFeel.cpp,
  TMBattleDirectorPlans.cpp). Undo before acting dropped at the human's word. Camera rules mocked up ("Camera Rules
  Mockups" canvas), waiting on the human's pick.
- 2026-10-03 21:00 (lobby and items session) Combat feel, not built, not committed to git (CHANGELOG v19;
  `Docs/design/feat-combat-feel.md`): new `TMBattleDirectorFeel.cpp` and `TMBattleHudFeel.cpp`; Quick Cast per
  ability key (`quick_cast_1..4` in settings.json), click rings and the `order` tick (sounds.json), context cursor,
  online input kept while waiting and walk prediction (`bHostRefused` set by the reject message), fast-forward (new
  action `fast_forward`, X; the clock's share from `UpdateDilation`, which now owns the world's time dilation:
  `HitStop`, `SlowWorld`), hit-stop/slow beats/camera kick/death weight, ultimate close-ups, zoom to cursor, edge pan,
  camera lead, smoothed and eased walks, coloured paths, the destination ghost, the portrait's turn clock, auto end
  turn. `OrderSelected` now returns whether the order was taken. Not done: facing on arrival and undo (rules,
  protocol 16), the frame-time pass (the long frames are the editor building Paragon meshes on first use).
- 2026-10-03 12:30 (lobby and items session) Not built, not committed to git: `Deny()` plays sounds.json events
  noMove (Wood_14-8) / noAction (Sci-Fi_Interface_8-1) on refused moves and actions (TMBattleDirector.{h,cpp});
  StatusIconScale default 1.875 (saved as `status_icon_scale_v2`; an old saved value is read x1.25, TMSettings);
  TurnPips 25% bigger and stronger flashing borders (TMBattleHud.cpp, TMBattleHudPanels.cpp).

- 2026-10-03 11:30 (lobby and items session) Queue polish, not built, not committed to git: short Go To walks hand the
  unit over first; out-of-range abilities become Go Tos (`SetAbilityGoTo`/`ApproachRoute`/`StepAbilityGoTo`); a
  queued unit that sees a new enemy has its Go To/plan cancelled (`CancelQueuesOnSight`, called before RunDuePlans);
  ground fills at 30% opacity. Files: TMBattleDirector.{h,cpp}, TMBattleDirectorPlans.cpp,
  TMBattleDirectorIndicators.cpp, TMBattleHud.cpp, Docs/design/feat-move-queue.md. This machine only: no rule or
  protocol change. Then: Go To roads show only while their unit is hovered (`GoToShown`; Indicators, HudPanels).

- 2026-10-03 10:30 (lobby and items session) Class creator only: the Effects and sound panel is now a sequencer
  (new `app/sequencer.mjs`, `tests/sequencer.test.mjs`): every effect, light, shake, sound and animation clip as a
  card on tracks under a ruler in seconds, a library to drag from, move/trim/mute/solo, ▶ per card. Cards still save
  as moment + delay (or swing part + share), so the game's files are unchanged in format.

- 2026-10-03 09:40 (lobby and items session) Class creator only: Copy & presets card in the right pane of the
  Effects tab (copy/paste animation, effects and sounds between abilities, undo, named presets) and favourite
  effects and sounds (☆ in both browsers). New `app/castkit.mjs`, `tests/castkit.test.mjs`; the Cast Studio ledger
  keeps `favorites` and `presets` (normaliseLedger). Restart the creator to use it.

- 2026-10-03 09:01 (lobby and items session) `TMSoundStudio.cpp` now finds a cue's wave through the asset registry
  when its wave players have nothing loaded (Paragon's voice cues): 20,503 of 20,827 sounds playable in the creator.
  The 324 left are Countess and Riktor `*_Dialogue_Cue`s whose DialogueWaves fail to load in the pack itself.

- 2026-10-03 08:19 (lobby and items session) Sound effects built (`scripts\build.bat` passes after a C4456 fix in
  `TMSoundStudio.cpp`) and `Tools\SoundCatalog.bat` run: 20,827 sounds listed from 47 packs, 14,979 with a .wav in
  `Saved/SoundCatalog` (the rest hold no editor audio, e.g. dialogue-wave cues). Class creator restarted.
  Script: `E:\Builds\agent-sound.bat`. Still not committed to git.

- 2026-10-03 (lobby and items session) Cast Studio sound effects, not built, not committed to git (CHANGELOG v19).
  TMCast: sound events (`"effect": "sound"`, `sound`, `volume`, `pitch`) and swing timing (`part` windup/release/
  recover + `share`, `SyncSeconds`). Director: `CastRaise` waits by the swing's parts, `CastSpawn` plays sounds,
  `CastSwing` measures the parts; `SoundBlowStarts/Lands` skip the default sound for an ability with its own; looks'
  sounds preloaded. New `TMSoundStudio.h/.cpp` (`-tmsoundcatalog`, branch in `TMBattleDirector::BeginPlay`) and
  `Tools\SoundCatalog.bat` write `Saved/SoundCatalog` (.wav + catalog.json; editor binary only). CastTest has
  `SoundsInTimeWithTheSwing`. Creator: `app/sound.mjs`, the Sound effects section, `/api/sound`, `/sound/*.wav`.
  Earlier today, creator data only: `data/library.json` revisions 102 and 103 gave 32 abilities an effect and
  changed 44 whose effect did not fit their name (backups in `data/backups/library-before-*.json`).

- 2026-10-02 23:45 (lobby and items session) v18 release asked for ("build new release v18"). Scripts:
  `E:\Builds\agent-v18.bat` (build, every test, the class lab, then `agent-release-build.bat` with OUTDIR
  `E:\Builds\TacticalMasters-2026-10-02-v18`; status in `E:\Builds\agent-v18-status.txt`). CHANGELOG's v18 is dated,
  protocol 15. Please don't build or cook while it runs.
- 2026-10-03 06:50 (lobby and items session) Cast Studio phase 2, the effects timeline, not built, not committed to
  git. TMCast: `CastLooks.h/.cpp` reads `Content/Data/CastStudio/AbilityLooks.json` (events by moment, strip,
  footprints, the statuses and reactions tables) and does the sizes and anchors; `CastLegacy.h/.cpp` holds today's
  look (the effect table and flavour reading moved out of `TMBattleDirectorAbilityFx.cpp`, unchanged) and writes it
  out as events. New `TMBattleDirectorCast.cpp` plays the events (hooks in Blows, Motion, Loading and the tick);
  strip legacy/all skips the class file's effect / today's look. ClassLab `looks <class file>` (Build.bat now
  compiles TMCast's two files). RunTests' cast block links every Sim*.cpp and TMCast. Creator: `castlooks.mjs`,
  `castpreview.mjs`, the Effects timeline panel, `/api/cast/legacy`, Publish writes both files. The effect sketch
  shows the class's body (Select model when it has none).
- 2026-10-03 01:30 (lobby and items session) Godot dependencies removed, not built, not committed to git. The
  rules tests' tables are the port's own baselines now (`Tests/Baselines/*.txt`, the old `Tests/Godot*.txt`
  renamed; the human deletes the old ones), and `scripts\test.bat --rebaseline` records them again from the
  rules after a deliberate change (`Tests/Baseline.h`; whole battles are replayed from their first line). Checked
  here: unchanged rules rebaseline to identical files, and corrupted tables are restored line for line.
  SimClassTest no longer compares classes with Godot's reading. ClassLab `--rules godot` is `--rules classic`.
  Creator: the Astra upgrade path and the Godot-project test are gone (delete `legacy/`, `tools/convert-astra.mjs`,
  `tests/convert.test.mjs`). CLAUDE.md: the project is its own source of truth.
- 2026-10-03 00:40 (lobby and items session) Cast Studio phase 1, not built, not committed to git: new engine-free
  module `Source/TMCast` (reads `Content/Data/CastStudio/AbilityAnimation.json`; in the .uproject and both targets),
  the director plays the picks (`TMBattleDirectorMotion.cpp`: wind-up/release/loop/recover chain, `TMBattleDirectorBlows.cpp`:
  picked contact), `TMAnimStudio` films v2 catalogues (32 fps grid strips, tracks, libraries, `-tmanimwanted`),
  `Tools\AnimClips.bat`, `Tests\CastTest.cpp` in RunTests. Creator: Cast Studio tab (`app/caststudio.mjs`).
  The plan now has Unreal closed while authoring (`Docs/CastStudio-Plan.md`), progress in `Docs/CastStudio-Resume.md`.
  Needs: `scripts\build.bat` (new module: UBT regenerates), `scripts\test.bat`, then `Tools\AnimCatalog.bat` once.
- 2026-10-02 23:50 (lobby and items session) Cast Studio: wrote `Docs/CastStudio-Plan.md` from the blueprint
  (`Docs/CastStudio-Blueprint.md`, copied in) -- per-ability look and animation timelines, built into the class
  creator, played by a new engine-free TMCast module and the director, previewed on a real stage battle
  (`-tmcaststage`). Plan only, no code; waiting on the human's approval before phase 1.
- 2026-10-02 21:30 (lobby and items session) Not built, not committed: the balance audit's changes ("Class Balance
  Audit" doc) and pets. Rules: FAbility PetJob/PetTurns ("special": "pet", "pet": {job, turns} in class files),
  FUnit PetOf/PetTurns (in the checksum), FBattle::PlacePets (after PlaceCamps, so monster ids are unchanged),
  CallPet (ResolveAbility, after the blow), SendPetAway (BecomeReady, when its turns run out); KnockOut/OnGone/
  CheckWinner/health share treat pets like monsters (gone at once, never a win); the computer doesn't loot with
  pets. Data: 29 class files rebalanced (all marked rebalanced + changedAt), 11 pet monster files
  (Content/Data/Monsters/*_pet). Tests: SimCampTest plays battles with three summoners (pets called, played, gone,
  replayed); SimTraceTest fights the Godot trace with Godot's own classes, read from GodotClassTable.txt (no class files needed; 21:15). Every sim test
  passes with g++. Game: ComputerPlaysUnit (pets are the computer's; online the host's), NameOf, the log line. Class
  lab: --monsters, or the Monsters folder beside --maps. Creator: tmclass.mjs knows "special" and "pet", FORMAT.md,
  library.json (rime_warden, frost_hexer, flame_sorcerer, thorn_summoner brought to the game's copies, revision 60).
- 2026-10-02 20:20 (lobby and items session) Not built, not committed: "Zone of Control Mockups" A, B, D. TMSim:
  FBattle::HoldsTheLine, ZoneShadow, PathIgnoringZones, StrikeReach, TankLanes (+ FLane); SimMoveTest's new zones block
  (5 checks). Game: ATMBattleDirector ZoneShadow/ZoneStop*/ZoneGhost/TankLanes (RefreshZoneShadow wherever Reachable is
  set; UpdateHoverPath works out the stop and the lanes), PaintZones + PaintZoneShadow + ZoneSignature
  (TMBattleDirectorIndicators.cpp: dashed rings for seen tanks, enemy reach rings and hatching while walking, a walked
  tank's ring at the path end); ATMBattleHud::DrawZoneWords (labels, stop mark, lanes, "cuts N of M"). Ground painting
  needs the indicator decal; without it only the words show. Every sim test passes with g++.
- 2026-10-02 19:55 (lobby and items session) Not built, not committed: watchtowers keep 6 m from the map's edge
  (Watchtower::FromEdge, PlaceWatchtowers; SimWatchtowerTest checks it: 627 of 3240 test battles now have room for
  fewer towers than asked, 448 before); defaults watchtower_turns 2 -> 1, watchtower_sight 14 -> 28 (slider max 30 ->
  60); SimWatchtowerTest pins the capture checks to 2 turns and checks the new defaults. Every sim test passes with g++.
  Also fixed the C2662 build error in DrawThreats (const FBattle::FindUnit).
- 2026-10-02 19:50 (lobby and items session) Not built, not committed: "Open Odds Mockups" A and C. TMSim: FOdds,
  FThreat, FBattle::OddsOf (hit/crit/graze/dodge in the order ResolveAbility rolls them, shields counted for KO) and
  FBattle::ThreatOn (best damaging blow at the attacker's next turn, from where it stands or after a walk); SimCalcTest
  checks OddsOf on all 12124 table rows (sum 100, split as rolled, KO = sum of fatal outcomes); passes with g++ here.
  Game: ATMBattleHud::OddsCard (the aim forecast's card for up to 3 units, the rest keep the line, now with KO %) and
  DrawThreats (an enemy pointed at: lines and odds to each of your units, the "threat_card" panel top right; planning a
  walk: who reaches its end).
- 2026-10-02 19:15 (lobby and items session) Not built, not committed: the squad strip ("Squad Strip Mockups" C, with
  B on the turn squares). ATMBattleHud::DrawSquadStrip (TMBattleHudPanels.cpp; drawn after the log; movable and sized as
  "squad"; the field list now sits under it via SquadBottom); option Squad strip (FTMSettings::bSquadStrip, saved as
  squad_strip, ETMHudAction::OptionSquadStrip). Turn squares: health bar coloured by HealthColour, up to 3 status icons
  under each (StatusChips gained Most, the last "+N"), statuses in the tooltip; SquareLane moves the log, field list and
  coming-turns drop-down down to make room. StatusChips: a red turns tab when a status ends next turn.
- 2026-10-02 18:30 (lobby and items session) Not built, not committed: "Camps and Bosses Mockups" A-D. Rules (TMSim):
  camp noise (FCamp Noise/NoiseQuiet/NoiseBy/LoudUnit/LastLoudSide/bNoiseWake, FBattle::MakeNoise, Camp::Noise*), clean
  kills (Camp::CleanKillTgPercent, EEventKind::CleanKill), a stagger breaks a boss's cast (MonsterHurt), the hunt
  (FTuning::BossHunt rule 41, FUnit Wrath/HuntTarget/HuntLost, UpdateHunt, "hunted" status, TargetWorth x3) and the claim
  (FTuning::BossClaim rule 42, FUnit Claim, ClaimBoss, "boon" status); events CampNoise, Hunting, BossClaimed, ClaimShare,
  CleanKill; all in the checksum. Tests: SimCampTest's new block, hunt and claim on in two of three whole battles;
  SimPlayTest expects 42 rule numbers. Every sim test passes with g++ here. Game: setup rows Bosses hunt / Claim the boss
  (saved, sent online, protocol 15), the boss bar (DrawBossBar), wind-up painting (PaintCasts), ghost chips/squares,
  wind-up seconds and marks, the hunt's line, camp label noise, log lines; icons hunted.png and boon.png.
- 2026-10-02 17:45 (lobby and items session) Not built, not committed: cooldowns on the turn order ("Cooldown Ghost
  Chip Mockups": C for the squares, B for the bars) and chip/square <-> unit hover linking. ATMBattleHud::ComingBack,
  TurnInSeconds, TurnLinked, DrawComingTurns (TMBattleHudPanels.cpp); PinLane in TMBattleHudStyle.h (the bars are
  taller by two lanes, the log and field list moved down with them); ATMBattleDirector::HudHoverUnitId, read by
  UpdateMarks. A square's tooltip is now on its health/time strip; the portrait shows the coming turns.
- 2026-10-02 16:15 (lobby and items session) Not built, not committed: v17 crashed twice (D3D12, BasePass,
  RHISetShaderParameters -> ValidateStaticUniformBuffer reading 0xffffffffffffffff: a freed uniform buffer; symbolised
  from the minidumps with the build's PDB). Breadcrumbs had MID_M_ArrowString3 / MID_M_Sparrow_Torso_Arms in flight,
  dynamic materials the animation's material curves make. Unit bodies now SetAllowAnimCurveEvaluation(false) and
  WearBody empties override materials before a new mesh (TMBattleDirector.cpp, TMBattleDirectorLoading.cpp).
- 2026-10-02 15:55 (lobby and items session) Not built, not committed: tooltips wrap at 440 px (scaled) with
  ATMBattleHud::Wrap and are clamped on screen (DrawTooltip, TMBattleHudPanels.cpp); the human's screenshot of the
  setup screen's map description running off the left edge.
- 2026-10-02 15:35 (lobby and items session) `7560c4a` v17 committed at the human's word ("Commit to git"), by
  Saved/agent-v17.bat; class creator `3268f3b` (Wounded in its vocabulary). The creator's `server.mjs` is not mine, left.
- 2026-10-02 15:15 (lobby and items session) Built and tested clean at 15:11 (every test passed). Packaging v17 at the
  human's word ("Start v17 release"): `E:\Builds\TacticalMasters-2026-10-02-v17` (protocol 14) by
  `E:\Builds\agent-release-build.bat`; "Play with crash details.bat" copied in from v16. Packaged 15:31: BUILD READY,
  `E:\Builds\TacticalMasters-2026-10-02-v17\Windows\TacticalMasters.exe`. Not committed (not asked).
- 2026-10-02 14:55 (lobby and items session) Not built, not committed: "Turn Left Indicator Mockups" A + D, the
  human's pick with flashing borders: ATMBattleHud::TurnPips (round joined tokens in front of the ring, for our own
  ready units with half a turn or more left; statuses stay beside the ring as rounded squares); the action bar's
  Move tile and usable ability tiles flash (a pulsing edge) once the other half is spent; spent halves read "moved"
  / "acted" (WordTile's under-line, AbilityTile's effect band).
- 2026-10-02 14:40 (lobby and items session) Not built, not committed: a tower just taken catches over
  ATMBattleDirector::TowerKindleSeconds (2 s): FTMTower::KindledAt/FlameTime, LightTower (colour, light and a flare by
  the catch), AdvanceTowers' flames eased from embers; AdvanceFog spreads the tower's sight with it in 16 steps (the
  rest of the side's sight cached in FogSteadySeen, so only the spread is worked out again), and IsSeen/IsPointSeen
  agree. Presentation only: the rules still see the whole circle at once (no protocol change).
- 2026-10-02 14:25 (lobby and items session) Not built, not committed: the action bar, "Action Bar Mockups" B (the
  human's pick): WordTile (TMBattleHud.cpp) draws a stroke icon beside each word (EGlyph: Move, Sprint, Items,
  Capture, End), small tiles 64 -> 74 px; AbilityTile (TMBattleHudPanels.cpp) shows a cooling ability on the bar as
  an hourglass badge with the turns left and a step bar per cooldown turn (the enemy panel keeps the large number).
- 2026-10-02 14:10 (lobby and items session) Not built, not committed: the Wounded status ("wounded", WND):
  FStatusDef::HealTakenPercent (new, last field; Wounded -50), FUnit::HealReceived applied to heal abilities
  (CalcAmount), Regen, springs, undamaged mending and lifesteal; the HUD's look and words (TMBattleHudStyle.h), its
  combat-text colour (Blows), its icon (Tools/StatusIcons, Content/Data/Icons/statuses/wounded.png), a test in
  SimStatusTest (passes here, with every other status test), and the class creator's vocabulary. No ability or item
  uses it yet. Go To stops drawn as rings on the ground (DrawGoToMarks, "Go To Marker Options" B). Mockups: "Action
  Bar Mockups" (icons on Move/Sprint/Items/Capture/End, two cooldown displays).
- 2026-10-02 13:45 (lobby and items session) v16 crashed again on DirectX 11 (-dx11): the same access violation in
  SetShaderParameters, base pass, breadcrumbs "SceneRender - BattleDirector" (likely one of the director's scene
  captures: the turn-card portraits, every frame). A Narbash body (piper) was on the field both times, but a 24-minute
  -dx11 match with two Narbash bodies ended cleanly, so that is not proven. Added "Play with crash details.bat" to the v16
  folder (-ExecCmds r.EmitMeshDrawEvents 1, r.ShowMaterialDrawEvents 1) so the next crash log names the mesh and
  material being drawn.
- 2026-10-02 13:20 (lobby and items session) Not built, not committed: the battle report shows each unit's items
  (ATMBattleHud::ReportGear in TMBattleHudReport.cpp, declared in TMBattleHud.h: an Items column in the tables, beside
  the name on a unit's page, "Carried" on the MVP card); the MVP summary is two short lines (it ran out of the card);
  moments name the class and side, not the unit id. Chest mockups ("Treasure Chest Mockups"): no beam through fog,
  the human's call.
- 2026-10-02 12:40 (lobby and items session) Committed `8180d91` (this session's work since `3ab5f9e`: Source, Tests,
  Docs, Content/Data, Config, Tools; never the Fab packs) and the class creator `fcb5908`. Release
  `E:\Builds\TacticalMasters-2026-10-02-v16` (protocol 14), by Saved/agent-v16.bat. v16 then crashed in play: D3D12
  SetShaderParameters in the base pass, the first tested battle with Narbash bodies (war_drummer, dirge_singer). The v14
  guard (TMDrawable) had never caught a material in v15 or v16: at run time the four cook-broken Fab materials still
  report a shader map. TMDrawable.cpp now refuses them by name too (Narbash's legs drawn with the default surface).
  Not built, not committed, no release: waits on build-and-test and the human's OK. Meanwhile "Play on DirectX 11.bat".
- 2026-10-02 10:50 (lobby and items session) Class creator (E:\TacticsClassCreator): a "Game roster" page
  (app/roster.mjs, sidebar button, #roster): all 101 game classes as cards grouped by role, a sortable table of
  every stat (tinted above/below the role's usual), or a map of budget against win rate; filters for role, rule
  breaks, over/under budget and the 40-60% band; a side panel with stats against the role's usual and "Bring in and
  open" (imports into the library if needed). tests/roster.test.mjs. Mockups: "Class Roster Mockups".
  Earlier, 10:13: build-and-test passed (replays and battle report compile); not committed.
- 2026-10-02 09:50 (lobby and items session) Not committed. End-of-battle stats screen built
  (Docs/design/feat-battle-report.md): new TMBattleDirectorReport.cpp (per-unit tallies, MVP score: damage .1, taken
  .04, mitigated .03, healing .1, kill 12, assist 5 within 60 s, death -10, monster 4, boss 15, buff/debuff 2,
  control 3, revive 10, tower 8) and TMBattleHudReport.cpp. First build-and-test of the replay and report code
  failed with 9 errors (std::vector .Num(); helpers named Initials and Role clashing with TMHudStyle::Initials and
  AActor::Role); fixed (ReportInitials, RoleWord, .size()), not yet rebuilt. Class creator (E:\TacticsClassCreator):
  class rules flagged, not refused (deals damage; a tank protects; a support helps allies) and a stat budget
  (app/rules.mjs, app/budget.mjs). The 7 game tanks that break "a tank protects" (blazeblade, crusader,
  earthshaker, frostblade, hexblade, thunder_spellblade, windblade) and the Spellblade archetype stay flagged; the
  human will design their kits.
- 2026-10-02 09:10 (lobby and items session) Not committed; built by the build-and-test script only: replays
  (Docs/design/feat-replays.md). New TMBattleDirectorReplay.cpp and TMBattleHudReplay.cpp; hooks in
  TMBattleDirector.cpp (BuildBattle records or applies the replay's start before Battle.Start; Submit records every
  applied order and refuses all but the replay's own while watching; Tick plays the replay; ViewerTeam; keys; menu),
  TMBattleDirector.h, TMBattleHud.cpp/h (Replays on the title, Watch replay at the end, the replay bar).
  Saved/agent-replay-check.bat plays a battle and checks its replay with -tmreplaycheck. The end-of-battle
  stats screen waits on the human's answer to "Battle Report Mockups".
- 2026-10-02 08:55 (lobby and items session) Not built, not committed: 15 item files retuned for defense model 1
  (Docs/design/feat-items.md section 13): defensive numbers rescaled so each item takes off about the same share
  of its holder's damage as under the old rules (modelled over every class against every damaging ability).
  Tests/SimItemTest.cpp expects Armor +3 +6 for Chain Vest + Bulwark Plate. Item, camp and status suites pass
  locally. Next in this session: end-of-game stats/MVP mockup, then a replay system.
- 2026-10-02 09:05 (lobby and items session) Not built, not committed: 28 ability descriptions in 24 class files
  (Content/Data/Classes) rewritten to say what the abilities do, numbers untouched: "Slumber"/Sleep say Stun,
  Power -> Crit, seconds -> turns. Each class carries `creator.reworded`; `Tests/SimClassTest.cpp` skips only the
  description comparison with Godot for those (numbers still compared). Local run: 77 compared, every field agrees.
  The creator's library copies of these classes will show "Game's copy differs" until taken.
- 2026-10-02 08:40 (lobby and items session) Class creator: ability descriptions written from the numbers
  (app/describe.mjs). Abilities tab, Description: "From the numbers" keeps a flavour line (creator.flavour[slot])
  and adds what the ability does in the game's terms, rewritten on every change (distances in the flavour follow
  the ranges); "By hand" is checked instead, and disagreements show in the authoring checks. New classes start
  written from their numbers. The check finds 28 of the game's 404 ability descriptions wrong in 24 classes: eight
  "Slumber"/Sleep abilities say sleep and give Stun, ten still name Power, eleven give seconds for turns.
- 2026-10-02 08:05 (lobby and items session) Class creator: a live scorecard heads the right pane. 1.2 s after a
  change the game would see, it plays 20 games (class lab, game rules, Highlands) and shows the win %, the 40-60%
  band, the change since the last run (same seeds, so like for like), rank in the last balance pass, margin, as-four,
  and how often each ability was used ("never" in amber). "+20 games" plays new games on top; Live/Off kept in the
  browser. (In a test library, the old Frost Hexer's Curse showed as never used.)
- 2026-10-02 07:40 (lobby and items session) Class creator (E:\TacticsClassCreator, its own folder): the
  Stats tab uses the game's names (Armor, Resist, Evasion A/M with "AttDef in the file") and marks the lower
  evasion number as unused; a new "In the game" panel shows what lands through Armor/Resist, the one Evasion and
  the damage to knock the class out (app/defense.mjs). The New class dialog shows the class lab's result for each
  archetype (the game's classes made from it, game rules) in place of the old Godot margins. A badge on each class
  says whether the game's copy matches (Same / Changed since install / Game's copy differs), opens a diff with
  Install or "Take the game's copy", and an "Out of step" library filter (app/sync.mjs). Tests: tests/sync.test.mjs.
- 2026-10-01 23:55 (lobby and items session) Not committed, built by the build-and-test script only (no release):
  the human chose option A and zone of control from "Class Rebalance Mockups". `FTuning::ZoneOfControl` (40th rule
  number; the game sets 1 in `TMSim::GameTuning`): within EngageRadius of an enemy whose first role is tank a walk ends
  (SimMovement RunDijkstra; a tank the walk starts beside doesn't hold it; a waypoint in a zone is refused).
  `TMSim::ApplyGameBalance` (SimAbility) changes the built-in Knight and Archer for the game only, so the Godot tests
  hold; the director and the class lab (game rules) call it. The computer no longer aims an area between two targets
  at fogged ground (SimAI). Class files: Berserker, Samurai (descriptions), Frost Hexer, Frost Witch (split), each
  marked `creator.rebalanced`, which SimClassTest's Godot comparison now skips. The setup screen's choices are kept
  between sessions (`FTMSettings::LastSetup`); Dev Tools no longer shows the five rules the setup screen owns.
  `Docs/CHANGELOG.md` is new: player-facing, newest first; the release script copies it next to the exe. Keep it
  current with every change a player would notice. Protocol 14. Docs/design/feat-class-balance.md.

- 2026-10-01 22:00 (lobby and items session) Release `E:\Builds\TacticalMasters-2026-10-01-v15` (protocol 13): combat
  text, the fight health bar, Go To, the portrait-capture crash (deferred captures). Then the class lab measures on the
  game's rules: `TMSim::GameTuning` (SimTypes) is now the one place the game's rules are set (the director calls it);
  `Tools/ClassLab` fights on them at hard, 40 games, the class standing in for the reference member with its first role,
  plus four of it ("stack"), `--maps` round every map, and a `tournament` command for every class. Its Build.bat now
  compiles every Sim*.cpp (the list had fallen behind, so the lab last built 2026-09-29). The class creator
  (`E:\TacticsClassCreator`, its own repository): 40-game playtests, an "Every map" switch, the stack figure, a rank, and
  "Among every class" (the tournament, kept in `Saved/ClassLab/balance.json`); the stale "auras do nothing" warning
  removed. Not committed.

- 2026-10-01 20:45 (lobby and items session) Go To (click beyond the walk area: walks there over several turns,
  walk-and-end by default, stops when an enemy comes into sight, when hurt or blocked; G keeps going, Backspace cancels;
  numbers on the ground for each turn's end, a ">N" corner on the turn square, a strip with its buttons) and the combat
  text redesign ("Combat Text Mockups": red damage, big bold crits with "!", dark red ember burn, crimson bleed, green
  heals, statuses in their own colours; own units' health bar for 3 s on dealing or taking damage, the lost part
  flashing as it drains over 2 s). Built with the build-and-test script; no release packaged (the human's rule: no
  release without their OK). Not committed.
- 2026-10-01 20:01 (lobby and items session) Release `E:\Builds\TacticalMasters-2026-10-01-v14` (protocol 13): the play-test
  crash. Four Fab materials are cooked with no shaders (textures missing from the packs; package.log "Shadermap pointer is
  null"): ParagonMorigesh bugs (M_Bug_Mesh, M_Bug_ParticleSubUV_Trans), Narbash's M_Narbash_Legs_Drumsticks (on the Narbash
  body), Rampage's M_Rock_To_Throw. Drawing one crashed the render thread (SetShaderParameters). New TMDrawable.{h,cpp}: in a
  packaged game a body slot with such a material gets the default surface, and a Cascade effect holding one isn't played.
  Fixing the materials themselves is editor work (human-only). Also the new defense rules (entry below).
- 2026-10-01 19:40 (lobby and items session) Not committed: new defense rules (Docs/design/feat-defense.md). Armor
  (AttDef) and Resist (MagDef) take a share, 30 / (30 + it), instead of being subtracted; one Evasion (the higher of
  A-Eva and M-Eva, plus bonuses to either); an evaded hit is dodged 1 in 10, grazed for half otherwise. Behind
  `FTuning::DefenseModel` (0, Godot's, stays the rules' default so the Godot comparisons hold; the game sets 1 in
  `GameTuning`). Protocol 13. Also: the setup screen's Start no longer covers its last rule; "Not ported yet" gone.
- 2026-10-01 17:30 (lobby and items session) Not committed: queued orders (Docs/design/feat-move-queue.md). Plan a
  waiting unit's next turn (click it): a walk and an ability aimed from where it ends, run when its turn begins; plan a
  turn and go in one (G); waypoints on any walk (Ctrl+click, up to 4; the move order carries them, protocol 12).
  New TMBattleDirectorPlans.cpp; sim tests pass here (SimMoveTest has waypoint checks).
- 2026-10-01 16:10 (lobby and items session) Built and tested, not committed: crash fixes from the play tests. Picking
  Berserker crashed every time (engine morph buffers, `FMorphVertexBufferPool::GetReadingIndex` "Index == 1", on Grux's
  mesh): morph targets are now off on every unit and guide body, and cloth too (WearBody). Also render targets are made
  new rather than resized (FilmOfSize), the Codex guide hero swaps at most every 0.25 s, fullscreen is debounced.
  Earlier today: the combat log redesign, a scrolling options menu, camp respawns (setup, off by default), auto-recenter
  (option, V). Protocol 11. Release `E:\Builds\TacticalMasters-2026-10-01-v11` (has the Berserker crash); v12 next.
- 2026-10-01 10:30 (lobby and items session) Built and tested, not committed: the online lobby for up to four
  (sides chosen, units shared out by order of joining, the computer on an empty side) and a LoL-style draft with bans
  (Docs/design/feat-lobby.md; protocol 8); friendly fire as a setup option (9); the team stash: items picked up go
  to the side, equipped from the team items screen, taking one off is a turn, the fallen's go back (10;
  Docs/design/feat-team-items.md); thin aiming lines for every shape, a dimmer walk area, range on ability
  buttons, Tab shows/hides status bars (rebindable; "next unit" moved to N). Free-for-all (3-4 sides) is not started.
- 2026-10-01 01:34 (watchtower session) LAN play-test build of `3ab5f9e` packaged:
  `E:\Builds\TacticalMasters-2026-09-30\Windows\TacticalMasters.exe` (online protocol 7; the 2026-09-29 build is kept in
  `E:\Builds\TacticalMasters`). `Tools/cook_list.py` now also cooks the `/Game/...` paths named in the C++; it and the
  regenerated `Config/DefaultGame.ini` are changed in this folder, not committed. `TM_Release`'s `Source` and `Config`
  were mirrored from here (its git HEAD still reads `a75a859`). Script `E:\Builds\agent-release-build.bat`. Block removed.
- 2026-09-30 19:45 (watchtower session) `3114b33`: everything this session had claimed since 2026-09-29, committed at
  the human's ask after a clean build and every test passing (19:12): watchtowers, items, neutral camps and bosses, the
  second set of statuses, four 64 x 64 maps, lighting and foliage, ability looks, fog of war over everything, the red
  hover outline, cliffs and ruins, walking onto items picks them up, item icons, outlined world text, the Runic Coffer
  chest and the Signal Beacon tower. Later the same day, in the same commit: lag fixes (looks and sounds preloaded, frame
  timers in the log), no sight lines, the comet trail round a toggle that is on, ground indicators and tower/centre rings
  painted over the land, taller towers that see their whole radius, blue hover for allies, the camera following to the
  next unit, the ten hazard looks (TMBattleDirectorHazards.cpp), and the Codex (TMBattleHudCodex.cpp). Online protocol 7.
  Block removed. Not committed, nobody's claim: the Fab content
  packs under `Content/`, `Content/{SampleMap,Lighting,ErodingCircle}`, `SnippingSS/`, `scripts/`, `Tools/MapAnalyzer`
  (but its four new maps), `Docs/{AgentTeam,AssetShortlist}.md`, `Docs/{qa,tech}/`, `Docs/design/_TEMPLATE.md`.
  The class creator's changes went into its own repository.
- 2026-09-29 20:48 (watchtower session, resuming packaging) Packaged build done: `E:\Builds\TacticalMasters\Windows\TacticalMasters.exe`,
  from `TM_Release` at `a75a859` (no watchtowers). The cook finished 11110/11110 but UAT failed on 1025 `LogAssetManager`
  "PrimaryAssetId TMDataNamed:... does not match object's real id" errors; an incremental rerun with `-ignorecookerrors`
  (`E:\Builds\agent-finish-package.bat`, log `E:\Builds\package.log`) finished it. Online guide copied next to the exe.
  Packaging session: those errors will fail the next normal cook too. (This entry was lost once when the file was rewritten.)

- 2026-09-29 (packaging) `a75a859` packaging config (startup map, cook list, data staged, cook memory cap);
  `08883a5`/`a177e4c` this work log and its rule in CLAUDE.md.
- 2026-09-29 (packaging) `d9622b5` three new maps committed (Caldera Crown, Frostwall Town, Riverwatch Fords).
- 2026-09-29 (packaging) `b61609b` background hero loading with a bar, map-button fix, dodges, turn cards,
  overhead gauges, damage number size, Terra's cloth off, every icon drawn by the class creator.
