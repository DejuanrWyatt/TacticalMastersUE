# Rules Spec: feat-status-effects

Status: Built 2026-09-30, from the research doc "Status Effects & Fab Environment Research"
(the human: "implement the status effects and design icons for each one and the existing ones").
Rules in `Source/TMSim` (tests: `Tests/SimStatusTest.cpp`), icons by `Tools/StatusIcons`.
Mode: Feature. Not Godot's: none of these statuses ever appears in a Godot battle, so the Godot
parity tests are untouched by them.

## 1. The sixteen

Durations are the holder's own turns (counted down as each begins), except Stop.

| Status | Kind | What it does |
| --- | --- | --- |
| Marked | harmful | The next hit that lands on it deals +30%, then the mark is spent. Monsters prefer Marked targets (+200 in PickQuarry). |
| Off-Balance | harmful | Its facing is locked (walking and aiming don't turn it); every hit counts as from behind (back bonus). Spent by the hit that lands. |
| Wet | harmful | Lightning stuns it and every Wet unit within 2 m (either side). Ice removes Wet and freezes it for 1 turn. Fire dries it. Burn arriving on it lasts half as long and dries it; Wet arriving puts Burn out. Embers underfoot dry it. |
| Oiled | harmful | Fire damage on it +50%. A fire hit ignites it: Burn for 2 turns doubled to 4, and the oil is used up. Any Burn arriving lasts twice as long. |
| Chilled | harmful | Gauge x0.8, move x0.8. Layers (Amount): three layers become Freeze for 1 turn. Fire and embers thaw it. |
| Haste | helpful | Gauge x1.5. Haste and Slow cancel: the one arriving removes the other and isn't added. |
| Stop | harmful | Gauge x0: it doesn't fill. Counted in ticks: 2 s per turn written (Amount), shown as seconds. A turn already earned isn't taken. |
| Suppressed | harmful | Its attacks miss 30% more. If it walks, the suppressor (By) hits it at once with its slot-0 ability, no roll; then the status ends. Hurting the suppressor frees everyone it suppressed. |
| Protect | helpful | Physical (Att-scale) damage taken x0.67. |
| Shell | helpful | Magic (Mag-scale) damage taken x0.67. |
| Guarded | helpful | A single-target damaging ability aimed at it lands on the guardian (By) instead, recomputed for the guardian, if the guardian is alive, on its side and within 3 m; farther, the status breaks. |
| Reraise | helpful | Knocked out, it stands up 3 s later with 25% max HP (not monsters). |
| Reflect | helpful | A single-target Mag ability from the other side that harms (damage, or a harmful status) bounces to the caster, recomputed from where it was aimed. Once. |
| Charmed | harmful | Fights for the charmer's side (Team changes, HomeTeam keeps its own) for its next turn, then goes home at that turn's end. Any damage, Immunity or a knock-out ends it at once. Sides' units only; bosses immune. The win, time and health checks count it for its own side. |
| Terrified | harmful | When its turn begins it first walks its full reach away from what it fears (By), then may act. |
| Decay | harmful | Healing on it deals that much damage instead; Regen and springs hurt it; undamaged mending stops. |

Unstoppable monsters (the bosses) also shrug off Stop, Charmed and Terrified.

## 2. Elements

An ability's element is its file's `"element"` (`fire`, `ice`, `lightning`, `water`, `none`),
else the words of its id (`flame`, `frost`, `thunder`, `tide`...; `ElementOf` in SimAbility.cpp),
else its status (Burn fire, Freeze and Chilled ice, Wet water). The reactions above happen
whenever a Wet, Oiled or Chilled unit is hit. With the **Elements** rule on (rule number
`elements`, the setup screen's row, offered on), water hits also leave their target Wet for 2
turns and ice hits add a layer of Chilled. Off, only abilities that name those statuses do.

## 3. Where they come from

- 16 items, one per status (`Content/Data/Items`): marking_dart,
  feint_gauntlet, waterskin, oil_flask, frost_charm, hastening_draught, hourglass_shard,
  suppressing_crossbow, warding_talisman, spellward_talisman, guardians_oath, rebirth_charm,
  mirror_ward, sirens_locket (epic), dread_mask, rot_censer).
- Monsters: Tar Skink spit Oils, Jungle Stalker pounce Marks, Dusk Archer bolt Suppresses,
  Qilin ward Protects, Crag Brute gains Roar (Terrified), Chronos' Stasis Stops, Helix Prime's
  last Prism Shield Reflects, Magma Colossus' Quake leaves Off-Balance.
- Any class file may use them (the class creator knows them, and the `element` key).

## 4. The view

- Every status has an icon, `Content/Data/Icons/statuses/<id>.png` (42, drawn by
  `Tools/StatusIcons/render.mjs` from `status_icons.mjs`; `sheet.png` is the contact sheet): a
  rounded square in the status's colour, a red rim for harmful and gold for helpful, one glyph.
  The HUD draws them in every status chip, with turns (Stop: seconds; Chilled: layers) on a tab.
- Glows and play rates on the body for Stop (held still), Charmed, Terrified, Marked, Decay,
  Reflect, Haste (faster), Chilled (slower), Protect, Shell.
- The log tells reactions ("is Wet: the lightning stuns it!"), guards, reflections, flights,
  Reraise and charms ending.
- Online: protocol 5; the host sends `elements`.
