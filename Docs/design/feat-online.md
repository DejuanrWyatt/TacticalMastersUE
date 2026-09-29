# feat-online: two players over the internet

Status: Draft (2026-09-29). The human chose direct IP as Godot has it, and Godot's whole feature set, for the
first version. Built to this draft; approval still wanted.

After the Godot game's `scripts/autoload/net.gd` (all of it), `scripts/battle/battle.gd` (the online parts,
lines cited below) and `scripts/main_menu.gd:79-230`.

## How a match is played
1. **Host.** Title → *Play Online* → *Host*. The host chooses the map, both teams and the ways to win on the
   setup screen, then *Host Game* starts listening on the port (default **7777**). The screen shows the port,
   the host's addresses on the local network, and what happened when it tried to open the port on the
   router (UPnP; `net.gd:293-325`): opened, with the public address to give out, or "forward port 7777
   yourself, or use a VPN such as Tailscale".
2. **Join.** Title → *Play Online* → type the host's address (and port), *Join*. The joiner says hello with
   its protocol version (`net.gd:168-170`). The host refuses another version, with both numbers in the
   words, and turns away a second joiner while it is playing (`net.gd:173-196`).
3. **Start.** The host plays **Blue**, the joiner **Red** (`net.gd:192-201`). The host sends the battle: map
   (the map file itself, so a map only the host has still works), both rosters, the class files of every
   class in them (checked by the rules before they are used, `net.gd:239`), the rule numbers from Developer
   Tools, the ways to win, planning time and a fresh seed. Both start the same battle from it.
4. **Play.** Each side sees through its own units' eyes (fog: `battle.gd:141-142`) and orders only its own
   units. Units of the other side are *remote*.
5. **End.** The result screen offers *Rematch*: it starts once both have asked (`net.gd:139-143, 283-287`),
   same map and teams, new seed. *Main menu* leaves and closes the connection.

## The rules it keeps
- **The host is the referee** (`net.gd:2-8`, `battle.gd:398-412, 634-646`).
  - The host's own orders: checked, **sent to the joiner first**, then applied, so anything they set off is
    sent after them.
  - The joiner's orders: sent to the host as *requests*. The host checks each as it would its own, and also
    refuses: time passing and rule changes ("Only the host moves time forward or changes the rules."), and
    orders for a unit (or a Ready for a side) that is not the joiner's ("That isn't your unit."). A refusal
    goes back with its reason, which the joiner shows.
  - The joiner applies only what the host sends, in the order sent. If something the host sent is refused by
    the joiner's rules, the games have split: it says so and stops (`battle.gd:625-631`).
  - While a request is out, the joiner can't give another order (`waiting_for_host`, `battle.gd:295, 402`).
- **Only the host moves time** (`battle.gd:316-322`). Time passes on the host as Advance orders, sent like any
  other. The joiner's clock only ever moves when one arrives, so the two games are in step by construction.
- **Checksums** (`battle.gd:35, 650-671`). Every **50 ticks** (5 s of battle) the host sends the battle's
  checksum at that tick. The joiner compares it with its own when it reaches the same tick. A mismatch stops
  both games: "Out of sync: the two games no longer match at 12.5 s. The match can't continue."
- **Time can't stop online.** Pause does nothing, and the menu, Options and the Unit Guide don't pause
  (`battle.gd:1030-1044`). Developer Tools can't change the rules mid-match (`battle.gd:150-152, 1226`).
- **Chat** (`net.gd:127-131`, `battle.gd:732, 1214-1221`). A key (Enter by default) opens a line to type in;
  Enter sends, Esc closes. At most 120 characters. Lines show in the log as "You: …" and "Opponent: …".
- **The opponent leaves**: the battle stops with "Opponent disconnected" (`battle.gd:691-694`).
- The computer plays no side online.

## Not in this version
Lobbies, invites, matchmaking and relays (Epic Online Services or Steam), more than two players, spectators,
reconnecting to a match in progress, and replays of online matches. The game side (orders, checksums, the
host as referee) stays the same when any of these are added.

## Differences from Godot
- **TCP instead of ENet.** Godot sends reliable, ordered messages over ENet (UDP). Here they go over TCP,
  which is reliable and ordered by itself. So the router port to open is **TCP 7777**, not UDP.
- **Protocol.** The messages are this game's own. A Godot build and an Unreal build can't play each other.
- **Unit Guide stat changes** are not sent: the port has no Unit Guide stat editing yet.

## Tests
- `SimOrderTextTest` (no engine): every kind of order survives being written as text and read back, bit for
  bit; malformed text is refused, never half-read; and a whole AI battle replayed from its orders as text
  ends with the same checksum.
- `scripts\online-test.bat`: two copies of the game on one machine, no window. One hosts, one joins on
  127.0.0.1, and each side's computer plays that side through the online path (the joiner's orders as
  requests). They must finish the same battle with the same checksum and no refusals. A second run makes the
  joiner's game differ on purpose, and both must report out of sync.
