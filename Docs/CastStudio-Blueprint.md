# Cast Studio Blueprint: a per-ability VFX, animation and movement authoring studio for Unreal Engine 5.8 (C++)

> **For Claude.** This file is a specification and a list of lessons, written for an AI coding agent working in a UE 5.8
> C++ game. It describes a system that was built and iterated in another game (a co-op hero defense, "Cire's Team
> Survival"). Your job is to build **the same capabilities, adapted to this game**, in fewer iterations, by following the
> rules and avoiding the pitfalls listed here. Examples from the original game are marked *(example)*; do not copy its
> content, only its shape.

---

## 0. How to start (paste this as the first message)

```
Read Docs/CastStudio-Blueprint.md completely. Then, before writing any code:
1. Survey this project: the ability system (GAS or custom; where abilities activate, commit, apply effects, spawn
   projectiles/areas, end), how units are spawned and animated (skeletons, AnimBPs/AnimInstances, montages), how VFX are
   spawned today, how per-unit action timers / action windows work, the replication model, and the existing data files.
2. Write Docs/CastStudio-Plan.md: how each section of the blueprint maps onto THIS game (moments, hooks, data files,
   module names, the unit/ability ids), what changes for live tactics (per-unit timers and action windows), and the
   phase order from section 10 with this game's acceptance checks.
3. Stop and show me the plan. Build phase by phase after I approve, committing and testing at the end of each phase.
```

---

## 1. What the system is and why

A **designer-facing studio** where every ability's look and feel is authored **per ability**, on a timeline, with a live
preview that is **exactly** what the game plays. The designer (not the code) decides, for each ability:

- which visual effect (Niagara, Cascade or a 3D static mesh) plays **at which moment** of the action: wind-up, cast,
  release, travel, impact on each target, ticks, persistent status on targets/allies/self, area on the ground, expiry or
  detonation, summons, passive procs, links between objects;
- **where** (Ground / Body / Head / Hand / Weapon / Center), **orientation** (from the caster, aim, target or world),
  **offset X/Y/Z**, **rotation**, **scale** (uniform and per axis, or "fit to the ability's real hit area"), **timing**
  (delay, duration, repeat), **colour**, **order/layering**;
- which **animation clips** play for the wind-up, release (with a draggable **contact frame**), loop (channels) and
  recover, and each unit's **locomotion set** and **movement feel** (turning, starts/stops, stride, leg IK, blending when
  acting while moving);
- whether the **old visuals are stripped** so only the designer's picks remain.

It has three parts that share **one data contract**:

| Part | What it is | Where it runs |
| --- | --- | --- |
| **Runtime player** | Reads the published data and plays events at the right moments in the real game. Cosmetic only. | Game module (client side) |
| **Preview window** | A small Unreal window showing the unit's real body acting against dummies, driven by the studio. Uses **the same runtime player code**. | A separate *UncookedOnly* module, launched with `-game` |
| **Studio (web app)** | A local browser app: pick unit → ability → timeline; pickers; publish. | Python stdlib HTTP server on 127.0.0.1 + plain HTML/CSS/JS |

**Non-negotiable principles** (each one cost iterations in the original build):

1. **What the preview shows is what the game plays.** One code path. The preview calls the same runtime functions with a
   simulated context. Never re-implement playback in the preview.
2. **No generic options.** Every ability starts with **named, empty slots** derived from the designer's own notes
   ("Kindling debuff on target, persistent", "Initial cone", "Ground effect"). The designer only picks assets and tunes.
3. **The designer controls everything visible.** Every animation choice and every movement-feel constant is a data field
   editable in the studio, with a live preview. Defaults reproduce today's look exactly until the designer changes them.
4. **Never fail silently.** If a clip, effect or body cannot play, show a clear message ("<clip> can't play on <body>:
   <reason>") and keep the idle running. No fallback that looks like "nothing changed".
5. **Verify with numbers, not pixels.** Every part reports machine-readable status (JSON) so tests and the AI can verify
   behaviour without looking at images. Screenshots are for the human only.

---

## 2. Architecture

```
Studio (browser) ──HTTP, token──► Studio server (Python) ──writes──► viewer-request.json ──► Preview window (UE -game)
      ▲                                  │   ▲                                                   │ same runtime player
      └──────────── /api/status ◄────────┘   └──────────── viewer-status.json ◄──────────────────┘
Studio server ──"Publish"──► Content/Data/AbilityVFX.json + UnitAnimation.json ──read by──► Game runtime player
```

- **Studio server:** Python 3.10+, standard library only (`http.server`), bound to **127.0.0.1**, checks the Host header,
  requires a per-session token on every API call, serves only whitelisted static files. Ledgers (the designer's drafts)
  live in `Saved/<Studio>/` and are written atomically (temp file + rename) under a file lock.
- **Preview window:** an `UncookedOnly` module (never cooked, never loaded by a packaged game) with its own GameMode chosen
  by the map URL option (`?game=/Script/<Module>.<PreviewGameMode>`). It polls `viewer-request.json` (by `seq`) and writes
  `viewer-status.json`. It spawns no AI, waves or simulation, only the unit body, dummies and the timeline.
- **Runtime player:** a small module in the game (`<Game>VFXTimeline`), plus an animation-plan module
  (`<Game>UnitAnimation`). Both load JSON from `Content/Data/`, reload on a console command and, in development builds,
  when the file timestamp changes.
- **Publish** is the only studio action that writes game data, and it is cosmetic data only (VFX timelines and animation
  choices). It backs up first. Gameplay numbers (damage, cooldowns, radii) do **not** go through Publish; they stay in
  the game's normal data/review path.

---

## 3. Data contract

### 3.1 `Content/Data/AbilityVFX.json`

```jsonc
{
  "schemaVersion": 1,
  "abilities": {
    "<abilityId>": {
      "status": "draft" | "final",
      "strip": "all" | "legacy" | "none",   // default "all"
      "keepTelegraph": true,                // the wind-up warning stays (the aim indicator ALWAYS stays)
      "keepZoneOutline": true,              // an active zone keeps a thin outline of its true edge
      "events": [ <event>, ... ]            // array order = spawn order and draw layering
    }
  },
  "statuses": {                             // visuals that belong to a status, whichever ability applied it
    "<statusKey>": { "events": [ <event>, ... ] }
  },
  "footprints": { "<effect path>": { "radius": 310, "height": 220 } }   // measured sizes, copied in by Publish
}
```

**Event** (all fields optional except `id`, `moment`; the server fills defaults):

| Field | Values | Meaning |
| --- | --- | --- |
| `id` | `ev_<12 hex>` | stable id |
| `label` | text | the designer's slot name |
| `enabled` | bool (true) | |
| `when` | `always` · `empowered` · `normal` | play only on the empowered instance (threshold cast, empowered recast, primed hit, status payoff), or only on the others |
| `effect` | object path (Niagara, Cascade or StaticMesh) or `""` | empty = slot not picked yet; skipped |
| `moment` | see 3.2 | when it spawns |
| `anchor` | `ground` · `body` · `head` · `hand` · `weapon` · `center` | `center` = the point itself (aim point, zone centre, projectile) |
| `follow` | bool | attached and moving with its owner, or left where spawned |
| `facing` | `caster` · `aim` · `target` · `world` | orientation frame |
| `offset` | `[forward, right, up]` cm | in the facing frame |
| `rotation` | `[pitch, yaw, roll]` deg | added to the facing |
| `scale`, `scale3` | number, `[x,y,z]` | uniform, then per axis |
| `fitRadius` | bool | fit to the ability's real hit area (3.4), then × scale (1.0 = exactly the hit area) |
| `delay` | s | after the moment |
| `durationMode` | `match` · `fixed` · `oneShot` | `match` = lives as long as the moment's owner |
| `duration`, `repeat` | s | `fixed` length; respawn every N s while the owner lives (pulses, trails) |
| `tint`, `tintStrength` | `#RRGGBB`, 0..1 | recolour exposed colour parameters, keeping brightness |
| `minRank` | int | e.g. visuals unlocked at a rank |
| `notes` | text | |

### 3.2 Moments

| Moment | Spawns on | Lives (`match`) |
| --- | --- | --- |
| `castStart` | the caster when the action begins | one-shot |
| `casting` | the caster from start to release (cast bar / channel) | the cast |
| `release` | caster or aim point, **on the animation's contact frame** | one-shot |
| `projectile` | each projectile / thrown object / bounce hop | its flight |
| `impact` | each unit hit (and the point of a ground hit) | one-shot |
| `targetStatus` | each enemy holding a debuff from this ability | the debuff |
| `allyStatus` | each ally holding a buff/aura from this ability | the buff |
| `selfStatus` | the caster while its self buff / shield / channel / dash lasts | the buff |
| `area` | the zone / field / wall | the zone |
| `tick` | each damage tick or pulse (zone centre or each ticked unit) | one-shot |
| `expire` | when a zone/shield/buff/summon ends or detonates, **before** the detonation's hits | one-shot |
| `summon` | each summon/construct the ability creates | its life |
| `proc` | a passive's trigger | one-shot |
| `link` | between two linked objects of one owner (beam: `BeamStart`/`BeamEnd`, else stretched along A→B) | the link |
| `statusActive` · `statusGain` · `statusPayoff` | (statuses table) unit holds the status · gains a stack · threshold/detonation | status / one-shot |

**Live tactics additions (adapt to this game's timers):** add moments for the per-unit action cycle, for example
`windowOpen` (the unit's action window opens: a ready glow), `windowClosing` (the last N seconds), `actionCommit` (the
player issued the action; useful for a "locked in" flash), `actionQueued`, and `timerReset`. They spawn on the unit,
live as long as the window (`match`), and are authored exactly like the others. If enemies telegraph intent, add
`intentShown` on the enemy and its target cell/area.

### 3.3 `Content/Data/UnitAnimation.json`

```jsonc
{ "schemaVersion": 1,
  "units": { "<unitId>": {
     "status": "draft|final",
     "locomotion": { "idle": "<clip>", "walk": "...", "run": "...", "strafeL": "...", "strafeR": "...", "backpedal": "...",
                     "start": "...", "stop": "...", "turnL": "...", "turnR": "...", "blendSpace": "<optional>" },
     "feel": { /* every movement constant; see section 6. Defaults = today's values. */ },
     "basicAttack": { "combo": ["<clip>", ...], "contact": [0.32, ...], "syncToInterval": true, "upperBodyWhileMoving": true },
     "roll": "...", "hitReact": ["..."], "stunned": "...", "knockedUp": "...", "death": "..." } },
  "abilities": { "<abilityId>": {
     "windup":  { "clip": "<path>", "start": 0, "end": 0, "rate": 1 },
     "release": { "clip": "<path>", "contact": 0.4, "rate": 1 },
     "loop":    { "clip": "<path>" },
     "recover": { "clip": "<path>", "rate": 1 },
     "rootMotion": "none|clip|toTarget", "upperBodyOnly": false, "blendIn": 0.1, "blendOut": 0.15, "matchCastTime": true } } }
```

- `contact` drives **the gameplay release**, the `release` VFX moment and the hit. All three land on the same frame.
- `matchCastTime` stretches **only the wind-up** to the cast time. Never squash the release/recover. *(A 0 s cast once
  squashed the release clip to 0.12 s and it looked like "the animation does not play".)*
- Publish writes **only the designer's picks**. Today's clips are derived live and never written, so publishing an
  untouched unit changes nothing.

### 3.4 Fit to area (per shape)

- circle/zone: footprint diameter = 2 × radius;
- cone: length = cone length, far width = 2 × length × tan(angle/2), centred half the length along the facing;
- line: length × width; wall: length × height.
- Footprint sources, in order: the published `footprints` table, the preview's own measurement (measure an unknown
  effect over its first ~1.5 s at scale 1 and cache it), a curated table, a mesh's bounds. Show the numbers in the
  studio ("effect 310 cm vs area 450 cm → ×1.45") and offer "Match area" (one-shot) as well as the live toggle.
- **Sizes are in-game sizes.** If the game globally scales areas (the original scaled every AoE ×1.3), the designer's
  "6 m" must still be 6 m on screen and in the hit test: author `metres × 100 / globalScale`, read the factor from data,
  and test it.

### 3.5 Strip

- `all` (default once the designer picks anything): the game draws **only** this ability's events. Legacy Niagara,
  procedural shapes, projectile heads/meshes, cast flares, circles under the caster and aura layers from this ability go.
  The aim indicator always stays. The wind-up warning stays with `keepTelegraph`. Active zones keep an outline with
  `keepZoneOutline`.
- `legacy`: only the old Niagara/Cascade entries go. `none`: events are added on top.
- On Publish, a **Final** ability with `all`/`legacy` also has its old entries **deleted** from the legacy data (backed
  up first), so only what the designer picked remains in the project.
- The studio has **Compare with old look** (plays the ability as the game draws it without the new entry) and warns
  "nothing will show here" for a stripped moment with no pick.

---

## 4. Game integration (GAS mapping; adapt to this project)

- **Server-authoritative gameplay, client-side cosmetics.** The runtime player only spawns visuals on clients. Server
  code raises moments with one call, e.g. `VFXTimeline::Notify(Caster, AbilityId, EMoment::Impact, Unit, Point, Aim,
  Seconds)`, which reaches clients through a controller/actor RPC or a **GameplayCue**. Cosmetic messages may be
  unreliable; anything tied to damage should ride with the damage event.
- **Activation and commit.** Split every action into **acceptance** (validation, cost, cooldown, the action-window
  rules, the wind-up animation, `castStart`/`casting`) and **release** (damage, CC, riders, `release`) on the contact
  frame: an AnimNotify on the montage → `WaitGameplayEvent` ability task, or a timer equal to the authored contact lead.
  A stun or death before the contact **cancels** the release. Cost and cooldown stay spent.
- **Statuses.** Map `targetStatus`/`allyStatus`/`selfStatus` and `statuses.<key>` onto **GameplayEffect applied/removed**
  (active effect handles and their tags). Make sure every applied effect **knows which ability applied it**
  (EffectContext source ability). Stack counts come from the effect's stack count, so check the stack limit type (one
  original bug: a `uint8` capped "unlimited poison" at 255).
- **Projectiles, areas, summons.** Raise `projectile` when a projectile actor spawns (attach and follow), `impact` on each
  hit, `area` when a zone actor spawns (owner = the zone), `tick` per pulse, `expire` before the detonation's hits,
  `summon` when the summon spawns.
- **Everything travels.** Bounces, chains, returning projectiles, hooks, charges and leaps must visibly travel and hit on
  arrival, server-side, replicated smoothly. Instant multi-hits read as "nothing happened".
- **Every damage instance has a moment.** Audit every ability's tooltip: each damage/heal instance it describes needs a
  moment (impact, tick, expire). Generate the table automatically and keep it in the repo.
- **Live tactics.** Raise the timer/window moments (3.2) from the action-timer system, and make the preview able to
  simulate a timer. Telegraphs are critical in tactics: draw wind-ups **at the target location only**, never under the
  caster when the action lands elsewhere.

---

## 5. Preview window

- **Real body.** Spawn the unit's actual game visual (mesh, weapons, anim instance, montages) facing 3 enemy dummies and 1
  allied dummy (static bodies, no AI). Two object dummies for skills that link objects.
- **Modes**, selected by the request:
  - `cast`: plays one ability's events on its real timing and loops. The loop waits for the animation to finish.
  - `effect`: a single effect.
  - `anim` (animation browser): any clip on any unit, looped, with rate and in-place/root-motion options.
  - `animCheck` (check mode): idle/walk/run/attack/cast/hit/death on every unit, with numeric red flags (foot slide, root
    drift, contact after the clip ends, missing clip). It can load a **candidate body** to compare a new model with the
    current one.
  - `movement`: the unit runs a scripted course (start, run, stop, 90°/180° turns, strafe, backpedal, turn in place,
    roll, act while moving, slope) using the live feel settings, and reports foot slide, heading error, pose pops and
    yaw snaps per segment.
- **Status JSON** (`viewer-status.json`): `seq`, `mode`, `phase`, `t`, `spawned[{event, moment, asset, resolved, at,
  t}]`, `errors[]`, timing, the body and clip in use, the clip's `resolved` state (`ok | live-retarget | offline | error`
  + reason), `animError`, `camera`, measured `footprints`, `hitArea`. Tests and the studio read this, never pixels.
- **Draw the true hit area** (an outline) under the effect while a slot is selected, so the designer can match sizes by eye.
- **Camera belongs to the designer.** Frame the stage once at session start. Play, replay, auto-replay, ability/unit/mode
  changes and stage rebuilds **must not move the camera**. Only an explicit camera command (`cameraSeq` bump from the
  studio's Yaw/Pitch/Distance controls or "Reset camera") or the window's own orbit/zoom moves it. Report the current
  camera so the studio's controls stay in sync.
- **Live retarget (essential).** Library clips sit on their packs' skeletons (UE5 Manny/Quinn for most marketplace packs).
  The browser must play **any** clip on **any** humanoid body immediately: sample the source clip on its pack mannequin,
  run each frame through the body's IK Retargeter, and write a **transient AnimSequence** on the body's skeleton (about
  1 s for the first bake per body, then 10–20 ms per clip). Play it through the normal anim layers, so timing, scrubbing
  and upper-body masks work. Keep every bone's reference scale, and refuse a bake whose pelvis height drifts more than
  ~35% from the source's. Build the per-body IK rigs and retargeters with a tools script and index them in data.
  The game still uses **offline** retargeting for published picks (a "Run retarget now" button in the studio runs the
  headless editor with the queue, then reopens the preview). Quadrupeds: report "can't retarget this body" clearly.

---

## 6. Animation and movement feel

- **The designer owns motion.** Every movement constant is a `feel` field: turn follow time and max turn rate, start and
  stop blends, stride matching, leg IK, `castMoveBlend` (upper body / full body / stop), upper-body blend-in, roll
  cross-fades, legs following a strafe, backpedal playing the run in reverse, keeping creature legs running under
  actions. **Defaults = today's exact values**, and a test checks that the defaults reproduce the old pose frame by frame.
  Publish suggestions for each unit, but don't apply them.
- **Directional locomotion matters most.** Bodies that only have forward idle/walk/run clips skate when strafing or
  backpedalling (measured 650–730 cm/s foot slide). Give every humanoid strafe/backpedal clips from the owned packs.
- **Basic attacks** are a combo of clips, each with a contact marker, optionally synced to the attack interval, with an
  upper-body-only option while moving.
- **Animation lane per ability**: wind-up / release (draggable contact) / loop / recover, plus root motion `none | clip |
  toTarget` (charges and leaps travel along the gameplay path, not the clip).

---

## 7. Studio (web app) requirements

- **Default view = the studio**: unit list (with Final progress), then the unit's abilities (chips: Needs picks / Draft /
  Final, the designer's note quoted under each name), then a **timeline** with lanes (Caster · Projectile · Targets ·
  Allies · Ground, plus the Animation lane on top) across phases (Cast → Release → Travel → Impact → Duration → Expire),
  sized from the ability's live numbers.
- **Seed file written by hand from the designer's notes** (not generated generically): named empty slots per ability,
  with sensible moment/anchor/follow/delay/repeat defaults. Abilities called "fine" import today's look as events.
  Seeding is idempotent and never overwrites edits.
- **Pickers** over catalogues exported from the Asset Registry: effects (Niagara/Cascade: path, pack, look tags,
  measured footprint), **meshes** (StaticMesh: path, pack, bounds cm, tris, tags) and **clips** (path, pack, skeleton,
  length, root motion, in-place, family/kind tags). Search, favourites, recently used, "suggested for this unit" first.
- **Slot editor in plain words**: when (moment, delay, repeat, duration), where (anchor, follow, facing), size (scale,
  stretch, fit to area + numbers + Match area), offset, rotation, colour, "only on the empowered cast / only on normal
  casts", order arrows, Solo / Duplicate / Remove, + Add effect at…
- **Strip control** per ability and per unit, Compare with old look, Keep zone outline, Keep wind-up warning.
- **Unit body switch** (current ↔ candidate model) wherever a candidate exists; key **B** swaps and replays.
- **Publish** (dry run preview, then write, backup, legacy cleanup for Final+strip), **Run retarget now**.
- **UX rules learned the hard way:**
  - lists **keep their scroll position, filter text and search focus** across select, try, pick, save and background status
    polls (store `scrollTop` per list + filter, or update rows in place);
  - base font ≥ 15 px; keyboard J/K (next/previous ability), Space (play), F (mark Final);
  - every slow action shows progress and a result per item;
  - the live game numbers (cooldown, cost, range, radius, duration) are read from the game data at display time.
- **Ledger schema evolution**: when a new field is added (e.g. `when`), old saved drafts must load and publish with the
  field's default, never with a `KeyError`. Normalise on read **and** on publish, and add a regression test that loads
  a ledger saved before the field existed.

---

## 8. Pitfalls (rules to follow from the start)

Each was a real bug or a wasted iteration. The symptom is given so you recognise it.

| # | Rule | Symptom if ignored |
| --- | --- | --- |
| 1 | The preview and the game share one playback code path. | "It looked right in the studio but not in the match." |
| 2 | Never fall back silently. Report `resolved` + reason. | Every clip the designer tried looked identical (36/37 picks were `missing-retarget`). |
| 3 | Live-retarget library clips in the preview. | The body flashed its reference pose (a weapon held horizontally) for one frame. |
| 4 | Never squash the release/recover clip to the cast time. Stretch only the wind-up. | A 0 s cast made animations last 0.12 s: "it plays too fast to see". |
| 5 | The camera never moves unless the user moves it. | Every Play reset the view the designer had set up. |
| 6 | Lists keep scroll/filter/focus. | The designer re-scrolled a 4,000-row list after every click. |
| 7 | Damage on the contact frame; every damage instance raises a moment. | Hits landed before the swing; DoT ticks and detonations had no visual slot. |
| 8 | Wind-up marks only at the target location. | A circle drawn under the caster for spells that land 20 m away. |
| 9 | Sizes are in-game sizes (divide by any global area scale). | The designer's "6 m" drew and hit 7.8 m. |
| 10 | "No cooldown" / "no cast time" means exactly 0. No minimum floor, no global rule rewrites it. | Rule tables quietly turned 0 s casts into 1–2 s casts. |
| 11 | Stack counts need a wide type (uint16+). | "Unlimited" stacks wrapped at 255. |
| 12 | Ground placement is never blocked by line of sight. Clamp to max range and trace only real walkable ground. | Red "invalid" areas where nothing was visible: hidden collision boxes, vendors' invisible capsules, the hero's own constructs. |
| 13 | Paragon bodies: turn off cloth simulation if its collision bodies include the weapons. Animate them through the creature/action layer, not a hidden fallback mesh. | Weapons "in pieces flailing around"; the browser played nothing on Paragon bodies. |
| 14 | Paragon retarget assets may reference a missing Rig class (`Orion_Proto_Retarget`); build your own IK Rigs. | Linker warnings, no retarget source. |
| 15 | Tripo / imported rigs with root scale 100: keep each bone's reference scale and validate the pelvis height. | IK retargets folded the legs (pelvis at 16 of 59 cm); imported clips played 100× too small. |
| 16 | Tripo rigged **GLB** exports are broken; use **FBX** for rigs. | Every vertex displaced; the model "all folded up". |
| 17 | Weapons and props are separate meshes on sockets/bones, never skinned to the body. | A hammer skinned 63% to a thumb smeared between hand and leg. |
| 18 | Defaults reproduce today's look exactly; suggestions are opt-in. | Feel sliders shipped with "nicer" defaults and changed every unit at once. |
| 19 | Old saved drafts load with new fields' defaults. | Publish crashed on the designer's real data after a field was added. |
| 20 | Tests whose fixtures write a value that the game now derives must set the source instead. | A HUD test set a status directly; the status engine mirrored it back to 0 and the test failed. |
| 21 | Generated HTML/JS for review pages: syntax-check the script (`node --check`). | A literal newline inside a JS string broke a review page; choices were silently lost. |
| 22 | Never let an agent read screenshots or model renders to verify; deliver them to the human with a gallery page and verify with logs/JSON. | Token cost and false confidence. |
| 23 | Licensed marketplace content (Fab, Paragon, packs) stays local: junction it into worktrees, never commit it. | Public repo exposure; worktrees missing assets. |
| 24 | Run heavy gates (editor builds, network/interface smokes) one at a time on a shared machine. | Out-of-memory and GPU "device removed" crashes produced false failures. |
| 25 | Respawning a Paragon body on top of itself can assert in GPU skinning (`FMorphVertexBufferPool`). Reuse the body when only the clip changes. | Rare preview crash. |

---

## 9. Testing (what "done" means)

- **Native tests** next to the existing ones: parse/validate; every moment spawns and ends with its owner; ordering;
  anchor placement numbers (ground z, head z); strip modes; `when` filtering; link placement and stretch; fit-to-area within 5%
  per shape; damage lands at the contact time, not at acceptance; cancelling before the contact; a DoT-then-detonation raising N
  ticks + 1 expire + impacts; no wind-up drawn under the caster for remote-landing abilities (expect 0 violations);
  status stack counts above 255.
- **Preview probes** (command-line flags, JSON output): each mode reports PASS/FAIL markers; live retarget plays a spell
  clip and a locomotion clip on several bodies for ≥ 90% of their length, the pelvis within 35% of rest height and the
  pose changing over time; the camera unchanged after 5 replays and mode switches.
- **Studio**: unit tests (store, validation, seeding idempotence, publish round trip, old-ledger publish), a headless
  browser self-test (DOM/text only, including the scroll-retention steps), and an end-to-end check against the real
  preview window (JSON only) that never touches the designer's real ledgers.
- **Multiplayer**: a network smoke where a remote client sees travel, drags and steered channels smoothly.

---

## 10. Build order (phases with acceptance checks)

1. **Data contract + runtime player** (3.1, 3.2, 4): parse, play every moment from server hooks, strip, statuses.
   *Accept:* native tests pass; a hand-written JSON entry changes one ability's look in a match.
2. **Preview window, cast mode** (5): real body, dummies, same player, status JSON, camera rule, hit-area outline.
   *Accept:* the cast probe passes; `spawned[]` matches the authored events.
3. **Studio MVP** (7): unit → ability → timeline, effect picker, slot editor, ledger, seed from the designer's notes,
   Publish with backup. *Accept:* unit tests, self-test and the e2e check pass; Publish round-trips.
4. **Contact-frame release + damage-moment audit** (4). *Accept:* the timing tests pass; every ability has its table row.
5. **Animation lane + UnitAnimation.json + live retarget + browser + check mode** (3.3, 5, 6). *Accept:* the retarget
   probe passes on every humanoid body; no silent fallback anywhere.
6. **Movement feel + movement course** (6). *Accept:* defaults reproduce the old pose; the course metrics are reported for
   every unit; suggestions are listed, not applied.
7. **Polish**: meshes as effects, fit to area per shape, empowered-only, links, candidate bodies, Run retarget now,
   scroll retention, keyboard. *Accept:* each item has a test.
8. **Live tactics moments** (3.2) and the timer simulation in the preview. *Accept:* the window/commit moments play in a
   match and in the preview.

Commit at the end of every phase, keep a RESUME document (done / not done / next / assumptions / gate logs), and stop to
ask the designer only for decisions that are genuinely theirs. For everything else, pick a sensible default, write it
down under "Assumptions", and continue.
