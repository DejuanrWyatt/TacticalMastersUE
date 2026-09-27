#include "SimAI.h"

#include "SimAbility.h"
#include "SimBattle.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace TMSim
{
	namespace
	{
		constexpr double Infinity = std::numeric_limits<double>::infinity();
	}

	FAIPlayer::FAIPlayer(const std::string& Difficulty)
	{
		SetDifficulty(Difficulty);
		Rng.Seed(0);
	}

	void FAIPlayer::SetDifficulty(const std::string& Difficulty)
	{
		if (Difficulty == "easy")
		{
			Level = FSkill{ 2.5, 1.2, 0.45, 5 };
		}
		else if (Difficulty == "medium")
		{
			Level = FSkill{ 1.3, 0.9, 0.2, 3 };
		}
		else
		{
			Level = FSkill{ 0.4, 0.6, 0.0, 1 };  // hard never makes a mistake
		}
	}

	double FAIPlayer::GroundValue(const FBattle& Battle, const FUnit& Unit, const FVec2& Spot) const
	{
		const int Kind = Battle.HazardAt(Spot);
		if (Kind == 0)
		{
			return 0.0;
		}
		const double Amount = Unit.MaxHp() * Battle.Tuning.HazardPercent * 0.01;
		if (Kind < 0)
		{
			return -Amount * 1.5;  // embers: worth going out of the way to avoid
		}
		// A spring is worth much more to something that is hurt.
		return Amount * (Unit.Hp < Unit.MaxHp() * 0.6 ? 2.0 : 0.2);
	}

	FVec2 FAIPlayer::ApproachSpot(FBattle& Battle, const FUnit& Unit, bool bSprint)
	{
		// Only what this side can see. A unit does not walk at an enemy nobody
		// has spotted, which is what makes sight worth having.
		std::vector<FVec2> Goals;
		for (const FUnit* Enemy : Battle.TeamUnits(1 - Unit.Team))
		{
			if (Battle.CanSee(Unit.Team, Enemy->Pos))
			{
				Goals.push_back(Enemy->Pos);
			}
		}
		if (Goals.empty())
		{
			// Nobody in sight: head for where the other side started.
			Goals.push_back(Battle.SpawnPoints[1 - Unit.Team]);
		}

		// How far it can actually threaten from, with what it can use now.
		// Only what it could actually use this turn. An ultimate whose meter is
		// empty reaches a long way and is no use at all, so counting it would
		// have the unit hang back at ultimate range for the first half of the
		// battle waiting for something it cannot cast.
		double AttackRange = 0.0;
		for (int Slot = 0; Slot < 4; ++Slot)
		{
			if (const FAbility* Ability = JobAbility(Unit.Job, Slot))
			{
				if (Ability->Effect == EEffect::Damage
					&& Battle.AbilityBlockedReason(Unit, Slot).empty())
				{
					AttackRange = std::max(AttackRange, static_cast<double>(Ability->MaxRange));
				}
			}
		}
		if (AttackRange <= 0.0)
		{
			const FAbility* First = JobAbility(Unit.Job, 0);
			const FAbility* Second = JobAbility(Unit.Job, 1);
			AttackRange = std::max(First ? First->MaxRange : 0.0f, Second ? Second->MaxRange : 0.0f);
		}
		// Just inside its own reach, so it can shoot without being walked over.
		const double Desired = std::max(1.0, AttackRange - 1.0);

		FVec2 Best = Unit.Pos;
		double BestValue = Infinity;
		for (const auto& Pair : Battle.ReachableNodes(Unit, bSprint))
		{
			const double Distance = Battle.DistanceToNearest(Goals, Pair.first);
			const FVec2 Spot = FMap::NodePos(Pair.first);
			// Near enough to threaten, high ground for the damage it adds, and
			// away from ground that burns.
			const double Value = std::abs(std::min(Distance, 999.0) - Desired)
				- Battle.Map.NodeLevel(Pair.first) * 0.1
				- GroundValue(Battle, Unit, Spot) * 0.1;
			if (Value < BestValue)
			{
				BestValue = Value;
				Best = Spot;
			}
		}
		return Best;
	}

	FVec2 FAIPlayer::RetreatSpot(FBattle& Battle, const FUnit& Unit, bool bSprint)
	{
		std::vector<FVec2> Seen;
		for (const FUnit* Enemy : Battle.TeamUnits(1 - Unit.Team))
		{
			if (Battle.CanSee(Unit.Team, Enemy->Pos))
			{
				Seen.push_back(Enemy->Pos);
			}
		}
		if (Seen.empty())
		{
			return Unit.Pos;  // nothing to back away from
		}

		FVec2 Best = Unit.Pos;
		double BestDistance = -Infinity;
		for (const auto& Pair : Battle.ReachableNodes(Unit, bSprint))
		{
			const FVec2 Spot = FMap::NodePos(Pair.first);
			const double Distance = std::min(Battle.DistanceToNearest(Seen, Pair.first), 999.0)
				+ GroundValue(Battle, Unit, Spot) * 0.1;
			if (Distance > BestDistance)
			{
				BestDistance = Distance;
				Best = Spot;
			}
		}
		return Best;
	}

	FOrder FAIPlayer::NextCommand(FBattle& Battle, const FUnit& Unit)
	{
		// Deciding what to do with the turn -- which ability, on whom -- comes
		// with abilities. Until then it decides where to stand, which is the
		// half of a tactics game that happens on the ground.

		if (!Unit.bMoved)
		{
			// Nothing worth doing with the action, so the walk may as well be a
			// sprint: it goes further and only costs the action already spare.
			const bool bSprint = !Unit.bActed && IsCareful();

			// Badly hurt with nothing to do: back off rather than walk in.
			if (IsCareful() && Unit.Hp < Unit.MaxHp() * 0.3)
			{
				const FVec2 Away = RetreatSpot(Battle, Unit, bSprint);
				if (Away != Unit.Pos)
				{
					return FOrder::MakeMove(Unit.Id, Unit.Serial, Away, bSprint);
				}
			}

			const FVec2 Towards = ApproachSpot(Battle, Unit, bSprint);
			if (Towards != Unit.Pos)
			{
				return FOrder::MakeMove(Unit.Id, Unit.Serial, Towards, bSprint);
			}
		}

		return FOrder::MakeEndTurn(Unit.Id, Unit.Serial);
	}
}
