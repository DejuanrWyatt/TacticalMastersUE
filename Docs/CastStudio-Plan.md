# Cast Studio: the plan for Tactical Masters

Written 2026-10-02 from `Docs/CastStudio-Blueprint.md` and a survey of this project and of the class creator
(`E:\TacticsClassCreator`). This is step 2 of the blueprint's section 0. Nothing is built yet: phase 1 waits for
approval.

**The short version.** Cast Studio is a feature of the class creator: a **Cast Studio** tab where each class's
animations and effects are authored, ability by ability. Everything on the game side exists only to play what the
creator authors and to show it back to the creator. It is not a separate Python app. The creator
already has most of the blueprint's studio: a local server locked to 127.0.0.1, an effect catalogue filmed by Unreal
with measured sizes, an animation catalogue filmed on every body, and per-ability effect and motion pickers. What
it lacks is the part that makes the studio worth having: a **timeline of events per ability** that the game plays
exactly. The work is in three places:

1. a small engine-free library that reads the published look files;
2. the director's playback code, which plays those looks in battle;
3. off-screen filming in Unreal (as the catalogues do now) that gives the creator what it needs to show the real
   thing with Unreal closed, plus **Film this ability** to record the real playback.

---

## 1. What the survey found

### The game

| Blueprint assumes | Tactical Masters has |
| --- | --- |
| GAS abilities that activate, commit and release on an AnimNotify | **A deterministic rules library (TMSim)** that settles an ability in one instant and reports a list of events (`FTickReport`: `CastStarted`, `Resolved`, `Hit`, `Evaded`, `Grazed`, `Critical`, `Absorbed`, `StatusApplied`, `Knocked`, `Revived`, `Teleported`, `Reaction`, `Redirected`, `BecameReady`, `TurnEnded`, `TimedOut`, …). The view never feeds back into the rules. |
| Server-authoritative gameplay; cosmetic RPCs or GameplayCues | **Lockstep online play.** Both machines run the same rules and compare checksums. Each machine draws its own looks from its own events. Replays work the same way. So looks need **no networking at all**, and looks can never cause a desync. |
| Damage moved to the montage's contact frame | **This is already done in the view.** The director holds each ability's consequences as a *blow* (`TMBattleDirectorBlows.cpp`). A blow lands at `ImpactAt` = the motion's `impact` share × the release clip's length, or when its arrow, orb or stone arrives. The health bars and log already have the result; only what is *seen* waits. |
| AnimBP with montages, layers and upper-body blends | **No AnimBP.** One clip at a time is played straight on each unit's mesh (`Animate` → `PlayAnimation`). Clips come from `Content/Data/CharacterMap/characters.json`: bodies (mesh, yaw, scale) and animation sets (idle/walk/run/attack/cast/hit/death, `motions.<motion>` with `intro`/`windup`/`release[]`/`castRelease`/`impact`, extras such as `stunned`, `ready`, `idle:<motion>`, plus `walkSpeed`/`runSpeed`). |
| Per-hero data | **Motion per ability, clips per body.** A class file's `anim` names one of 11 motions (`melee`, `heavy`, `shoot`, `bolt`, `area`, `heal`, `buff`, `revive`, `channel`, `dash`, `none`), or `TMSim::MotionOf` works one out. Each body's set turns a motion into clips, falling back to the nearest motion (`FindMotion`). Clips are bound to their skeleton, and several classes share one body. |
| One VFX entry per ability | **Two layers today.** (a) The class file's `vfx {system, at: user\|point\|targets, scale}`, played by `PlayVfx`. (b) The procedural "look" in `TMBattleDirectorAbilityFx.cpp`: each ability is read for its flavour (fire, ice, lightning, water, wind, earth, nature, holy, shadow, arcane, steel) from its name, borrowed fx, status, element and id. The flavour gives a cast flare in the hands, a flying shot, hits on each target (two layers), a ground burst (two layers), an ally aura, coloured light pulses and a camera jolt. Status looks come from `ShowStatuses` in `TMBattleDirectorBlows.cpp`. **The procedural look is the "legacy look" that strip removes.** |
| Stack counts | None. A status here is `{Id, Turns, Amount, By}` with no stacks, so pitfall 11 does not apply. A status does not record which ability gave it. The view will remember that itself (see 3.2). |
| Action timers | **Live gauge.** Each unit fills `Tg` to `Pace::TgMax`, becomes `bReady`, then has `Clock` ticks to act before `TimedOut`. Casts take `Casting.Total` ticks and channels repeat on later turns. Bosses already draw their cast telegraph **at the target spot** (`TMBattleDirectorIndicators.cpp`). |
| Units on an AoE scale | No global area scale. One metre is `TileSize` = 100 cm, and an area is drawn at `2 × aoe × 100` cm. Pitfall 9 is satisfied by construction; a test keeps it so. |
| Existing studios | `ATMVfxStudio` (`-tmvfxcatalog`) films every Niagara/Cascade system and records its `radius`. `ATMAnimStudio` (`-tmanimcatalog`) films every clip on every body through the director's own reading of the character map. Both are actors in the game module, started by a command-line flag from a `Tools\*.bat`. |

### The class creator

- **Server.** `server.mjs` is Node 22 (stdlib `node:http`). It allows only Host `127.0.0.1:<port>`, checks the
  origin, and requires an `x-creator-client: studio` header on every change. It saves atomically (temp, backup,
  rename, last 30 kept) and refuses stale revisions with a 409. It serves only `app/*.{html,css,mjs,svg,json}`.
- **Unreal access.** `app/runner.mjs` reaches Unreal through `Tools\VfxCatalog.bat` and `Tools\AnimCatalog.bat`, one
  at a time, and follows their progress from the log. It reaches the class lab for checks and playtests.
- **The "Animations & effects" tab** (`effectsTab` in `app/app.mjs`) has three parts:
  - an effect picker over the filmed catalogue, with packs, search, "show the ones that show nothing alone", Size and
    Where (`user`/`point`/`targets`);
  - a motion picker (Automatic or one of 11 motions) that plays the body's filmed clip sequence (`app/anim.mjs`
    `sequenceFor`);
  - the drawn sketch with its levers, which the game ignores.
- **What it lacks for Cast Studio:** events, moments, timing, statuses, a per-ability animation lane, contact
  markers, publishing to separate look files, and a live window into the real game.

---

## 2. Architecture for this game

**Revised 2026-10-02: Unreal is never open while authoring.** The creator works from strips Unreal films off-screen
and then closes, as the effect and animation catalogues already do. The live preview window is gone. In its place,
**Film this ability** runs the real playback off-screen and brings back a recording.

```
Unreal, off-screen, then exits:  -tmanimcatalog ──► Saved/AnimCatalog (strips + per-frame tracks, fixed camera)
                                 -tmvfxcatalog  ──► Saved/VfxCatalog  (strips + measured sizes)
                                 -tmcastfilm    ──► Saved/CastFilm    (one ability, played for real, filmed)   [phase 3]
Class creator (browser, Unreal closed) ── composes strips on the timeline ── "Publish" ──►
    Content/Data/CastStudio/AbilityAnimation.json (+ AbilityLooks.json in phase 2) ──► TMCast ──► director (matches, replays, online)
```

| Part | Where | Notes |
| --- | --- | --- |
| **TMCast** (new module, engine-free C++20 like TMSim) | `Source/TMCast/` | Reads and normalises both files. Answers "for this ability and moment, in this context, what spawns, where, for how long". Does fit-to-area maths, strip decisions and `when` filtering. It has no Unreal types, so the existing g++ test harness tests it (`Tests/CastTest.cpp`). TMSim never includes it, so the rules and checksums cannot be touched. |
| **Runtime player** | `TMBattleDirectorCast.cpp` (new) in the game module | Raises moments from the director's existing hooks and turns TMCast's answers into Niagara, Cascade and StaticMesh components, following owners and ending with them. It keeps a small spawn log that only Film this ability writes out. |
| **Film this ability** (replaces the blueprint's preview window) | `TMCastFilm.cpp` (new, phase 3), started with `-tmcastfilm` from `Tools\CastFilm.bat` | Off-screen, then exits, like the catalogue studios. It runs a **real battle**: the ability's class against 3 enemy dummies and 1 ally dummy on a small flat stage, played by the normal director, so pitfall 1 holds by construction. It films about 3 seconds from a fixed camera and writes a strip and `film-status.json` (`spawned[]`, errors, clip state). Development builds only. |
| **Studio** | The class creator: a **Cast Studio** tab that replaces "Animations & effects" and keeps its pickers, plus an all-classes progress view | Drafts live in the creator's `data/caststudio.json` under the same atomic save, backup and revision rules as `library.json`. **Publish** is the only action that writes game data, and it writes only the two look files. |

Why a module and not part of the game module: the playback rules (what spawns when, sizes, strip and `when`) are
where the bugs hide. Engine-free code can be tested by the same 1-second g++ harness the rules use, with no editor
start-up. The game module stays the thin part that spawns components.

Why not an UncookedOnly module with its own GameMode: this project does not use GameModes for its tools. The two
existing studios are flag-started actors, and Film this ability reuses the director itself. Following that pattern keeps
one way of doing things.

---

## 3. Data contract

### 3.1 `Content/Data/CastStudio/AbilityLooks.json` (the blueprint's AbilityVFX.json)

It follows the blueprint's 3.1 shape (`schemaVersion`, `abilities`, `statuses`, `footprints`), keyed by **ability
id** (`frost_stalker_ice_dagger`, the built-ins' ids, monster and pet ability ids). These are global in the rules
(`TMSim::FindAbility`). This game adds:

- `reactions.<shock|freeze|ignite|douse|thaw>`: events for the elemental reactions (`EEventKind::Reaction`).
- `battle.<moment>`: the live-tactics moments for every unit (3.2), for example the ready glow, so they are not
  copied onto 400 abilities.
- Event `effect` may also be `"light"` (a coloured pulse: `tint`, `brightness`, `radius`, `duration`) or `"shake"`
  (a camera jolt: `strength`). These are today's `Pulse` and `Jolt`, so a stripped ability can keep them by choice.
- `when` is `always · critical · normal`. This game's "empowered" is the critical hit. An ultimate is its own slot
  and needs no flag. An `impact` event can also say `on` (`struck · evaded · revived · any`) and `side`
  (`any · enemy · ally`).
- A shake's `harm` (`none · scale · add`) makes it as hard as the blow hurt, as today's jolts are.
- Event `effect` may also be `"sound"` (added 2026-10-03), with `sound` (a SoundWave or SoundCue's object path),
  `volume` (0-4, 1) and `pitch` (0.25-4, 1), heard at its anchor. An ability with a sound of its own replaces today's
  swing and landing sounds (its voice still plays). A `swing` event (of any kind) can be timed to a part of the swing:
  `part` (`windup · release · recover`) and `share` (0-1 of the way through it), measured on the body that swings
  (`TMCast::SyncSeconds`: the part's start, plus share × its length, plus `delay`).
- `footprints` gives how wide each effect was filmed. The creator writes it for every effect a look names: from an
  import of today's look where one brought it, else the effect filming's `2 × radius`. An effect with none is taken
  as a metre across.
- Anchors: `ground`, `body` (95 cm, where hits play today), `head`, `hand` (the `hand_r` socket if the body has one;
  otherwise today's hands point, 115 cm up and 45 cm forward, reported as `resolved: "fallback"`), `weapon` (the
  `weapon_r`/`weapon` socket, else as `hand`), `center`.

### 3.2 Moments mapped onto this game's events

| Moment | Raised from | Lives (`match`) |
| --- | --- | --- |
| `castStart` | `CastStarted` (cast-time abilities); otherwise when the blow starts (after any walk there: `Blow.bStarted`) | one-shot |
| `swing` (added in phase 2) | when the blow starts, cast or not: where today's flare in the hands plays, so an imported look keeps its timing | one-shot |
| `casting` | while `Unit.IsCasting()` or `IsChanneling()` for that slot | the cast |
| `release` | the blow's contact: `Blow.Since >= Blow.ImpactAt`, where `LaunchShots` runs today | one-shot |
| `projectile` | each `FTMShot` (the arrow, orb or stone), attached to it | its flight |
| `impact` | `LandBlow`, for each unit touched (`Hit`, `Evaded`, `Absorbed`, `Revived`, `StatusApplied`), plus the aim point for a ground blow. `Grazed` and `Evaded` are context the event can filter on later. | one-shot |
| `area` | `LandBlow`, for an area ability: the ground it covers. There are no lasting ability-made zones in the rules today, so `match` behaves as `fixed`. | the burst |
| `targetStatus` · `allyStatus` · `selfStatus` | each frame, from the unit's statuses (as `ShowStatuses` reads them). The view remembers which ability applied each `(unit, status)` at `StatusApplied`. This needs no change to the rules. | the status |
| `tick` | a status's per-turn damage or healing (a `Hit` carrying a status id outside a blow) | one-shot |
| `expire` | a status that ends (seen gone from the unit), a pet leaving (`Gone` on a pet), a channel's last turn. It plays **before** that frame's hits. | one-shot |
| `summon` | a pet arriving (`Teleported`, Id `pet`), a monster tamed (`Tamed`) | its stay |
| `proc` | passives: a `Saved` (Phoenix Feather), a `Staggered`, and passive abilities' own events | one-shot |
| `link` | `Redirected` (a guardian taking a hit for someone; a reflect), between the two units | one-shot, stretched A→B |
| `statusActive` · `statusGain` · `statusTick` · `statusEnd` | the `statuses` table, for any source | as above |
| **Live tactics** (`battle` table, or per ability where it makes sense) | | |
| `windowOpen` | `BecameReady` until the unit acts (`bReady`) | the window |
| `windowClosing` | the last N seconds of `Clock` (N in the file; default 3 s) | the rest of the window |
| `actionCommit` | the order accepted for that unit (the director's order path) | one-shot |
| `actionQueued` | an order waiting for its walk (`Motion.Queued`), and Go To orders | until it starts |
| `timerReset` | `TurnEnded` (the gauge back to 0) | one-shot |
| `windowMissed` | `TimedOut` | one-shot |
| `intentShown` | a monster's cast telegraph (the boss casting marker), on the caster and at the target spot | the cast |

Every damage or healing instance gets a moment. Phase 4 generates the table that proves it
(`Docs/CastStudio-Moments.md`).

### 3.3 `Content/Data/CastStudio/AbilityAnimation.json` (the blueprint's UnitAnimation.json)

This game's clips belong to **animation sets** (one per skeleton). So the file has two halves:

```jsonc
{ "schemaVersion": 1,
  "sets": { "<animation set>": {            // overrides on characters.json, only what the designer picked
      "status": "draft|final",
      "locomotion": { "idle": "<clip>", "walk": "...", "run": "..." },
      "feel": { "turnRate": 540, "runFrom": 3.5, "walkSpeed": 170, "runSpeed": 380 },   // defaults = today's values
      "reactions": { "hit": ["..."], "death": ["..."], "stunned": "...", "ready": "..." } } },
  "abilities": { "<abilityId>": {
      "default": { "windup": {...}, "release": {"clip": "", "contact": 0.4, "rate": 1}, "loop": {...}, "recover": {...} },
      "sets": { "<animation set>": { ...the same, for one body... } },
      "matchCastTime": true } } }
```

- `contact` replaces the set motion's `impact` for that ability. It drives the held blow, the `release` moment and
  the shot launch. All three already share one clock (`ImpactAt`), so they land together.
- `matchCastTime` stretches only the wind-up, which already loops for the cast's length (`StandingClip`). Release
  and recover are never squashed (pitfall 4).
- A clip picked for an ability without a per-set entry plays only on bodies whose skeleton it fits. Any other body
  reports `"<clip> can't play on <body>: it is made for <skeleton>"` (pitfall 2) and keeps today's motion.
- `rootMotion`, `upperBodyOnly` and `blendIn`/`blendOut` are **left out**. This game has no AnimBP, so there are no
  layers to blend. Dashes already travel along the rules' path. They can be added if an AnimBP ever arrives.
- Publish writes only the designer's picks. `characters.json` stays the source of today's look and is never
  rewritten.

### 3.4 Fit to area

The shapes come from the rules (`TMSim::ShapeOf`, `MaxRange`, `Aoe`):

- circle or self-circle: diameter `2 × aoe × 100` cm;
- cone: length = range, centred at 0.55 × range as today;
- line or vector: width `2 × aoe`, from the caster to the aim.

Footprints come, in order, from the published `footprints` table, the effect catalogue's measured `radius`
(`ATMVfxStudio` already measures it), and a Film this ability run's measurement. The studio shows "effect 310 cm vs area
450 cm → ×1.45" and offers Match area.

### 3.5 Strip

| Mode | What it removes for that ability |
| --- | --- |
| `all` | the procedural look (`LookCast`, `LookLand`, `LookOn`, `LookShot`, its light pulses and jolt, an orb's glow), the class file's `vfx`, and the plain sphere, rod or stone mesh when a `projectile` event plays an effect |
| `legacy` | only the class file's `vfx` |
| `none` | nothing; the events are added on top |

The aim indicator, zone rings and boss telegraphs always stay (`keepTelegraph`). Sounds are untouched.

**Import today's look** gives a designer something to edit instead of a blank. The class lab (`TMClassLab looks
<class file>`, engine-free, so Unreal stays closed) writes, for each of a class's abilities, the events the procedural
look would spawn (flavour, sizes, offsets, light, jolt, and the class file's own effect), from the same table and
reading the director uses (`Source/TMCast/Public/CastLegacy.h`). The studio can seed an ability from it: strip `all` plus the imported events
looks the same as today, but every piece is now editable. A Final ability with strip `all` or `legacy` also has its
class-file `vfx` removed through the creator's normal install path, after a backup.

---

## 4. Filmed instead of live (revised 2026-10-02)

**Animation filming** (`ATMAnimStudio`, `-tmanimcatalog`; catalogue version 2):
- **One fixed camera for every body.** The catalogue records its field of view, pitch, yaw, distance and look
  point. Any point in centimetres projects to a pixel, so effect strips (sized in centimetres by the effect
  filming) can be drawn on a body strip at the right size and place.
- **Tracks per frame:** where the `hand_r`, `hand_l`, `head` and weapon sockets are (pixels and centimetres), and
  where the root and pelvis bones are, giving root drift. Effects anchored to a hand follow the swing in the
  browser, and the animation check reads its numbers from the tracks (phase 4).
- **32 frames a second**, at most 3 seconds, laid out as a grid (16 frames a row) so no picture is too wide for a
  browser. The contact marker is accurate to about 30 ms.
- **Each body's whole clip library** is listed (every clip on its skeleton, not only the ones the character map
  uses), with names only. **Film the picked clips** films just the clips a designer has picked, off-screen, without
  re-filming everything. Every clip named in the published `AbilityAnimation.json` is filmed by the full filming
  too.
- Each motion's contact share (`impact`) is written, so the creator starts from what the game does today.

**What the browser shows is a composed sketch**, labelled as one: tint is approximated, and lighting and depth are
not exact. **Film this ability** (phase 3) is the check against the real thing: same director, same playback,
recorded off-screen.

---

## 5. Studio in the class creator

- **Cast Studio tab** for the selected class:
  - ability chips (Needs picks / Draft / Final);
  - a **timeline** with lanes (Animation · Caster · Projectile · Targets · Allies · Ground) across phases
    (Cast → Release → Travel → Impact → Status → Expire). Phase lengths come from the ability's live numbers: cast
    seconds, the release clip × contact, flight = distance ÷ shot speed, status turns;
  - the existing effect picker and motion strips, now feeding slots;
  - a slot editor in plain words (when, where, size + Match area, offset, rotation, colour, critical-only, order,
    Solo/Duplicate/Remove, + Add effect at…);
  - strip controls with **Compare with today's look**;
  - **Film this ability**.
- **All classes view:** each class with its Final progress, and the bodies with whether their sets have picks
  ("shared by 7 classes").
- **In-browser playback:** the timeline plays the filmed strips together as a quick sketch, labelled as filmed. The
  Film this ability is the truth.
- **Server routes** (same guards as today, plus a per-session token): `GET/PUT /api/cast` (the ledger),
  `POST /api/cast/publish?dry=1`, `POST /api/anim/film-clips` (film picked clips), and in phase 3
  `POST /api/cast/film` with `GET /api/cast/film` (progress and result).
- **UX rules from the blueprint:**
  - lists keep scroll, filter and focus across picks and polls;
  - base font ≥ 15 px;
  - keys J/K (next/previous ability), Space (play), F (mark Final);
  - progress and a per-item result for every slow action;
  - live numbers come from the class at display time.
- **Ledger evolution:** normalise on read and on publish; a regression test loads a ledger saved before each new
  field was added (pitfall 19).

---

## 6. Phases and this game's acceptance checks

Each phase ends with:

- the g++ sim and cast tests, plus `node --test` in the creator;
- a changelog and worklog entry, and a RESUME note (`Docs/CastStudio-Resume.md`: done / not done / next /
  assumptions / gate logs).

You run `build-and-test.bat` on your machine; I won't run anything on your PC without asking.

**Revised 2026-10-02: creator first, animations first.** Each phase lands something you use in the class creator.
The game-side work in a phase is only what that creator feature needs.

| # | Phase (what you get in the creator) | Accept when |
| --- | --- | --- |
| 1 | **Animations in the Cast Studio tab.** The tab replaces "Animations & effects" and keeps its pickers. For each ability you get an animation lane: wind-up, release with a **contact marker dragged on the filmed strip**, loop and recover, previewed in sequence. Clips are picked from the body's whole clip library (its own skeleton only; with the reason given when one can't play), and **Film the picked clips** films new ones off-screen. Drafts are kept in `data/caststudio.json`. **Publish** writes `AbilityAnimation.json` with a backup. Game side: the animation filming upgrade in section 4, plus TMCast, which reads the file so the director plays the picks and uses the picked contact for when the blow lands. | Node tests pass (store, normalise, old-ledger publish, publish round trip). The cast tests pass (parse, normalise, contact maths). With no file, a battle plays as today. After a re-film, the creator shows tracks and 32 fps strips, and a pick made with Unreal closed plays in a match after Publish. |
| 2 (done 2026-10-03) | **Effects timeline.** Lanes (Caster · Projectile · Targets · Allies · Ground) across Cast → Release → Travel → Impact → Status → Expire, with the slot editor, **Import today's look**, strip and Compare with today's look. Publish also writes `AbilityLooks.json`. Game side: the runtime player for every moment in 3.2 except the live-tactics ones. | The cast tests pass (moments, ordering, anchors, strip, `when`, fit to area within 5%). With no file, today's look is unchanged. An imported-then-stripped ability looks the same as today. One authored event changes one ability in a match. |
| 2b (done 2026-10-03) | **Sound effects.** A Sound effects section beside the timeline: the project's sounds (copied out as .wav by `Tools\SoundCatalog.bat`), auditioned and placed at any moment, timed to a part of the swing on a ruler of the filmed swing, heard in the sketch in time with the body. Game side: sound events, swing timing, today's swing and landing sounds replaced by an ability's own. | The cast tests pass (sound events read and refused, timing by part on two bodies, round trip). With no sound authored, today's sounds play. |
| 3 | **Film this ability**, from the creator. It runs off-screen and closes, and the recording plays next to the browser sketch with the run's `spawned[]`, errors and clip state. | Film runs for 6 sample abilities report PASS (`spawned[]` matches the authored events). |
| 4 | **Animation check and movement feel** in the creator, from the filmed tracks: numeric flags per body (root drift, contact past the clip's end, missing clip), `turnRate`/`runFrom`/`walkSpeed`/`runSpeed` per body (defaults = today), and a movement course's foot-slide numbers. | Every body checks with no silent fallback. Defaults reproduce today's motion (test). Suggestions are listed, not applied. |
| 5 | **Damage-moment audit**, shown in the creator as a per-ability "every hit has a look" check. | Every ability has a row in `Docs/CastStudio-Moments.md`. No damage or healing lacks a moment. No wind-up is drawn under a caster whose ability lands elsewhere. |
| 6 | **Polish.** StaticMesh effects, critical-only events, links, keyboard (J/K, Space, F), scroll retention. | Each item has a test. |
| 7 | **Live tactics moments.** Ready glow, closing window, commit, queued, reset, missed and intent, authored in the creator, previewed on the composed timeline. | They play in a match and in a Film this ability run. |
| later | Live retargeting (deferred, decision B). | |

---

## 7. The blueprint's pitfalls, for this game

| # | Here |
| --- | --- |
| 1 | Holds by construction: Film this ability is the director. The browser's composed sketch is labelled as a sketch. |
| 2 | Every missing effect, socket, clip or body reports `resolved` + reason in a film run and in the studio. The game already logs a missing effect once. |
| 3, 14, 15, 16 | Only if live retargeting is built (decision B). |
| 4 | The wind-up loops; release and recover are never stretched. |
| 5, 6, 21, 22 | The filming camera is fixed, and the studio keeps scroll, filter and focus. `node --check` runs on generated pages. |
| 7, 8 | Phase 4. Boss telegraphs are already at the target. |
| 9 | Metres × 100, tested. |
| 10 | A 0-tick cast plays no wind-up, not a floored one. |
| 11 | No stacks in this game. |
| 12 | Ground aim is already the rules' business and untouched. |
| 13, 25 | Cloth is already off where needed (Terra). The filming reuses a body when only the clip changes. |
| 17 | Already true of the Paragon bodies. |
| 18 | Empty files = today's look, tested in phase 1. |
| 19 | Normalise on read and publish, tested. |
| 20 | Tests set the rules' inputs, not view state. |
| 23 | Fab and Paragon content stays out of git, as now. |
| 24 | One Unreal at a time (the runner's lock). Your gates stay yours to run. |

---

## 8. Assumptions (my defaults; say if any is wrong)

1. The files go in `Content/Data/CastStudio/`, beside Classes, Maps and CharacterMap. They are named for this game
   (AbilityLooks, AbilityAnimation) rather than the blueprint's names.
2. Drafts live in the creator (`data/caststudio.json`), not in `Saved/`, because the creator is the studio and
   already keeps backups.
3. The class files' `vfx` and `anim` keys stay. `vfx` becomes the "legacy" layer, and `anim` still chooses the
   motion.
4. Sounds are out of scope. A `sound` event kind can come later.
5. No lasting ability-made zones exist, so `area` is the landing burst. If zones are added to the rules, `area` gets
   a real owner.
6. Built-in classes, monsters and pets are covered by id. Phase 3 adds a class-lab command that lists the built-ins'
   abilities for the studio.

## 9. Decisions

- **A. The seed: decided 2026-10-02, import today's look.** Every ability is seeded from its procedural look
  (Import today's look), and the designer renames and re-picks.
- **B. Live retargeting: decided 2026-10-02, deferred.** Phase 1 offers only clips made for each body's own
  skeleton and says why for the rest. Live retargeting can be its own phase later.
- **C. Settled 2026-10-02:** there is no live window. All filming, including Film this ability, runs in the editor
  build off-screen, like the catalogue studios.
- **Plan revised 2026-10-02:** creator first, animations first (section 6).
- **Plan revised 2026-10-02:** no Unreal open while authoring; filmed strips with tracks, and Film this ability instead of a live window (sections 2 and 4).
