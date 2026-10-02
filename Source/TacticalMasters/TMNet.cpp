#include "TMNet.h"

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

void FTMNet::TurnAway(FSocket* Socket, TArray<uint8>&& Pending, const FString& Reason)
{
	FTurnedAway Away;
	Away.Socket = Socket;
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
			TurnAway(Peers[i].Socket, MoveTemp(Peers[i].Outgoing), Reason);
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

void FTMNet::DropPeer(int32 Index, const FString& Why)
{
	if (!Peers.IsValidIndex(Index))
	{
		return;
	}
	const int32 Id = Peers[Index].Id;
	Destroy(Peers[Index].Socket);
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
			TurnAway(Incoming, TArray<uint8>(), bAccepting ? FString(TEXT("That game is full: four players already.")) : BusyReason);
			continue;
		}
		FPeer Peer;
		Peer.Id = NextPeerId++;
		Peer.Socket = Incoming;
		Peer.bConnected = true;
		Peers.Add(MoveTemp(Peer));
		Arrived.Add(Peers.Last().Id);
	}
	for (int32 i = TurnedAway.Num() - 1; i >= 0; --i)
	{
		FTurnedAway& Away = TurnedAway[i];
		Away.Age += DeltaSeconds;
		// Half a second for the refusal to arrive, as net.gd:185 gives it.
		if (!Flush(Away.Socket, Away.Outgoing) || Away.Age > 0.5f)
		{
			Destroy(Away.Socket);
			TurnedAway.RemoveAt(i);
		}
	}

	for (int32 i = Peers.Num() - 1; i >= 0; --i)
	{
		FPeer& Peer = Peers[i];
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
		if (!Flush(Peer.Socket, Peer.Outgoing))
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
