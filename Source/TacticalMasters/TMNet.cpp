#include "TMNet.h"

#include "Common/TcpSocketBuilder.h"
#include "IPAddress.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
	/** How long a join may take to connect before it is given up (Godot's ENet waits about as long). */
	constexpr float ConnectSeconds = 10.0f;
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
	Destroy(Peer);
	Destroy(Listener);
	for (FTurnedAway& Away : TurnedAway)
	{
		Destroy(Away.Socket);
	}
	TurnedAway.Reset();
	bHosting = false;
	bPeerConnected = false;
	Outgoing.Reset();
	Incoming.Reset();
	Received.Reset();
	bJustConnected = false;
	Lost.Reset();
}

FString FTMNet::Host(int32 Port)
{
	Close();
	Listener = FTcpSocketBuilder(TEXT("TacticalMastersHost"))
		.AsReusable()
		.AsNonBlocking()
		.BoundToPort(Port)
		.Listening(1)
		.Build();
	if (!Listener)
	{
		return FString::Printf(TEXT("Couldn't host on port %d: is something else using it?"), Port);
	}
	bHosting = true;
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
	Peer = FTcpSocketBuilder(TEXT("TacticalMastersJoin")).AsNonBlocking().Build();
	if (!Peer)
	{
		return TEXT("Couldn't open a connection.");
	}
	Peer->SetNoDelay(true);
	// Non-blocking, so this only starts it; Poll sees it through.
	Peer->Connect(*Where);
	ConnectingFor = 0.0f;
	return FString();
}

void FTMNet::Send(const TSharedRef<FJsonObject>& Message)
{
	if (Peer)
	{
		Frame(Message, Outgoing);
	}
}

void FTMNet::Kick(const FString& Reason)
{
	if (!Peer)
	{
		return;
	}
	FTurnedAway Away;
	Away.Socket = Peer;
	Away.Outgoing = MoveTemp(Outgoing);
	TSharedRef<FJsonObject> Refusal = MakeShared<FJsonObject>();
	Refusal->SetStringField(TEXT("t"), TEXT("refuse"));
	Refusal->SetStringField(TEXT("reason"), Reason);
	Frame(Refusal, Away.Outgoing);
	TurnedAway.Add(MoveTemp(Away));
	Peer = nullptr;
	bPeerConnected = false;
	Outgoing.Reset();
	Incoming.Reset();
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

void FTMNet::DropPeer(const FString& Why)
{
	Destroy(Peer);
	bPeerConnected = false;
	Outgoing.Reset();
	Incoming.Reset();
	Lost = Why;
}

void FTMNet::ReadPeer()
{
	uint8 Buffer[16384];
	for (;;)
	{
		// For a stream, Recv says false only when the other end has closed (or
		// failed); true with nothing read is just nothing yet (SocketsBSD.cpp).
		int32 Read = 0;
		if (!Peer->Recv(Buffer, sizeof(Buffer), Read))
		{
			DropPeer(bHosting ? TEXT("Your opponent disconnected.") : TEXT("The host disconnected."));
			return;
		}
		if (Read == 0)
		{
			break;
		}
		Incoming.Append(Buffer, Read);
	}
	// Whole messages out of what has arrived.
	while (Peer && Incoming.Num() >= 4)
	{
		const uint32 Length = (static_cast<uint32>(Incoming[0]) << 24) | (static_cast<uint32>(Incoming[1]) << 16)
			| (static_cast<uint32>(Incoming[2]) << 8) | static_cast<uint32>(Incoming[3]);
		if (Length > static_cast<uint32>(MaxMessageBytes))
		{
			DropPeer(TEXT("The other game sent something it shouldn't have, so the connection was closed."));
			return;
		}
		if (static_cast<uint32>(Incoming.Num()) < 4 + Length)
		{
			break;
		}
		const FString Text = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Incoming.GetData() + 4), Length));
		Incoming.RemoveAt(0, 4 + Length, EAllowShrinking::No);
		TSharedPtr<FJsonObject> Message;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Message) || !Message.IsValid())
		{
			DropPeer(TEXT("The other game sent something it shouldn't have, so the connection was closed."));
			return;
		}
		Received.Add(Message);
	}
}

void FTMNet::Poll(float DeltaSeconds)
{
	// A player at the door.
	bool bWaiting = false;
	while (Listener && Listener->HasPendingConnection(bWaiting) && bWaiting)
	{
		FSocket* Arrived = Listener->Accept(TEXT("TacticalMastersPeer"));
		if (!Arrived)
		{
			break;
		}
		Arrived->SetNonBlocking(true);
		Arrived->SetNoDelay(true);
		if (Peer)
		{
			// Someone is already playing: say so, then let them go.
			FTurnedAway Away;
			Away.Socket = Arrived;
			TSharedRef<FJsonObject> Refusal = MakeShared<FJsonObject>();
			Refusal->SetStringField(TEXT("t"), TEXT("refuse"));
			Refusal->SetStringField(TEXT("reason"), BusyReason);
			Frame(Refusal, Away.Outgoing);
			TurnedAway.Add(MoveTemp(Away));
			continue;
		}
		Peer = Arrived;
		bPeerConnected = true;
		bJustConnected = true;
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

	if (!Peer)
	{
		return;
	}
	if (!bPeerConnected)
	{
		ConnectingFor += DeltaSeconds;
		const ESocketConnectionState State = Peer->GetConnectionState();
		if (State == SCS_Connected)
		{
			bPeerConnected = true;
			bJustConnected = true;
		}
		else if (State == SCS_ConnectionError || ConnectingFor > ConnectSeconds)
		{
			DropPeer(TEXT("Could not connect to the host."));
			return;
		}
		else
		{
			return;
		}
	}
	if (!Flush(Peer, Outgoing))
	{
		DropPeer(bHosting ? TEXT("Your opponent disconnected.") : TEXT("The host disconnected."));
		return;
	}
	ReadPeer();
}
