# Replays

2026-10-02. The human asked for a replay system: save every battle and rewatch it ("Save + rewatch").
Mockup: "Battle Report Mockups", artboard 4.

## What a replay is

The rules are deterministic (SimOrder.h): the same start and the same orders give the same battle, tick for
tick, and every change to a battle goes through `ATMBattleDirector::Submit`, time passing included (an
Advance order). So a replay holds no positions or health -- only:

- the start: map and look, the eight classes and the three items each wore, every rule number
  (`TMSim::TuningKeys()`, as the battle started: Developer Tools, the setup, an online host's), the boss, the seed;
- the orders, in the order they were applied: `TMSim::OrderToText` lines, with runs of time joined up as `a N`;
- the battle's checksum at the start, at every whole minute of play, and at the end;
- marks for the timeline: falls, revives, towers taken, camps cleared, the result.

Recording starts in `BuildBattle` just before `Battle.Start` (`BeginRecording`) and every order `Submit` applies
is added (`RecordApplied`). When the battle is decided it is written to `Saved/Replays/<date>_<map>_<seed>.tmreplay`
(JSON, format `tactical-masters-replay` version 1); the newest 50 are kept. Online, each machine keeps its own.

## Watching

`WatchReplay` puts the replay's map and classes into the setup and builds the battle; `ApplyReplayStart` puts in
its rule numbers, items, boss and seed just before `Battle.Start`, the same point recording read them. Nobody plays:
both sides count as the computer's, the computer never thinks, and `Submit` refuses every order but the replay's
own. `AdvanceReplay` spends real time times the speed on the replay's time steps and applies the orders between
them as they come.

- Speed 1/2, 1, 2, 4 (keys 1-4); Space or P plays and pauses; Left and Right step to the previous or next order.
- The timeline jumps anywhere: forward by playing on quietly (rules only, no blows or effects shown), back by
  building the battle again and playing on quietly to the point.
- Fog: everything, or blue's or red's view.
- The battle's checksum is compared at every recorded minute and at the end. A replay made before the rules,
  classes, items or map changed says where it stops matching rather than showing a different battle quietly.

## Where

- Title screen: Replays (the list, newest first, eight a page; Watch and Delete).
- The end of a battle: Watch replay (the battle just played). The end of a replay: Watch again, Replays, Main menu.

## Proof

`-tmreplaycheck`: when an unattended battle is decided, its saved replay is watched to the end at once and
the log says `REPLAY CHECK: THE REPLAY IS THE SAME BATTLE` (same checksum, same tick) or `THE REPLAY DIFFERS`.
`Saved/agent-replay-check.bat` plays a computer battle with it.
