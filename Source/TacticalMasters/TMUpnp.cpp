#include "TMUpnp.h"

#include "Common/UdpSocketBuilder.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
	/** How long to wait for a router to answer the search: Godot gives it two seconds, twice (net.gd:302). */
	constexpr float SearchSeconds = 4.0f;

	FString ByHand(int32 Port)
	{
		return FString::Printf(TEXT("forward TCP port %d to this computer on your router yourself, or use a VPN such as Tailscale."), Port);
	}
}

FTMUpnp::~FTMUpnp()
{
	Stop();
	*Alive = false;
}

void FTMUpnp::CloseSearch()
{
	if (Search)
	{
		Search->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Search);
		Search = nullptr;
	}
}

void FTMUpnp::Finish(const FString& Said)
{
	CloseSearch();
	Message = Said;
	bDone = true;
	Step = EStep::Done;
}

FString FTMUpnp::Tag(const FString& Xml, const FString& Name, int32 From)
{
	// Just enough XML for a router's answers: the text of the first <Name> after From.
	const FString Open = TEXT("<") + Name + TEXT(">");
	const FString Close = TEXT("</") + Name + TEXT(">");
	int32 Start = Xml.Find(Open, ESearchCase::IgnoreCase, ESearchDir::FromStart, From);
	if (Start == INDEX_NONE)
	{
		// Some routers write a namespace prefix: <m:NewExternalIPAddress>.
		const int32 Colon = Xml.Find(TEXT(":") + Name + TEXT(">"), ESearchCase::IgnoreCase, ESearchDir::FromStart, From);
		if (Colon == INDEX_NONE)
		{
			return FString();
		}
		Start = Colon + Name.Len() + 2;
	}
	else
	{
		Start += Open.Len();
	}
	int32 End = Xml.Find(TEXT("</"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Start);
	const int32 Plain = Xml.Find(Close, ESearchCase::IgnoreCase, ESearchDir::FromStart, Start);
	End = Plain != INDEX_NONE ? Plain : End;
	return End == INDEX_NONE ? FString() : Xml.Mid(Start, End - Start).TrimStartAndEnd();
}

void FTMUpnp::Start(int32 InPort)
{
	Stop();
	Port = InPort;
	bDone = false;
	Message.Reset();
	Search = FUdpSocketBuilder(TEXT("TacticalMastersUpnp")).AsNonBlocking().AsReusable().Build();
	if (!Search)
	{
		Finish(TEXT("Couldn't look for a router: ") + ByHand(Port));
		return;
	}
	// The SSDP search, as every UPnP router listens for it.
	const FString Ask = TEXT("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n")
		TEXT("ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n");
	const FTCHARToUTF8 Bytes(*Ask);
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	bool bValid = false;
	TSharedRef<FInternetAddr> Group = Sockets->CreateInternetAddr();
	Group->SetIp(TEXT("239.255.255.250"), bValid);
	Group->SetPort(1900);
	int32 Sent = 0;
	Search->SendTo(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Sent, *Group);
	SearchingFor = 0.0f;
	Step = EStep::Searching;
}

void FTMUpnp::Poll(float DeltaSeconds)
{
	if (Step != EStep::Searching || !Search)
	{
		return;
	}
	SearchingFor += DeltaSeconds;
	uint8 Buffer[2048];
	int32 Read = 0;
	TSharedRef<FInternetAddr> From = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	while (Search->RecvFrom(Buffer, sizeof(Buffer) - 1, Read, *From) && Read > 0)
	{
		Buffer[Read] = 0;
		const FString Answer = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Buffer));
		TArray<FString> Lines;
		Answer.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("LOCATION:"), ESearchCase::IgnoreCase))
			{
				Location = Line.Mid(9).TrimStartAndEnd();
			}
		}
		if (!Location.IsEmpty())
		{
			// This computer's address as the router sees it: the one the answer came to.
			LocalAddress.Reset();
			TArray<TSharedPtr<FInternetAddr>> Mine;
			if (ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalAdapterAddresses(Mine))
			{
				const FString Router = From->ToString(false);
				for (const TSharedPtr<FInternetAddr>& Address : Mine)
				{
					const FString Text = Address->ToString(false);
					if (Text.Contains(TEXT(".")) && !Text.StartsWith(TEXT("127.")) && !Text.StartsWith(TEXT("169.254.")))
					{
						// On the router's network if the first three numbers match; otherwise any will do.
						const FString Prefix = Router.Left(Router.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd));
						if (LocalAddress.IsEmpty() || Text.StartsWith(Prefix))
						{
							LocalAddress = Text;
						}
					}
				}
			}
			CloseSearch();
			Describe();
			return;
		}
	}
	if (SearchingFor > SearchSeconds)
	{
		Finish(TEXT("No UPnP router answered: for internet play, ") + ByHand(Port));
	}
}

void FTMUpnp::Describe()
{
	Step = EStep::Describing;
	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Location);
	Request->SetVerb(TEXT("GET"));
	Request->OnProcessRequestComplete().BindLambda([this, Alive = Alive](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
	{
		if (!*Alive || Step != EStep::Describing)
		{
			return;
		}
		if (!bOk || !Response.IsValid())
		{
			Finish(TEXT("The router didn't describe itself: ") + ByHand(Port));
			return;
		}
		const FString Xml = Response->GetContentAsString();
		// The service that runs the internet connection: IP, or PPP on some DSL routers.
		for (const TCHAR* Wanted : { TEXT("WANIPConnection:2"), TEXT("WANIPConnection:1"), TEXT("WANPPPConnection:1") })
		{
			const int32 At = Xml.Find(Wanted);
			if (At != INDEX_NONE)
			{
				ServiceType = FString(TEXT("urn:schemas-upnp-org:service:")) + Wanted;
				ControlUrl = Tag(Xml, TEXT("controlURL"), At);
				break;
			}
		}
		if (ControlUrl.IsEmpty())
		{
			Finish(TEXT("The router doesn't offer port forwarding: ") + ByHand(Port));
			return;
		}
		if (!ControlUrl.StartsWith(TEXT("http")))
		{
			// Relative to the router: scheme, host and port from where it was found.
			const int32 Slash = Location.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 8);
			const FString Base = Slash == INDEX_NONE ? Location : Location.Left(Slash);
			ControlUrl = Base + (ControlUrl.StartsWith(TEXT("/")) ? TEXT("") : TEXT("/")) + ControlUrl;
		}
		Map();
	});
	Request->ProcessRequest();
}

FHttpRequestPtr FTMUpnp::Soap(const FString& Action, const FString& Arguments)
{
	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ControlUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("text/xml; charset=\"utf-8\""));
	Request->SetHeader(TEXT("SOAPAction"), FString::Printf(TEXT("\"%s#%s\""), *ServiceType, *Action));
	Request->SetContentAsString(FString::Printf(
		TEXT("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">")
		TEXT("<s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>"),
		*Action, *ServiceType, *Arguments, *Action));
	return Request;
}

void FTMUpnp::Map()
{
	if (LocalAddress.IsEmpty())
	{
		Finish(TEXT("Couldn't tell this computer's address: ") + ByHand(Port));
		return;
	}
	Step = EStep::Mapping;
	FHttpRequestPtr Request = Soap(TEXT("AddPortMapping"), FString::Printf(
		TEXT("<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>TCP</NewProtocol>")
		TEXT("<NewInternalPort>%d</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>")
		TEXT("<NewPortMappingDescription>Tactical Masters</NewPortMappingDescription><NewLeaseDuration>0</NewLeaseDuration>"),
		Port, Port, *LocalAddress));
	Request->OnProcessRequestComplete().BindLambda([this, Alive = Alive](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
	{
		if (!*Alive || Step != EStep::Mapping)
		{
			return;
		}
		if (!bOk || !Response.IsValid() || Response->GetResponseCode() != 200)
		{
			Finish(TEXT("The router wouldn't open the port: ") + ByHand(Port));
			return;
		}
		bMapped = true;
		AskAddress();
	});
	Request->ProcessRequest();
}

void FTMUpnp::AskAddress()
{
	Step = EStep::Asking;
	FHttpRequestPtr Request = Soap(TEXT("GetExternalIPAddress"), FString());
	Request->OnProcessRequestComplete().BindLambda([this, Alive = Alive](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
	{
		if (!*Alive || Step != EStep::Asking)
		{
			return;
		}
		const FString Public = bOk && Response.IsValid() ? Tag(Response->GetContentAsString(), TEXT("NewExternalIPAddress")) : FString();
		Finish(Public.IsEmpty()
			? FString::Printf(TEXT("Router port %d opened. Players on the internet join at your public address, port %d."), Port, Port)
			: FString::Printf(TEXT("Router port %d opened automatically. Players on the internet can join at %s:%d."), Port, *Public, Port));
	});
	Request->ProcessRequest();
}

void FTMUpnp::Stop()
{
	CloseSearch();
	if (bMapped && !ControlUrl.IsEmpty())
	{
		// Sent and not waited for: hosting is over either way.
		FHttpRequestPtr Request = Soap(TEXT("DeletePortMapping"), FString::Printf(
			TEXT("<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>TCP</NewProtocol>"), Port));
		Request->ProcessRequest();
	}
	bMapped = false;
	// Answers still on their way find a step they no longer belong to and do nothing.
	Step = EStep::Idle;
}
