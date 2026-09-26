#include "SimUnit.h"

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

		// Always-on abilities add to a stat too. Not ported yet: that needs the
		// ability table, which arrives with the rest of the rules.

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
