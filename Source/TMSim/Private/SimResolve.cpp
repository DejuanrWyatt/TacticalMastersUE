// What an ability actually does when it goes off.
//
// Ported from game_state.gd: _resolve_ability (:1730-1867), _hurt (:1339),
// _take_from_shield (:1354), _add_status (:1414), _knock_out (:1439),
// _stun_interrupt (:1644) and _check_winner.
//
// This is the only place in the rules the dice are rolled, and the order they
// come out in is part of the rules rather than an implementation detail. Targets
// are taken in unit id order, and for each one: a roll to evade, then -- only if
// it did not -- a roll to crit, and only for damage. Healing, reviving and
// support roll nothing at all. Two machines playing the same match step the same
// orders and must draw the same numbers in the same order, so rearranging any of
// this desyncs a game several seconds after the mistake, somewhere else.
//
// The per-hit order matters for a second reason: a target that got out of the way
// takes no status, no gauge change and no buff, and neither does one the blow
// killed. Moving the status line above the death check would have a corpse
// quietly burning.

#include "SimAbility.h"
#include "SimBattle.h"

#include <algorithm>
#include <cmath>

namespace TMSim
{
	int FBattle::TakeFromShield(FUnit& Target, int Amount, FTickReport& Report)
	{
		// Shield and Barrier both soak, and they stack: each takes what it can
		// until either the damage or the shield runs out.
		std::vector<FStatus> Kept;
		Kept.reserve(Target.Statuses.size());
		for (FStatus& Status : Target.Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Amount > 0 && Def && Def->bAbsorbs)
			{
				const int Soaked = std::min(Status.Amount, Amount);
				Amount -= Soaked;
				Status.Amount -= Soaked;
				if (Soaked > 0)
				{
					FEvent Event;
					Event.Kind = EEventKind::Absorbed;
					Event.Unit = Target.Id;
					Event.Amount = Soaked;
					Event.Where = Target.Pos;
					Event.Id = Status.Id;
					Report.Events.push_back(Event);
				}
				if (Status.Amount <= 0)
				{
					continue;  // used up, and gone
				}
			}
			Kept.push_back(Status);
		}
		Target.Statuses.swap(Kept);
		return Amount;
	}

	int FBattle::Hurt(FUnit& Target, int Amount)
	{
		// The one door health is lost through, so this is where Invulnerable can
		// turn everything away and where anything asleep is shaken awake.
		if (Amount <= 0 || Target.IsInvulnerable())
		{
			return 0;
		}
		Target.Hp = std::max(0, Target.Hp - Amount);
		Target.UnharmedTurns = 0;

		std::vector<FStatus> Kept;
		Kept.reserve(Target.Statuses.size());
		for (const FStatus& Status : Target.Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (!Def || !Def->bWakesOnDamage)
			{
				Kept.push_back(Status);
			}
		}
		Target.Statuses.swap(Kept);
		return Amount;
	}

	void FBattle::AddStatus(FUnit& Target, const std::string& StatusId, int Turns, int Amount, int By)
	{
		const FStatusDef* Incoming = FindStatus(StatusId);
		if (!Incoming)
		{
			return;
		}
		// Immunity turns away anything unpleasant, and clears what is already
		// there when it arrives.
		if (Incoming->bHarmful && Target.IsImmune())
		{
			return;
		}
		if (Incoming->bCleanse)
		{
			std::vector<FStatus> Clean;
			for (const FStatus& Status : Target.Statuses)
			{
				const FStatusDef* Def = FindStatus(Status.Id);
				if (!Def || !Def->bHarmful)
				{
					Clean.push_back(Status);
				}
			}
			Target.Statuses.swap(Clean);
		}
		for (FStatus& Status : Target.Statuses)
		{
			if (Status.Id == StatusId)
			{
				// Refreshing takes the longer of the two rather than replacing,
				// so a short one cannot cut a long one short. A second Shield
				// adds to what is left of the first; a second Taunt takes over
				// who it has to go after.
				Status.Turns = std::max(Status.Turns, Turns);
				if (Amount > 0)
				{
					Status.Amount += Amount;
				}
				if (By >= 0)
				{
					Status.By = By;
				}
				return;
			}
		}
		FStatus Status;
		Status.Id = StatusId;
		Status.Turns = Turns;
		Status.Amount = Amount;
		Status.By = By;
		Target.Statuses.push_back(Status);
	}

	void FBattle::KnockOut(FUnit& Target, FTickReport& Report)
	{
		Target.Hp = 0;
		Target.KoTicks = RoundToInt(Tuning.KoSeconds * Pace::TicksPerSecond);
		Target.bReady = false;
		Target.Clock = 0;
		Target.bMoved = false;
		Target.bActed = false;
		Target.Statuses.clear();
		if (Target.IsCasting())
		{
			// Whatever it was part-way through is lost with it. Death is the only
			// thing that stops a cast: being stunned, silenced or frozen does not.
			FEvent Event;
			Event.Kind = EEventKind::CastFizzled;
			Event.Unit = Target.Id;
			Event.Slot = Target.Casting.Slot;
			Event.Where = Target.Pos;
			Report.Events.push_back(Event);
			Target.Casting = FCast();
		}
		Report.Say(EEventKind::Knocked, Target.Id);
		if (Target.KoTicks <= 0)
		{
			Report.Say(EEventKind::Gone, Target.Id);
		}
	}

	void FBattle::StunInterrupt(FUnit& Target, FTickReport& Report)
	{
		if (Target.bReady)
		{
			EndTurnFor(Target, false, Report);
		}
		// It loses the turn, but is left most of the way to the next one rather
		// than starting the fill over. That is what makes this a weaker Stun than
		// simply zeroing the gauge.
		const int Keep = Pace::TgMax * std::max(0, std::min(100, RoundToInt(Tuning.StunTgPercent))) / 100;
		Target.Tg = std::max(Target.Tg, Keep);
	}

	void FBattle::CheckWinner()
	{
		for (int Team = 0; Team < 2; ++Team)
		{
			if (TeamUnits(Team).empty())
			{
				Winner = TeamUnits(1 - Team).empty() ? Draw : 1 - Team;
				return;
			}
		}
	}

	int FBattle::CastTicks(const FAbility& Ability) const
	{
		return RoundToInt(Ability.Cast * Tuning.CastTimeMultiplier * Pace::TicksPerSecond);
	}

	void FBattle::UseAbility(FUnit& User, int Slot, const FVec2& Target, int Follow, FTickReport& Report)
	{
		const FAbility* Ability = JobAbility(User.Job, Slot);
		if (!Ability)
		{
			return;
		}

		// A toggle is only a switch. It costs nothing -- no cooldown, no meter, not
		// even the unit's action -- so it returns before any of that, and may be
		// flipped only once a turn so it cannot be waved on and off for free.
		if (Ability->Kind == "toggle")
		{
			User.Toggled[Slot] = !User.Toggled[Slot];
			User.ToggledTurn[Slot] = true;
			FEvent Event;
			Event.Kind = EEventKind::Resolved;
			Event.Unit = User.Id;
			Event.Slot = Slot;
			Event.Amount = User.Toggled[Slot] ? 1 : 0;
			Event.Where = User.Pos;
			Event.Id = Ability->Id;
			Report.Events.push_back(Event);
			return;
		}

		// The ultimate spends the meter whether it hits anything or not; everything
		// else fills it a little.
		if (Slot == 3)
		{
			User.Ult = 0;
		}
		else
		{
			User.Ult = std::min(Pace::UltMax, User.Ult + RoundToInt(Tuning.UltPerAction));
		}
		// One more than written down, because the count comes off at the start of
		// each of this unit's own turns -- including the next one.
		User.Cooldowns[Slot] = Ability->Cooldown > 0 ? Ability->Cooldown + 1 : 0;
		User.bActed = true;
		if (Target.DistanceTo(User.Pos) > 0.01f)
		{
			User.Facing = (Target - User.Pos).Normalized();
		}

		// Channelled: it goes off now and again on each of the next few turns, and
		// the unit is busy doing it, so this turn ends here.
		if (Ability->Kind == "channeled")
		{
			User.Channeling.Slot = Slot;
			User.Channeling.Target = Target;
			User.Channeling.Turns = std::max(1, Ability->Channel);
			ResolveAbility(User, Slot, Target, Report);
			if (User.IsAlive())
			{
				EndTurnFor(User, false, Report);
			}
			return;
		}

		const int Ticks = CastTicks(*Ability);
		if (Ticks <= 0)
		{
			ResolveAbility(User, Slot, Target, Report);
			return;
		}

		// A slow spell. Aimed at a unit it tracks them; aimed at the ground it
		// stays where it was put, which is how you catch someone walking in.
		User.Casting.Slot = Slot;
		User.Casting.Target = Target;
		User.Casting.FollowId = Follow;
		User.Casting.Ticks = Ticks;
		User.Casting.Total = Ticks;

		FEvent Event;
		Event.Kind = EEventKind::CastStarted;
		Event.Unit = User.Id;
		Event.Slot = Slot;
		Event.Amount = Ticks;
		Event.Where = Target;
		Event.Id = Ability->Id;
		Report.Events.push_back(Event);
	}

	void FBattle::ResolveAbility(FUnit& User, int Slot, const FVec2& Target, FTickReport& Report)
	{
		const FAbility* Ability = JobAbility(User.Job, Slot);
		if (!Ability)
		{
			return;
		}

		// Worked out before anything moves or changes, because a "vector" ability
		// carries the caster along its own line and the damage is owed from where
		// the swing started, not from where it finished.
		const std::vector<FHit> Hits = Preview(User, Slot, User.Pos, Target);

		if (ShapeOf(*Ability) == "vector" && Map.NodeWalkable(FMap::NodeOf(Target)))
		{
			const FUnit* Blocking = UnitNear(Target, Ground::UnitSpacing);
			if (Blocking == nullptr || Blocking->Id == User.Id)
			{
				User.Pos = FMap::Snap(Target);
				Report.Say(EEventKind::Moved, User.Id);
			}
		}

		FEvent Cast;
		Cast.Kind = EEventKind::Resolved;
		Cast.Unit = User.Id;
		Cast.Slot = Slot;
		Cast.Where = Target;
		Cast.Id = Ability->Id;
		Report.Events.push_back(Cast);

		for (const FHit& Hit : Hits)
		{
			FUnit* Struck = FindUnit(Hit.UnitId);
			if (!Struck)
			{
				continue;
			}
			int Amount = Hit.Amount;

			// The dice, and the only place in the rules they are thrown.
			bool bEvaded = false;
			bool bCritical = false;
			if (Ability->Effect == EEffect::Damage)
			{
				bEvaded = static_cast<int>(Rng.RandiRange(1, 100)) <= EvadeChance(*Struck, *Ability, &User);
				if (!bEvaded)
				{
					bCritical = static_cast<int>(Rng.RandiRange(1, 100)) <= CritChance(User);
					if (bCritical)
					{
						Amount = std::max(1, RoundToInt(Amount * Tuning.CritMultiplier));
					}
				}
			}

			if (bEvaded)
			{
				// Out of the way, and so out of everything that came with it: no
				// status, no gauge change, no buff.
				FEvent Event;
				Event.Kind = EEventKind::Evaded;
				Event.Unit = Struck->Id;
				Event.By = User.Id;
				Event.Where = Struck->Pos;
				Event.Id = Ability->Id;
				Report.Events.push_back(Event);
				continue;
			}

			if (bCritical)
			{
				FEvent Event;
				Event.Kind = EEventKind::Critical;
				Event.Unit = Struck->Id;
				Event.By = User.Id;
				Event.Where = Struck->Pos;
				Report.Events.push_back(Event);
			}

			switch (Ability->Effect)
			{
			case EEffect::Damage:
			{
				Amount = TakeFromShield(*Struck, Amount, Report);
				Amount = Hurt(*Struck, Amount);
				// Taking a beating earns a comeback, and it is what got through
				// that counts: a hit a shield ate entirely earns nothing.
				if (Struck->MaxHp() > 0)
				{
					Struck->Ult = std::min(Pace::UltMax, Struck->Ult
						+ RoundToInt(Amount * 100.0 / Struck->MaxHp() * Combat::UltFromDamage));
				}
				break;
			}
			case EEffect::Heal:
				Struck->Hp += Amount;
				break;
			case EEffect::Revive:
				Struck->Hp = Amount;
				Struck->KoTicks = 0;
				Struck->Tg = 0;
				Struck->bReady = false;
				Report.Say(EEventKind::Revived, Struck->Id);
				break;
			case EEffect::Support:
				break;  // all of its work is in the status, gauge and buffs below
			}

			FEvent Landed;
			Landed.Kind = EEventKind::Hit;
			Landed.Unit = Struck->Id;
			Landed.By = User.Id;
			Landed.Slot = Slot;
			Landed.Amount = Amount;
			Landed.Where = Struck->Pos;
			Landed.Id = Ability->Id;
			Report.Events.push_back(Landed);

			if (!Struck->IsAlive())
			{
				if (!Struck->IsKo())
				{
					KnockOut(*Struck, Report);
				}
				continue;  // nothing follows a killing blow
			}

			if (Ability->HasStatus())
			{
				const FStatusDef* Def = FindStatus(Ability->StatusId);
				// A Shield is worth the ability's own power, and a Taunt has to
				// remember who is owed the attention.
				const int Soak = (Def && Def->bAbsorbs) ? std::max(1, RoundToInt(Ability->Power)) : 0;
				const int By = (Def && Def->bTaunt) ? User.Id : -1;
				AddStatus(*Struck, Ability->StatusId, Ability->StatusTurns, Soak, By);

				FEvent Event;
				Event.Kind = EEventKind::StatusApplied;
				Event.Unit = Struck->Id;
				Event.By = User.Id;
				Event.Where = Struck->Pos;
				Event.Id = Ability->StatusId;
				Report.Events.push_back(Event);

				if (Def && Def->bInterrupt)
				{
					StunInterrupt(*Struck, Report);
				}
			}

			// A shove to the gauge only means anything to something still filling
			// it; a unit already waiting its turn cannot be hurried or delayed.
			if (Ability->TgChange != 0 && !Struck->bReady)
			{
				Struck->Tg = std::max(0, std::min(Pace::TgMax,
					Struck->Tg + Ability->TgChange * Pace::TgMax / 100));
				FEvent Event;
				Event.Kind = EEventKind::GaugeChanged;
				Event.Unit = Struck->Id;
				Event.Amount = Ability->TgChange;
				Event.Where = Struck->Pos;
				Report.Events.push_back(Event);
			}

			// Buffs stack rather than refresh: two castings of the same thing
			// are two buffs, each counting down on its own.
			for (const FBuff& Buff : Ability->Buffs)
			{
				Struck->Buffs.push_back(Buff);
			}
		}

		CheckWinner();
		if (Winner != -1)
		{
			Report.Say(EEventKind::Won, Winner);
		}
	}
}
