// The second set of statuses: what they do beyond a number in the status table
// (Docs/design/feat-status-effects.md).
//
// Marked, Protect, Shell, Oiled and Off-Balance change what a hit is worth
// (FBattle::CalcAmount, FlankBonus); the rest is here, called from where it
// happens. Nothing in this file rolls a die, and nothing here does anything to
// a unit without one of these statuses on it, so a Godot battle -- which never
// carries any of them -- plays exactly as before.

#include "SimAbility.h"
#include "SimBattle.h"

#include <algorithm>
#include <cmath>

namespace TMSim
{
	bool FBattle::RemoveStatus(FUnit& Unit, const std::string& StatusId)
	{
		const size_t Before = Unit.Statuses.size();
		Unit.Statuses.erase(std::remove_if(Unit.Statuses.begin(), Unit.Statuses.end(),
			[&StatusId](const FStatus& Status) { return Status.Id == StatusId; }), Unit.Statuses.end());
		return Unit.Statuses.size() != Before;
	}

	void FBattle::ReleaseCharm(FUnit& Unit, FTickReport* Report)
	{
		if (Unit.CharmedFrom < 0 || Unit.HasStatus("charmed"))
		{
			return;
		}
		Unit.Team = Unit.CharmedFrom;
		Unit.CharmedFrom = -1;
		if (Report)
		{
			FEvent Event;
			Event.Kind = EEventKind::CharmEnded;
			Event.Unit = Unit.Id;
			Event.Where = Unit.Pos;
			Report->Events.push_back(Event);
		}
	}

	void FBattle::Redirect(FUnit& User, const FAbility& Ability, FUnit*& Struck, int& Amount, FTickReport& Report)
	{
		if (!Struck || Struck->Id == User.Id || Struck->Statuses.empty() || ShapeOf(Ability) != "unit")
		{
			return;
		}

		// Reflect: a spell aimed at it by the other side goes back where it came from, once.
		if (Struck->HasStatus("reflect") && Ability.Scale == EScale::Mag && Struck->HomeTeam() != User.HomeTeam())
		{
			const FStatusDef* Def = Ability.HasStatus() ? FindStatus(Ability.StatusId) : nullptr;
			const bool bHarmful = Ability.Effect == EEffect::Damage || (Ability.Effect == EEffect::Support && Def && Def->bHarmful);
			if (bHarmful)
			{
				RemoveStatus(*Struck, "reflect");
				if (Ability.Effect == EEffect::Damage)
				{
					Amount = CalcAmount(User, Ability, Struck->Pos, User, User.Pos, LevelAt(Struck->Pos), LevelAt(User.Pos));
				}
				FEvent Event;
				Event.Kind = EEventKind::Redirected;
				Event.Unit = User.Id;
				Event.By = Struck->Id;
				Event.Where = User.Pos;
				Event.Id = "reflect";
				Report.Events.push_back(Event);
				Struck = &User;
				return;
			}
		}

		// Guarded: a blow for it lands on its guardian, standing close by.
		if (Ability.Effect != EEffect::Damage)
		{
			return;
		}
		for (const FStatus& Status : Struck->Statuses)
		{
			if (Status.Id != "guarded")
			{
				continue;
			}
			FUnit* Guardian = FindUnit(Status.By);
			if (!Guardian || !Guardian->IsAlive() || Guardian->Id == User.Id || Guardian->Id == Struck->Id
				|| Guardian->HomeTeam() != Struck->HomeTeam())
			{
				return;
			}
			if (Guardian->Pos.DistanceTo(Struck->Pos) > GuardReach)
			{
				RemoveStatus(*Struck, "guarded");  // too far to step in: the oath is broken
				return;
			}
			Amount = CalcAmount(User, Ability, User.Pos, *Guardian, Guardian->Pos, LevelAt(User.Pos), LevelAt(Guardian->Pos));
			FEvent Event;
			Event.Kind = EEventKind::Redirected;
			Event.Unit = Guardian->Id;
			Event.By = Struck->Id;
			Event.Where = Guardian->Pos;
			Event.Id = "guard";
			Report.Events.push_back(Event);
			Struck = Guardian;
			return;
		}
	}

	void FBattle::ElementReactions(FUnit& User, const FAbility& Ability, FUnit& Struck, FTickReport& Report)
	{
		const bool bRules = Tuning.Elements >= 0.5;
		if (!Struck.IsAlive() || (Struck.Statuses.empty() && !bRules) || Ability.Effect == EEffect::Heal || Ability.Effect == EEffect::Revive)
		{
			return;
		}
		const std::string& Element = ElementOf(Ability);
		if (Element.empty())
		{
			return;
		}
		auto Say = [&](FUnit& On, const char* What)
		{
			FEvent Event;
			Event.Kind = EEventKind::Reaction;
			Event.Unit = On.Id;
			Event.By = User.Id;
			Event.Where = On.Pos;
			Event.Id = What;
			Report.Events.push_back(Event);
		};

		if (Element == "fire")
		{
			if (Struck.HasStatus("oiled"))
			{
				// Burn on an Oiled unit lasts twice as long and uses the oil up (AddStatus).
				AddStatus(Struck, "burn", 2);
				Say(Struck, "ignite");
			}
			if (RemoveStatus(Struck, "chilled"))
			{
				Say(Struck, "thaw");
			}
			if (RemoveStatus(Struck, "wet"))
			{
				Say(Struck, "dry");
			}
		}
		else if (Element == "ice")
		{
			if (RemoveStatus(Struck, "wet"))
			{
				AddStatus(Struck, "freeze", 1);
				if (Struck.HasStatus("freeze"))
				{
					Say(Struck, "freeze");
				}
			}
			else if (bRules)
			{
				AddStatus(Struck, "chilled", 2, 1);
				FEvent Event;
				Event.Kind = EEventKind::StatusApplied;
				Event.Unit = Struck.Id;
				Event.By = User.Id;
				Event.Where = Struck.Pos;
				Event.Id = Struck.HasStatus("chilled") ? "chilled" : "freeze";
				Report.Events.push_back(Event);
			}
		}
		else if (Element == "lightning")
		{
			if (Struck.HasStatus("wet"))
			{
				// Through the water: every Wet unit near it is shocked too, whichever side.
				const FVec2 Centre = Struck.Pos;
				for (FUnit& Near : Units)
				{
					if (Near.IsAlive() && Near.HasStatus("wet") && Near.Pos.DistanceTo(Centre) <= ShockReach)
					{
						AddStatus(Near, "stun", 1);
						if (Near.HasStatus("stun"))
						{
							Say(Near, "shock");
							StunInterrupt(Near, Report);
						}
					}
				}
			}
		}
		else if (Element == "water")
		{
			if (bRules)
			{
				const bool bBurning = Struck.HasStatus("burn");
				AddStatus(Struck, "wet", 2);
				if (bBurning && !Struck.HasStatus("burn"))
				{
					Say(Struck, "douse");
				}
				FEvent Event;
				Event.Kind = EEventKind::StatusApplied;
				Event.Unit = Struck.Id;
				Event.By = User.Id;
				Event.Where = Struck.Pos;
				Event.Id = "wet";
				Report.Events.push_back(Event);
			}
		}
	}

	void FBattle::SuppressedMoved(FUnit& Unit, FTickReport& Report)
	{
		int By = -1;
		for (const FStatus& Status : Unit.Statuses)
		{
			By = Status.Id == "suppressed" ? Status.By : By;
		}
		RemoveStatus(Unit, "suppressed");
		FUnit* Suppressor = FindUnit(By);
		if (!Suppressor || !Suppressor->IsAlive() || Suppressor->bOffBoard || Suppressor->HomeTeam() == Unit.HomeTeam() || !Unit.IsAlive())
		{
			return;
		}
		const FAbility* Blow = Suppressor->Ability(0);
		if (!Blow || Blow->Effect != EEffect::Damage)
		{
			return;
		}
		// Its plain first ability, no roll to dodge it: it was waiting for exactly this.
		int Amount = CalcAmount(*Suppressor, *Blow, Suppressor->Pos, Unit, Unit.Pos, LevelAt(Suppressor->Pos), LevelAt(Unit.Pos));
		Amount = TakeFromShield(Unit, Amount, Report);
		Amount = Hurt(Unit, Amount);
		FEvent Event;
		Event.Kind = EEventKind::Hit;
		Event.Unit = Unit.Id;
		Event.By = Suppressor->Id;
		Event.Amount = Amount;
		Event.Where = Unit.Pos;
		Event.Id = "suppressed";
		Report.Events.push_back(Event);
		if (!Unit.IsAlive())
		{
			KnockOut(Unit, Report);
			CheckWinner();
			if (Winner != -1)
			{
				Report.Say(EEventKind::Won, Winner);
			}
		}
	}

	void FBattle::Flee(FUnit& Unit, int From, FTickReport& Report)
	{
		const FUnit* Feared = FindUnit(From);
		if (!Feared || Unit.IsRooted() || Unit.bMoved)
		{
			return;
		}
		// As far as it can get from what it fears; the first of equals, so every
		// machine picks the same spot.
		const std::vector<std::pair<FNode, double>> Reach = ReachableNodes(Unit);
		FVec2 Best = Unit.Pos;
		double BestAway = Unit.Pos.DistanceTo(Feared->Pos);
		for (const auto& Pair : Reach)
		{
			const FVec2 Spot = FMap::NodePos(Pair.first);
			const double Away = Spot.DistanceTo(Feared->Pos);
			if (Away > BestAway + 1e-6)
			{
				BestAway = Away;
				Best = Spot;
			}
		}
		if (Best == Unit.Pos)
		{
			return;
		}
		const int Id = Unit.Id;
		ApplyMove(Id, Best, false, Report);
		if (const FUnit* After = FindUnit(Id))
		{
			FEvent Event;
			Event.Kind = EEventKind::Fled;
			Event.Unit = Id;
			Event.By = From;
			Event.Where = After->Pos;
			Report.Events.push_back(Event);
		}
	}

	void FBattle::TickStops(FUnit& Unit)
	{
		for (FStatus& Status : Unit.Statuses)
		{
			if (Status.Id == "stop")
			{
				--Status.Amount;
				Status.Turns = std::max(0, (Status.Amount + Pace::TicksPerSecond - 1) / Pace::TicksPerSecond);
			}
		}
		Unit.Statuses.erase(std::remove_if(Unit.Statuses.begin(), Unit.Statuses.end(),
			[](const FStatus& Status) { return Status.Id == "stop" && Status.Amount <= 0; }), Unit.Statuses.end());
	}
}
