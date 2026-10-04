// The neutral camps as the board shows them: a ring of stones at each camp in
// the colour of its tier (grey until this side has seen the spot), a label over
// it with its name, its temperament and how long until it wakes, and a chest
// where items lie on the ground. And the orders a person gives about items:
// taking one from the ground, leaving one there.
//
// Docs/design/feat-neutral-camps.md 9 and 14. None of this is state: the rules
// (TMSim, SimCamps.cpp) keep the camps; this only draws them.

#include "TMBattleDirector.h"
#include "TMSettings.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "SimAbility.h"
#include "SimItem.h"

namespace
{
	/** Stones round each camp's ring. */
	constexpr int32 RingStones = 12;
	/** The ring's radius, in metres. */
	constexpr float RingMetres = 1.6f;

	/** Easy green, medium blue, hard purple, epic orange; grey unseen. */
	FLinearColor CampColour(int32 Tier)
	{
		switch (Tier)
		{
		case 0: return FLinearColor(0.3f, 0.8f, 0.35f);
		case 1: return FLinearColor(0.25f, 0.55f, 1.0f);
		case 2: return FLinearColor(0.7f, 0.35f, 0.95f);
		case 3: return FLinearColor(1.0f, 0.55f, 0.1f);
		default: return FLinearColor(0.45f, 0.45f, 0.48f);
		}
	}

	FColor LabelColour(int32 Tier)
	{
		return CampColour(Tier).ToFColor(true);
	}

	const TCHAR* TemperamentWord(TMSim::ETemperament Temperament)
	{
		switch (Temperament)
		{
		case TMSim::ETemperament::Docile: return TEXT("docile");
		case TMSim::ETemperament::Skittish: return TEXT("skittish");
		case TMSim::ETemperament::Provoked: return TEXT("fights back");
		case TMSim::ETemperament::Territorial: return TEXT("territorial");
		case TMSim::ETemperament::Aggressive: return TEXT("aggressive");
		case TMSim::ETemperament::GuardPlace: return TEXT("guards its ground");
		case TMSim::ETemperament::GuardUnit: return TEXT("guardian");
		case TMSim::ETemperament::Patrol: return TEXT("patrols");
		}
		return TEXT("");
	}

	FString Clock(int32 Ticks)
	{
		const int32 Seconds = FMath::Max(0, (Ticks + TMSim::Pace::TicksPerSecond - 1) / TMSim::Pace::TicksPerSecond);
		return FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
	}
}

void ATMBattleDirector::BuildCamps()
{
	// The rings', labels' and chests' parts go in BoardProps with the rest of the board,
	// so ClearBattle takes them away; only the lists of them start again here.
	CampRings.Reset();
	CampRingTier.Reset();
	CampSeen.Reset();
	CampLabels.Reset();
	CacheChests.Reset();
	Chests.Reset();
	TakePickerCache = -1;
	SnapUnits.Reset();

	const float M = TileSize;
	for (const TMSim::FCamp& Camp : Battle.Camps)
	{
		const int32 Level = Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Camp.Spot));
		const FVector Middle = WorldFromMetres(Camp.Spot, Level);
		for (int32 k = 0; k < RingStones; ++k)
		{
			const float Angle = 2.0f * PI * k / RingStones;
			const FVector At = Middle + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * RingMetres * M + FVector(0.0f, 0.0f, 6.0f);
			CampRings.Add(Shape(TEXT("Cube"), At, FVector(22.0f, 22.0f, 12.0f), FRotator(0.0f, FMath::RadiansToDegrees(Angle) + 45.0f, 0.0f),
				CampColour(-1)));
		}
		if (Camp.bShrine)
		{
			// The Crag Brute's shrine: a squat stone with a gold top, in the ring's middle.
			Shape(TEXT("Cylinder"), Middle + FVector(0.0f, 0.0f, 30.0f), FVector(60.0f, 60.0f, 60.0f), FRotator::ZeroRotator, FLinearColor(0.35f, 0.33f, 0.3f));
			Shape(TEXT("Cylinder"), Middle + FVector(0.0f, 0.0f, 64.0f), FVector(50.0f, 50.0f, 8.0f), FRotator::ZeroRotator, FLinearColor(0.95f, 0.75f, 0.25f));
		}
		CampRingTier.Add(-2);
		CampSeen.Add(0);

		UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
		Label->SetMobility(EComponentMobility::Movable);
		Label->SetupAttachment(RootComponent);
		Label->RegisterComponent();
		Label->SetWorldSize(26.0f);
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetRelativeLocation(Middle + FVector(0.0f, 0.0f, 120.0f));
		// In a game the HUD draws it, outlined (ATMBattleHud::DrawWorldWords).
		Label->SetHiddenInGame(GetWorld() && GetWorld()->IsGameWorld());
		CampLabels.Add(Label);
		BoardProps.Add(Label);
	}
	RefreshCamps(0.0f);
}

void ATMBattleDirector::RefreshCamps(float DeltaSeconds)
{
	(void)DeltaSeconds;
	if (Battle.Camps.empty() && Battle.Caches.empty())
	{
		return;
	}
	const int32 Viewer = ViewerTeam();
	const bool bAll = Viewer < 0 || Battle.Winner != -1;

	FRotator Towards = FRotator::ZeroRotator;
	if (const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FVector Eye;
		FRotator Look;
		Player->GetPlayerViewPoint(Eye, Look);
		Towards = FRotator(0.0f, Look.Yaw + 180.0f, 0.0f);
	}

	for (int32 i = 0; i < static_cast<int32>(Battle.Camps.size()) && i < CampSeen.Num(); ++i)
	{
		const TMSim::FCamp& Camp = Battle.Camps[static_cast<size_t>(i)];
		// Once a side has seen the spot, it knows the camp's tier for good.
		for (int32 Team = 0; Team < 2; ++Team)
		{
			if (Battle.CanSee(Team, Camp.Spot))
			{
				CampSeen[i] |= static_cast<uint8>(1 << Team);
			}
		}
		const bool bKnown = bAll || (CampSeen[i] & (1 << Viewer)) != 0;
		const int32 Tier = bKnown ? Camp.Tier : -1;
		if (CampRingTier.IsValidIndex(i) && CampRingTier[i] != Tier)
		{
			CampRingTier[i] = Tier;
			for (int32 k = i * RingStones; k < (i + 1) * RingStones && k < CampRings.Num(); ++k)
			{
				if (CampRings[k])
				{
					CampRings[k]->SetMaterial(0, Paint(CampColour(Tier)));
				}
			}
		}

		if (!CampLabels.IsValidIndex(i) || !CampLabels[i])
		{
			continue;
		}
		UTextRenderComponent* Label = CampLabels[i];
		// Its name: the boss's own, or the kind's.
		FString Name;
		const TMSim::FMonsterInfo* Info = nullptr;
		if (!Camp.Members.empty())
		{
			if (const TMSim::FUnit* First = Battle.FindUnit(Camp.Members[0]))
			{
				const TMSim::FJobDef* Job = TMSim::FindJob(First->Job);
				Info = First->MonsterInfo();
				if (Camp.Kind < 0 && Job)
				{
					Name = UTF8_TO_TCHAR(Job->Name.c_str());
				}
			}
		}
		if (Name.IsEmpty() && Camp.Kind >= 0 && Camp.Kind < static_cast<int32>(TMSim::CampKinds().size()))
		{
			Name = UTF8_TO_TCHAR(TMSim::CampKinds()[static_cast<size_t>(Camp.Kind)].Name.c_str());
		}
		if (!bKnown)
		{
			// Unseen: that a camp is there, and its temperament (the design's answer: yes, shown).
			Name = TEXT("Camp");
		}
		FString Line = Name;
		if (Info)
		{
			Line += FString::Printf(TEXT("  (%s)"), TemperamentWord(Info->Temperament));
		}
		bool bShow = true;
		if (Camp.State == TMSim::ECampState::Waiting)
		{
			Line += FString::Printf(TEXT("\nwakes in %s"), *Clock(Camp.Timer));
			// The noise of fights near it (2026-10-02, "Camps and Bosses Mockups" A).
			if (Camp.bNoiseWake)
			{
				Line += TEXT("\nroused by the noise!");
			}
			else if (Camp.Noise > 0)
			{
				FString Meter;
				for (int32 Step = 0; Step < TMSim::Camp::NoiseFull; ++Step)
				{
					Meter += Step < Camp.Noise ? TEXT("#") : TEXT("-");
				}
				Line += FString::Printf(TEXT("\nnoise [%s]"), *Meter);
			}
		}
		else
		{
			// Awake: the label stays while no member of it is in sight; each
			// monster's own line (over its head) says the rest.
			bool bAnySeen = false;
			bool bAlert = false;
			for (const int32 Id : Camp.Members)
			{
				const TMSim::FUnit* Member = Battle.FindUnit(Id);
				if (Member && Member->IsAlive())
				{
					bAnySeen = bAnySeen || IsSeen(*Member);
					bAlert = bAlert || Member->Mind == TMSim::EMind::Alert || Member->Mind == TMSim::EMind::Fighting;
				}
			}
			bShow = !bAnySeen;
			Line += bAlert ? TEXT("  !") : TEXT("");
		}
		if (Camp.bShrine && Camp.ShrineRest > 0)
		{
			Line += FString::Printf(TEXT("\nshrine rests %s"), *Clock(Camp.ShrineRest));
		}
		Label->SetText(FText::FromString(Line));
		Label->SetTextRenderColor(LabelColour(Tier));
		Label->SetVisibility(bShow && Screen == EScreen::Battle);
		Label->SetWorldRotation(Towards);
	}

	// A chest for each heap of items: made when the heap first appears, shown
	// while it holds anything and this side can see the spot.
	while (CacheChests.Num() < static_cast<int32>(Battle.Caches.size()))
	{
		const int32 Index = CacheChests.Num();
		const TMSim::FCache& Cache = Battle.Caches[static_cast<size_t>(Index)];
		const int32 Level = Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Cache.Pos));
		const FVector Foot = WorldFromMetres(Cache.Pos, Level);
		// The Runic Coffer, in the look of the best item in it (TMBattleDirectorChest.cpp).
		int32 Tier = 0;
		for (const TMSim::FItemDef* Item : Cache.Items)
		{
			Tier = FMath::Max(Tier, static_cast<int32>(Item->Tier));
		}
		CacheChests.Add(MakeChest(Foot, Tier, 20.0f + 37.0f * (Index % 5), 101 + Index * 7));
	}
	for (int32 i = 0; i < CacheChests.Num() && i < static_cast<int32>(Battle.Caches.size()); ++i)
	{
		const TMSim::FCache& Cache = Battle.Caches[static_cast<size_t>(i)];
		if (CacheChests[i])
		{
			CacheChests[i]->SetVisibility(!Cache.Items.empty() && IsPointSeen(Cache.Pos), true);
		}
	}
	AdvanceChests(DeltaSeconds);

	// The items panel follows the unit: what lies in reach of it now.
	if (TakePickerCache != -1)
	{
		const TMSim::FUnit* Unit = SelectedUnit();
		if (!Unit || !Unit->IsAlive())
		{
			TakePickerCache = -1;
		}
		else
		{
			const int32 Near = Battle.CacheNear(Unit->Pos);
			TakePickerCache = Near >= 0 ? Near : -2;
		}
	}
}

int32 ATMBattleDirector::TakeableCache(const TMSim::FUnit& Unit, FString* WhyNot) const
{
	const int32 Near = Battle.CacheNear(Unit.Pos);
	if (Near < 0)
	{
		if (WhyNot)
		{
			*WhyNot = TEXT("Walk onto items on the ground to pick them up.");
		}
		return -1;
	}
	// Whether the rules would take something from it now, into the side's stash.
	std::string Refused = "nothing there";
	for (const TMSim::FItemDef* Item : Battle.Caches[static_cast<size_t>(Near)].Items)
	{
		Refused = Battle.ValidateTake(Unit.Id, Near, Item->Id, -1);
		if (Refused.empty())
		{
			return Near;
		}
	}
	if (WhyNot)
	{
		*WhyNot = UTF8_TO_TCHAR(Refused.c_str());
	}
	return -1;
}

void ATMBattleDirector::TakeItem(int32 Code)
{
	// One item lying within the selected unit's reach, into its side's stash: free.
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!PlayerCanOrder(Unit))
	{
		return;
	}
	const int32 Near = Battle.CacheNear(Unit->Pos);
	if (Near < 0 || Code < 0 || Code >= static_cast<int32>(Battle.Caches[static_cast<size_t>(Near)].Items.size()))
	{
		return;
	}
	const std::string ItemId = Battle.Caches[static_cast<size_t>(Near)].Items[static_cast<size_t>(Code)]->Id;
	const std::string Refused = Battle.ValidateTake(Unit->Id, Near, ItemId, -1);
	if (!Refused.empty())
	{
		Tell(UTF8_TO_TCHAR(Refused.c_str()));
		return;
	}
	OrderSelected(TMSim::FOrder::MakeTake(Unit->Id, Unit->Serial, Near, ItemId, -1));
}

void ATMBattleDirector::DropItem(int32 Code)
{
	// Taking an item off is that unit's whole turn: only on its turn, before it has done anything.
	const TMSim::FUnit* Unit = Battle.FindUnit(Code / 4);
	const int32 GearSlot = Code % 4;
	if (!Unit || !PlayerCanOrder(Unit))
	{
		Tell(TEXT("An item comes off only on its wearer's turn, and takes the whole turn."));
		return;
	}
	const std::string Refused = Battle.ValidateDrop(Unit->Id, GearSlot);
	if (!Refused.empty())
	{
		Tell(UTF8_TO_TCHAR(Refused.c_str()));
		return;
	}
	if (Unit->Id != SelectedId)
	{
		SelectUnit(Unit->Id);
	}
	OrderSelected(TMSim::FOrder::MakeDrop(Unit->Id, Unit->Serial, GearSlot));
	bTeamItemsOpen = false;
}

void ATMBattleDirector::EquipItem(int32 Code)
{
	const TMSim::FUnit* Unit = Battle.FindUnit(Code / 4);
	const int32 GearSlot = Code % 4;
	const int32 Team = ItemsTeam();
	if (!Unit || Team < 0 || Unit->Team != Team || !MayManageItems(Team) || Battle.Winner != -1)
	{
		return;
	}
	const std::vector<TMSim::FBattle::FStashed>& Held = Battle.Stash[Team];
	if (StashPick < 0 || StashPick >= static_cast<int32>(Held.size()) || !Held[static_cast<size_t>(StashPick)].Item)
	{
		Tell(Held.empty() ? TEXT("The stash is empty: walk onto items on the ground to pick them up.")
			: TEXT("Choose an item from the stash first, then an open slot."));
		return;
	}
	const std::string ItemId = Held[static_cast<size_t>(StashPick)].Item->Id;
	const TMSim::FOrder Order = TMSim::FOrder::MakeEquip(Unit->Id, ItemId, GearSlot);
	const std::string Refused = Battle.Validate(Order);
	if (!Refused.empty())
	{
		Tell(UTF8_TO_TCHAR(Refused.c_str()));
		return;
	}
	const FString Problem = Submit(Order);
	if (!Problem.IsEmpty())
	{
		Tell(Problem);
		return;
	}
	StashPick = -1;
}

int32 ATMBattleDirector::ItemsTeam() const
{
	if (bOnline)
	{
		return LocalTeam;
	}
	// One person against the computer: their side. Two at one screen: the side of the unit in hand.
	if (ComputerPlays(0) != ComputerPlays(1))
	{
		return ComputerPlays(0) ? 1 : 0;
	}
	if (const TMSim::FUnit* Unit = SelectedUnit())
	{
		return Unit->Team == 0 || Unit->Team == 1 ? Unit->Team : 0;
	}
	return 0;
}

bool ATMBattleDirector::MayManageItems(int32 Team) const
{
	if (Team != 0 && Team != 1)
	{
		return false;
	}
	if (bOnline)
	{
		return Team == LocalTeam && OnlineStopped.IsEmpty() && !bWaitingForHost;
	}
	return !ComputerPlays(Team) && bPlayerInput;
}

FString ATMBattleDirector::MonsterLine(const TMSim::FUnit& Unit) const
{
	const TMSim::FJobDef* Job = TMSim::FindJob(Unit.Job);
	FString Line = Job ? FString(UTF8_TO_TCHAR(Job->Name.c_str())) : FString(UTF8_TO_TCHAR(Unit.Job.c_str()));
	const TMSim::FMonsterInfo* Info = Unit.MonsterInfo();
	if (Info && !Info->Phases.empty() && Unit.Phase > 0)
	{
		Line += FString::Printf(TEXT(" (phase %d)"), Unit.Phase + 1);
	}
	if (Unit.TamedTurns > 0)
	{
		return Line + FString::Printf(TEXT("  tamed %d"), Unit.TamedTurns);
	}
	switch (Unit.Mind)
	{
	case TMSim::EMind::Resting: return Info ? Line + TEXT("  ") + TemperamentWord(Info->Temperament) : Line;
	case TMSim::EMind::Alert: return Line + TEXT("  !");
	case TMSim::EMind::Fighting: return Line;
	case TMSim::EMind::Returning: return Line + TEXT("  going home");
	case TMSim::EMind::Fleeing: return Line + TEXT("  fleeing");
	}
	return Line;
}
