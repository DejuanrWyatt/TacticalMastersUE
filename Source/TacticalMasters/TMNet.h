// The connection between two players in an online match (Docs/tech/feat-online.md).
//
// Only the carrying lives here: one TCP connection, messages sent as JSON
// objects with a 4-byte length in front, and whether the other player is
// still there. What the messages mean -- hello, the battle, orders, checksums,
// chat -- is the director's (TMBattleDirectorOnline.cpp), as it is in Godot's
// battle.gd over net.gd.
//
// Godot sends reliable, ordered messages over ENet. TCP is reliable and
// ordered by itself, so the router port a host opens is TCP, not UDP.
//
// Everything is non-blocking and polled once a frame from the director's
// Tick, so nothing here ever holds the game up waiting on the network.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FSocket;

class FTMNet
{
public:
	/** Bumped whenever the rules or the messages change: two builds that differ can't play each other. */
	static constexpr int32 ProtocolVersion = 7;  // 2: watchtowers (the capture order, three rule numbers); 3: items (loadouts, the item budget); 4: neutral camps (take and drop orders, seven ability slots); 5: the second set of statuses (element reactions); 6: walking onto items picks them up; 7: towers see their whole radius, nothing blocking
	static constexpr int32 DefaultPort = 7777;
	/** A message longer than this closes the connection: nothing the game sends comes near it. */
	static constexpr int32 MaxMessageBytes = 4 * 1024 * 1024;

	~FTMNet();

	/** Starts listening for one player. Returns "" or what went wrong. */
	FString Host(int32 Port);
	/** Starts connecting to a host. Returns "" or what went wrong (the connection itself is reported later). */
	FString Join(const FString& Address, int32 Port);
	/** Drops the connection and stops listening. */
	void Close();

	/** Accepts, connects, sends and reads. Once a frame. */
	void Poll(float DeltaSeconds);

	bool IsHost() const { return bHosting; }
	bool IsListening() const { return Listener != nullptr; }
	/** A player at the other end, connected. */
	bool IsConnected() const { return Peer != nullptr && bPeerConnected; }
	bool IsConnecting() const { return Peer != nullptr && !bPeerConnected; }

	/** Queues a message for the other player. */
	void Send(const TSharedRef<FJsonObject>& Message);
	/** Sends the other player a refusal with this reason, then drops them once it has gone. */
	void Kick(const FString& Reason);

	/** What arrived since the last look, oldest first. */
	TArray<TSharedPtr<FJsonObject>> Received;
	/** Set once when a player connects (the host) or the connection is made (the joiner). */
	bool bJustConnected = false;
	/** Set once when the other player goes, or a join fails; says why. */
	FString Lost;

	/** What a player who connects while another is playing is told, before being dropped. */
	FString BusyReason = TEXT("That host is already playing someone else.");

private:
	FSocket* Listener = nullptr;
	FSocket* Peer = nullptr;
	bool bHosting = false;
	bool bPeerConnected = false;
	float ConnectingFor = 0.0f;
	TArray<uint8> Outgoing;
	TArray<uint8> Incoming;

	/** Players turned away: their refusal is sent, then they are dropped. */
	struct FTurnedAway
	{
		FSocket* Socket = nullptr;
		TArray<uint8> Outgoing;
		float Age = 0.0f;
	};
	TArray<FTurnedAway> TurnedAway;

	static void Frame(const TSharedRef<FJsonObject>& Message, TArray<uint8>& Into);
	/** Sends what it can; false if the connection is gone. */
	static bool Flush(FSocket* Socket, TArray<uint8>& Bytes);
	void ReadPeer();
	void DropPeer(const FString& Why);
	static void Destroy(FSocket*& Socket);
};
