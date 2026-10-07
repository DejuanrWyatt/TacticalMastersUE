#include "TMNet.h"

#include "TMEos.h"

#include "Common/TcpSocketBuilder.h"
#include "IPAddress.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace TMNetPrivate
{
	/** How long a join may take to connect before it is given up (Godot's ENet waits about as long). */
	constexpr float ConnectSeconds = 10.0f;
	/** Through Epic, finding a way between two routers (and a relay if there is none) takes longer. */
	constexpr float CodeConnectSeconds = 25.0f;
	/** How long a refusal has to go out before the connection is closed: longer through Epic, which queues it. */
	constexpr float RefusalSeconds = 0.5f;
	constexpr float EosRefusalSeconds = 1.5f;
	const TCHAR* const Garbled = TEXT("The other game sent something it shouldn't have, so the connection was closed.");
}

FTMNet::~FTMNet()
{
	Close();
}

void FTMNet::Destroy(FSocket*& Socket)
{
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void FTMNet::Close()
{
	for (FPeer& Peer : Peers)
	{
		Destroy(Peer.Socket);
	}
	Peers.Reset();
	Destroy(Listener);
	for (FTurnedAway& Away : TurnedAway)
	{
		Destroy(Away.Socket);
	}
	TurnedAway.Reset();
	if (bWantCode || bJoiningByCode)
	{
		// Every connection on the code's socket, then the lobby (closed for everyone, hosting).
		FTMEos::Get().DisconnectAll();
		FTMEos::Get().LeaveLobby();
	}
	bWantCode = false;
	bJoiningByCode = false;
	WantedCode.Reset();
	LastLobby = 0;
	bCodeRefusalTold = false;
	CodeNews.Reset();
	bHosting = false;
	Received.Reset();
	Arrived.Reset();
	Departed.Reset();
	bJustConnected = false;
	Lost.Reset();
}

bool FTMNet::IsConnected() const
{
	for (const FPeer& Peer : Peers)
	{
		if (Peer.bConnected)
		{
			return true;
		}
	}
	return false;
}

bool FTMNet::IsConnecting() const
{
	if (bJoiningByCode && Peers.Num() == 0)
	{
		return Lost.IsEmpty();  // still finding or joining the game
	}
	return !bHosting && Peers.Num() > 0 && !Peers[0].bConnected;
}

int32 FTMNet::PeerCount() const
{
	int32 Count = 0;
	for (const FPeer& Peer : Peers)
	{
		Count += Peer.bConnected ? 1 : 0;
	}
	return Count;
}

FString FTMNet::Host(int32 Port)
{
	Close();
	Listener = FTcpSocketBuilder(TEXT("TacticalMastersHost"))
		.AsReusable()
		.AsNonBlocking()
		.BoundToPort(Port)
		.Listening(MaxPeers + 2)
		.Build();
	if (!Listener)
	{
		return FString::Printf(TEXT("Couldn't host on port %d: is something else using it?"), Port);
	}
	bHosting = true;
	bAccepting = true;
	return FString();
}

FString FTMNet::Join(const FString& Address, int32 Port)
{
	Close();
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	// A name (a computer on the network, a domain) or a number: both resolve here.
	const FAddressInfoResult Found = Sockets->GetAddressInfo(*Address, nullptr, EAddressInfoFlags::Default, NAME_None, ESocketType::SOCKTYPE_Streaming);
	if (Found.ReturnCode != SE_NO_ERROR || Found.Results.Num() == 0)
	{
		return FString::Printf(TEXT("Couldn't find %s: check the address."), *Address);
	}
	const TSharedRef<FInternetAddr> Where = Found.Results[0].Address;
	Where->SetPort(Port);
	FPeer HostPeer;
	HostPeer.Id = 0;
	HostPeer.Socket = FTcpSocketBuilder(TEXT("TacticalMastersJoin")).AsNonBlocking().Build();
	if (!HostPeer.Socket)
	{
		return TEXT("Couldn't open a connection.");
	}
	HostPeer.Socket->SetNoDelay(true);
	// Non-blocking, so this only starts it; Poll sees it through.
	HostPeer.Socket->Connect(*Where);
	Peers.Add(MoveTemp(HostPeer));
	ConnectingFor = 0.0f;
	return FString();
}

void FTMNet::OfferCode()
{
	// Hosting even if the port couldn't be opened: the code is enough.
	bHosting = true;
	bWantCode = true;
	bCodeRefusalTold = false;
}

FString FTMNet::JoinCode(const FString& Typed)
{
	Close();
	const FString Clean = FTMEos::CleanCode(Typed);
	if (Clean.IsEmpty())
	{
		return TEXT("A join code is six letters and digits (never 0, O, 1, I or L), as the host's lobby shows it.");
	}
	const FTMEos::EState State = FTMEos::Get().GetState();
	if (State == FTMEos::EState::Unavailable || State == FTMEos::EState::Failed || State == FTMEos::EState::Off)
	{
		return FTMEos::Get().StatusLine();
	}
	bJoiningByCode = true;
	WantedCode = Clean;
	ConnectingFor = 0.0f;
	return FString();
}

FString FTMNet::Code() const
{
	const FTMEos& Eos = FTMEos::Get();
	if ((bWantCode && Eos.LobbyState() == FTMEos::ELobby::Hosting) || (bJoiningByCode && !Eos.LobbyCode().IsEmpty()))
	{
		return Eos.LobbyCode();
	}
	return bJoiningByCode ? WantedCode : FString();
}

int32 FTMNet::FindEosPeer(const FString& User) const
{
	for (int32 i = 0; i < Peers.Num(); ++i)
	{
		if (!Peers[i].EosUser.IsEmpty() && Peers[i].EosUser == User)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FTMNet::FPeer* FTMNet::FindPeer(int32 PeerId)
{
	for (FPeer& Peer : Peers)
	{
		if (Peer.Id == PeerId)
		{
			return &Peer;
		}
	}
	return nullptr;
}

void FTMNet::Send(const TSharedRef<FJsonObject>& Message)
{
	for (FPeer& Peer : Peers)
	{
		if (Peer.bConnected)
		{
			Frame(Message, Peer.Outgoing);
		}
	}
}

void FTMNet::SendTo(int32 PeerId, const TSharedRef<FJsonObject>& Message)
{
	if (FPeer* Peer = FindPeer(PeerId))
	{
		Frame(Message, Peer->Outgoing);
	}
}

void FTMNet::TurnAway(FSocket* Socket, const FString& EosUser, TArray<uint8>&& Pending, const FString& Reason)
{
	FTurnedAway Away;
	Away.Socket = Socket;
	Away.EosUser = EosUser;
	Away.Outgoing = MoveTemp(Pending);
	TSharedRef<FJsonObject> Refusal = MakeShared<FJsonObject>();
	Refusal->SetStringField(TEXT("t"), TEXT("refuse"));
	Refusal->SetStringField(TEXT("reason"), Reason);
	Frame(Refusal, Away.Outgoing);
	TurnedAway.Add(MoveTemp(Away));
}

void FTMNet::Kick(int32 PeerId, const FString& Reason)
{
	for (int32 i = 0; i < Peers.Num(); ++i)
	{
		if (Peers[i].Id == PeerId)
		{
			TurnAway(Peers[i].Socket, Peers[i].EosUser, MoveTemp(Peers[i].Outgoing), Reason);
			Peers.RemoveAt(i);
			return;
		}
	}
}

void FTMNet::Frame(const TSharedRef<FJsonObject>& Message, TArray<uint8>& Into)
{
	FString Text;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Message, Writer);
	const FTCHARToUTF8 Utf8(*Text);
	const uint32 Length = static_cast<uint32>(Utf8.Length());
	// The length first, most significant byte first, then the text.
	Into.Add(static_cast<uint8>(Length >> 24));
	Into.Add(static_cast<uint8>(Length >> 16));
	Into.Add(static_cast<uint8>(Length >> 8));
	Into.Add(static_cast<uint8>(Length));
	Into.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

bool FTMNet::Flush(FSocket* Socket, TArray<uint8>& Bytes)
{
	while (Socket && Bytes.Num() > 0)
	{
		int32 Sent = 0;
		if (!Socket->Send(Bytes.GetData(), Bytes.Num(), Sent))
		{
			const ESocketErrors Error = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
			return Error == SE_EWOULDBLOCK || Error == SE_NO_ERROR;
		}
		if (Sent <= 0)
		{
			return true;  // the connection's buffer is full: the rest next frame
		}
		Bytes.RemoveAt(0, Sent, EAllowShrinking::No);
	}
	return true;
}

bool FTMNet::FlushEos(const FString& User, TArray<uint8>& Bytes)
{
	// Packets of at most EOS's size, as many as its queue takes; the rest next frame.
	int32 Sent = 0;
	bool bFailed = false;
	while (Sent < Bytes.Num())
	{
		const int32 Size = FMath::Min(FTMEos::MaxPacket, Bytes.Num() - Sent);
		if (!FTMEos::Get().Send(User, Bytes.GetData() + Sent, Size, bFailed))
		{
			break;
		}
		Sent += Size;
	}
	if (Sent > 0)
	{
		Bytes.RemoveAt(0, Sent, EAllowShrinking::No);
	}
	return !bFailed;
}

void FTMNet::DropPeer(int32 Index, const FString& Why)
{
	if (!Peers.IsValidIndex(Index))
	{
		return;
	}
	const int32 Id = Peers[Index].Id;
	Destroy(Peers[Index].Socket);
	if (!Peers[Index].EosUser.IsEmpty())
	{
		FTMEos::Get().Disconnect(Peers[Index].EosUser);
	}
	Peers.RemoveAt(Index);
	if (bHosting)
	{
		Departed.Add({ Id, Why });
	}
	else
	{
		Lost = Why;
	}
}

bool FTMNet::ReadPeer(FPeer& Peer, FString& Why)
{
	if (!Peer.Socket)
	{
		// Through Epic: PollCode has put its packets into Incoming already.
		return TakeMessages(Peer, Why);
	}
	uint8 Buffer[16384];
	for (;;)
	{
		// For a stream, Recv says false only when the other end has closed (or
		// failed); true with nothing read is just nothing yet (SocketsBSD.cpp).
		int32 Read = 0;
		if (!Peer.Socket->Recv(Buffer, sizeof(Buffer), Read))
		{
			Why = bHosting ? TEXT("A player disconnected.") : TEXT("The host disconnected.");
			return false;
		}
		if (Read == 0)
		{
			break;
		}
		Peer.Incoming.Append(Buffer, Read);
	}
	return TakeMessages(Peer, Why);
}

bool FTMNet::TakeMessages(FPeer& Peer, FString& Why)
{
	// Whole messages out of what has arrived.
	while (Peer.Incoming.Num() >= 4)
	{
		const uint32 Length = (static_cast<uint32>(Peer.Incoming[0]) << 24) | (static_cast<uint32>(Peer.Incoming[1]) << 16)
			| (static_cast<uint32>(Peer.Incoming[2]) << 8) | static_cast<uint32>(Peer.Incoming[3]);
		if (Length > static_cast<uint32>(MaxMessageBytes))
		{
			Why = TMNetPrivate::Garbled;
			return false;
		}
		if (static_cast<uint32>(Peer.Incoming.Num()) < 4 + Length)
		{
			break;
		}
		const FString Text = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Peer.Incoming.GetData() + 4), Length));
		Peer.Incoming.RemoveAt(0, 4 + Length, EAllowShrinking::No);
		TSharedPtr<FJsonObject> Message;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Message) || !Message.IsValid())
		{
			Why = TMNetPrivate::Garbled;
			return false;
		}
		Received.Add({ Peer.Id, Message });
	}
	return true;
}

void FTMNet::Poll(float DeltaSeconds)
{
	// Players at the door.
	bool bWaiting = false;
	while (Listener && Listener->HasPendingConnection(bWaiting) && bWaiting)
	{
		FSocket* Incoming = Listener->Accept(TEXT("TacticalMastersPeer"));
		if (!Incoming)
		{
			break;
		}
		Incoming->SetNonBlocking(true);
		Incoming->SetNoDelay(true);
		if (!bAccepting || Peers.Num() >= MaxPeers)
		{
			// A battle on, or four already: say so, then let them go.
			TurnAway(Incoming, FString(), TArray<uint8>(), bAccepting ? FString(TEXT("That game is full: four players already.")) : BusyReason);
			continue;
		}
		FPeer Peer;
		Peer.Id = NextPeerId++;
		Peer.Socket = Incoming;
		Peer.bConnected = true;
		Peers.Add(MoveTemp(Peer));
		Arrived.Add(Peers.Last().Id);
	}
	// Players through a join code: the lobby, arrivals, packets, connections closed.
	PollCode(DeltaSeconds);
	for (int32 i = TurnedAway.Num() - 1; i >= 0; --i)
	{
		FTurnedAway& Away = TurnedAway[i];
		Away.Age += DeltaSeconds;
		// Half a second for the refusal to arrive, as net.gd:185 gives it (more through Epic, which queues it).
		const bool bEos = !Away.EosUser.IsEmpty();
		const bool bGoing = bEos ? FlushEos(Away.EosUser, Away.Outgoing) : Flush(Away.Socket, Away.Outgoing);
		if (!bGoing || Away.Age > (bEos ? TMNetPrivate::EosRefusalSeconds : TMNetPrivate::RefusalSeconds))
		{
			Destroy(Away.Socket);
			if (bEos)
			{
				FTMEos::Get().Disconnect(Away.EosUser);
			}
			TurnedAway.RemoveAt(i);
		}
	}

	for (int32 i = Peers.Num() - 1; i >= 0; --i)
	{
		FPeer& Peer = Peers[i];
		if (!Peer.bConnected && !Peer.EosUser.IsEmpty())
		{
			// Joining by a code: PollCode marks the host connected once Epic says so.
			ConnectingFor += DeltaSeconds;
			if (ConnectingFor > TMNetPrivate::CodeConnectSeconds)
			{
				DropPeer(i, TEXT("Couldn't reach the host, even through Epic's relays. Check both PCs are online, then try again."));
			}
			continue;
		}
		if (!Peer.bConnected)
		{
			// Joining: the connection to the host is still being made.
			ConnectingFor += DeltaSeconds;
			const ESocketConnectionState State = Peer.Socket->GetConnectionState();
			if (State == SCS_Connected)
			{
				Peer.bConnected = true;
				bJustConnected = true;
			}
			else if (State == SCS_ConnectionError || ConnectingFor > TMNetPrivate::ConnectSeconds)
			{
				DropPeer(i, TEXT("Could not connect to the host."));
				continue;
			}
			else
			{
				continue;
			}
		}
		FString Why;
		if (!(Peer.Socket ? Flush(Peer.Socket, Peer.Outgoing) : FlushEos(Peer.EosUser, Peer.Outgoing)))
		{
			DropPeer(i, bHosting ? TEXT("A player disconnected.") : TEXT("The host disconnected."));
			continue;
		}
		if (!ReadPeer(Peer, Why))
		{
			DropPeer(i, Why);
		}
	}
}

int32 FTMNet::AddEosPeer(const FString& User)
{
	for (const FTurnedAway& Away : TurnedAway)
	{
		if (Away.EosUser == User)
		{
			return INDEX_NONE;  // already being told no
		}
	}
	if (!bAccepting || Peers.Num() >= MaxPeers)
	{
		TurnAway(nullptr, User, TArray<uint8>(), bAccepting ? FString(TEXT("That game is full: four players already.")) : BusyReason);
		return INDEX_NONE;
	}
	FPeer Peer;
	Peer.Id = NextPeerId++;
	Peer.EosUser = User;
	Peer.bConnected = true;
	Peers.Add(MoveTemp(Peer));
	Arrived.Add(Peers.Last().Id);
	return Peers.Num() - 1;
}

void FTMNet::PollCode(float DeltaSeconds)
{
	if (!bWantCode && !bJoiningByCode)
	{
		return;
	}
	FTMEos& Eos = FTMEos::Get();
	using ELobby = FTMEos::ELobby;
	const FTMEos::EState State = Eos.GetState();

	// Start the lobby once the sign-in is ready (it may still be signing in when Host or Join is pressed).
	if (Eos.LobbyState() == ELobby::None && Lost.IsEmpty())
	{
		if (State == FTMEos::EState::Ready)
		{
			if (bWantCode)
			{
				Eos.HostLobby(MaxPeers + 1, ProtocolVersion);
			}
			else if (Peers.Num() == 0)
			{
				Eos.JoinLobby(WantedCode, ProtocolVersion);
			}
		}
		else if (State != FTMEos::EState::SigningIn)
		{
			if (bWantCode && !bCodeRefusalTold)
			{
				bCodeRefusalTold = true;
				CodeNews = TEXT("No join code: ") + Eos.StatusLine();
			}
			else if (bJoiningByCode)
			{
				Lost = Eos.StatusLine();
				bJoiningByCode = false;
				return;
			}
		}
	}

	// What the screen says as the lobby goes along.
	const ELobby Now = Eos.LobbyState();
	if (static_cast<uint8>(Now) != LastLobby)
	{
		LastLobby = static_cast<uint8>(Now);
		switch (Now)
		{
		case ELobby::Creating:
			CodeNews = TEXT("Making a join code...");
			break;
		case ELobby::Hosting:
			CodeNews = FString::Printf(TEXT("Your join code is %s: send it to your friends."), *Eos.LobbyCode());
			break;
		case ELobby::Searching:
			CodeNews = FString::Printf(TEXT("Looking for the game %s..."), *Eos.LobbyCode());
			break;
		case ELobby::Joining:
			CodeNews = TEXT("Found it. Joining...");
			break;
		case ELobby::Joined:
			CodeNews = TEXT("Connecting to the host (through Epic's relays if need be)...");
			break;
		case ELobby::Failed:
			if (bJoiningByCode)
			{
				Lost = Eos.LobbyError();
				bJoiningByCode = false;
				Eos.LeaveLobby();
				return;
			}
			CodeNews = TEXT("No join code: ") + Eos.LobbyError() + TEXT(" Players can still join by address.");
			break;
		default:
			break;
		}
	}

	if (bJoiningByCode && Now == ELobby::Joined && Peers.Num() == 0 && Lost.IsEmpty())
	{
		// In the host's lobby: now the connection to it.
		FPeer HostPeer;
		HostPeer.Id = 0;
		HostPeer.EosUser = Eos.LobbyHost();
		Peers.Add(MoveTemp(HostPeer));
		ConnectingFor = 0.0f;
		Eos.Connect(Eos.LobbyHost());
	}

	// Connections made.
	for (const FString& User : Eos.Established)
	{
		const int32 Index = FindEosPeer(User);
		if (Index != INDEX_NONE)
		{
			if (!Peers[Index].bConnected)
			{
				Peers[Index].bConnected = true;
				bJustConnected = true;
			}
		}
		else if (bHosting && bWantCode)
		{
			AddEosPeer(User);
		}
	}
	Eos.Established.Reset();

	// Packets: each peer's share of the stream, in order (a host's player may be heard before Epic says it connected).
	FString From;
	TArray<uint8> Bytes;
	for (int32 Budget = 8192; Budget > 0 && Eos.Receive(From, Bytes); --Budget)
	{
		int32 Index = FindEosPeer(From);
		if (Index == INDEX_NONE && bHosting && bWantCode)
		{
			Index = AddEosPeer(From);
		}
		if (Index != INDEX_NONE)
		{
			FPeer& Peer = Peers[Index];
			if (!Peer.bConnected)
			{
				Peer.bConnected = true;
				bJustConnected = true;
			}
			Peer.Incoming.Append(Bytes);
		}
	}

	// Connections closed, and players who left the lobby.
	for (const TPair<FString, FString>& Gone : Eos.Closed)
	{
		const int32 Index = FindEosPeer(Gone.Key);
		if (Index != INDEX_NONE)
		{
			DropPeer(Index, bHosting ? FString::Printf(TEXT("A player disconnected (%s)."), *Gone.Value)
				: (Peers[Index].bConnected ? FString::Printf(TEXT("The host disconnected (%s)."), *Gone.Value)
					: TEXT("Couldn't connect to the host: ") + Gone.Value + TEXT(".")));
		}
	}
	Eos.Closed.Reset();
	for (const FString& User : Eos.LeftLobby)
	{
		const int32 Index = FindEosPeer(User);
		if (Index != INDEX_NONE)
		{
			DropPeer(Index, TEXT("A player left."));
		}
	}
	Eos.LeftLobby.Reset();
	if (Eos.bLobbyClosed && bJoiningByCode)
	{
		Eos.bLobbyClosed = false;
		const int32 Index = Peers.Num() > 0 ? 0 : INDEX_NONE;
		if (Index != INDEX_NONE)
		{
			DropPeer(Index, TEXT("The host closed the game."));
		}
		else if (Lost.IsEmpty())
		{
			Lost = TEXT("The host closed the game.");
		}
	}
}
