#include "SimUnit.h"

#include "SimAbility.h"

namespace TMSim
{
	int FUnit::Stat(EStat Which) const
	{
		if (!Stats)
		{
			return 0;
		}

		int Value = Stats->Get(Which);
		for (const FBuff& Buff : Buffs)
		{
			if (Buff.Stat == Which)
			{
				Value += Buff.Amount;
			}
		}

		// Always-on abilities add to a stat too: a passive, an active + passive,
		// and a toggle while it is switched on -- but only their buffs meant for
		// the owner's side, since one aimed at the enemy is what it does to them
		// (unit.gd:70-87). Auras are not counted here: they reach units, the
		// owner included, as timed buffs at the start of each turn (ApplyAuras).
		for (int Slot = 0; Slot < 4; ++Slot)
		{
			const FAbility* Ability = JobAbility(Job, Slot);
			if (!Ability || Ability->Buffs.empty() || Ability->Target == ETargetSide::Enemy)
			{
				continue;
			}
			const bool bOn = Ability->Kind == "passive" || Ability->Kind == "active_passive"
				|| (Ability->Kind == "toggle" && Toggled[Slot]);
			if (!bOn)
			{
				continue;
			}
			for (const FBuff& Buff : Ability->Buffs)
			{
				if (Buff.Stat == Which)
				{
					Value += Buff.Amount;
				}
			}
		}

		// Shred cuts the defences and Freeze multiplies them, so those scale what
		// everything else has added up to rather than adding to it.
		if (Which == EStat::AttDef || Which == EStat::MagDef)
		{
			Value = RoundToInt(Value * StatusProduct(&FStatusDef::DefenseFactor));
		}
		return Value;
	}

	bool FUnit::HasStatus(const std::string& StatusId) const
	{
		for (const FStatus& Status : Statuses)
		{
			if (Status.Id == StatusId)
			{
				return true;
			}
		}
		return false;
	}

	float FUnit::TgFactor() const
	{
		return StatusProduct(&FStatusDef::TgFactor);
	}

	float FUnit::MoveFactor() const
	{
		return StatusProduct(&FStatusDef::MoveFactor);
	}

	bool FUnit::ActsOnce() const
	{
		for (const FStatus& Status : Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bOneAction)
			{
				return true;
			}
		}
		return false;
	}

	bool FUnit::Flies() const
	{
		for (const FStatus& Status : Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bFly)
			{
				return true;
			}
		}
		return false;
	}

	bool FUnit::IsInvulnerable() const
	{
		for (const FStatus& Status : Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bInvulnerable)
			{
				return true;
			}
		}
		return false;
	}

	bool FUnit::IsImmune() const
	{
		for (const FStatus& Status : Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bImmune)
			{
				return true;
			}
		}
		return false;
	}

	bool FUnit::HasExtraTurn() const
	{
		for (const FStatus& Status : Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bExtraTurn)
			{
				return true;
			}
		}
		return false;
	}

	int FUnit::TauntedBy() const
	{
		for (const FStatus& Status : Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bTaunt)
			{
				return Status.By;
			}
		}
		return -1;
	}

	bool FUnit::IsRooted() const
	{
		for (const FStatus& Status : Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bNoMove)
			{
				return true;
			}
		}
		return false;
	}

	int FUnit::MissChance() const
	{
		int Total = 0;
		for (const FStatus& Status : Statuses)
		{
			if (const FStatusDef* Def = FindStatus(Status.Id))
			{
				Total += Def->MissPercent;
			}
		}
		return Total;
	}

	std::string FUnit::NoAbilitiesStatus() const
	{
		for (const FStatus& Status : Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bNoAbilities)
			{
				return Status.Id;
			}
		}
		return std::string();
	}

	std::string FUnit::NoOrdersStatus() const
	{
		for (const FStatus& Status : Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bNoOrders)
			{
				return Status.Id;
			}
		}
		return std::string();
	}

	float FUnit::StatusProduct(float FStatusDef::* Member) const
	{
		float Factor = 1.0f;
		for (const FStatus& Status : Statuses)
		{
			if (const FStatusDef* Def = FindStatus(Status.Id))
			{
				Factor *= Def->*Member;
			}
		}
		return Factor;
	}
}
