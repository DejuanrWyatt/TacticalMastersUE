// The online lobby (Docs/design/feat-lobby.md): up to four players, each on
// their own machine, choose their side before a battle, and each picks the
// classes of the units they will order. The human asked for it on 2026-10-01:
// "Players should be able to choose their own team in lobby. I would also like
// to add up 4 players."
//
// The host keeps the lobby and is its referee, as it is the battle's: players
// ask (a side, ready, a class for a slot), the host checks and tells everyone
// how the lobby now stands. A side's four units are shared out among the
// players on it, in order of joining: one player orders all four, two order
// two each, three order two, one and one. A side nobody joins is played by
// the computer, on the host's machine, as the monsters are.
//
// Nothing here is read by the rules: they still know two sides of four units.

#include "TMBattleDirector.h"

#include "HAL/PlatformProcess.h"
#include "Misc/App.h"

#include "SimAbility.h"
#include "SimMap.h"

namespace TMLobby
{
	/** A side's slot code: team * 4 + slot, as the setup screen counts them. */
	constexpr int32 Slots = 4;
}

// ---------------------------------------------------------------- who is who

FString ATMBattleDirector::LocalName() const
{
	// The computer's name on the network: different on every machine, and already set.
	FString Name = FPlatformProcess::ComputerName();
	Name = Name.Left(20);
	return Name.IsEmpty() ? FString(TEXT("Player")) : Name;
}

int32 ATMBattleDirector::PlayerOfPeer(int32 Peer) const
{
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		if (Players[i].Peer == Peer)
		{
			return i;
		}
	}
	return -1;
}

int32 ATMBattleDirector::SlotOwner(int32 Team, int32 Slot) const
{
	// The players on the side, in order of joining, share its four slots in turn.
	TArray<int32, TInlineAllocator<4>> OnSide;
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		if (Players[i].Team == Team && Players[i].bPresent)
		{
			OnSide.Add(i);
		}
	}
	if (OnSide.Num() == 0 || Slot < 0 || Slot >= TMLobby::Slots)
	{
		return -1;
	}
	return OnSide[Slot * OnSide.Num() / TMLobby::Slots];
}

int32 ATMBattleDirector::UnitOwner(const TMSim::FUnit& Unit) const
{
	return UnitPlayer.IsValidIndex(Unit.Id) ? UnitPlayer[Unit.Id] : -1;
}

bool ATMBattleDirector::ComputerPlaysUnit(const TMSim::FUnit& Unit) const
{
	// A pet (2026-10-02) is always the computer's, for the side that called it;
	// online, the host's computer plays it, as it does a unit nobody holds.
	if (Unit.PetOf >= 0)
	{
		return !bOnline || bOnlineHost;
	}
	if (!bOnline || Unit.Team == 2 || !UnitPlayer.IsValidIndex(Unit.Id))
	{
		return ComputerPlays(Unit.Team);
	}
	const int32 Holder = UnitPlayer[Unit.Id];
	// Nobody's (an empty side, or a player who left): the host's computer plays it.
	if (Holder < 0)
	{
		return bOnlineHost;
	}
	// The online test (-tmnetbots): the computer plays this machine's own.
	return bNetBots && Holder == LocalPlayer;
}

void ATMBattleDirector::AssignUnits()
{
	// The rules number a battle's units 0-3 blue, 4-7 red (BuildBattle).
	UnitPlayer.Init(-1, 2 * TMLobby::Slots);
	for (int32 Team = 0; Team < 2; ++Team)
	{
		for (int32 Slot = 0; Slot < TMLobby::Slots; ++Slot)
		{
			UnitPlayer[Team * TMLobby::Slots + Slot] = SlotOwner(Team, Slot);
		}
	}
}

FString ATMBattleDirector::SideNames(int32 Team) const
{
	TArray<FString> Names;
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		if (Players[i].Team == Team && Players[i].bPresent)
		{
			Names.Add(i == LocalPlayer ? FString(TEXT("you")) : Players[i].Name);
		}
	}
	return Names.Num() > 0 ? FString::Join(Names, TEXT(" and ")) : FString(TEXT("the computer"));
}

// ---------------------------------------------------------------- the host's lobby

void ATMBattleDirector::OpenLobby()
{
	Screen = EScreen::Lobby;
	PickerSlot = -1;
	ItemPickerSlot = -1;
	bMenuOpen = false;
	bPaused = false;
}

void ATMBattleDirector::StartLobby()
{
	// The host is the first player, on blue, ready by being the one who starts it.
	Players.Reset();
	FTMOnlinePlayer Me;
	Me.Peer = -1;
	Me.Name = LocalName();
	Me.Team = 0;
	Me.bReady = true;
	Players.Add(Me);
	LocalPlayer = 0;
	LocalTeam = 0;
	if (Net.IsValid())
	{
		Net->bAccepting = true;
	}
	OpenLobby();
}

void ATMBattleDirector::LobbyArrive(int32 Peer, const FString& Name)
{
	if (Players.Num() >= 4)
	{
		Net->Kick(Peer, TEXT("That game is full: four players already."));
		return;
	}
	// On the side with fewer players, blue if they are even.
	int32 Count[2] = { 0, 0 };
	for (const FTMOnlinePlayer& Player : Players)
	{
		++Count[FMath::Clamp(Player.Team, 0, 1)];
	}
	FTMOnlinePlayer Joined;
	Joined.Peer = Peer;
	Joined.Name = Name.TrimStartAndEnd().Left(20);
	if (Joined.Name.IsEmpty())
	{
		Joined.Name = FString::Printf(TEXT("Player %d"), Players.Num() + 1);
	}
	Joined.Team = Count[1] < Count[0] ? 1 : 0;
	Players.Add(Joined);
	OnlineStatus = FString::Printf(TEXT("%s joined."), *Joined.Name);
	UE_LOG(LogTemp, Log, TEXT("LOBBY: %s joined (peer %d) on %s"), *Joined.Name, Peer, Joined.Team == 0 ? TEXT("blue") : TEXT("red"));
	BroadcastLobby();
}

void ATMBattleDirector::LobbyDepart(int32 Peer, const FString& Why)
{
	const int32 Gone = PlayerOfPeer(Peer);
	if (Gone < 0)
	{
		return;
	}
	const FString Name = Players[Gone].Name;
	if (Screen == EScreen::Battle && bOnline)
	{
		// Mid-battle: the computer takes over their units, and the others play on.
		Players[Gone].bPresent = false;
		for (int32& Holder : UnitPlayer)
		{
			if (Holder == Gone)
			{
				Holder = -1;
			}
		}
		LogNote(FString::Printf(TEXT("%s left: the computer plays their units."), *Name));
		Tell(FString::Printf(TEXT("%s left: the computer plays their units."), *Name));
		SendOnline(TEXT("left"), [Gone](FJsonObject& Message) { Message.SetNumberField(TEXT("player"), Gone); });
		return;
	}
	Players.RemoveAt(Gone);
	OnlineStatus = FString::Printf(TEXT("%s left."), *Name);
	UE_LOG(LogTemp, Log, TEXT("LOBBY: %s left (%s)"), *Name, *Why);
	if (Screen == EScreen::Draft)
	{
		// The draft was made for the players who were there: start it again from the lobby.
		Tell(FString::Printf(TEXT("%s left: back to the lobby."), *Name));
		BackToLobby();
		return;
	}
	BroadcastLobby();
}

void ATMBattleDirector::BroadcastLobby()
{
	if (!Net.IsValid() || !Net->IsHost())
	{
		return;
	}
	// The draft, if one is on, is told separately (TMBattleDirectorDraft.cpp).
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("t"), TEXT("lobby"));
	TArray<TSharedPtr<FJsonValue>> List;
	for (const FTMOnlinePlayer& Player : Players)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Player.Name);
		Entry->SetNumberField(TEXT("team"), Player.Team);
		Entry->SetBoolField(TEXT("ready"), Player.bReady);
		List.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Message->SetArrayField(TEXT("players"), List);
	TArray<TSharedPtr<FJsonValue>> Sides;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		TArray<TSharedPtr<FJsonValue>> Ids;
		for (int32 Slot = 0; Slot < TMLobby::Slots; ++Slot)
		{
			Ids.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Setup.Rosters[Team][Slot].c_str())));
		}
		Sides.Add(MakeShared<FJsonValueArray>(Ids));
	}
	Message->SetArrayField(TEXT("rosters"), Sides);
	Message->SetStringField(TEXT("map"), UTF8_TO_TCHAR(TMSim::FindMap(Setup.MapId).Name.c_str()));
	Message->SetStringField(TEXT("rules"), LobbyRules());
	Message->SetBoolField(TEXT("draft"), Setup.bDraft);
	Message->SetBoolField(TEXT("unique"), Setup.bUniqueClasses);
	Message->SetNumberField(TEXT("draft_seconds"), Setup.DraftSeconds);
	// Each player is told which of the list they are.
	for (int32 i = 1; i < Players.Num(); ++i)
	{
		Message->SetNumberField(TEXT("you"), i);
		Net->SendTo(Players[i].Peer, Message);
	}
}

FString ATMBattleDirector::LobbyRules() const
{
	// One line of what the host has set, for the players who can't see the setup screen.
	TArray<FString> Parts;
	Parts.Add(UTF8_TO_TCHAR(TMSim::FindMap(Setup.MapId).Name.c_str()));
	if (Setup.Watchtowers > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d watchtowers"), Setup.Watchtowers));
	}
	if (Setup.bUniqueClasses)
	{
		Parts.Add(TEXT("one of each class"));
	}
	if (Setup.CampLevel > 0)
	{
		Parts.Add(Setup.CampLevel == 1 ? TEXT("light camps") : Setup.CampLevel == 2 ? TEXT("standard camps") : TEXT("wild camps"));
		if (Setup.bCampRespawn)
		{
			Parts.Add(TEXT("camps respawn"));
		}
		if (Setup.bBossHunt)
		{
			Parts.Add(TEXT("bosses hunt"));
		}
		if (Setup.bBossClaim)
		{
			Parts.Add(TEXT("claim the boss"));
		}
	}
	if (Setup.CaptureSeconds > 0.0)
	{
		Parts.Add(FString::Printf(TEXT("hold the middle %.0f s"), Setup.CaptureSeconds));
	}
	if (Setup.BattleSeconds > 0.0)
	{
		Parts.Add(FString::Printf(TEXT("%.0f min limit"), Setup.BattleSeconds / 60.0));
	}
	if (Setup.ItemBudget > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d item points"), Setup.ItemBudget));
	}
	if (Setup.bFriendlyFire)
	{
		Parts.Add(TEXT("friendly fire"));
	}
	return FString::Join(Parts, TEXT(" · "));
}

bool ATMBattleDirector::LobbyCanStart(FString* WhyNot) const
{
	auto Say = [WhyNot](const TCHAR* Why) { if (WhyNot) { *WhyNot = Why; } return false; };
	if (Players.Num() < 2)
	{
		return Say(TEXT("Waiting for players to join."));
	}
	for (int32 i = 1; i < Players.Num(); ++i)
	{
		if (!Players[i].bReady)
		{
			return Say(TEXT("Waiting for every player to be ready."));
		}
	}
	return true;
}

void ATMBattleDirector::LobbyStart()
{
	FString WhyNot;
	if (!IsHostInLobby() || !LobbyCanStart(&WhyNot))
	{
		if (!WhyNot.IsEmpty())
		{
			Tell(WhyNot);
		}
		return;
	}
	if (Setup.bDraft && !bDraftDone)
	{
		StartDraft();
		return;
	}
	StartOnlineAsHost();
}

bool ATMBattleDirector::IsHostInLobby() const
{
	return Net.IsValid() && Net->IsHost() && Screen == EScreen::Lobby;
}

void ATMBattleDirector::BackToLobby()
{
	// After a battle: everyone back to the lobby, to change sides, classes or rules.
	if (!Net.IsValid() || !Net->IsHost())
	{
		return;
	}
	for (int32 i = Players.Num() - 1; i >= 1; --i)
	{
		if (!Players[i].bPresent)
		{
			Players.RemoveAt(i);
		}
	}
	for (int32 i = 1; i < Players.Num(); ++i)
	{
		Players[i].bReady = false;
	}
	bOnline = false;
	bDraftDone = false;
	OnlineStopped.Reset();
	Net->bAccepting = true;
	OpenLobby();
	BroadcastLobby();
}

// ---------------------------------------------------------------- asking the host

void ATMBattleDirector::LobbySide(int32 Team)
{
	Team = FMath::Clamp(Team, 0, 1);
	if (Net.IsValid() && Net->IsHost())
	{
		if (Players.IsValidIndex(0) && Players[0].Team != Team)
		{
			Players[0].Team = Team;
			LocalTeam = Team;
			BroadcastLobby();
		}
		return;
	}
	SendOnline(TEXT("side"), [Team](FJsonObject& Message) { Message.SetNumberField(TEXT("team"), Team); });
}

void ATMBattleDirector::LobbyReady()
{
	if (!Players.IsValidIndex(LocalPlayer) || (Net.IsValid() && Net->IsHost()))
	{
		return;
	}
	const bool bReady = !Players[LocalPlayer].bReady;
	SendOnline(TEXT("ready"), [bReady](FJsonObject& Message) { Message.SetBoolField(TEXT("on"), bReady); });
}

void ATMBattleDirector::LobbyPick(int32 SlotCode, const std::string& JobId)
{
	if (SlotCode < 0 || SlotCode >= 2 * TMLobby::Slots || !TMSim::FindJob(JobId))
	{
		return;
	}
	if (Net.IsValid() && Net->IsHost())
	{
		LobbyApplyPick(0, SlotCode, JobId);
		return;
	}
	SendOnline(TEXT("pick"), [SlotCode, &JobId](FJsonObject& Message)
	{
		Message.SetNumberField(TEXT("slot"), SlotCode);
		Message.SetStringField(TEXT("class"), UTF8_TO_TCHAR(JobId.c_str()));
	});
}

bool ATMBattleDirector::LobbyMayPick(int32 Player, int32 SlotCode) const
{
	if (SlotCode < 0 || SlotCode >= 2 * TMLobby::Slots)
	{
		return false;
	}
	const int32 Holder = SlotOwner(SlotCode / TMLobby::Slots, SlotCode % TMLobby::Slots);
	// Your own slots; the host also fills the computer's.
	return Holder == Player || (Holder < 0 && Player == 0);
}

bool ATMBattleDirector::ClassTaken(const std::string& JobId, int32 Team, int32 Slot) const
{
	for (int32 T = 0; T < 2; ++T)
	{
		for (int32 S = 0; S < 4; ++S)
		{
			if ((T != Team || S != Slot) && Setup.Rosters[T][S] == JobId)
			{
				return true;
			}
		}
	}
	return false;
}

void ATMBattleDirector::DedupeRosters()
{
	if (!Setup.bUniqueClasses)
	{
		return;
	}
	TSet<FString> Seen;
	for (int32 T = 0; T < 2; ++T)
	{
		for (int32 S = 0; S < 4; ++S)
		{
			std::string& Held = Setup.Rosters[T][S];
			if (!Seen.Contains(UTF8_TO_TCHAR(Held.c_str())))
			{
				Seen.Add(UTF8_TO_TCHAR(Held.c_str()));
				continue;
			}
			// A class of the same first role nobody has, else any nobody has.
			const TMSim::FJobDef* Was = TMSim::FindJob(Held);
			const std::string FirstRole = Was && !Was->Roles.empty() ? Was->Roles[0] : std::string();
			const TMSim::FJobDef* Instead = nullptr;
			for (int32 Pass = 0; Pass < 2 && !Instead; ++Pass)
			{
				for (const TMSim::FJobDef* Job : TMSim::AllJobs())
				{
					if (!Seen.Contains(UTF8_TO_TCHAR(Job->Id.c_str())) && !ClassTaken(Job->Id, T, S)
						&& (Pass == 1 || FirstRole.empty() || TMSim::JobHasRole(Job->Id, FirstRole)))
					{
						Instead = Job;
						break;
					}
				}
			}
			if (Instead)
			{
				Held = Instead->Id;
				Seen.Add(UTF8_TO_TCHAR(Held.c_str()));
			}
		}
	}
}

void ATMBattleDirector::LobbyApplyPick(int32 Player, int32 SlotCode, const std::string& JobId)
{
	if (Setup.bDraft || !LobbyMayPick(Player, SlotCode) || !TMSim::FindJob(JobId)
		|| (Setup.bUniqueClasses && ClassTaken(JobId, SlotCode / TMLobby::Slots, SlotCode % TMLobby::Slots)))
	{
		return;
	}
	Setup.Rosters[SlotCode / TMLobby::Slots][SlotCode % TMLobby::Slots] = JobId;
	BuildBattle();
	BroadcastLobby();
}

// ---------------------------------------------------------------- the messages

bool ATMBattleDirector::OnLobbyMessage(const FString& Kind, const FJsonObject& Message, int32 From)
{
	const bool bHost = Net->IsHost();
	if (bHost)
	{
		const int32 Player = PlayerOfPeer(From);
		if (Player < 0 || Screen != EScreen::Lobby)
		{
			return Kind == TEXT("side") || Kind == TEXT("ready") || Kind == TEXT("pick");
		}
		if (Kind == TEXT("side"))
		{
			int32 Team = 0;
			Message.TryGetNumberField(TEXT("team"), Team);
			Players[Player].Team = FMath::Clamp(Team, 0, 1);
			Players[Player].bReady = false;
			BroadcastLobby();
			return true;
		}
		if (Kind == TEXT("ready"))
		{
			bool bOn = false;
			Message.TryGetBoolField(TEXT("on"), bOn);
			Players[Player].bReady = bOn;
			BroadcastLobby();
			// The online test starts as soon as it can.
			if (bNetBots && LobbyCanStart())
			{
				LobbyStart();
			}
			return true;
		}
		if (Kind == TEXT("pick"))
		{
			int32 Slot = -1;
			FString Job;
			Message.TryGetNumberField(TEXT("slot"), Slot);
			Message.TryGetStringField(TEXT("class"), Job);
			LobbyApplyPick(Player, Slot, TCHAR_TO_UTF8(*Job));
			return true;
		}
		return false;
	}

	if (Kind == TEXT("lobby"))
	{
		ApplyLobby(Message);
		return true;
	}
	if (Kind == TEXT("left"))
	{
		int32 Gone = -1;
		Message.TryGetNumberField(TEXT("player"), Gone);
		if (Players.IsValidIndex(Gone))
		{
			Players[Gone].bPresent = false;
			for (int32& Holder : UnitPlayer)
			{
				if (Holder == Gone)
				{
					Holder = -1;
				}
			}
			LogNote(FString::Printf(TEXT("%s left: the computer plays their units."), *Players[Gone].Name));
		}
		return true;
	}
	return false;
}

void ATMBattleDirector::ApplyLobby(const FJsonObject& Message)
{
	int32 You = 0;
	Message.TryGetNumberField(TEXT("you"), You);
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Message.TryGetArrayField(TEXT("players"), List) || !List->IsValidIndex(You))
	{
		return;
	}
	Players.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!Value->TryGetObject(Entry))
		{
			continue;
		}
		FTMOnlinePlayer Player;
		(*Entry)->TryGetStringField(TEXT("name"), Player.Name);
		(*Entry)->TryGetNumberField(TEXT("team"), Player.Team);
		Player.Team = FMath::Clamp(Player.Team, 0, 1);
		(*Entry)->TryGetBoolField(TEXT("ready"), Player.bReady);
		Players.Add(Player);
	}
	LocalPlayer = FMath::Clamp(You, 0, Players.Num() - 1);
	LocalTeam = Players.IsValidIndex(LocalPlayer) ? Players[LocalPlayer].Team : 1;
	const TArray<TSharedPtr<FJsonValue>>* Sides = nullptr;
	if (Message.TryGetArrayField(TEXT("rosters"), Sides) && Sides->Num() == 2)
	{
		for (int32 Team = 0; Team < 2; ++Team)
		{
			const TArray<TSharedPtr<FJsonValue>>& Ids = (*Sides)[Team]->AsArray();
			for (int32 Slot = 0; Slot < TMLobby::Slots && Slot < Ids.Num(); ++Slot)
			{
				const std::string Id = TCHAR_TO_UTF8(*Ids[Slot]->AsString());
				if (TMSim::FindJob(Id))
				{
					Setup.Rosters[Team][Slot] = Id;
				}
			}
		}
	}
	Message.TryGetStringField(TEXT("map"), LobbyMap);
	Message.TryGetStringField(TEXT("rules"), LobbyRulesLine);
	Message.TryGetBoolField(TEXT("draft"), Setup.bDraft);
	Message.TryGetBoolField(TEXT("unique"), Setup.bUniqueClasses);
	Message.TryGetNumberField(TEXT("draft_seconds"), Setup.DraftSeconds);
	OnlineStatus.Reset();
	Setup.Mode = TEXT("online");
	if (Screen != EScreen::Lobby)
	{
		// Joined, or back from a battle.
		bOnline = false;
		OnlineStopped.Reset();
		OpenLobby();
	}
	// The online test: ready at once.
	if (bNetBots && Players.IsValidIndex(LocalPlayer) && !Players[LocalPlayer].bReady)
	{
		LobbyReady();
	}
}
