// One unit on the battlefield.
//
// Ported from unit.gd. The fields the clock needs are here and working; the
// rest of a unit's turn -- casting, channelling, toggles, facing -- is named
// where it belongs so the shape matches the original, and filled in as each
// part of the rules is ported.

#pragma once

#include "SimTypes.h"

#include <string>
#include <vector>

namespace TMSim
{
	/** A class's stats. Read from the class files; the six built-ins are code. */
	struct FJobStats
	{
		int Values[StatCount] = { 0 };

		int Get(EStat Stat) const { return Values[static_cast<int>(Stat)]; }
		void Set(EStat Stat, int Value) { Values[static_cast<int>(Stat)] = Value; }
	};

	/** A timed change to one stat. */
	struct FBuff
	{
		EStat Stat = EStat::Count;
		int Amount = 0;
		int Turns = 0;
	};

	/** A status on a unit: which one, and how many of its own turns are left. */
	struct FStatus
	{
		std::string Id;
		int Turns = 0;
		/** What a Shield or Barrier can still soak up. */
		int Amount = 0;
		/** Who taunted it, or -1. */
		int By = -1;
	};

	struct FUnit
	{
		int Id = 0;
		int Team = 0;
		std::string Job;
		/** The class's own numbers. Shared by every unit of that class. */
		const FJobStats* Stats = nullptr;

		/** Where it stands, in meters. Always a navigation node centre. */
		FVec2 Pos;
		/**
		 * The way it faces: where it last walked or aimed. Hits from the side or
		 * from behind land harder, so this is part of the rules, not decoration.
		 */
		FVec2 Facing = FVec2(0.0f, 1.0f);

		int Hp = 0;
		/** Ready to act once this reaches Pace::TgMax. */
		int Tg = 0;
		bool bReady = false;
		/** While ready: ticks left before the turn is lost. */
		int Clock = 0;
		/** Up by one every time the unit becomes ready; orders carry it. */
		int Serial = 0;
		int Ult = 0;
		bool bMoved = false;
		bool bActed = false;
		/**
		 * Ended a turn without using an ability, so the gauge fills faster until
		 * the next one. Cleared when that turn comes round.
		 */
		bool bHustling = false;
		/** While knocked out: ticks left before the unit is gone for good. */
		int KoTicks = 0;
		/** Turns begun since it last took damage; it mends once left alone. */
		int UnharmedTurns = 0;

		std::vector<FStatus> Statuses;
		std::vector<FBuff> Buffs;
		int Cooldowns[4] = { 0, 0, 0, 0 };

		// ------------------------------------------------------------ state

		bool IsAlive() const { return Hp > 0; }
		bool IsKo() const { return Hp <= 0 && KoTicks > 0; }
		bool IsHustling() const { return bHustling; }

		int MaxHp() const { return Stats ? Stats->Get(EStat::Hp) : 0; }

		/** A stat with its buffs, and the statuses that scale the defences. */
		TMSIM_API int Stat(EStat Which) const;

		TMSIM_API bool HasStatus(const std::string& StatusId) const;

		/** How fast the gauge fills compared with normal: Slow halves it. */
		TMSIM_API float TgFactor() const;

		/** Added to the chance this unit's own attacks are evaded (Blind). */
		TMSIM_API int MissChance() const;

		/** How far it walks compared with its Move stat (Crippled, Stride). */
		TMSIM_API float MoveFactor() const;

		/** May walk or act on its turn, but not both (Knockdown). */
		TMSIM_API bool ActsOnce() const;

		/** Crosses any height, and melee cannot reach it (Fly). */
		TMSIM_API bool Flies() const;

		/** Cannot walk at all (Root, Freeze). */
		TMSIM_API bool IsRooted() const;

		/** The status taking this unit's orders away, or "" if it can act. */
		TMSIM_API std::string NoOrdersStatus() const;
		bool IsStunned() const { return !NoOrdersStatus().empty(); }

		/** The status sealing its abilities, or empty. It can still walk. */
		TMSIM_API std::string NoAbilitiesStatus() const;
		bool IsSilenced() const { return !NoAbilitiesStatus().empty(); }

	private:
		/** Every status flag of this kind multiplied together. */
		float StatusProduct(float FStatusDef::* Member) const;
	};
}
