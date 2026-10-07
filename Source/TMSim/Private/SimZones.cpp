// Ground zones (2026-10-04, the "area denial" mockups; Docs/design/feat-ground-zones.md).
//
// An ability with "special": "zone" lays its shape on the ground where it was
// aimed, and the ground keeps doing what the ability says for a number of its
// caster's turns: it hurts, it hangs statuses on whoever stands in it, it sees
// for its side, it hides its side. Everything here is built from what the
// rules already had -- burning ground's harm, the statuses, the element
// reactions, tall grass and watchtower sight -- and nothing here does anything
// in a battle where nobody has laid a zone, so every older battle plays as
// before.
//
// When a zone touches a unit: as the unit's turn begins in it, and when it ends
// a walk in it, once a turn whichever comes first. A zone does nothing as it is
// laid; the caster's aim is to have it waiting.

#include "SimAbility.h"
#include "SimBattle.h"

#include <algorithm>
#include <cmath>

namespace TMSim
{
	namespace
	{
		/** What a burning zone's fire is, for the element reactions. */
		const FAbility& FireTouch()
		{
			static const FAbility Fire = []
			{
				FAbility Out;
				Out.Id = "zone_fire";
				Out.Effect = EEffect::Support;
				Out.Element = "fire";
				return Out;
			}();
			return Fire;
		}

		/** Whether touching does anything at all (a flare's or a smoke's touches nobody). */
		bool ZoneActs(const FAbility& Ability, bool bIgnited)
		{
			return bIgnited || Ability.ZonePercent > 0.0f || Ability.HasStatus() || !Ability.ZoneStatus2.empty()
				|| !ElementOf(Ability).empty();
		}
	}

	const FAbility* FBattle::ZoneAbility(const FZone& Zone) const
	{
		return FindAbility(Zone.AbilityId);
	}

	bool FBattle::ZoneIsWarning(const FZone& Zone) const
	{
		const FAbility* Ability = ZoneAbility(Zone);
		return Ability && Ability->Special == "warned";
	}

	bool FBattle::ZoneLive(const FZone& Zone) const
	{
		for (const FUnit& Each : Units)
		{
			if (Each.Id == Zone.Owner)
			{
				return Each.IsAlive();
			}
		}
		return false;
	}

	bool FBattle::ZoneCovers(const FZone& Zone, const FVec2& Point) const
	{
		const FAbility* Ability = ZoneAbility(Zone);
		return Ability && InShape(*Ability, Zone.From, Zone.Target, Point);
	}

	bool FBattle::ZoneTouches(const FZone& Zone, const FUnit& Unit, const FVec2& Point) const
	{
		const FAbility* Ability = ZoneAbility(Zone);
		if (!Ability || !Unit.IsAlive() || Unit.bOffBoard || Unit.Flies() || !ZoneActs(*Ability, Zone.bIgnited) || !ZoneLive(Zone)
			|| Ability->Special == "warned")
		{
			return false;
		}
		const bool bForAllies = Ability->Target == ETargetSide::Ally;
		if ((Unit.Team == Zone.Team) != bForAllies)
		{
			return false;
		}
		return InShape(*Ability, Zone.From, Zone.Target, Point);
	}

	bool FBattle::ZoneSees(int Team, const FVec2& Point) const
	{
		for (const FZone& Zone : Zones)
		{
			if (Zone.Team != Team || !ZoneLive(Zone))
			{
				continue;
			}
			const FAbility* Ability = ZoneAbility(Zone);
			if (Ability && Ability->ZoneSight > 0.0f && Zone.Target.DistanceTo(Point) <= Ability->ZoneSight)
			{
				return true;
			}
		}
		return false;
	}

	double FBattle::ZoneHarm(const FUnit& Unit, const FVec2& Point) const
	{
		double Harm = 0.0;
		for (const FZone& Zone : Zones)
		{
			const FAbility* Ability = ZoneAbility(Zone);
			if (!Ability || !ZoneLive(Zone))
			{
				continue;
			}
			// A warned blow (2026-10-06) waiting to land on the other side: somewhere
			// not to be when its caster's turn comes.
			if (Ability->Special == "warned")
			{
				const bool bAimedAt = (Unit.Team == Zone.Team) == (Ability->Target == ETargetSide::Ally);
				if (bAimedAt && !Unit.Flies() && ZoneCovers(Zone, Point))
				{
					Harm += 12.0 + (Ability->HasStatus() ? 8.0 : 0.0);
				}
				continue;
			}
			// Smoke of its own side: somewhere to hide. Ground of its own side that
			// helps it (2026-10-05, Ice Slide): somewhere to stand.
			if ((Ability->bZoneHide || Ability->Target == ETargetSide::Ally) && Unit.Team == Zone.Team && !Unit.Flies() && ZoneCovers(Zone, Point))
			{
				Harm -= 4.0;
				continue;
			}
			if (!ZoneTouches(Zone, Unit, Point))
			{
				continue;
			}
			const double Percent = Zone.bIgnited ? IgnitedPercent : Ability->ZonePercent;
			Harm += Unit.MaxHp() * Percent * 0.01 * 1.5;
			const bool bOnce = Ability->bZoneOnce
				&& std::find(Zone.Once.begin(), Zone.Once.end(), Unit.Id) != Zone.Once.end();
			if (!bOnce)
			{
				Harm += (Ability->HasStatus() || Zone.bIgnited ? 8.0 : 0.0) + (Ability->ZoneStatus2.empty() ? 0.0 : 6.0);
			}
			Harm += ElementOf(Zone.bIgnited ? FireTouch() : *Ability).empty() ? 0.0 : 3.0;
		}
		return Harm;
	}

	void FBattle::LayZone(FUnit& User, int Slot, const FAbility& Ability, const FVec2& Target, FTickReport& Report, int Turns)
	{
		// One of each ability per caster: laid again, the old one goes.
		for (size_t i = 0; i < Zones.size(); ++i)
		{
			if (Zones[i].Owner == User.Id && Zones[i].AbilityId == Ability.Id)
			{
				FEvent Ended;
				Ended.Kind = EEventKind::ZoneEnded;
				Ended.Unit = User.Id;
				Ended.Slot = Zones[i].Slot;
				Ended.Where = Zones[i].Target;
				Ended.Id = Ability.Id;
				Report.Events.push_back(Ended);
				Zones.erase(Zones.begin() + static_cast<std::ptrdiff_t>(i));
				break;
			}
		}
		FZone Zone;
		Zone.Owner = User.Id;
		Zone.Team = User.Team;
		Zone.Slot = Slot;
		Zone.AbilityId = Ability.Id;
		Zone.From = User.Pos;
		Zone.Target = Target;
		// A warned blow (2026-10-06) waits one of its caster's turns, whatever it says.
		const int Lasts = Turns > 0 ? Turns : Ability.ZoneTurns;
		Zone.Turns = Lasts;
		Zone.Total = Lasts;
		Zones.push_back(Zone);

		FEvent Laid;
		Laid.Kind = EEventKind::ZoneLaid;
		Laid.Unit = User.Id;
		Laid.Slot = Slot;
		Laid.Amount = Lasts;
		Laid.Where = Target;
		Laid.Id = Ability.Id;
		Report.Events.push_back(Laid);
	}

	bool FBattle::ZonesAtTurnStart(FUnit& Unit, FTickReport& Report)
	{
		if (Zones.empty())
		{
			return false;
		}
		// The zones it laid count down on its turns; a fallen caster's go at once.
		// A warned blow whose wait is over lands now (2026-10-06), once the
		// zones are settled.
		std::vector<FZone> Kept;
		std::vector<FZone> Strikes;
		Kept.reserve(Zones.size());
		for (FZone& Zone : Zones)
		{
			const FUnit* Owner = FindUnit(Zone.Owner);
			if (Zone.Owner == Unit.Id)
			{
				--Zone.Turns;
			}
			if (Zone.Owner == Unit.Id && Zone.Turns <= 0 && Owner && Owner->IsAlive() && ZoneIsWarning(Zone))
			{
				Strikes.push_back(Zone);
			}
			if (!Owner || !Owner->IsAlive() || Zone.Turns <= 0)
			{
				FEvent Ended;
				Ended.Kind = EEventKind::ZoneEnded;
				Ended.Unit = Zone.Owner;
				Ended.Slot = Zone.Slot;
				Ended.Where = Zone.Target;
				Ended.Id = Zone.AbilityId;
				Report.Events.push_back(Ended);
				continue;
			}
			Kept.push_back(Zone);
		}
		Zones.swap(Kept);
		for (const FZone& Strike : Strikes)
		{
			const FAbility* Ability = Unit.Ability(Strike.Slot);
			if (!Unit.IsAlive() || Winner != -1 || !Ability || Ability->Id != Strike.AbilityId)
			{
				continue;
			}
			bWarnedStrike = true;
			WarnedFrom = Strike.From;
			ResolveAbility(Unit, Strike.Slot, Strike.Target, Report);
			bWarnedStrike = false;
		}
		if (!Unit.IsAlive() || Winner != -1)
		{
			return false;
		}

		// What leaves it able to do nothing at all: a status that takes its orders,
		// or one that holds it still and silent both (Freeze).
		auto Helpless = [](const FUnit& Who)
		{
			std::string Id = Who.NoOrdersStatus();
			for (const FStatus& Status : Who.Statuses)
			{
				const FStatusDef* Def = FindStatus(Status.Id);
				if (Id.empty() && Def && Def->bNoMove && Def->bNoAbilities)
				{
					Id = Status.Id;
				}
			}
			return Id;
		};
		const std::string Before = Helpless(Unit);
		const bool bWasStunned = Unit.HasStatus("stun");
		for (size_t i = 0; i < Zones.size(); ++i)
		{
			if (ZoneTouches(Zones[i], Unit, Unit.Pos))
			{
				ZoneTouch(Zones[i], Unit, true, Report);
				if (!Unit.IsAlive())
				{
					return false;
				}
			}
		}
		// A shock or a freeze as the turn begins takes that turn, as a status that
		// takes its orders away does -- and has counted down for it, so it isn't
		// the next turn's too.
		bool bLost = false;
		const std::string Now = Helpless(Unit);
		if (Before.empty() && !Now.empty())
		{
			bLost = true;
			for (FStatus& Status : Unit.Statuses)
			{
				Status.Turns -= Status.Id == Now ? 1 : 0;
			}
			Unit.Statuses.erase(std::remove_if(Unit.Statuses.begin(), Unit.Statuses.end(),
				[&Now](const FStatus& Status) { return Status.Id == Now && Status.Turns <= 0; }), Unit.Statuses.end());
		}
		if (!bWasStunned && Unit.HasStatus("stun"))
		{
			bLost = true;
			RemoveStatus(Unit, "stun");
		}
		return bLost;
	}

	void FBattle::ZonesAfterWalk(FUnit& Unit, FTickReport& Report)
	{
		// Rift Gate (2026-10-05): a walk ending at one mouth comes out of the other.
		for (const FZone& Zone : Zones)
		{
			const FAbility* Gate = ZoneAbility(Zone);
			if (!Gate || !Gate->bZonePortal || Zone.Team != Unit.Team || !ZoneLive(Zone))
			{
				continue;
			}
			const bool bAtFrom = Unit.Pos.DistanceTo(Zone.From) <= 1.0f;
			const bool bAtTarget = Unit.Pos.DistanceTo(Zone.Target) <= 1.0f;
			if (bAtFrom == bAtTarget)
			{
				continue;
			}
			const FVec2 Exit = bAtFrom ? Zone.Target : Zone.From;
			FVec2 Spot;
			if (SpotNear(Exit, Exit, 1.6f, 0.0f, Unit.Id, Spot))
			{
				SpellMoved(Unit, Spot, "gate", Report);
				break;
			}
		}
		for (size_t i = 0; i < Zones.size() && Unit.IsAlive(); ++i)
		{
			if (ZoneTouches(Zones[i], Unit, Unit.Pos))
			{
				ZoneTouch(Zones[i], Unit, false, Report);
			}
		}
	}

	void FBattle::ZoneTouch(FZone& Zone, FUnit& Unit, bool bTurnStart, FTickReport& Report)
	{
		const FAbility* Ability = ZoneAbility(Zone);
		FUnit* Owner = FindUnit(Zone.Owner);
		if (!Ability || !Owner || !Owner->IsAlive())
		{
			return;
		}
		// Once a turn: the turn beginning counts as the one it begins (Serial goes
		// up after the ground has had its say).
		const int Stamp = Unit.Serial + (bTurnStart ? 1 : 0);
		bool bKnown = false;
		for (std::pair<int, int>& Touch : Zone.Touched)
		{
			if (Touch.first == Unit.Id)
			{
				if (Touch.second == Stamp)
				{
					return;
				}
				Touch.second = Stamp;
				bKnown = true;
			}
		}
		if (!bKnown)
		{
			Zone.Touched.push_back(std::make_pair(Unit.Id, Stamp));
		}

		// The harm, as burning ground does it: a share of its health, no roll.
		const float Percent = Zone.bIgnited ? IgnitedPercent : Ability->ZonePercent;
		if (Percent > 0.0f)
		{
			const int Taken = Hurt(Unit, std::max(1, RoundToInt(Unit.MaxHp() * Percent * 0.01)));
			if (Taken > 0)
			{
				FEvent Event;
				Event.Kind = EEventKind::Hit;
				Event.Unit = Unit.Id;
				Event.By = Owner->Id;
				Event.Amount = Taken;
				Event.Where = Unit.Pos;
				Event.Id = "zone";
				Report.Events.push_back(Event);
				Unit.bSpotted = true;  // hurt in the grass: found
				if (!Unit.IsAlive())
				{
					KnockOut(Unit, Report);
					CheckWinner();
					if (Winner != -1)
					{
						Report.Say(EEventKind::Won, Winner);
					}
					return;
				}
				if (Unit.bMonster && Owner->Id != Unit.Id)
				{
					BossHurtBy(Unit, *Owner, Taken);
					MonsterHurt(Unit, *Owner, 1.0, Report);
					if (!Unit.IsAlive())
					{
						return;
					}
				}
			}
		}

		// Its element meets what is on the unit. What the element gave, the status
		// needn't give again (ice's Chill, water's Wet).
		const FAbility& Elemental = Zone.bIgnited ? FireTouch() : *Ability;
		const std::string Own = Zone.bIgnited ? std::string("burn") : Ability->StatusId;
		const bool bHad = !Own.empty() && Unit.HasStatus(Own);
		const bool bWasFrozen = Unit.HasStatus("freeze");
		if (!ElementOf(Elemental).empty())
		{
			ElementReactions(*Owner, Elemental, Unit, Report);
			if (!Unit.IsAlive())
			{
				return;
			}
		}
		const bool bElementGave = !Own.empty() && ((!bHad && Unit.HasStatus(Own))
			|| (Own == "chilled" && !bWasFrozen && Unit.HasStatus("freeze")));

		const bool bFirst = !Ability->bZoneOnce
			|| std::find(Zone.Once.begin(), Zone.Once.end(), Unit.Id) == Zone.Once.end();
		if (!bFirst)
		{
			return;
		}
		if (Ability->bZoneOnce)
		{
			Zone.Once.push_back(Unit.Id);
		}
		auto Give = [&](const std::string& StatusId, int Turns)
		{
			const FStatusDef* Def = FindStatus(StatusId);
			if (!Def || !Unit.IsAlive())
			{
				return;
			}
			// Stopped in it part-way through a turn, what holds a unit back during
			// its turn has to last into the next one to mean anything.
			if (!bTurnStart && (Def->bNoMove || Def->bNoAbilities || Def->MoveFactor != 1.0f || Def->MissPercent > 0))
			{
				++Turns;
			}
			const int By = (Def->bTaunt || Def->bSourced) ? Owner->Id : -1;
			AddStatus(Unit, StatusId, Turns, 0, By);
			if (!Unit.HasStatus(StatusId))
			{
				return;  // turned away, or it met something and became something else
			}
			FEvent Event;
			Event.Kind = EEventKind::StatusApplied;
			Event.Unit = Unit.Id;
			Event.By = Owner->Id;
			Event.Where = Unit.Pos;
			Event.Id = StatusId;
			Report.Events.push_back(Event);
			if (Def->bInterrupt && !bTurnStart)
			{
				StunInterrupt(Unit, Report);
			}
		};
		if (Zone.bIgnited)
		{
			if (!bElementGave)
			{
				Give("burn", 1);
			}
			return;
		}
		if (Ability->HasStatus() && !bElementGave)
		{
			Give(Ability->StatusId, Ability->StatusTurns);
		}
		if (!Ability->ZoneStatus2.empty())
		{
			Give(Ability->ZoneStatus2, Ability->ZoneStatus2Turns);
		}
	}

	bool FBattle::ReachesZone(const FAbility& Ability, const FVec2& From, const FVec2& Target, const FZone& Zone) const
	{
		const FAbility* Laid = ZoneAbility(Zone);
		if (!Laid)
		{
			return false;
		}
		if (ShapeOf(Ability) == "global")
		{
			return true;
		}
		// The zone's ground, walked on a half-metre lattice over the box round it:
		// any of it inside the ability's shape will do.
		const std::string Shape = ShapeOf(*Laid);
		const bool bLine = Shape == "line" || Shape == "vector";
		const float Reach = (bLine ? std::max(Laid->Aoe, 0.6f) : Laid->Aoe) + Ground::HitRadius;
		const float MinX = (bLine ? std::min(Zone.From.X, Zone.Target.X) : Zone.Target.X) - Reach;
		const float MaxX = (bLine ? std::max(Zone.From.X, Zone.Target.X) : Zone.Target.X) + Reach;
		const float MinY = (bLine ? std::min(Zone.From.Y, Zone.Target.Y) : Zone.Target.Y) - Reach;
		const float MaxY = (bLine ? std::max(Zone.From.Y, Zone.Target.Y) : Zone.Target.Y) + Reach;
		const int StepsX = static_cast<int>((MaxX - MinX) / 0.5f);
		const int StepsY = static_cast<int>((MaxY - MinY) / 0.5f);
		for (int Y = 0; Y <= StepsY; ++Y)
		{
			for (int X = 0; X <= StepsX; ++X)
			{
				const FVec2 Point(MinX + X * 0.5f, MinY + Y * 0.5f);
				if (InShape(*Laid, Zone.From, Zone.Target, Point) && InShape(Ability, From, Target, Point))
				{
					return true;
				}
			}
		}
		return false;
	}

	void FBattle::ZonesMeetElement(const FUnit& User, const FAbility& Ability, const FVec2& Target, FTickReport& Report)
	{
		if (Zones.empty() || Ability.Effect == EEffect::Heal || Ability.Effect == EEffect::Revive)
		{
			return;
		}
		const std::string& Element = ElementOf(Ability);
		if (Element != "fire" && Element != "water")
		{
			return;
		}
		std::vector<FZone> Kept;
		Kept.reserve(Zones.size());
		for (FZone& Zone : Zones)
		{
			const FAbility* Laid = ZoneAbility(Zone);
			if (!Laid)
			{
				Kept.push_back(Zone);
				continue;
			}
			const bool bBurning = Zone.bIgnited || ElementOf(*Laid) == "fire";
			// Fire catches tar: it burns now, and for two of its caster's turns at least.
			if (Element == "fire" && Laid->bZoneFlammable && !Zone.bIgnited && ReachesZone(Ability, User.Pos, Target, Zone))
			{
				Zone.bIgnited = true;
				Zone.Turns = std::max(Zone.Turns, 2);
				Zone.Touched.clear();
				FEvent Caught;
				Caught.Kind = EEventKind::ZoneIgnited;
				Caught.Unit = Zone.Owner;
				Caught.By = User.Id;
				Caught.Slot = Zone.Slot;
				Caught.Where = Zone.Target;
				Caught.Id = Zone.AbilityId;
				Report.Events.push_back(Caught);
			}
			// Water puts burning ground out.
			else if (Element == "water" && bBurning && ReachesZone(Ability, User.Pos, Target, Zone))
			{
				FEvent Ended;
				Ended.Kind = EEventKind::ZoneEnded;
				Ended.Unit = Zone.Owner;
				Ended.By = User.Id;
				Ended.Slot = Zone.Slot;
				Ended.Where = Zone.Target;
				Ended.Id = Zone.AbilityId;
				Report.Events.push_back(Ended);
				continue;
			}
			Kept.push_back(Zone);
		}
		Zones.swap(Kept);
	}
}
