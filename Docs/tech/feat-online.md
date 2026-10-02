# feat-online: how it is built

Status: Draft (2026-09-29). Rules and behaviour are in `Docs/design/feat-online.md`.

## Pieces
| Where | What |
|---|---|
| `TMSim/SimOrderText.h/.cpp` | `OrderToText` / `OrderFromText`: one order as a line of text, and back, exactly. Positions are `float`s written with 9 significant digits and rule numbers `double`s with 17, which is what it takes for every value to come back bit for bit. Reading is strict: unknown kinds, missing or extra fields, numbers that don't parse, and values out of range (a slot outside 0-3, negative ticks) are refused with a reason. Plain C++, so the standalone tests cover it. |
| `TacticalMasters/TMNet.h/.cpp` | `FTMNet`: the connection. One TCP socket (Unreal's `Sockets` module), non-blocking, polled from the director's `Tick`. Messages are JSON objects, each sent as a 4-byte length and then the text. A message over 4 MB closes the connection. |
| `TacticalMasters/TMUpnp.h/.cpp` | `FTMUpnp`: asks the router to forward the port. It finds the router (an SSDP search on UDP 239.255.255.250:1900), reads its description (HTTP), and asks its WAN connection service for `AddPortMapping` and `GetExternalIPAddress` (SOAP over HTTP). It runs in the background, says what happened in one line, and removes the mapping when hosting ends. |
| `TMBattleDirector` | The online match: its setup, the referee on the host, the inbox on the joiner, checksums, chat, rematch. |
| HUD | *Play Online* screen (address, port, Host, Join, status), the chat line, the online result panel. |

## Messages
Each is a JSON object with `"t"` saying what it is.

| t | From | Carries |
|---|---|---|
| `hello` | joiner | `version` |
| `refuse` | host | `reason`; the host closes after it |
| `start` | host | `team` (1), `seed`, `map` (id and file text), `rosters`, `classes` (id → class file text), `tuning` (key → value), `capture`, `limit`, `planning`, `theme` |
| `cmd` | host | `o`: an order as text (`OrderToText`) |
| `req` | joiner | `o`: an order as text |
| `reject` | host | `reason` |
| `sum` | host | `tick`, `value` (as a string: JSON numbers are doubles and a checksum is 64 bits) |
| `desync` | either | `detail` |
| `chat` | either | `text` (at most 120 characters) |
| `rematch` | either | nothing |

`PROTOCOL_VERSION` starts at 1 and goes up whenever the rules or the messages change.

## The director
- `Setup.Mode == "online"`, with `bHost` and `LocalTeam` (0 host, 1 joiner). `ComputerPlays` is false for
  both sides. `PlayerCanOrder` also needs the unit to be on `LocalTeam` and no request to be out.
- `Submit` splits three ways, as Godot's `_submit` (`battle.gd:400-412`):
  - Offline, as now.
  - The host checks the order, sends it as `cmd`, then applies it.
  - The joiner sends it as `req` (not Advance or Tune) and waits.
- `StepTicks` (time) runs only on the host.
- The joiner's inbox is applied in `Tick` before anything else, each order through `Validate` + `Apply`. A
  refusal is a split: `desync`.
- The host's requests go through `Validate` plus the referee's checks, then `cmd` + apply, or `reject`.
- Checksums: the host sends one each time the tick passes a multiple of 50. The joiner keeps them by tick
  and compares on reaching it.
- Both machines load classes and maps from the host's `start` through the same readers the files use
  (`TMSim::LoadClassFile`, `TMSim::ReadMapFile`), so a broken or hostile file is refused, not half-used.

## Test switches
- `-tmhost[=port]` hosts at once, with the setup as given (`-tmroster=`, `-tmmap=`, …).
- `-tmjoin=address[:port]` joins at once.
- `-tmnetbots` makes the computer play the local side through the online path.
- `-tmnetdesync` makes the joiner's game differ once, early, to prove a split is caught.

`scripts\online-test.bat` runs a host and a joiner with these, headless, and compares their results.
