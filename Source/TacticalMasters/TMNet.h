// The connections of an online match (Docs/design/feat-online.md, feat-lobby.md).
//
// Only the carrying lives here: TCP connections, messages sent as JSON objects
// with a 4-byte length in front, and who is still there. What the messages
// mean -- hello, the lobby, the draft, the battle, orders, checksums, chat --
// is the director's (TMBattleDirectorOnline.cpp, TMBattleDirectorLobby.cpp).
//
// A star: the host takes up to three players, and every player talks only to
// the host, which passes on what the others must hear. A player who joins has
// one connection, to the host, which it knows as peer 0.
//
// TCP is reliable and ordered by itself, so the router port a host opens is TCP,
// not UDP. Everything is non-blocking and polled once a frame from the
// director's Tick, so nothing here ever holds the game up waiting on the network.
//
// A player can also come through Epic Online Services (TMEos.h,
// Docs/design/feat-online-eos.md): the host offers a six-letter join code as
// well as its port, and a friend who types the code is connected through
// Epic's servers, relayed if the routers won't let the two PCs meet. Such a
// peer carries the same byte stream, cut into EOS packets, so the framing,
// Send/SendTo/Kick and everything after are the same for both kinds of peer.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FSocket;

class FTMNet
{
public:
	/** Bumped whenever the rules or the messages change: two builds that differ can't play each other. */
	static constexpr int32 ProtocolVersion = 27;  // 2: watchtowers (the capture order, three rule numbers); 3: items (loadouts, the item budget); 4: neutral camps (take and drop orders, seven ability slots); 5: the second set of statuses (element reactions); 6: walking onto items picks them up; 7: towers see their whole radius, nothing blocking; 8: the lobby (up to four players, sides chosen) and the draft; 9: friendly fire; 10: the team stash (items picked up go to the side, equipped from it, taking one off is a turn); 11: camp respawns a setup option, off by default; 12: walks by waypoints (the move order carries them); 13: Armor/Resist as a share, one Evasion that dodges or grazes; 14: zone of control (a 40th rule number), the Knight and Archer rebalanced; 15: camp noise, clean kills, boss wind-ups broken by stagger, the boss's hunt and claim (two rule numbers); 16: a walk can say which way to face on arrival (the move order's "f"); 17: movement skills (leap, behind), an ability's self status, the Ninja, Berserker and eight more classes reworked, summoners' pets and the 2026-10-02 class balance; 18: the 2026-10-03 class tuning pass (35 class files); 19: casters' signatures and hexers' status curses (eleven new abilities); 20: the held buffs and nerfs (seven class files); 21: the v19 play test (springs rest, tall grass, three rule numbers, map files with "grass"); 22: the v20 play test (a turn ended at a watchtower captures it; the lobby carries every setting and each unit's items, and the "item" message); 23: the v21 play test (casts four times as long, bosses' reach, a 46th rule number); 24: ground zones (twelve area denial abilities in twelve class files, three event kinds); 25: the unique and mobility spells (thirty abilities in thirty class files, eight statuses); 26: the ground's distances walked node to node with tile movement on (towers and camps placed again); 27: four codex picks with rules of their own (warned, ricochet, execute, crowd) and thirteen class files
	static constexpr int32 DefaultPort = 7777;
	/** Players a host takes besides itself: four in all. */
	static constexpr int32 MaxPeers = 3;
	/** A message longer than this closes the connection: nothing the game sends comes near it. */
	static constexpr int32 MaxMessageBytes = 4 * 1024 * 1024;

	~FTMNet();

	/** Starts listening for players. Returns "" or what went wrong. */
	FString Host(int32 Port);
	/** Starts connecting to a host. Returns "" or what went wrong (the connection itself is reported later). */
	FString Join(const FString& Address, int32 Port);
	/** Hosting (with or without a port): also offers the game by a join code, once Epic's sign-in is ready. */
	void OfferCode();
	/** Starts joining the game with this join code (TMEos.h). Returns "" or what went wrong (the rest comes later, as for Join). */
	FString JoinCode(const FString& Code);
	/** The join code: the one this host offers, once made, or the one this player joined with. */
	FString Code() const;
	/** Drops every connection and stops listening. */
	void Close();

	/** Accepts, connects, sends and reads. Once a frame. */
	void Poll(float DeltaSeconds);

	bool IsHost() const { return bHosting; }
	bool IsListening() const { return Listener != nullptr; }
	/** Joining: connected to the host. Hosting: at least one player connected. */
	bool IsConnected() const;
	/** Joining: still connecting to the host. */
	bool IsConnecting() const;
	/** Players connected to this host (or 1, the host, once a join has connected). */
	int32 PeerCount() const;

	/** Joining: a message for the host. Hosting: for every player. */
	void Send(const TSharedRef<FJsonObject>& Message);
	/** For one player (hosting) or the host (joining, peer 0). */
	void SendTo(int32 PeerId, const TSharedRef<FJsonObject>& Message);
	/** Sends one player a refusal with this reason, then drops them once it has gone. */
	void Kick(int32 PeerId, const FString& Reason);

	/** A message and who it came from (the host is peer 0 to a player who joined). */
	struct FArrival
	{
		int32 From = 0;
		TSharedPtr<FJsonObject> Message;
	};
	/** What arrived since the last look, oldest first. */
	TArray<FArrival> Received;
	/** Hosting: players who connected since the last look. */
	TArray<int32> Arrived;
	/** Joining: set once when the connection to the host is made. */
	bool bJustConnected = false;
	/** Hosting: players who went since the last look, and why. */
	TArray<TPair<int32, FString>> Departed;
	/** Joining: set once when the host goes, or the join fails; says why. */
	FString Lost;
	/** The join code's progress, for the screen: made, looking, found, or why there is none. Set when it changes. */
	FString CodeNews;

	/** Hosting: whether new players are let in (the lobby) or turned away (a battle). */
	bool bAccepting = true;
	/** What a player who connects while a battle is on, or the lobby is full, is told before being dropped. */
	FString BusyReason = TEXT("That host is already playing.");

private:
	FSocket* Listener = nullptr;
	bool bHosting = false;
	float ConnectingFor = 0.0f;
	int32 NextPeerId = 1;

	/** Through a join code: the host wants one made, or this player is joining with one. */
	bool bWantCode = false;
	bool bJoiningByCode = false;
	/** The join code being looked for (joining), until Epic's sign-in is ready to look. */
	FString WantedCode;
	/** The lobby's state last frame, to tell the screen when it changes. */
	uint8 LastLobby = 0;
	/** Hosting: the sign-in's failure was told once. */
	bool bCodeRefusalTold = false;

	struct FPeer
	{
		int32 Id = 0;
		/** A TCP peer's connection; null for a peer through Epic. */
		FSocket* Socket = nullptr;
		/** A peer through Epic: its Product User ID. */
		FString EosUser;
		bool bConnected = false;
		TArray<uint8> Outgoing;
		TArray<uint8> Incoming;
	};
	TArray<FPeer> Peers;

	/** Players turned away: their refusal is sent, then they are dropped. */
	struct FTurnedAway
	{
		FSocket* Socket = nullptr;
		FString EosUser;
		TArray<uint8> Outgoing;
		float Age = 0.0f;
	};
	TArray<FTurnedAway> TurnedAway;

	FPeer* FindPeer(int32 PeerId);
	int32 FindEosPeer(const FString& User) const;
	static void Frame(const TSharedRef<FJsonObject>& Message, TArray<uint8>& Into);
	/** Sends what it can; false if the connection is gone. */
	static bool Flush(FSocket* Socket, TArray<uint8>& Bytes);
	/** The same for a peer through Epic: the bytes cut into packets. */
	static bool FlushEos(const FString& User, TArray<uint8>& Bytes);
	/** Reads what has come; false if the connection is gone or sent nonsense (Why says which). */
	bool ReadPeer(FPeer& Peer, FString& Why);
	/** Whole messages out of what has come; false if it is nonsense. */
	bool TakeMessages(FPeer& Peer, FString& Why);
	void DropPeer(int32 Index, const FString& Why);
	void TurnAway(FSocket* Socket, const FString& EosUser, TArray<uint8>&& Pending, const FString& Reason);
	/** Hosting: a player through Epic has connected; let in, or turned away. Returns its index, or -1. */
	int32 AddEosPeer(const FString& User);
	/** The join code's lobby and connections, once a frame. */
	void PollCode(float DeltaSeconds);
	static void Destroy(FSocket*& Socket);
};
