// Epic Online Services: join codes and connections through Epic's servers
// (Docs/design/feat-online-eos.md).
//
// Three of Epic's free services, through the EOS SDK that ships with the engine
// (the EOSShared plugin starts the SDK; this starts the product's platform):
// - Connect sign-in with a Device ID: anonymous, no Epic account, no browser.
// - Lobbies: a host's game is an EOS lobby with a six-letter join code and the
//   online protocol on it; a friend finds it by the code.
// - P2P: reliable, ordered packets between the players, straight through when
//   the routers allow it and through Epic's relays when they don't.
//
// Nothing here knows what the game says: FTMNet (TMNet.cpp) frames its JSON
// messages into a byte stream as it does over TCP, and this carries the bytes.
// Users are their Product User IDs as text, so no EOS type leaves this file.
//
// The product's ids are read from Content/Data/Online/eos-ids.json, which is
// kept out of git (the client secret ships inside every build, as EOS intends
// for game clients, but there is no reason to publish it). Without the file,
// or in a build without the SDK, everything here says it is unavailable and
// online play by address still works.
//
// Everything is polled from the game thread: the SDK's callbacks, run by the
// engine's ticker on the game thread, only change state and queue events.

#pragma once

#include "CoreMinimal.h"

class FTMEos
{
public:
	static FTMEos& Get();

	// ------------------------------------------------ sign-in

	enum class EState : uint8 { Off, Unavailable, SigningIn, Ready, Failed };

	/** Starts the platform and signs in, once (again after a failure). Cheap to call every time the online screen opens. */
	void Start(const FString& DisplayName);
	EState GetState() const { return State; }
	bool IsReady() const { return State == EState::Ready; }
	/** One line for the online screen: ready, signing in, or why not. */
	FString StatusLine() const;

	// ------------------------------------------------ lobbies

	enum class ELobby : uint8 { None, Creating, Hosting, Searching, Joining, Joined, Failed };

	/** Hosting: makes a lobby for up to MaxMembers with a fresh join code. Watch LobbyState. */
	void HostLobby(int32 MaxMembers, int32 Protocol);
	/** Joining: looks for the lobby with this code and joins it. Watch LobbyState. */
	void JoinLobby(const FString& Code, int32 Protocol);
	/** Leaves (or, hosting, closes) the lobby, and forgets it. */
	void LeaveLobby();

	ELobby LobbyState() const { return Lobby; }
	/** The join code: the one made, hosting, or the one found, joining. */
	const FString& LobbyCode() const { return Code; }
	/** Why the lobby failed, for the player. */
	const FString& LobbyError() const { return Error; }
	/** Joined: the host's user. */
	const FString& LobbyHost() const { return HostUser; }
	/** The name the players' connections are made on: the code in it, so only those who know it are answered. */
	FString SocketName() const;

	/** Makes a code: six letters and digits, none that look alike (no 0/O, 1/I/L). */
	static FString MakeCode();
	/** A typed code as it is looked up: capitals, spaces and dashes gone. "" if it can't be one. */
	static FString CleanCode(const FString& Typed);

	// ------------------------------------------------ connections

	/** Opens (or keeps) a connection to this user on the lobby's socket. Joining: the host. */
	void Connect(const FString& User);
	/** Queues these bytes for the user, reliable and ordered. False if EOS's queue is full: try the rest again next frame. */
	bool Send(const FString& User, const uint8* Data, int32 Bytes, bool& bFailed);
	/** The next packet that has come for the game: who sent it, and its bytes. False when there are none. */
	bool Receive(FString& From, TArray<uint8>& Into);
	/** Closes the connection to one user. */
	void Disconnect(const FString& User);
	/** Closes every connection on the lobby's socket. */
	void DisconnectAll();

	/** Users whose connection was made since the last look. */
	TArray<FString> Established;
	/** Users whose connection closed since the last look, and why. */
	TArray<TPair<FString, FString>> Closed;
	/** Hosting: members who left the lobby. Joining: set when the host's lobby is gone. */
	TArray<FString> LeftLobby;
	bool bLobbyClosed = false;

	/** The largest packet EOS takes; a message is cut into packets this size. */
	static constexpr int32 MaxPacket = 1170;
	/** The packets of the game's stream go on this channel; a knock that opens a connection on another. */
	static constexpr uint8 StreamChannel = 0;
	static constexpr uint8 KnockChannel = 1;

private:
	FTMEos() = default;
	/** The SDK's callbacks (TMEos.cpp) write what follows. */
	friend struct FTMEosCallbacks;
	/** What only TMEos.cpp knows: the platform handle and the SDK's handles. */
	struct FImpl;
	FImpl* Impl = nullptr;

	/** Bumped by every new lobby: a callback for an older one does nothing but tidy up. */
	uint32 Generation = 0;
	EState State = EState::Off;
	FString StateWhy;
	ELobby Lobby = ELobby::None;
	FString Code;
	FString Error;
	FString HostUser;
	FString LobbyId;
	bool bLobbyOwner = false;
	int32 WantedProtocol = 0;
	FString LocalUser;
	FString Name;

	void Fail(const FString& Why);
	void LoggedIn(const FString& User);
	void ClearLobby();
	void Shutdown();
};
