# Feature Spec: feat-lobby

Status: Draft  <!-- Draft | Approved (human only) | Superseded -->
Mode: Feature
Backlog goal: online play for up to four, sides chosen in a lobby, and a draft (the human, 2026-10-01).

## 1. Summary
Online, the host opens a lobby instead of starting a battle when someone connects. Up to four players
(the host and three who join) choose Blue or Red; each side's four units are shared out among its players.
The host can turn on a draft: the two sides ban and pick classes in turn, League of Legends style, and a
class banned or taken is gone for everyone. The rules (TMSim) are untouched: they still see two sides of
four units. Who orders which unit is the director's business.

## 2. The lobby
1. Host: Play Online -> Host Game -> the setup screen (map, rules) -> Host Game opens the lobby. The host is
   player 0, on Blue, and always counts as ready.
2. A joiner connects and says hello with this build's protocol version and its computer name. A different
   version is refused. A joiner arriving during a battle or a draft, or as a fifth player, is refused.
3. A new player goes on the side with fewer players (Blue when even). Anyone can Join Blue / Join Red;
   changing side un-readies a joined player.
4. Units: a side's players, in order of joining, share its four slots: one player orders 1-4, two order
   1-2 and 3-4, three order 1-2, 3 and 4 (`SlotOwner`: OnSide[Slot * n / 4]). A side nobody joins is
   played by the computer on the host, as the monsters are.
5. Without a draft, each player clicks their own units' slots to choose classes; the host also fills
   the computer's.
6. The host's Battle settings go back to the setup screen; Back to lobby returns. The host's Draft and
   Pick timer buttons set the draft. Everyone else sees the rules in one line.
7. Start (host) needs at least two players and every joined player ready. With the draft on, it starts the draft.

## 2a. Settings, items and Random (v20 play test, 2026-10-04)
- Every battle setting is on the lobby screen, three to a row: map, theme, duplicates, victory, time, planning,
  watchtowers, item points, camps, respawns, boss, elements, friendly fire, bosses hunt, claim the boss, draft,
  pick timer. The host clicks to change one and everyone is told; the others see the values. (The setup screen
  is still where the host starts.)
- Items: beside each unit its three item slots. A unit's own player buys its items from the side's points (the
  host buys the computer's), with the setup screen's item picker; the player asks the host (`item`: slot code
  team * 12 + unit * 3 + slot, item id, "" to empty), the host checks owner, points and the rest and tells
  everyone. The `lobby` message carries `settings` (label, value pairs), `item_budget` and `items` (24 ids).
- Random: the class picker's Random (of the role shown); Random classes in the lobby, a different class for each
  of the player's own units; Random pick / Random ban in the draft. Each is an ordinary pick, checked as one.

## 3. The draft
Order (`TMDraftOrder::Steps`), 14 steps:

| Phase | Steps |
|-------|-------|
| Bans 1 | Blue, Red, Blue, Red |
| Picks 1 | Blue, Red, Red, Blue (serpentine) |
| Bans 2 | Red, Blue |
| Picks 2 | Red, Blue, Blue, Red (serpentine) |

- A pick fills the side's next slot and is made by that slot's player; a ban by any player on the side.
- A computer side bans and picks after a short pause: a class of a role it lacks, else any.
- Pick timer: off, 20, 30 or 60 s (the host's choice). Out of time: a ban is let go, a pick is made for them.
- Unique per match: a class banned or picked can't be banned or picked again.
- The host keeps the draft and checks every choice; everyone sees the same board. When it is done the
  rosters are the draft's and the battle starts after 2.5 s.
- A player leaving during the draft sends everyone back to the lobby.

## 4. The battle
- The host's start message carries every player (name, side) and who orders each unit ("owners", by unit id).
  Each player is told which of the list they are ("you").
- A player may order only their own units (`RefereeCheck`: owner == player). The host's computer plays units
  nobody owns, and those of a player who leaves mid-battle; the others play on.
- Chat goes through the host, with the sender's name.
- After a battle the host takes everyone back to the lobby (R or the button); players who left are dropped.

## 5. Protocol
`FTMNet::ProtocolVersion` 8 (22 since the v20 play test: settings, items and `item` above). New messages: `lobby` (host -> each, with "you"), `side`, `ready`, `pick`
(player -> host), `left` (host -> all), `draft` (host -> all), `draft_choose` (player -> host).
`start` gains `players`, `owners` and per-player `you`; `team` is gone. `rematch` is gone.
Transport: a star of TCP connections (TMNet.cpp), the host taking up to three.

## 6. Not in this version: free-for-all (3-4 sides)
The rules know two sides. Free-for-all needs: a team number per player rather than per side (the monsters
and a draw both use 2 today, so they move), spawn areas and mirrored maps for three or four, win and time-out
rules for more than two (`Standing[2]`, `HealthShare`, the capture ring), the planning stage's Ready per side,
fog and sight per side, the computer player's enemy choice, every `1 - Team` in the rules and the view, the
turn order's two rows, team colours, and new Godot-free tests. Estimate: three to four weeks of work.

## 7. Proof
The online test (`Tests/OnlineTest.bat`, `-tmnetbots`): two games join, ready at once, draft by computer
choice and play; checksums agree. Hand test on two PCs: four players, both sides, draft on with a timer.
