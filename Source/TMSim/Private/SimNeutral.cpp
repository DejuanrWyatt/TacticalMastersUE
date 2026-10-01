// The neutral monsters' player: what a monster does with its turn, given the
// mood the rules have put it in (FBattle::MonsterTurnStarts, SimCamps.cpp).
//
// Docs/design/feat-neutral-camps.md 14. It rolls nothing: its fighting is the
// computer player's own weighing of options at its best setting, which never
// settles for a worse one, so the same battle always gets the same orders.

#include "SimAI.h"

#include "SimAbility.h"
#include "SimBattle.h"
#include "SimItem.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace TMSim
{
	namespace
	{
		constexpr double Far = std::numeric_limits<double>::infinity();

		/** How far from its anchor a fighting monster may stand. */
		double LeashOf(const FMonsterInfo& Info)
		{
			switch (Info.Temperament)
			{
			case ETemperament::GuardUnit: return Camp::WardLeash + 1.0;
			case ETemperament::GuardPlace: return static_cast<double>(Info.Ring) + 1.0;
			default: return static_cast<double>(Info.Leash);
			}
		}
	}

	FNeutralPlayer::FNeutralPlayer()
		: Fighter("hard")
	{
	}

	FVec2 FNeutralPlayer::SpotToward(FBattle& Battle, const FUnit& Unit, const std::vector<std::pair<FNode, double>>& Reach,
		const FVec2& Goal, double Desired) const
	{
		FVec2 Best = Unit.Pos;
		const std::vector<double> Field = Battle.DistanceFrom(Goal);
		auto ValueAt = [&](const FNode& Node)
		{
			const int Index = Battle.Map.NodeIndex(Node);
			const double Walk = Index >= 0 && Index < static_cast<int>(Field.size()) ? Field[static_cast<size_t>(Index)] : Far;
			return std::isfinite(Walk) ? std::abs(Walk - Desired) : Far;
		};
		double BestValue = ValueAt(FMap::NodeOf(Unit.Pos));
		for (const auto& Pair : Reach)
		{
			const double Value = ValueAt(Pair.first);
			// Strictly better, so the first found wins a tie: the same every time.
			if (Value + 1e-9 < BestValue)
			{
				BestValue = Value;
				Best = FMap::NodePos(Pair.first);
			}
		}
		return Best;
	}

	int FNeutralPlayer::PickQuarry(FBattle& Battle, const FUnit& Unit) const
	{
		const FMonsterInfo* Info = Unit.MonsterInfo();
		if (!Info)
		{
			return -1;
		}
		const FVec2 Anchor = Battle.LeashAnchor(Unit);
		const double Leash = LeashOf(*Info) + 6.0;
		int Best = -1;
		double BestScore = -Far;
		for (const FUnit& Other : Battle.Units)
		{
			if (!Battle.IsQuarry(Other))
			{
				continue;
			}
			const bool bGrudge = Other.Id == Unit.Grudge;
			if (!bGrudge && (!Battle.CanSeeUnit(Unit.Team, Other) || static_cast<double>(Other.Pos.DistanceTo(Anchor)) > Leash))
			{
				continue;
			}
			// Lured first, then its grudge, then what its traits look for, then the nearest.
			double Score = -Other.Pos.DistanceTo(Unit.Pos);
			for (const FStatus& Status : Other.Statuses)
			{
				const FStatusDef* Def = FindStatus(Status.Id);
				Score += Def && Def->bLured ? 1000.0 : 0.0;
				Score += Status.Id == "marked" ? 200.0 : 0.0;
			}
			Score += bGrudge ? 100.0 : 0.0;
			if (Info->Has(MonsterTrait::PackHunter))
			{
				int Friends = 0;
				for (const FUnit& Near : Battle.Units)
				{
					Friends += Near.Id != Other.Id && Near.IsAlive() && Near.Team == Other.Team
						&& Near.Pos.DistanceTo(Other.Pos) <= Items::AloneReach ? 1 : 0;
				}
				Score += 40.0 - Friends * 20.0 - Other.Hp * 0.1;
			}
			if (Info->Has(MonsterTrait::Scavenger) && Other.HasItems())
			{
				Score += 50.0;
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Other.Id;
			}
		}
		return Best;
	}

	FOrder FNeutralPlayer::NextCommand(FBattle& Battle, const FUnit& Unit)
	{
		const FMonsterInfo* Info = Unit.MonsterInfo();
		if (!Info || Unit.Team != 2)
		{
			return FOrder::MakeEndTurn(Unit.Id, Unit.Serial);
		}
		const bool bCanWalk = !Unit.bMoved && !Unit.IsCasting() && !Unit.IsRooted() && !(Unit.ActsOnce() && Unit.bActed);
		const bool bCanAct = !Unit.bActed && !(Unit.ActsOnce() && Unit.bMoved);
		std::vector<std::pair<FNode, double>> Reach;
		if (bCanWalk)
		{
			Reach = Battle.ReachableNodes(Unit);
		}
		const FCamp* Held = Unit.Camp >= 0 && Unit.Camp < static_cast<int>(Battle.Camps.size())
			? &Battle.Camps[static_cast<size_t>(Unit.Camp)] : nullptr;

		// A scavenger picks up whatever lies at its feet, whatever its mood.
		if (bCanAct && Info->Has(MonsterTrait::Scavenger))
		{
			const int Cache = Battle.CacheNear(Unit.Pos);
			if (Cache >= 0)
			{
				for (const FItemDef* Item : Battle.Caches[static_cast<size_t>(Cache)].Items)
				{
					if (Battle.ValidateTake(Unit.Id, Cache, Item->Id, -1).empty())
					{
						return FOrder::MakeTake(Unit.Id, Unit.Serial, Cache, Item->Id, -1);
					}
				}
			}
		}

		switch (Unit.Mind)
		{
		case EMind::Resting:
		{
			if (!bCanWalk)
			{
				break;
			}
			// Walking its route.
			if (Held && !Held->Route.empty())
			{
				const FVec2 Next = Held->Route[static_cast<size_t>(Unit.RouteStep) % Held->Route.size()];
				const FVec2 Spot = SpotToward(Battle, Unit, Reach, Next, 0.0);
				if (Spot != Unit.Pos)
				{
					return FOrder::MakeMove(Unit.Id, Unit.Serial, Spot);
				}
			}
			// A scavenger goes looking for what lies about near its home.
			if (Info->Has(MonsterTrait::Scavenger))
			{
				int Nearest = -1;
				for (int i = 0; i < static_cast<int>(Battle.Caches.size()); ++i)
				{
					const FCache& Cache = Battle.Caches[static_cast<size_t>(i)];
					if (!Cache.Items.empty() && static_cast<double>(Cache.Pos.DistanceTo(Unit.Home)) <= Info->Leash + 4.0
						&& (Nearest < 0 || Cache.Pos.DistanceTo(Unit.Pos) < Battle.Caches[static_cast<size_t>(Nearest)].Pos.DistanceTo(Unit.Pos)))
					{
						Nearest = i;
					}
				}
				if (Nearest >= 0)
				{
					const FVec2 Spot = SpotToward(Battle, Unit, Reach, Battle.Caches[static_cast<size_t>(Nearest)].Pos, 0.0);
					if (Spot != Unit.Pos)
					{
						return FOrder::MakeMove(Unit.Id, Unit.Serial, Spot);
					}
				}
			}
			break;
		}

		case EMind::Fighting:
		{
			// Only from where its leash lets it stand.
			const FVec2 Anchor = Battle.LeashAnchor(Unit);
			const double Leash = LeashOf(*Info);
			std::vector<std::pair<FNode, double>> Inside;
			for (const auto& Pair : Reach)
			{
				if (static_cast<double>(FMap::NodePos(Pair.first).DistanceTo(Anchor)) <= Leash)
				{
					Inside.push_back(Pair);
				}
			}
			if (bCanAct)
			{
				static const std::vector<std::pair<FNode, double>> Here;
				const FChoice Best = Fighter.BestAction(Battle, Unit, Unit.ActsOnce() ? Here : Inside);
				if (Best.Slot >= 0)
				{
					if (Best.Spot != Unit.Pos)
					{
						return FOrder::MakeMove(Unit.Id, Unit.Serial, Best.Spot);
					}
					return FOrder::MakeUseAbility(Unit.Id, Unit.Serial, Best.Slot, Best.Target, Best.Follow);
				}
			}
			if (bCanWalk)
			{
				const FUnit* Quarry = Battle.FindUnit(PickQuarry(Battle, Unit));
				if (Quarry)
				{
					// Just inside its own reach, as the computer player stands.
					double Range = 1.0;
					for (int Slot = 0; Slot < ClassSlots; ++Slot)
					{
						const FAbility* Ability = Unit.Ability(Slot);
						if (Ability && Ability->Effect == EEffect::Damage && Battle.AbilityBlockedReason(Unit, Slot).empty())
						{
							Range = std::max(Range, static_cast<double>(Ability->MaxRange));
						}
					}
					const FVec2 Spot = SpotToward(Battle, Unit, Inside, Quarry->Pos, std::max(1.0, Range - 1.0));
					if (Spot != Unit.Pos)
					{
						return FOrder::MakeMove(Unit.Id, Unit.Serial, Spot);
					}
				}
			}
			break;
		}

		case EMind::Returning:
			if (bCanWalk)
			{
				const FVec2 Spot = SpotToward(Battle, Unit, Reach, Battle.LeashAnchor(Unit), 0.0);
				if (Spot != Unit.Pos)
				{
					return FOrder::MakeMove(Unit.Id, Unit.Serial, Spot);
				}
			}
			break;

		case EMind::Fleeing:
			if (bCanWalk)
			{
				const FVec2 Size = Battle.Map.SizeMeters();
				const bool bToEdge = Info->Temperament == ETemperament::Skittish;
				FVec2 Best = Unit.Pos;
				double BestValue = -Far;
				auto Value = [&](const FVec2& Spot)
				{
					// Away from whoever it can see, and for a runner, towards the edge.
					double Away = 0.0;
					for (const FUnit& Other : Battle.Units)
					{
						if (Battle.IsQuarry(Other) && static_cast<double>(Other.Pos.DistanceTo(Unit.Pos)) <= Battle.SightOf(Unit))
						{
							Away += std::min(10.0, static_cast<double>(Other.Pos.DistanceTo(Spot)));
						}
					}
					const double Edge = std::min(std::min(Spot.X, Spot.Y), std::min(Size.X - Spot.X, Size.Y - Spot.Y));
					return bToEdge ? Away * 0.2 - Edge : Away;
				};
				BestValue = Value(Unit.Pos);
				for (const auto& Pair : Reach)
				{
					const FVec2 Spot = FMap::NodePos(Pair.first);
					const double Here = Value(Spot);
					if (Here > BestValue + 1e-9)
					{
						BestValue = Here;
						Best = Spot;
					}
				}
				if (Best != Unit.Pos)
				{
					return FOrder::MakeMove(Unit.Id, Unit.Serial, Best);
				}
			}
			break;

		case EMind::Alert:
			break;
		}
		return FOrder::MakeEndTurn(Unit.Id, Unit.Serial);
	}
}
