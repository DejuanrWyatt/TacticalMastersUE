// Queued orders (2026-10-01; Docs/design/feat-move-queue.md): a walk, by
// waypoints if it has any, and an ability aimed from where that walk ends, set
// for one of the player's units ahead of its turn and carried out the moment
// the turn begins, or set inside a turn and carried out together on Go.
//
// Nothing here is a rule. A plan is this machine's note of what its player
// wants; what goes out when it runs is the same Move and UseAbility orders a
// person clicking would give, with the unit's real serial, checked by the
// rules then. So a plan that has gone stale -- its spot taken, its target
// gone, the unit stunned -- is simply refused, dropped, and the turn is the
// player's as usual.
//
// 2026-10-03 ("the queue needs polish"): a Go To whose next walk would be short
// -- the last stretch, or as far as a blocked way lets it -- hands the unit to
// the player before walking, so no turn goes on a step; an ability aimed out of
// reach becomes a Go To that uses it the turn it gets in range; and a queued
// unit that sees an enemy it did not see before has its queue cancelled and is
// handed back at once.

#include "TMBattleDirector.h"
#include "TMSettings.h"

#include "GameFramework/PlayerController.h"

#include "SimAbility.h"

#include <algorithm>

namespace TMPlans
{
	TMSim::FUnit* Find(TMSim::FBattle& Battle, int32 UnitId)
	{
		return Battle.FindUnit(UnitId);
	}

	const TMSim::FUnit* Find(const TMSim::FBattle& Battle, int32 UnitId)
	{
		return const_cast<TMSim::FBattle&>(Battle).FindUnit(UnitId);
	}
}

// --------------------------------------------------------------- stand-in

ATMBattleDirector::FPlanStandIn::FPlanStandIn(ATMBattleDirector& Director)
{
	if (!Director.IsPlanningSelected())
	{
		return;
	}
	Unit = TMPlans::Find(Director.Battle, Director.SelectedId);
	if (!Unit)
	{
		return;
	}
	Pos = Unit->Pos;
	bReady = Unit->bReady;
	bMoved = Unit->bMoved;
	bActed = Unit->bActed;
	for (int Slot = 0; Slot < TMSim::AbilitySlots; ++Slot)
	{
		Cooldowns[Slot] = Unit->Cooldowns[Slot];
	}
	// As it will be when its turn begins: ready, and its cooldowns a turn on
	// (SimBattle.cpp, BecomeReady). A unit already in its turn is as it is.
	if (!Unit->bReady)
	{
		Unit->bReady = true;
		Unit->bMoved = false;
		Unit->bActed = false;
		for (int Slot = 0; Slot < TMSim::AbilitySlots; ++Slot)
		{
			Unit->Cooldowns[Slot] = std::max(0, Unit->Cooldowns[Slot] - 1);
		}
	}
	// And where its walk ends, once one is planned -- unless the walk is the
	// thing being aimed again, when it starts from where it stands.
	const FTMPlan* Plan = Director.Plans.Find(Unit->Id);
	if (Plan && Plan->bWalk && Director.AimMode != EAimMode::Move)
	{
		Unit->Pos = Plan->To;
		Unit->bMoved = true;
		// A sprint is the turn's action as well (ApplyMove).
		Unit->bActed = Unit->bActed || Plan->bSprint;
	}
}

ATMBattleDirector::FPlanStandIn::~FPlanStandIn()
{
	if (!Unit)
	{
		return;
	}
	Unit->Pos = Pos;
	Unit->bReady = bReady;
	Unit->bMoved = bMoved;
	Unit->bActed = bActed;
	for (int Slot = 0; Slot < TMSim::AbilitySlots; ++Slot)
	{
		Unit->Cooldowns[Slot] = Cooldowns[Slot];
	}
}

// ------------------------------------------------------------ who and when

bool ATMBattleDirector::PlayerCanPlan(const TMSim::FUnit* Unit) const
{
	// As PlayerCanOrder, but whether or not it is the unit's turn, and online
	// whether or not the host has answered the last order: a plan is only a note.
	if (!Unit || !Unit->IsAlive() || Unit->bMonster || ComputerPlaysUnit(*Unit))
	{
		return false;
	}
	if (bOnline && (UnitOwner(*Unit) != LocalPlayer || !OnlineStopped.IsEmpty()))
	{
		return false;
	}
	return Battle.Winner == -1 && !Battle.IsPlanning() && (bOnline || !bMenuOpen) && Screen == EScreen::Battle;
}

bool ATMBattleDirector::PlayerCanCommand(const TMSim::FUnit* Unit) const
{
	if (Unit && bPlanMode && Unit->Id == SelectedId)
	{
		return PlayerCanPlan(Unit);
	}
	return PlayerCanOrder(Unit);
}

bool ATMBattleDirector::IsPlanningSelected() const
{
	return bPlanMode && SelectedId >= 0 && TMPlans::Find(Battle, SelectedId) != nullptr;
}

FString ATMBattleDirector::PlanName(const TMSim::FUnit& Unit) const
{
	return LogName(Unit.Id);
}

bool ATMBattleDirector::WayPointHeld() const
{
	const UWorld* World = GetWorld();
	const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	if (!Player)
	{
		return false;
	}
	for (const FKey& Key : FTMSettings::Get().Keys(ETMAction::Waypoint))
	{
		if (Player->IsInputKeyDown(Key))
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------- the walk aimed

TMSim::FVec2 ATMBattleDirector::WalkStart(const TMSim::FUnit& Unit) const
{
	return WayPoints.empty() ? Unit.Pos : WayPoints.back();
}

void ATMBattleDirector::RefreshReachable()
{
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!Unit)
	{
		Reachable.clear();
		ZoneShadow.clear();
		return;
	}
	Reachable = Battle.ReachableVia(*Unit, WayPoints, bSprinting);
	RefreshZoneShadow();
	PathNode = TMSim::FNode{ -9999, -9999 };
	PathShown.clear();
}

bool ATMBattleDirector::AddWayPoint(const TMSim::FVec2& Point)
{
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!Unit)
	{
		return false;
	}
	if (static_cast<int32>(WayPoints.size()) >= TMSim::MaxWaypoints)
	{
		Tell(FString::Printf(TEXT("A walk has at most %d waypoints: click where it ends."), TMSim::MaxWaypoints));
		return false;
	}
	const TMSim::FVec2 Spot = TMSim::FMap::Snap(Point);
	if (TMSim::FMap::NodeOf(Spot) == TMSim::FMap::NodeOf(WalkStart(*Unit)))
	{
		return false;
	}
	// Walked to with what is left, the same way the rules will check it.
	std::vector<TMSim::FVec2> Longer = WayPoints;
	Longer.push_back(Spot);
	double Left = 0.0;
	if (!Battle.WalkVia(*Unit, Longer, bSprinting, Left))
	{
		Tell(TEXT("It can't reach that waypoint."));
		return false;
	}
	WayPoints = Longer;
	RefreshReachable();
	return true;
}

// ------------------------------------------------------- making a plan

void ATMBattleDirector::StartPlanning(int32 UnitId)
{
	const TMSim::FUnit* Unit = TMPlans::Find(Battle, UnitId);
	if (!PlayerCanPlan(Unit))
	{
		return;
	}
	SelectedId = UnitId;
	SelectedSerial = Unit->Serial;
	bPlanMode = true;
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
	WayPoints.clear();
	Reachable.clear();
	PathShown.clear();
	PathNode = TMSim::FNode{ -9999, -9999 };
	const FTMPlan* Plan = Plans.Find(UnitId);
	// Straight to its walk, as selecting a unit does, unless that is planned already.
	if ((!Plan || !Plan->bWalk) && !Unit->bMoved && !Unit->IsCasting())
	{
		EnterMoveMode(false);
	}
	const FString Go = FTMSettings::Get().KeyName(ETMAction::PlanTurn);
	Tell(Unit->bReady
		? FString::Printf(TEXT("Planning %s's turn: its walk, then an ability aimed from there. %s to go."), *PlanName(*Unit), *Go)
		: FString::Printf(TEXT("Planning %s's next turn: its walk, then an ability. It runs when the turn comes; %s when done."), *PlanName(*Unit), *Go));
}

void ATMBattleDirector::StopPlanning()
{
	const bool bWas = bPlanMode;
	bPlanMode = false;
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
	WayPoints.clear();
	Reachable.clear();
	PathShown.clear();
	PathNode = TMSim::FNode{ -9999, -9999 };
	// A waiting unit was only selected to be planned.
	const TMSim::FUnit* Unit = SelectedUnit();
	if (bWas && (!Unit || !PlayerCanOrder(Unit)))
	{
		Deselect();
	}
}

void ATMBattleDirector::PlanKey()
{
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!Unit)
	{
		return;
	}
	// A Go To stopped this turn: carry on (2026-10-01).
	if (FTMGoTo* Order = GoTos.Find(Unit->Id))
	{
		if (Order->bStopped && Unit->bReady && Unit->Serial == Order->StoppedSerial && PlayerCanOrder(Unit))
		{
			StepGoTo(Unit->Id, true);
			return;
		}
	}
	if (!bPlanMode)
	{
		// Its turn, ordered as it goes: plan it instead, to go in one.
		if (PlayerCanOrder(Unit) && !Unit->bMoved && !Unit->bActed)
		{
			StartPlanning(Unit->Id);
		}
		return;
	}
	const FTMPlan* Plan = Plans.Find(Unit->Id);
	if (Unit->bReady && PlayerCanOrder(Unit))
	{
		if (Plan && !Plan->IsEmpty())
		{
			RunPlan(Unit->Id);
		}
		else
		{
			// Nothing planned: back to ordering it as it goes.
			bPlanMode = false;
			SelectUnit(Unit->Id);
		}
		return;
	}
	// A waiting unit: done planning it. The plan stays, and runs when its turn comes.
	if (Plan && !Plan->IsEmpty())
	{
		Tell(FString::Printf(TEXT("%s's plan is set: it runs when its turn comes."), *PlanName(*Unit)));
	}
	StopPlanning();
	AutoSelect();
}

void ATMBattleDirector::PlanWalk(const TMSim::FVec2& To, int32 Face)
{
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!Unit)
	{
		return;
	}
	GoTos.Remove(Unit->Id);
	FTMPlan& Plan = Plans.FindOrAdd(Unit->Id);
	Plan.Serial = Unit->Serial;
	Plan.bThisTurn = Unit->bReady;
	Plan.Seen = EnemiesInOwnSight(*Unit);
	Plan.bWalk = true;
	Plan.bSprint = bSprinting;
	Plan.Via = WayPoints;
	Plan.To = TMSim::FMap::Snap(To);
	Plan.Face = Face;
	Plan.Path = Battle.PathVia(*Unit, Plan.Via, TMSim::FMap::NodeOf(Plan.To), bSprinting);
	Plan.Metres = 0.0;
	const TMSim::FNode End = TMSim::FMap::NodeOf(Plan.To);
	for (const std::pair<TMSim::FNode, double>& Entry : Reachable)
	{
		if (Entry.first == End)
		{
			Plan.Metres = Entry.second;
		}
	}
	// An ability aimed from the old spot is aimed again from the new one.
	const bool bHadAbility = Plan.HasAbility();
	Plan.Slot = -1;
	Plan.Follow = -1;
	if (bSprinting)
	{
		Tell(FString::Printf(TEXT("%s will sprint %.1f m: a sprint is the turn's action, so no ability."), *PlanName(*Unit), Plan.Metres));
	}
	else
	{
		Tell(FString::Printf(TEXT("%s will walk %.1f m%s. Now an ability, aimed from there (1-4), or %s."), *PlanName(*Unit), Plan.Metres,
			bHadAbility ? TEXT(" (aim its ability again from the new spot)") : TEXT(""),
			Unit->bReady ? *FString::Printf(TEXT("%s to go"), *FTMSettings::Get().KeyName(ETMAction::PlanTurn))
				: TEXT("leave it at the walk")));
	}
	WayPoints.clear();
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
	Reachable.clear();
	PathShown.clear();
	PathNode = TMSim::FNode{ -9999, -9999 };
}

void ATMBattleDirector::PlanAbility(int32 Slot, const TMSim::FVec2& Target, int32 Follow)
{
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!Unit)
	{
		return;
	}
	GoTos.Remove(Unit->Id);
	FTMPlan& Plan = Plans.FindOrAdd(Unit->Id);
	Plan.Serial = Unit->Serial;
	Plan.bThisTurn = Unit->bReady;
	Plan.Seen = EnemiesInOwnSight(*Unit);
	Plan.Slot = Slot;
	Plan.Target = Target;
	Plan.Follow = Follow;
	const TMSim::FAbility* Ability = Unit->Ability(Slot);
	Tell(FString::Printf(TEXT("%s will %s%hs%s."), *PlanName(*Unit), Plan.bWalk ? TEXT("walk, then use ") : TEXT("use "),
		Ability ? Ability->Name.c_str() : "its ability",
		Unit->bReady ? *FString::Printf(TEXT(": %s to go"), *FTMSettings::Get().KeyName(ETMAction::PlanTurn))
			: TEXT(" when its turn comes")));
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
}

bool ATMBattleDirector::PlanWalkIntoRange(const TMSim::FUnit& Unit, const TMSim::FVec2& Point)
{
	// As WalkIntoRange, into the plan: the walk to the nearest spot it can be
	// used from, and the ability from there.
	const FTMPlan* Plan = Plans.Find(Unit.Id);
	if ((Plan && Plan->bWalk) || Unit.bMoved || Unit.IsCasting())
	{
		return false;
	}
	const int32 Slot = AimSlot;
	TMSim::FVec2 Spot;
	double Walk = 0.0;
	if (!ClosestSpotInRange(Unit, Slot, Point, Spot, Walk))
	{
		return false;
	}
	WayPoints.clear();
	Reachable = Battle.ReachableNodes(Unit);
	PlanWalk(Spot);
	PlanAbility(Slot, Point, -1);
	return true;
}

void ATMBattleDirector::UndoPlanStep()
{
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!Unit)
	{
		return;
	}
	// A waypoint, planned or not.
	if (!WayPoints.empty())
	{
		WayPoints.pop_back();
		RefreshReachable();
		return;
	}
	// Then its Go To, if it has one.
	if (GoTos.Contains(Unit->Id))
	{
		CancelGoTo(Unit->Id, true);
		return;
	}
	if (!bPlanMode)
	{
		return;
	}
	FTMPlan* Plan = Plans.Find(Unit->Id);
	if (!Plan)
	{
		return;
	}
	if (Plan->HasAbility())
	{
		Plan->Slot = -1;
		Plan->Follow = -1;
		Tell(TEXT("Its planned ability is taken back."));
	}
	else if (Plan->bWalk)
	{
		Plan->bWalk = false;
		Plan->Via.clear();
		Plan->Path.clear();
		Tell(TEXT("Its planned walk is taken back."));
		EnterMoveMode(false);
	}
	if (Plan->IsEmpty())
	{
		Plans.Remove(Unit->Id);
	}
}

void ATMBattleDirector::ClearPlan(int32 UnitId)
{
	Plans.Remove(UnitId);
	if (UnitId == SelectedId)
	{
		WayPoints.clear();
		if (bPlanMode)
		{
			AimMode = EAimMode::None;
			AimSlot = -1;
			if (const TMSim::FUnit* Unit = SelectedUnit())
			{
				if (!Unit->bMoved)
				{
					EnterMoveMode(false);
				}
			}
		}
	}
}

// ------------------------------------------------------ carrying one out

bool ATMBattleDirector::RunPlan(int32 UnitId)
{
	const FTMPlan* Found = Plans.Find(UnitId);
	if (!Found)
	{
		return false;
	}
	const FTMPlan Plan = *Found;
	Plans.Remove(UnitId);
	const TMSim::FUnit* Unit = TMPlans::Find(Battle, UnitId);
	if (!PlayerCanOrder(Unit) || Plan.IsEmpty())
	{
		return false;
	}
	const FString Name = PlanName(*Unit);
	if (bPlanMode && SelectedId == UnitId)
	{
		bPlanMode = false;
		WayPoints.clear();
	}
	// It takes the selection, unless the player is in the middle of ordering
	// another ready unit: then it runs where it stands and leaves them be.
	// Not from one being planned either (2026-10-03, camera rules B).
	const TMSim::FUnit* Busy = SelectedUnit();
	const bool bTakeSelection = !bPlanMode && (!Busy || Busy->Id == UnitId || !PlayerCanOrder(Busy));
	if (bTakeSelection)
	{
		SelectUnit(UnitId);
	}
	const int32 Serial = Unit->Serial;
	if (Plan.bWalk)
	{
		const TMSim::FOrder Walk = TMSim::FOrder::MakeMove(UnitId, Serial, Plan.To, Plan.bSprint, Plan.Via, Plan.Face);
		const std::string Refused = Battle.Validate(Walk);
		if (!Refused.empty())
		{
			Tell(FString::Printf(TEXT("%s's plan was dropped: %hs It's your turn to give."), *Name, Refused.c_str()));
			return false;
		}
		if (bTakeSelection)
		{
			OrderSelected(Walk);
		}
		else
		{
			const FString Why = Submit(Walk);
			if (!Why.IsEmpty())
			{
				Tell(FString::Printf(TEXT("%s's plan was dropped: %s"), *Name, *Why));
				return false;
			}
		}
		if (Plan.HasAbility())
		{
			// On arrival, as a walk into range does (FirePendingAbility).
			PendingAbility.UnitId = UnitId;
			PendingAbility.Serial = Serial;
			PendingAbility.Slot = Plan.Slot;
			PendingAbility.Target = Plan.Target;
			PendingAbility.Follow = Plan.Follow;
			PendingAbility.bAfterWalk = true;
			// The aim on screen is this unit's only if it took the selection.
			if (bTakeSelection)
			{
				AimMode = EAimMode::None;
				AimSlot = -1;
				Reachable.clear();
				PathShown.clear();
			}
		}
		return true;
	}
	// The ability alone, from where it stands.
	TMSim::FVec2 Target = Plan.Target;
	if (const TMSim::FUnit* Followed = Plan.Follow >= 0 ? TMPlans::Find(Battle, Plan.Follow) : nullptr)
	{
		Target = Followed->Pos;
	}
	const TMSim::FOrder Use = TMSim::FOrder::MakeUseAbility(UnitId, Serial, Plan.Slot, Target, Plan.Follow);
	const std::string Refused = Battle.Validate(Use);
	if (!Refused.empty())
	{
		Tell(FString::Printf(TEXT("%s's plan was dropped: %hs It's your turn to give."), *Name, Refused.c_str()));
		return false;
	}
	if (bTakeSelection)
	{
		OrderSelected(Use);
	}
	else
	{
		Submit(Use);
	}
	return true;
}

void ATMBattleDirector::RunDuePlans()
{
	// Fallen units' plans go with them, and so does a plan made inside a turn
	// that ended without its Go.
	TArray<int32> Gone;
	for (const TPair<int32, FTMPlan>& Pair : Plans)
	{
		const TMSim::FUnit* Unit = TMPlans::Find(Battle, Pair.Key);
		if (!Unit || !Unit->IsAlive() || (Pair.Value.bThisTurn && Unit->Serial != Pair.Value.Serial))
		{
			Gone.Add(Pair.Key);
		}
	}
	for (const int32 Id : Gone)
	{
		Plans.Remove(Id);
	}
	// One at a time: an ability still waiting on its walk, or an order still
	// with the host, goes first.
	if (PendingAbility.UnitId >= 0 || bWaitingForHost || Plans.Num() == 0)
	{
		return;
	}
	for (const TPair<int32, FTMPlan>& Pair : Plans)
	{
		const TMSim::FUnit* Unit = TMPlans::Find(Battle, Pair.Key);
		// Its turn, and a later one than it was planned on: a plan made inside
		// a turn waits for Go.
		if (!Pair.Value.bThisTurn && PlayerCanOrder(Unit) && Unit->Serial != Pair.Value.Serial && !Unit->bMoved && !Unit->bActed)
		{
			RunPlan(Pair.Key);
			return;
		}
	}
}

// ------------------------------------------------------------------- Go To

TSet<int32> ATMBattleDirector::EnemiesInSight(const TMSim::FUnit& Unit) const
{
	// The other side's units only: monsters asleep in their camps are no news.
	TSet<int32> Out;
	for (const TMSim::FUnit& Other : Battle.Units)
	{
		if (Other.IsAlive() && !Other.bMonster && (Other.Team == 0 || Other.Team == 1) && Other.Team != Unit.Team && IsSeen(Other))
		{
			Out.Add(Other.Id);
		}
	}
	return Out;
}

TSet<int32> ATMBattleDirector::EnemiesInOwnSight(const TMSim::FUnit& Unit) const
{
	// What the unit itself has in view -- within its sight, nothing in the way --
	// rather than all its side knows: an enemy a far-off ally spots is no reason
	// to stop this one.
	TSet<int32> Out;
	const double Sight = Battle.SightOf(Unit);
	for (const TMSim::FUnit& Other : Battle.Units)
	{
		if (Other.IsAlive() && !Other.bMonster && (Other.Team == 0 || Other.Team == 1) && Other.Team != Unit.Team && IsSeen(Other)
			&& static_cast<double>(Unit.Pos.DistanceTo(Other.Pos)) <= Sight && Battle.HasLineOfSight(Unit.Pos, Other.Pos))
		{
			Out.Add(Other.Id);
		}
	}
	return Out;
}

void ATMBattleDirector::CancelQueuesOnSight()
{
	// Online, a walk still with the host has not moved anyone yet.
	if (bWaitingForHost || Battle.Winner != -1)
	{
		return;
	}
	auto FirstNew = [](const TSet<int32>& Now, const TSet<int32>& Before)
	{
		for (const int32 Id : Now)
		{
			if (!Before.Contains(Id))
			{
				return Id;
			}
		}
		return -1;
	};
	// Go Tos: an enemy coming into the unit's view ends it, wherever it is.
	TArray<TPair<int32, int32>> Seen;
	for (TPair<int32, FTMGoTo>& Pair : GoTos)
	{
		const TMSim::FUnit* Unit = TMPlans::Find(Battle, Pair.Key);
		if (!Unit || !Unit->IsAlive())
		{
			continue;
		}
		const TSet<int32> Now = EnemiesInOwnSight(*Unit);
		// The unit it was sent to use an ability on is no news: it is going to it.
		TSet<int32> Known = Pair.Value.Seen;
		if (Pair.Value.Follow >= 0)
		{
			Known.Add(Pair.Value.Follow);
		}
		const int32 New = FirstNew(Now, Known);
		// What it sees now is what is news against next time: one that leaves and comes back is news again.
		Pair.Value.Seen = Now;
		if (New >= 0)
		{
			Seen.Add(TPair<int32, int32>(Pair.Key, New));
		}
	}
	for (const TPair<int32, int32>& Each : Seen)
	{
		const int32 UnitId = Each.Key;
		GoTos.Remove(UnitId);
		// A walk-and-end step that saw it on the way: the turn is not ended, the
		// unit can still act.
		if (GoToEndUnit == UnitId)
		{
			GoToEndUnit = -1;
		}
		const TMSim::FUnit* Unit = TMPlans::Find(Battle, UnitId);
		if (!Unit)
		{
			continue;
		}
		const bool bTurn = PlayerCanOrder(Unit);
		if (bTurn)
		{
			// Its turn: taken up if the player is busy with nobody else; then the
			// camera may go to it, by the camera rules. Busy (ordering another, or
			// planning), it is pointed out instead (2026-10-03).
			const TMSim::FUnit* Busy = SelectedUnit();
			if (!bPlanMode && (!Busy || Busy->Id == UnitId || !PlayerCanOrder(Busy)))
			{
				SelectUnit(UnitId);
				RequestFollow(UnitId);
			}
			else
			{
				PointOut(UnitId);
			}
		}
		Tell(FString::Printf(TEXT("%s sees %s: its Go To is cancelled.%s"), *PlanName(*Unit), *LogName(Each.Value),
			bTurn ? TEXT(" Its turn is yours.") : TEXT(" Give it new orders.")));
	}
	// Plans for a coming turn: the same news makes them stale.
	TArray<TPair<int32, int32>> Stale;
	for (TPair<int32, FTMPlan>& Pair : Plans)
	{
		const TMSim::FUnit* Unit = TMPlans::Find(Battle, Pair.Key);
		if (!Unit || !Unit->IsAlive() || Pair.Value.bThisTurn)
		{
			continue;
		}
		// While it is being aimed it stands where its walk ends (FPlanStandIn); not now.
		const TSet<int32> Now = EnemiesInOwnSight(*Unit);
		TSet<int32> Known = Pair.Value.Seen;
		if (Pair.Value.Follow >= 0)
		{
			Known.Add(Pair.Value.Follow);
		}
		const int32 New = FirstNew(Now, Known);
		Pair.Value.Seen = Now;
		if (New >= 0)
		{
			Stale.Add(TPair<int32, int32>(Pair.Key, New));
		}
	}
	for (const TPair<int32, int32>& Each : Stale)
	{
		ClearPlan(Each.Key);
		if (const TMSim::FUnit* Unit = TMPlans::Find(Battle, Each.Key))
		{
			Tell(FString::Printf(TEXT("%s sees %s: its plan is dropped. Give it new orders."), *PlanName(*Unit), *LogName(Each.Value)));
		}
	}
}

std::vector<TMSim::FVec2> ATMBattleDirector::SplitRoute(const TMSim::FUnit& Unit, const std::vector<TMSim::FVec2>& Route) const
{
	// Each turn walks as far as its move goes, step by step along the way.
	// Breaking away from an enemy costs more than this counts, which is why
	// each turn's walk is worked out again by the rules when it comes.
	std::vector<TMSim::FVec2> Stops;
	const double Move = FMath::Max(0.5, Battle.MoveOf(Unit));
	double Left = Move;
	for (size_t i = 1; i < Route.size(); ++i)
	{
		const double Step = Route[i - 1].DistanceTo(Route[i]);
		if (Step > Left + 0.001)
		{
			Stops.push_back(Route[i - 1]);
			Left = Move;
		}
		Left -= Step;
	}
	if (Route.size() > 1)
	{
		Stops.push_back(Route.back());
	}
	return Stops;
}

bool ATMBattleDirector::SetGoTo(int32 UnitId, const TMSim::FVec2& Dest)
{
	const TMSim::FUnit* Unit = TMPlans::Find(Battle, UnitId);
	if (!PlayerCanPlan(Unit))
	{
		return false;
	}
	const TMSim::FVec2 To = TMSim::FMap::Snap(Dest);
	double Metres = 0.0;
	const std::vector<TMSim::FVec2> Route = Battle.RouteTo(*Unit, WayPoints, TMSim::FMap::NodeOf(To), &Metres);
	if (Route.size() < 2)
	{
		Tell(TEXT("There is no way there."));
		return false;
	}
	// A Go To replaces whatever was planned for it.
	Plans.Remove(UnitId);
	FTMGoTo Order;
	Order.Dest = To;
	Order.Via = WayPoints;
	Order.Hp = Unit->Hp;
	Order.Seen = EnemiesInOwnSight(*Unit);
	Order.Route = Route;
	Order.Stops = SplitRoute(*Unit, Route);
	const int32 Turns = static_cast<int32>(Order.Stops.size());
	GoTos.Add(UnitId, Order);
	WayPoints.clear();
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
	Reachable.clear();
	PathShown.clear();
	GoToHoverStops.clear();
	PathNode = TMSim::FNode{ -9999, -9999 };
	Tell(FString::Printf(TEXT("%s goes there: %.0f m, %d turn%s. It walks and ends each turn; the turn it arrives is yours."),
		*PlanName(*Unit), Metres, Turns, Turns == 1 ? TEXT("") : TEXT("s")));
	// Its turn, with its walk to come: the first walk now.
	if (Unit->bReady && PlayerCanOrder(Unit) && !Unit->bMoved && !Unit->bActed && !Unit->IsCasting())
	{
		if (bPlanMode)
		{
			bPlanMode = false;
		}
		StepGoTo(UnitId, true);
	}
	else if (bPlanMode && SelectedId == UnitId)
	{
		// A waiting unit: it sets off when its turn comes.
		StopPlanning();
		AutoSelect();
	}
	return true;
}

void ATMBattleDirector::StopGoTo(int32 UnitId, const FString& Why)
{
	FTMGoTo* Order = GoTos.Find(UnitId);
	const TMSim::FUnit* Unit = TMPlans::Find(Battle, UnitId);
	if (!Order || !Unit)
	{
		return;
	}
	Order->bStopped = true;
	Order->StoppedSerial = Unit->Serial;
	Order->Why = Why;
	// Handed back: taken up, and the camera may go to it (by the camera rules),
	// unless the player is busy with another unit or planning: then it is
	// pointed out (2026-10-03).
	const TMSim::FUnit* Busy = SelectedUnit();
	if (!bPlanMode && (!Busy || Busy->Id == UnitId || !PlayerCanOrder(Busy)))
	{
		SelectUnit(UnitId);
		RequestFollow(UnitId);
	}
	else if (PlayerCanOrder(Unit))
	{
		PointOut(UnitId);
	}
	Tell(FString::Printf(TEXT("%s stopped on the way: %s. %s: keep going; %s: cancel the order."), *PlanName(*Unit), *Why,
		*FTMSettings::Get().KeyName(ETMAction::PlanTurn), *FTMSettings::Get().KeyName(ETMAction::PlanUndo)));
}

void ATMBattleDirector::CancelGoTo(int32 UnitId, bool bTell)
{
	if (GoTos.Remove(UnitId) > 0 && bTell)
	{
		if (const TMSim::FUnit* Unit = TMPlans::Find(Battle, UnitId))
		{
			Tell(FString::Printf(TEXT("%s's Go To is cancelled."), *PlanName(*Unit)));
		}
	}
	if (GoToEndUnit == UnitId)
	{
		GoToEndUnit = -1;
	}
}

std::vector<TMSim::FVec2> ATMBattleDirector::ApproachRoute(const TMSim::FUnit& Unit, int32 Slot, const TMSim::FVec2& Target, double* OutMetres)
{
	// The way towards the target, cut at the first spot along it the ability
	// can be used from: in range, in sight of the target where it needs that,
	// and nobody else standing there.
	const TMSim::FAbility* Ability = Unit.Ability(Slot);
	if (!Ability)
	{
		return {};
	}
	double Metres = 0.0;
	const TMSim::FNode Aimed = TMSim::FMap::NodeOf(Target);
	std::vector<TMSim::FVec2> Route = Battle.RouteTo(Unit, {}, Aimed, &Metres);
	// A unit aimed at stands on its spot, which no way ends on: towards the
	// nearest open spot by it instead, the few nearest the walker first.
	for (int Ring = 1; Ring <= 4 && Route.size() < 2; ++Ring)
	{
		std::vector<TMSim::FNode> Around;
		for (int DX = -Ring; DX <= Ring; ++DX)
		{
			for (int DY = -Ring; DY <= Ring; ++DY)
			{
				if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) == Ring)
				{
					Around.push_back(TMSim::FNode{ Aimed.X + DX, Aimed.Y + DY });
				}
			}
		}
		std::sort(Around.begin(), Around.end(), [&Unit](const TMSim::FNode& A, const TMSim::FNode& B)
		{
			return TMSim::FMap::NodePos(A).DistanceTo(Unit.Pos) < TMSim::FMap::NodePos(B).DistanceTo(Unit.Pos);
		});
		for (size_t k = 0; k < Around.size() && k < 3 && Route.size() < 2; ++k)
		{
			Route = Battle.RouteTo(Unit, {}, Around[k], &Metres);
		}
	}
	if (Route.size() < 2)
	{
		return {};
	}
	double Walked = 0.0;
	for (size_t i = 1; i < Route.size(); ++i)
	{
		Walked += Route[i - 1].DistanceTo(Route[i]);
		const TMSim::FVec2& Spot = Route[i];
		bool bTaken = false;
		for (const TMSim::FUnit& Other : Battle.Units)
		{
			bTaken = bTaken || (Other.Id != Unit.Id && Other.IsAlive() && TMSim::FMap::NodeOf(Other.Pos) == TMSim::FMap::NodeOf(Spot));
		}
		if (bTaken || !Battle.InAbilityRange(Unit, Slot, Spot, Target)
			|| (TMSim::NeedsLineOfSight(*Ability) && !Battle.HasLineOfSight(Spot, Target)))
		{
			continue;
		}
		Route.resize(i + 1);
		if (OutMetres)
		{
			*OutMetres = Walked;
		}
		return Route;
	}
	return {};
}

bool ATMBattleDirector::SetAbilityGoTo(int32 UnitId, int32 Slot, const TMSim::FVec2& Target, int32 Follow)
{
	const TMSim::FUnit* Unit = TMPlans::Find(Battle, UnitId);
	const TMSim::FAbility* Ability = Unit ? Unit->Ability(Slot) : nullptr;
	if (!PlayerCanPlan(Unit) || !Ability)
	{
		return false;
	}
	double Metres = 0.0;
	const std::vector<TMSim::FVec2> Route = ApproachRoute(*Unit, Slot, Target, &Metres);
	if (Route.size() < 2)
	{
		// The click says so (TMBattleDirector.cpp).
		return false;
	}
	// A Go To to where it can be used from, carrying the ability.
	WayPoints.clear();
	Plans.Remove(UnitId);
	FTMGoTo Order;
	Order.Dest = Route.back();
	Order.Hp = Unit->Hp;
	Order.Seen = EnemiesInOwnSight(*Unit);
	Order.Route = Route;
	Order.Stops = SplitRoute(*Unit, Route);
	Order.Slot = Slot;
	Order.Target = Target;
	Order.Follow = Follow;
	const int32 Turns = static_cast<int32>(Order.Stops.size());
	GoTos.Add(UnitId, Order);
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
	Reachable.clear();
	PathShown.clear();
	GoToHoverStops.clear();
	bAbilityHoverGoTo = false;
	PathNode = TMSim::FNode{ -9999, -9999 };
	Tell(FString::Printf(TEXT("%s goes into range: %.0f m, %d turn%s, then %hs. It walks and ends each turn; it stops if it sees an enemy."),
		*PlanName(*Unit), Metres, Turns, Turns == 1 ? TEXT("") : TEXT("s"), Ability->Name.c_str()));
	if (Unit->bReady && PlayerCanOrder(Unit) && !Unit->bMoved && !Unit->bActed && !Unit->IsCasting())
	{
		if (bPlanMode)
		{
			bPlanMode = false;
		}
		StepGoTo(UnitId, true);
	}
	else if (bPlanMode && SelectedId == UnitId)
	{
		StopPlanning();
		AutoSelect();
	}
	return true;
}

bool ATMBattleDirector::StepAbilityGoTo(int32 UnitId)
{
	FTMGoTo* Order = GoTos.Find(UnitId);
	const TMSim::FUnit* Unit = TMPlans::Find(Battle, UnitId);
	if (!Order || !Unit || !Order->HasAbility())
	{
		return false;
	}
	const FString Name = PlanName(*Unit);
	// The turn is the player's to see: it takes the selection, unless they are busy with another unit.
	// Not from one being planned, nor from another being ordered (2026-10-03, camera rules B).
	auto Take = [this, UnitId]()
	{
		const TMSim::FUnit* Busy = SelectedUnit();
		if (!bPlanMode && (!Busy || Busy->Id == UnitId || !PlayerCanOrder(Busy)))
		{
			SelectUnit(UnitId);
			return true;
		}
		return false;
	};
	// A unit aimed at is followed to where it stands now, while it can be seen.
	if (Order->Follow >= 0)
	{
		const TMSim::FUnit* Followed = TMPlans::Find(Battle, Order->Follow);
		if (!Followed || !Followed->IsAlive() || !IsSeen(*Followed))
		{
			GoTos.Remove(UnitId);
			if (Take())
			{
				RequestFollow(UnitId);
			}
			else
			{
				PointOut(UnitId);
			}
			Tell(FString::Printf(TEXT("%s's target is gone or out of sight: its Go To is over. Its turn is yours."), *Name));
			return true;
		}
		Order->Target = Followed->Pos;
	}
	const int32 Slot = Order->Slot;
	const TMSim::FVec2 Target = Order->Target;
	const int32 Follow = Order->Follow;
	const int32 Serial = Unit->Serial;
	// From where it stands.
	const TMSim::FOrder Use = TMSim::FOrder::MakeUseAbility(UnitId, Serial, Slot, Target, Follow);
	if (Battle.Validate(Use).empty())
	{
		GoTos.Remove(UnitId);
		TGuardValue<bool> Own(bGoToOrdering, true);
		if (Take())
		{
			OrderSelected(Use);
		}
		else
		{
			Submit(Use);
		}
		return true;
	}
	// From a spot this turn's walk reaches: there, then it, as a walk into range does.
	TMSim::FVec2 Spot;
	double Walk = 0.0;
	if (ClosestSpotInRange(*Unit, Slot, Target, Spot, Walk))
	{
		GoTos.Remove(UnitId);
		TGuardValue<bool> Own(bGoToOrdering, true);
		const bool bTook = Take();
		const FString Refused = Submit(TMSim::FOrder::MakeMove(UnitId, Serial, Spot));
		if (!Refused.IsEmpty())
		{
			Tell(FString::Printf(TEXT("%s's Go To stopped: %s"), *Name, *Refused));
			return true;
		}
		PendingAbility.UnitId = UnitId;
		PendingAbility.Serial = Serial;
		PendingAbility.Slot = Slot;
		PendingAbility.Target = Target;
		PendingAbility.Follow = Follow;
		PendingAbility.bAfterWalk = true;
		// The aim on screen is this unit's only if it took the selection.
		if (bTook)
		{
			AimMode = EAimMode::None;
			AimSlot = -1;
			Reachable.clear();
			PathShown.clear();
		}
		return true;
	}
	// Not yet: on towards it, the way worked out again from where the target is now.
	const std::vector<TMSim::FVec2> Route = ApproachRoute(*Unit, Slot, Target);
	if (Route.size() < 2)
	{
		StopGoTo(UnitId, TEXT("there is no way into range any more"));
		return true;
	}
	Order->Dest = Route.back();
	Order->Via.clear();
	return false;
}

bool ATMBattleDirector::StepGoTo(int32 UnitId, bool bResume)
{
	FTMGoTo* Order = GoTos.Find(UnitId);
	const TMSim::FUnit* Unit = TMPlans::Find(Battle, UnitId);
	if (!Order || !Unit || !PlayerCanOrder(Unit) || Unit->bMoved || Unit->bActed || Unit->IsCasting())
	{
		return false;
	}
	const FString Name = PlanName(*Unit);
	// What would make the order wrong now: news it couldn't have been given with.
	if (!bResume)
	{
		if (Unit->Hp < Order->Hp)
		{
			StopGoTo(UnitId, TEXT("it was hurt"));
			return false;
		}
		// An enemy coming into view cancels it outright (CancelQueuesOnSight).
	}
	// Into range of what it was sent to use, this turn: used, and the order is over.
	if (Order->HasAbility() && StepAbilityGoTo(UnitId))
	{
		return true;
	}
	Order = GoTos.Find(UnitId);
	if (!Order)
	{
		return false;
	}
	Order->bStopped = false;
	Order->Why.Reset();
	Order->LastSerial = Unit->Serial;
	Order->Hp = Unit->Hp;
	Order->Seen = EnemiesInOwnSight(*Unit);

	// The way from here, and how far along it this turn's move goes: the
	// farthest spot on it the rules would let it walk to now.
	double Metres = 0.0;
	const std::vector<TMSim::FVec2> Route = Battle.RouteTo(*Unit, Order->Via, TMSim::FMap::NodeOf(Order->Dest), &Metres);
	if (Route.size() < 2)
	{
		StopGoTo(UnitId, TEXT("there is no way there any more"));
		return false;
	}
	// Where each waypoint falls on it, to send the ones a walk passes with it.
	std::vector<size_t> ViaAt;
	{
		size_t From = 0;
		for (const TMSim::FVec2& Point : Order->Via)
		{
			const TMSim::FNode Node = TMSim::FMap::NodeOf(Point);
			for (size_t i = From; i < Route.size(); ++i)
			{
				if (TMSim::FMap::NodeOf(Route[i]) == Node)
				{
					From = i;
					break;
				}
			}
			ViaAt.push_back(From);
		}
	}
	auto ViaBefore = [&](size_t Index)
	{
		std::vector<TMSim::FVec2> Out;
		for (size_t v = 0; v < ViaAt.size(); ++v)
		{
			if (ViaAt[v] < Index)
			{
				Out.push_back(Order->Via[v]);
			}
		}
		return Out;
	};
	// Straight-line steps first, as a guess; then back from there until the rules agree.
	const double Move = Battle.MoveOf(*Unit);
	size_t Guess = 0;
	{
		double Walked = 0.0;
		for (size_t i = 1; i < Route.size(); ++i)
		{
			Walked += Route[i - 1].DistanceTo(Route[i]);
			if (Walked > Move + 0.001)
			{
				break;
			}
			Guess = i;
		}
	}
	size_t Best = 0;
	for (size_t i = Guess; i >= 1; --i)
	{
		if (Battle.ValidateMove(UnitId, Route[i], false, ViaBefore(i)).empty())
		{
			Best = i;
			break;
		}
	}
	if (Best == 0)
	{
		StopGoTo(UnitId, TEXT("the way is blocked"));
		return false;
	}
	// There, or as near as it gets when someone stands on the spot.
	const TMSim::FNode End = TMSim::FMap::NodeOf(Order->Dest);
	bool bTaken = false;
	for (const TMSim::FUnit& Other : Battle.Units)
	{
		bTaken = bTaken || (Other.Id != UnitId && Other.IsAlive() && TMSim::FMap::NodeOf(Other.Pos) == End);
	}
	const bool bArrives = Best == Route.size() - 1 || (bTaken && Metres <= Move + 0.001);
	// A short walk -- the last stretch, or as far as a blocked way lets it --
	// would spend the turn on a step (2026-10-03): the player takes the unit
	// first, and may walk it on (Keep going) or use the turn better.
	if (!bResume)
	{
		double Walked = 0.0;
		for (size_t i = 1; i <= Best; ++i)
		{
			Walked += Route[i - 1].DistanceTo(Route[i]);
		}
		if (Walked < Move - FMath::Max(1.0, Move * 0.15))
		{
			StopGoTo(UnitId, bArrives
				? FString::Printf(TEXT("the last stretch is only %.1f m of its %.1f m move"), Walked, Move)
				: FString::Printf(TEXT("the way is blocked after %.1f m"), Walked));
			return false;
		}
	}
	const std::vector<TMSim::FVec2> Via = ViaBefore(Best);
	const int32 Serial = Unit->Serial;
	const TMSim::FOrder Walk = TMSim::FOrder::MakeMove(UnitId, Serial, Route[Best], false, Via);
	// The rest of the way, from where this walk ends.
	{
		std::vector<TMSim::FVec2> Rest(Route.begin() + static_cast<std::ptrdiff_t>(Best), Route.end());
		Order->Route = Rest;
		Order->Stops = SplitRoute(*Unit, Rest);
		std::vector<TMSim::FVec2> Left;
		for (size_t v = 0; v < ViaAt.size(); ++v)
		{
			if (ViaAt[v] > Best)
			{
				Left.push_back(Order->Via[v]);
			}
		}
		Order->Via = Left;
	}
	const bool bHandOver = bArrives || Order->bWaitForMe;
	TGuardValue<bool> Own(bGoToOrdering, true);
	if (bHandOver)
	{
		// The turn is the player's after this walk: it takes the selection,
		// unless they are busy with another unit.
		// Not from one being planned either (2026-10-03, camera rules B): then it
		// is pointed out once it has walked.
		const TMSim::FUnit* Busy = SelectedUnit();
		const bool bTake = !bPlanMode && (!Busy || Busy->Id == UnitId || !PlayerCanOrder(Busy));
		if (bArrives)
		{
			GoTos.Remove(UnitId);
		}
		if (bTake)
		{
			SelectUnit(UnitId);
			OrderSelected(Walk);
		}
		else
		{
			const FString Why = Submit(Walk);
			if (!Why.IsEmpty())
			{
				Tell(FString::Printf(TEXT("%s's Go To stopped: %s"), *Name, *Why));
				GoTos.Remove(UnitId);
				return false;
			}
		}
		if (!bTake && PlayerCanOrder(Unit))
		{
			PointOut(UnitId);
		}
		Tell(bArrives ? FString::Printf(TEXT("%s has arrived: its turn is yours."), *Name)
			: FString::Printf(TEXT("%s walked on (%d turn%s to go): its turn is yours."), *Name,
				static_cast<int32>(Order->Stops.size()), Order->Stops.size() == 1 ? TEXT("") : TEXT("s")));
		return true;
	}
	const FString Why = Submit(Walk);
	if (!Why.IsEmpty())
	{
		StopGoTo(UnitId, Why);
		return false;
	}
	// Walk and end: the turn ends once the walk is in (RunDueGoTos).
	GoToEndUnit = UnitId;
	GoToEndSerial = Serial;
	return true;
}

void ATMBattleDirector::RunDueGoTos()
{
	// A walk-and-end step's turn is ended once its walk has been applied
	// (online: once the host has answered it).
	if (GoToEndUnit >= 0)
	{
		const TMSim::FUnit* Unit = TMPlans::Find(Battle, GoToEndUnit);
		if (!Unit || !Unit->IsAlive() || !Unit->bReady || Unit->Serial != GoToEndSerial)
		{
			GoToEndUnit = -1;
		}
		else if (!bWaitingForHost && Unit->bMoved)
		{
			GoToEndUnit = -1;
			TGuardValue<bool> Own(bGoToOrdering, true);
			if (PlayerCanOrder(Unit) && !Unit->bActed)
			{
				Submit(TMSim::FOrder::MakeEndTurn(Unit->Id, Unit->Serial));
			}
		}
		return;
	}
	// Fallen units' orders go with them, and so does one stopped on a turn now over.
	TArray<int32> Gone;
	for (const TPair<int32, FTMGoTo>& Pair : GoTos)
	{
		const TMSim::FUnit* Unit = TMPlans::Find(Battle, Pair.Key);
		if (!Unit || !Unit->IsAlive() || (Pair.Value.bStopped && Unit->Serial != Pair.Value.StoppedSerial))
		{
			Gone.Add(Pair.Key);
		}
	}
	for (const int32 Id : Gone)
	{
		GoTos.Remove(Id);
	}
	if (PendingAbility.UnitId >= 0 || bWaitingForHost || GoTos.Num() == 0)
	{
		return;
	}
	for (const TPair<int32, FTMGoTo>& Pair : GoTos)
	{
		const TMSim::FUnit* Unit = TMPlans::Find(Battle, Pair.Key);
		if (!Pair.Value.bStopped && PlayerCanOrder(Unit) && Unit->Serial != Pair.Value.LastSerial
			&& !Unit->bMoved && !Unit->bActed && !Unit->IsCasting())
		{
			StepGoTo(Pair.Key, false);
			return;
		}
	}
}
