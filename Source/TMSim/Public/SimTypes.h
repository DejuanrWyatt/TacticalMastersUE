// The vocabulary the battle rules are written in: stats, statuses, and the
// handful of numbers that set the pace of a fight.
//
// Plain C++ on purpose. The rules must run headless so a battle can be replayed
// and checked against the Godot version tick for tick, and nothing here should
// need the editor to be open.

#pragma once

#include <cstdint>
#include <string>

// Unreal builds each module as its own library, so anything used from another
// module has to be exported. UBT defines TMSIM_API when it compiles this; the
// fallback is for the standalone tests, which build these files with nothing
// but a compiler.
#ifndef TMSIM_API
#define TMSIM_API
#endif

namespace TMSim
{
	// --------------------------------------------------------------- vectors

	/**
	 * A spot on the ground, in meters. Single precision on purpose: Godot's
	 * Vector2 is float, and the flank test turns on the sign of a dot product,
	 * so widening it here could take a different branch from the original.
	 */
	struct FVec2
	{
		float X = 0.0f;
		float Y = 0.0f;

		FVec2() = default;
		FVec2(float InX, float InY) : X(InX), Y(InY) {}

		FVec2 operator-(const FVec2& Other) const { return FVec2(X - Other.X, Y - Other.Y); }
		TMSIM_API float Length() const;
		TMSIM_API FVec2 Normalized() const;
		float Dot(const FVec2& Other) const { return X * Other.X + Y * Other.Y; }
		float DistanceTo(const FVec2& Other) const { return (*this - Other).Length(); }
	};

	// ---------------------------------------------------------------- stats

	enum class EStat : uint8_t
	{
		Hp, AttDef, MagDef, AEva, MEva, Crit, Speed, Move, Patience, Sight, Count
	};

	inline constexpr int StatCount = static_cast<int>(EStat::Count);

	/** The name a class file uses for a stat, for reading data and for messages. */
	TMSIM_API const char* StatName(EStat Stat);

	/** EStat::Count if the name belongs to no stat (a class file may carry its own). */
	TMSIM_API EStat StatFromName(const std::string& Name);

	// ------------------------------------------------------------- statuses

	/**
	 * What a status does. A status is data: it names the flags it needs and the
	 * rules read them, so adding one is a line in the table rather than a new
	 * branch in the middle of a fight.
	 *
	 * Ported from Jobs.STATUSES in the Godot version, which stays the source of
	 * truth until the rules live here. Only the parts the clock needs are acted
	 * on so far -- TgFactor and bNoOrders -- but the whole table is here so the
	 * two versions can be compared without translating as you read.
	 */
	struct FStatusDef
	{
		const char* Id = "";
		const char* Name = "";
		const char* Tag = "";

		/** Health gained (positive) or lost (negative) each turn, as a share of max HP. */
		float PerTurn = 0.0f;
		/** Multiplies how fast the Turn Gauge fills. Slow halves it. */
		float TgFactor = 1.0f;
		/** Multiplies how far the unit walks. */
		float MoveFactor = 1.0f;
		/** Multiplies AttDef and MagDef. Shred cuts them, Freeze multiplies them. */
		float DefenseFactor = 1.0f;
		/** Added to the chance the unit's own attacks are evaded. */
		int MissPercent = 0;

		bool bHarmful = false;      /** Immunity clears these and turns them away. */
		bool bNoOrders = false;     /** Takes the unit's turn away entirely. */
		bool bNoMove = false;
		bool bNoAbilities = false;
		bool bInterrupt = false;    /** Takes the turn a unit is caught in. */
		bool bAbsorbs = false;      /** Soaks damage before HP. */
		bool bTaunt = false;
		bool bOneAction = false;    /** May walk or act, not both. */
		bool bExtraTurn = false;    /** Comes straight back round. */
		bool bFly = false;
		bool bImmune = false;
		bool bInvulnerable = false;
		bool bCleanse = false;
		bool bWakesOnDamage = false;
		bool bDoom = false;         /** Falls when the count runs out. */
	};

	/** Every status, in the order the Godot table lists them. */
	TMSIM_API const FStatusDef* AllStatuses(int& OutCount);

	/** Null if no status has that id. */
	TMSIM_API const FStatusDef* FindStatus(const std::string& StatusId);

	// ----------------------------------------------------------------- time

	namespace Pace
	{
		/** The simulation steps this many times a second. */
		inline constexpr int TicksPerSecond = 10;
		/** A full Turn Gauge. Fine-grained so half speed stays whole for any Speed. */
		inline constexpr int TgMax = 4000;
		/** Gauge gained per tick per point of Speed. */
		inline constexpr int TgPerSpeed = 2;
		/** Head start at the beginning of a battle, per point of Speed. */
		inline constexpr int StartTgPerSpeed = 320;
		/** Gauge kept after a turn that only moved or only acted, and neither. */
		inline constexpr int TgKeepOne = 800;
		inline constexpr int TgKeepNone = 1600;
	}

	/**
	 * The rule numbers a battle is played with. Defaults match GameState.TUNING;
	 * a battle carries its own copy so a match or a replay can be played with
	 * the numbers it was recorded under.
	 */
	struct FTuning  // doubles: GDScript has no 32-bit float
	{
		double SpeedMultiplier = 1.0;
		double ClockBase = 8.0;
		double PatienceMultiplier = 2.0;
		/** Percent faster the gauge fills for a unit that held its ability back. */
		double HustleBonus = 25.0;

		/** What every hit is multiplied by once defence has been taken off. */
		double DamageMultiplier = 0.5;
		double HealMultiplier = 1.0;
		/** Damage added per height level above the target, taken off below. */
		double HeightBonus = 0.1;
		double SideBonus = 1.1;
		double BackBonus = 1.25;
		/** What a critical hit multiplies damage by. */
		double CritMultiplier = 1.5;
		double EvadeMultiplier = 1.0;
		double CritChanceMultiplier = 1.0;
	};

	namespace Combat
	{
		/** An ability's power is the damage; this is the scale it is read at. */
		inline constexpr double DamageScale = 1.0;
		inline constexpr double HealScale = 1.5;
		/** Height counts for at most this many levels either way. */
		inline constexpr int MaxHeightLevels = 3;
		/** However good the defence, a hit still lands for this much. */
		inline constexpr int MinimumDamage = 1;
	}

	/** Godot's roundi(): halves go away from zero, which is what the rules assume. */
	TMSIM_API int RoundToInt(double Value);
}
