// The draft (Docs/design/feat-lobby.md): before an online battle the two sides
// take turns banning and picking classes, as League of Legends drafts its
// champions. The human chose it on 2026-10-01: phases as League has them, a
// class taken or banned gone for everyone, and a pick timer the host sets.
//
//   bans    Blue, Red, Blue, Red              (two each)
//   picks   Blue, Red, Red, Blue              (two each, serpentine)
//   bans    Red, Blue                         (one each)
//   picks   Red, Blue, Blue, Red              (two each, serpentine)
//
// A side's picks fill its slots in order, and each is made by the player who
// will order that unit (TMBattleDirectorLobby.cpp, SlotOwner); a ban by any
// player on the side. The computer drafts for a side nobody plays. The host
// keeps the draft and its clock, checks every choice, and tells everyone.

#include "TMBattleDirector.h"

#include "SimAbility.h"

namespace TMDraftOrder
{
	struct FStep
	{
		bool bBan;
		int32 Team;
	};
	const FStep Steps[] =
	{
		{ true, 0 }, { true, 1 }, { true, 0 }, { true, 1 },
		{ false, 0 }, { false, 1 }, { false, 1 }, { false, 0 },
		{ true, 1 }, { true, 0 },
		{ false, 1 }, { false, 0 }, { false, 0 }, { false, 1 },
	};
	constexpr int32 Count = UE_ARRAY_COUNT(Steps);
	/** How long the computer seems to think over a choice, and how long the finished draft stays up. */
	constexpr float ComputerSeconds = 1.2f;
	constexpr float ShowSeconds = 2.5f;
}

int32 ATMBattleDirector::DraftSteps() const
{
	return TMDraftOrder::Count;
}

bool ATMBattleDirector::DraftStepIsBan(int32 Step) const
{
	return Step >= 0 && Step < TMDraftOrder::Count && TMDraftOrder::Steps[Step].bBan;
}

int32 ATMBattleDirector::DraftStepTeam(int32 Step) const
{
	return Step >= 0 && Step < TMDraftOrder::Count ? TMDraftOrder::Steps[Step].Team : -1;
}

bool ATMBattleDirector::DraftDone() const
{
	return Draft.Step >= TMDraftOrder::Count;
}

bool ATMBattleDirector::DraftUsed(const FString& JobId) const
{
	for (int32 Team = 0; Team < 2; ++Team)
	{
		if (Draft.Bans[Team].Contains(JobId) || Draft.Picks[Team].Contains(JobId))
		{
			return true;
		}
	}
	return false;
}

int32 ATMBattleDirector::DraftChooser(int32 Step) const
{
	// A pick is the slot owner's; a ban any player's on the side (-2); the computer's -1.
	const int32 Team = DraftStepTeam(Step);
	if (Team < 0)
	{
		return -1;
	}
	if (DraftStepIsBan(Step))
	{
		return SlotOwner(Team, 0) < 0 ? -1 : -2;
	}
	return SlotOwner(Team, Draft.Picks[Team].Num());
}

bool ATMBattleDirector::DraftMayChoose(int32 Player) const
{
	if (DraftDone() || !Players.IsValidIndex(Player))
	{
		return false;
	}
	const int32 Chooser = DraftChooser(Draft.Step);
	return Chooser == Player || (Chooser == -2 && Players[Player].Team == DraftStepTeam(Draft.Step));
}

// ---------------------------------------------------------------- the host's draft

void ATMBattleDirector::StartDraft()
{
	Draft = FTMDraftState();
	Draft.Left = Setup.DraftSeconds;
	Draft.Computer = TMDraftOrder::ComputerSeconds;
	bDraftDone = false;
	Net->bAccepting = false;
	Screen = EScreen::Draft;
	PickerRole = -1;
	UE_LOG(LogTemp, Log, TEXT("DRAFT: started, %d s a choice"), Setup.DraftSeconds);
	BroadcastDraft();
}

void ATMBattleDirector::BroadcastDraft()
{
	if (!Net.IsValid() || !Net->IsHost())
	{
		return;
	}
	SendOnline(TEXT("draft"), [this](FJsonObject& Message)
	{
		Message.SetNumberField(TEXT("step"), Draft.Step);
		Message.SetNumberField(TEXT("left"), Draft.Left);
		for (int32 Team = 0; Team < 2; ++Team)
		{
			TArray<TSharedPtr<FJsonValue>> Bans;
			for (const FString& Id : Draft.Bans[Team])
			{
				Bans.Add(MakeShared<FJsonValueString>(Id));
			}
			TArray<TSharedPtr<FJsonValue>> Picks;
			for (const FString& Id : Draft.Picks[Team])
			{
				Picks.Add(MakeShared<FJsonValueString>(Id));
			}
			Message.SetArrayField(Team == 0 ? TEXT("blue_bans") : TEXT("red_bans"), Bans);
			Message.SetArrayField(Team == 0 ? TEXT("blue_picks") : TEXT("red_picks"), Picks);
		}
	});
}

void ATMBattleDirector::DraftApply(int32 Player, int32 Step, const FString& JobId)
{
	// A choice from a player (or this host, or its computer), for this step only.
	if (Step != Draft.Step || DraftDone())
	{
		return;
	}
	const bool bBan = DraftStepIsBan(Step);
	const int32 Team = DraftStepTeam(Step);
	if (Player >= 0 && !DraftMayChoose(Player))
	{
		return;
	}
	if (!JobId.IsEmpty())
	{
		if (DraftUsed(JobId) || !TMSim::FindJob(TCHAR_TO_UTF8(*JobId)))
		{
			return;
		}
	}
	else if (!bBan)
	{
		return;  // a pick can't be skipped
	}
	if (bBan)
	{
		if (!JobId.IsEmpty())
		{
			Draft.Bans[Team].Add(JobId);
		}
		LogNote(JobId.IsEmpty() ? FString::Printf(TEXT("%s let a ban go."), Team == 0 ? TEXT("Blue") : TEXT("Red"))
			: FString::Printf(TEXT("%s bans %hs."), Team == 0 ? TEXT("Blue") : TEXT("Red"), TMSim::FindJob(TCHAR_TO_UTF8(*JobId))->Name.c_str()));
	}
	else
	{
		Draft.Picks[Team].Add(JobId);
	}
	++Draft.Step;
	Draft.Left = Setup.DraftSeconds;
	Draft.Computer = TMDraftOrder::ComputerSeconds;
	if (DraftDone())
	{
		// The classes the draft made are the battle's.
		for (int32 T = 0; T < 2; ++T)
		{
			for (int32 Slot = 0; Slot < 4 && Slot < Draft.Picks[T].Num(); ++Slot)
			{
				Setup.Rosters[T][Slot] = TCHAR_TO_UTF8(*Draft.Picks[T][Slot]);
			}
		}
		Draft.Show = TMDraftOrder::ShowSeconds;
		UE_LOG(LogTemp, Log, TEXT("DRAFT: done"));
	}
	BroadcastDraft();
}

FString ATMBattleDirector::DraftComputerChoice(int32 Team) const
{
	// Something sensible, not clever: a class of a role the side lacks if there
	// is one, else any; a ban, any class at all. Never one already gone.
	const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
	TArray<const TMSim::FJobDef*> Free;
	for (const TMSim::FJobDef* Job : Jobs)
	{
		if (Job && !DraftUsed(UTF8_TO_TCHAR(Job->Id.c_str())))
		{
			Free.Add(Job);
		}
	}
	if (Free.Num() == 0)
	{
		return FString();
	}
	if (!DraftStepIsBan(Draft.Step))
	{
		TSet<FString> Have;
		for (const FString& Id : Draft.Picks[Team])
		{
			if (const TMSim::FJobDef* Job = TMSim::FindJob(TCHAR_TO_UTF8(*Id)))
			{
				for (const std::string& RoleId : Job->Roles)
				{
					Have.Add(UTF8_TO_TCHAR(RoleId.c_str()));
				}
			}
		}
		TArray<const TMSim::FJobDef*> Wanted;
		for (const TMSim::FJobDef* Job : Free)
		{
			for (const std::string& RoleId : Job->Roles)
			{
				if (!Have.Contains(UTF8_TO_TCHAR(RoleId.c_str())))
				{
					Wanted.Add(Job);
					break;
				}
			}
		}
		if (Wanted.Num() > 0)
		{
			return UTF8_TO_TCHAR(Wanted[FMath::RandRange(0, Wanted.Num() - 1)]->Id.c_str());
		}
	}
	return UTF8_TO_TCHAR(Free[FMath::RandRange(0, Free.Num() - 1)]->Id.c_str());
}

void ATMBattleDirector::AdvanceDraft(float DeltaSeconds)
{
	if (Screen != EScreen::Draft)
	{
		return;
	}
	// Everyone counts the clock down to show it; only the host acts on it.
	if (Setup.DraftSeconds > 0 && !DraftDone())
	{
		Draft.Left = FMath::Max(0.0f, Draft.Left - DeltaSeconds);
	}
	if (!Net.IsValid() || !Net->IsHost())
	{
		return;
	}
	if (DraftDone())
	{
		Draft.Show -= DeltaSeconds;
		if (Draft.Show <= 0.0f)
		{
			bDraftDone = true;
			StartOnlineAsHost();
		}
		return;
	}
	const int32 Team = DraftStepTeam(Draft.Step);
	if (DraftChooser(Draft.Step) == -1)
	{
		// The computer's side.
		Draft.Computer -= DeltaSeconds;
		if (Draft.Computer <= 0.0f)
		{
			DraftApply(-1, Draft.Step, DraftComputerChoice(Team));
		}
		return;
	}
	if (Setup.DraftSeconds > 0 && Draft.Left <= 0.0f)
	{
		// Time: a ban is let go, a pick is made for them.
		DraftApply(-1, Draft.Step, DraftStepIsBan(Draft.Step) ? FString() : DraftComputerChoice(Team));
	}
}

// ---------------------------------------------------------------- choosing

void ATMBattleDirector::DraftChoose(const FString& JobId)
{
	if (!DraftMayChoose(LocalPlayer))
	{
		return;
	}
	if (Net.IsValid() && Net->IsHost())
	{
		DraftApply(LocalPlayer, Draft.Step, JobId);
		return;
	}
	const int32 Step = Draft.Step;
	SendOnline(TEXT("draft_choose"), [Step, &JobId](FJsonObject& Message)
	{
		Message.SetNumberField(TEXT("step"), Step);
		Message.SetStringField(TEXT("class"), JobId);
	});
}

bool ATMBattleDirector::OnDraftMessage(const FString& Kind, const FJsonObject& Message, int32 From)
{
	if (Net->IsHost())
	{
		if (Kind != TEXT("draft_choose"))
		{
			return false;
		}
		if (Screen == EScreen::Draft)
		{
			int32 Step = -1;
			FString Job;
			Message.TryGetNumberField(TEXT("step"), Step);
			Message.TryGetStringField(TEXT("class"), Job);
			const int32 Player = PlayerOfPeer(From);
			if (Player >= 0)
			{
				DraftApply(Player, Step, Job);
			}
		}
		return true;
	}
	if (Kind != TEXT("draft"))
	{
		return false;
	}
	Message.TryGetNumberField(TEXT("step"), Draft.Step);
	double Left = 0.0;
	Message.TryGetNumberField(TEXT("left"), Left);
	Draft.Left = static_cast<float>(Left);
	for (int32 Team = 0; Team < 2; ++Team)
	{
		Draft.Bans[Team].Reset();
		Draft.Picks[Team].Reset();
		const TArray<TSharedPtr<FJsonValue>>* Bans = nullptr;
		if (Message.TryGetArrayField(Team == 0 ? TEXT("blue_bans") : TEXT("red_bans"), Bans))
		{
			for (const TSharedPtr<FJsonValue>& Id : *Bans)
			{
				Draft.Bans[Team].Add(Id->AsString());
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Picks = nullptr;
		if (Message.TryGetArrayField(Team == 0 ? TEXT("blue_picks") : TEXT("red_picks"), Picks))
		{
			for (const TSharedPtr<FJsonValue>& Id : *Picks)
			{
				Draft.Picks[Team].Add(Id->AsString());
			}
		}
	}
	if (Screen != EScreen::Draft)
	{
		Screen = EScreen::Draft;
		PickerSlot = -1;
		PickerRole = -1;
	}
	// The online test: choose at once when it is this machine's turn.
	if (bNetBots && DraftMayChoose(LocalPlayer))
	{
		DraftChoose(DraftComputerChoice(LocalTeam));
	}
	return true;
}
