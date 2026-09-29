// Asks the router to let players on the internet reach a host (UPnP), as the
// Godot game does when it hosts (net.gd:293-325).
//
// Three steps, all in the background: find the router (an SSDP search on the
// local network), read what it offers (its description, over HTTP), and ask
// its internet connection to forward the port here (a SOAP call over HTTP).
// Then ask it for the public address, which is what the other player types.
// Plenty of routers have UPnP switched off, so failing is normal: the message
// then says how to open the port by hand.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

class FSocket;

class FTMUpnp
{
public:
	~FTMUpnp();

	/** Starts asking for TCP Port to be forwarded here. */
	void Start(int32 Port);
	/** Takes the forward away again, if it was made. */
	void Stop();
	/** Drives the search. Once a frame. */
	void Poll(float DeltaSeconds);

	/** What happened, in a sentence, once there is something to say. */
	FString Message;
	bool bDone = false;

private:
	enum class EStep : uint8 { Idle, Searching, Describing, Mapping, Asking, Done };
	EStep Step = EStep::Idle;
	int32 Port = 0;
	float SearchingFor = 0.0f;
	FSocket* Search = nullptr;
	/** The router's description, and the control address of its internet connection service. */
	FString Location;
	FString ControlUrl;
	FString ServiceType;
	FString LocalAddress;
	bool bMapped = false;
	/** Cleared when this goes, so an answer from the router that arrives later does nothing. */
	TSharedRef<bool, ESPMode::ThreadSafe> Alive = MakeShared<bool, ESPMode::ThreadSafe>(true);

	void Finish(const FString& Said);
	void Describe();
	void Map();
	void AskAddress();
	FHttpRequestPtr Soap(const FString& Action, const FString& Arguments);
	static FString Tag(const FString& Xml, const FString& Name, int32 From = 0);
	void CloseSearch();
};
