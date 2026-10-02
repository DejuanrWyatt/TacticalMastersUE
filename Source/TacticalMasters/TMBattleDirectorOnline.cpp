// Online play: two to four players, each on their own machine, one battle
// between them (Docs/design/feat-online.md, after the Godot game's net.gd and
// battle.gd; the lobby before it, Docs/design/feat-lobby.md).
//
// The host is the referee. Its own orders are checked, sent to every other
// player, then applied; the others' come to it as requests, which it
// checks like its own (and a little more) before doing the same. Only the host
// moves time. So both machines apply exactly the same orders in exactly the
// same order, and since the rules are deterministic the two battles are the
// same battle -- which the host's checksums, every five seconds of battle,
// keep proving. The connection itself is FTMNet's (TMNet.cpp).

#include "TMBattleDirector.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

#include "SimClassFile.h"
#include "SimMap.h"
#include "SimOrderText.h"
#include "TMSettings.h"
#include "TMTextInput.h"
#include "TMViewportClient.h"

#include <cstring>

namespace
{
	/** How often the host sends where the battle stands: every 5 s of battle (battle.gd:35). */
	constexpr int32 ChecksumEveryTicks = 50;
	/** The longest chat line (net.gd:36). */
	constexpr int32 MaxChat = 120;

	/** A double as its exact bits, since a JSON number could come back a hair different. */
	FString DoubleBits(double Value)
	{
		uint64 Bits = 0;
		std::memcpy(&Bits, &Value, sizeof(Bits));
		return FString::Printf(TEXT("%llx"), Bits);
	}

	bool FromBits(const FString& Text, double& Out)
	{
		if (Text.IsEmpty() || Text.Len() > 16)
		{
			return false;
		}
		for (const TCHAR Character : Text)
		{
			if (!FChar::IsHexDigit(Character))
			{
				return false;
			}
		}
		const uint64 Bits = FCString::Strtoui64(*Text, nullptr, 16);
		std::memcpy(&Out, &Bits, sizeof(Out));
		return FMath::IsFinite(Out);
	}

	/** A class or map file's text as this machine has it, line endings aside; "" if it has none. */
	FString LocalFile(const FString& Folder, const FString& Name)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data") / Folder / Name));
		Text.ReplaceInline(TEXT("\r"), TEXT(""));
		return Text;
	}

	/**
	 * One number for every file in a folder of Content/Data, by name and
	 * content. The camps draw their loot from the whole item catalog and wake
	 * the monster classes, so with camps on both games need the same of each.
	 */
	uint32 FolderPrint(const FString& Folder)
	{
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(FPaths::ProjectContentDir() / TEXT("Data") / Folder / TEXT("*.json")), true, false);
		Names.Sort();
		uint32 Print = 0;
		for (const FString& Name : Names)
		{
			Print = FCrc::StrCrc32(*Name, Print);
			Print = FCrc::StrCrc32(*LocalFile(Folder, Name), Print);
		}
		return Print;
	}
}

// ---------------------------------------------------------------- the screens

void ATMBattleDirector::OpenOnline()
{
	// Remember the offline setup once, on the way in, to put back on leaving.
	if (!Net.IsValid() && Setup.Mode != TEXT("online"))
	{
		OfflineSetup = Setup;
	}
	Screen = EScreen::Online;
	bMenuOpen = false;
	bPaused = false;
}

void ATMBattleDirector::HostOnline()
{
	const int32 Port = FMath::Clamp(FCString::Atoi(*JoinPort), 1, 65535);
	Net = MakeUnique<FTMNet>();
	const FString Refused = Net->Host(Port);
	if (!Refused.IsEmpty())
	{
		OnlineStatus = Refused;
		Net.Reset();
		return;
	}
	OnlineStatus = FString::Printf(TEXT("Hosting on port %d. Waiting for players..."), Port);
	// The addresses a player on the same network would type (main_menu.gd:204-210).
	TArray<FString> Near;
	TArray<TSharedPtr<FInternetAddr>> Mine;
	if (ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalAdapterAddresses(Mine))
	{
		for (const TSharedPtr<FInternetAddr>& Address : Mine)
		{
			const FString Text = Address->ToString(false);
			if (Text.Contains(TEXT(".")) && !Text.StartsWith(TEXT("127.")) && !Text.StartsWith(TEXT("169.254.")))
			{
				Near.AddUnique(Text);
			}
		}
	}
	OnlineAddresses = TEXT("On your network: ") + (Near.Num() > 0 ? FString::Join(Near, TEXT(", ")) : FString(TEXT("unknown")));
	OnlineRouter = TEXT("Trying to open the port on your router...");
	Upnp = MakeUnique<FTMUpnp>();
	Upnp->Start(Port);
	UE_LOG(LogTemp, Log, TEXT("ONLINE: hosting on port %d"), Port);
	StartLobby();
}

void ATMBattleDirector::JoinOnline()
{
	FString Address = JoinAddress.TrimStartAndEnd();
	int32 Port = FMath::Clamp(FCString::Atoi(*JoinPort), 1, 65535);
	// "host:port" typed into the address works too.
	FString Host;
	FString PortText;
	if (Address.Split(TEXT(":"), &Host, &PortText, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && PortText.IsNumeric() && !Host.Contains(TEXT(":")))
	{
		Address = Host;
		Port = FMath::Clamp(FCString::Atoi(*PortText), 1, 65535);
	}
	if (Address.IsEmpty())
	{
		OnlineStatus = TEXT("Enter the host's address first.");
		return;
	}
	OnlineAddresses.Reset();
	OnlineRouter.Reset();
	Net = MakeUnique<FTMNet>();
	const FString Refused = Net->Join(Address, Port);
	if (!Refused.IsEmpty())
	{
		OnlineStatus = Refused;
		Net.Reset();
		return;
	}
	OnlineStatus = FString::Printf(TEXT("Connecting to %s..."), *Address);
	UE_LOG(LogTemp, Log, TEXT("ONLINE: joining %s:%d"), *Address, Port);
}

void ATMBattleDirector::LeaveOnline()
{
	Upnp.Reset();
	Net.Reset();
	const bool bWasOnline = Setup.Mode == TEXT("online");
	bOnline = false;
	bOnlineHost = false;
	bWaitingForHost = false;
	bWantRematch = false;
	bOpponentWantsRematch = false;
	OnlineStopped.Reset();
	OnlineStatus.Reset();
	OnlineAddresses.Reset();
	OnlineRouter.Reset();
	HostSums.Reset();
	Players.Reset();
	UnitPlayer.Reset();
	LocalPlayer = 0;
	Draft = FTMDraftState();
	bDraftDone = false;
	if (bWasOnline)
	{
		Setup = OfflineSetup;
	}
	if (TextInput.IsValid())
	{
		TextInput->Stop();
	}
	Typing = ETypeField::None;
}

void ATMBattleDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	LeaveOnline();
	if (TextInput.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(TextInput);
	}
	TextInput.Reset();
	UTMViewportClient::Typist = nullptr;
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------- typing

void ATMBattleDirector::StartTyping(ETypeField Field)
{
	if (!TextInput.IsValid())
	{
		return;
	}
	FString* Into = Field == ETypeField::Address ? &JoinAddress : Field == ETypeField::Port ? &JoinPort : &ChatLine;
	const int32 Longest = Field == ETypeField::Chat ? MaxChat : Field == ETypeField::Port ? 5 : 200;
	Typing = Field;
	TextInput->Begin(Into, Longest, [this](bool bSubmit) { TypedDone(bSubmit); });
}

bool ATMBattleDirector::IsTyping() const
{
	return TextInput.IsValid() && TextInput->IsTyping();
}

void ATMBattleDirector::TypedDone(bool bSubmit)
{
	const ETypeField Was = Typing;
	Typing = ETypeField::None;
	if (Was == ETypeField::Chat)
	{
		if (bSubmit)
		{
			SendChat();
		}
		ChatLine.Reset();
	}
	else if (Was == ETypeField::Address && bSubmit)
	{
		JoinOnline();
	}
}

void ATMBattleDirector::SendChat()
{
	const FString Said = ChatLine.TrimStartAndEnd().Left(MaxChat);
	if (Said.IsEmpty() || !Net.IsValid() || !Net->IsConnected())
	{
		return;
	}
	// The host passes every line on to the others, with who said it.
	const FString Name = Players.IsValidIndex(LocalPlayer) ? Players[LocalPlayer].Name : LocalName();
	SendOnline(TEXT("chat"), [&Said, &Name](FJsonObject& Message)
	{
		Message.SetStringField(TEXT("text"), Said);
		Message.SetStringField(TEXT("name"), Name);
	});
	LogNote(TEXT("You: ") + Said);
}

// ---------------------------------------------------------------- messages

void ATMBattleDirector::SendOnline(const TCHAR* Kind, TFunctionRef<void(FJsonObject&)> Fill)
{
	if (!Net.IsValid() || !Net->IsConnected())
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("t"), Kind);
	Fill(*Message);
	Net->Send(Message);
}

void ATMBattleDirector::AdvanceOnline(float DeltaSeconds)
{
	if (Upnp.IsValid())
	{
		Upnp->Poll(DeltaSeconds);
		if (Upnp->bDone)
		{
			OnlineRouter = Upnp->Message;
		}
	}
	// An unattended run (the online test) that can't play ends rather than waits.
	if (FApp::IsUnattended() && (!OnlineStopped.IsEmpty() || (Screen == EScreen::Online && !Net.IsValid() && !OnlineStatus.IsEmpty())))
	{
		DecidedFor += DeltaSeconds;
		if (DecidedFor > 1.5f)
		{
			UE_LOG(LogTemp, Log, TEXT("ONLINE ENDED: %s"), OnlineStopped.IsEmpty() ? *OnlineStatus : *OnlineStopped);
			FPlatformMisc::RequestExit(false);
		}
	}
	if (!Net.IsValid())
	{
		return;
	}
	Net->Poll(DeltaSeconds);
	if (Net->bJustConnected)
	{
		// net.gd:168-170: say hello with this build's version, and a name.
		Net->bJustConnected = false;
		OnlineStatus = TEXT("Connected! Checking versions...");
		const FString Name = LocalName();
		SendOnline(TEXT("hello"), [&Name](FJsonObject& Message)
		{
			Message.SetNumberField(TEXT("version"), FTMNet::ProtocolVersion);
			Message.SetStringField(TEXT("name"), Name);
		});
	}
	if (Net->Arrived.Num() > 0)
	{
		Net->Arrived.Reset();
		if (Screen == EScreen::Lobby)
		{
			OnlineStatus = TEXT("A player is connecting...");
		}
	}
	TArray<FTMNet::FArrival> Came = MoveTemp(Net->Received);
	Net->Received.Reset();
	for (const FTMNet::FArrival& Arrival : Came)
	{
		if (!Net.IsValid())
		{
			break;  // a refusal closed it
		}
		if (Arrival.Message.IsValid())
		{
			OnNetMessage(*Arrival.Message, Arrival.From);
		}
	}
	if (Net.IsValid() && Net->IsHost() && Net->Departed.Num() > 0)
	{
		TArray<TPair<int32, FString>> Gone = MoveTemp(Net->Departed);
		Net->Departed.Reset();
		for (const TPair<int32, FString>& Left : Gone)
		{
			LobbyDepart(Left.Key, Left.Value);
		}
	}
	if (Net.IsValid() && !Net->IsHost() && !Net->Lost.IsEmpty())
	{
		const FString Why = Net->Lost;
		Net->Lost.Reset();
		if (bOnline && Screen == EScreen::Battle)
		{
			if (Battle.Winner == -1)
			{
				// battle.gd:691-694.
				StopOnline(TEXT("The host disconnected"));
			}
			else
			{
				LogNote(TEXT("The host left."));
			}
		}
		else
		{
			// From the lobby or the draft: back to where an address is typed.
			OnlineStatus = Why;
			Screen = EScreen::Online;
			Players.Reset();
		}
		Net.Reset();
	}

	// -tmnetdesync: the joiner's game made to differ, once, to prove the split is caught.
	if (bNetDesync && !bDesyncDone && bOnline && !bOnlineHost && Battle.TickCount > 30 && !Battle.Units.empty())
	{
		bDesyncDone = true;
		Battle.Units[0].Hp = FMath::Max(1, Battle.Units[0].Hp - 1);
		UE_LOG(LogTemp, Log, TEXT("ONLINE: this game was made to differ on purpose (-tmnetdesync)"));
	}
}

void ATMBattleDirector::OnNetMessage(const FJsonObject& Message, int32 From)
{
	FString Kind;
	Message.TryGetStringField(TEXT("t"), Kind);
	const bool bHost = Net->IsHost();

	if (Kind == TEXT("hello") && bHost)
	{
		// net.gd:173-196: a different build would play a different game.
		int32 Version = 0;
		Message.TryGetNumberField(TEXT("version"), Version);
		if (Version != FTMNet::ProtocolVersion)
		{
			Net->Kick(From, FString::Printf(TEXT("Version mismatch: the host runs version %d, you run %d. Every player needs the same build."),
				FTMNet::ProtocolVersion, Version));
			OnlineStatus = FString::Printf(TEXT("Refused a player on a different version (%d)."), Version);
			return;
		}
		if (Screen != EScreen::Lobby || PlayerOfPeer(From) >= 0)
		{
			Net->Kick(From, Net->BusyReason);
			return;
		}
		FString Name;
		Message.TryGetStringField(TEXT("name"), Name);
		LobbyArrive(From, Name);
		return;
	}
	if (OnLobbyMessage(Kind, Message, From) || OnDraftMessage(Kind, Message, From))
	{
		return;
	}
	if (Kind == TEXT("refuse") && !bHost)
	{
		FString Reason;
		Message.TryGetStringField(TEXT("reason"), Reason);
		OnlineStatus = Reason;
		UE_LOG(LogTemp, Log, TEXT("ONLINE: refused: %s"), *Reason);
		Net.Reset();
	}
	else if (Kind == TEXT("start") && !bHost)
	{
		const FString Refused = StartOnlineFrom(Message);
		if (!Refused.IsEmpty())
		{
			OnlineStatus = Refused;
			UE_LOG(LogTemp, Log, TEXT("ONLINE: can't play the host's battle: %s"), *Refused);
			SendOnline(TEXT("desync"), [&Refused](FJsonObject& Out) { Out.SetStringField(TEXT("detail"), TEXT("the other player can't play this battle: ") + Refused); });
		}
	}
	else if (Kind == TEXT("cmd") && !bHost && bOnline)
	{
		// The host's word, applied as it comes (battle.gd:623-631). The rules
		// still check it: an order the host could play and this game can't
		// means the two games have already parted.
		FString Line;
		Message.TryGetStringField(TEXT("o"), Line);
		TMSim::FOrder Order;
		const std::string Unreadable = TMSim::OrderFromText(TCHAR_TO_UTF8(*Line), Order);
		if (!Unreadable.empty())
		{
			ReportOutOfSync(FString::Printf(TEXT("the host sent an order this game can't read (%hs)"), Unreadable.c_str()));
			return;
		}
		bApplyingFromHost = true;
		const FString Refused = Submit(Order);
		bApplyingFromHost = false;
		if (!Refused.IsEmpty())
		{
			ReportOutOfSync(TEXT("the host played something this game can't: ") + Refused);
			return;
		}
		// Anything but time passing answers the last request (battle.gd:415-417).
		if (Order.Type != TMSim::EOrderType::Advance)
		{
			bWaitingForHost = false;
		}
	}
	else if (Kind == TEXT("req") && bHost && bOnline)
	{
		FString Line;
		Message.TryGetStringField(TEXT("o"), Line);
		TMSim::FOrder Order;
		const std::string Unreadable = TMSim::OrderFromText(TCHAR_TO_UTF8(*Line), Order);
		FString Refused = Unreadable.empty() ? RefereeCheck(Order, PlayerOfPeer(From)) : FString(TEXT("That order couldn't be read."));
		if (Refused.IsEmpty())
		{
			// Checked, sent back, applied: Submit does all three on the host.
			Refused = Submit(Order);
		}
		if (!Refused.IsEmpty())
		{
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("t"), TEXT("reject"));
			Out->SetStringField(TEXT("reason"), Refused);
			Net->SendTo(From, Out);
		}
	}
	else if (Kind == TEXT("reject") && !bHost)
	{
		FString Reason;
		Message.TryGetStringField(TEXT("reason"), Reason);
		bWaitingForHost = false;
		Tell(Reason);
		LogNote(Reason);
	}
	else if (Kind == TEXT("sum") && !bHost && bOnline)
	{
		int32 Tick = 0;
		FString Value;
		Message.TryGetNumberField(TEXT("tick"), Tick);
		Message.TryGetStringField(TEXT("value"), Value);
		HostSums.Add(Tick, FCString::Strtoui64(*Value, nullptr, 10));
		CheckHostSums();
	}
	else if (Kind == TEXT("desync"))
	{
		FString Detail;
		Message.TryGetStringField(TEXT("detail"), Detail);
		if (bHost)
		{
			// One player's game split from the host's: everyone stops, told why.
			ReportOutOfSync(Detail.Left(200));
		}
		else
		{
			StopOnline(TEXT("Out of sync: ") + Detail.Left(200));
		}
	}
	else if (Kind == TEXT("chat"))
	{
		FString Said;
		FString Name;
		Message.TryGetStringField(TEXT("text"), Said);
		Said = Said.Left(MaxChat);
		if (bHost)
		{
			// From a player: the host knows who, and passes it on to the rest.
			const int32 Player = PlayerOfPeer(From);
			if (Player < 0)
			{
				return;
			}
			Name = Players[Player].Name;
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("t"), TEXT("chat"));
			Out->SetStringField(TEXT("text"), Said);
			Out->SetStringField(TEXT("name"), Name);
			for (int32 i = 1; i < Players.Num(); ++i)
			{
				if (i != Player && Players[i].bPresent)
				{
					Net->SendTo(Players[i].Peer, Out);
				}
			}
		}
		else
		{
			Message.TryGetStringField(TEXT("name"), Name);
		}
		const FString Line = (Name.IsEmpty() ? FString(TEXT("Player")) : Name.Left(20)) + TEXT(": ") + Said;
		LogNote(Line);
		Tell(Line);
	}
}

// ---------------------------------------------------------------- starting

void ATMBattleDirector::StartOnlineAsHost()
{
	// net.gd:190-201: the host plays blue and sends everything the battle
	// starts from, so the joiner starts the very same one.
	if (Setup.Mode != TEXT("online"))
	{
		OfflineSetup = Setup;
	}
	bWantRematch = false;
	bOpponentWantsRematch = false;
	bOnline = true;
	bOnlineHost = true;
	LocalPlayer = 0;
	LocalTeam = Players.IsValidIndex(0) ? Players[0].Team : 0;
	AssignUnits();
	if (Net.IsValid())
	{
		Net->bAccepting = false;
	}
	bWaitingForHost = false;
	OnlineStopped.Reset();
	HostSums.Reset();
	Setup.Mode = TEXT("online");
	Setup.bRandomSeed = true;
	OnlineTuning = FTMSettings::Get().Tuning;
	StartMatch(true);
	LastSumTick = Battle.TickCount;

	TSharedRef<FJsonObject> Start = MakeShared<FJsonObject>();
	Start->SetStringField(TEXT("t"), TEXT("start"));
	auto Fill = [this](FJsonObject& Message)
	{
		// Who is in the battle, on which side, and who orders which unit.
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FTMOnlinePlayer& Player : Players)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Player.Name);
			Entry->SetNumberField(TEXT("team"), Player.Team);
			Entry->SetBoolField(TEXT("present"), Player.bPresent);
			List.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Message.SetArrayField(TEXT("players"), List);
		TArray<TSharedPtr<FJsonValue>> Owners;
		for (const int32 Holder : UnitPlayer)
		{
			Owners.Add(MakeShared<FJsonValueNumber>(Holder));
		}
		Message.SetArrayField(TEXT("owners"), Owners);
		Message.SetStringField(TEXT("seed"), FString::Printf(TEXT("%llu"), BattleSeed));
		const FString MapId = UTF8_TO_TCHAR(Setup.MapId.c_str());
		Message.SetStringField(TEXT("map"), MapId);
		// The map file itself, so a map only the host has still plays.
		const FString MapText = LocalFile(TEXT("Maps"), MapId + TEXT(".tmmap.json"));
		if (!MapText.IsEmpty())
		{
			Message.SetStringField(TEXT("map_file"), MapText);
		}
		Message.SetStringField(TEXT("theme"), Setup.ThemeId);
		TArray<TSharedPtr<FJsonValue>> Sides;
		TSharedRef<FJsonObject> Classes = MakeShared<FJsonObject>();
		for (int32 Team = 0; Team < 2; ++Team)
		{
			TArray<TSharedPtr<FJsonValue>> Ids;
			for (int32 Slot = 0; Slot < 4; ++Slot)
			{
				const FString Id = UTF8_TO_TCHAR(Setup.Rosters[Team][Slot].c_str());
				Ids.Add(MakeShared<FJsonValueString>(Id));
				// Classes from files go with it; the six built into the game don't need to.
				const FString ClassText = LocalFile(TEXT("Classes"), Id + TEXT(".tmclass.json"));
				if (!ClassText.IsEmpty())
				{
					Classes->SetStringField(Id, ClassText);
				}
			}
			Sides.Add(MakeShared<FJsonValueArray>(Ids));
		}
		Message.SetArrayField(TEXT("rosters"), Sides);
		Message.SetObjectField(TEXT("classes"), Classes);
		TSharedRef<FJsonObject> Tuning = MakeShared<FJsonObject>();
		for (const TPair<FString, double>& Rule : OnlineTuning)
		{
			Tuning->SetStringField(Rule.Key, DoubleBits(Rule.Value));
		}
		Message.SetObjectField(TEXT("tuning"), Tuning);
		Message.SetStringField(TEXT("capture"), DoubleBits(Setup.CaptureSeconds));
		Message.SetStringField(TEXT("limit"), DoubleBits(Setup.BattleSeconds));
		Message.SetStringField(TEXT("planning"), DoubleBits(Setup.PlanningSeconds));
		// How many watchtowers: with the seed, both games place the same ones.
		Message.SetNumberField(TEXT("towers"), Setup.Watchtowers);
		// Items: the points, and what every unit carries (the host chooses for
		// both sides). The joiner must have the same item files.
		Message.SetNumberField(TEXT("item_budget"), Setup.ItemBudget);
		// Neutral camps (protocol 4): how many, which boss, and what both games must share for them.
		Message.SetNumberField(TEXT("camps"), Setup.CampLevel);
		Message.SetBoolField(TEXT("random_boss"), Setup.bRandomBoss);
		// Element reactions (protocol 5).
		Message.SetBoolField(TEXT("elements"), Setup.bElements);
		// Friendly fire (protocol 9).
		Message.SetBoolField(TEXT("friendly_fire"), Setup.bFriendlyFire);
		// Camp respawns (protocol 11).
		Message.SetBoolField(TEXT("camp_respawn"), Setup.bCampRespawn);
		Message.SetStringField(TEXT("camp_files"), FString::Printf(TEXT("%08x-%08x"), FolderPrint(TEXT("Monsters")), FolderPrint(TEXT("Items"))));
		TArray<TSharedPtr<FJsonValue>> Carried;
		for (int32 Team = 0; Team < 2; ++Team)
		{
			for (int32 Unit = 0; Unit < 4; ++Unit)
			{
				for (int32 Slot = 0; Slot < 3; ++Slot)
				{
					Carried.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Setup.Items[Team][Unit][Slot].c_str())));
				}
			}
		}
		Message.SetArrayField(TEXT("items"), Carried);
	};
	Fill(*Start);
	// Each player is told which of the list they are.
	for (int32 i = 1; i < Players.Num(); ++i)
	{
		if (Players[i].bPresent && Net.IsValid())
		{
			Start->SetNumberField(TEXT("you"), i);
			Net->SendTo(Players[i].Peer, Start);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("ONLINE: started as host, seed %llu, %d players"), BattleSeed, Players.Num());
}

FString ATMBattleDirector::StartOnlineFrom(const FJsonObject& Start)
{
	// net.gd:224-247. Everything the host sends is checked before it reaches
	// the rules: a class or map file goes through the same reader a file on
	// disk does.
	int32 You = -1;
	Start.TryGetNumberField(TEXT("you"), You);
	FString SeedText;
	Start.TryGetStringField(TEXT("seed"), SeedText);
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Owners = nullptr;
	if (!SeedText.IsNumeric() || !Start.TryGetArrayField(TEXT("players"), List) || !List->IsValidIndex(You) || You < 1
		|| !Start.TryGetArrayField(TEXT("owners"), Owners) || Owners->Num() != 8)
	{
		return TEXT("The host's battle couldn't be read.");
	}
	TArray<FTMOnlinePlayer> NextPlayers;
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		FTMOnlinePlayer Player;
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (Value->TryGetObject(Entry))
		{
			(*Entry)->TryGetStringField(TEXT("name"), Player.Name);
			(*Entry)->TryGetNumberField(TEXT("team"), Player.Team);
			(*Entry)->TryGetBoolField(TEXT("present"), Player.bPresent);
		}
		Player.Team = FMath::Clamp(Player.Team, 0, 1);
		NextPlayers.Add(Player);
	}
	TArray<int32> NextOwners;
	for (const TSharedPtr<FJsonValue>& Value : *Owners)
	{
		const int32 Holder = static_cast<int32>(Value->AsNumber());
		NextOwners.Add(NextPlayers.IsValidIndex(Holder) ? Holder : -1);
	}
	const int32 Team = NextPlayers[You].Team;

	FString MapId;
	Start.TryGetStringField(TEXT("map"), MapId);
	FString MapText;
	if (Start.TryGetStringField(TEXT("map_file"), MapText))
	{
		TMSim::FMapDef Map;
		std::string Refused = TMSim::ReadMapFile(TCHAR_TO_UTF8(*MapText), Map);
		if (Refused.empty())
		{
			Refused = TMSim::RegisterMap(Map);
		}
		if (!Refused.empty())
		{
			return FString::Printf(TEXT("The host's map can't be played: %hs"), Refused.c_str());
		}
	}
	else if (!TMSim::HasMap(TCHAR_TO_UTF8(*MapId)))
	{
		return FString::Printf(TEXT("The host's map %s isn't in this game."), *MapId);
	}

	// Classes: one this game doesn't have is loaded; one it has must be the same file.
	const TSharedPtr<FJsonObject>* Classes = nullptr;
	if (Start.TryGetObjectField(TEXT("classes"), Classes))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Classes)->Values)
		{
			FString Text = Entry.Value->AsString();
			Text.ReplaceInline(TEXT("\r"), TEXT(""));
			if (TMSim::FindJob(TCHAR_TO_UTF8(*Entry.Key)))
			{
				if (LocalFile(TEXT("Classes"), Entry.Key + TEXT(".tmclass.json")) != Text)
				{
					return FString::Printf(TEXT("The host's %s is not the same as yours: both players need the same class files."), *Entry.Key);
				}
				continue;
			}
			const std::string Refused = TMSim::LoadClassFile(TCHAR_TO_UTF8(*Text));
			if (!Refused.empty())
			{
				return FString::Printf(TEXT("The host's class %s can't be used: %hs"), *Entry.Key, Refused.c_str());
			}
		}
	}

	FMatchSetup Next = Setup;
	const TArray<TSharedPtr<FJsonValue>>* Sides = nullptr;
	if (!Start.TryGetArrayField(TEXT("rosters"), Sides) || Sides->Num() != 2)
	{
		return TEXT("The host's teams couldn't be read.");
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const TArray<TSharedPtr<FJsonValue>>& Ids = (*Sides)[Side]->AsArray();
		if (Ids.Num() != 4)
		{
			return TEXT("The host's teams couldn't be read.");
		}
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			const std::string Id = TCHAR_TO_UTF8(*Ids[Slot]->AsString());
			if (!TMSim::FindJob(Id))
			{
				return FString::Printf(TEXT("The host's team has a class this game doesn't: %hs."), Id.c_str());
			}
			Next.Rosters[Side][Slot] = Id;
		}
	}

	OnlineTuning.Reset();
	const TSharedPtr<FJsonObject>* Tuning = nullptr;
	if (Start.TryGetObjectField(TEXT("tuning"), Tuning))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Rule : (*Tuning)->Values)
		{
			double Value = 0.0;
			if (FromBits(Rule.Value->AsString(), Value))
			{
				OnlineTuning.Add(Rule.Key, Value);
			}
		}
	}
	FString Bits;
	double Seconds = 0.0;
	Next.CaptureSeconds = Start.TryGetStringField(TEXT("capture"), Bits) && FromBits(Bits, Seconds) ? Seconds : 0.0;
	Next.BattleSeconds = Start.TryGetStringField(TEXT("limit"), Bits) && FromBits(Bits, Seconds) ? Seconds : 0.0;
	Next.PlanningSeconds = Start.TryGetStringField(TEXT("planning"), Bits) && FromBits(Bits, Seconds) ? Seconds : 0.0;
	// A host from before watchtowers sends none, and gets none.
	int32 Towers = 0;
	Next.Watchtowers = Start.TryGetNumberField(TEXT("towers"), Towers) ? FMath::Clamp(Towers, 0, 8) : 0;
	// Items: a host from before items sends none, and its battle has none.
	int32 ItemBudget = 0;
	Next.ItemBudget = Start.TryGetNumberField(TEXT("item_budget"), ItemBudget) ? FMath::Clamp(ItemBudget, 0, 20) : 0;
	// Camps: a host from before them sends none, and its battle has none.
	int32 CampLevel = 0;
	Next.CampLevel = Start.TryGetNumberField(TEXT("camps"), CampLevel) ? FMath::Clamp(CampLevel, 0, 3) : 0;
	bool bRandomBoss = false;
	Next.bRandomBoss = Start.TryGetBoolField(TEXT("random_boss"), bRandomBoss) && bRandomBoss;
	bool bElements = false;
	Next.bElements = Start.TryGetBoolField(TEXT("elements"), bElements) && bElements;
	bool bFriendlyFire = false;
	Next.bFriendlyFire = Start.TryGetBoolField(TEXT("friendly_fire"), bFriendlyFire) && bFriendlyFire;
	bool bCampRespawn = false;
	Next.bCampRespawn = Start.TryGetBoolField(TEXT("camp_respawn"), bCampRespawn) && bCampRespawn;
	if (Next.CampLevel > 0)
	{
		FString Theirs;
		const FString Ours = FString::Printf(TEXT("%08x-%08x"), FolderPrint(TEXT("Monsters")), FolderPrint(TEXT("Items")));
		if (!Start.TryGetStringField(TEXT("camp_files"), Theirs) || Theirs != Ours)
		{
			return TEXT("The host's monsters or items are not the same as yours: with neutral camps on, both players need the same monster and item files.");
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Carried = nullptr;
	for (int32 Code = 0; Code < 24; ++Code)
	{
		Next.Items[Code / 12][(Code % 12) / 3][Code % 3].clear();
	}
	if (Start.TryGetArrayField(TEXT("items"), Carried) && Carried->Num() == 24)
	{
		for (int32 Code = 0; Code < 24; ++Code)
		{
			const std::string Id = TCHAR_TO_UTF8(*(*Carried)[Code]->AsString());
			if (!Id.empty() && !TMSim::FindItem(Id))
			{
				return FString::Printf(TEXT("The host's item %hs isn't in this game: both players need the same item files."), Id.c_str());
			}
			Next.Items[Code / 12][(Code % 12) / 3][Code % 3] = Id;
		}
	}
	Next.MapId = TCHAR_TO_UTF8(*MapId);
	Start.TryGetStringField(TEXT("theme"), Next.ThemeId);
	Next.Mode = TEXT("online");
	Next.bRandomSeed = false;
	Next.FixedSeed = FCString::Strtoui64(*SeedText, nullptr, 10);

	if (Setup.Mode != TEXT("online"))
	{
		OfflineSetup = Setup;
	}
	Setup = Next;
	bOnline = true;
	bOnlineHost = false;
	Players = MoveTemp(NextPlayers);
	UnitPlayer = MoveTemp(NextOwners);
	LocalPlayer = You;
	LocalTeam = Team;
	bWaitingForHost = false;
	bWantRematch = false;
	bOpponentWantsRematch = false;
	OnlineStopped.Reset();
	HostSums.Reset();
	StartMatch(false);
	LogNote(FString::Printf(TEXT("Press %s to chat."), *FTMSettings::Get().KeyName(ETMAction::Chat)));
	UE_LOG(LogTemp, Log, TEXT("ONLINE: started as joiner, seed %llu"), BattleSeed);
	return FString();
}

// ---------------------------------------------------------------- playing

FString ATMBattleDirector::RefereeCheck(const TMSim::FOrder& Order, int32 Player) const
{
	// battle.gd:634-646: the host's own checks on top of the rules'.
	if (!Players.IsValidIndex(Player) || Player == 0)
	{
		return TEXT("You aren't in this battle.");
	}
	if (Order.Type == TMSim::EOrderType::Advance || Order.Type == TMSim::EOrderType::Tune)
	{
		return TEXT("Only the host moves time forward or changes the rules.");
	}
	const std::string Refused = const_cast<TMSim::FBattle&>(Battle).Validate(Order);
	if (!Refused.empty())
	{
		return UTF8_TO_TCHAR(Refused.c_str());
	}
	if (Order.Type == TMSim::EOrderType::Ready)
	{
		return Order.Team != Players[Player].Team ? TEXT("That isn't your side.") : TEXT("");
	}
	// The side's stash is shared: any of its players may put an item on any of its units.
	if (Order.Type == TMSim::EOrderType::Equip)
	{
		const TMSim::FUnit* Wearer = const_cast<TMSim::FBattle&>(Battle).FindUnit(Order.UnitId);
		return !Wearer || Wearer->Team != Players[Player].Team ? TEXT("That isn't your side's unit.") : TEXT("");
	}
	// Only the player's own units: not a teammate's, the other side's, or the monsters, which the host plays.
	const TMSim::FUnit* Unit = const_cast<TMSim::FBattle&>(Battle).FindUnit(Order.UnitId);
	return !Unit || UnitOwner(*Unit) != Player ? TEXT("That isn't your unit.") : TEXT("");
}

void ATMBattleDirector::AfterOnlineApply(const TMSim::FOrder& Order)
{
	if (bOnlineHost)
	{
		// battle.gd:650-655: every so often, where the battle stands.
		if (Order.Type == TMSim::EOrderType::Advance && Battle.TickCount - LastSumTick >= ChecksumEveryTicks)
		{
			LastSumTick = Battle.TickCount;
			const uint64 Sum = Battle.Checksum();
			const int32 Tick = Battle.TickCount;
			SendOnline(TEXT("sum"), [Sum, Tick](FJsonObject& Message)
			{
				Message.SetNumberField(TEXT("tick"), Tick);
				// As text: a JSON number is a double, and a checksum is 64 bits.
				Message.SetStringField(TEXT("value"), FString::Printf(TEXT("%llu"), Sum));
			});
		}
	}
	else
	{
		CheckHostSums();
	}
}

void ATMBattleDirector::CheckHostSums()
{
	// battle.gd:657-671: each of the host's checksums, once this game is at its tick.
	for (auto It = HostSums.CreateIterator(); It; ++It)
	{
		if (It.Key() > Battle.TickCount)
		{
			continue;
		}
		if (It.Key() == Battle.TickCount && It.Value() != Battle.Checksum())
		{
			const float Seconds = It.Key() / static_cast<float>(TMSim::Pace::TicksPerSecond);
			It.RemoveCurrent();
			ReportOutOfSync(FString::Printf(TEXT("the two games no longer match at %.1f s"), Seconds));
			return;
		}
		It.RemoveCurrent();
	}
}

void ATMBattleDirector::ReportOutOfSync(const FString& Detail)
{
	// Both stop: neither plays on alone in a battle the other isn't in (net.gd:120-124).
	SendOnline(TEXT("desync"), [&Detail](FJsonObject& Message) { Message.SetStringField(TEXT("detail"), Detail); });
	StopOnline(TEXT("Out of sync: ") + Detail);
}

void ATMBattleDirector::StopOnline(const FString& Why)
{
	if (!OnlineStopped.IsEmpty() || Battle.Winner != -1)
	{
		return;
	}
	OnlineStopped = Why;
	LogNote(Why);
	CancelAim();
	DecidedFor = 0.0f;
	UE_LOG(LogTemp, Log, TEXT("ONLINE STOPPED: %s"), *Why);
}

void ATMBattleDirector::RequestRematch()
{
	// With up to four players, a rematch is the lobby again: the host takes
	// everyone back to change sides, classes or rules, then starts once more.
	if (!Net.IsValid())
	{
		Tell(TEXT("The host has left."));
		return;
	}
	if (Net->IsHost())
	{
		BackToLobby();
		return;
	}
	Tell(TEXT("The host takes everyone back to the lobby."));
}
