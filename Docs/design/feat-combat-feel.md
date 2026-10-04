# Combat feel: controls that answer, blows that land

2026-10-03. Asked for: "Brainstorm ways to make the movement and feel of combat better. The game's controls and
fluidity must be clean and responsive", then "Start adding all suggestions except the hold to aim and release to cast
suggestion. Implement that as an option in the settings next to the keybinds of abilities. Call it Quick Cast."

None of this is a rule. The battle's clock is the rules' and runs on real time whatever the look of the world does,
with one exception (fast-forward, below). Nothing here sends anything new online.

Code: `TMBattleDirectorFeel.cpp` (the director's side), `TMBattleHudFeel.cpp` (what the HUD draws), and the places
named below.

## Input

- **Act on press.** Every key already acts as it goes down (it always has).
- **Quick Cast** (Options, Controls: a box next to each of Ability 1-4's keys, off by default). Held, the key aims the
  ability; let go, it is used where the pointer is, no click. Let go over a button and the aim is put down. An ability
  centred on the user goes off wherever the pointer is. While held, "let go to use" shows by the pointer. Keys are
  now bound on release too (`SetUpPlayerInput`); `OnKeyUp` -> `QuickRelease` -> `ClickAbility`, which is also what a
  click while aiming does.
- **A click answers.** Where a click on the board is taken, a ring spreads and fades on the ground (gold a walk or
  waypoint, orange a Go To or a walk into range, violet an ability, red refused) with a short tick (sounds.json
  event `order`, the menu click for now). `Acknowledge`, drawn in `DrawFeel`.
- **The pointer says what a click would do** (`UpdateCursor`): a hand over a button or one of your units, crosshairs
  aiming (where it can go, or be walked into range of), a barred circle where it can't, four arrows past the walk area
  (a Go To), a barred circle on open ground once the walk is spent; a closed hand right-dragging.
- **Online, nothing pressed is lost** while the last order is out with the host: an order key or a board click is
  kept (the newest), and given again when the answer comes, if within 1.5 s and the pointer hasn't moved 40 px
  (`BufferWhileWaiting`, `ReplayBuffered`).

## Moving

- **Corners cut.** A walk is node to node; it is walked with its corners cut (Chaikin, twice), and the path on the
  ground is drawn the same way. Never across a change of level, and it ends exactly where the rules put the unit
  (`SmoothedWalk`, TMBattleDirectorMotion.cpp).
- **Eased.** A walk gets into its stride over a fifth of a second and slows over its last half metre (not before a
  swing it walked to make).
- **Long walks go faster**: past 8 m, up to a third faster at 24 m, the steps quickened to match.
- **Ghost.** While a walk is aimed, an outline of the unit stands where it would end, in its side's colour, with an
  arrow the way it would face.
- **The path's colour says what it meets**: gold; orange where it sets off from inside an enemy's reach (breaking away
  costs extra); red where it ends inside a tank's zone (the zone stops it).
- **Online, the joiner's walk sets off at once**, before the host answers (`PredictWalk`, on screen only); if the host
  refuses it (or doesn't answer in 3 s) the unit walks back to where the rules have it (`SettlePredictions`).

## Blows

Hit flash and knockback were there; now they scale, and the world reacts (`React`, `ShowOne`, TMBattleDirectorBlows.cpp):

- Knockback by the share of health a hit took (12 cm a scratch, up to 32; 30-50 for a critical).
- A struck unit's light flares brighter for a bigger wound.
- **Hit-stop**: the world all but still (6% speed) for 0.05 s as a hit taking 15% or more lands, 0.09 s for a critical.
- **Slow beat** on a critical (40% speed, 0.3 s), on a unit going down (45%, 0.5 s, and the camera shakes), and on an
  ultimate (35%, 0.9 s, as before).
- **Camera kick** sized to the damage (a hit taking 6% or more, or any critical).
- **Sound on contact**: blows were already heard when they land, at the swing's contact (`LandBlow`).
- All of it only in a window someone is watching (not `-unattended`), and it costs nobody turn time: the clock and the
  computer's thinking both run on real time.

## Turn flow

- **Fast-forward**: hold X (rebindable), or turn on "Fast-forward the other side's turns" in Options. While none of
  this machine's units is ready, the world and the battle's clock run three times as fast; never while one of yours
  has a turn, never online (time is the host's), not while planning a waiting unit (the option; the key still works).
  A badge at the top says so. `WantsFastForward`, `UpdateDilation`.
- **End the turn by itself** (Options, off by default): a unit that has walked and has no ability it could use now
  ends its turn. Not beside a chest or a tower it could take, not for a plan's walk or a Go To. Online, the host only.
- **A clock round the portrait**: the selected unit's portrait frame drains, clockwise from the top, as its turn's time
  runs out; red and beating in the last five seconds.
- Auto-pause in menus: offline the menus already pause the battle.

## Camera

- **Zoom toward the pointer** (Options, on): the ground under the pointer stays under it as the wheel zooms.
- **Edge pan** (Options, on): resting the pointer within 8 px of the window's edge pans that way, easing up to the
  keys' speed over a third of a second; not over a HUD button there, not while dragging.
- **Lead**: a walk whose end is near the edge of the screen (or off it) brings the camera halfway along it.
- **Close-up on ultimates** (Options, on): the camera closes in on the caster and what it aims at for 1.4 s, then goes
  back. Any key or click ends it at once. A unit's turn coming up meanwhile changes where it goes back to.
- The camera now moves on real time, so it is as quick in a slow beat or a fast-forward as ever.

## Facing on arrival (2026-10-03, a rule)

Asked for: "Build the facing mechanics." Facing decides whether a blow lands on the front, side or back.

- A walk is now **pressed** where it ends and given when the button is **let go**. Dragged away from that spot first
  (18 px or more), the unit ends its walk facing the way of the drag, snapped to the nearest of eight; the ghost shows
  the eight ways and the one chosen, in gold ("drag to face" until it is dragged far enough). A plain click walks as
  before and faces its last step. Esc or a right click while pressed calls the walk off. It works for a plan's walk
  too (`FTMPlan::Face`). Not for waypoints (Ctrl+click), a Go To, or a walk into range (the ability turns it anyway).
  `FTMWalkPress`, `UpdateWalkPress`, `ReleaseWalk` (TMBattleDirectorFeel.cpp).
- Rules: `FOrder::Face` (-1, or 0-7 for `TMSim::FacingWay`: 0 along +X, 45 degrees a step toward +Y, written as float
  literals so every machine faces the same way). `ApplyMove` faces that way instead of the last step; Off-Balance
  still keeps it from turning. `Validate` refuses anything outside -1..7. The order text adds `f <way>` last, and only
  when one was chosen, so every older order and replay reads the same. Facing was already in the checksum. Online
  protocol 16. Tests: `SimOrderTextTest` (round trips, seven new malformed lines), `SimMoveTest` ("facing on arrival":
  each way, none, Off-Balance, out of range). No baseline moved.

Undo a move before acting: not wanted (2026-10-03).

## Camera rules (2026-10-03)

Asked for: "camera snapped into another character whose turn became ready while I was issuing orders to another
character. There need to be rules to prevent unwanted camera movement." The "Camera Rules Mockups" canvas; the human
chose the suggestion, A + B + C with D.

The one rule: **the camera moves by itself only when your hands are off the controls, and only because of your own
unit's turn ending** (or a queued unit of yours being handed back). Your own keys -- N, C, panning, the wheel -- always
work.

- **A, hands busy.** `MidOrder()`: aiming an ability, pressing a walk, planning, waypoints set, Quick Cast held,
  dragging the camera, a panel, a card or a slider, a menu, the guide, Options, the team items, Edit layout, edge
  panning. A follow waits through these (and in When I'm idle, 1.5 s after any key, click or camera move:
  `LastInputAt`), with a small "Camera held: ..." note on the left (Options); it is let go if it can't happen within
  4 s. An ultimate's close-up is skipped too, unless it is the one just ordered.
- **B, ask, don't take.** A unit of yours that becomes ready while you are busy with another (ordering or planning)
  never takes the selection: a note at the top says who and how long its turn has, with Go; off screen, an arrow at
  that edge points to it; N goes to it first. The same for a planned turn that starts, a Go To that hands a unit back,
  and one whose target is lost (`PointOut`, `ReadyToastId`). Before, planning was put by and the camera slid over.
- **C, only when it has to.** A follow is asked only when your own unit's turn ends and the next is taken up within
  1.5 s (`FollowArmedUntil`), or a queued unit is handed back; one ready later is taken up quietly (pointed out if it
  is off screen) -- before, the follow waited for ever and fired whenever. A unit already comfortably on screen (the
  middle 80%) gets no camera move.
- **D, the setting.** Options: "Camera follows to the next ready unit": Always (at once, still never mid-order), When
  I'm idle (the default), Never. V cycles them. A saved On reads as When I'm idle, Off as Never. Also: "Camera goes
  ahead of a walk to the edge of the screen" and "Say 'Camera held'".
- Code: `RequestFollow`, `UpdateFollow`, `OnScreenNow`, `PointOut`, `MidOrder` (TMBattleDirectorFeel.cpp); the note
  and arrow in `DrawFeel`; `MaintainSelection`, `OrderSelected` (TMBattleDirector.cpp); `RunPlan`, `StepGoTo`,
  `StopGoTo`, `StepAbilityGoTo`, `CancelQueuesOnSight` (TMBattleDirectorPlans.cpp).

## Not done yet
- **A steady 60 fps.** The play logs of 2026-10-03 show 20-40 fps with frames up to 400 ms. Most of the long frames
  are the editor building meshes and textures the first time they are used ("Built Skeletal Mesh [9.18s] ... Wukong",
  "AssetCompile memory estimate is greater than available"): it happens playing from the editor's files, not in a
  packaged build, and saving those assets once stops it. Of the game's own work, the HUD's panels take 4-6 ms a frame
  ("SLOW Hud rest") and the fog about 5 ms when it redraws.
