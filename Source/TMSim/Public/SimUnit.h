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
	struct FItemDef;
	struct FAbility;
	struct FMonsterInfo;

	/** What a neutral monster is doing (Docs/design/feat-neutral-camps.md 14). */
	enum class EMind : uint8_t
	{
		/** At home, or walking its route. */
		Resting,
		/** Set off: it shows it, and fights from its next turn. */
		Alert,
		Fighting,
		/** Nobody left to chase: walking home, where it mends. */
		Returning,
		/** Running: from anyone (skittish), or for a turn after being hit (docile). */
		Fleeing,
	};

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
		/**
		 * The name of the aura that keeps it topped up, or empty for a buff
		 * that was simply given. An aura finds its own buff by this to refresh
		 * it rather than stack another (game_state.gd:1305-1316). It is the
		 * ability's name, not its id, because that is what Godot keys it by.
		 */
		std::string Aura;
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

	/**
	 * A spell part-way out. It is simulation state rather than an animation,
	 * because everything about it is a rule: the gauge stops filling, the turn
	 * countdown does not, and where it lands is decided when it lands.
	 */
	struct FCast
	{
		int Slot = -1;
		/** Where it was aimed, used unless it is following someone. */
		FVec2 Target;
		/** The unit it follows, or -1 for a spot on the ground. */
		int FollowId = -1;
		int Ticks = 0;
		int Total = 0;
	};

	/**
	 * An ability that keeps working over the caster's next few turns, and takes
	 * each of them to do it.
	 */
	struct FChannel
	{
		int Slot = -1;
		FVec2 Target;
		int Turns = 0;
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
		 * Its last walk's waypoints, and where that walk began, so a viewer can
		 * show it going the way it was told rather than the shortest way. Not part
		 * of the rules: nothing reads it but the screen, and no checksum holds it.
		 */
		std::vector<FVec2> WalkVia;
		FVec2 WalkFrom;
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
		/** Turns left on each slot: the class's four, then its items' (AbilitySlots). */
		int Cooldowns[AbilitySlots] = { 0, 0, 0, 0, 0, 0, 0 };

		/** A spell part-way out, or Slot -1 for none. */
		FCast Casting;
		/** A channelled ability in progress, or Slot -1 for none. */
		FChannel Channeling;
		/** Which toggles are switched on, and which were flipped this turn. */
		bool Toggled[AbilitySlots] = { false, false, false, false, false, false, false };
		bool ToggledTurn[AbilitySlots] = { false, false, false, false, false, false, false };

		/**
		 * What it carries (SimItem.h): three open slots, each an item or null.
		 * Set before the battle starts (the setup screen's loadout), and later
		 * by picking items up. Points into the item registry, which outlives
		 * every battle.
		 */
		const FItemDef* Gear[3] = { nullptr, nullptr, nullptr };

		// ------------------------------------------ item effects (SimItem.h)

		/** The last damage it took, which Rewind gives back. */
		int LastHurt = 0;
		/** Once-a-battle items already spent: Rewind, First Strike, Phoenix Feather. */
		bool bRewindUsed = false;
		bool bFirstStrikeUsed = false;
		bool bPhoenixUsed = false;
		/** It didn't walk on its last turn (Anchor Stone), and wasn't seen as its turn began (Nightcloak). */
		bool bStillLastTurn = false;
		bool bUnseenAtStart = false;
		/** Gauge owed for kills this turn (Momentum Charm), in percent, paid when the turn ends. */
		int KillTgPercent = 0;

		// ------------------------- neutral monsters (Docs/design/feat-neutral-camps.md)
		// All at rest for a side's own units.

		/** A camp's monster: on team 2, enemy of both sides, never counted towards a win. */
		bool bMonster = false;
		/** Waiting off the board for its camp to wake, or gone until it does again. */
		bool bOffBoard = false;
		/** Its camp, by place in FBattle::Camps, or -1. */
		int Camp = -1;
		EMind Mind = EMind::Resting;
		/** Where it rests and goes back to. */
		FVec2 Home;
		/** Who last hurt it, and for how many more of its turns it holds that against them. */
		int Grudge = -1;
		int GrudgeTurns = 0;
		/** Turns more it runs (a docile one hit). */
		int FleeTurns = 0;
		/** Its next waypoint, walking a route. */
		int RouteStep = 0;
		/** A boss's phase: 0 its first abilities, then each FMonsterPhase in turn. Never goes back. */
		int Phase = 0;
		/** Hits from behind towards a stagger. */
		int Stagger = 0;
		/** Tamed: its own turns left on the side that tamed it, then back to team 2. */
		int TamedTurns = 0;

		// ------------------------------- the second set of statuses (feat-status-effects.md)

		/** Charmed: the side it belongs to while it fights for the other one, or -1. */
		int CharmedFrom = -1;
		/** Reraise caught its fall: ticks until it stands again, or 0. */
		int ReraiseTicks = 0;

		/** The side it belongs to, whoever it fights for just now (Charmed). */
		int HomeTeam() const { return CharmedFrom >= 0 ? CharmedFrom : Team; }

		// ------------------------------------------------------------ state

		bool IsCasting() const { return Casting.Slot >= 0; }
		bool IsChanneling() const { return Channeling.Slot >= 0; }

		bool IsAlive() const { return Hp > 0; }

		/**
		 * The ability in a slot: the class's four (a boss's for its phase), then
		 * one per item that gives one. Null for an empty slot.
		 */
		TMSIM_API const FAbility* Ability(int Slot) const;
		/** Its class's monster rules, or null for a class a side fields. */
		TMSIM_API const FMonsterInfo* MonsterInfo() const;
		bool IsKo() const { return Hp <= 0 && KoTicks > 0; }
		bool IsHustling() const { return bHustling; }

		/** The class's HP and what its items add. */
		int MaxHp() const { return Stats ? Stats->Get(EStat::Hp) + (HasItems() ? ItemStat(EStat::Hp) : 0) : 0; }

		// ------------------------------------------------------------ items
		// (SimItem.cpp). All zero for a unit carrying nothing, which is what
		// keeps every number exactly what it was before items.

		bool HasItems() const { return Gear[0] || Gear[1] || Gear[2]; }
		/** What its items add to a stat, held to the caps (Items::ChanceCap, Items::StepCap). */
		TMSIM_API int ItemStat(EStat Which) const;
		/** Flat power and percent its items add to its damage abilities of this scale (true: AttDef-resisted). */
		TMSIM_API int ItemDamageFlat(bool bAttScale) const;
		TMSIM_API int ItemDamagePercent(bool bAttScale) const;
		TMSIM_API int ItemHealFlat(bool bAttScale) const;
		TMSIM_API int ItemHealPercent(bool bAttScale) const;
		/** Percent faster its Turn Gauge fills. */
		TMSIM_API int ItemTgPercent() const;
		/** Extra levels it can climb in one step. */
		TMSIM_API int ItemJump() const;
		/** Whether it carries that item. */
		TMSIM_API bool Carries(const std::string& ItemId) const;

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

		/** Takes no damage at all while it lasts (Invulnerable). */
		TMSIM_API bool IsInvulnerable() const;

		/** Turns away anything harmful coming its way (Immunity). */
		TMSIM_API bool IsImmune() const;

		/** Goes again the moment this turn ends, and is spent doing so (Relentless). */
		TMSIM_API bool HasExtraTurn() const;

		/** Who it must attack while they are in reach, or -1 (Taunt). */
		TMSIM_API int TauntedBy() const;

	private:
		/** Every status flag of this kind multiplied together. */
		float StatusProduct(float FStatusDef::* Member) const;
	};
}
