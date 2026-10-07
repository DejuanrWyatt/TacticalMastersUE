# feat-online-eos: playing a friend over the internet with Epic Online Services

Status: Built, not yet compiled on the PC (2026-10-06): see section 9. Asked for by the human after "what are some
options to play multiplayer with a friend over the internet", then "scope out epic online services"; the human made
the Epic product and sent its ids the same morning. Builds on feat-online.md, whose game side (the host as referee,
orders, checksums, the lobby and draft) does not change at all.

## 1. What the players get
- **Host.** Title -> *Play Online* -> *Host*. Besides today's port and addresses, the screen shows a **join code**,
  six letters (say `KRAT7Q`), and a *Copy* button. The host sends the code to friends any way they like.
- **Join.** Title -> *Play Online* -> type the code -> *Join*. No address, no port, no router settings, no VPN.
- It works behind any home router, on mobile hotspots and behind carrier-grade NAT: when the two PCs can't reach
  each other directly, Epic's relay servers carry the traffic. For a turn-based game the relay's extra delay
  (tens of milliseconds) is not felt.
- Nobody needs an Epic Games account or the Epic launcher. Each PC is signed in anonymously by the game itself.
- Direct IP stays, under *Advanced*, as it is today (and for LAN parties and the two-copies-on-one-PC test).
- Up to four players, as the lobby already allows. The protocol version check stays: different builds still
  can't play each other.

## 2. What EOS is and costs
Epic Online Services is Epic's free set of online services for games on any store or engine: sign-in, lobbies,
peer-to-peer connections with NAT traversal and relays, voice, achievements and more. The services this needs --
Connect sign-in, Lobbies and P2P -- are free, with no per-player charge, and the game does not have to ship on the
Epic store. The EOS SDK already ships inside Unreal Engine 5.8 (the engine's EOSShared plugin), so nothing extra
is downloaded or licensed.

## 3. How it fits the game
Today's online code is already in two layers, which is what makes this cheap:
- `FTMNet` (TMNet.cpp, ~350 lines) only carries messages: JSON objects with a length in front, over TCP, in a
  star (every player talks to the host, peer 0). It knows nothing about what the messages mean.
- The director (TMBattleDirectorOnline.cpp, TMBattleDirectorLobby.cpp) gives the messages their meaning: hello and
  version, the lobby, the draft, orders, checksums, chat.

EOS replaces only the carrying. The plan:
1. **A transport seam in `FTMNet`.** The connection kept behind a small interface -- open, send bytes to a peer,
   read bytes, who arrived, who left -- with two implementations: today's TCP one, unchanged, and an EOS one.
   The framing, `Send`/`SendTo`/`Kick`, `Received`/`Arrived`/`Departed`/`Lost` and everything the director uses
   stay exactly as they are, so not one line of the lobby, draft or battle code changes.
2. **Sign-in (`TMEos.cpp`, new).** Starts the EOS platform with the product's ids, then signs this PC in with the
   Connect interface using a **Device ID** credential: an anonymous id made for this PC the first time, kept by
   EOS, which gives the player an EOS user id (a ProductUserId) with no account and no browser window. A display
   name (the one the lobby already asks for) goes with it. Done once, in the background, when *Play Online* opens.
3. **The lobby (EOS Lobbies interface).** *Host* creates an EOS lobby, at most four members, not listed publicly,
   with the join code as a searchable attribute and the protocol version as another. *Join* searches for the
   code, checks the version before connecting (so a mismatched build is told at once, without a connection),
   and joins the lobby. The lobby is only a meeting place; the game's own lobby screen goes on as today.
4. **The connection (EOS P2P interface).** The host accepts P2P connection requests on the game's socket name,
   only from members of its lobby. Messages go **reliable and ordered**, with relays allowed (EOS tries a direct
   path first and falls back to a relay). EOS packets are small (about 1.1 KB each), so the transport cuts each
   framed message into packets and the other side joins them up again -- simple, because reliable-ordered means
   they arrive whole and in order. The battle's opening message (map file, class files) is the only big one, a
   few hundred kilobytes at most.
5. **Leaving.** A peer that leaves the lobby or whose P2P connection closes is reported through `Departed` /
   `Lost`, so "Opponent disconnected" works as today. The host closing the lobby ends it for everyone.

## 4. What the human does (I can't make accounts)
1. Sign in at dev.epicgames.com (a free Epic account for the developer only; players never need one) and make an
   organisation and a product, "Tactical Masters".
2. In the product's settings: note the **Product ID, Sandbox ID and Deployment ID**; make a **client** with a
   client policy that allows Connect sign-in, Lobbies and P2P (the "peer-to-peer" style policy), and note its
   **Client ID and Client Secret**. Epic Account Services are not needed.
3. Paste the five values to me (they went in `Content/Data/Online/eos-ids.json`, kept out of git; the client secret
   ships in every game build, which is how EOS is meant to be used for clients, so it is not a password to guard,
   but there is no reason to publish it with the code either).
About fifteen minutes. Epic may ask for an organisation name and to accept its developer terms.

## 5. The work, in order
| Step | What | Size |
|---|---|---|
| 1 | Enable EOSShared, start the platform from the ids, Device ID sign-in, a status line on the Online screen ("Online services: ready") | half a day |
| 2 | Lobbies: create with a join code, search and join by code, version attribute, leave | a day |
| 3 | The transport seam in `FTMNet`; the EOS transport over P2P (packets in and out, joining them up, connection requests only from lobby members, closed connections) | one to two days |
| 4 | Online screen: the code and *Copy* for the host, a code box for the joiner, direct IP under *Advanced*; status and error words ("No game with that code", "That host is on another version") | half a day |
| 5 | Packaging: the EOS SDK's DLL staged with the build, config checked in a packaged run; the release script unchanged otherwise | a few hours |
| 6 | Tests: an EOS-off build plays exactly as today (all rules tests, `online-test.bat`); two copies on one PC through EOS; then you and a friend over the internet | a session with the friend |
About four to six working sessions in all. Nothing in the rules (TMSim) changes, and the online protocol only
changes if the hello needs the join code (it doesn't, as planned).

## 6. Risks and how they're handled
- **Device ID on PC.** Epic describes Device ID as an anonymous sign-in, mainly for mobile and for trying a game
  before linking an account. It does work on Windows; if Epic's terms or a later SDK limit it on PC, the fallback is
  signing in with an Epic account through the browser (the AccountPortal login the engine plugin supports), which
  costs players one click. To confirm at step 1.
- **Two copies on one PC.** Both would get the same Device ID. For testing, the second copy uses a different
  device model name (or Epic's developer sign-in tool), behind a command-line switch only. To confirm at step 1.
- **Packet and queue sizes.** The P2P packet limit and the outgoing queue size are checked against the SDK headers
  at step 3 (the cutting-up is sized from the SDK's own constant, not a guess).
- **A relay's delay.** Irrelevant to a turn-based game; checksums and the host-as-referee rule already keep the two
  games in step whatever the delay.
- **Epic's services down.** Direct IP still works.

## 7. Not in this scope
Matchmaking with strangers, a public game browser, friends lists and invites through Epic, voice chat, reconnecting
to a match in progress, Steam. Each could come later on the same pieces (a public lobby list is a small step from
step 2).

## 8. Open questions for the human (answered 2026-10-06)
- Keep direct IP visible, or only under *Advanced*? **Under Advanced.**
- The join code: six letters and digits, no look-alikes? **Six letters** (and digits: never 0, O, 1, I or L).
- Show the game's lobby in a public list for anyone with the same build? **No: invite by code only, for now.**

## 9. Built (2026-10-06, the lobby and items session)
Steps 1 to 4 and the scripts for 5 and 6; not yet compiled on the PC. The EOS code was checked here against UE 5.8's
own SDK headers (EOS SDK 1.19.1 headers and the EOSShared plugin, read from the engine) with g++ and a small stand-in for
the engine; the director and HUD edits were checked by reading.
- `Source/TacticalMasters/TMEos.{h,cpp}` (new): the platform started through the engine's `IEOSSDKManager` from
  `Content/Data/Online/eos-ids.json` (no overlay, no voice; its cache in `Saved/EOS`); Device ID sign-in through
  Connect (made once per PC, a user made the first time, refreshed when the hour runs out); a lobby per hosted game,
  advertised but never listed, with `CODE` and `PROTOCOL` attributes, four members, no host migration; a search by
  `CODE`, the protocol checked before joining ("That game is on another build"); P2P on the socket `TM<code>`, reliable
  and ordered, relays allowed, packets of `EOS_P2P_MAX_PACKET_SIZE` (1170 bytes); a connection request answered only
  on our socket, and joining, only from the host. Without the ids file, or in a build without the SDK, it says join
  codes aren't available and direct IP works as before.
- `TMNet.{h,cpp}`: a peer is a TCP socket or an EOS user; both carry the same framed byte stream, so everything above
  it is unchanged. `OfferCode()` (the host offers a code beside its port), `JoinCode()`, `Code()`, `CodeNews`. A host
  whose port is taken still hosts by code.
- The Online screen: Host Game, and a join code box with Join; the sign-in's state on one line; direct IP behind
  "Advanced: direct IP". The lobby shows the code at the top right with Copy. The setup screen's Host Game says
  "a join code, and port 7777".
- Switches: `-tmeos` (sign in at start, so `-tmhost` offers a code), `-tmcode=ABCDEF` (that code, for tests),
  `-tmjoincode=ABCDEF`, `-tmeosuser=2` (a second copy signs in as another device).
- Build: the EOSShared plugin in the .uproject; EOSShared and EOSSDK in TacticalMasters.Build.cs (Win64); the SDK's
  DLL is staged by the engine's EOSSDK module. Online protocol unchanged (27): no message changed.
- `E:\Builds\agent-eos-check.bat`: builds, runs the tests, then `eos-test.ps1`: two headless copies on this PC, one
  hosting with TMTEST, one joining by it as a second device, the computer playing a battle through Epic.
- Still to learn on the PC: whether `-tmeosuser=2` really makes a second Device ID on one Windows user (if not, the
  two copies get one user and the local test can't connect; a real test with a friend is unaffected), and how long a
  relayed connection takes to open.
