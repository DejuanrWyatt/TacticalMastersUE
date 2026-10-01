#include "SimUnit.h"

#include "SimAbility.h"
#include "SimItem.h"

#include <algorithm>

namespace TMSim
{
	const FAbility* FUnit::Ability(int Slot) const
	{
		if (Slot >= ClassSlots && Slot < AbilitySlots)
		{
			const FItemDef* Item = Gear[Slot - ClassSlots];
			return Item && !Item->AbilityId.empty() ? FindAbility(Item->AbilityId) : nullptr;
		}
		// A boss's later phases bring abilities of their own. Everyone else
		// (Phase 0) has its class's four, exactly as before.
		if (Phase > 0 && Slot >= 0 && Slot < ClassSlots)
		{
			const FJobDef* Def = FindJob(Job);
			if (Def && Phase <= static_cast<int>(Def->Monster.Phases.size()))
			{
				return FindAbility(Def->Monster.Phases[static_cast<size_t>(Phase - 1)].AbilityIds[Slot]);
			}
		}
		return JobAbility(Job, Slot);
	}

	const FMonsterInfo* FUnit::MonsterInfo() const
	{
		const FJobDef* Def = FindJob(Job);
		return Def && Def->Monster.bMonster ? &Def->Monster : nullptr;
	}

	int FUnit::Stat(EStat Which) const
	{
		if (!Stats)
		{
			return 0;
		}

		int Value = Stats->Get(Which);
		// What it carries adds to the class's own, before buffs and statuses,
		// so a Shred or a Freeze scales items' defence with the rest. Held to
		// the floors a class file is (Speed and Move at least 1, Sight at least
		// 3), so a trade-off item can't take a unit to nothing. A unit with no
		// items skips this entirely: its numbers are exactly the old ones.
		if (HasItems())
		{
			Value += ItemStat(Which);
			// The items that only count sometimes (Docs/design/feat-neutral-camps.md 14).
			if ((Which == EStat::AttDef || Which == EStat::MagDef) && bStillLastTurn)
			{
				Value += GearSum(*this, &FItemDef::StillDefence);
			}
			if (Which == EStat::AEva && Stats && Hp > 0 && Hp < MaxHp() * Items::LowHealthShare)
			{
				Value += GearSum(*this, &FItemDef::LowHealthEvasion);
			}
			if ((Which == EStat::AEva || Which == EStat::MEva) && bUnseenAtStart)
			{
				Value += GearSum(*this, &FItemDef::UnseenEvasion);
			}
			const int Floor = Which == EStat::Speed || Which == EStat::Move ? 1 : Which == EStat::Sight ? 3 : 0;
			Value = std::max(Floor, Value);
		}
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
		for (int Slot = 0; Slot < ClassSlots; ++Slot)
		{
			const FAbility* Held = Ability(Slot);
			if (!Held || Held->Buffs.empty() || Held->Target == ETargetSide::Enemy)
			{
				continue;
			}
			const bool bOn = Held->Kind == "passive" || Held->Kind == "active_passive"
				|| (Held->Kind == "toggle" && Toggled[Slot]);
			if (!bOn)
			{
				continue;
			}
			for (const FBuff& Buff : Held->Buffs)
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
