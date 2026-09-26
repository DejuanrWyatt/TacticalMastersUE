#include "SimBattle.h"

#include <algorithm>
#include <cmath>

namespace TMSim
{
	void FBattle::Start(uint64_t InSeed)
	{
		Rng.Seed(InSeed);
		TickCount = 0;
		Winner = -1;
		for (FUnit& Unit : Units)
		{
			Unit.Hp = Unit.MaxHp();
			// A faster unit starts nearer its first turn, so the opening order is
			// the Speed order rather than a scramble. One short of full: becoming
			// ready is the clock's job, not the setup's.
			Unit.Tg = std::min(Pace::TgMax - 1, Unit.Stat(EStat::Speed) * Pace::StartTgPerSpeed);
			Unit.bReady = false;
			Unit.Clock = 0;
			Unit.Serial = 0;
			Unit.bMoved = false;
			Unit.bActed = false;
			Unit.bHustling = false;
			Unit.UnharmedTurns = 0;
		}
	}

	int FBattle::BaseTgGain(const FUnit& Unit) const
	{
		return std::max(1, RoundToInt(Unit.Stat(EStat::Speed) * Pace::TgPerSpeed * Tuning.SpeedMultiplier));
	}

	double FBattle::HustleFactor(const FUnit& Unit) const
	{
		return Unit.IsHustling() ? 1.0 + Tuning.HustleBonus * 0.01 : 1.0;
	}

	int FBattle::TgGain(const FUnit& Unit) const
	{
		return RoundToInt(BaseTgGain(Unit) * Unit.TgFactor() * HustleFactor(Unit));
	}

	int FBattle::ClockTicks(const FUnit& Unit) const
	{
		return RoundToInt((Tuning.ClockBase + Tuning.PatienceMultiplier * Unit.Stat(EStat::Patience))
			* Pace::TicksPerSecond);
	}

	int FBattle::TicksToReady(const FUnit& Unit) const
	{
		if (Unit.bReady)
		{
			return 0;
		}
		// A spell in flight holds the gauge: the wait is the cast, then the fill.
		// Casting is not ported yet, so that part is zero for now.
		const int CastTicks = 0;
		int Gain = TgGain(Unit);
		if (Gain <= 0)
		{
			Gain = BaseTgGain(Unit);  // held by a status: guess as if it were not
		}
		const int Remaining = Pace::TgMax - Unit.Tg;
		return CastTicks + std::max(0, static_cast<int>(std::ceil(static_cast<double>(Remaining) / Gain)));
	}

	FUnit* FBattle::FindUnit(int Id)
	{
		for (FUnit& Unit : Units)
		{
			if (Unit.Id == Id)
			{
				return &Unit;
			}
		}
		return nullptr;
	}

	void FBattle::Tick(FTickReport& Report)
	{
		if (Winner != -1)
		{
			return;
		}

		// While the sides are still placing their units, nothing else happens.
		if (PlanningTicks > 0)
		{
			--PlanningTicks;
			// The original starts the fighting here once this reaches zero.
			return;
		}

		++TickCount;

		// The original also ends the battle on its time limit here, and runs the
		// hold-the-middle rule. Both belong with victory conditions.

		for (FUnit& Unit : Units)
		{
			if (Unit.IsKo())
			{
				--Unit.KoTicks;
				if (Unit.KoTicks <= 0)
				{
					Report.Gone.push_back(Unit.Id);
				}
				continue;
			}
			if (!Unit.IsAlive())
			{
				continue;
			}

			// A unit part-way through casting does not fill its gauge; the spell
			// going off is what its turn was spent on. Casting is not ported yet,
			// so this is always false and the shape is kept for when it is.
			const bool bWasCasting = false;

			if (Unit.bReady)
			{
				--Unit.Clock;
				if (Unit.Clock <= 0)
				{
					EndTurn(Unit, true, Report);
				}
			}
			else if (!bWasCasting)
			{
				Unit.Tg = std::min(Pace::TgMax, Unit.Tg + TgGain(Unit));
				if (Unit.Tg >= Pace::TgMax)
				{
					BecomeReady(Unit, Report);
				}
			}
		}
	}

	void FBattle::Advance(int Ticks, FTickReport& Report)
	{
		for (int i = 0; i < Ticks; ++i)
		{
			Tick(Report);
		}
	}

	double FBattle::FlankBonus(const FUnit& Target, FVec2 TargetPos, FVec2 From) const
	{
		const FVec2 ToAttacker = From - TargetPos;
		if (ToAttacker.Length() < 0.01f)
		{
			return 1.0;
		}
		// Which way the target is looking, against where the blow comes from.
		const float Facing = Target.Facing.Dot(ToAttacker.Normalized());
		if (Facing < -0.5f)
		{
			return Tuning.BackBonus;
		}
		if (Facing < 0.5f)
		{
			return Tuning.SideBonus;
		}
		return 1.0;
	}

	int FBattle::CalcAmount(const FUnit& User, const FAbility& Ability, FVec2 From,
		const FUnit& Target, FVec2 TargetPos, int FromLevel, int TargetLevel) const
	{
		(void)User;  // nothing of the user's is added: the ability is the whole of it

		// What an ability does is its own power, and nothing else.
		const double Power = Ability.Power;

		switch (Ability.Effect)
		{
		case EEffect::Damage:
		{
			const int Defence = Target.Stat(Ability.Scale == EScale::Att ? EStat::AttDef : EStat::MagDef);

			int Levels = FromLevel - TargetLevel;
			Levels = std::max(-Combat::MaxHeightLevels, std::min(Combat::MaxHeightLevels, Levels));
			const double Height = 1.0 + Tuning.HeightBonus * Levels;
			const double Flank = FlankBonus(Target, TargetPos, From);

			const int Raw = RoundToInt(Power * Combat::DamageScale * Height * Flank);
			return std::max(Combat::MinimumDamage, RoundToInt((Raw - Defence) * Tuning.DamageMultiplier));
		}
		case EEffect::Heal:
		{
			const int Full = RoundToInt(Power * Combat::HealScale * Tuning.HealMultiplier);
			// Never more than it is short of: overhealing is not a thing here.
			const int Missing = Target.MaxHp() - Target.Hp;
			return std::min(Full, Missing);
		}
		case EEffect::Revive:
			// A revive's power is the share of max HP it comes back with.
			return std::max(1, RoundToInt(Target.MaxHp() * Power));

		case EEffect::Support:
		default:
			return 0;
		}
	}

	int FBattle::EvadeChance(const FUnit& Target, const FAbility& Ability, const FUnit* Attacker) const
	{
		// Only something harmful can be got out of the way of.
		if (Ability.Effect != EEffect::Damage)
		{
			return 0;
		}
		const int Base = Target.Stat(Ability.Scale == EScale::Att ? EStat::AEva : EStat::MEva);
		// A blinded attacker is that much easier to step around.
		const int Blind = Attacker ? Attacker->MissChance() : 0;
		const int Chance = RoundToInt(Base * Tuning.EvadeMultiplier) + Blind;
		return std::max(0, std::min(95, Chance));
	}

	int FBattle::CritChance(const FUnit& User) const
	{
		const int Chance = RoundToInt(User.Stat(EStat::Crit) * Tuning.CritChanceMultiplier);
		return std::max(0, std::min(100, Chance));
	}

	void FBattle::BecomeReady(FUnit& Unit, FTickReport& Report)
	{
		// Before the turn starts, the original lets this unit's statuses act and
		// count down, burns or heals it on the ground it stands on, mends it if it
		// has been left alone, refreshes auras, and carries on a channelled spell.
		// A status that takes its orders away costs it the turn here. None of that
		// is ported yet; what follows is the turn itself beginning.

		Unit.bReady = true;
		Unit.Tg = Pace::TgMax;
		++Unit.Serial;
		Unit.Clock = ClockTicks(Unit);
		Unit.bMoved = false;
		Unit.bActed = false;
		Unit.bHustling = false;
		++Unit.UnharmedTurns;

		for (int Slot = 0; Slot < 4; ++Slot)
		{
			Unit.Cooldowns[Slot] = std::max(0, Unit.Cooldowns[Slot] - 1);
		}

		// Buffs last a number of the unit's own turns.
		std::vector<FBuff> Kept;
		Kept.reserve(Unit.Buffs.size());
		for (FBuff& Buff : Unit.Buffs)
		{
			if (--Buff.Turns > 0)
			{
				Kept.push_back(Buff);
			}
		}
		Unit.Buffs.swap(Kept);

		Report.BecameReady.push_back(Unit.Id);
	}

	void FBattle::EndTurn(FUnit& Unit, bool bTimedOut, FTickReport& Report)
	{
		// What it kept of its gauge says what it did with the turn. Doing nothing
		// leaves it nearest its next one, which is what makes waiting a choice
		// rather than a punishment.
		if (bTimedOut)
		{
			Unit.Tg = 0;
			Report.TimedOut.push_back(Unit.Id);
		}
		else if (Unit.bMoved && Unit.bActed)
		{
			Unit.Tg = 0;
		}
		else if (Unit.bMoved || Unit.bActed)
		{
			Unit.Tg = Pace::TgKeepOne;
		}
		else
		{
			Unit.Tg = Pace::TgKeepNone;
		}

		// Held its ability back: the gauge fills faster until its next turn.
		Unit.bHustling = !Unit.bActed && !bTimedOut;
		Unit.bReady = false;
		Unit.Clock = 0;
		Unit.bMoved = false;
		Unit.bActed = false;
		Report.TurnEnded.push_back(Unit.Id);

		// Relentless: straight back round again, and it is spent doing so. Ported
		// with the rest of EndTurn for fidelity, but nothing exercises it until
		// abilities can apply a status.
		bool bGoesAgain = false;
		std::vector<FStatus> Kept;
		Kept.reserve(Unit.Statuses.size());
		for (const FStatus& Status : Unit.Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bExtraTurn)
			{
				bGoesAgain = true;
			}
			else
			{
				Kept.push_back(Status);
			}
		}
		if (bGoesAgain)
		{
			Unit.Statuses.swap(Kept);
			Unit.Tg = Pace::TgMax - 1;
		}
	}
}
