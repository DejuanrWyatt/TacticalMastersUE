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
		Target.LastHurt = Amount;
		// Phoenix Feather: once a battle, a blow that would knock it out leaves it standing.
		if (Target.Hp == 0 && Target.HasItems() && !Target.bPhoenixUsed && GearHas(Target, &FItemDef::bPhoenix))
		{
			Target.bPhoenixUsed = true;
			Target.Hp = 1;
		}

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
		// Charmed: any damage brings it to its senses. Suppressed: hurting the
		// suppressor frees whoever it had pinned (Docs/design/feat-status-effects.md).
		if (Target.CharmedFrom >= 0)
		{
			RemoveStatus(Target, "charmed");
			ReleaseCharm(Target, nullptr);
		}
		for (FUnit& Other : Units)
		{
			if (!Other.Statuses.empty())
			{
				Other.Statuses.erase(std::remove_if(Other.Statuses.begin(), Other.Statuses.end(),
					[&Target](const FStatus& Status) { return Status.Id == "suppressed" && Status.By == Target.Id; }), Other.Statuses.end());
			}
		}
		return Amount;
	}

	void FBattle::AddStatus(FUnit& Target, const std::string& StatusId, int Turns, int Amount, int By)
	{
		const FStatusDef* Incoming = FindStatus(StatusId);
		if (!Incoming)
		{
			return;
		}
		// A boss shrugs off what would stop it, and wears slows and burns half as
		// long; Colossus Heart keeps its holder on its feet. Neither is Godot's.
		if (Target.bMonster)
		{
			const FMonsterInfo* Info = Target.MonsterInfo();
			if (Info && Info->Has(MonsterTrait::Unstoppable))
			{
				if ((Incoming->bNoOrders && StatusId != "staggered") || Incoming->bInterrupt || Incoming->bOneAction || StatusId == "freeze"
					|| StatusId == "stop" || StatusId == "charmed" || StatusId == "terrified")
				{
					return;
				}
				if (Incoming->bHarmful)
				{
					Turns = std::max(1, (Turns + 1) / 2);
				}
			}
		}
		if (Incoming->bOneAction && Target.HasItems() && GearHas(Target, &FItemDef::bSteady))
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
			ReleaseCharm(Target, nullptr);
		}
		// The second set (Docs/design/feat-status-effects.md): how they meet
		// what is already there. None of these statuses is ever on a unit in a
		// Godot battle, so none of this changes one.
		if (StatusId == "charmed")
		{
			// A side's own units only (monsters are tamed instead), and to the charmer's side.
			const FUnit* Charmer = FindUnit(By);
			if (Target.bMonster || !Charmer || Charmer->Team == Target.Team || (Target.Team != 0 && Target.Team != 1)
				|| (Charmer->Team != 0 && Charmer->Team != 1))
			{
				return;
			}
		}
		if ((StatusId == "haste" && RemoveStatus(Target, "slow")) || (StatusId == "slow" && RemoveStatus(Target, "haste")))
		{
			return;  // the two cancel out
		}
		if (StatusId == "burn")
		{
			if (RemoveStatus(Target, "wet"))
			{
				Turns = std::max(1, Turns / 2);
			}
			if (RemoveStatus(Target, "oiled"))
			{
				Turns *= 2;
			}
		}
		if (StatusId == "wet")
		{
			RemoveStatus(Target, "burn");
		}
		if (StatusId == "stop")
		{
			// Counted in ticks: two seconds a turn, since it has no turns while it lasts.
			Amount = Turns * 2 * Pace::TicksPerSecond;
			Turns = (Amount + Pace::TicksPerSecond - 1) / Pace::TicksPerSecond;
			for (FStatus& Status : Target.Statuses)
			{
				if (Status.Id == "stop")
				{
					Status.Amount = std::max(Status.Amount, Amount);
					Status.Turns = std::max(Status.Turns, Turns);
					return;
				}
			}
		}
		if (StatusId == "chilled")
		{
			Amount = std::max(1, Amount);
			for (FStatus& Status : Target.Statuses)
			{
				if (Status.Id == "chilled" && Status.Amount + Amount >= 3)
				{
					// Three layers of cold: it freezes solid.
					RemoveStatus(Target, "chilled");
					AddStatus(Target, "freeze", 1);
					return;
				}
			}
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
		if (StatusId == "charmed")
		{
			// Over to the charmer's side. Caught mid-turn, this turn is its charmed one.
			Target.CharmedFrom = Target.Team;
			Target.Team = FindUnit(By)->Team;
			Status.Amount = Target.bReady ? 1 : 0;
		}
		Target.Statuses.push_back(Status);
	}

	void FBattle::KnockOut(FUnit& Target, FTickReport& Report)
	{
		Target.Hp = 0;
		// A monster that falls is gone at once: nothing raises it.
		Target.KoTicks = Target.bMonster ? 0 : RoundToInt(Tuning.KoSeconds * Pace::TicksPerSecond);
		Target.bReady = false;
		Target.Clock = 0;
		Target.bMoved = false;
		Target.bActed = false;
		// Reraise: it stands again a few seconds later, so it lies there until then.
		const bool bReraise = !Target.bMonster && Target.HasStatus("reraise");
		Target.Statuses.clear();
		if (Target.CharmedFrom >= 0)
		{
			ReleaseCharm(Target, &Report);
		}
		if (bReraise)
		{
			Target.ReraiseTicks = FBattle::ReraiseSeconds * Pace::TicksPerSecond;
			Target.KoTicks = std::max(Target.KoTicks, Target.ReraiseTicks + 1);
		}
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
			OnGone(Target, Report);
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
		// Only a side's own units count: a monster it has tamed doesn't keep it
		// in the battle.
		auto Standing = [this](int Team)
		{
			for (const FUnit& Unit : Units)
			{
				if (Unit.IsAlive() && Unit.HomeTeam() == Team && !Unit.bMonster)
				{
					return true;
				}
			}
			return false;
		};
		for (int Team = 0; Team < 2; ++Team)
		{
			if (!Standing(Team))
			{
				Winner = !Standing(1 - Team) ? Draw : 1 - Team;
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
		const FAbility* Ability = User.Ability(Slot);
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
		if (Target.DistanceTo(User.Pos) > 0.01f && !User.HasStatus("offbalance"))
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
		const FAbility* Ability = User.Ability(Slot);
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

		// Vanished: striking out shows where it is.
		if (Ability->Effect == EEffect::Damage && User.HasStatus("veil"))
		{
			User.Statuses.erase(std::remove_if(User.Statuses.begin(), User.Statuses.end(),
				[](const FStatus& Status) { return Status.Id == "veil"; }), User.Statuses.end());
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
			// Reflect and Guarded send it elsewhere, before the dice (SimStatuses.cpp).
			Redirect(User, *Ability, Struck, Amount, Report);

			// The dice, and the only place in the rules they are thrown.
			bool bEvaded = false;
			bool bCritical = false;
			bool bGrazed = false;
			if (Ability->Effect == EEffect::Damage)
			{
				bEvaded = static_cast<int>(Rng.RandiRange(1, 100)) <= EvadeChance(*Struck, *Ability, &User);
				// Defense model 1 (2026-10-01): of the hits evaded, one in ten is
				// dodged outright and the rest are grazed, landing for half and
				// never critical. Its effects still come with it.
				if (bEvaded && NewDefense() && static_cast<int>(Rng.RandiRange(1, Combat::DodgeOneIn)) != 1)
				{
					bEvaded = false;
					bGrazed = true;
					Amount = std::max(Combat::MinimumDamage, RoundToInt(Amount * Combat::GrazeDamage));
					FEvent Graze;
					Graze.Kind = EEventKind::Grazed;
					Graze.Unit = Struck->Id;
					Graze.By = User.Id;
					Graze.Where = Struck->Pos;
					Graze.Id = Ability->Id;
					Report.Events.push_back(Graze);
				}
				if (bGrazed)
				{
					// A graze is never critical, and doesn't spend a First Strike.
				}
				// First Strike Gauntlet: its first hit that lands is critical, without a roll.
				else if (!bEvaded && User.HasItems() && !User.bFirstStrikeUsed && GearHas(User, &FItemDef::bFirstStrike))
				{
					User.bFirstStrikeUsed = true;
					bCritical = true;
					Amount = std::max(1, RoundToInt(Amount * Tuning.CritMultiplier));
				}
				else if (!bEvaded)
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
				if (Amount > 0 && !Struck->Statuses.empty())
				{
					// Marked and Off-Balance pay out on the hit that lands, and are gone.
					RemoveStatus(*Struck, "marked");
					RemoveStatus(*Struck, "offbalance");
				}
				if (Amount > 0 && Struck->HasStatus("veil"))
				{
					Struck->Statuses.erase(std::remove_if(Struck->Statuses.begin(), Struck->Statuses.end(),
						[](const FStatus& Status) { return Status.Id == "veil"; }), Struck->Statuses.end());
				}
				// Items that answer a hit, and a monster's temper (neither is Godot's).
				if (User.HasItems() && Amount > 0 && Struck->Id != User.Id)
				{
					const int Steal = GearSum(User, &FItemDef::LifestealPercent);
					if (Steal > 0 && User.IsAlive())
					{
						User.Hp = std::min(User.MaxHp(), User.Hp + std::max(1, RoundToInt(Amount * Steal / 100.0)));
					}
				}
				if (Struck->HasItems() && Struck->Id != User.Id && Ability->MaxRange <= Ground::MeleeRange && User.IsAlive())
				{
					const int Thorns = GearSum(*Struck, &FItemDef::Thorns);
					if (Thorns > 0 && Hurt(User, Thorns) > 0)
					{
						FEvent Back;
						Back.Kind = EEventKind::Hit;
						Back.Unit = User.Id;
						Back.By = Struck->Id;
						Back.Amount = Thorns;
						Back.Where = User.Pos;
						Back.Id = "thorns";
						Report.Events.push_back(Back);
						if (!User.IsAlive())
						{
							KnockOut(User, Report);
						}
					}
				}
				if (Struck->bMonster)
				{
					MonsterHurt(*Struck, User, Hit.Flank, Report);
				}
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
				if (Struck->HasStatus("decay"))
				{
					// Decay: healing rots it instead.
					const int Taken = Hurt(*Struck, Amount);
					FEvent Rot;
					Rot.Kind = EEventKind::Hit;
					Rot.Unit = Struck->Id;
					Rot.By = User.Id;
					Rot.Amount = Taken;
					Rot.Where = Struck->Pos;
					Rot.Id = "decay";
					Report.Events.push_back(Rot);
					Amount = 0;
					break;
				}
				Struck->Hp += Amount;
				break;
			case EEffect::Revive:
				Struck->Hp = Amount;
				Struck->KoTicks = 0;
				Struck->ReraiseTicks = 0;
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
				if (!Struck->IsKo() && !Struck->bOffBoard)
				{
					KnockOut(*Struck, Report);
					if (User.HasItems())
					{
						User.KillTgPercent += GearSum(User, &FItemDef::KillTgPercent);
					}
				}
				continue;  // nothing follows a killing blow
			}

			if (!Ability->Special.empty())
			{
				ApplySpecial(User, *Ability, Target, Struck, Report);
			}

			ElementReactions(User, *Ability, *Struck, Report);
			if (!Struck->IsAlive())
			{
				continue;
			}

			if (Ability->HasStatus())
			{
				const FStatusDef* Def = FindStatus(Ability->StatusId);
				// A Shield is worth the ability's own power, and a Taunt has to
				// remember who is owed the attention.
				const int Soak = (Def && Def->bAbsorbs) ? std::max(1, RoundToInt(Ability->Power)) : 0;
				const int By = (Def && (Def->bTaunt || Def->bSourced)) ? User.Id : -1;
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

		if (Ability->Special == "blink" && User.IsAlive())
		{
			ApplySpecial(User, *Ability, Target, nullptr, Report);
		}

		CheckWinner();
		if (Winner != -1)
		{
			Report.Say(EEventKind::Won, Winner);
		}
	}
}
