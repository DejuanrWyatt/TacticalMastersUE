#include "SimAI.h"

#include "SimSort.h"

#include "SimAbility.h"
#include "SimMap.h"
#include "SimBattle.h"
#include "SimItem.h"

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
		// Ground zones (2026-10-04): the other side's are worth keeping out of.
		const double Zoned = Battle.Zones.empty() ? 0.0 : -Battle.ZoneHarm(Unit, Spot);
		return Zoned + HazardValue(Battle, Unit, Spot);
	}

	double FAIPlayer::HazardValue(const FBattle& Battle, const FUnit& Unit, const FVec2& Spot) const
	{
		const int Kind = Battle.HazardAt(Spot);
		if (Kind == 0 || (Kind > 0 && Battle.SpringRestAt(Spot) > 0))
		{
			return 0.0;  // nothing there, or a spring run dry
		}
		const double Amount = Unit.MaxHp() * (Kind > 0 ? Battle.Tuning.SpringPercent : Battle.Tuning.HazardPercent) * 0.01;
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
		for (int Slot = 0; Slot < AbilitySlots; ++Slot)
		{
			if (const FAbility* Ability = Unit.Ability(Slot))
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
			const FAbility* First = Unit.Ability(0);
			const FAbility* Second = Unit.Ability(1);
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

	FVec2 FAIPlayer::TowerSpot(FBattle& Battle, const FUnit& Unit,
		const std::vector<std::pair<FNode, double>>& Reach) const
	{
		// Only with nobody to fight: a unit that can see an enemy goes on
		// thinking about the enemy.
		for (const FUnit* Enemy : Battle.TeamUnits(1 - Unit.Team))
		{
			if (Battle.CanSee(Unit.Team, Enemy->Pos))
			{
				return Unit.Pos;
			}
		}
		// Every tower its side does not hold yet.
		std::vector<FVec2> Goals;
		for (const FWatchtower& Tower : Battle.Watchtowers)
		{
			if (Tower.Owner != Unit.Team)
			{
				Goals.push_back(Tower.Pos);
			}
		}
		if (Goals.empty())
		{
			return Unit.Pos;
		}
		// Already standing at one it can take: stay (it had no action to spend,
		// or the tower is contested, and walking off would waste the ground).
		for (const FVec2& Goal : Goals)
		{
			if (static_cast<double>(Unit.Pos.DistanceTo(Goal)) <= Watchtower::Reach)
			{
				return Unit.Pos;
			}
		}
		// The reachable spot that walks nearest to one; the first found on a tie.
		FVec2 Best = Unit.Pos;
		double BestDistance = Battle.DistanceToNearest(Goals, FMap::NodeOf(Unit.Pos));
		for (const auto& Pair : Reach)
		{
			const double Distance = Battle.DistanceToNearest(Goals, Pair.first);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = FMap::NodePos(Pair.first);
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
		// A boss on the hunt (2026-10-02, a setup option) goes for its prey above all.
		const double Hunt = User.bMonster && User.HuntTarget >= 0 && Target.Id == User.HuntTarget ? 3.0 : 1.0;
		if (!bSmart || Target.Team == User.Team)
		{
			return Hunt;
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
		return Worth * Hunt;
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

		if (Ability.LaysZone())
		{
			return ZoneScore(Battle, User, Ability, Hits, bSmart);
		}

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
				// Defense model 1: an evasion only takes part of the hit away.
				const double Evade = Battle.EvadeChance(*Target, Ability, &User) / 100.0
					* (Battle.NewDefense() ? Combat::EvadedShare : 1.0);
				const double Crit = Battle.CritChance(User) / 100.0;
				Amount = std::max(1, RoundToInt(Amount * (1.0 - Evade)
					* (1.0 + Crit * (Battle.Tuning.CritMultiplier - 1.0))));
			}

			switch (Ability.Effect)
			{
			case EEffect::Damage:
				if (Target->Team == User.Team)
				{
					// Friendly fire: its own side in the blast counts against it,
					// more than the same harm to an enemy counts for it.
					Total -= Amount * 1.5 + (Amount >= Target->Hp ? 60.0 : 0.0);
					break;
				}
				Total += Amount * TargetWorth(User, *Target, bSmart);
				if (Amount >= Target->Hp)
				{
					Total += 30.0;  // finishing somebody off is worth going for
				}
				break;
			case EEffect::Heal:
				// Decay turns a heal into harm (feat-status-effects.md).
				Total += Target->HasStatus("decay") ? -Amount * 1.5 : Amount * 1.2;
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
				else if (Ability.StatusId == "stop" || Ability.StatusId == "charmed") { Worth = 16.0; }
				else if (Ability.StatusId == "reraise") { Worth = 14.0; }
				else if (Ability.StatusId == "haste" || Ability.StatusId == "marked") { Worth = 12.0; }
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
				// A harm laid on its own side counts against it (2026-10-05: Overcharge's Slow).
				if (Def && Def->bHarmful && Target->Team == User.Team && Ability.Effect == EEffect::Support)
				{
					Worth = -Worth;
				}
				Total += Worth;
			}
		}

		// The new spells that do nothing a number shows (2026-10-05).
		if (!Ability.Special.empty())
		{
			Total += SpellWorth(Battle, User, Ability, Hits);
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

	double FAIPlayer::ZoneScore(FBattle& Battle, const FUnit& User, const FAbility& Ability,
		const std::vector<FHit>& Hits, bool bSmart) const
	{
		// Ground (2026-10-04): what it does to each unit standing in it now, which
		// will be touched as its turn begins unless something moves it first, and
		// a little more for every turn the ground stays to keep others out.
		double Total = 0.0;
		for (const FHit& Hit : Hits)
		{
			const FUnit* Target = Battle.FindUnit(Hit.UnitId);
			if (!Target || !Target->IsAlive())
			{
				continue;
			}
			if (Ability.bZoneHide)
			{
				// Smoke over its own: more for the hurt, nothing for one already hidden.
				if (Target->Team == User.Team && !Battle.Hidden(1 - User.Team, *Target))
				{
					Total += 6.0 + (Target->Hp < Target->MaxHp() / 2 ? 6.0 : 0.0);
				}
				continue;
			}
			if (Target->Team == User.Team)
			{
				// Ground that helps its own side (2026-10-05, Ice Slide).
				Total += Ability.Target == ETargetSide::Ally && Ability.HasStatus() && !Target->HasStatus(Ability.StatusId) ? 5.0 : 0.0;
				continue;
			}
			// Its harm most likely lands twice before it walks clear, and ground it
			// stands in is ground it has to give up.
			double Worth = Hit.Amount * 2.0 * TargetWorth(User, *Target, bSmart) + (Ability.ZoneSight > 0.0f ? 0.0 : 4.0);
			if (Ability.HasStatus() && !Target->HasStatus(Ability.StatusId))
			{
				Worth += Ability.StatusId == "silence" ? 16.0 : Ability.StatusId == "root" ? 12.0 : 10.0;
			}
			if (!Ability.ZoneStatus2.empty() && !Target->HasStatus(Ability.ZoneStatus2))
			{
				Worth += 6.0;
			}
			const std::string& Element = ElementOf(Ability);
			if ((Element == "lightning" || Element == "ice") && Target->HasStatus("wet"))
			{
				Worth += 12.0;  // a shock or a freeze waiting for it
			}
			else if (Element == "fire" && Target->HasStatus("oiled"))
			{
				Worth += 8.0;
			}
			else if (!Element.empty())
			{
				Worth += 3.0;
			}
			if (Ability.ZoneSight > 0.0f)
			{
				// Light where its side is blind: worth most on what it can't see now.
				Worth += Battle.CanSeeUnit(User.Team, *Target) ? 2.0 : 12.0;
			}
			Total += Worth;
		}
		if (bSmart)
		{
			Total *= 1.0 + 0.15 * (Ability.ZoneTurns - 1);
		}
		return Total;
	}

	double FAIPlayer::SpellWorth(FBattle& Battle, const FUnit& User, const FAbility& Ability, const std::vector<FHit>& Hits) const
	{
		// Rough worths for what the new spells do (2026-10-05) that no damage,
		// healing or status number says. Nothing here for a spell it can't judge
		// (Dash, Grapple, Recall, gates): it leaves those to a person.
		const std::string& Special = Ability.Special;
		double Total = 0.0;
		const FVec2 Stand = Hits.empty() ? User.Pos : Hits[0].Where;
		if (Special == "disengage")
		{
			// Out of reach of an enemy at its elbow, for anything but a tank.
			if (!FBattle::HoldsTheLine(User))
			{
				for (const FUnit& Other : Battle.Units)
				{
					if (Other.IsAlive() && !Other.bOffBoard && Other.Team != User.Team && Other.Pos.DistanceTo(Stand) <= 2.0f)
					{
						return 14.0;
					}
				}
			}
			return 0.0;
		}
		for (const FHit& Hit : Hits)
		{
			const FUnit* Target = Battle.FindUnit(Hit.UnitId);
			if (!Target || !Target->IsAlive() || Target->Id == User.Id || Target->Team != User.Team)
			{
				continue;
			}
			const double Share = Target->MaxHp() > 0 ? static_cast<double>(Target->Hp) / Target->MaxHp() : 1.0;
			const double Mine = User.MaxHp() > 0 ? static_cast<double>(User.Hp) / User.MaxHp() : 1.0;
			if (Special == "transfer")
			{
				for (const FStatus& Status : Target->Statuses)
				{
					const FStatusDef* Def = FindStatus(Status.Id);
					Total += Def && Def->bHarmful && Status.Id != "charmed" && Status.Id != "hunted" ? 8.0 : 0.0;
				}
			}
			else if (Special == "pact")
			{
				int Waiting = 0;
				for (int Slot = 0; Slot < 3; ++Slot)
				{
					Waiting += Target->Cooldowns[Slot];
				}
				Total += Waiting >= 3 && Mine > 0.5 ? 10.0 : 0.0;
			}
			else if (Special == "spiritswap")
			{
				Total += Share < 0.35 && Mine > 0.7 ? (Mine - Share) * 40.0 : 0.0;
			}
			else if (Special == "rally" && Share < 0.4)
			{
				for (const FUnit& Other : Battle.Units)
				{
					if (Other.IsAlive() && !Other.bOffBoard && Other.Team != User.Team && Other.Pos.DistanceTo(Target->Pos) <= 2.0f)
					{
						Total += 12.0;
						break;
					}
				}
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
		const FAbility* Ability = Unit.Ability(Slot);
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
		// Somewhere to land, a vault's far side, cover to hop into (2026-10-05).
		if (!Ability->Special.empty() && !Battle.SpecialProblem(Unit, *Ability, Spot, Target).empty())
		{
			return 0.0;
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
		// Every option worth anything, in the order Godot builds them: slot, then
		// spot, then target (ai_player.gd:118-148).
		std::vector<FChoice> Options;
		const std::vector<FVec2> Stands = Spots(Unit, Reach);
		const double Sight = Battle.SightOf(Unit);
		const FUnit* Taunter = Battle.FindUnit(Unit.TauntedBy());
		if (Taunter && !Taunter->IsAlive())
		{
			Taunter = nullptr;
		}

		for (int Slot = 0; Slot < AbilitySlots; ++Slot)
		{
			if (!Battle.AbilityBlockedReason(Unit, Slot).empty())
			{
				continue;
			}
			const FAbility* Ability = Unit.Ability(Slot);
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
				// Vanished, or lying in ambush: not something it knows to aim at.
				if (!bFits || Battle.Hidden(Unit.Team, Other))
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
						// And ground the side can see, or will once the caster stands
						// there, as for a unit (2026-10-01): a midpoint between a seen
						// enemy and an unseen one can lie in the fog, and the rules
						// refuse an aim there ("You can't see that spot").
						if (Distance >= MinRange && Distance <= MaxRange && Battle.InBounds(Middle)
							&& (!bNeedsLos || Battle.HasLineOfSight(Spot, Middle))
							&& (Battle.CanSee(Unit.Team, Middle)
								|| (Distance <= Sight && (bNeedsLos || Battle.HasLineOfSight(Spot, Middle)))))
						{
							Aims.push_back(Middle);
						}
					}
				}

				for (const FVec2& Target : Aims)
				{
					// Taunted (also a divergence from Godot, as NextCommand says): an
					// attack that could reach the taunter has to catch it, as
					// FBattle::Validate insists, so no other aim is considered.
					if (Taunter && Ability->Effect == EEffect::Damage
						&& Battle.InAbilityRange(Unit, Slot, Spot, Taunter->Pos)
						&& !Battle.InShape(*Ability, Spot, Target, Taunter->Pos))
					{
						continue;
					}
					const double Value = ValueOfOption(Battle, Unit, Slot, Spot, Target);
					if (Value <= 0.0)
					{
						continue;
					}

					FChoice Option;
					Option.Score = Value;
					Option.Slot = Slot;
					Option.Spot = Spot;
					Option.Target = Target;
					Options.push_back(Option);
				}
			}
		}
		if (Options.empty())
		{
			return FChoice();
		}

		// Best first, with Godot's own sort: it is not a stable one, but it is a
		// fixed one, so given the same options in the same order it settles a tie
		// the same way every time (SimSort.h). Then an easy or medium player may
		// settle for one of the few best instead (ai_player.gd:151-155).
		GodotSort(Options, [](const FChoice& A, const FChoice& B) { return A.Score > B.Score; });
		FChoice Best = Options[0];
		if (Level.Mistakes > 0.0 && static_cast<double>(Rng.Randf()) < Level.Mistakes)
		{
			const int64_t Among = std::min<int64_t>(Level.Top, static_cast<int64_t>(Options.size()));
			Best = Options[static_cast<size_t>(Rng.RandiRange(0, Among - 1))];
		}
		// How many shared the top score, for the parity test to report.
		Best.Ties = 0;
		for (const FChoice& Option : Options)
		{
			if (Option.Score == Options[0].Score)
			{
				++Best.Ties;
			}
		}

		if (Best.Slot >= 0)
		{
			const FAbility* Ability = Unit.Ability(Best.Slot);
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

		// DIVERGES FROM GODOT, by the human's decision (2026-09-28). Godot's
		// computer player (ai_player.gd:43-50) takes no notice of Root, Freeze or
		// Knockdown: it plans a walk the rules then refuse, drops the refused
		// order, thinks again, and stalls until its turn runs out. This one plans
		// only what those statuses allow: no walk while rooted or frozen, and a
		// knocked-down unit walks or acts, never both. With none of them on the
		// unit every choice below is Godot's, which is why the recorded battles
		// (SimTraceTest, SimAIActionTest) still match decision for decision.
		// The side's stash first (2026-10-01): an item waiting there goes on
		// whoever has room, this unit before the rest. Free, so the next call
		// carries on with the turn itself.
		if (!Unit.bMonster && (Unit.Team == 0 || Unit.Team == 1) && Unit.Team == Unit.HomeTeam() && !Battle.Stash[Unit.Team].empty())
		{
			for (const FBattle::FStashed& Held : Battle.Stash[Unit.Team])
			{
				if (!Held.Item)
				{
					continue;
				}
				if (Battle.ValidateEquip(Unit.Id, Held.Item->Id, -1).empty())
				{
					return FOrder::MakeEquip(Unit.Id, Held.Item->Id, -1);
				}
				for (const FUnit& Ally : Battle.Units)
				{
					if (Ally.Id != Unit.Id && Ally.Team == Unit.Team && Battle.ValidateEquip(Ally.Id, Held.Item->Id, -1).empty())
					{
						return FOrder::MakeEquip(Ally.Id, Held.Item->Id, -1);
					}
				}
			}
		}

		const bool bCanWalk = !Unit.bMoved && !Unit.IsCasting() && !Unit.IsRooted()
			&& !(Unit.ActsOnce() && Unit.bActed);
		const bool bCanAct = !Unit.bActed && !(Unit.ActsOnce() && Unit.bMoved);

		// One search of the ground per decision, shared by everything below.
		std::vector<std::pair<FNode, double>> Reach;
		if (bCanWalk)
		{
			Reach = Battle.ReachableNodes(Unit);
		}

		if (bCanAct)
		{
			// Knocked down, an action cannot follow a walk: it acts from here.
			static const std::vector<std::pair<FNode, double>> Here;
			const FChoice Best = BestAction(Battle, Unit, Unit.ActsOnce() ? Here : Reach);
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

		// Watchtowers (not Godot's; Docs/design/feat-objectives.md). With none in
		// the battle nothing here runs, so every choice above and below is still
		// Godot's. Nothing worth hitting: spend the turn taking a tower if it is
		// standing at one; and with no enemy in sight, walk to one rather than
		// towards where the other side started.
		if (!Battle.Watchtowers.empty())
		{
			if (bCanAct)
			{
				for (int Tower = 0; Tower < static_cast<int>(Battle.Watchtowers.size()); ++Tower)
				{
					if (Battle.ValidateCapture(Unit.Id, Tower).empty())
					{
						return FOrder::MakeCapture(Unit.Id, Unit.Serial, Tower);
					}
				}
			}
			if (bCanWalk && bCanAct)
			{
				const FVec2 Tower = TowerSpot(Battle, Unit, Reach);
				if (Tower != Unit.Pos)
				{
					// Walked, not sprinted: a sprint spends the action the capture needs.
					return FOrder::MakeMove(Unit.Id, Unit.Serial, Tower);
				}
			}
		}

		// Neutral camps (not Godot's; Docs/design/feat-neutral-camps.md 12.6).
		// With none in the battle nothing here runs. Loot within reach is taken
		// with a spare action; with no enemy in sight it walks to loot it can see,
		// or to a camp it can see awake -- the boss only while its side is ahead.
		if (!Battle.Camps.empty() || !Battle.Caches.empty())
		{
			// Loot within reach goes to the side's stash, free, whatever it carries.
			if (!Unit.bMonster && Unit.PetOf < 0 && (Unit.Team == 0 || Unit.Team == 1))
			{
				const int Cache = Battle.CacheNear(Unit.Pos);
				if (Cache >= 0 && !Battle.Caches[static_cast<size_t>(Cache)].Items.empty())
				{
					const std::string& First = Battle.Caches[static_cast<size_t>(Cache)].Items[0]->Id;
					if (Battle.ValidateTake(Unit.Id, Cache, First, -1).empty())
					{
						return FOrder::MakeTake(Unit.Id, Unit.Serial, Cache, First, -1);
					}
				}
			}
			if (bCanAct && Unit.bMonster && (Unit.Team == 0 || Unit.Team == 1))
			{
				const int Cache = Battle.CacheNear(Unit.Pos);
				if (Cache >= 0)
				{
					// The best tier it can take; into an empty slot, or over its cheapest item if the new one is better.
					const FItemDef* Best = nullptr;
					for (const FItemDef* Item : Battle.Caches[static_cast<size_t>(Cache)].Items)
					{
						if (!Unit.Carries(Item->Id) && (!Best || Item->Tier > Best->Tier))
						{
							Best = Item;
						}
					}
					int Slot = -1;
					if (Best && Unit.Gear[0] && Unit.Gear[1] && Unit.Gear[2])
					{
						for (int i = 0; i < Items::Slots; ++i)
						{
							if (Unit.Gear[i]->Tier < Best->Tier && (Slot < 0 || Unit.Gear[i]->Tier < Unit.Gear[Slot]->Tier))
							{
								Slot = i;
							}
						}
						Best = Slot >= 0 ? Best : nullptr;
					}
					if (Best && Battle.ValidateTake(Unit.Id, Cache, Best->Id, Slot).empty())
					{
						return FOrder::MakeTake(Unit.Id, Unit.Serial, Cache, Best->Id, Slot);
					}
				}
			}
			bool bEnemySeen = false;
			for (const FUnit* Enemy : Battle.TeamUnits(1 - Unit.Team))
			{
				bEnemySeen = bEnemySeen || Battle.CanSeeUnit(Unit.Team, *Enemy);
			}
			if (bCanWalk && !bEnemySeen && (Unit.Team == 0 || Unit.Team == 1))
			{
				std::vector<FVec2> Goals;
				for (const FCache& Cache : Battle.Caches)
				{
					if (!Cache.Items.empty() && Battle.CanSee(Unit.Team, Cache.Pos))
					{
						Goals.push_back(Cache.Pos);
					}
				}
				const bool bAhead = Battle.HealthShare(Unit.Team) > Battle.HealthShare(1 - Unit.Team) + 0.1;
				for (const FUnit& Monster : Battle.Units)
				{
					if (Monster.bMonster && Monster.Team == 2 && Monster.IsAlive() && Battle.CanSeeUnit(Unit.Team, Monster))
					{
						const FMonsterInfo* Info = Monster.MonsterInfo();
						if (Info && (Info->Tier < 3 || bAhead) && Info->Temperament != ETemperament::Skittish)
						{
							Goals.push_back(Monster.Pos);
						}
					}
				}
				if (!Goals.empty())
				{
					FVec2 Best = Unit.Pos;
					double BestDistance = Battle.DistanceToNearest(Goals, FMap::NodeOf(Unit.Pos));
					for (const auto& Pair : Reach)
					{
						const double Distance = Battle.DistanceToNearest(Goals, Pair.first);
						if (Distance < BestDistance && Distance >= 1.0)
						{
							BestDistance = Distance;
							Best = FMap::NodePos(Pair.first);
						}
					}
					if (Best != Unit.Pos && BestDistance < 40.0)
					{
						return FOrder::MakeMove(Unit.Id, Unit.Serial, Best);
					}
				}
			}
		}

		if (bCanWalk)
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
