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

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FSocket;

class FTMNet
{
public:
	/** Bumped whenever the rules or the messages change: two builds that differ can't play each other. */
	static constexpr int32 ProtocolVersion = 14;  // 2: watchtowers (the capture order, three rule numbers); 3: items (loadouts, the item budget); 4: neutral camps (take and drop orders, seven ability slots); 5: the second set of statuses (element reactions); 6: walking onto items picks them up; 7: towers see their whole radius, nothing blocking; 8: the lobby (up to four players, sides chosen) and the draft; 9: friendly fire; 10: the team stash (items picked up go to the side, equipped from it, taking one off is a turn); 11: camp respawns a setup option, off by default; 12: walks by waypoints (the move order carries them); 13: Armor/Resist as a share, one Evasion that dodges or grazes; 14: zone of control (a 40th rule number), the Knight and Archer rebalanced
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

	/** Hosting: whether new players are let in (the lobby) or turned away (a battle). */
	bool bAccepting = true;
	/** What a player who connects while a battle is on, or the lobby is full, is told before being dropped. */
	FString BusyReason = TEXT("That host is already playing.");

private:
	FSocket* Listener = nullptr;
	bool bHosting = false;
	float ConnectingFor = 0.0f;
	int32 NextPeerId = 1;

	struct FPeer
	{
		int32 Id = 0;
		FSocket* Socket = nullptr;
		bool bConnected = false;
		TArray<uint8> Outgoing;
		TArray<uint8> Incoming;
	};
	TArray<FPeer> Peers;

	/** Players turned away: their refusal is sent, then they are dropped. */
	struct FTurnedAway
	{
		FSocket* Socket = nullptr;
		TArray<uint8> Outgoing;
		float Age = 0.0f;
	};
	TArray<FTurnedAway> TurnedAway;

	FPeer* FindPeer(int32 PeerId);
	static void Frame(const TSharedRef<FJsonObject>& Message, TArray<uint8>& Into);
	/** Sends what it can; false if the connection is gone. */
	static bool Flush(FSocket* Socket, TArray<uint8>& Bytes);
	/** Reads what has come; false if the connection is gone or sent nonsense (Why says which). */
	bool ReadPeer(FPeer& Peer, FString& Why);
	void DropPeer(int32 Index, const FString& Why);
	void TurnAway(FSocket* Socket, TArray<uint8>&& Pending, const FString& Reason);
	static void Destroy(FSocket*& Socket);
};
