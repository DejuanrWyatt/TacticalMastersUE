#include "SimAI.h"

#include "SimAbility.h"
#include "SimMap.h"
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

	double FAIPlayer::TargetWorth(const FUnit& User, const FUnit& Target, bool bSmart) const
	{
		// Only what the other side is made of matters, and only when it is playing
		// well enough to notice. Deliberately nothing for a target that is merely
		// hurt: a finishing blow is already worth a great deal on its own, and
		// chasing whoever is wounded spread damage about instead of killing.
		if (!bSmart || Target.Team == User.Team)
		{
			return 1.0;
		}
		double Worth = 1.0;
		if (JobHasRole(Target.Job, "support"))
		{
			Worth = std::max(Worth, 1.45);
		}
		if (JobHasRole(Target.Job, "special"))
		{
			Worth = std::max(Worth, 1.25);
		}
		return Worth;
	}

	double FAIPlayer::Score(FBattle& Battle, const FUnit& User, int Slot,
		const FAbility& Ability, const std::vector<FHit>& Hits) const
	{
		double Total = 0.0;

		// A toggle is worth switching on once and never worth switching back off.
		if (Ability.Kind == "toggle")
		{
			if (User.Toggled[Slot])
			{
				return 0.0;
			}
			bool bHelps = false;
			for (const FBuff& Buff : Ability.Buffs)
			{
				bHelps = bHelps || Buff.Amount > 0;
			}
			if (!bHelps)
			{
				return 0.0;
			}
		}

		// Easy keeps the simpler reasoning: it takes the damage at face value and
		// does not think about statuses, channels or when to spend an ultimate.
		const bool bSmart = Level.Mistakes < 0.4;

		for (const FHit& Hit : Hits)
		{
			const FUnit* Target = Battle.FindUnit(Hit.UnitId);
			if (!Target)
			{
				continue;
			}
			int Amount = Hit.Amount;
			if (bSmart && Ability.Effect == EEffect::Damage)
			{
				// What it would do on average rather than at best: some of it gets
				// dodged, some of it lands hard.
				const double Evade = Battle.EvadeChance(*Target, Ability, &User) / 100.0;
				const double Crit = Battle.CritChance(User) / 100.0;
				Amount = std::max(1, RoundToInt(Amount * (1.0 - Evade)
					* (1.0 + Crit * (Battle.Tuning.CritMultiplier - 1.0))));
			}

			switch (Ability.Effect)
			{
			case EEffect::Damage:
				Total += Amount * TargetWorth(User, *Target, bSmart);
				if (Amount >= Target->Hp)
				{
					Total += 30.0;  // finishing somebody off is worth going for
				}
				break;
			case EEffect::Heal:
				Total += Amount * 1.2;
				break;
			case EEffect::Revive:
				Total += 70.0 + Amount;
				break;
			case EEffect::Support:
				// Hurrying an ally along only helps one still filling its gauge.
				if (Ability.TgChange != 0 && Target->Id != User.Id && !Target->bReady)
				{
					Total += 12.0;
				}
				if (!Ability.Buffs.empty())
				{
					Total += 8.0;
				}
				break;
			}

			// A fresh status is worth more the longer it lasts, and most of all on
			// a fast enemy about to act. An interrupt is the exception: it takes the
			// turn a unit is in, so it is worth a great deal against one that is
			// mid-turn and next to nothing against one still filling its gauge,
			// however long the status itself would have sat there.
			if (Ability.HasStatus() && Target->IsAlive() && !Target->HasStatus(Ability.StatusId))
			{
				const FStatusDef* Def = FindStatus(Ability.StatusId);
				double Worth = 10.0;
				if (Ability.StatusId == "silence") { Worth = 16.0; }
				else if (Ability.StatusId == "shield") { Worth = 14.0; }
				else if (Ability.StatusId == "root") { Worth = 12.0; }
				else if (Ability.StatusId == "stun") { Worth = 12.0; }
				else if (Ability.StatusId == "taunt") { Worth = 10.0; }
				if (bSmart)
				{
					const bool bMidTurn = Target->bReady || Target->IsCasting();
					if (Def && Def->bInterrupt)
					{
						Worth *= bMidTurn ? 2.5 : 0.2;
					}
					else
					{
						Worth *= 0.6 + 0.4 * Ability.StatusTurns;
					}
					if (Target->Team != User.Team)
					{
						Worth *= 1.0 + Target->Stat(EStat::Speed) / 20.0;
						if (bMidTurn)
						{
							Worth *= 1.5;
						}
					}
				}
				Total += Worth;
			}
		}

		// A channelled ability keeps working over its turns.
		if (bSmart && Ability.Kind == "channeled")
		{
			Total *= 1.0 + 0.4 * (Ability.Channel - 1);
		}

		// The ultimate is worth saving until it catches two, or finishes somebody.
		if (bSmart && Slot == 3 && Ability.Effect == EEffect::Damage)
		{
			int Caught = 0;
			bool bFinishes = false;
			for (const FHit& Hit : Hits)
			{
				const FUnit* Target = Battle.FindUnit(Hit.UnitId);
				if (Target && Target->Team != User.Team)
				{
					++Caught;
					bFinishes = bFinishes || Hit.Amount >= Target->Hp;
				}
			}
			if (Caught < 2 && !bFinishes)
			{
				Total *= 0.4;
			}
		}
		return Total;
	}

	int FAIPlayer::UnitAt(FBattle& Battle, const FVec2& Target, const FUnit& User,
		const FVec2& Spot, const FAbility& Ability) const
	{
		// A revive follows the fallen ally, and the caster may be standing on it.
		if (Ability.Target == ETargetSide::KoAlly)
		{
			for (const FUnit& Each : Battle.Units)
			{
				if (Each.IsKo() && Each.Team == User.Team && Each.Pos == Target)
				{
					return Each.Id;
				}
			}
			return -1;
		}
		if (Target == Spot)
		{
			// Aimed at its own feet: itself, but only if it is not walking there
			// first, because then the spell is nailed to the ground instead.
			return Spot == User.Pos ? User.Id : -1;
		}
		for (const FUnit& Each : Battle.Units)
		{
			if (Each.IsAlive() && Each.Pos == Target)
			{
				return Each.Id;
			}
		}
		return -1;
	}

	std::vector<FVec2> FAIPlayer::Spots(const FUnit& Unit,
		const std::vector<std::pair<FNode, double>>& Reach) const
	{
		// Where it is, plus everywhere it could walk to, thinned to a one-metre
		// lattice. Every spot costs a look at every target, and half a metre of
		// extra precision is not worth four times the thinking.
		std::vector<FVec2> Out;
		Out.push_back(Unit.Pos);
		for (const auto& Pair : Reach)
		{
			if (Pair.first.X % 2 == 0 && Pair.first.Y % 2 == 0)
			{
				Out.push_back(FMap::NodePos(Pair.first));
			}
		}
		return Out;
	}

	double FAIPlayer::ValueOfOption(FBattle& Battle, const FUnit& Unit, int Slot,
		const FVec2& Spot, const FVec2& Target) const
	{
		const FAbility* Ability = JobAbility(Unit.Job, Slot);
		if (!Ability)
		{
			return 0.0;
		}
		const double Worth = Score(Battle, Unit, Slot, *Ability,
			Battle.Preview(Unit, Slot, Spot, Target));
		if (Worth <= 0.0)
		{
			return 0.0;  // nothing worth doing is worth nothing, not a small amount
		}
		// A slow cast gives its target time to walk out of it.
		double Value = Worth * (1.0 - 0.08 * Ability->Cast);
		// High ground for the damage it adds, and a mild dislike of walking
		// further than it has to.
		Value += Battle.LevelAt(Spot) * 0.5 - Spot.DistanceTo(Unit.Pos) * 0.05;
		Value += GroundValue(Battle, Unit, Spot);
		return Value;
	}

	FChoice FAIPlayer::BestAction(FBattle& Battle, const FUnit& Unit,
		const std::vector<std::pair<FNode, double>>& Reach)
	{
		FChoice Best;
		const std::vector<FVec2> Stands = Spots(Unit, Reach);
		const double Sight = Battle.SightOf(Unit);

		for (int Slot = 0; Slot < 4; ++Slot)
		{
			if (!Battle.AbilityBlockedReason(Unit, Slot).empty())
			{
				continue;
			}
			const FAbility* Ability = JobAbility(Unit.Job, Slot);
			if (!Ability)
			{
				continue;
			}
			const float MinRange = Ability->MinRange;
			const float MaxRange = Ability->MaxRange;

			// Who this could be aimed at, and whether the side already sees them.
			// Neither depends on where the caster ends up standing.
			struct FCandidate { int Id; FVec2 Pos; bool bSeen; };
			std::vector<FCandidate> Candidates;
			for (const FUnit& Other : Battle.Units)
			{
				const bool bFits = Ability->Target == ETargetSide::KoAlly
					? (Other.IsKo() && Other.Team == Unit.Team)
					: (Other.IsAlive()
						&& (Other.Team != Unit.Team) == (Ability->Target == ETargetSide::Enemy));
				if (!bFits)
				{
					continue;
				}
				// Leaving this unit out of the looking, so what the rest of the
				// team can see is what lets it shoot at something it cannot.
				const bool bSeen = Other.Id == Unit.Id
					|| Battle.CanSee(Unit.Team, Other.Pos, Unit.Id);
				Candidates.push_back({ Other.Id, Other.Pos, bSeen });
			}
			const bool bNeedsLos = NeedsLineOfSight(*Ability);

			// An area ability can be aimed between two targets to catch both. Only
			// the best difficulty bothers, only for pairs it could actually cover,
			// and only a few of them, because each one costs a look at every spot.
			std::vector<FVec2> Spreads;
			if (Ability->Aoe > 0.0f && Level.Mistakes == 0.0)
			{
				const float Span = Ability->Aoe * 2.0f;
				for (size_t i = 0; i < Candidates.size() && Spreads.size() < 3; ++i)
				{
					for (size_t j = i + 1; j < Candidates.size(); ++j)
					{
						if (!(Candidates[i].bSeen || Candidates[j].bSeen))
						{
							continue;
						}
						if (Candidates[i].Pos.DistanceTo(Candidates[j].Pos) > Span)
						{
							continue;
						}
						Spreads.push_back((Candidates[i].Pos + Candidates[j].Pos) * 0.5f);
						if (Spreads.size() >= 3)
						{
							break;
						}
					}
				}
			}

			for (const FVec2& Spot : Stands)
			{
				std::vector<FVec2> Aims;
				if (MaxRange == 0.0f)
				{
					Aims.push_back(Spot);  // centred on the caster, wherever it stands
				}
				else
				{
					for (const FCandidate& Candidate : Candidates)
					{
						const FVec2 Where = Candidate.Id == Unit.Id ? Spot : Candidate.Pos;
						const float Distance = Spot.DistanceTo(Where);
						// The cheap checks first: range, then the terrain, which is
						// the dear one.
						if (Distance < MinRange || Distance > MaxRange)
						{
							continue;
						}
						if (bNeedsLos && !Battle.HasLineOfSight(Spot, Where))
						{
							continue;
						}
						if (Candidate.bSeen
							|| (Distance <= Sight
								&& (bNeedsLos || Battle.HasLineOfSight(Spot, Where))))
						{
							Aims.push_back(Where);
						}
					}
					for (const FVec2& Middle : Spreads)
					{
						const float Distance = Spot.DistanceTo(Middle);
						if (Distance >= MinRange && Distance <= MaxRange && Battle.InBounds(Middle)
							&& (!bNeedsLos || Battle.HasLineOfSight(Spot, Middle)))
						{
							Aims.push_back(Middle);
						}
					}
				}

				for (const FVec2& Target : Aims)
				{
					const double Value = ValueOfOption(Battle, Unit, Slot, Spot, Target);
					if (Value <= 0.0)
					{
						continue;
					}

					// Strictly better, so the first of equals wins, and the first
					// is the earliest slot, then the earliest spot, then the
					// earliest target: the order the original builds them in. Its
					// own sort is not a stable one, so a tie at the top could fall
					// either way there. Rather than pretend to know which way, the
					// count below lets the test insist there was no tie.
					if (Value > Best.Score)
					{
						Best.Score = Value;
						Best.Slot = Slot;
						Best.Spot = Spot;
						Best.Target = Target;
						Best.Ties = 1;
					}
					else if (Best.Slot >= 0 && Value == Best.Score)
					{
						++Best.Ties;
					}
				}
			}
		}

		if (Best.Slot >= 0)
		{
			const FAbility* Ability = JobAbility(Unit.Job, Best.Slot);
			Best.Follow = Ability ? UnitAt(Battle, Best.Target, Unit, Best.Spot, *Ability) : -1;
		}
		return Best;
	}

	FOrder FAIPlayer::NextCommand(FBattle& Battle, const FUnit& Unit)
	{
		// One order per call, and no plan kept between them. If the best thing to
		// do is from somewhere else, this returns only the walk, and the next call
		// works the whole thing out again from where the unit now stands. That
		// costs a second search and is worth it: anything may have changed in
		// between, including the target walking off.
		//
		// The action is considered before the walk, which is not merely tidy. A
		// sprint spends the action, and it would sprint whenever it had one going
		// spare, so deciding to move first would throw the turn away.

		// One search of the ground per decision, shared by everything below.
		std::vector<std::pair<FNode, double>> Reach;
		if (!Unit.bMoved && !Unit.IsCasting())
		{
			Reach = Battle.ReachableNodes(Unit);
		}

		if (!Unit.bActed)
		{
			const FChoice Best = BestAction(Battle, Unit, Reach);
			if (Best.Slot >= 0)
			{
				if (Best.Spot != Unit.Pos)
				{
					// Walk there first. The ability follows on the next call.
					return FOrder::MakeMove(Unit.Id, Unit.Serial, Best.Spot);
				}
				return FOrder::MakeUseAbility(Unit.Id, Unit.Serial, Best.Slot, Best.Target, Best.Follow);
			}
		}

		if (!Unit.bMoved && !Unit.IsCasting())
		{
			// Nothing worth doing with the action, so the walk may as well be a
			// sprint: it goes further and only costs the action already spare.
			const bool bSprint = !Unit.bActed && IsCareful();
			std::vector<std::pair<FNode, double>> Far = bSprint
				? Battle.ReachableNodes(Unit, true) : Reach;

			// Badly hurt with nothing worth doing: back off rather than walk in.
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
