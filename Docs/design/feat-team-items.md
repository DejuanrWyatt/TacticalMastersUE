# Feature Spec: feat-team-items (and friendly fire)

Status: Draft  <!-- Draft | Approved (human only) | Superseded -->
Mode: Feature
Backlog goal: the human, 2026-10-01: "Once an item is picked up, user can assign it to a unit with an open
slot. Once the item is equipped to a unit, it cannot be removed unless the unit dies or uses a turn to
unequip. The items button should show each party member and the item they are wearing and item slots
available ... in 1 screen." And friendly fire as a match option.

## 1. Summary
Items a side's unit picks up go into that side's stash, not onto the unit. From the team items screen any
item in the stash goes on any of the side's units with an open slot, free, at any time. A worn item stays
on until its wearer falls (it goes back to the stash) or spends a whole turn taking it off.

## 2. Rules (TMSim)
1. `FBattle::Stash[2]`: each side's items, with each item's ability cooldown. Cleared at the start of a battle. In the checksum.
2. Walking onto items (`PickUpAt`): every item in the cache goes to the walker's side's stash. Free.
3. Take (a side's unit, within 1.5 m of a cache): that item to the stash. Free. A monster's Take is unchanged.
4. Equip (new order, `EOrderType::Equip`, text `equip <unit> <item> <slot>`): from the side's stash into an
   open slot of one of its units; slot -1 is the first open one. Any time, not tied to a turn (no serial).
   Refused: not a side's own unit, item not in the stash, already wears one, the slot is taken, no open slot,
   the battle is over.
5. Drop (unequip): a side's unit, on its turn, before it has moved or acted: the item goes to the stash and the
   turn ends (as a full turn). A monster's Drop is unchanged (on the ground, free).
6. A side's unit gone for good: what it wore goes to its side's stash (it used to fall on the ground).
   A knocked-out unit that can still be revived keeps its items.
7. Setup-screen loadouts (item points) are unchanged.

## 3. Friendly fire (`FTuning::FriendlyFire`, setup row "Friendly fire")
On: an area blow meant for the enemy (damage, aimed at enemies, aoe > 0 or a cone / line / charge / circle)
also hits the caster's own side standing in it. Never the caster, never a single-target blow, never a
monster's blow. The computer player counts its own side in a blast against the option (1.5 x the damage,
more for knocking one out).

## 4. The view
- Items: the ITEMS tile on the action bar, or Items in the top-right buttons, opens the team items screen:
  the stash (click an item), then each unit with its three slots (click an open slot to equip). Worn items
  show "Take off (turn)", lit only on that unit's turn before it moves or acts. Items within reach of the
  unit in hand can be picked up from the same screen.
- Online, any player on a side may equip any of its units; only a unit's own player takes items off it.

## 5. Protocol
9: friendly fire. 10: the stash and Equip.

## 6. Proof
`SimCampTest`: take into the stash with full slots; equip only into an open slot; unequip is the whole turn
and refused after moving; a fallen unit's items go to the stash; walking onto items stashes all of them;
equip survives the trip as text. `SimPlayTest`: friendly fire off and on, never the caster or a single
target. Every other test unchanged.
