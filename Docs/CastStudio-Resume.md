# Cast Studio: where it stands

The plan is `Docs/CastStudio-Plan.md`; the blueprint it adapts is `Docs/CastStudio-Blueprint.md`. Newest first.

## Phase 2b: sound effects (2026-10-03)

### Done
- **Game, `Source/TMCast`.** `CastLooks.h/.cpp`: `EEffectKind::Sound` with `Sound`, `Volume`, `Pitch`; `EPart` and
  `Share` on any event (a part on a non-swing event is set aside); `FSwingTimes` and `SyncSeconds`; `FLook::HasSound`,
  `FLooksFile::Sounds`. Written back in the same key order the creator writes.
- **Game, the director.** `CastRaise` takes the swing's times and waits `SyncSeconds` for a swing event;
  `CastSwing` measures the wind-up (`LastLead`), the release clip and the picked recover on the caster's body;
  `CastSpawn` plays a sound at its anchor (or on a shot's carrier) at `volume × SfxVolume` and its pitch, and logs it.
  `SoundBlowStarts`/`SoundBlowLands` skip the default sound when the ability's look has a sound (the voice stays).
  `LookAssetPaths` preloads the looks' sounds.
- **Game, the sound studio.** `TMSoundStudio.h/.cpp`, asked for with `-tmsoundcatalog` (the branch is in
  `ATMBattleDirector::BeginPlay`): every SoundWave and SoundCue under /Game, each wave's imported audio written with
  `SerializeWaveFile` to `Saved/SoundCatalog/<pack path>.wav` (cut at 12 s), a cue heard as its first wave, and
  `catalog.json` (`tactical-masters-sound-catalog` v1: path, name, pack, kind, seconds, loops, file). It needs the
  editor's audio, so `Tools\SoundCatalog.bat` runs UnrealEditor-Cmd; a packaged game says so and writes nothing.
- **Tests.** `CastTest.cpp` `SoundsInTimeWithTheSwing`: reading and refusing, 0.9 s on one body and 0.8 s on another,
  a share past 1, the round trip, the sounds named.
- **Creator.**
  - `app/castlooks.mjs`: sound events normalised as the game reads them; `PARTS`, `swingParts`, `partSeconds`,
    `partAt`; `phaseTimes` gives the sketch's swing (lined up on its contact); `eventTimes` times swing parts.
  - `app/sound.mjs` (new): fetches and decodes the .wav files, plays them through Web Audio at volume and pitch, mute.
  - `app/castpreview.mjs`: plays each sound as the sketch passes it, and names what is heard.
  - `app/app.mjs`: panel 02 is now **Effects and sound**; its Sound effects section has the swing ruler (wind-up,
    release, recover, the contact, a ♪ per swing sound, dragged or clicked; letting go replays the swing), the list,
    Add a sound at seven moments, the sound browser (search, pack, ▶, Add/Use; 150 rows at most), and the sound
    editor (moment, timed to the swing with "At the contact" and how far through, volume, pitch, delay, heard from,
    when, on/side, note). Sounds stay out of the effects grid.
  - `app/runner.mjs` `soundCatalogue`, `soundFile`, `filmSounds`, `soundFilming`; `server.mjs` `GET /api/sound`,
    `POST /api/sound/film`, `GET /sound/<file>.wav`.
  - `tests/sound.test.mjs` (4 tests).

### Not done yet
- Not built in Unreal: `scripts\build.bat`, then `Tools\SoundCatalog.bat` (or Copy the sounds in the creator).
- A cue's random variations, modulators and attenuation are the game's alone; the creator plays its first wave.
- No limit on overlapping sounds yet: a look that sounds on every unit hit plays one per unit.

### Assumptions
- A sound authored for an ability replaces both of today's blow sounds (swing and landing), whatever the strip.
- The wind-up part is what plays before the release once the swing begins: nothing for a charged ability.
- Previews are cut at 12 seconds.

### Gate logs
- `g++ -std=c++17 -Wall -Wextra -Wshadow -Wconversion` on TMCast + CastTest: no new warnings; CastTest passes.
- A creator-written look with sounds read by TMCast: no problems, written back identically, timed as the creator times it.
- `node --test`: 122 pass, 1 fails (the existing test that reads `E:\` directly).
- A headless browser run against a fake sound catalogue: add on the swing, search, audition, drag along the ruler,
  At the contact, pitch; sounds heard in the sketch; no page errors. The sound studio itself is not compiled here.

## Phase 2: the effects timeline (2026-10-03)

### Done
- **Game, `Source/TMCast`.**
  - `CastLooks.h/.cpp` reads `Content/Data/CastStudio/AbilityLooks.json`: per ability a strip (`none · legacy · all`)
    and events; a `statuses` table (`statusGain · statusActive · statusTick · statusEnd`) and a `reactions` table;
    `footprints`. It answers which events play at a moment (`when`, `on`, `side`, in the order written), the area's
    width and centre (as the rules make it), the size and scale (fit to area, held 0.08-6 as today), anchors (sockets
    with today's points as the fallback, said as `fallback`), light radii and shake strengths. Anything wrong is set
    aside with a reason; a newer schema plays nothing.
  - `CastLegacy.h/.cpp` holds today's look: the effect table and the reading of an ability for its flavour, moved
    unchanged out of `TMBattleDirectorAbilityFx.cpp`, and `LegacyLook(ability, slot)`, the same look as events with
    strip all. Checked: all 536 abilities of the 101 classes and the monsters write out, go through the creator's
    import, and read back in the game identically.
- **Game, the director.** `TMBattleDirectorCast.cpp` (new):
  - `LoadCastLooks` re-reads the file when it changes (as a battle loads; its effects are loaded behind the loading bar).
  - Moments: `castStart` (CastStarted, or an instant ability's swing), `casting` (while casting or channelling),
    `swing`, `release` (before the shots leave), `projectile` (riding each shot; an unseen carrier if nothing flies
    today), `impact` (each unit touched, first of what was done to it), `area` (once), `target/ally/selfStatus` (from
    StatusApplied shown until the status is gone; held while a felling blow is in flight), `tick` (a status's Hit),
    `expire` (status gone, pet gone, channel over; before the frame's hits), `summon` (a pet's Teleported "pet").
    The statuses and reactions tables play for any source.
  - Spawning: particle effects at an anchor and offset turned by facing, scaled by footprint; followers moved every
    frame; lights (a pulse, or steady while a lasting moment lasts); shakes through `Jolt`; delays, durations, repeats;
    nothing for what fog hides; a spawn log (400 lines) for Film this ability.
  - Strip: `legacy` skips the class file's effect (user, point, targets); `all` also skips `LookCast`, `LookLand`
    (with `LookOn`), the shot's trail and glow, and hides the plain shot under an authored projectile effect.
- **Class lab.** `TMClassLab looks <class file>` prints `LOOKS {...}`: each ability's flavour, motion and today's look
  as events, and the footprints. `Tools\ClassLab\Build.bat` compiles TMCast's two files with it.
- **Tests.** `Tests/CastTest.cpp`: the creator's sample looks, setting aside, refusing, moments and order, the round
  trip, anchors, fit to area within 5% on every shape, shakes, and today's look (flare, shot, hits 1.4 times on a
  critical, area bursts at least 220 and at most 1400 cm, heals, buffs, blades, hammers, raises, the class file's
  effect, and every built-in ability reading back cleanly). `RunTests.bat` links every `Sim*.cpp` and TMCast.
- **Creator.**
  - `app/castlooks.mjs`: events and looks as the game reads them (same limits), editing, Import today's look,
    footprints, the looks file and its changes, sizes as the game works them out, the timeline's lanes and phases.
  - `app/castpreview.mjs`: the sketch, the caster and a target as filmed, effects drawn from their strips at the size
    the game plays them, on hands that follow the filmed tracks; a shot flies across; lights glow; shakes shake.
  - `app/caststudio.mjs`: the ledger keeps looks and footprints (old ledgers load); a look alone is a Draft;
    `allChanges` says which file each change is in.
  - `server.mjs`: Publish writes both files (the looks file only when there is something in it or the game has one);
    `POST /api/cast/legacy` asks the lab for today's look of the class as the screen has it.
  - `app/runner.mjs`: `publishedLooks`, `publishLooks` (backup, atomic), `legacyLooks`.
  - `app/app.mjs`: the **Effects timeline** panel: strip choice, Import today's look, Compare with today's look, the
    lane × phase grid with + in every cell, the sketch, the effect browser (filmed effects, a light, a shake), and the
    event editor (moment, where, size or Match area with "effect 310 cm vs area 450 cm → ×1.45", offset, turn, delay,
    how long, repeat, when, on/side, follow, colour and brightness, strength, note; up/down, Solo, Duplicate, Remove).
  - The effect sketch (panel 03) shows the class's filmed body, and Select model when the class has none.
  - `tests/castlooks.test.mjs` (10 tests).

### Not done yet
- Not built in Unreal: `scripts\build.bat`, then `scripts\test.bat`, then `Tools\ClassLab\Build.bat` (Import today's
  look needs the rebuilt lab).
- The statuses and reactions tables are read and played by the game but not authored in the creator yet.
- `proc` and `link` moments, StaticMesh effects, and the live-tactics moments are later phases (6 and 7).
- The sketch places area and status effects on the target's spot; it does not draw several targets.

### Next
- Phase 3, Film this ability (the spawn log is ready for it).

### Assumptions
- Today's flare plays at the swing, not at CastStarted: the new `swing` moment keeps an imported look's timing.
- The class file's "user" effect, imported, plays at the swing (today it plays as the rules resolve, before any walk).
- Effects in imported looks sit on today's fixed points (hands 115 cm up and 45 cm ahead), not on sockets, so they
  look as they did; a designer can move them to the hand socket.

### Gate logs
- `g++ -std=c++17` and `-std=c++20 -Wall -Wextra -Wshadow -Wconversion` on TMCast + CastTest: clean; CastTest passes.
- The class lab built here with TMCast; `looks` on all 101 class files and the monsters: every ability, valid JSON.
- `node --test`: 118 pass, 1 fails (the existing test that reads `E:\` directly).
- A headless browser run against a fake filming: Import today's look, select, Match area, offsets, add from a cell,
  Compare, Publish of both files; the body sketch and Select model. No page errors (the fake filming has no tracks).

## Phase 1: animations in the class creator (2026-10-03)

### Done
- **Game, `Source/TMCast` (new module).** It reads `Content/Data/CastStudio/AbilityAnimation.json` (`CastAnimation.h`
  has the format): for each ability, each animation set can have
  - a wind-up,
  - a release (clip and/or contact),
  - a loop,
  - a recover.

  The module is plain C++17 and depends on TMSim only for `SimJson`. Anything wrong in the file is set aside with a
  reason, and a newer schema plays nothing and says so. It is registered in the .uproject, both targets and
  `TacticalMasters.Build.cs`.
- **Game, the director.**
  - `LoadCastAnimation` re-reads the file whenever it changes, at each battle start.
  - `CastClipsFor` loads the picks for each unit's abilities in `ResetMotion`. A clip made for another skeleton is
    left out and logged once.
  - `AnimateEvents` plays the picks:
    - a picked wind-up replaces the intro at a cast's start;
    - for an instant ability the swing chains wind-up → release → recover (`FTMMotion::Then`, `QueuedThen` after a
      walk).
  - `StandingClip` loops the picked loop while casting or channelling.
  - `GatherBlows` lands the blow at `TMCast::ContactSeconds(lead, release length, contact)`.
  - With no file, the numbers are today's: the lead is 0, the contact is the set's or the usual, and a "none" motion
    still lands at once.
- **Game, filming (`TMAnimStudio`, catalogue version 2).**
  - 32 fps, laid out 16 frames to a row; thumbs keep every other frame in one row.
  - One fixed camera, written to the catalogue.
  - `<sheet>.tracks.json` per clip: hand_r, hand_l, head, weapon, root and pelvis per frame, in cm from the feet.
  - Each clip's root `drift` and `mostDrift`, and each motion's `impact`.
  - Each body's `skeleton` and `library` (every non-additive AnimSequence on its skeleton, from the asset registry).
  - The published picks are filmed too.
  - `-tmanimwanted=<file>` films only the listed `set path` lines into `catalog-wanted-<ticks>.json`
    (`Tools\AnimClips.bat` / `.ps1`).
  - `Tools\AnimCatalog.ps1` merges the new fields.
- **Tests.** `Tests/CastTest.cpp` is in `RunTests.bat`. It covers:
  - the sample the creator publishes;
  - setting aside what is wrong;
  - refusing what is not its own;
  - the contact maths;
  - the real published file, when there is one.
- **Creator (`E:\TacticsClassCreator`).**
  - `app/caststudio.mjs`:
    - the ledger, its normalisation (old ledgers load; pitfall 19) and the pick editing;
    - Publish's file (the same picks always give the same bytes), and what Publish would change;
    - today's clips, the swing as picked, the contact sum (the same as the game's);
    - each body's clips (its own skeleton only), unfilmed picks, the wearers;
    - projecting a point in cm onto a strip.
  - `server.mjs`:
    - `GET/PUT /api/cast` (revisions, 409 on a stale window, backups);
    - `POST /api/cast/publish[?dry=1]`;
    - `POST /api/anim/film-clips`;
    - `/anim/*.tracks.json`.
  - `app/runner.mjs`:
    - merges the `catalog-wanted-*` files;
    - `filmClips`;
    - `publishAnimation`, which backs up to `Saved/CastStudio/backups` and writes atomically.
  - `app/app.mjs`: the **Cast Studio** tab (it was "Animations & effects"):
    - each ability shows its state (Needs picks / Draft / Final);
    - the motion chips;
    - the swing preview, with a timeline, a contact tick and a flash on the contact frame;
    - wind-up, release, loop and recover rows with Choose / Back to today's;
    - the clip browser (search filtered in place; scroll kept);
    - the contact slider with that frame shown;
    - a drift warning;
    - Film the picked clips, and Publish with a confirm step;
    - keys J/K (ability), Space (play) and F (Final).
  - `app/anim.mjs` plays v1 and v2 strips.
  - `tests/caststudio.test.mjs` has 15 tests; the full suite passes except 3 tests that need this machine's D: and E:
    paths.

### Not done yet
- Nobody has built it in Unreal: `scripts\build.bat` (a new module, so UBT regenerates the project), then
  `scripts\test.bat`.
- Nobody has re-filmed: run `Tools\AnimCatalog.bat` once to get v2 catalogues (32 fps, tracks, libraries). Until
  then, the creator works from the old filming and says so.
- The tracks are written but are not drawn in the creator yet. They are for phase 2 (effects anchored to a hand)
  and phase 4 (the animation check).
- Play rate per pick (the blueprint's `rate`) is not in the format yet.

### Next
- Phase 2, the effects timeline (`AbilityLooks.json`, the runtime player, Import today's look, strip).

### Assumptions
- Picks are keyed by animation set (a hero and its skins share one), not by body or class.
- A Final ability is published just like a Draft: Final is the designer's word, not a gate.
- An ultimate's `<motion>_ult` clips are still used where nothing is picked; a picked release replaces them.
- The base font stays the creator's 14 px rather than the blueprint's 15 px, to match the rest of the tool.

### Gate logs
- `g++ -std=c++17 -Wall -Wextra -Wshadow -Wconversion` on TMCast + CastTest: clean; `CastTest` passes, and reads the
  creator's own Publish output.
- `node --test`: 108 pass, 3 fail (the existing tests that read `D:\ProgramsByMe` and `E:\` directly), 10 skipped
  (the class lab is not built here).
- A headless browser run of the tab against a fake filming: the browser filters in place and keeps its search, a
  pick and a contact are saved, F marks Final, J/K step through the abilities, and Publish asks, then writes. There
  were no page errors.
